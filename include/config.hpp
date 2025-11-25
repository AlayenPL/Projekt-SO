#pragma once

#include <cstdint>
#include <string>

struct Config {
    // Park hours simulated as total duration in milliseconds.
    // We model Tp=0 and Tk=duration_ms.
    int64_t duration_ms = 30000; // 30s

    int N = 60;   // max entries per day
    int M = 6;    // group size
    int P = 2;    // guides

    int X1 = 4;   // bridge capacity (X1 < M)
    int X2 = 10;  // tower capacity (X2 < 2M)
    int X3 = 8;   // ferry capacity (X3 < 1.5M)

    int ferry_T_ms = 900; // crossing time one way

    // Arrivals
    int tourists_total = 80;       // how many tourists are spawned (arrivals), may be > N
    int arrival_jitter_ms = 500;   // max random delay between consecutive arrivals

    // Random seed
    uint32_t seed = 12345;

    // Signals
    double signal1_prob = 0.10; // probability per group before leaving tower
    double signal2_prob = 0.05; // probability per group before each next segment

    // Times (base) in ms
    int segment_min_ms = 400;
    int segment_max_ms = 1200;

    int bridge_min_ms = 400;
    int bridge_max_ms = 1000;

    int tower_min_ms = 700;
    int tower_max_ms = 1500;

    static Config from_args(int argc, char** argv);
    static void print_help(const char* prog);
};
