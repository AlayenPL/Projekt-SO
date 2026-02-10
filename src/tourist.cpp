#include "tourist.hpp"

#include "park.hpp"
#include "group.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

/**
 * @brief Construct a tourist with identifiers and VIP flag.
 */
Tourist::Tourist(int id_, int age_, bool vip_, Park* park_)
    : id(id_), age(age_), vip(vip_), park(park_) {}

/**
 * @brief Launch the tourist thread.
 */
void Tourist::start() {
    thr = std::thread(&Tourist::run, this);
}

/**
 * @brief Join the tourist thread if running.
 */
void Tourist::join() {
    if (thr.joinable()) thr.join();
}

/**
 * @brief Mark tourist as admitted by cashier.
 */
void Tourist::on_admitted() {
    std::lock_guard<std::mutex> lk(mu);
    admitted = true;
    cv.notify_all();
}

/**
 * @brief Mark tourist as rejected by cashier.
 */
void Tourist::on_rejected() {
    std::lock_guard<std::mutex> lk(mu);
    rejected = true;
    cv.notify_all();
}

/**
 * @brief Assign group id and guide id.
 */
void Tourist::assign_to_group(int gid, int pid) {
    std::lock_guard<std::mutex> lk(mu);
    group_id = gid;
    guide_id = pid;
    cv.notify_all();
}

/**
 * @brief Set next step and bump epoch for synchronization.
 */
void Tourist::set_step(Step s) {
    std::lock_guard<std::mutex> lk(mu);
    next_step = s;
    step_ready = true;
    step_epoch++;
    cv.notify_all();
}

/**
 * @brief Assign guardian pointer; records missing guardian for children.
 */
void Tourist::set_guardian(Tourist* g, bool is_u5_child) {
    guardian = g;
    if (!guardian) {
        no_guard.store(true);
    } else {
        no_guard.store(false);
        if (is_u5_child) {
            guardian->guardian_of_u5.store(true);
        }
    }
}

// Helpers (VIP path)
/**
 * @brief Sleep in small slices while honoring abort flag.
 */
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

/**
 * @brief Guardian notifies wards they may proceed for given epoch.
 */
void Tourist::guardian_notify_wards_ready(int epoch) {
    std::lock_guard<std::mutex> lk(escort_mu);
    escort_epoch = epoch;
    escort_cv.notify_all();
}

/**
 * @brief Child waits until guardian ready for epoch or abort is triggered.
 */
void Tourist::child_wait_for_guardian_ready(int epoch, const char* where) {
    if (!guardian) return;

    std::unique_lock<std::mutex> lk(guardian->escort_mu);
    guardian->escort_cv.wait(lk, [&] {
        return guardian->escort_epoch >= epoch || abort_to_k.load();
    });

    if (abort_to_k.load()) {
        park->log.log_ts("GUARD",
                         std::string("CHILD_ABORT_WAIT id=") + std::to_string(id) +
                         " where=" + where +
                         " gid=" + std::to_string(group_id));
    }
}

/**
 * @brief Map route number to direction choice for forward/backward legs.
 */
static Direction dir_from_route(int route, Direction d_for_route1, Direction d_for_route2) {
    return (route == 1) ? d_for_route1 : d_for_route2;
}

/**
 * @brief Main tourist thread entry: admission then VIP or guided path.
 */
void Tourist::run() {
    {
        std::ostringstream oss;
        oss << "ARRIVE id=" << id << " age=" << age << " vip=" << (vip ? 1 : 0);
        park->log.log_ts("TOURIST", oss.str());
    }

    park->enqueue_entry(this);

    {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&] { return admitted || rejected; });
    }

    if (rejected) {
        park->log.log_ts("TOURIST", "LEAVE_NO_ENTRY id=" + std::to_string(id));
        return;
    }

    if (vip) run_vip();
    else run_guided();
}

/**
 * @brief VIP unguided visit flow with segment, bridge, tower, ferry.
 */
void Tourist::run_vip() {
    if (age < 15) {
        park->log.log_ts("VIP",
                         "DENY_CHILD id=" + std::to_string(id) +
                         " age=" + std::to_string(age) +
                         " reason=NEEDS_GUARDIAN");
        park->report_exit(id);
        return;
    }

    int route = park->rand_int(1, 2);
    park->log.log_ts("VIP", "START id=" + std::to_string(id) + " route=" + std::to_string(route));

    auto segment_sleep = [&] {
        int ms = park->rand_int(park->cfg.segment_min_ms, park->cfg.segment_max_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
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
        sleep_interruptible_ms(ms, abort_to_k);
        park->tower.leave(id);
    };

    auto ferry_cross = [&](Direction d) {
        park->ferry.board(id, true, d);
        std::this_thread::sleep_for(std::chrono::milliseconds(park->cfg.ferry_T_ms));
        park->ferry.unboard(id);
    };

    Direction bridge_dir = dir_from_route(route, Direction::FORWARD, Direction::BACKWARD);
    Direction ferry_dir  = dir_from_route(route, Direction::FORWARD, Direction::BACKWARD);

    if (route == 1) {
        segment_sleep();
        bridge_cross(bridge_dir);
        segment_sleep();
        tower_visit();
        segment_sleep();
        ferry_cross(ferry_dir);
        segment_sleep();
    } else {
        segment_sleep();
        ferry_cross(ferry_dir);
        segment_sleep();
        tower_visit();
        segment_sleep();
        bridge_cross(bridge_dir);
        segment_sleep();
    }

    park->log.log_ts("VIP", "END id=" + std::to_string(id));
    park->report_exit(id);
}

/**
 * @brief Guided visit flow; waits for group and executes guided steps.
 */
void Tourist::run_guided() {
    park->enqueue_group_wait(this);

    {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&] { return group_id >= 0 || rejected; });
    }

    if (rejected) {
        park->report_exit(id);
        return;
    }

    park->log.log_ts("TOURIST",
                     "GROUP_JOIN id=" + std::to_string(id) +
                     " gid=" + std::to_string(group_id) +
                     " guide=" + std::to_string(guide_id));

    while (true) {
        Step s;
        int epoch;
        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, [&] { return step_ready; });
            s = next_step;
            epoch = step_epoch;
            step_ready = false;
        }

        if (s == Step::EXIT) {
            park->report_exit(id);
            if (group) group->mark_done();
            return;
        }

        if (abort_to_k.load() && s != Step::RETURN_K) {
            s = Step::RETURN_K;
        }

        // Centralne wykonanie kroku w Parku (spójny punkt dla dalszej refaktoryzacji grupowej)
        park->do_step(this, s, epoch);

        if (group) group->mark_done();
    }
}
