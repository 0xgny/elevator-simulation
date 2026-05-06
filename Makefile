CXX = g++
CXXFLAGS = -O2 -std=c++17 -pthread -Wall
LDLIBS = -lcurl -lpthread

# picks up main.cpp and api_client.cpp automatically
SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:.cpp=.o)

scheduler_os: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f src/*.o scheduler_os
