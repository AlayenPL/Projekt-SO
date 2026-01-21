// main.cpp - IPC2 fixed: semaphore gate + shared memory stats + message-queue bridge server
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

#include "config.hpp"
#include "ipc_sem.hpp"
#include "ipc_shm.hpp"
#include "ipc_msg.hpp"

static const char* get_arg(int argc, char** argv, const char* key) {
    size_t klen = std::strlen(key);
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], key, klen) == 0 && argv[i][klen] == '=') {
            return argv[i] + klen + 1;
        }
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

static std::vector<std::string> build_exec_args(
    const char* self,
    const char* role,
    int id,
    int argc,
    char** argv
) {
    std::vector<std::string> out;
    out.reserve((size_t)argc + 6);

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

static void atomic_log_line(const char* tag, int id) {
    char buf[220];
    int n = std::snprintf(buf, sizeof(buf),
                          "[%s] pid=%d id=%d\n",
                          tag, (int)getpid(), id);
    if (n > 0) (void)!write(STDERR_FILENO, buf, (size_t)n);
}

// Token files (ftok); created as 0600, removed by main at end.
static const char* SEM_CAP_TOKEN  = "/tmp/park_sim.semcap";
static const int   SEM_CAP_PROJ   = 0x11;

static const char* SEM_MTX_TOKEN  = "/tmp/park_sim.semmtx";
static const int   SEM_MTX_PROJ   = 0x12;

static const char* SHM_TOKEN      = "/tmp/park_sim.shmkey";
static const int   SHM_PROJ       = 0x21;

static const char* MSG_TOKEN      = "/tmp/park_sim.msgkey";
static const int   MSG_PROJ       = 0x31;

static std::vector<pid_t> g_children;

static void on_sigint(int) {
    for (pid_t p : g_children) {
        if (p > 0) kill(p, SIGTERM);
    }
}

// Update shared stats with mutex semaphore
static void stats_inc(SysVSharedMemory& shm, SysVSemaphore& mtx, void (*mut)(SharedStats*)) {
    if (mtx.down() < 0) return;
    auto* s = (SharedStats*)shm.attach();
    if (s) {
        mut(s);
        shm.detach();
    }
    mtx.up();
}

// --- bridge server: processes requests sequentially ---
static int run_bridge_server(const Config& cfg) {
    (void)cfg;

    SysVMessageQueue mq;
    if (mq.create_or_open(MSG_TOKEN, MSG_PROJ, 0600) < 0) return 1;

    SysVSharedMemory shm;
    if (shm.create_or_open(SHM_TOKEN, SHM_PROJ, sizeof(SharedStats), 0600) < 0) return 1;

    SysVSemaphore mtx;
    if (mtx.create_or_open(SEM_MTX_TOKEN, SEM_MTX_PROJ, 1, 0600) < 0) return 1;

    while (true) {
        BridgeReqMsg req;
        if (mq.recv_req(&req) < 0) return 1;

        atomic_log_line("bridge:begin", req.tourist_id);
        usleep(150 * 1000);

        stats_inc(shm, mtx, [](SharedStats* s){ s->bridge_crossings++; });

        usleep(150 * 1000);
        atomic_log_line("bridge:end", req.tourist_id);

        if (mq.send_done(req.tourist_id, req.tourist_pid) < 0) return 1;
    }
    return 0;
}

static int run_tourist_process(int id, const Config& cfg) {
    (void)cfg;

    SysVSemaphore cap;
    if (cap.create_or_open(SEM_CAP_TOKEN, SEM_CAP_PROJ, 1, 0600) < 0) return 1;

    SysVSharedMemory shm;
    if (shm.create_or_open(SHM_TOKEN, SHM_PROJ, sizeof(SharedStats), 0600) < 0) return 1;

    SysVSemaphore mtx;
    if (mtx.create_or_open(SEM_MTX_TOKEN, SEM_MTX_PROJ, 1, 0600) < 0) return 1;

    SysVMessageQueue mq;
    if (mq.create_or_open(MSG_TOKEN, MSG_PROJ, 0600) < 0) return 1;

    if (cap.down() < 0) return 1;
    atomic_log_line("tourist-enter", id);
    stats_inc(shm, mtx, [](SharedStats* s){ s->tourists_entered++; });

    if (mq.send_req(id, (int)getpid()) < 0) return 1;
    if (mq.recv_done(id, (int)getpid()) < 0) return 1;

    usleep(200 * 1000);

    stats_inc(shm, mtx, [](SharedStats* s){ s->tourists_exited++; });
    atomic_log_line("tourist-exit", id);

    if (cap.up() < 0) return 1;

    return 0;
}

static int run_guide_process(int id, const Config& cfg) {
    (void)cfg;
    atomic_log_line("guide", id);
    usleep(400 * 1000);
    return 0;
}

int main(int argc, char **argv) {
    const char* role = get_arg(argc, argv, "--role");
    const char* id_s = get_arg(argc, argv, "--id");

    int guides_total = parse_nonneg_int_arg(argc, argv, "--guides", 0);

    Config cfg = Config::from_args(argc, argv);
    int capacity = parse_nonneg_int_arg(argc, argv, "--capacity", cfg.tourists_total);
    if (capacity <= 0) capacity = 1;

    if (role && std::strcmp(role, "tourist") == 0) {
        if (!id_s) {
            const char msg[] = "tourist role requires --id\n";
            (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
            return 2;
        }
        return run_tourist_process(std::atoi(id_s), cfg);
    }

    if (role && std::strcmp(role, "guide") == 0) {
        if (!id_s) {
            const char msg[] = "guide role requires --id\n";
            (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
            return 2;
        }
        return run_guide_process(std::atoi(id_s), cfg);
    }

    if (role && std::strcmp(role, "bridge") == 0) {
        return run_bridge_server(cfg);
    }

    std::cout << "[MAIN] start (semaphore gate + shared stats + message-queue bridge)\n";

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, nullptr) < 0) perror("sigaction(SIGINT)");

    SysVSemaphore cap;
    if (cap.create_or_open(SEM_CAP_TOKEN, SEM_CAP_PROJ, capacity, 0600) < 0) return 1;

    SysVSemaphore mtx;
    if (mtx.create_or_open(SEM_MTX_TOKEN, SEM_MTX_PROJ, 1, 0600) < 0) return 1;

    SysVSharedMemory shm;
    if (shm.create_or_open(SHM_TOKEN, SHM_PROJ, sizeof(SharedStats), 0600) < 0) return 1;

    SysVMessageQueue mq;
    if (mq.create_or_open(MSG_TOKEN, MSG_PROJ, 0600) < 0) return 1;

    std::vector<pid_t> children;
    children.reserve((size_t)cfg.tourists_total + (size_t)guides_total + 1);

    auto spawn_role = [&](const char* r, int id) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return; }
        if (pid == 0) {
            auto args = build_exec_args(argv[0], r, id, argc, argv);
            auto cargv = to_exec_argv(args);
            execv(cargv[0], cargv.data());
            perror("execv");
            _exit(127);
        }
        children.push_back(pid);
        g_children.push_back(pid);
    };

    spawn_role("bridge", 0);
    for (int i = 0; i < guides_total; ++i) spawn_role("guide", i);
    for (int i = 0; i < cfg.tourists_total; ++i) spawn_role("tourist", i);

    // Wait for non-bridge processes (bridge is children[0])
    for (size_t i = 1; i < children.size(); ++i) {
        int status = 0;
        if (waitpid(children[i], &status, 0) < 0) perror("waitpid");
    }

    // Stop bridge
    if (!children.empty()) {
        kill(children[0], SIGTERM);
        int st = 0;
        waitpid(children[0], &st, 0);
    }

    // Print final stats
    if (mtx.down() == 0) {
        auto* s = (SharedStats*)shm.attach();
        if (s) {
            std::cout << "[STATS] entered=" << s->tourists_entered
                      << " exited=" << s->tourists_exited
                      << " bridge_crossings=" << s->bridge_crossings
                      << "\n";
            shm.detach();
        }
        mtx.up();
    }

    mq.remove();
    shm.remove();
    cap.remove();
    mtx.remove();

    unlink(SEM_CAP_TOKEN);
    unlink(SEM_MTX_TOKEN);
    unlink(SHM_TOKEN);
    unlink(MSG_TOKEN);

    std::cout << "[MAIN] done\n";
    return 0;
}
