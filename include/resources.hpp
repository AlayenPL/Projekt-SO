#pragma once

#include <condition_variable>
#include <mutex>
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

// Uwaga dot. VIP:
// - Bridge (A): VIP NIE omija kolejki (w specyfikacji VIP czeka jak inni).
// - Tower (B) i Ferry (C): VIP omija kolejkę -> implementujemy kolejkę z priorytetem VIP.
//
// Dodatkowo wprowadzamy prostą "fairness": po wpuszczeniu VIP określoną liczbę razy z rzędu,
// jeśli czekają normalni, wpuszczamy normalnego, by uniknąć głodzenia.

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

    std::mutex mu;
    std::condition_variable cv;

    int inside = 0;
    int waiting_vip = 0;
    int waiting_norm = 0;

    int vip_streak = 0;
    static constexpr int VIP_BURST = 5; // po tylu VIP z rzędu wpuszczamy normalnego (jeśli czeka)

    Tower(int cap, Logger& log);

    void enter(int tourist_id, bool vip);
    void leave(int tourist_id);
};

struct Ferry {
    int cap;
    Logger& log;

    std::mutex mu;
    std::condition_variable cv;

    int onboard = 0;
    int waiting_vip = 0;
    int waiting_norm = 0;

    int vip_streak = 0;
    static constexpr int VIP_BURST = 5;

    Ferry(int cap, Logger& log);

    void board(int tourist_id, bool vip, Direction d);
    void unboard(int tourist_id);
};
