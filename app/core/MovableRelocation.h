#pragma once

#include "DefCache.h"
#include "Town.h"
#include "TownConfig.h"

struct PlotConfig;
struct TerrainAtlas;

void relocateMovableBuildingsAfterRingBump(Town& town, const DefCache& defs, const PlotConfig& plots,
                                           const TownConfig& townCfg, int townSeed, int maxBuildings,
                                           const TerrainAtlas* terrain, float prevSuburbanDist,
                                           float prevCoreDist);
