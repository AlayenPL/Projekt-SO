// main.cpp - IPC3: SIGUSR1 evacuation + FIFO log server + sem+shm+msg bridge
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

// --- helper to read --key=value ---
static const char* get_arg(int argc, char** argv, const char* key) {
    size_t klen = std::strlen(key);
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], key, klen) == 0 && argv[i][klen] == '=') return argv[i] + klen + 1;
    }
    return nullptr;
}

static bool has_prefix(const char* s, const char* prefix) {
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

static int parse_nonneg_int_arg(int argc, char** argv, const char* key, int default_val) {
    const char* v = get_arg(argc, argv, key);
    if (!v) return default_val;
    errno = 0;
    char* end = nullptr;
    long x = std::strtol(v, &end, 10);
    if (errno != 0 || end == v || *end != '\0' || x < 0 || x > 1000000) {
        std::cerr << "Invalid value for " << key << ": " << v << "\n";
        return default_val;
    }
    return (int)x;
}

static std::vector<std::string> build_exec_args(const char* self, const char* role, int id, int argc, char** argv) {
    std::vector<std::string> out;
    out.reserve((size_t)argc + 8);
    out.emplace_back(self);
    out.emplace_back(std::string("--role=") + role);
    if (id >= 0) out.emplace_back(std::string("--id=") + std::to_string(id));
    for (int i = 1; i < argc; ++i) {
        if (has_prefix(argv[i], "--role=")) continue;
        if (has_prefix(argv[i], "--id=")) continue;
        out.emplace_back(argv[i]);
    }
    return out;
}

static std::vector<char*> to_exec_argv(std::vector<std::string>& args) {
    std::vector<char*> out;
    out.reserve(args.size() + 1);
    for (auto& s : args) out.push_back(const_cast<char*>(s.c_str()));
    out.push_back(nullptr);
    return out;
}

// ===== FIFO logging =====
static const char* FIFO_PATH = "/tmp/park_sim.fifo";
static const char* LOG_PATH  = "/tmp/park_simulation.log";

// Open FIFO for writing (non-blocking). If server isn't ready, fall back to stderr.
static void fifo_log_line(const char* tag, int id) {
    char line[256];
    int n = std::snprintf(line, sizeof(line), "[%s] pid=%d id=%d\n", tag, (int)getpid(), id);
    if (n <= 0) return;

    int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        // fallback: stderr
        (void)!write(STDERR_FILENO, line, (size_t)n);
        return;
    }
    (void)!write(fd, line, (size_t)n);
    close(fd);
}

// Log server reads FIFO and appends to a log file using syscalls open/read/write/close.
static int run_log_server() {
    // Create FIFO if missing
    if (mkfifo(FIFO_PATH, 0600) < 0) {
        if (errno != EEXIST) { perror("mkfifo"); return 1; }
    }

    int fifo_fd = open(FIFO_PATH, O_RDONLY);
    if (fifo_fd < 0) { perror("open(fifo read)"); return 1; }

    // Open log file
    int log_fd = open(LOG_PATH, O_CREAT | O_WRONLY | O_APPEND, 0600);
    if (log_fd < 0) { perror("open(log)"); close(fifo_fd); return 1; }

    char buf[512];
    while (true) {
        ssize_t r = read(fifo_fd, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("read(fifo)");
            break;
        }
        if (r == 0) {
            // All writers closed; keep FIFO open by reopening to continue service
            close(fifo_fd);
            fifo_fd = open(FIFO_PATH, O_RDONLY);
            if (fifo_fd < 0) { perror("open(fifo read)"); break; }
            continue;
        }
        if (write(log_fd, buf, (size_t)r) < 0) {
            perror("write(log)");
            break;
        }
    }

    close(log_fd);
    close(fifo_fd);
    return 0;
}

// ===== IPC tokens =====
static const char* SEM_CAP_TOKEN  = "/tmp/park_sim.semcap";
static const int   SEM_CAP_PROJ   = 0x11;

static const char* SEM_MTX_TOKEN  = "/tmp/park_sim.semmtx";
static const int   SEM_MTX_PROJ   = 0x12;

static const char* SHM_TOKEN      = "/tmp/park_sim.shmkey";
static const int   SHM_PROJ       = 0x21;

static const char* MSG_TOKEN      = "/tmp/park_sim.msgkey";
static const int   MSG_PROJ       = 0x31;

static std::vector<pid_t> g_children;
static volatile sig_atomic_t g_evacuate = 0;

static void on_sigint(int) { g_evacuate = 1; }
static void on_sigusr1(int) { g_evacuate = 1; }

static void kill_children(int sig) {
    for (pid_t p : g_children) if (p > 0) kill(p, sig);
}

static void stats_inc(SysVSharedMemory& shm, SysVSemaphore& mtx, void (*mut)(SharedStats*)) {
    if (mtx.down() < 0) return;
    auto* s = (SharedStats*)shm.attach();
    if (s) {
        mut(s);
        shm.detach();
    }
    mtx.up();
}

// --- bridge server ---
static int run_bridge_server(const Config&) {
    SysVMessageQueue mq;
    if (mq.create_or_open(MSG_TOKEN, MSG_PROJ, 0600) < 0) return 1;

    SysVSharedMemory shm;
    if (shm.create_or_open(SHM_TOKEN, SHM_PROJ, sizeof(SharedStats), 0600) < 0) return 1;

    SysVSemaphore mtx;
    if (mtx.create_or_open(SEM_MTX_TOKEN, SEM_MTX_PROJ, 1, 0600) < 0) return 1;

    while (true) {
        BridgeReqMsg req;
        if (mq.recv_req(&req) < 0) return 1;

        fifo_log_line("bridge:begin", req.tourist_id);
        usleep(150 * 1000);

        stats_inc(shm, mtx, [](SharedStats* s){ s->bridge_crossings++; });

        usleep(150 * 1000);
        fifo_log_line("bridge:end", req.tourist_id);

        if (mq.send_done(req.tourist_id, req.tourist_pid) < 0) return 1;
    }
    return 0;
}

// --- tourist ---
static int run_tourist_process(int id, const Config&) {
    // Capacity gate
    SysVSemaphore cap;
    if (cap.create_or_open(SEM_CAP_TOKEN, SEM_CAP_PROJ, 1, 0600) < 0) return 1;

    // Stats shm + mutex
    SysVSharedMemory shm;
    if (shm.create_or_open(SHM_TOKEN, SHM_PROJ, sizeof(SharedStats), 0600) < 0) return 1;

    SysVSemaphore mtx;
    if (mtx.create_or_open(SEM_MTX_TOKEN, SEM_MTX_PROJ, 1, 0600) < 0) return 1;

    // Bridge queue
    SysVMessageQueue mq;
    if (mq.create_or_open(MSG_TOKEN, MSG_PROJ, 0600) < 0) return 1;

    if (cap.down() < 0) return 1;
    fifo_log_line("tourist-enter", id);
    stats_inc(shm, mtx, [](SharedStats* s){ s->tourists_entered++; });

    if (mq.send_req(id, (int)getpid()) < 0) return 1;
    if (mq.recv_done(id, (int)getpid()) < 0) return 1;

    usleep(200 * 1000);

    stats_inc(shm, mtx, [](SharedStats* s){ s->tourists_exited++; });
    fifo_log_line("tourist-exit", id);

    if (cap.up() < 0) return 1;
    return 0;
}

// --- guide ---
static int run_guide_process(int id, const Config&) {
    fifo_log_line("guide", id);
    usleep(400 * 1000);
    return 0;
}

int main(int argc, char** argv) {
    const char* role = get_arg(argc, argv, "--role");
    const char* id_s = get_arg(argc, argv, "--id");

    int guides_total = parse_nonneg_int_arg(argc, argv, "--guides", 0);

    Config cfg = Config::from_args(argc, argv);
    int capacity = parse_nonneg_int_arg(argc, argv, "--capacity", cfg.tourists_total);
    if (capacity <= 0) capacity = 1;

    // Child roles
    if (role && std::strcmp(role, "log") == 0) return run_log_server();
    if (role && std::strcmp(role, "bridge") == 0) return run_bridge_server(cfg);

    if (role && std::strcmp(role, "tourist") == 0) {
        if (!id_s) { const char msg[] = "tourist role requires --id\n"; (void)!write(2, msg, sizeof(msg)-1); return 2; }
        return run_tourist_process(std::atoi(id_s), cfg);
    }
    if (role && std::strcmp(role, "guide") == 0) {
        if (!id_s) { const char msg[] = "guide role requires --id\n"; (void)!write(2, msg, sizeof(msg)-1); return 2; }
        return run_guide_process(std::atoi(id_s), cfg);
    }

    // MAIN
    std::cout << "[MAIN] start (SIGUSR1 evacuation + FIFO logger + sem+shm+msg bridge)\n";

    // Install signal handlers (SIGINT, SIGUSR1)
    struct sigaction sa{};
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, nullptr) < 0) perror("sigaction(SIGINT)");

    struct sigaction sb{};
    sb.sa_handler = on_sigusr1;
    sigemptyset(&sb.sa_mask);
    if (sigaction(SIGUSR1, &sb, nullptr) < 0) perror("sigaction(SIGUSR1)");

    // Create IPC objects (owner responsibilities)
    SysVSemaphore cap;
    if (cap.create_or_open(SEM_CAP_TOKEN, SEM_CAP_PROJ, capacity, 0600) < 0) return 1;

    SysVSemaphore mtx;
    if (mtx.create_or_open(SEM_MTX_TOKEN, SEM_MTX_PROJ, 1, 0600) < 0) return 1;

    SysVSharedMemory shm;
    if (shm.create_or_open(SHM_TOKEN, SHM_PROJ, sizeof(SharedStats), 0600) < 0) return 1;

    SysVMessageQueue mq;
    if (mq.create_or_open(MSG_TOKEN, MSG_PROJ, 0600) < 0) return 1;

    // Spawn helper processes: log server first (so writers can open FIFO), then bridge
    std::vector<pid_t> children;

    auto spawn_role = [&](const char* r, int id) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return (pid_t)-1; }
        if (pid == 0) {
            auto args = build_exec_args(argv[0], r, id, argc, argv);
            auto cargv = to_exec_argv(args);
            execv(cargv[0], cargv.data());
            perror("execv");
            _exit(127);
        }
        children.push_back(pid);
        g_children.push_back(pid);
        return pid;
    };

    pid_t log_pid = spawn_role("log", 0);
    (void)log_pid;

    pid_t bridge_pid = spawn_role("bridge", 0);

    for (int i = 0; i < guides_total; ++i) spawn_role("guide", i);
    for (int i = 0; i < cfg.tourists_total; ++i) spawn_role("tourist", i);

    // Main loop: wait for tourists+guides; if SIGUSR1 arrives => evacuation
    // children order: log, bridge, guides..., tourists...
    // Wait for guides+tourists while monitoring evacuation flag via EINTR.
    for (size_t i = 2; i < children.size(); ++i) {
        int status = 0;
        while (true) {
            pid_t w = waitpid(children[i], &status, 0);
            if (w < 0) {
                if (errno == EINTR) {
                    if (g_evacuate) break;
                    continue;
                }
                perror("waitpid");
                break;
            }
            break;
        }
        if (g_evacuate) break;
    }

    if (g_evacuate) {
        // Mark evacuation in shared stats
        if (mtx.down() == 0) {
            auto* s = (SharedStats*)shm.attach();
            if (s) {
                s->evacuations++;
                s->evacuation_on = 1;
                shm.detach();
            }
            mtx.up();
        }

        std::cout << "[MAIN] evacuation triggered (SIGUSR1/SIGINT) -> terminating children\n";
        kill_children(SIGTERM);
    }

    // Ensure bridge/log are stopped
    if (bridge_pid > 0) kill(bridge_pid, SIGTERM);
    if (children.size() >= 1) kill(children[0], SIGTERM); // log

    // Reap all children
    for (pid_t p : children) {
        int st = 0;
        while (waitpid(p, &st, 0) < 0) {
            if (errno == EINTR) continue;
            break;
        }
    }

    // Print final stats
    if (mtx.down() == 0) {
        auto* s = (SharedStats*)shm.attach();
        if (s) {
            std::cout << "[STATS] entered=" << s->tourists_entered
                      << " exited=" << s->tourists_exited
                      << " bridge_crossings=" << s->bridge_crossings
                      << " evacuations=" << s->evacuations
                      << "\n";
            shm.detach();
        }
        mtx.up();
    }

    // Cleanup IPC
    mq.remove();
    shm.remove();
    cap.remove();
    mtx.remove();

    // Remove token files + FIFO
    unlink(SEM_CAP_TOKEN);
    unlink(SEM_MTX_TOKEN);
    unlink(SHM_TOKEN);
    unlink(MSG_TOKEN);
    unlink(FIFO_PATH);

    std::cout << "[MAIN] done. Log file: " << LOG_PATH << "\n";
    return 0;
}
