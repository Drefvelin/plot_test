#pragma once

#include <yaml-cpp/yaml.h>

#include <SFML/Graphics/Color.hpp>

#include <array>
#include <cstdint>
#include <filesystem>

// Shared helpers for the YAML-backed config loaders (Config / TownConfig /
// DefCache) and the CLI entry point. Centralizes RGB parsing, color
// conversion, executable-directory discovery and the load-or-default pattern.
namespace yamlutil {

std::array<uint8_t, 3> readRgb(const YAML::Node& node, const std::array<uint8_t, 3>& fallback);

sf::Color toSfColor(const std::array<uint8_t, 3>& rgb);

std::filesystem::path executableDir();

// Loads a YAML document. On failure logs "[tag] Could not load ... Using
// defaults." to std::cerr and returns false, leaving `out` untouched.
bool loadYamlFile(const std::filesystem::path& path, const char* tag, YAML::Node& out);

}  // namespace yamlutil
