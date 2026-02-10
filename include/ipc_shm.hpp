#pragma once
#include <cstdint>
#include <cstddef>

struct SharedStats {
    uint64_t tourists_entered;
    uint64_t tourists_exited;
    uint64_t bridge_crossings;
    uint64_t evacuations;   // number of evacuation events observed
    uint32_t evacuation_on; // 0/1 flag
};

class SysVSharedMemory {
public:
    SysVSharedMemory() = default;
    ~SysVSharedMemory() = default;

    /**
     * @brief Create or open a SysV shared memory segment.
     *
     * Initializes the segment to zeros when newly created.
     *
     * @param token_path path to ftok token file
     * @param proj_id project id for ftok
     * @param size_bytes size of the segment in bytes
     * @param perms permission bits (default 0600)
     * @return 0 on success, -1 on error
     */
    int create_or_open(const char* token_path, int proj_id, size_t size_bytes, int perms = 0600);

    /**
     * @brief Attach the shared memory and return its address.
     * @return pointer to mapped memory, or nullptr on error
     */
    void* attach();

    /**
     * @brief Detach the currently attached segment.
     * @return 0 on success, -1 on error
     */
    int detach();

    /**
     * @brief Remove the shared memory segment (IPC_RMID).
     * @return 0 on success, -1 on error
     */
    int remove();

    /**
     * @brief Get the shared memory id.
     */
    int id() const { return shmid_; }

private:
    int shmid_ = -1;
    void* addr_ = (void*)0;
    size_t size_ = 0;
};
