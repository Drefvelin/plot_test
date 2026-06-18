#include "Logger.h"

#include <iomanip>
#include <iostream>
#include <sstream>

std::mutex Logger::mutex_;
std::filesystem::path Logger::logDirectory_;
std::unordered_map<std::string, std::ofstream> Logger::streams_;
std::string Logger::defaultChannel_ = "app";
bool Logger::warnedUnknownChannel_  = false;
int Logger::flushIntervalMs_        = 2000;
std::chrono::steady_clock::time_point Logger::lastFlushTime_ = std::chrono::steady_clock::now();

void Logger::init(const Config& config, const std::filesystem::path& projectRoot) {
    std::lock_guard<std::mutex> lock(mutex_);
    streams_.clear();
    warnedUnknownChannel_ = false;
    flushIntervalMs_      = config.logging.flushIntervalMs;
    lastFlushTime_        = std::chrono::steady_clock::now();

    logDirectory_ = projectRoot / config.logging.directory;
    if (std::filesystem::exists(logDirectory_)) {
        for (const auto& entry : std::filesystem::directory_iterator(logDirectory_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log") {
                std::filesystem::remove(entry.path());
            }
        }
    }
    std::filesystem::create_directories(logDirectory_);

    for (const auto& file : config.logging.files) {
        const auto path = logDirectory_ / file.filename;
        streams_[file.channel].open(path, std::ios::out);
        if (!streams_[file.channel].is_open()) {
            std::cerr << "[Logger] Failed to open log file: " << path << '\n';
        }
    }

    if (!config.logging.files.empty()) {
        defaultChannel_ = config.logging.files.front().channel;
    }
}

std::string Logger::timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::ofstream& Logger::streamFor(const std::string& channel) {
    auto it = streams_.find(channel);
    if (it != streams_.end() && it->second.is_open()) {
        return it->second;
    }

    if (!warnedUnknownChannel_) {
        warnedUnknownChannel_ = true;
        std::cerr << "[Logger] Unknown channel '" << channel << "', falling back to '"
                  << defaultChannel_ << "'.\n";
    }

    auto fallback = streams_.find(defaultChannel_);
    if (fallback != streams_.end()) {
        return fallback->second;
    }

    static std::ofstream nullStream;
    return nullStream;
}

void Logger::flushAllUnlocked() {
    for (auto& [_, stream] : streams_) {
        if (stream.is_open()) {
            stream.flush();
        }
    }
    lastFlushTime_ = std::chrono::steady_clock::now();
}

void Logger::maybeFlushUnlocked() {
    if (flushIntervalMs_ <= 0) {
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - lastFlushTime_);
    if (elapsed.count() >= flushIntervalMs_) {
        flushAllUnlocked();
    }
}

void Logger::log(const std::string& channel, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& stream = streamFor(channel);
    if (!stream.is_open()) {
        return;
    }
    stream << timestamp() << " [" << channel << "] " << message << '\n';
    maybeFlushUnlocked();
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    flushAllUnlocked();
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    flushAllUnlocked();
    for (auto& [_, stream] : streams_) {
        if (stream.is_open()) {
            stream.close();
        }
    }
    streams_.clear();
}

const std::filesystem::path& Logger::directory() {
    return logDirectory_;
}
