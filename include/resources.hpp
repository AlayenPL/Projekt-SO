#pragma once

#include <condition_variable>
#include <mutex>
#include <string>

#include "logger.hpp"

enum class Direction { NONE = 0, FORWARD = 1, BACKWARD = 2 };

/**
 * @brief Convert Direction enum to short string.
 */
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

    /**
     * @brief Construct bridge monitor with capacity and logger.
     */
    Bridge(int cap, Logger& log);

    /**
     * @brief Enter the bridge, blocking until direction and capacity allow.
     * @param tourist_id id for logging
     * @param d requested direction
     */
    void enter(int tourist_id, Direction d);
    /**
     * @brief Leave the bridge and release capacity; resets direction when empty.
     */
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

    /**
     * @brief Construct tower monitor with capacity and logger.
     */
    Tower(int cap, Logger& log);

    // per-osoba (VIP path / fallback)
    /**
     * @brief Enter tower as single visitor (handles VIP fairness).
     */
    void enter(int tourist_id, bool vip);
    /**
     * @brief Leave tower as single visitor.
     */
    void leave(int tourist_id);

    // grupowo (zajęcie k miejsc naraz)
    /**
     * @brief Enter tower as a group occupying k slots.
     */
    void enter_group(int group_id, int k, bool vip_like);
    /**
     * @brief Leave tower as a group releasing k slots.
     */
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

    /**
     * @brief Construct ferry monitor with capacity and logger.
     */
    Ferry(int cap, Logger& log);

    // per-osoba (VIP path / fallback)
    /**
     * @brief Board ferry as single visitor with direction and VIP fairness.
     */
    void board(int tourist_id, bool vip, Direction d);
    /**
     * @brief Unboard ferry as single visitor.
     */
    void unboard(int tourist_id);

    // grupowo (zajęcie k miejsc naraz)
    /**
     * @brief Board ferry as a group occupying k slots.
     */
    void board_group(int group_id, int k, bool vip_like, Direction d);
    /**
     * @brief Unboard ferry as a group releasing k slots.
     */
    void unboard_group(int group_id, int k);
};
