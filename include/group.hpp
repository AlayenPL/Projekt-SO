#pragma once

#include <condition_variable>
#include <mutex>
#include <vector>

#include "tourist.hpp"

struct GroupControl {
    int group_id;
    int guide_id;

    int route = 1;

    std::mutex mu;
    std::condition_variable cv;

    Step current = Step::NONE;
    bool step_active = false;
    int completed = 0;

    std::vector<Tourist*> members;

    // ---- Bridge gate (GO_A) ----
    int bridge_epoch_done = 0;
    bool bridge_in_progress = false;
    int bridge_coordinator_id = -1;
    std::condition_variable bridge_cv;

    // ---- Tower gate (GO_B) ----
    int tower_epoch_done = 0;
    bool tower_in_progress = false;
    int tower_coordinator_id = -1;
    std::condition_variable tower_cv;

    // ---- Ferry gate (GO_C) ----
    int ferry_epoch_done = 0;
    bool ferry_in_progress = false;
    int ferry_coordinator_id = -1;
    std::condition_variable ferry_cv;

    /**
     * @brief Construct group control for given group and guide ids.
     */
    GroupControl(int gid, int pid) : group_id(gid), guide_id(pid) {}

    // Preferuj dorosłego (>=15). Jeśli nie ma — bierz najniższe id.
    /**
     * @brief Choose coordinator tourist id (adult preferred, then lowest id).
     */
    int pick_coordinator_id() const {
        int best_adult = -1;
        int best_any = -1;

        for (auto* t : members) {
            if (!t) continue;
            if (best_any < 0 || t->id < best_any) best_any = t->id;

            if (t->age >= 15) {
                if (best_adult < 0 || t->id < best_adult) best_adult = t->id;
            }
        }
        return (best_adult >= 0) ? best_adult : best_any;
    }

    /**
     * @brief Begin a group step, resetting per-step coordination state.
     */
    void begin_step(Step s) {
        std::unique_lock<std::mutex> lk(mu);
        current = s;
        completed = 0;
        step_active = true;

        if (s == Step::GO_A) {
            bridge_in_progress = false;
            bridge_coordinator_id = pick_coordinator_id();
        }
        if (s == Step::GO_B) {
            tower_in_progress = false;
            tower_coordinator_id = pick_coordinator_id();
        }
        if (s == Step::GO_C) {
            ferry_in_progress = false;
            ferry_coordinator_id = pick_coordinator_id();
        }

        cv.notify_all();
        bridge_cv.notify_all();
        tower_cv.notify_all();
        ferry_cv.notify_all();
    }

    /**
     * @brief Mark current member as done with the step.
     */
    void mark_done() {
        std::unique_lock<std::mutex> lk(mu);
        completed++;
        if (completed >= static_cast<int>(members.size())) {
            step_active = false;
            cv.notify_all();
        }
    }

    /**
     * @brief Block until all members finished the step.
     */
    void wait_step_done() {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&]{ return !step_active; });
    }

    // ---- Bridge gate ----
    /**
     * @brief Try to become bridge coordinator for this epoch.
     */
    bool bridge_try_become_coordinator(int epoch, int tourist_id) {
        std::unique_lock<std::mutex> lk(mu);
        if (bridge_epoch_done >= epoch) return false;
        if (bridge_in_progress) return false;
        if (tourist_id != bridge_coordinator_id) return false;
        bridge_in_progress = true;
        return true;
    }

    /**
     * @brief Signal that bridge crossing is finished for this epoch.
     */
    void bridge_finish(int epoch) {
        std::unique_lock<std::mutex> lk(mu);
        bridge_epoch_done = epoch;
        bridge_in_progress = false;
        bridge_cv.notify_all();
    }

    /**
     * @brief Wait until bridge epoch is completed by coordinator.
     */
    void bridge_wait_done(int epoch) {
        std::unique_lock<std::mutex> lk(mu);
        bridge_cv.wait(lk, [&]{ return bridge_epoch_done >= epoch; });
    }

    // ---- Tower gate ----
    /**
     * @brief Try to become tower coordinator for this epoch.
     */
    bool tower_try_become_coordinator(int epoch, int tourist_id) {
        std::unique_lock<std::mutex> lk(mu);
        if (tower_epoch_done >= epoch) return false;
        if (tower_in_progress) return false;
        if (tourist_id != tower_coordinator_id) return false;
        tower_in_progress = true;
        return true;
    }

    /**
     * @brief Signal tower visit finished for this epoch.
     */
    void tower_finish(int epoch) {
        std::unique_lock<std::mutex> lk(mu);
        tower_epoch_done = epoch;
        tower_in_progress = false;
        tower_cv.notify_all();
    }

    /**
     * @brief Wait until tower epoch is completed by coordinator.
     */
    void tower_wait_done(int epoch) {
        std::unique_lock<std::mutex> lk(mu);
        tower_cv.wait(lk, [&]{ return tower_epoch_done >= epoch; });
    }

    // ---- Ferry gate ----
    /**
     * @brief Try to become ferry coordinator for this epoch.
     */
    bool ferry_try_become_coordinator(int epoch, int tourist_id) {
        std::unique_lock<std::mutex> lk(mu);
        if (ferry_epoch_done >= epoch) return false;
        if (ferry_in_progress) return false;
        if (tourist_id != ferry_coordinator_id) return false;
        ferry_in_progress = true;
        return true;
    }

    /**
     * @brief Signal ferry crossing finished for this epoch.
     */
    void ferry_finish(int epoch) {
        std::unique_lock<std::mutex> lk(mu);
        ferry_epoch_done = epoch;
        ferry_in_progress = false;
        ferry_cv.notify_all();
    }

    /**
     * @brief Wait until ferry epoch is completed by coordinator.
     */
    void ferry_wait_done(int epoch) {
        std::unique_lock<std::mutex> lk(mu);
        ferry_cv.wait(lk, [&]{ return ferry_epoch_done >= epoch; });
    }
};
