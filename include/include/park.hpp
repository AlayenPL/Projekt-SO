#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "config.hpp"
#include "logger.hpp"
#include "resources.hpp"
#include "tourist.hpp"   // Step + Tourist

struct Park {
    Config cfg;
    Logger& log;

    Bridge bridge;
    Tower tower;
    Ferry ferry;

    std::atomic<bool> open{true};
    std::atomic<int> entered{0};

    // Cashier entry queues (VIP has priority).
    std::mutex entry_mu;
    std::condition_variable entry_cv;
    std::deque<Tourist*> entry_vip;
    std::deque<Tourist*> entry_norm;

    // Queue for guided groups (non-VIP after entering).
    std::mutex group_mu;
    std::condition_variable group_cv;
    std::deque<Tourist*> group_wait;

    // Exit reports from guides/VIPs.
    std::mutex exit_mu;
    std::condition_variable exit_cv;
    std::deque<int> exit_ids;

    // Threads
    std::thread cashier_thr;
    std::vector<std::thread> guide_thrs;

    // Random
    std::mt19937 rng;
    std::mutex rng_mu;

    Park(const Config& cfg, Logger& log);

    // Thread lifecycle
    void start();
    void stop();

    // Random helpers
    int rand_int(int lo, int hi);
    double rand01();

    // Queues
    void enqueue_entry(Tourist* t);
    Tourist* dequeue_for_cashier();

    void enqueue_group_wait(Tourist* t);
    std::vector<Tourist*> dequeue_group(int M);

    void report_exit(int tourist_id);

    // KROK SYMULACJI: wykonanie Step przez turystę (guided)
    void do_step(Tourist* t, Step s, int epoch);

private:
    void cashier_loop();
    void guide_loop(int guide_id);
};
