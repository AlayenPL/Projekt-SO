// main.cpp – IPC3 FIXED
// SIGUSR1 evacuation + FIFO logger + SysV sem/shm/msg
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdio>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "config.hpp"
#include "ipc_sem.hpp"
#include "ipc_shm.hpp"
#include "ipc_msg.hpp"

/* ===================== helpers ===================== */

static const char* get_arg(int argc, char** argv, const char* key) {
    size_t klen = std::strlen(key);
    for (int i = 1; i < argc; ++i)
        if (std::strncmp(argv[i], key, klen) == 0 && argv[i][klen] == '=')
            return argv[i] + klen + 1;
    return nullptr;
}

static bool has_prefix(const char* s, const char* p) {
    return std::strncmp(s, p, std::strlen(p)) == 0;
}

static int parse_nonneg(int argc, char** argv, const char* key, int def) {
    const char* v = get_arg(argc, argv, key);
    if (!v) return def;
    errno = 0;
    char* e = nullptr;
    long x = std::strtol(v, &e, 10);
    if (errno || e == v || *e || x < 0) return def;
    return (int)x;
}

static std::vector<std::string> build_exec_args(
    const char* self, const char* role, int id, int argc, char** argv
) {
    std::vector<std::string> out;
    out.emplace_back(self);
    out.emplace_back(std::string("--role=") + role);
    if (id >= 0) out.emplace_back("--id=" + std::to_string(id));
    for (int i = 1; i < argc; ++i) {
        if (has_prefix(argv[i], "--role=")) continue;
        if (has_prefix(argv[i], "--id=")) continue;
        out.emplace_back(argv[i]);
    }
    return out;
}

static std::vector<char*> to_exec_argv(std::vector<std::string>& v) {
    std::vector<char*> r;
    for (auto& s : v) r.push_back(const_cast<char*>(s.c_str()));
    r.push_back(nullptr);
    return r;
}

/* ===================== FIFO logger ===================== */

static const char* FIFO_PATH = "/tmp/park_sim.fifo";
static const char* LOG_PATH  = "/tmp/park_simulation.log";
static volatile sig_atomic_t g_stop = 0;

static void fifo_log(const char* tag, int id) {
    char buf[200];
    int n = std::snprintf(buf, sizeof(buf),
        "[%s] pid=%d id=%d\n", tag, (int)getpid(), id);
    if (n <= 0) return;

    int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        write(2, buf, (size_t)n);
        return;
    }
    write(fd, buf, (size_t)n);
    close(fd);
}

static void sigterm_handler(int) { g_stop = 1; }

static int run_log_server() {
    signal(SIGTERM, sigterm_handler);

    if (mkfifo(FIFO_PATH, 0600) < 0 && errno != EEXIST) {
        perror("mkfifo");
        return 1;
    }

    int fifo = open(FIFO_PATH, O_RDONLY);
    if (fifo < 0) { perror("open fifo"); return 1; }

    int logf = open(LOG_PATH, O_CREAT | O_WRONLY | O_APPEND, 0600);
    if (logf < 0) { perror("open log"); return 1; }

    char buf[512];
    while (!g_stop) {
        ssize_t r = read(fifo, buf, sizeof(buf));
        if (r > 0) write(logf, buf, (size_t)r);
        else if (r < 0 && errno != EINTR) break;
    }

    close(logf);
    close(fifo);
    return 0;
}

/* ===================== IPC tokens ===================== */

static const char* SEM_CAP_TOKEN = "/tmp/park_sim.semcap";
static const char* SEM_MTX_TOKEN = "/tmp/park_sim.semmtx";
static const char* SHM_TOKEN     = "/tmp/park_sim.shmkey";
static const char* MSG_TOKEN     = "/tmp/park_sim.msgkey";

/* ===================== bridge ===================== */

static int run_bridge() {
    signal(SIGTERM, sigterm_handler);

    SysVMessageQueue mq;
    SysVSharedMemory shm;
    SysVSemaphore mtx;

    mq.create_or_open(MSG_TOKEN, 0x31, 0600);
    shm.create_or_open(SHM_TOKEN, 0x21, sizeof(SharedStats), 0600);
    mtx.create_or_open(SEM_MTX_TOKEN, 0x12, 1, 0600);

    while (!g_stop) {
        BridgeReqMsg req;
        if (mq.recv_req(&req) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        fifo_log("bridge:begin", req.tourist_id);
        usleep(100000);

        mtx.down();
        auto* s = (SharedStats*)shm.attach();
        if (s) { s->bridge_crossings++; shm.detach(); }
        mtx.up();

        fifo_log("bridge:end", req.tourist_id);
        mq.send_done(req.tourist_id, req.tourist_pid);
    }
    return 0;
}

/* ===================== tourist / guide ===================== */

static int run_tourist(int id) {
    SysVSemaphore cap, mtx;
    SysVSharedMemory shm;
    SysVMessageQueue mq;

    cap.create_or_open(SEM_CAP_TOKEN, 0x11, 1, 0600);
    mtx.create_or_open(SEM_MTX_TOKEN, 0x12, 1, 0600);
    shm.create_or_open(SHM_TOKEN, 0x21, sizeof(SharedStats), 0600);
    mq.create_or_open(MSG_TOKEN, 0x31, 0600);

    if (cap.down() < 0) return 1;
    fifo_log("tourist-enter", id);

    mtx.down();
    auto* s = (SharedStats*)shm.attach();
    if (s) { s->tourists_entered++; shm.detach(); }
    mtx.up();

    mq.send_req(id, getpid());
    mq.recv_done(id, getpid());

    usleep(150000);

    mtx.down();
    s = (SharedStats*)shm.attach();
    if (s) { s->tourists_exited++; shm.detach(); }
    mtx.up();

    fifo_log("tourist-exit", id);
    cap.up();
    return 0;
}

static int run_guide(int id) {
    fifo_log("guide", id);
    usleep(200000);
    return 0;
}

/* ===================== MAIN ===================== */

static volatile sig_atomic_t g_evacuate = 0;
static void on_evacuate(int) { g_evacuate = 1; }

int main(int argc, char** argv) {
    const char* role = get_arg(argc, argv, "--role");
    const char* id_s = get_arg(argc, argv, "--id");

    if (role) {
        if (!std::strcmp(role, "log"))    return run_log_server();
        if (!std::strcmp(role, "bridge")) return run_bridge();
        if (!std::strcmp(role, "tourist")) return run_tourist(std::atoi(id_s));
        if (!std::strcmp(role, "guide"))   return run_guide(std::atoi(id_s));
    }

    int tourists = parse_nonneg(argc, argv, "--tourists", 1);
    int guides   = parse_nonneg(argc, argv, "--guides", 0);
    int capacity = parse_nonneg(argc, argv, "--capacity", tourists);

    signal(SIGUSR1, on_evacuate);
    signal(SIGINT,  on_evacuate);

    SysVSemaphore cap, mtx;
    SysVSharedMemory shm;
    SysVMessageQueue mq;

    cap.create_or_open(SEM_CAP_TOKEN, 0x11, capacity, 0600);
    mtx.create_or_open(SEM_MTX_TOKEN, 0x12, 1, 0600);
    shm.create_or_open(SHM_TOKEN, 0x21, sizeof(SharedStats), 0600);
    mq.create_or_open(MSG_TOKEN, 0x31, 0600);

    std::vector<pid_t> children;

    auto spawn = [&](const char* r, int id) {
        pid_t p = fork();
        if (p == 0) {
            auto a = build_exec_args(argv[0], r, id, argc, argv);
            auto c = to_exec_argv(a);
            execv(c[0], c.data());
            _exit(127);
        }
        children.push_back(p);
    };

    spawn("log", 0);
    spawn("bridge", 0);
    for (int i = 0; i < guides; ++i) spawn("guide", i);
    for (int i = 0; i < tourists; ++i) spawn("tourist", i);

    for (pid_t p : children) {
        int st;
        while (waitpid(p, &st, 0) < 0 && errno == EINTR) {
            if (g_evacuate) break;
        }
    }

    if (g_evacuate) {
        std::cout << "[MAIN] evacuation triggered\n";
        for (pid_t p : children) kill(p, SIGTERM);
        for (pid_t p : children) waitpid(p, nullptr, 0);
    }

    mtx.down();
    auto* s = (SharedStats*)shm.attach();
    if (s) {
        std::cout << "[STATS] entered=" << s->tourists_entered
                  << " exited=" << s->tourists_exited
                  << " bridge=" << s->bridge_crossings << "\n";
        shm.detach();
    }
    mtx.up();

    mq.remove();
    shm.remove();
    cap.remove();
    mtx.remove();

    unlink(FIFO_PATH);
    unlink(SEM_CAP_TOKEN);
    unlink(SEM_MTX_TOKEN);
    unlink(SHM_TOKEN);
    unlink(MSG_TOKEN);

    std::cout << "[MAIN] done. log=" << LOG_PATH << "\n";
    return 0;
}
