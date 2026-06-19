#pragma once

#include "config/DefCache.h"

struct PlacementFloors {
    float minPlotFrontage = 0.f;
    float minPlotDepth    = 0.f;
    float minGapWidth     = 0.f;

    static PlacementFloors fromDefs(const DefCache& defs, float maxDepthToFrontRatio);
};
