CXX=g++
CXXFLAGS=-std=c++17 -Wall -Wextra -O2
INCLUDES=-Iinclude
SRCS=src/main.cpp src/config.cpp src/ipc_sem.cpp src/ipc_shm.cpp src/ipc_msg.cpp
OUT=sim

all:
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(OUT)

run:
	./$(OUT) --tourists=8 --guides=1 --capacity=2

evac:
	./$(OUT) --tourists=30 --guides=1 --capacity=3 & echo $$! > /tmp/sim.pid; sleep 1; kill -USR1 $$(cat /tmp/sim.pid)

clean:
	rm -f $(OUT)
