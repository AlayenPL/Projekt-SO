// main.cpp - Step IPC1: fork+exec + forward args + guide role + SysV semaphore capacity gate
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
    out.reserve((size_t)argc + 4);

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

static const char* SEM_TOKEN_PATH = "/tmp/park_sim.semkey";
static const int   SEM_PROJ_ID    = 0x42;

static void atomic_log_line(const char* role, int id) {
    char buf[200];
    int n = std::snprintf(buf, sizeof(buf),
                          "[%s] pid=%d id=%d\n",
                          role, (int)getpid(), id);
    if (n > 0) (void)!write(STDERR_FILENO, buf, (size_t)n);
}

static int run_tourist_process(int id, const Config& cfg) {
    (void)cfg;

    SysVSemaphore sem;
    if (sem.create_or_open(SEM_TOKEN_PATH, SEM_PROJ_ID, 1, 0600) < 0) {
        return 1;
    }

    if (sem.down() < 0) return 1;

    atomic_log_line("tourist-enter", id);

    // TODO: plug existing Tourist logic here

    atomic_log_line("tourist-exit", id);

    if (sem.up() < 0) return 1;

    return 0;
}

static int run_guide_process(int id, const Config& cfg) {
    (void)cfg;
    atomic_log_line("guide", id);
    // TODO: plug existing Guide logic here
    return 0;
}

static std::vector<pid_t> g_children;

static void on_sigint(int) {
    for (pid_t p : g_children) {
        if (p > 0) kill(p, SIGTERM);
    }
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

    std::cout << "[MAIN] starting simulation (process mode + SysV semaphore gate)\n";

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, nullptr) < 0) {
        perror("sigaction(SIGINT)");
    }

    SysVSemaphore sem;
    if (sem.create_or_open(SEM_TOKEN_PATH, SEM_PROJ_ID, capacity, 0600) < 0) {
        return 1;
    }

    std::vector<pid_t> children;
    children.reserve((size_t)cfg.tourists_total + (size_t)guides_total);

    auto spawn_role = [&](const char* r, int id) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return;
        }
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

    for (int i = 0; i < guides_total; ++i) spawn_role("guide", i);
    for (int i = 0; i < cfg.tourists_total; ++i) spawn_role("tourist", i);

    for (pid_t c : children) {
        int status = 0;
        if (waitpid(c, &status, 0) < 0) {
            perror("waitpid");
        }
    }

    sem.remove();
    if (unlink(SEM_TOKEN_PATH) < 0) {
        perror("unlink(sem token)");
    }

    std::cout << "[MAIN] all child processes finished\n";
    return 0;
}
