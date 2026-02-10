// config.cpp
#include "config.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

Config Config::from_args(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        auto parse_int = [&](const char* key, int& out) {
            size_t klen = std::strlen(key);
            if (std::strncmp(argv[i], key, klen) == 0) {
                out = std::atoi(argv[i] + klen);
                return true;
            }
            return false;
        };
        auto parse_double = [&](const char* key, double& out) {
            size_t klen = std::strlen(key);
            if (std::strncmp(argv[i], key, klen) == 0) {
                out = std::atof(argv[i] + klen);
                return true;
            }
            return false;
        };

        if (parse_int("--tourists=", cfg.tourists_total)) continue;
        if (parse_int("--N=", cfg.N)) continue;
        if (parse_int("--M=", cfg.M)) continue;
        if (parse_int("--P=", cfg.P)) continue;
        if (parse_int("--X1=", cfg.X1)) continue;
        if (parse_int("--X2=", cfg.X2)) continue;
        if (parse_int("--X3=", cfg.X3)) continue;
        if (parse_int("--seg-min=", cfg.segment_min_ms)) continue;
        if (parse_int("--seg-max=", cfg.segment_max_ms)) continue;
        if (parse_int("--bridge-min=", cfg.bridge_min_ms)) continue;
        if (parse_int("--bridge-max=", cfg.bridge_max_ms)) continue;
        if (parse_int("--tower-min=", cfg.tower_min_ms)) continue;
        if (parse_int("--tower-max=", cfg.tower_max_ms)) continue;
        if (parse_int("--ferry-ms=", cfg.ferry_T_ms)) continue;
        if (parse_double("--signal1=", cfg.signal1_prob)) continue;
        if (parse_double("--signal2=", cfg.signal2_prob)) continue;
        if (parse_double("--vip-prob=", cfg.vip_prob)) continue;
        if (parse_int("--status-port=", cfg.status_port)) continue;
        if (std::strncmp(argv[i], "--seed=", 7) == 0) {
            cfg.seed = static_cast<unsigned int>(std::strtoul(argv[i] + 7, nullptr, 10));
            continue;
        }
    }
    return cfg;
}

void Config::validate_or_throw() const {
    auto fail = [](const std::string& msg) {
        throw std::runtime_error(msg);
    };
    if (tourists_total <= 0) fail("tourists must be > 0");
    if (N <= 0) fail("N must be > 0");
    if (M <= 0) fail("M must be > 0");
    if (P <= 0) fail("P must be > 0");
    if (X1 <= 0 || X1 >= M) fail("X1 must be in (0, M)");
    if (X2 <= 0 || X2 >= 2 * M) fail("X2 must be in (0, 2*M)");
    double max_ferry = 1.5 * static_cast<double>(M);
    if (X3 <= 0 || X3 >= max_ferry) fail("X3 must be in (0, 1.5*M)");
    if (segment_min_ms <= 0 || segment_max_ms < segment_min_ms) fail("segment range invalid");
    if (bridge_min_ms <= 0 || bridge_max_ms < bridge_min_ms) fail("bridge range invalid");
    if (tower_min_ms <= 0 || tower_max_ms < tower_min_ms) fail("tower range invalid");
    if (ferry_T_ms <= 0) fail("ferry time must be > 0");
    if (signal1_prob < 0.0 || signal1_prob > 1.0) fail("signal1 must be in [0,1]");
    if (signal2_prob < 0.0 || signal2_prob > 1.0) fail("signal2 must be in [0,1]");
    if (vip_prob < 0.0 || vip_prob > 1.0) fail("vip-prob must be in [0,1]");
    if (status_port != -1 && (status_port <= 0 || status_port > 65535)) fail("status-port out of range");
}
