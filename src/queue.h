/*
Name: Ajibola Ganiyu
R#: R11788396
Class: Operating Systems (CS4352-001)
File: queue.h
Description: A generic thread-safe queue that safely passes Person and Assignment structs between the three concurrent threads
*/

#ifndef QUEUE_H
#define QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>

// Thread-safe queue used as the handoff point between the three pipeline threads.
// The input thread pushes Persons in, the scheduler thread pops them out and pushes
// Assignments into a second instance, and the output thread pops those out to send.
template <typename T>
class TSQueue {
    std::queue<T> q;
    std::mutex m;
    std::condition_variable cv;
    bool done = false;  // set by shutdown() to signal that no more items will ever arrive

public:
    void push(T v) {
        {
            // Lock only long enough to insert — the inner scope releases the lock
            // before notify so the woken consumer can acquire it immediately.
            std::lock_guard<std::mutex> lk(m);
            q.push(std::move(v));
        }
        cv.notify_one();  // wake exactly one waiting consumer; no thundering herd
    }

    // Blocks the calling thread until an item is available or the queue is shut down.
    // Returns true and fills `out` if an item was retrieved, false if the queue is
    // empty and done — the caller should treat false as "exit your loop."
    bool pop(T &out) {
        std::unique_lock<std::mutex> lk(m);
        // Sleep until there is something to consume OR shutdown has been signaled.
        // Using a predicate prevents spurious wakeups from causing an empty-pop.
        cv.wait(lk, [&] { return !q.empty() || done; });
        if (q.empty()) return false;  // woken by shutdown(), nothing left to process

        out = std::move(q.front());
        q.pop();
        return true;
    }

    // Called by the input thread when the simulation ends. Sets done under the lock
    // so the flag write is visible to all threads, then wakes every blocked consumer
    // so they can evaluate the exit condition and return false from pop().
    // notify_all() is required here — notify_one() would leave other blocked threads
    // sleeping forever if more than one consumer is waiting at shutdown time.
    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(m);
            done = true;
        }
        cv.notify_all();
    }
};

#endif
