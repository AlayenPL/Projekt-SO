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

    int create_or_open(const char* token_path, int proj_id, size_t size_bytes, int perms = 0600);
    void* attach();
    int detach();
    int remove();

    int id() const { return shmid_; }

private:
    int shmid_ = -1;
    void* addr_ = (void*)0;
    size_t size_ = 0;
};
