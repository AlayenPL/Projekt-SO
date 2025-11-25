#include "config.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

static bool starts_with(const char* s, const char* pref) {
    return std::strncmp(s, pref, std::strlen(pref)) == 0;
}

static int to_int(const char* s) {
    return std::atoi(s);
}

static int64_t to_i64(const char* s) {
    return std::atoll(s);
}

static double to_double(const char* s) {
    return std::atof(s);
}

Config Config::from_args(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            print_help(argv[0]);
            std::exit(0);
        }
        auto getv = [&](const char* key) -> const char* {
            size_t klen = std::strlen(key);
            if (starts_with(a, key) && a[klen] == '=') return a + klen + 1;
            return nullptr;
        };

        if (auto v = getv("--duration_ms")) c.duration_ms = to_i64(v);
        else if (auto v = getv("--N")) c.N = to_int(v);
        else if (auto v = getv("--M")) c.M = to_int(v);
        else if (auto v = getv("--P")) c.P = to_int(v);
        else if (auto v = getv("--X1")) c.X1 = to_int(v);
        else if (auto v = getv("--X2")) c.X2 = to_int(v);
        else if (auto v = getv("--X3")) c.X3 = to_int(v);
        else if (auto v = getv("--ferry_T_ms")) c.ferry_T_ms = to_int(v);
        else if (auto v = getv("--tourists_total")) c.tourists_total = to_int(v);
        else if (auto v = getv("--arrival_jitter_ms")) c.arrival_jitter_ms = to_int(v);
        else if (auto v = getv("--seed")) c.seed = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        else if (auto v = getv("--signal1_prob")) c.signal1_prob = to_double(v);
        else if (auto v = getv("--signal2_prob")) c.signal2_prob = to_double(v);
        else if (auto v = getv("--segment_min_ms")) c.segment_min_ms = to_int(v);
        else if (auto v = getv("--segment_max_ms")) c.segment_max_ms = to_int(v);
        else if (auto v = getv("--bridge_min_ms")) c.bridge_min_ms = to_int(v);
        else if (auto v = getv("--bridge_max_ms")) c.bridge_max_ms = to_int(v);
        else if (auto v = getv("--tower_min_ms")) c.tower_min_ms = to_int(v);
        else if (auto v = getv("--tower_max_ms")) c.tower_max_ms = to_int(v);
        else {
            std::cerr << "Unknown argument: " << a << "\n";
            print_help(argv[0]);
            std::exit(2);
        }
    }
    return c;
}

void Config::print_help(const char* prog) {
    std::cout
        << "Usage: " << prog << " [--key=value ...]\n\n"
        << "Key parameters:\n"
        << "  --duration_ms=30000\n"
        << "  --N=60 --M=6 --P=2\n"
        << "  --X1=4 --X2=10 --X3=8\n"
        << "  --ferry_T_ms=900\n"
        << "  --tourists_total=80 --arrival_jitter_ms=500\n"
        << "  --seed=12345\n"
        << "  --signal1_prob=0.10 --signal2_prob=0.05\n"
        << "  --segment_min_ms=400 --segment_max_ms=1200\n"
        << "  --bridge_min_ms=400 --bridge_max_ms=1000\n"
        << "  --tower_min_ms=700 --tower_max_ms=1500\n";
}
