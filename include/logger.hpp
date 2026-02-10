#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <chrono>

class Logger {
public:
    /**
     * @brief Create logger writing to @p path (truncates existing file).
     */
    explicit Logger(const std::string& path);

    // log z timestampem od startu loggera
    /**
     * @brief Log a message with milliseconds since logger start.
     *
     * @param tag short tag/category
     * @param msg message body
     */
    void log_ts(const std::string& tag, const std::string& msg);

    // statyczny helper używany w częściach kodu, jeśli masz taki styl
    /**
     * @brief Log using the global logger instance if set.
     */
    static void log(const std::string& msg);

private:
    std::ofstream out_;
    std::mutex mu_;
    std::chrono::steady_clock::time_point t0_;

    static Logger* g_logger_;
};
