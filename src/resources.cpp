#include "resources.hpp"

#include <sstream>

// ------------------------- BRIDGE (A) -------------------------

/**
 * @brief Initialize bridge monitor with capacity and logger.
 */
Bridge::Bridge(int cap_, Logger& log_) : cap(cap_), log(log_) {}

/**
 * @brief Enter bridge respecting direction and capacity constraints.
 */
void Bridge::enter(int tourist_id, Direction d) {
    std::unique_lock<std::mutex> lk(mu);
    cv.wait(lk, [&]{
        bool dir_ok = (dir == Direction::NONE || dir == d);
        bool cap_ok = (on_bridge < cap);
        return dir_ok && cap_ok;
    });

    if (dir == Direction::NONE) {
        dir = d;
        std::ostringstream oss;
        oss << "BRIDGE_DIR_SET dir=" << dir_str(dir);
        log.log_ts("BRIDGE", oss.str());
    }

    ++on_bridge;
    {
        std::ostringstream oss;
        oss << "ENTER id=" << tourist_id << " dir=" << dir_str(d)
            << " occ=" << on_bridge << "/" << cap;
        log.log_ts("BRIDGE", oss.str());
    }

    lk.unlock();
    cv.notify_all();
}

/**
 * @brief Leave bridge; clears direction when last leaves.
 */
void Bridge::leave(int tourist_id) {
    std::unique_lock<std::mutex> lk(mu);

    --on_bridge;
    {
        std::ostringstream oss;
        oss << "LEAVE id=" << tourist_id << " occ=" << on_bridge << "/" << cap;
        log.log_ts("BRIDGE", oss.str());
    }

    if (on_bridge == 0) {
        dir = Direction::NONE;
        log.log_ts("BRIDGE", "BRIDGE_DIR_SET dir=NONE");
    }

    lk.unlock();
    cv.notify_all();
}

// ------------------------- TOWER (B) -------------------------

/**
 * @brief Initialize tower monitor with capacity and logger.
 */
Tower::Tower(int cap_, Logger& log_) : cap(cap_), log(log_) {}

/**
 * @brief Enter tower as single visitor with VIP fairness logic.
 */
void Tower::enter(int tourist_id, bool vip) {
    std::unique_lock<std::mutex> lk(mu);

    if (vip) ++waiting_vip;
    else     ++waiting_norm;

    {
        std::ostringstream oss;
        oss << "QUEUE_JOIN id=" << tourist_id
            << " vip=" << (vip ? 1 : 0)
            << " wait_vip=" << waiting_vip
            << " wait_norm=" << waiting_norm;
        log.log_ts("TOWER", oss.str());
    }

    cv.wait(lk, [&]{
        if (inside >= cap) return false;

        if (vip) {
            if (waiting_norm > 0 && vip_streak >= VIP_BURST) return false;
            return true;
        } else {
            if (waiting_vip == 0) return true;
            if (vip_streak >= VIP_BURST) return true;
            return false;
        }
    });

    if (vip) --waiting_vip;
    else     --waiting_norm;

    ++inside;
    if (vip) ++vip_streak;
    else     vip_streak = 0;

    {
        std::ostringstream oss;
        oss << "ENTER id=" << tourist_id << " vip=" << (vip ? 1 : 0)
            << " occ=" << inside << "/" << cap
            << " wait_vip=" << waiting_vip
            << " wait_norm=" << waiting_norm
            << " vip_streak=" << vip_streak;
        log.log_ts("TOWER", oss.str());
    }

    lk.unlock();
    cv.notify_all();
}

/**
 * @brief Leave tower as single visitor.
 */
void Tower::leave(int tourist_id) {
    std::unique_lock<std::mutex> lk(mu);

    if (inside > 0) --inside;

    {
        std::ostringstream oss;
        oss << "LEAVE id=" << tourist_id
            << " occ=" << inside << "/" << cap;
        log.log_ts("TOWER", oss.str());
    }

    lk.unlock();
    cv.notify_all();
}

// ---- Tower: wejście grupowe ----
/**
 * @brief Enter tower as group occupying k slots with VIP-like priority toggle.
 */
void Tower::enter_group(int group_id, int k, bool vip_like) {
    if (k <= 0) return;

    std::unique_lock<std::mutex> lk(mu);

    if (vip_like) waiting_vip += k;
    else          waiting_norm += k;

    {
        std::ostringstream oss;
        oss << "GROUP_QUEUE_JOIN gid=" << group_id
            << " k=" << k
            << " vip_like=" << (vip_like ? 1 : 0)
            << " wait_vip=" << waiting_vip
            << " wait_norm=" << waiting_norm;
        log.log_ts("TOWER", oss.str());
    }

    cv.wait(lk, [&]{
        if (inside + k > cap) return false;

        if (vip_like) {
            if (waiting_norm > 0 && vip_streak >= VIP_BURST) return false;
            return true;
        } else {
            if (waiting_vip == 0) return true;
            if (vip_streak >= VIP_BURST) return true;
            return false;
        }
    });

    if (vip_like) waiting_vip -= k;
    else          waiting_norm -= k;

    inside += k;
    if (vip_like) ++vip_streak;
    else          vip_streak = 0;

    {
        std::ostringstream oss;
        oss << "GROUP_ENTER gid=" << group_id
            << " k=" << k
            << " vip_like=" << (vip_like ? 1 : 0)
            << " occ=" << inside << "/" << cap
            << " wait_vip=" << waiting_vip
            << " wait_norm=" << waiting_norm
            << " vip_streak=" << vip_streak;
        log.log_ts("TOWER", oss.str());
    }

    lk.unlock();
    cv.notify_all();
}

/**
 * @brief Leave tower as group releasing k slots.
 */
void Tower::leave_group(int group_id, int k) {
    if (k <= 0) return;

    std::unique_lock<std::mutex> lk(mu);

    inside -= k;
    if (inside < 0) inside = 0;

    {
        std::ostringstream oss;
        oss << "GROUP_LEAVE gid=" << group_id
            << " k=" << k
            << " occ=" << inside << "/" << cap;
        log.log_ts("TOWER", oss.str());
    }

    lk.unlock();
    cv.notify_all();
}

// ------------------------- FERRY (C) -------------------------

/**
 * @brief Initialize ferry monitor with capacity and logger.
 */
Ferry::Ferry(int cap_, Logger& log_) : cap(cap_), log(log_) {}

/**
 * @brief Board ferry as single visitor with VIP fairness and direction log.
 */
void Ferry::board(int tourist_id, bool vip, Direction d) {
    std::unique_lock<std::mutex> lk(mu);

    if (vip) ++waiting_vip;
    else     ++waiting_norm;

    {
        std::ostringstream oss;
        oss << "QUEUE_JOIN id=" << tourist_id
            << " vip=" << (vip ? 1 : 0)
            << " dir=" << dir_str(d)
            << " wait_vip=" << waiting_vip
            << " wait_norm=" << waiting_norm;
        log.log_ts("FERRY", oss.str());
    }

    cv.wait(lk, [&]{
        if (onboard >= cap) return false;

        if (vip) {
            if (waiting_norm > 0 && vip_streak >= VIP_BURST) return false;
            return true;
        } else {
            if (waiting_vip == 0) return true;
            if (vip_streak >= VIP_BURST) return true;
            return false;
        }
    });

    if (vip) --waiting_vip;
    else     --waiting_norm;

    ++onboard;
    if (vip) ++vip_streak;
    else     vip_streak = 0;

    {
        std::ostringstream oss;
        oss << "BOARD id=" << tourist_id
            << " vip=" << (vip ? 1 : 0)
            << " dir=" << dir_str(d)
            << " occ=" << onboard << "/" << cap
            << " wait_vip=" << waiting_vip
            << " wait_norm=" << waiting_norm
            << " vip_streak=" << vip_streak;
        log.log_ts("FERRY", oss.str());
    }

    lk.unlock();
    cv.notify_all();
}

/**
 * @brief Unboard ferry as single visitor.
 */
void Ferry::unboard(int tourist_id) {
    std::unique_lock<std::mutex> lk(mu);

    if (onboard > 0) --onboard;

    {
        std::ostringstream oss;
        oss << "UNBOARD id=" << tourist_id
            << " occ=" << onboard << "/" << cap;
        log.log_ts("FERRY", oss.str());
    }

    lk.unlock();
    cv.notify_all();
}

// ---- Ferry: wejście grupowe ----
/**
 * @brief Board ferry as group occupying k slots with VIP-like fairness.
 */
void Ferry::board_group(int group_id, int k, bool vip_like, Direction d) {
    if (k <= 0) return;

    std::unique_lock<std::mutex> lk(mu);

    if (vip_like) waiting_vip += k;
    else          waiting_norm += k;

    {
        std::ostringstream oss;
        oss << "GROUP_QUEUE_JOIN gid=" << group_id
            << " k=" << k
            << " vip_like=" << (vip_like ? 1 : 0)
            << " dir=" << dir_str(d)
            << " wait_vip=" << waiting_vip
            << " wait_norm=" << waiting_norm;
        log.log_ts("FERRY", oss.str());
    }

    cv.wait(lk, [&]{
        if (onboard + k > cap) return false;

        if (vip_like) {
            if (waiting_norm > 0 && vip_streak >= VIP_BURST) return false;
            return true;
        } else {
            if (waiting_vip == 0) return true;
            if (vip_streak >= VIP_BURST) return true;
            return false;
        }
    });

    if (vip_like) waiting_vip -= k;
    else          waiting_norm -= k;

    onboard += k;
    if (vip_like) ++vip_streak;
    else          vip_streak = 0;

    {
        std::ostringstream oss;
        oss << "GROUP_BOARD gid=" << group_id
            << " k=" << k
            << " vip_like=" << (vip_like ? 1 : 0)
            << " dir=" << dir_str(d)
            << " occ=" << onboard << "/" << cap
            << " wait_vip=" << waiting_vip
            << " wait_norm=" << waiting_norm
            << " vip_streak=" << vip_streak;
        log.log_ts("FERRY", oss.str());
    }

    lk.unlock();
    cv.notify_all();
}

/**
 * @brief Unboard ferry as group releasing k slots.
 */
void Ferry::unboard_group(int group_id, int k) {
    if (k <= 0) return;

    std::unique_lock<std::mutex> lk(mu);

    onboard -= k;
    if (onboard < 0) onboard = 0;

    {
        std::ostringstream oss;
        oss << "GROUP_UNBOARD gid=" << group_id
            << " k=" << k
            << " occ=" << onboard << "/" << cap;
        log.log_ts("FERRY", oss.str());
    }

    lk.unlock();
    cv.notify_all();
}
