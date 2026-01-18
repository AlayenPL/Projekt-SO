#include "resources.hpp"

#include <sstream>

// ------------------------- BRIDGE (A) -------------------------

Bridge::Bridge(int cap_, Logger& log_) : cap(cap_), log(log_) {}

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

    // WAŻNE: pozwól kolejnym wejść aż do cap (w przeciwnym razie często wpuszczało „po 1”)
    lk.unlock();
    cv.notify_all();
}

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

Tower::Tower(int cap_, Logger& log_) : cap(cap_), log(log_) {}

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
            // FAIRNESS: jeśli czekają normalni i VIP był wpuszczany zbyt długo,
            // VIP musi ustąpić aż wejdzie przynajmniej jeden normalny (reset vip_streak).
            if (waiting_norm > 0 && vip_streak >= VIP_BURST) return false;
            return true;
        } else {
            // Normalny wchodzi, gdy:
            // - nie ma VIP w kolejce, albo
            // - VIP byli wpuszczani zbyt długo (vip_streak >= VIP_BURST)
            if (waiting_vip == 0) return true;
            if (vip_streak >= VIP_BURST) return true;
            return false;
        }
    });

    // schodzimy z kolejki
    if (vip) --waiting_vip;
    else     --waiting_norm;

    // wejście
    ++inside;
    if (vip) ++vip_streak;
    else     vip_streak = 0;

    {
        std::ostringstream oss;
        oss << "QUEUE_LEAVE id=" << tourist_id
            << " vip=" << (vip ? 1 : 0)
            << " inside=" << inside << "/" << cap
            << " wait_vip=" << waiting_vip
            << " wait_norm=" << waiting_norm
            << " vip_streak=" << vip_streak;
        log.log_ts("TOWER", oss.str());
    }

    {
        std::ostringstream oss;
        oss << "ENTER id=" << tourist_id << " vip=" << (vip ? 1 : 0)
            << " occ=" << inside << "/" << cap;
        log.log_ts("TOWER", oss.str());
    }

    // WAŻNE: po zmianie inside/vip_streak obudź innych (np. VIP po wejściu normalnego)
    lk.unlock();
    cv.notify_all();
}

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

// ------------------------- FERRY (C) -------------------------

Ferry::Ferry(int cap_, Logger& log_) : cap(cap_), log(log_) {}

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
            // FAIRNESS: analogicznie do Tower
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
        oss << "QUEUE_LEAVE id=" << tourist_id
            << " vip=" << (vip ? 1 : 0)
            << " dir=" << dir_str(d)
            << " onboard=" << onboard << "/" << cap
            << " wait_vip=" << waiting_vip
            << " wait_norm=" << waiting_norm
            << " vip_streak=" << vip_streak;
        log.log_ts("FERRY", oss.str());
    }

    {
        std::ostringstream oss;
        oss << "BOARD id=" << tourist_id
            << " vip=" << (vip ? 1 : 0)
            << " dir=" << dir_str(d)
            << " occ=" << onboard << "/" << cap;
        log.log_ts("FERRY", oss.str());
    }

    // WAŻNE: po zmianie onboard/vip_streak obudź innych
    lk.unlock();
    cv.notify_all();
}

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
