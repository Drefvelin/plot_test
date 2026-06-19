#pragma once

#include "placement/orchestration/BuildingGrowthQueue.h"
#include "config/Config.h"
#include "config/DefCache.h"
#include "placement/orchestration/PlacementFloors.h"
#include "town/Town.h"
#include "config/TownConfig.h"

struct TerrainAtlas;

class BuildingPlacer {
public:
    static void sync(Town& town, const BuildingGrowthQueue& queue, const DefCache& defs,
                     const PlotConfig& plots, const TownConfig& townCfg, const Config& config,
                     const PlacementFloors& floors, float pixelsPerUnit, int townSeed,
                     const TerrainAtlas* terrain = nullptr);
};
