// config.hpp
#pragma once
#include <cstddef>

struct Config {
    int tourists_total = 5;
    unsigned int seed = 1234;

    /**
     * @brief Parse command-line arguments into a Config.
     *
     * Recognises --tourists=<int> and --seed=<uint>; falls back to defaults
     * if flags are missing or invalid.
     *
     * @param argc argument count from main
     * @param argv argument vector from main
     * @return filled Config instance
     */
    static Config from_args(int argc, char** argv);
};
