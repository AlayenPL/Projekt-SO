#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "resources.hpp"

class Park;
struct GroupControl;

enum class Step {
    NONE,
    GO_A,
    GO_B,
    GO_C,
    RETURN_K,
    EXIT
};

class Tourist {
public:
    int id;
    int age;
    bool vip;

    Park* park;

    // Grupa / przewodnik
    int group_id = -1;
    int guide_id = -1;
    std::shared_ptr<GroupControl> group;

    // Spójne przypięcie grupy (bez dereferencji GroupControl w nagłówku!)
    void set_group(std::shared_ptr<GroupControl> g) {
        std::lock_guard<std::mutex> lk(mu);
        group = std::move(g);
        cv.notify_all();
    }

    Tourist* guardian = nullptr;
    std::atomic<bool> no_guard{false};
    std::atomic<bool> guardian_of_u5{false};

    std::atomic<bool> abort_to_k{false};
    std::atomic<bool> tower_evacuate{false};

    Tourist(int id_, int age_, bool vip_, Park* park_);

    void start();
    void join();

    void on_admitted();
    void on_rejected();

    // Group assignment (tu ustawiamy gid/pid – to jest miejsce, gdzie i tak to robisz)
    void assign_to_group(int gid, int pid);

    void set_step(Step s);

    void set_guardian(Tourist* g, bool is_u5_child);
    void guardian_notify_wards_ready(int epoch);
    void child_wait_for_guardian_ready(int epoch, const char* where_tag);

private:
    std::thread thr;

    std::mutex mu;
    std::condition_variable cv;

    bool admitted = false;
    bool rejected = false;

    Step next_step = Step::NONE;
    bool step_ready = false;
    int step_epoch = 0;

    std::mutex escort_mu;
    std::condition_variable escort_cv;
    int escort_epoch = 0;

    void run();
    void run_vip();
    void run_guided();
};
