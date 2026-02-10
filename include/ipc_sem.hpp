#pragma once
#include <sys/types.h>

class SysVSemaphore {
public:
    SysVSemaphore() = default;
    ~SysVSemaphore() = default;

    /**
     * @brief Create or open a System V semaphore.
     *
     * Uses ftok on @p token_path with @p proj_id to obtain the key. Creates the
     * semaphore with @p initial_value if it does not exist, otherwise opens the
     * existing one.
     *
     * @param token_path path to a token file used by ftok
     * @param proj_id project id for ftok
     * @param initial_value initial semaphore value when created
     * @param perms permission bits (default 0600)
     * @return 0 on success, -1 on error (errno set)
     */
    int create_or_open(const char* token_path, int proj_id, int initial_value, int perms = 0600);

    /**
     * @brief Decrement (P) the semaphore.
     * @return 0 on success, -1 on error (errno set)
     */
    int down();

    /**
     * @brief Increment (V) the semaphore.
     * @return 0 on success, -1 on error (errno set)
     */
    int up();

    /**
     * @brief Remove the semaphore (IPC_RMID).
     * @return 0 on success, -1 on error
     */
    int remove();

    /**
     * @brief Get the semaphore id.
     */
    int id() const { return semid_; }

private:
    int semid_ = -1;
};
