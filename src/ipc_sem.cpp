#include "ipc_sem.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef SEMUN_DEFINED
// Some libcs don't declare union semun in headers.
union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
    struct seminfo* __buf;
};
#define SEMUN_DEFINED 1
#endif

static int ensure_token_file(const char* path) {
    int fd = open(path, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) {
        perror("open(token)");
        return -1;
    }
    const char b = 'x';
    if (write(fd, &b, 1) < 0) {
        perror("write(token)");
        close(fd);
        return -1;
    }
    if (close(fd) < 0) {
        perror("close(token)");
        return -1;
    }
    return 0;
}

int SysVSemaphore::create_or_open(const char* token_path, int proj_id, int initial_value, int perms) {
    if (ensure_token_file(token_path) < 0) return -1;

    key_t key = ftok(token_path, proj_id);
    if (key == (key_t)-1) {
        perror("ftok");
        return -1;
    }

    int semid = semget(key, 1, IPC_CREAT | IPC_EXCL | perms);
    if (semid >= 0) {
        union semun arg;
        arg.val = initial_value;
        if (semctl(semid, 0, SETVAL, arg) < 0) {
            perror("semctl(SETVAL)");
            semctl(semid, 0, IPC_RMID);
            return -1;
        }
        semid_ = semid;
        return 0;
    }

    if (errno != EEXIST) {
        perror("semget(IPC_CREAT|IPC_EXCL)");
        return -1;
    }

    semid = semget(key, 1, perms);
    if (semid < 0) {
        perror("semget(open)");
        return -1;
    }

    semid_ = semid;
    return 0;
}

int SysVSemaphore::down() {
    if (semid_ < 0) {
        errno = EINVAL;
        perror("semop(down): invalid semid");
        return -1;
    }
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op  = -1;
    op.sem_flg = SEM_UNDO;
    if (semop(semid_, &op, 1) < 0) {
        perror("semop(down)");
        return -1;
    }
    return 0;
}

int SysVSemaphore::up() {
    if (semid_ < 0) {
        errno = EINVAL;
        perror("semop(up): invalid semid");
        return -1;
    }
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op  = +1;
    op.sem_flg = SEM_UNDO;
    if (semop(semid_, &op, 1) < 0) {
        perror("semop(up)");
        return -1;
    }
    return 0;
}

int SysVSemaphore::remove() {
    if (semid_ < 0) return 0;
    if (semctl(semid_, 0, IPC_RMID) < 0) {
        perror("semctl(IPC_RMID)");
        return -1;
    }
    semid_ = -1;
    return 0;
}
