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
    /**
     * @brief Create or open a SysV message queue.
     *
     * @param token_path ftok token file
     * @param proj_id project id for ftok
     * @param perms permission bits (default 0600)
     * @return 0 on success, -1 on error
     */
    int create_or_open(const char* token_path, int proj_id, int perms = 0600);

    /**
     * @brief Remove the queue (IPC_RMID).
     * @return 0 on success, -1 on error
     */
    int remove();

    /**
     * @brief Force-drop all messages by removing and recreating the queue.
     */
    int reset_queue(const char* token_path, int proj_id, int perms = 0600);

    /**
     * @brief Send a bridge crossing request.
     */
    int send_req(int tourist_id, int tourist_pid);
    /**
     * @brief Receive the next bridge crossing request.
     */
    int recv_req(BridgeReqMsg* out);

    /**
     * @brief Send a completion notification to a specific tourist pid.
     */
    int send_done(int tourist_id, int tourist_pid);
    /**
     * @brief Receive the completion notification for this tourist pid.
     */
    int recv_done(int tourist_id, int tourist_pid);

    /**
     * @brief Get the queue id.
     */
    int id() const { return msqid_; }

private:
    int msqid_ = -1;
};
