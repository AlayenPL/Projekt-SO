CXX := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pthread -Iinclude
LDFLAGS := -pthread

BIN := park_sim
SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:.cpp=.o)

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all clean
