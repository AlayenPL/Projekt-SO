#include "park.hpp"
#include "tourist.hpp"
#include "group.hpp"

#include <algorithm>
#include <sstream>

Park::Park(const Config& cfg_, Logger& log_)
    : cfg(cfg_), log(log_), bridge(cfg.X1, log_), tower(cfg.X2, log_), ferry(cfg.X3, log_), rng(cfg.seed) {}

int Park::rand_int(int lo, int hi) {
    std::lock_guard<std::mutex> lk(rng_mu);
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng);
}

double Park::rand01() {
    std::lock_guard<std::mutex> lk(rng_mu);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
}

void Park::start() {
    cashier_thr = std::thread(&Park::cashier_loop, this);
    for (int i = 0; i < cfg.P; ++i) {
        guide_thrs.emplace_back(&Park::guide_loop, this, i);
    }
}

void Park::stop() {
    open.store(false);
    entry_cv.notify_all();
    group_cv.notify_all();
    exit_cv.notify_all();

    // Odrzuć wszystkich czekających na grupę
    {
        std::lock_guard<std::mutex> lk(group_mu);
        for (auto* t : group_wait) t->on_rejected();
        group_wait.clear();
    }

    if (cashier_thr.joinable()) cashier_thr.join();
    for (auto& t : guide_thrs) if (t.joinable()) t.join();
}

void Park::enqueue_entry(Tourist* t) {
    {
        std::lock_guard<std::mutex> lk(entry_mu);
        if (t->vip) entry_vip.push_back(t);
        else entry_norm.push_back(t);
    }
    entry_cv.notify_one();
}

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

void Park::enqueue_group_wait(Tourist* t) {
    {
        std::lock_guard<std::mutex> lk(group_mu);
        group_wait.push_back(t);
    }
    group_cv.notify_one();
}

std::vector<Tourist*> Park::dequeue_group(int M) {
    std::unique_lock<std::mutex> lk(group_mu);
    group_cv.wait(lk, [&]{
        return !open.load() || static_cast<int>(group_wait.size()) >= M;
    });

    std::vector<Tourist*> g;
    if (static_cast<int>(group_wait.size()) < M) return g;

    for (int i = 0; i < M; ++i) {
        g.push_back(group_wait.front());
        group_wait.pop_front();
    }
    return g;
}

void Park::report_exit(int tourist_id) {
    {
        std::lock_guard<std::mutex> lk(exit_mu);
        exit_ids.push_back(tourist_id);
    }
    exit_cv.notify_one();
}

void Park::cashier_loop() {
    log.log_ts("CASHIER", "START");

    while (open.load()) {
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

        // Zbierz zaległe wyjścia
        {
            std::unique_lock<std::mutex> lk(exit_mu);
            while (!exit_ids.empty()) {
                int id = exit_ids.front();
                exit_ids.pop_front();
                log.log_ts("CASHIER", "EXIT id=" + std::to_string(id));
            }
        }
    }

    // Drain
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

void Park::guide_loop(int guide_id) {
    int group_seq = 0;
    log.log_ts("GUIDE", "START guide=" + std::to_string(guide_id));

    while (open.load()) {
        auto members = dequeue_group(cfg.M);
        if (members.empty()) continue;

        int gid = guide_id * 100000 + group_seq++;
        GroupControl group(gid, guide_id);
        group.members = members;

        // przypisz do grupy
        for (auto* t : members) {
            t->group = &group;
            t->assign_to_group(gid, guide_id);
        }

        // --- Guardian assignment (dowolny dorosły z grupy) ---
        // Dzieci (<15) muszą mieć opiekuna (>=15) do A/B/C.
        // Jeśli nie ma dorosłych -> dziecko ma no_guard=true (turysta zaloguje DENY_NO_GUARD i pominie atrakcje).
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
        // --- end guardian assignment ---

        // route: 1 lub 2
        int route = rand_int(1, 2);
        group.route = route;

        log.log_ts("GUIDE",
                   "GROUP_START guide=" + std::to_string(guide_id) +
                   " gid=" + std::to_string(gid) +
                   " route=" + std::to_string(route));

        // segment sleep (x1.5 jeśli jest dziecko <12)
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
            group.begin_step(s);
            for (auto* t : members) t->set_step(s);
            group.wait_step_done();
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
            maybe_signal1(); // ewakuacja może wystąpić „w trakcie” wieży

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
        // zakończ grupę (bezpiecznie: czekamy aż wszyscy wyjdą z wątku turysty)
        group.begin_step(Step::EXIT);
        for (auto* t : members) t->set_step(Step::EXIT);
        group.wait_step_done();

        log.log_ts("GUIDE", "GROUP_END guide=" + std::to_string(guide_id) + " gid=" + std::to_string(gid));
    }

    log.log_ts("GUIDE", "STOP guide=" + std::to_string(guide_id));
}
