# Elevator Simulation — Design Document

## Scheduling Strategy

For this project, I implemented the **Nearest-Idle / Best-Fit** strategy to manage passenger
routing. When a passenger request is retrieved from the input queue, the scheduler iterates
through all available elevators parsed from the static building configuration file to verify
range eligibility. If an elevator's service floors encompass both the passenger's starting and
target floors, the algorithm queries the HTTP API for the elevator's real-time status. The
elevator is scored based on the absolute floor distance between its current location and
the passenger's start floor. The passenger is ultimately assigned to the eligible elevator with
the lowest absolute distance score that also possesses remaining capacity. I chose this
algorithm because it provides a highly deterministic, reliable approach that effectively
reduces wait times by minimizing redundant elevator travel, successfully clearing the
baseline timestep requirements without introducing excessive computational overhead.


## Design Decisions

The application architecture is strictly designed around a **three-thread producer-consumer
model** to manage asynchronous HTTP communication efficiently:

- **Input Thread** — serves as the producer, continuously polling the `/NextInput` endpoint
  and placing retrieved passengers into a thread-safe Input Queue.
- **Scheduler Thread** — bridges the gap by pulling from the Input Queue, executing the
  routing algorithm, and placing assignments into a thread-safe Output Queue.
- **Output Thread** — consumes these assignments by sending HTTP `PUT` requests to the
  simulation.

Synchronization between these concurrent threads is handled via a generic `TSQueue`
template class backed by standard `std::mutex` and `std::condition_variable` primitives.
All HTTP communication is handled using standard `libcurl` wrappers for `GET` and `PUT`
requests. To adhere to proper project structure, all source code and headers were organized
into a `src/` directory, compiled via a root-level `Makefile`.


## Testing and Validation

Testing was conducted iteratively, beginning with verifying the `libcurl` HTTP wrappers and
the `.bldg` file parser. During the build phase, I encountered and resolved several
compilation and linking issues, including an undefined reference to `main` caused by `make`
failing to locate the source files before they were properly grouped into the `src/` directory.
I also resolved scoping errors by explicitly including standard C++ headers like `<cstring>`
for memory allocation in the API client and `<algorithm>` to resolve conflicts with the
`std::remove` string manipulation function.

Once compiled, I verified thread stability by running the system against standard test
building configurations more than ten consecutive times, ensuring the resulting timestep
counts did not wildly fluctuate, which proved the absence of race conditions. I also tested
against edge cases like elevators with different, non-overlapping floor ranges. A primary
synchronization bug involved deadlocking upon shutdown; this was remedied by
implementing a unified `done` flag in the `Queue` class alongside a `notify_all()` call,
waking all hanging consumer threads to gracefully exit when the `/Simulation/check`
completion signal was detected.


## Performance Considerations

To minimize total timesteps within the simulation while maintaining system stability,
several tradeoffs were made:

- A **50 ms sleep timeout** was implemented in the Input Thread whenever the API returned
  `"NONE"`. While this slightly delays input processing by milliseconds, it drastically
  improves overall OS performance by preventing a busy-wait cycle that would otherwise
  consume CPU resources and choke the locally running simulation process.
- The choice to use an **absolute-distance metric** trades perfect theoretical optimization
  for raw decision speed. By avoiding the overhead of maintaining a complex local
  prediction model of elevator states, the scheduler thread can make fast routing
  decisions and continuously feed the output queue, preventing bottlenecks and easily
  clearing the 110% baseline limit.


## Limitations and Improvements

The most notable limitation of the Nearest-Idle / Best-Fit algorithm is its **lack of
directional awareness**. In scenarios with high passenger density, an elevator moving
*away* from a passenger might be assigned to them simply because it is currently physically
closer than an elevator moving *toward* them from further away.

Given more time, I would pursue the following improvements:

1. **LOOK/SCAN algorithm** — calculate not just distance but vector trajectory, preferring
   elevators already moving in the passenger's direction to mimic classic elevator
   dispatching.
2. **Localized caching layer** — cache elevator capacities and predicted loads to reduce
   the volume of synchronous `GET` requests hitting the `/ElevatorStatus` endpoint during
   high-traffic surges.
