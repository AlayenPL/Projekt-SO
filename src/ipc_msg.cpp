#include "ipc_msg.hpp"

#include <cerrno>
#include <cstdio>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <unistd.h>

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

int SysVMessageQueue::create_or_open(const char* token_path, int proj_id, int perms) {
    if (ensure_token_file(token_path) < 0) return -1;

    key_t key = ftok(token_path, proj_id);
    if (key == (key_t)-1) {
        perror("ftok");
        return -1;
    }

    int q = msgget(key, IPC_CREAT | perms);
    if (q < 0) {
        perror("msgget");
        return -1;
    }
    msqid_ = q;
    return 0;
}

int SysVMessageQueue::remove() {
    if (msqid_ < 0) return 0;
    if (msgctl(msqid_, IPC_RMID, nullptr) < 0) {
        perror("msgctl(IPC_RMID)");
        return -1;
    }
    msqid_ = -1;
    return 0;
}

int SysVMessageQueue::send_req(int tourist_id, int tourist_pid) {
    if (msqid_ < 0) {
        errno = EINVAL;
        perror("msgsnd(req): invalid msqid");
        return -1;
    }
    BridgeReqMsg msg;
    msg.mtype = 1;
    msg.kind = (int32_t)BridgeMsgKind::REQ_CROSS;
    msg.tourist_id = tourist_id;
    msg.tourist_pid = tourist_pid;

    if (msgsnd(msqid_, &msg, sizeof(BridgeReqMsg) - sizeof(long), 0) < 0) {
        perror("msgsnd(req)");
        return -1;
    }
    return 0;
}

int SysVMessageQueue::recv_req(BridgeReqMsg* out) {
    if (msqid_ < 0) {
        errno = EINVAL;
        perror("msgrcv(req): invalid msqid");
        return -1;
    }
    BridgeReqMsg msg;
    if (msgrcv(msqid_, &msg, sizeof(BridgeReqMsg) - sizeof(long), 1, 0) < 0) {
        perror("msgrcv(req)");
        return -1;
    }
    if (out) *out = msg;
    return 0;
}

int SysVMessageQueue::send_done(int tourist_id, int tourist_pid) {
    if (msqid_ < 0) {
        errno = EINVAL;
        perror("msgsnd(done): invalid msqid");
        return -1;
    }
    BridgeResMsg msg;
    msg.mtype = (long)tourist_pid;
    msg.kind = (int32_t)BridgeMsgKind::RES_DONE;
    msg.tourist_id = tourist_id;

    if (msgsnd(msqid_, &msg, sizeof(BridgeResMsg) - sizeof(long), 0) < 0) {
        perror("msgsnd(done)");
        return -1;
    }
    return 0;
}

int SysVMessageQueue::recv_done(int tourist_id, int tourist_pid) {
    if (msqid_ < 0) {
        errno = EINVAL;
        perror("msgrcv(done): invalid msqid");
        return -1;
    }
    (void)tourist_id;
    BridgeResMsg msg;
    if (msgrcv(msqid_, &msg, sizeof(BridgeResMsg) - sizeof(long), (long)tourist_pid, 0) < 0) {
        perror("msgrcv(done)");
        return -1;
    }
    return 0;
}
