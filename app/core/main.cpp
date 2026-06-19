#include "App.h"
#include "Config.h"
#include "DefCache.h"
#include "Logger.h"
#include "PlacementFloors.h"
#include "Profile.h"
#include "TerrainAtlas.h"
#include "TerrainCatalog.h"
#include "TownConfig.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

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

GrowthConfig parseGrowthCli(int argc, char* argv[], const Config& config, int defaultTarget) {
    GrowthConfig growth = config.growth;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--auto-grow") {
            growth.autoGrow = defaultTarget;
            if (i + 1 < argc) {
                try {
                    const int value = std::stoi(argv[i + 1]);
                    growth.autoGrow = std::max(0, value);
                    ++i;
                } catch (...) {
                }
            }
        } else if (arg.rfind("--auto-grow=", 0) == 0) {
            try {
                growth.autoGrow = std::max(0, std::stoi(arg.substr(12)));
            } catch (...) {
            }
        } else if (arg == "--auto-grow-ms" && i + 1 < argc) {
            try {
                growth.autoGrowMs = std::max(0, std::stoi(argv[++i]));
            } catch (...) {
            }
        } else if (arg.rfind("--auto-grow-ms=", 0) == 0) {
            try {
                growth.autoGrowMs = std::max(0, std::stoi(arg.substr(15)));
            } catch (...) {
            }
        } else if (arg == "--auto-exit") {
            growth.autoExit = true;
        } else if (arg == "--profile") {
            growth.profile = true;
        }
    }

    return growth;
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto configPath = Config::resolveConfigPath();
    const Config config = Config::load(configPath);
    const auto projectRoot = Config::resolveProjectRoot(executableDirectory());

    Logger::init(config, projectRoot);
    Logger::log("app", "config loaded from " + configPath.string());

    const auto terrainCatalogPath = configPath.parent_path() / "terrains.yml";
    const TerrainCatalog terrainCatalog = TerrainCatalog::load(terrainCatalogPath);
    Logger::log("app", "terrain catalog loaded from " + terrainCatalogPath.string() + " types="
                           + std::to_string(terrainCatalog.count()));

    const auto buildingsPath = DefCache::resolveBuildingsPath();
    const DefCache defs = DefCache::load(buildingsPath, terrainCatalog);
    Logger::log("app", "buildings loaded from " + buildingsPath.string() + " types="
                           + std::to_string(defs.buildings().size()) + " plot_bands="
                           + std::to_string(defs.plotSizes().size()) + " building_bands="
                           + std::to_string(defs.buildingSizes().size()));

    const auto townPath = TownConfig::resolveTownPath();
    const TownConfig townConfig = TownConfig::load(townPath);
    Logger::log("app", "town loaded from " + townPath.string() + " total_buildings="
                           + std::to_string(townConfig.totalBuildings()));

    const int autoGrowDefault = std::max(200, townConfig.totalBuildings());
    const GrowthConfig growthAuto = parseGrowthCli(argc, argv, config, autoGrowDefault);
    if (growthAuto.profile) {
        Profile::setEnabled(true);
    }
    if (growthAuto.autoGrow > 0) {
        Logger::log("app",
                    "auto_grow enabled target=" + std::to_string(growthAuto.autoGrow) + " ms="
                        + std::to_string(growthAuto.autoGrowMs) + " auto_exit="
                        + (growthAuto.autoExit ? "yes" : "no"));
    } else if (growthAuto.autoExit) {
        Logger::log("app", "auto_exit enabled (quit after town load)");
    }

    TerrainAtlas terrainAtlas = bakeTerrain(config, terrainCatalog, projectRoot);

    const PlacementFloors placementFloors =
        PlacementFloors::fromDefs(defs, config.plots.maxDepthToFrontRatio);

    App app(config, townConfig, terrainCatalog, defs, std::move(terrainAtlas), placementFloors,
            growthAuto);
    const int code = app.run();

    Logger::shutdown();
    return code;
}
