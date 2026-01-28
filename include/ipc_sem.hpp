#pragma once
#include <sys/types.h>

class SysVSemaphore {
public:
    SysVSemaphore() = default;
    ~SysVSemaphore() = default;

    int create_or_open(const char* token_path, int proj_id, int initial_value, int perms = 0600);
    int down();
    int up();
    int remove();

    int id() const { return semid_; }

private:
    int semid_ = -1;
};
