#pragma once

#include "config/Config.h"
#include "config/DefCache.h"
#include "placement/orchestration/GrowthRings.h"
#include "placement/logging/PlacementLogging.h"
#include "placement/orchestration/PlacementPrep.h"
#include "terrain/TerrainAtlas.h"
#include "placement/logging/TerrainPlacementLogging.h"
#include "town/Town.h"

bool tryPlaceBorderPlot(Town& town, BuildingInstance& instance, const DefCache& defs,
                        const PlotConfig& plots, const PlacementPrep& prep, int townSeed,
                        const TerrainAtlas& terrain, const BandFilter& bandFilter,
                        TerrainPlacementTrace* trace = nullptr, bool forceBandStyle = false);
