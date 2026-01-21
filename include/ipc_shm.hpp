#pragma once
#include <cstdint>
#include <cstddef>

// Shared statistics stored in SysV shared memory.
struct SharedStats {
    uint64_t tourists_entered;
    uint64_t tourists_exited;
    uint64_t bridge_crossings;
};

class SysVSharedMemory {
public:
    SysVSharedMemory() = default;
    ~SysVSharedMemory() = default;

    // Create or open a shm segment of given size. Initializes to zero if created.
    int create_or_open(const char* token_path, int proj_id, size_t size_bytes, int perms = 0600);

    // Attach and return pointer.
    void* attach();

    // Detach currently attached address (if any).
    int detach();

    // Mark segment for removal (IPC_RMID). Typically by owner (main).
    int remove();

    int id() const { return shmid_; }

private:
    int shmid_ = -1;
    void* addr_ = (void*)0;
    size_t size_ = 0;
};
