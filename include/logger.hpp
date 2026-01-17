#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <chrono>

class Logger {
public:
    explicit Logger(const std::string& path);

    void log(const std::string& line);

    // Convenience: timestamped log.
    void log_ts(const std::string& tag, const std::string& msg);

private:
    std::ofstream out_;
    std::mutex mu_;
    std::chrono::steady_clock::time_point t0_;
};
