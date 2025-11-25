#include "resources.hpp"

#include <sstream>

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

Tower::Tower(int cap_, Logger& log_) : cap(cap_), log(log_), sem(cap_) {}

void Tower::enter(int tourist_id, bool vip) {
    // VIP "skips queue" by attempting immediately; normals also attempt; semaphore is the limiter.
    // FIFO is not guaranteed; we record VIP flag for verification.
    sem.acquire();
    std::ostringstream oss;
    oss << "ENTER id=" << tourist_id << " vip=" << (vip ? 1 : 0);
    log.log_ts("TOWER", oss.str());
}

void Tower::leave(int tourist_id) {
    std::ostringstream oss;
    oss << "LEAVE id=" << tourist_id;
    log.log_ts("TOWER", oss.str());
    sem.release();
}

Ferry::Ferry(int cap_, Logger& log_) : cap(cap_), log(log_), sem(cap_) {}

void Ferry::board(int tourist_id, bool vip, Direction d) {
    sem.acquire();
    std::ostringstream oss;
    oss << "BOARD id=" << tourist_id << " vip=" << (vip ? 1 : 0) << " dir=" << dir_str(d);
    log.log_ts("FERRY", oss.str());
}

void Ferry::unboard(int tourist_id) {
    std::ostringstream oss;
    oss << "UNBOARD id=" << tourist_id;
    log.log_ts("FERRY", oss.str());
    sem.release();
}
