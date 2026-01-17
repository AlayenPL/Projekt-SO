#pragma once

#include <condition_variable>
#include <mutex>
#include <vector>

#include "tourist.hpp"

struct GroupControl {
    int group_id;
    int guide_id;

    std::mutex mu;
    std::condition_variable cv;

    Step current = Step::NONE;
    bool step_active = false;
    int completed = 0;

    std::vector<Tourist*> members;

    GroupControl(int gid, int pid) : group_id(gid), guide_id(pid) {}

    void begin_step(Step s) {
        std::unique_lock<std::mutex> lk(mu);
        current = s;
        completed = 0;
        step_active = true;
        cv.notify_all();
    }

    void mark_done() {
        std::unique_lock<std::mutex> lk(mu);
        completed++;
        if (completed >= static_cast<int>(members.size())) {
            step_active = false;
            cv.notify_all();
        }
    }

    void wait_step_done() {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [&]{ return !step_active; });
    }
};
