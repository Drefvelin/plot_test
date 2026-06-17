#pragma once

#include "BuildingGrowthQueue.h"
#include "Config.h"
#include "DefCache.h"
#include "PlacementFloors.h"
#include "Town.h"
#include "TownConfig.h"

struct TerrainAtlas;

class BuildingPlacer {
public:
    static void sync(Town& town, const BuildingGrowthQueue& queue, const DefCache& defs,
                     const PlotConfig& plots, const TownConfig& townCfg, const Config& config,
                     const PlacementFloors& floors, float pixelsPerUnit, int townSeed,
                     const TerrainAtlas* terrain = nullptr);
};
