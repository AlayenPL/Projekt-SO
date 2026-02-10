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

    /**
     * @brief Construct tourist with id/age/VIP and owning park pointer.
     */
    Tourist(int id_, int age_, bool vip_, Park* park_);

    /**
     * @brief Start the tourist thread.
     */
    void start();
    /**
     * @brief Join the tourist thread.
     */
    void join();

    /**
     * @brief Notify the tourist that cashier admitted them.
     */
    void on_admitted();
    /**
     * @brief Notify the tourist that cashier rejected them.
     */
    void on_rejected();

    // Group assignment (tu ustawiamy gid/pid – to jest miejsce, gdzie i tak to robisz)
    /**
     * @brief Assign group id and guide id once grouped.
     */
    void assign_to_group(int gid, int pid);

    /**
     * @brief Set the next step for this tourist (used by group control).
     */
    void set_step(Step s);

    /**
     * @brief Assign a guardian; marks missing guardian for children.
     */
    void set_guardian(Tourist* g, bool is_u5_child);
    /**
     * @brief Guardian signals wards they are ready for the epoch.
     */
    void guardian_notify_wards_ready(int epoch);
    /**
     * @brief Child waits until guardian ready or abort flag.
     */
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

    /**
     * @brief Thread body entry point.
     */
    void run();
    /**
     * @brief VIP tour flow (unguided).
     */
    void run_vip();
    /**
     * @brief Guided tour flow (wait for group and follow guide steps).
     */
    void run_guided();
};
