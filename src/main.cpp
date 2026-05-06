/*
Name: Ajibola Ganiyu
R#: R11788396
Class: Operating Systems (CS4352-001)
File: main.cpp
Description: The entry point of the scheduler: spins up three threads (input, scheduler, output),
parses the building file, kicks off the simulation, and joins everything on completion
*/

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <atomic>
#include "queue.h"
#include "api_client.h"
#include <algorithm>

// A passenger retrieved from /NextInput — only the fields the scheduler needs.
struct Person {
    std::string id;
    int start_floor;
    int end_floor;
};

// The result of a scheduling decision: which person goes to which elevator.
// Passed from the scheduler thread to the output thread via outputQueue.
struct Assignment {
    std::string person_id;
    std::string elev_id;
};

// Static elevator configuration parsed once from the .bldg file at startup.
// The scheduler thread reads this on every decision; it is never written to
// after parse_building() returns, so no mutex is needed.
struct ElevatorConfig {
    std::string name;
    int lowest_floor;
    int highest_floor;
    int start_floor;
    int capacity;
};

// BASE_URL is set once in main() and then read-only across all threads.
std::string BASE_URL;
// Atomic flag so the input thread can signal completion without a mutex.
// When true, all threads are winding down.
std::atomic<bool> sim_complete(false);
// Pipeline queues: input thread -> inputQueue -> scheduler thread -> outputQueue -> output thread.
TSQueue<Person> inputQueue;
TSQueue<Assignment> outputQueue;
// Populated by parse_building() before threads launch; read-only after that.
std::vector<ElevatorConfig> building_elevators;

// General-purpose string splitter used to parse both .bldg file lines (tab-delimited)
// and API response strings (pipe-delimited).
std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) elems.push_back(item);
    return elems;
}

// Reads the .bldg file and fills building_elevators with one ElevatorConfig per line.
// Called once before any threads launch, so no synchronization is needed.
// Lines that are blank or don't have exactly 5 tab-separated fields are silently skipped.
void parse_building(const std::string& filepath) {
    std::ifstream file(filepath);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto parts = split(line, '\t');
        if (parts.size() == 5) {
            building_elevators.push_back({parts[0], std::stoi(parts[1]), std::stoi(parts[2]), std::stoi(parts[3]), std::stoi(parts[4])});
        }
    }
}

// Producer thread: polls the simulation API and feeds passengers into inputQueue.
void input_thread_func() {
    while (!sim_complete.load()) {
        // Check completion before fetching the next passenger so we don't push
        // new work into the queue after the simulation has already finished.
        std::string status = http_get(BASE_URL + "/Simulation/check");
        if (status.find("Simulation is complete.") != std::string::npos) {
            sim_complete.store(true);
            // Shut down both queues so the scheduler and output threads
            // wake from pop() and exit their loops rather than blocking forever.
            inputQueue.shutdown();
            outputQueue.shutdown();
            break;
        }

        std::string person_resp = http_get(BASE_URL + "/NextInput");
        if (person_resp == "NONE" || person_resp.empty()) {
            // No passenger ready yet — sleep rather than hammering the API in a
            // tight loop, which would starve the simulation process of CPU time.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } else {
            // Response format: "PersonID | StartFloor | EndFloor"
            // Strip spaces first so the pipe split produces clean tokens.
            person_resp.erase(std::remove(person_resp.begin(), person_resp.end(), ' '), person_resp.end());
            auto parts = split(person_resp, '|');
            if (parts.size() == 3) {
                inputQueue.push({parts[0], std::stoi(parts[1]), std::stoi(parts[2])});
            }
        }
    }
}

// Consumer/producer thread: applies the Nearest-Idle / Best-Fit algorithm to assign
// each passenger to an elevator, then forwards the decision to the output thread.
void scheduler_thread_func() {
    Person p;
    // pop() blocks until a Person is available or the queue shuts down.
    // Returns false on shutdown, which exits the loop cleanly.
    while (inputQueue.pop(p)) {
        std::string best_elev = "";
        int best_score = 999999;  // sentinel; any real distance will be smaller

        // Nearest-Idle / Best-Fit: find the closest elevator with capacity
        // whose service range covers both the passenger's start and end floor.
        for (const auto& elev : building_elevators) {
            // Range check first — skip elevators that can never serve this passenger
            // regardless of where they currently are, avoiding a wasted GET request.
            if (p.start_floor < elev.lowest_floor || p.start_floor > elev.highest_floor ||
                p.end_floor < elev.lowest_floor || p.end_floor > elev.highest_floor) {
                continue;
            }

            // Query live elevator state. Response format: "ID|Floor|Direction|Occupancy|RemainingCapacity"
            std::string stat_resp = http_get(BASE_URL + "/ElevatorStatus/" + elev.name);
            stat_resp.erase(std::remove(stat_resp.begin(), stat_resp.end(), ' '), stat_resp.end());
            auto sparts = split(stat_resp, '|');

            if (sparts.size() == 5) {
                int curr_floor = std::stoi(sparts[1]);
                int cap_left = std::stoi(sparts[4]);

                // Only consider elevators with remaining capacity.
                // Score is absolute floor distance — lower is better.
                if (cap_left > 0) {
                    int score = std::abs(curr_floor - p.start_floor);
                    if (score < best_score) {
                        best_score = score;
                        best_elev = elev.name;
                    }
                }
            }
        }

        if (!best_elev.empty()) {
            outputQueue.push({p.id, best_elev});
        } else {
            // All eligible elevators are currently full. Push the passenger back
            // and sleep briefly to give elevators time to drop off passengers
            // before we retry, rather than spinning and flooding /ElevatorStatus.
            inputQueue.push(p);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

// Consumer thread: drains the output queue by firing a PUT request for each assignment.
// Deliberately kept simple — all routing logic lives in the scheduler thread.
void output_thread_func() {
    Assignment a;
    while (outputQueue.pop(a)) {
        http_put(BASE_URL + "/AddPersonToElevator/" + a.person_id + "/" + a.elev_id);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: ./scheduler_os <path_to_building_file> <port_number>\n";
        return 1;
    }

    std::string bldg_file = argv[1];
    std::string port = argv[2];
    BASE_URL = "http://127.0.0.1:" + port;

    // Parse the building config before launching threads so building_elevators
    // is fully populated and read-only by the time the scheduler thread starts.
    parse_building(bldg_file);

    // Tell the simulation server to begin advancing timesteps.
    http_put(BASE_URL + "/Simulation/start");

    // Launch all three pipeline threads. Order doesn't matter for correctness —
    // each thread blocks on its input queue until data arrives.
    std::thread t1(input_thread_func);
    std::thread t2(scheduler_thread_func);
    std::thread t3(output_thread_func);

    // Block until all threads exit. The input thread drives termination by calling
    // shutdown() on both queues, which causes t2 and t3 to drain and return.
    t1.join();
    t2.join();
    t3.join();

    return 0;
}
