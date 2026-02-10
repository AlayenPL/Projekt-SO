#include "park.hpp"
#include "tourist.hpp"
#include "group.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>
#include <thread>

/**
 * @brief Construct park with resources initialized from config and logger.
 */
Park::Park(const Config& cfg_, Logger& log_)
    : cfg(cfg_), log(log_), bridge(cfg.X1, log_), tower(cfg.X2, log_), ferry(cfg.X3, log_), rng(cfg.seed) {}

/**
 * @brief Thread-safe uniform integer.
 */
int Park::rand_int(int lo, int hi) {
    std::lock_guard<std::mutex> lk(rng_mu);
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng);
}

/**
 * @brief Thread-safe uniform double in [0,1).
 */
double Park::rand01() {
    std::lock_guard<std::mutex> lk(rng_mu);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
}

/**
 * @brief Map route number to direction choice for forward/backward legs.
 */
static Direction dir_from_route(int route, Direction d_for_route1, Direction d_for_route2) {
    return (route == 1) ? d_for_route1 : d_for_route2;
}

/**
 * @brief Sleep in slices while checking abort flag.
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
 * @brief Execute one simulation step for a guided tourist, handling group coordination and constraints.
 */
void Park::do_step(Tourist* t, Step s, int epoch) {
    auto deny_no_guard_for = [&](Tourist* who, const char* where) {
        log.log_ts("GUARD",
                   std::string("DENY_NO_GUARD id=") + std::to_string(who->id) +
                   " age=" + std::to_string(who->age) +
                   " where=" + where +
                   " gid=" + std::to_string(who->group_id));
    };

    int route = (t->group ? t->group->route : 1);

    Direction bridge_dir = dir_from_route(route, Direction::FORWARD, Direction::BACKWARD);
    Direction ferry_dir  = dir_from_route(route, Direction::FORWARD, Direction::BACKWARD);

    switch (s) {
        case Step::GO_A: {
            // Bridge nadal "symbolicznie" (1 osoba), ale grupa logicznie razem.
            auto g = t->group;
            if (!g) {
                bridge.enter(t->id, bridge_dir);
                int ms = rand_int(cfg.bridge_min_ms, cfg.bridge_max_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                bridge.leave(t->id);
                break;
            }

            if (!g->bridge_try_become_coordinator(epoch, t->id)) {
                g->bridge_wait_done(epoch);
                break;
            }

            // Log DENY dla dzieci bez opiekuna (A)
            for (auto* m : g->members) {
                if (!m) continue;
                if (m->age < 15) {
                    if (m->no_guard.load() || m->guardian == nullptr) {
                        deny_no_guard_for(m, "A");
                    }
                }
            }

            bridge.enter(t->id, bridge_dir);
            int ms = rand_int(cfg.bridge_min_ms, cfg.bridge_max_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            bridge.leave(t->id);

            g->bridge_finish(epoch);
            break;
        }

        case Step::GO_B: {
            auto g = t->group;
            if (!g) {
                if (t->age <= 5) {
                    log.log_ts("TOWER", "DENY id=" + std::to_string(t->id) + " reason=AGE<=5");
                    break;
                }
                if (t->guardian_of_u5.load()) {
                    log.log_ts("TOWER", "DENY id=" + std::to_string(t->id) + " reason=GUARD_OF_AGE<=5");
                    break;
                }
                tower.enter(t->id, t->vip);
                int ms = rand_int(cfg.tower_min_ms, cfg.tower_max_ms);
                sleep_interruptible_ms(ms, t->tower_evacuate);
                tower.leave(t->id);
                break;
            }

            if (!g->tower_try_become_coordinator(epoch, t->id)) {
                g->tower_wait_done(epoch);
                break;
            }

            // Policz ilu realnie wchodzi na wieżę (k)
            int k = 0;
            for (auto* m : g->members) {
                if (!m) continue;

                if (m->age <= 5) {
                    log.log_ts("TOWER", "DENY id=" + std::to_string(m->id) + " reason=AGE<=5");
                    continue;
                }
                if (m->guardian_of_u5.load()) {
                    log.log_ts("TOWER", "DENY id=" + std::to_string(m->id) + " reason=GUARD_OF_AGE<=5");
                    continue;
                }

                if (m->age < 15) {
                    if (m->no_guard.load() || m->guardian == nullptr) {
                        deny_no_guard_for(m, "B");
                        continue;
                    }
                    // Jeśli opiekun nie może wejść na wieżę, dziecko też odpada
                    if (m->guardian->guardian_of_u5.load()) {
                        log.log_ts("TOWER", "DENY id=" + std::to_string(m->id) + " reason=GUARD_CANNOT_TOWER");
                        continue;
                    }
                }

                ++k;
            }

            if (k <= 0) {
                log.log_ts("TOWER", "GROUP_SKIP gid=" + std::to_string(t->group_id) + " reason=NO_ELIGIBLE");
                g->tower_finish(epoch);
                break;
            }

            // Guided grupa jest non-VIP => vip_like = false
            tower.enter_group(t->group_id, k, false);

            int ms = rand_int(cfg.tower_min_ms, cfg.tower_max_ms);
            if (t->tower_evacuate.load()) {
                log.log_ts("TOWER",
                           "EVACUATE_GROUP gid=" + std::to_string(t->group_id) +
                           " k=" + std::to_string(k));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                sleep_interruptible_ms(ms, t->tower_evacuate);
            }

            tower.leave_group(t->group_id, k);
            g->tower_finish(epoch);
            break;
        }

        case Step::GO_C: {
            auto g = t->group;
            if (!g) {
                if (t->age < 15) {
                    if (t->no_guard.load() || t->guardian == nullptr) {
                        deny_no_guard_for(t, "C");
                        break;
                    }
                }
                ferry.board(t->id, t->vip, ferry_dir);
                std::this_thread::sleep_for(std::chrono::milliseconds(cfg.ferry_T_ms));
                ferry.unboard(t->id);
                break;
            }

            if (!g->ferry_try_become_coordinator(epoch, t->id)) {
                g->ferry_wait_done(epoch);
                break;
            }

            int k = 0;
            for (auto* m : g->members) {
                if (!m) continue;
                if (m->age < 15) {
                    if (m->no_guard.load() || m->guardian == nullptr) {
                        deny_no_guard_for(m, "C");
                        continue;
                    }
                }
                ++k;
            }

            if (k <= 0) {
                log.log_ts("FERRY", "GROUP_SKIP gid=" + std::to_string(t->group_id) + " reason=NO_ELIGIBLE");
                g->ferry_finish(epoch);
                break;
            }

            ferry.board_group(t->group_id, k, false, ferry_dir);
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.ferry_T_ms));
            ferry.unboard_group(t->group_id, k);

            g->ferry_finish(epoch);
            break;
        }

        case Step::RETURN_K: {
            log.log_ts("TOURIST",
                       "RETURN_K id=" + std::to_string(t->id) +
                       " gid=" + std::to_string(t->group_id));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            break;
        }

        default:
            break;
    }
}

/**
 * @brief Start cashier and guide threads.
 */
void Park::start() {
    cashier_thr = std::thread(&Park::cashier_loop, this);
    for (int i = 0; i < cfg.P; ++i) {
        guide_thrs.emplace_back(&Park::guide_loop, this, i);
    }
}

/**
 * @brief Stop simulation, wake queues, and join threads.
 */
void Park::stop() {
    close();

    if (cashier_thr.joinable()) cashier_thr.join();
    for (auto& t : guide_thrs) if (t.joinable()) t.join();
}

void Park::close() {
    open.store(false);
    entry_cv.notify_all();
    group_cv.notify_all();
    exit_cv.notify_all();
}

/**
 * @brief Enqueue tourist for cashier entry with VIP priority.
 */
void Park::enqueue_entry(Tourist* t) {
    {
        std::lock_guard<std::mutex> lk(entry_mu);
        if (t->vip) entry_vip.push_back(t);
        else entry_norm.push_back(t);
    }
    enqueued.fetch_add(1);
    entry_cv.notify_one();
}

/**
 * @brief Dequeue next tourist for cashier; blocks until available or park closed.
 */
Tourist* Park::dequeue_for_cashier() {
    std::unique_lock<std::mutex> lk(entry_mu);
    entry_cv.wait(lk, [&]{
        return !open.load() || !entry_vip.empty() || !entry_norm.empty();
    });

    if (!entry_vip.empty()) {
        auto* t = entry_vip.front();
        entry_vip.pop_front();
        return t;
    }
    if (!entry_norm.empty()) {
        auto* t = entry_norm.front();
        entry_norm.pop_front();
        return t;
    }
    return nullptr;
}

/**
 * @brief Enqueue tourist waiting to form a guided group.
 */
void Park::enqueue_group_wait(Tourist* t) {
    {
        std::lock_guard<std::mutex> lk(group_mu);
        group_wait.push_back(t);
    }
    group_cv.notify_one();
}

/**
 * @brief Dequeue exactly M tourists to form a group; blocks until enough.
 */
std::vector<Tourist*> Park::dequeue_group(int M) {
    std::unique_lock<std::mutex> lk(group_mu);
    group_cv.wait(lk, [&]{
        return !open.load() || static_cast<int>(group_wait.size()) >= M;
    });

    std::vector<Tourist*> g;
    if (static_cast<int>(group_wait.size()) < M) {
        if (!open.load()) {
            // final partial group when park closed
            while (!group_wait.empty()) {
                g.push_back(group_wait.front());
                group_wait.pop_front();
            }
        }
        return g;
    }

    for (int i = 0; i < M; ++i) {
        g.push_back(group_wait.front());
        group_wait.pop_front();
    }
    return g;
}

/**
 * @brief Report tourist exit to cashier logger.
 */
void Park::report_exit(int tourist_id) {
    {
        std::lock_guard<std::mutex> lk(exit_mu);
        exit_ids.push_back(tourist_id);
    }
    exited.fetch_add(1);
    exit_cv.notify_one();
}

/**
 * @brief Cashier thread loop controlling entry limit N and logging exits.
 */
void Park::cashier_loop() {
    log.log_ts("CASHIER", "START");

    while (open.load() || !entry_vip.empty() || !entry_norm.empty()) {
        Tourist* t = dequeue_for_cashier();
        if (!t) continue;

        int current = entered.load();
        if (current >= cfg.N) {
            log.log_ts("CASHIER", "REJECT id=" + std::to_string(t->id) + " reason=LIMIT_N");
            t->on_rejected();
            continue;
        }

        int after = entered.fetch_add(1) + 1;

        std::ostringstream oss;
        oss << "ENTER id=" << t->id
            << " age=" << t->age
            << " vip=" << (t->vip ? 1 : 0)
            << " count=" << after << "/" << cfg.N
            << " pay=" << ((t->age < 7 || t->vip) ? 0 : 1);
        log.log_ts("CASHIER", oss.str());

        t->on_admitted();

        {
            std::unique_lock<std::mutex> lk(exit_mu);
            while (!exit_ids.empty()) {
                int id = exit_ids.front();
                exit_ids.pop_front();
                log.log_ts("CASHIER", "EXIT id=" + std::to_string(id));
            }
        }
    }

    {
        std::unique_lock<std::mutex> lk(exit_mu);
        while (!exit_ids.empty()) {
            int id = exit_ids.front();
            exit_ids.pop_front();
            log.log_ts("CASHIER", "EXIT id=" + std::to_string(id));
        }
    }

    log.log_ts("CASHIER", "STOP");
}

/**
 * @brief Guide thread loop forming groups, assigning guardians, driving routes.
 */
void Park::guide_loop(int guide_id) {
    int group_seq = 0;
    log.log_ts("GUIDE", "START guide=" + std::to_string(guide_id));

    while (true) {
        auto members = dequeue_group(cfg.M);
        if (members.empty()) {
            if (!open.load()) break;
            continue;
        }

        int gid = guide_id * 100000 + group_seq++;

        auto group = std::make_shared<GroupControl>(gid, guide_id);
        group->members = members;

        for (auto* t : members) {
            t->set_group(group);
            t->assign_to_group(gid, guide_id);
        }

        std::vector<Tourist*> adults;
        std::vector<Tourist*> children;
        adults.reserve(members.size());
        children.reserve(members.size());

        for (auto* t : members) {
            if (t->age >= 15) adults.push_back(t);
            else children.push_back(t);
        }

        for (auto* c : children) {
            if (adults.empty()) {
                c->set_guardian(nullptr, (c->age <= 5));
                log.log_ts("GUARD",
                           "GUARD_NONE child=" + std::to_string(c->id) +
                           " age=" + std::to_string(c->age) +
                           " gid=" + std::to_string(gid));
            } else {
                int idx = rand_int(0, static_cast<int>(adults.size()) - 1);
                Tourist* g = adults[idx];
                c->set_guardian(g, (c->age <= 5));
                log.log_ts("GUARD",
                           "GUARD_ASSIGN child=" + std::to_string(c->id) +
                           " age=" + std::to_string(c->age) +
                           " guardian=" + std::to_string(g->id) +
                           " gid=" + std::to_string(gid));
            }
        }

        int route = rand_int(1, 2);
        group->route = route;

        log.log_ts("GUIDE",
                   "GROUP_START guide=" + std::to_string(guide_id) +
                   " gid=" + std::to_string(gid) +
                   " route=" + std::to_string(route));

        bool has_child_u12 = false;
        for (auto* t : members) if (t->age < 12) { has_child_u12 = true; break; }

        auto do_segment_sleep = [&] {
            int base = rand_int(cfg.segment_min_ms, cfg.segment_max_ms);
            if (has_child_u12) base = (base * 3) / 2;
            std::this_thread::sleep_for(std::chrono::milliseconds(base));
        };

        auto maybe_signal2 = [&]() {
            if (rand01() < cfg.signal2_prob) {
                log.log_ts("GUIDE", "SIGNAL2 guide=" + std::to_string(guide_id) + " gid=" + std::to_string(gid));
                for (auto* t : members) t->abort_to_k.store(true);
            }
        };

        auto maybe_signal1 = [&]() {
            if (rand01() < cfg.signal1_prob) {
                log.log_ts("GUIDE", "SIGNAL1 guide=" + std::to_string(guide_id) + " gid=" + std::to_string(gid));
                for (auto* t : members) t->tower_evacuate.store(true);
            }
        };

        auto step_all = [&](Step s) {
            group->begin_step(s);
            for (auto* t : members) t->set_step(s);
            group->wait_step_done();
        };

        auto segment = [&](const char* from, const char* to) -> bool {
            maybe_signal2();
            if (std::any_of(members.begin(), members.end(),
                            [](Tourist* t){ return t->abort_to_k.load(); })) {
                step_all(Step::RETURN_K);
                return false;
            }
            log.log_ts("GUIDE", std::string("SEGMENT ") + from + "->" + to + " gid=" + std::to_string(gid));
            do_segment_sleep();
            return true;
        };

        if (route == 1) {
            if (!segment("K", "A")) goto done;
            step_all(Step::GO_A);

            if (!segment("A", "B")) goto done;
            step_all(Step::GO_B);
            maybe_signal1();

            if (!segment("B", "C")) goto done;
            step_all(Step::GO_C);

            if (!segment("C", "K")) goto done;
            step_all(Step::RETURN_K);
        } else {
            if (!segment("K", "C")) goto done;
            step_all(Step::GO_C);

            if (!segment("C", "B")) goto done;
            step_all(Step::GO_B);
            maybe_signal1();

            if (!segment("B", "A")) goto done;
            step_all(Step::GO_A);

            if (!segment("A", "K")) goto done;
            step_all(Step::RETURN_K);
        }

    done:
        group->begin_step(Step::EXIT);
        for (auto* t : members) t->set_step(Step::EXIT);
        group->wait_step_done();

        log.log_ts("GUIDE", "GROUP_END guide=" + std::to_string(guide_id) + " gid=" + std::to_string(gid));
    }

    log.log_ts("GUIDE", "STOP guide=" + std::to_string(guide_id));
}
