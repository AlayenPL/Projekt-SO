// main.cpp - Step C: fork + exec skeleton
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.hpp"

// --- helper to read --key=value ---
static const char* get_arg(int argc, char** argv, const char* key) {
    size_t klen = std::strlen(key);
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], key, klen) == 0 && argv[i][klen] == '=') {
            return argv[i] + klen + 1;
        }
    }
    return nullptr;
}

// --- tourist process entry ---
static int run_tourist_process(int id, const Config& cfg) {
    std::cerr << "[tourist] pid=" << getpid() << " id=" << id << "\n";
    // TODO: plug existing Tourist logic here
    return 0;
}

int main(int argc, char **argv) {
    const char* role = get_arg(argc, argv, "--role");
    const char* id_s = get_arg(argc, argv, "--id");

    Config cfg = Config::from_args(argc, argv);

    // ===== CHILD ROLE =====
    if (role && std::strcmp(role, "tourist") == 0) {
        if (!id_s) {
            std::cerr << "tourist role requires --id\n";
            return 2;
        }
        int id = std::atoi(id_s);
        return run_tourist_process(id, cfg);
    }

    // ===== MAIN ROLE =====
    std::cout << "[MAIN] starting simulation (process mode)\n";

    std::vector<pid_t> children;
    children.reserve(cfg.tourists_total);

    for (int i = 0; i < cfg.tourists_total; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            break;
        }

        if (pid == 0) {
            std::string id_arg = "--id=" + std::to_string(i);
            char* exec_argv[] = {
                argv[0],
                (char*)"--role=tourist",
                id_arg.data(),
                nullptr
            };
            execv(argv[0], exec_argv);
            perror("execv");
            _exit(127);
        }

        children.push_back(pid);
    }

    for (pid_t c : children) {
        int status;
        if (waitpid(c, &status, 0) < 0) {
            perror("waitpid");
        }
    }

    std::cout << "[MAIN] all tourist processes finished\n";
    return 0;
}
