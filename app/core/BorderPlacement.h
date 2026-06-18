#pragma once

#include "Config.h"
#include "DefCache.h"
#include "GrowthRings.h"
#include "PlacementLogging.h"
#include "PlacementPrep.h"
#include "TerrainAtlas.h"
#include "TerrainPlacementLogging.h"
#include "Town.h"

bool tryPlaceBorderPlot(Town& town, BuildingInstance& instance, const DefCache& defs,
                        const PlotConfig& plots, const PlacementPrep& prep, int townSeed,
                        const TerrainAtlas& terrain, const BandFilter& bandFilter,
                        TerrainPlacementTrace* trace = nullptr, bool forceBandStyle = false);
