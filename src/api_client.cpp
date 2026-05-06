/*
Name: Ajibola Ganiyu
R#: R11788396
Class: Operating Systems (CS4352-001)
File: api_client.cpp
Description: Implements the two HTTP functions using libcurl, buffering the server's response into
a heap-grown string that the scheduler and input threads read back as plain text
*/

#include "api_client.h"
#include <curl/curl.h>
#include <iostream>
#include <cstring>

// Holds the raw HTTP response bytes as they arrive from libcurl.
// libcurl delivers response data in chunks, so the buffer must grow dynamically.
struct MemoryStruct {
    char *memory;
    size_t size;
};

// libcurl calls this function each time a new chunk of response data arrives.
// We realloc the buffer to fit the new chunk, copy it in, and null-terminate
// so the buffer can be safely treated as a C string when the request is done.
// Returning realsize tells libcurl the chunk was fully consumed; returning 0
// signals an error and causes curl_easy_perform to abort.
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) return 0;  // realloc failed — signal error to libcurl
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;  // null-terminate after every chunk
    return realsize;
}

// Single internal function for both GET and PUT. The is_put flag switches the
// HTTP method; everything else — URL, response buffering, cleanup — is identical,
// so there is no reason to duplicate the curl setup code.
std::string perform_request(const std::string& url, bool is_put) {
    CURL *curl_handle = curl_easy_init();

    // Start with a 1-byte allocation so realloc in the callback always has a
    // valid pointer to work with rather than having to handle the NULL case.
    struct MemoryStruct chunk;
    chunk.memory = (char *)malloc(1);
    chunk.size = 0;

    std::string response = "";
    if(curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
        // Route all response bytes through our callback instead of stdout.
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        if (is_put) {
            // CURLOPT_CUSTOMREQUEST overrides the method without requiring a body,
            // which is all the simulation API needs for its PUT endpoints.
            curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PUT");
        }
        CURLcode res = curl_easy_perform(curl_handle);
        if(res == CURLE_OK) {
            response = std::string(chunk.memory);
        }
        curl_easy_cleanup(curl_handle);  // release curl handle resources
    }
    free(chunk.memory);  // always free the response buffer regardless of success
    return response;
}

// Public interface
std::string http_get(const std::string& url) { return perform_request(url, false); }
std::string http_put(const std::string& url) { return perform_request(url, true); }
