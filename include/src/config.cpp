// config.cpp
#include "config.hpp"
#include <cstring>
#include <cstdlib>

Config Config::from_args(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--tourists=", 11) == 0) {
            cfg.tourists_total = std::atoi(argv[i] + 11);
        } else if (std::strncmp(argv[i], "--seed=", 7) == 0) {
            cfg.seed = std::strtoul(argv[i] + 7, nullptr, 10);
        }
    }
    return cfg;
}
