#include "ipc_shm.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <unistd.h>

/**
 * @brief Ensure token file exists for ftok usage.
 */
static int ensure_token_file(const char* path) {
    int fd = open(path, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) { perror("open(token)"); return -1; }
    const char b = 'x';
    if (write(fd, &b, 1) < 0) { perror("write(token)"); close(fd); return -1; }
    if (close(fd) < 0) { perror("close(token)"); return -1; }
    return 0;
}

/**
 * @brief Create or open shared memory segment; zero-initialize when created.
 */
int SysVSharedMemory::create_or_open(const char* token_path, int proj_id, size_t size_bytes, int perms) {
    if (ensure_token_file(token_path) < 0) return -1;
    size_ = size_bytes;

    key_t key = ftok(token_path, proj_id);
    if (key == (key_t)-1) { perror("ftok"); return -1; }

    int shmid = shmget(key, size_bytes, IPC_CREAT | IPC_EXCL | perms);
    bool created = false;
    if (shmid >= 0) created = true;
    else {
        if (errno != EEXIST) { perror("shmget(create)"); return -1; }
        shmid = shmget(key, size_bytes, perms);
        if (shmid < 0) { perror("shmget(open)"); return -1; }
    }

    shmid_ = shmid;

    if (created) {
        void* a = shmat(shmid_, nullptr, 0);
        if (a == (void*)-1) { perror("shmat(init)"); return -1; }
        std::memset(a, 0, size_bytes);
        if (shmdt(a) < 0) { perror("shmdt(init)"); return -1; }
    }
    return 0;
}

/**
 * @brief Attach to the shared memory segment.
 */
void* SysVSharedMemory::attach() {
    if (shmid_ < 0) { errno = EINVAL; perror("shmat: invalid shmid"); return (void*)0; }
    void* a = shmat(shmid_, nullptr, 0);
    if (a == (void*)-1) { perror("shmat"); return (void*)0; }
    addr_ = a;
    return addr_;
}

/**
 * @brief Detach from the shared memory segment.
 */
int SysVSharedMemory::detach() {
    if (addr_ && addr_ != (void*)-1) {
        if (shmdt(addr_) < 0) { perror("shmdt"); return -1; }
    }
    addr_ = (void*)0;
    return 0;
}

/**
 * @brief Remove the shared memory segment.
 */
int SysVSharedMemory::remove() {
    if (shmid_ < 0) return 0;
    if (shmctl(shmid_, IPC_RMID, nullptr) < 0) { perror("shmctl(IPC_RMID)"); return -1; }
    shmid_ = -1;
    return 0;
}
