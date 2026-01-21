#pragma once
#include <sys/types.h>

// Simple SysV semaphore wrapper for a single counting semaphore.
// - Uses SEM_UNDO on operations.
// - Minimal permissions (0600 by default).
class SysVSemaphore {
public:
    SysVSemaphore() = default;
    ~SysVSemaphore() = default;

    // Create or open (1 semaphore). Initializes to initial_value if created.
    // token_path is used with ftok(token_path, proj_id).
    int create_or_open(const char* token_path, int proj_id, int initial_value, int perms = 0600);

    // P (down): blocks if value == 0
    int down();

    // V (up)
    int up();

    // Remove semaphore set (IPC_RMID)
    int remove();

    int id() const { return semid_; }

private:
    int semid_ = -1;
};
