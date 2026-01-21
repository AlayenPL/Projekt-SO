#pragma once
#include <cstdint>

// SysV message queue types for a simple "bridge server" resource.
enum class BridgeMsgKind : int32_t {
    REQ_CROSS = 1,   // tourist -> bridge
    RES_DONE  = 2    // bridge -> tourist
};

struct BridgeReqMsg {
    long mtype;          // 1 for requests
    int32_t kind;        // BridgeMsgKind::REQ_CROSS
    int32_t tourist_id;  // logical id
    int32_t tourist_pid; // pid for reply routing
};

struct BridgeResMsg {
    long mtype;          // tourist_pid so tourist can msgrcv(type=pid)
    int32_t kind;        // BridgeMsgKind::RES_DONE
    int32_t tourist_id;
};

class SysVMessageQueue {
public:
    SysVMessageQueue() = default;
    ~SysVMessageQueue() = default;

    int create_or_open(const char* token_path, int proj_id, int perms = 0600);
    int remove();

    int send_req(int tourist_id, int tourist_pid);
    int recv_req(BridgeReqMsg* out);

    int send_done(int tourist_id, int tourist_pid);
    int recv_done(int tourist_id, int tourist_pid);

    int id() const { return msqid_; }

private:
    int msqid_ = -1;
};
