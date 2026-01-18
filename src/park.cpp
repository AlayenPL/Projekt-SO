#include "park.hpp"
#include "tourist.hpp"
#include "group.hpp"

#include <algorithm>
#include <sstream>

Park::Park(const Config& cfg_, Logger& log_)
    : cfg(cfg_), log(log_),
      bridge(cfg.X1, log_), tower(cfg.X2, log_), ferry(cfg.X3, log_),
      rng(cfg.seed) {}

int Park::rand_int(int lo, int hi) {
    std::lock_guard<std::mutex> lk(rng_mu);
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng);
}

double Park::rand01() {
    std::lock_guard<std::mutex> lk(rng_mu);
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(rng);
}

void Park::start() {
    cashier_thr = std::thread(&Park::cashier_loop, this);
    for (int i = 0; i < cfg.P; ++i)
        guide_thrs.emplace_back(&Park::guide_loop, this, i);
}

void Park::stop() {
    open.store(false);
    entry_cv.notify_all();
    group_cv.notify_all();
    exit_cv.notify_all();

    if (cashier_thr.joinable()) cashier_thr.join();
    for (auto& t : guide_thrs)
        if (t.joinable()) t.join();
}

void Park::enqueue_entry(Tourist* t) {
    std::lock_guard<std::mutex> lk(entry_mu);
    (t->vip ? entry_vip : entry_norm).push_back(t);
    entry_cv.notify_one();
}

Tourist* Park::dequeue_for_cashier() {
    std::unique_lock<std::mutex> lk(entry_mu);
    entry_cv.wait(lk, [&]{
        return !open || !entry_vip.empty() || !entry_norm.empty();
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
    std::lock_guard<std::mutex> lk(group_mu);
    group_wait.push_back(t);
    group_cv.notify_one();
}

std::vector<Tourist*> Park::dequeue_group(int M) {
    std::unique_lock<std::mutex> lk(group_mu);
    group_cv.wait(lk, [&]{
        return !open || static_cast<int>(group_wait.size()) >= M;
    });
    if (group_wait.size() < static_cast<size_t>(M)) return {};
    std::vector<Tourist*> g;
    for (int i = 0; i < M; ++i) {
        g.push_back(group_wait.front());
        group_wait.pop_front();
    }
    return g;
}

void Park::report_exit(int id) {
    std::lock_guard<std::mutex> lk(exit_mu);
    exit_ids.push_back(id);
    exit_cv.notify_one();
}

void Park::cashier_loop() {
    log.log_ts("CASHIER", "START");
    while (open) {
        Tourist* t = dequeue_for_cashier();
        if (!t) continue;

        if (entered.load() >= cfg.N) {
            log.log_ts("CASHIER", "REJECT id=" + std::to_string(t->id));
            t->on_rejected();
            continue;
        }

        entered++;
        log.log_ts("CASHIER", "ENTER id=" + std::to_string(t->id));
        t->on_admitted();
    }
    log.log_ts("CASHIER", "STOP");
}

void Park::guide_loop(int guide_id) {
    int seq = 0;
    log.log_ts("GUIDE", "START guide=" + std::to_string(guide_id));

    while (open) {
        auto members = dequeue_group(cfg.M);
        if (members.empty()) continue;

        int gid = guide_id * 100000 + seq++;
        GroupControl group(gid, guide_id);
        group.members = members;
        group.route = rand_int(1, 2);

        for (auto* t : members) {
            t->group = &group;
            t->assign_to_group(gid, guide_id);
        }

        std::vector<Tourist*> adults, children;
        for (auto* t : members)
            (t->age >= 15 ? adults : children).push_back(t);

        for (auto* c : children) {
            if (adults.empty()) {
                c->set_guardian(nullptr, c->age <= 5);
            } else {
                auto* g = adults[rand_int(0, adults.size() - 1)];
                c->set_guardian(g, c->age <= 5);
            }
        }

        log.log_ts("GUIDE",
            "GROUP_START guide=" + std::to_string(guide_id) +
            " gid=" + std::to_string(gid) +
            " route=" + std::to_string(group.route));

        auto step_all = [&](Step s) {
            group.begin_step(s);
            for (auto* t : members) t->set_step(s);
            group.wait_step_done();
        };

        if (group.route == 1) {
            step_all(Step::GO_A);
            step_all(Step::GO_B);
            step_all(Step::GO_C);
        } else {
            step_all(Step::GO_C);
            step_all(Step::GO_B);
            step_all(Step::GO_A);
        }

        step_all(Step::RETURN_K);
        step_all(Step::EXIT);

        log.log_ts("GUIDE", "GROUP_END guide=" +
            std::to_string(guide_id) + " gid=" + std::to_string(gid));
    }

    log.log_ts("GUIDE", "STOP guide=" + std::to_string(guide_id));
}
