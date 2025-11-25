#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

struct Park;
struct GroupControl;

enum class Step { NONE=0, GO_A=1, GO_B=2, GO_C=3, RETURN_K=4, EXIT=5 };

static inline const char* step_str(Step s) {
    switch (s) {
        case Step::NONE: return "NONE";
        case Step::GO_A: return "A";
        case Step::GO_B: return "B";
        case Step::GO_C: return "C";
        case Step::RETURN_K: return "K";
        case Step::EXIT: return "EXIT";
    }
    return "?";
}

struct Tourist {
    int id;
    int age;      // years
    bool vip;

    Park* park;

    // Admission
    std::mutex mu;
    std::condition_variable cv;
    bool admitted = false;
    bool rejected = false;

    // Group assignment
    int group_id = -1;
    int guide_id = -1;
    GroupControl* group = nullptr;

    // Control flow set by guide
    std::atomic<bool> abort_to_k{false};   // signal2
    std::atomic<bool> tower_evacuate{false}; // signal1

    // next step from guide
    Step next_step = Step::NONE;
    bool step_ready = false;

    std::thread thr;

    Tourist(int id, int age, bool vip, Park* park);
    void start();
    void join();

    // Called by cashier
    void on_admitted();
    void on_rejected();

    // Called by guide
    void assign_to_group(int group_id, int guide_id);
    void set_step(Step s);

private:
    void run();
    void run_vip();
    void run_guided();
};
