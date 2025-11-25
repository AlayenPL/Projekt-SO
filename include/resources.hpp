#pragma once

#include <condition_variable>
#include <mutex>
#include <semaphore>
#include <string>

#include "logger.hpp"

enum class Direction { NONE = 0, FORWARD = 1, BACKWARD = 2 };

static inline const char* dir_str(Direction d) {
    switch (d) {
        case Direction::NONE: return "NONE";
        case Direction::FORWARD: return "FWD";
        case Direction::BACKWARD: return "BWD";
    }
    return "?";
}

struct Bridge {
    int cap;
    Logger& log;

    std::mutex mu;
    std::condition_variable cv;
    Direction dir = Direction::NONE;
    int on_bridge = 0;

    Bridge(int cap, Logger& log);

    void enter(int tourist_id, Direction d);
    void leave(int tourist_id);
};

struct Tower {
    int cap;
    Logger& log;

    std::counting_semaphore<> sem;

    Tower(int cap, Logger& log);

    void enter(int tourist_id, bool vip);
    void leave(int tourist_id);
};

struct Ferry {
    int cap;
    Logger& log;

    std::counting_semaphore<> sem;

    Ferry(int cap, Logger& log);

    void board(int tourist_id, bool vip, Direction d);
    void unboard(int tourist_id);
};
