#pragma once

#include "config/DefCache.h"
#include "town/Town.h"
#include "config/TownConfig.h"

struct PlotConfig;
struct TerrainAtlas;

void relocateMovableBuildingsAfterRingBump(Town& town, const DefCache& defs, const PlotConfig& plots,
                                           const TownConfig& townCfg, int townSeed, int maxBuildings,
                                           const TerrainAtlas* terrain, float prevSuburbanDist,
                                           float prevCoreDist);
