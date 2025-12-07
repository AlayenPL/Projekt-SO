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

// VIP:
// - Bridge (A): VIP NIE omija kolejki.
// - Tower (B) i Ferry (C): VIP omija kolejkę + fairness.

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

    int inside = 0;          // liczba osób w środku
    int waiting_vip = 0;     // liczba osób VIP czekających
    int waiting_norm = 0;    // liczba osób normalnych czekających

    int vip_streak = 0;
    static constexpr int VIP_BURST = 5;

    Tower(int cap, Logger& log);

    // per-osoba (VIP path / fallback)
    void enter(int tourist_id, bool vip);
    void leave(int tourist_id);

    // grupowo (zajęcie k miejsc naraz)
    void enter_group(int group_id, int k, bool vip_like);
    void leave_group(int group_id, int k);
};

struct Ferry {
    int cap;
    Logger& log;

    std::mutex mu;
    std::condition_variable cv;

    int onboard = 0;         // liczba osób na pokładzie
    int waiting_vip = 0;
    int waiting_norm = 0;

    int vip_streak = 0;
    static constexpr int VIP_BURST = 5;

    Ferry(int cap, Logger& log);

    // per-osoba (VIP path / fallback)
    void board(int tourist_id, bool vip, Direction d);
    void unboard(int tourist_id);

    // grupowo (zajęcie k miejsc naraz)
    void board_group(int group_id, int k, bool vip_like, Direction d);
    void unboard_group(int group_id, int k);
};
