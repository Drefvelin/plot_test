#pragma once

#include "Config.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

class Logger {
public:
    static void init(const Config& config, const std::filesystem::path& projectRoot);
    static void log(const std::string& channel, const std::string& message);
    static void shutdown();

private:
    static std::string timestamp();
    static std::ofstream& streamFor(const std::string& channel);

    static std::mutex mutex_;
    static std::filesystem::path logDirectory_;
    static std::unordered_map<std::string, std::ofstream> streams_;
    static std::string defaultChannel_;
    static bool warnedUnknownChannel_;
};
