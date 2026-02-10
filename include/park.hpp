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

    /**
     * @brief Construct park with resources configured and bound to logger.
     */
    Park(const Config& cfg, Logger& log);

    // Thread lifecycle
    /**
     * @brief Start cashier and guide threads.
     */
    void start();
    /**
     * @brief Stop simulation threads and wake any waiting queues.
     */
    void stop();

    // Random helpers
    /**
     * @brief Uniform integer in [lo, hi].
     */
    int rand_int(int lo, int hi);
    /**
     * @brief Uniform double in [0,1).
     */
    double rand01();

    // Queues
    /**
     * @brief Enqueue a tourist for cashier admission (VIP priority).
     */
    void enqueue_entry(Tourist* t);
    /**
     * @brief Dequeue next tourist for the cashier (blocks until available).
     */
    Tourist* dequeue_for_cashier();

    /**
     * @brief Enqueue a tourist waiting to form a guided group.
     */
    void enqueue_group_wait(Tourist* t);
    /**
     * @brief Dequeue exactly M tourists to form a group; blocks until enough.
     */
    std::vector<Tourist*> dequeue_group(int M);

    /**
     * @brief Report that a tourist exited; cashier thread logs exits.
     */
    void report_exit(int tourist_id);

    // KROK SYMULACJI: wykonanie Step przez turystę (guided)
    /**
     * @brief Execute one simulation step for a guided tourist.
     * @param t tourist instance
     * @param s step to execute
     * @param epoch synchronization epoch inside the group
     */
    void do_step(Tourist* t, Step s, int epoch);

private:
    /**
     * @brief Cashier thread loop handling entry and exit logging.
     */
    void cashier_loop();
    /**
     * @brief Guide thread loop forming groups and driving route steps.
     */
    void guide_loop(int guide_id);
};
