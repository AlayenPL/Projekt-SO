#include "logger.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

Logger::Logger(const std::string& path)
    : out_(path, std::ios::out | std::ios::trunc), t0_(std::chrono::steady_clock::now()) {
    if (!out_) {
        throw std::runtime_error("Cannot open log file: " + path);
    }
}

void Logger::log(const std::string& line) {
    std::lock_guard<std::mutex> lk(mu_);
    out_ << line << "\n";
    out_.flush();
}

void Logger::log_ts(const std::string& tag, const std::string& msg) {
    using namespace std::chrono;
    auto ms = duration_cast<milliseconds>(steady_clock::now() - t0_).count();

    std::ostringstream oss;
    oss << "t=" << ms << "ms " << tag;
    if (!msg.empty()) oss << " " << msg;
    log(oss.str());
}
