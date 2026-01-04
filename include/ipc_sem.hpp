#pragma once
#include <sys/types.h>

// Simple SysV semaphore wrapper for a single counting semaphore.
// - Uses SEM_UNDO on operations (safer on abnormal process exit).
// - Minimal permissions (0600 by default).
//
// NOTE: This is intentionally small and syscall-oriented to satisfy project requirements.
class SysVSemaphore {
public:
    SysVSemaphore() = default;
    ~SysVSemaphore() = default;

    // Create (or open existing) semaphore set with 1 semaphore.
    // If create==true, attempts IPC_CREAT|IPC_EXCL first, and initializes the value.
    // token_path is used with ftok(token_path, proj_id).
    int create_or_open(const char* token_path, int proj_id, int initial_value, int perms = 0600);

    // Decrement (P). Blocks if value is 0. Uses SEM_UNDO.
    int down();

    // Increment (V). Uses SEM_UNDO.
    int up();

    // Remove semaphore set (IPC_RMID). Typically called by main/owner process.
    int remove();

    int id() const { return semid_; }

private:
    int semid_ = -1;
};
