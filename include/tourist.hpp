#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "resources.hpp"   // Direction + zasoby
// Step trzymamy w tym pliku – jest używany w Tourist i GroupControl.

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
    GroupControl* group = nullptr;

    // Opiekun (dla dzieci <15 w A/B/C) + blokada “opiekun dziecka <=5 nie wchodzi do wieży”
    Tourist* guardian = nullptr;
    std::atomic<bool> no_guard{false};
    std::atomic<bool> guardian_of_u5{false};

    // Flagi awaryjne
    std::atomic<bool> abort_to_k{false};
    std::atomic<bool> tower_evacuate{false};

    Tourist(int id_, int age_, bool vip_, Park* park_);

    void start();
    void join();

    // Cashier / park
    void on_admitted();
    void on_rejected();

    // Group assignment
    void assign_to_group(int gid, int pid);

    // Step dispatch
    void set_step(Step s);

    // Guardian logic
    void set_guardian(Tourist* g, bool is_u5_child);
    void guardian_notify_wards_ready(int epoch);
    void child_wait_for_guardian_ready(int epoch, const char* where_tag);

private:
    std::thread thr;

    std::mutex mu;
    std::condition_variable cv;

    bool admitted = false;
    bool rejected = false;

    // krok przewodnika
    Step next_step = Step::NONE;
    bool step_ready = false;
    int step_epoch = 0;

    // synchronizacja “opiekun gotowy”
    std::mutex escort_mu;
    std::condition_variable escort_cv;
    int escort_epoch = 0;

    void run();
    void run_vip();
    void run_guided();
};
