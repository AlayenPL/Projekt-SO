#pragma once
#include <cstdint>

enum class BridgeMsgKind : int32_t { REQ_CROSS = 1, RES_DONE = 2 };

struct BridgeReqMsg {
    long mtype;
    int32_t kind;
    int32_t tourist_id;
    int32_t tourist_pid;
};

struct BridgeResMsg {
    long mtype;
    int32_t kind;
    int32_t tourist_id;
};

class SysVMessageQueue {
public:
    int create_or_open(const char* token_path, int proj_id, int perms = 0600);
    int remove();

    int reset_queue(const char* token_path, int proj_id, int perms = 0600);

    int send_req(int tourist_id, int tourist_pid);
    int recv_req(BridgeReqMsg* out);

    int send_done(int tourist_id, int tourist_pid);
    int recv_done(int tourist_id, int tourist_pid);

    int id() const { return msqid_; }

private:
    int msqid_ = -1;
};
