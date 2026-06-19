#include "config/YamlUtil.h"

#include <exception>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace yamlutil {

std::array<uint8_t, 3> readRgb(const YAML::Node& node, const std::array<uint8_t, 3>& fallback) {
    if (node && node.IsSequence() && node.size() >= 3) {
        return {node[0].as<uint8_t>(), node[1].as<uint8_t>(), node[2].as<uint8_t>()};
    }
    return fallback;
}

sf::Color toSfColor(const std::array<uint8_t, 3>& rgb) {
    return sf::Color(rgb[0], rgb[1], rgb[2]);
}

std::filesystem::path executableDir() {
#ifdef _WIN32
    wchar_t     buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    (void)len;
    return std::filesystem::path(buffer).parent_path();
#else
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
}

bool loadYamlFile(const std::filesystem::path& path, const char* tag, YAML::Node& out) {
    try {
        out = YAML::LoadFile(path.string());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[" << tag << "] Could not load '" << path.string() << "': " << e.what()
                  << "\n[" << tag << "] Using defaults.\n";
        return false;
    }
}

}  // namespace yamlutil
