#pragma once

#include "Config.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

class Logger {
public:
    static void init(const Config& config, const std::filesystem::path& projectRoot);
    static void log(const std::string& channel, const std::string& message);
    static void flush();
    static void shutdown();
    static const std::filesystem::path& directory();

private:
    static std::string timestamp();
    static std::ofstream& streamFor(const std::string& channel);
    static void flushAllUnlocked();
    static void maybeFlushUnlocked();

    static std::mutex mutex_;
    static std::filesystem::path logDirectory_;
    static std::unordered_map<std::string, std::ofstream> streams_;
    static std::string defaultChannel_;
    static bool warnedUnknownChannel_;
    static int flushIntervalMs_;
    static std::chrono::steady_clock::time_point lastFlushTime_;
};
