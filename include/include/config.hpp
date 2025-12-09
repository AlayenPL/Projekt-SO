// config.hpp
#pragma once
#include <cstddef>

struct Config {
    int tourists_total = 5;
    unsigned int seed = 1234;

    static Config from_args(int argc, char** argv);
};
