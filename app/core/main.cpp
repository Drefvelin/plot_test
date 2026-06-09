#include "App.h"
#include "Config.h"
#include "DefCache.h"
#include "Logger.h"
#include "TerrainAtlas.h"
#include "TownConfig.h"

#include <filesystem>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
#else
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
}

}  // namespace

int main() {
    const auto configPath = Config::resolveConfigPath();
    const Config config = Config::load(configPath);
    const auto projectRoot = Config::resolveProjectRoot(executableDirectory());

    Logger::init(config, projectRoot);
    Logger::log("app", "config loaded from " + configPath.string());

    const auto buildingsPath = DefCache::resolveBuildingsPath();
    const DefCache defs = DefCache::load(buildingsPath);
    Logger::log("app", "buildings loaded from " + buildingsPath.string() + " types="
                           + std::to_string(defs.buildings().size()) + " plot_bands="
                           + std::to_string(defs.plotSizes().size()) + " building_bands="
                           + std::to_string(defs.buildingSizes().size()));

    const auto townPath = TownConfig::resolveTownPath();
    const TownConfig townConfig = TownConfig::load(townPath);
    Logger::log("app", "town loaded from " + townPath.string() + " total_buildings="
                           + std::to_string(townConfig.totalBuildings()));

    TerrainAtlas terrainAtlas = bakeTerrain(config, projectRoot);

    App app(config, townConfig, defs, std::move(terrainAtlas));
    const int code = app.run();

    Logger::shutdown();
    return code;
}
