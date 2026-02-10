// config.hpp
#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>

struct Config {
    int tourists_total = 30;
    int N = 100;          // max per day
    int M = 5;            // group size
    int P = 2;            // guides
    int X1 = 3;           // bridge cap
    int X2 = 8;           // tower cap
    int X3 = 7;           // ferry cap

    int segment_min_ms = 200;
    int segment_max_ms = 400;
    int bridge_min_ms  = 150;
    int bridge_max_ms  = 300;
    int tower_min_ms   = 200;
    int tower_max_ms   = 400;
    int ferry_T_ms     = 250;

    double signal1_prob = 0.1;
    double signal2_prob = 0.05;
    double vip_prob     = 0.1;

    int status_port = -1;
    unsigned int seed = 1234;

    /**
     * @brief Parse command-line arguments into a Config.
     *
     * Recognises flags like --tourists, --N, --M, --P, --X1..X3, duration ranges,
     * signal probabilities, vip probability, status port and seed.
     *
     * @param argc argument count from main
     * @param argv argument vector from main
     * @return filled Config instance
     */
    static Config from_args(int argc, char** argv);

    /**
     * @brief Validate constraints or throw std::runtime_error with description.
     */
    void validate_or_throw() const;
};
