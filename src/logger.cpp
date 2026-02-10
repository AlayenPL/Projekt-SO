#include "logger.hpp"

#include <filesystem>
#include <stdexcept>
#include <sstream>
#include <iostream>

Logger* Logger::g_logger_ = nullptr;

/**
 * @brief Construct logger writing to given path; truncates existing file.
 */
Logger::Logger(const std::string& path)
{
    namespace fs = std::filesystem;

    // utwórz katalog nadrzędny, jeśli trzeba
    fs::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        // jeśli create_directories się nie uda, i tak spróbujemy otworzyć plik, ale z lepszym błędem
    }

    out_.open(path, std::ios::out | std::ios::trunc);
    if (!out_.is_open()) {
        std::ostringstream oss;
        oss << "Cannot open log file: " << path
            << " (cwd=" << fs::current_path().string() << ")";
        throw std::runtime_error(oss.str());
    }

    t0_ = std::chrono::steady_clock::now();

    // ustaw globalny logger (jeśli korzystasz z Logger::log())
    g_logger_ = this;
}

/**
 * @brief Log a message with relative timestamp in milliseconds.
 */
void Logger::log_ts(const std::string& tag, const std::string& msg)
{
    auto now = std::chrono::steady_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now - t0_).count();

    std::lock_guard<std::mutex> lk(mu_);
    out_ << "t=" << ms << "ms " << tag << " " << msg << "\n";
    out_.flush();
}

/**
 * @brief Static helper to log through global logger if initialized.
 */
void Logger::log(const std::string& msg)
{
    if (!g_logger_) return;
    g_logger_->log_ts("LOG", msg);
}
