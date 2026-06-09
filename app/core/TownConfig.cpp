#include "TownConfig.h"

#include "Config.h"

#include <yaml-cpp/yaml.h>

#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int TownConfig::totalBuildings() const {
    int total = 0;
    for (const auto& [_, count] : buildingCounts) {
        total += count;
    }
    return total;
}

int TownConfig::countFor(const std::string& buildingType) const {
    const auto it = buildingCounts.find(buildingType);
    if (it == buildingCounts.end()) {
        return 0;
    }
    return it->second;
}

std::filesystem::path TownConfig::resolveTownPath() {
    const auto configPath = Config::resolveConfigPath();
    const auto besideConfig = configPath.parent_path() / "town.yml";
    if (std::filesystem::exists(besideConfig)) {
        return besideConfig;
    }

#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    const auto projectRoot = Config::resolveProjectRoot(std::filesystem::path(buffer).parent_path());
#else
    const auto projectRoot =
        Config::resolveProjectRoot(std::filesystem::read_symlink("/proc/self/exe").parent_path());
#endif
    return projectRoot / "app" / "config" / "town.yml";
}

TownConfig TownConfig::load(const std::filesystem::path& path) {
    TownConfig config;

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const std::exception& e) {
        std::cerr << "[TownConfig] Could not load '" << path.string() << "': " << e.what()
                  << "\n[TownConfig] Using defaults.\n";
        return config;
    }

    if (auto buildings = root["buildings"]) {
        for (const auto& entry : buildings) {
            config.buildingCounts[entry.first.as<std::string>()] = entry.second.as<int>();
        }
    }

    if (root["segment_gap_fill_count"]) {
        config.segmentGapFillCount = std::max(0, root["segment_gap_fill_count"].as<int>());
    }

    if (root["min_wall_gap_for_alley"]) {
        config.minWallGapForAlley = std::max(0.f, root["min_wall_gap_for_alley"].as<float>());
    }
    if (root["min_alley_length"]) {
        config.minAlleyLength = std::max(0.f, root["min_alley_length"].as<float>());
    }
    if (root["max_alley_length"]) {
        config.maxAlleyLength = std::max(0.f, root["max_alley_length"].as<float>());
    }
    if (root["max_alley_angle_deg"]) {
        config.maxAlleyAngleDeg = std::max(0.f, root["max_alley_angle_deg"].as<float>());
    }
    if (root["alleys_per_unit_length"]) {
        config.alleysPerUnitLength = std::max(0.f, root["alleys_per_unit_length"].as<float>());
    }
    if (root["alley_angle_count"]) {
        config.alleyAngleCount = std::max(1, root["alley_angle_count"].as<int>());
    }
    if (root["min_alley_crossing_angle_deg"]) {
        config.minAlleyCrossingAngleDeg =
            std::max(0.f, root["min_alley_crossing_angle_deg"].as<float>());
    }
    if (root["min_alley_endpoint_spacing"]) {
        config.minAlleyEndpointSpacing =
            std::max(0.f, root["min_alley_endpoint_spacing"].as<float>());
    }
    if (root["min_alley_created_area"]) {
        config.minAlleyCreatedArea = std::max(0.f, root["min_alley_created_area"].as<float>());
    }
    if (root["min_alley_side_road_dist"]) {
        config.minAlleySideRoadDist = std::max(0.f, root["min_alley_side_road_dist"].as<float>());
    }
    if (root["alley_side_road_sample_count"]) {
        config.alleySideRoadSampleCount =
            std::max(2, root["alley_side_road_sample_count"].as<int>());
    }
    if (root["alley_fill_fail_limit"]) {
        config.alleyFillFailLimit = std::max(1, root["alley_fill_fail_limit"].as<int>());
    }

    return config;
}
