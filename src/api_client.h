/*
Name: Ajibola Ganiyu
R#: R11788396
Class: Operating Systems (CS4352-001)
File: api_client.h
Description: Declares the two HTTP primitives (http_get, http_put) that the rest
of the project calls to talk to the Flask simulation server
*/

#ifndef API_CLIENT_H
#define API_CLIENT_H
#include <string>


// Thin HTTP interface used by all three pipeline threads to communicate with
// the Flask simulation server running on localhost. Implementation is in api_client.cpp.

// Sends a GET request to `url` and returns the full response body as a string.
// Used by the input thread to poll /Simulation/check and /NextInput,
// and by the scheduler thread to query /ElevatorStatus/<id>.
std::string http_get(const std::string& url);

// Sends a PUT request to `url` and returns the response body.
// Used by main() to trigger /Simulation/start and by the output thread
// to submit assignments via /AddPersonToElevator/<personID>/<elevatorID>.
std::string http_put(const std::string& url);
#endif
