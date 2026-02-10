CXX=g++
CXXFLAGS=-std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS=-lstdc++fs
INCLUDES=-Iinclude
SRCS=src/main.cpp src/config.cpp src/ipc_sem.cpp src/ipc_shm.cpp src/ipc_msg.cpp src/logger.cpp src/resources.cpp src/park.cpp src/tourist.cpp
OUT=sim

all:
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(OUT) $(LDFLAGS)

run:
	./$(OUT) --tourists=30 --P=2 --M=5 --X1=3 --X2=8 --X3=7

evac:
	./$(OUT) --tourists=30 --P=2 --M=5 --X1=3 --X2=8 --X3=7

clean:
	rm -f $(OUT)
