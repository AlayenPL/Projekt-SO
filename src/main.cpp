// src/main.cpp – IPC3 FINAL (clean shutdown)
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
    r.reserve(v.size() + 1);
    for (auto& s : v) r.push_back(const_cast<char*>(s.c_str()));
    r.push_back(nullptr);
    return r;
}

/* ===================== globals & signals ===================== */

static volatile sig_atomic_t g_stop = 0;      // for servers (SIGTERM)
static volatile sig_atomic_t g_evacuate = 0;  // for main (SIGUSR1/SIGINT)

static void sigterm_handler(int) { g_stop = 1; }
static void on_evacuate(int) { g_evacuate = 1; }

/* ===================== FIFO logger ===================== */

static const char* FIFO_PATH = "/tmp/park_sim.fifo";
static const char* LOG_PATH  = "/tmp/park_simulation.log";

static void fifo_log(const char* tag, int id) {
    char buf[220];
    int n = std::snprintf(buf, sizeof(buf),
        "[%s] pid=%d id=%d\n", tag, (int)getpid(), id);
    if (n <= 0) return;

    int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        // fallback to stderr (atomic-ish for small lines)
        (void)!write(2, buf, (size_t)n);
        return;
    }
    (void)!write(fd, buf, (size_t)n);
    close(fd);
}

static int run_log_server() {
    // SIGTERM => set g_stop, let loop exit
    struct sigaction sa{};
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM, &sa, nullptr) < 0) {
        perror("sigaction(SIGTERM)");
        return 1;
    }

    if (mkfifo(FIFO_PATH, 0600) < 0 && errno != EEXIST) {
        perror("mkfifo");
        return 1;
    }

    // Open FIFO read end; if interrupted, retry; if stopping, exit cleanly.
    int fifo = -1;
    while (!g_stop) {
        fifo = open(FIFO_PATH, O_RDONLY);
        if (fifo >= 0) break;
        if (errno == EINTR) continue;
        perror("open fifo");
        return 1;
    }
    if (g_stop) return 0;

    int logf = open(LOG_PATH, O_CREAT | O_WRONLY | O_APPEND, 0600);
    if (logf < 0) { perror("open log"); close(fifo); return 1; }

    char buf[512];
    while (!g_stop) {
        ssize_t r = read(fifo, buf, sizeof(buf));
        if (r > 0) {
            if (write(logf, buf, (size_t)r) < 0) {
                if (errno == EINTR) continue;
                perror("write log");
                break;
            }
        } else if (r == 0) {
            // all writers closed; reopen FIFO unless stopping
            close(fifo);
            fifo = -1;
            while (!g_stop) {
                fifo = open(FIFO_PATH, O_RDONLY);
                if (fifo >= 0) break;
                if (errno == EINTR) continue;
                perror("open fifo");
                g_stop = 1;
                break;
            }
        } else { // r < 0
            if (errno == EINTR) continue;
            perror("read fifo");
            break;
        }
    }

    if (fifo >= 0) close(fifo);
    close(logf);
    return 0;
}

/* ===================== IPC tokens ===================== */

static const char* SEM_CAP_TOKEN = "/tmp/park_sim.semcap";
static const char* SEM_MTX_TOKEN = "/tmp/park_sim.semmtx";
static const char* SHM_TOKEN     = "/tmp/park_sim.shmkey";
static const char* MSG_TOKEN     = "/tmp/park_sim.msgkey";

/* ===================== bridge ===================== */

static int run_bridge() {
    struct sigaction sa{};
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM, &sa, nullptr) < 0) {
        perror("sigaction(SIGTERM)");
        return 1;
    }

    SysVMessageQueue mq;
    SysVSharedMemory shm;
    SysVSemaphore mtx;

    if (mq.create_or_open(MSG_TOKEN, 0x31, 0600) < 0) return 1;
    if (shm.create_or_open(SHM_TOKEN, 0x21, sizeof(SharedStats), 0600) < 0) return 1;
    if (mtx.create_or_open(SEM_MTX_TOKEN, 0x12, 1, 0600) < 0) return 1;

    while (!g_stop) {
        BridgeReqMsg req{};
        if (mq.recv_req(&req) < 0) {
            // If we are stopping and syscall was interrupted => exit quietly.
            if (errno == EINTR && g_stop) break;
            if (errno == EINTR) continue;
            // Real error:
            perror("msgrcv(req)");
            break;
        }

        fifo_log("bridge:begin", req.tourist_id);
        usleep(100000);

        if (mtx.down() == 0) {
            auto* s = (SharedStats*)shm.attach();
            if (s) { s->bridge_crossings++; shm.detach(); }
            mtx.up();
        }

        fifo_log("bridge:end", req.tourist_id);

        if (mq.send_done(req.tourist_id, req.tourist_pid) < 0) {
            if (errno == EINTR && g_stop) break;
            if (errno == EINTR) continue;
            perror("msgsnd(done)");
            break;
        }
    }

    return 0;
}

/* ===================== tourist / guide ===================== */

static int run_tourist(int id) {
    SysVSemaphore cap, mtx;
    SysVSharedMemory shm;
    SysVMessageQueue mq;

    if (cap.create_or_open(SEM_CAP_TOKEN, 0x11, 1, 0600) < 0) return 1;
    if (mtx.create_or_open(SEM_MTX_TOKEN, 0x12, 1, 0600) < 0) return 1;
    if (shm.create_or_open(SHM_TOKEN, 0x21, sizeof(SharedStats), 0600) < 0) return 1;
    if (mq.create_or_open(MSG_TOKEN, 0x31, 0600) < 0) return 1;

    if (cap.down() < 0) return 1;
    fifo_log("tourist-enter", id);

    if (mtx.down() == 0) {
        auto* s = (SharedStats*)shm.attach();
        if (s) { s->tourists_entered++; shm.detach(); }
        mtx.up();
    }

    if (mq.send_req(id, getpid()) < 0) return 1;

    // If main evacuates and sends SIGTERM, tourist will likely die anyway,
    // but in normal end this returns cleanly.
    if (mq.recv_done(id, getpid()) < 0) return 1;

    usleep(150000);

    if (mtx.down() == 0) {
        auto* s = (SharedStats*)shm.attach();
        if (s) { s->tourists_exited++; shm.detach(); }
        mtx.up();
    }

    fifo_log("tourist-exit", id);
    if (cap.up() < 0) return 1;
    return 0;
}

static int run_guide(int id) {
    fifo_log("guide", id);
    usleep(200000);
    return 0;
}

/* ===================== MAIN ===================== */

static void safe_kill(pid_t p, int sig) {
    if (p > 0) kill(p, sig);
}

int main(int argc, char** argv) {
    const char* role = get_arg(argc, argv, "--role");
    const char* id_s = get_arg(argc, argv, "--id");

    // Child roles
    if (role) {
        if (!std::strcmp(role, "log"))    return run_log_server();
        if (!std::strcmp(role, "bridge")) return run_bridge();
        if (!std::strcmp(role, "tourist")) {
            if (!id_s) { const char m[]="tourist needs --id\n"; (void)!write(2,m,sizeof(m)-1); return 2; }
            return run_tourist(std::atoi(id_s));
        }
        if (!std::strcmp(role, "guide")) {
            if (!id_s) { const char m[]="guide needs --id\n"; (void)!write(2,m,sizeof(m)-1); return 2; }
            return run_guide(std::atoi(id_s));
        }
    }

    // MAIN mode (no --role=)
    int tourists = parse_nonneg(argc, argv, "--tourists", 1);
    int guides   = parse_nonneg(argc, argv, "--guides", 0);
    int capacity = parse_nonneg(argc, argv, "--capacity", tourists);
    if (capacity <= 0) capacity = 1;

    // evacuation signals
    struct sigaction ev{};
    ev.sa_handler = on_evacuate;
    sigemptyset(&ev.sa_mask);
    sigaction(SIGUSR1, &ev, nullptr);
    sigaction(SIGINT,  &ev, nullptr);

    // Create IPC objects (owner)
    SysVSemaphore cap, mtx;
    SysVSharedMemory shm;
    SysVMessageQueue mq;

    if (cap.create_or_open(SEM_CAP_TOKEN, 0x11, capacity, 0600) < 0) return 1;
    if (mtx.create_or_open(SEM_MTX_TOKEN, 0x12, 1, 0600) < 0) return 1;
    if (shm.create_or_open(SHM_TOKEN, 0x21, sizeof(SharedStats), 0600) < 0) return 1;
    if (mq.create_or_open(MSG_TOKEN, 0x31, 0600) < 0) return 1;

    std::cout << "[MAIN] start (SIGUSR1 evacuation + FIFO logger + sem+shm+msg)\n";

    // Spawn children
    pid_t log_pid = -1, bridge_pid = -1;
    std::vector<pid_t> work_pids; // ONLY tourists+guides

    auto spawn = [&](const char* r, int id) -> pid_t {
        pid_t p = fork();
        if (p < 0) { perror("fork"); return -1; }
        if (p == 0) {
            auto a = build_exec_args(argv[0], r, id, argc, argv);
            auto c = to_exec_argv(a);
            execv(c[0], c.data());
            perror("execv");
            _exit(127);
        }
        return p;
    };

    log_pid    = spawn("log", 0);
    bridge_pid = spawn("bridge", 0);

    for (int i = 0; i < guides; ++i) {
        pid_t p = spawn("guide", i);
        if (p > 0) work_pids.push_back(p);
    }
    for (int i = 0; i < tourists; ++i) {
        pid_t p = spawn("tourist", i);
        if (p > 0) work_pids.push_back(p);
    }

    // Wait for work processes
    for (pid_t p : work_pids) {
        int st = 0;
        while (waitpid(p, &st, 0) < 0) {
            if (errno == EINTR) {
                if (g_evacuate) break;
                continue;
            }
            perror("waitpid");
            break;
        }
        if (g_evacuate) break;
    }

    if (g_evacuate) {
        std::cout << "[MAIN] evacuation triggered -> SIGTERM work processes\n";
        for (pid_t p : work_pids) safe_kill(p, SIGTERM);

        // reap remaining work processes
        for (pid_t p : work_pids) {
            int st = 0;
            while (waitpid(p, &st, 0) < 0) {
                if (errno == EINTR) continue;
                break;
            }
        }
    }

    // Stop servers
    safe_kill(bridge_pid, SIGTERM);
    safe_kill(log_pid, SIGTERM);

    // Reap servers
    for (pid_t p : {bridge_pid, log_pid}) {
        if (p <= 0) continue;
        int st = 0;
        while (waitpid(p, &st, 0) < 0) {
            if (errno == EINTR) continue;
            break;
        }
    }

    // Print stats
    if (mtx.down() == 0) {
        auto* s = (SharedStats*)shm.attach();
        if (s) {
            std::cout << "[STATS] entered=" << s->tourists_entered
                      << " exited=" << s->tourists_exited
                      << " bridge=" << s->bridge_crossings << "\n";
            shm.detach();
        }
        mtx.up();
    }

    // Cleanup IPC AFTER children are gone
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

