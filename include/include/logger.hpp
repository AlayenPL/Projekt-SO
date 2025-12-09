#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <chrono>

class Logger {
public:
    explicit Logger(const std::string& path);

    // log z timestampem od startu loggera
    void log_ts(const std::string& tag, const std::string& msg);

    // statyczny helper używany w częściach kodu, jeśli masz taki styl
    static void log(const std::string& msg);

private:
    std::ofstream out_;
    std::mutex mu_;
    std::chrono::steady_clock::time_point t0_;

    static Logger* g_logger_;
};
