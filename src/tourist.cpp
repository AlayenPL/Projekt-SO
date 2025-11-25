#include "tourist.hpp"

#include "park.hpp"
#include "group.hpp"

#include <chrono>
#include <sstream>
#include <thread>

Tourist::Tourist(int id_, int age_, bool vip_, Park* park_)
    : id(id_), age(age_), vip(vip_), park(park_) {}

void Tourist::start() {
    thr = std::thread(&Tourist::run, this);
}

void Tourist::join() {
    if (thr.joinable()) thr.join();
}

void Tourist::on_admitted() {
    std::lock_guard<std::mutex> lk(mu);
    admitted = true;
    cv.notify_all();
}

void Tourist::on_rejected() {
    std::lock_guard<std::mutex> lk(mu);
    rejected = true;
    cv.notify_all();
}

void Tourist::assign_to_group(int gid, int pid) {
    std::lock_guard<std::mutex> lk(mu);
    group_id = gid;
    guide_id = pid;
    cv.notify_all();
}

void Tourist::set_step(Step s) {
    std::lock_guard<std::mutex> lk(mu);
    next_step = s;
    step_ready = true;
    cv.notify_all();
}

// Helpers
static void sleep_interruptible_ms(int total_ms, std::atomic<bool>& abort_flag) {
    int slice = 50;
    int slept = 0;
    while (slept < total_ms) {
        if (abort_flag.load()) return;
        int d = std::min(slice, total_ms - slept);
        std::this_thread::sleep_for(std::chrono::milliseconds(d));
        slept += d;
    }
}

void Tourist::run() {
    // Arrived: enqueue at cashier
    {
        std::ostringstream oss;
        oss << "ARRIVE id=" << id << " age=" << age << " vip=" << (vip ? 1 : 0);
        park->log.log_ts("TOURIST", oss.str());
    }

    park->enqueue_entry(this);

    // Wait admission or rejection
    {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&]{ return admitted || rejected; });
    }

    if (rejected) {
        park->log.log_ts("TOURIST", "LEAVE_NO_ENTRY id=" + std::to_string(id));
        return;
    }

    if (vip) run_vip();
    else run_guided();
}

void Tourist::run_vip() {
    // VIP self-guided: choose route 1/2 and traverse resources.
    int route = park->rand_int(1, 2);
    park->log.log_ts("VIP", "START id=" + std::to_string(id) + " route=" + std::to_string(route));

    auto segment_sleep = [&] {
        int ms = park->rand_int(park->cfg.segment_min_ms, park->cfg.segment_max_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    };

    auto do_A = [&] {
        // Direction is derived from route position; we map forward/backward based on call site.
    };

    auto bridge_cross = [&](Direction d) {
        park->bridge.enter(id, d);
        int ms = park->rand_int(park->cfg.bridge_min_ms, park->cfg.bridge_max_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        park->bridge.leave(id);
    };

    auto tower_visit = [&] {
        if (age <= 5) {
            park->log.log_ts("VIP", "TOWER_SKIP id=" + std::to_string(id) + " reason=AGE<=5");
            return;
        }
        park->tower.enter(id, true);
        int ms = park->rand_int(park->cfg.tower_min_ms, park->cfg.tower_max_ms);
        sleep_interruptible_ms(ms, abort_to_k); // if signal2-like behavior ever used for VIP, allow abort
        park->tower.leave(id);
    };

    auto ferry_cross = [&](Direction d) {
        park->ferry.board(id, true, d);
        std::this_thread::sleep_for(std::chrono::milliseconds(park->cfg.ferry_T_ms));
        park->ferry.unboard(id);
    };

    // For VIP we ignore child-with-guardian constraints (VIP is single person).
    if (route == 1) {
        segment_sleep();
        bridge_cross(Direction::FORWARD);
        segment_sleep();
        tower_visit();
        segment_sleep();
        ferry_cross(Direction::FORWARD);
        segment_sleep();
    } else {
        segment_sleep();
        ferry_cross(Direction::FORWARD);
        segment_sleep();
        tower_visit();
        segment_sleep();
        bridge_cross(Direction::FORWARD);
        segment_sleep();
    }

    park->log.log_ts("VIP", "END id=" + std::to_string(id));
    park->report_exit(id);
}

void Tourist::run_guided() {
    // Join group formation queue
    park->enqueue_group_wait(this);

    // Wait until guide assigns a group id (non-negative)
    {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&]{ return group_id >= 0 || rejected; });
    }

    if (rejected) {
        park->report_exit(id);
        return;
    }

    park->log.log_ts("TOURIST", "GROUP_JOIN id=" + std::to_string(id) +
                                 " gid=" + std::to_string(group_id) +
                                 " guide=" + std::to_string(guide_id));

    while (true) {
        Step s;
        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, [&]{ return step_ready; });
            s = next_step;
            step_ready = false;
        }

        if (s == Step::EXIT) {
            // Group completed; exit park.
            park->report_exit(id);
            if (group) group->mark_done();
            return;
        }

        if (abort_to_k.load() && s != Step::RETURN_K) {
            // If signal2, ignore other steps.
            s = Step::RETURN_K;
        }

        switch (s) {
            case Step::GO_A: {
                // Bridge crossing: direction derived from group route segment; we approximate with FORWARD.
                park->bridge.enter(id, Direction::FORWARD);
                int ms = park->rand_int(park->cfg.bridge_min_ms, park->cfg.bridge_max_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                park->bridge.leave(id);
                break;
            }
            case Step::GO_B: {
                // Tower restrictions
                if (age <= 5) {
                    park->log.log_ts("TOWER", "DENY id=" + std::to_string(id) + " reason=AGE<=5");
                    break;
                }
                // Guided tourists are non-VIP, so vip=false
                park->tower.enter(id, false);
                int ms = park->rand_int(park->cfg.tower_min_ms, park->cfg.tower_max_ms);

                // Evacuation signal1: if set, descend immediately
                if (tower_evacuate.load()) {
                    park->log.log_ts("TOWER", "EVACUATE id=" + std::to_string(id) + " gid=" + std::to_string(group_id));
                    // short descend time
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } else {
                    sleep_interruptible_ms(ms, tower_evacuate);
                }
                park->tower.leave(id);
                break;
            }
            case Step::GO_C: {
                park->ferry.board(id, false, Direction::FORWARD);
                std::this_thread::sleep_for(std::chrono::milliseconds(park->cfg.ferry_T_ms));
                park->ferry.unboard(id);
                break;
            }
            case Step::RETURN_K: {
                park->log.log_ts("TOURIST", "RETURN_K id=" + std::to_string(id) + " gid=" + std::to_string(group_id));
                // simulate short walk back
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                break;
            }
            default:
                break;
        }

        if (group) group->mark_done();
    }
}
