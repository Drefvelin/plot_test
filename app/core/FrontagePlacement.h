#pragma once

#include "Config.h"
#include "DefCache.h"
#include "PlacementLogging.h"
#include "Town.h"

#include <string>

struct FrontageSlot {
    int   segmentId  = -1;
    int   roadId     = -1;
    int   cellId     = -1;
    int   bankIndex  = 0;
    float startT     = 0.f;
    float endT       = 0.f;
    float centerDist = 0.f;
    float zoneScore  = 0.f;

    float width() const { return endT - startT; }
};

void collectFrontageSlots(const Town& town, const DefCache& defs, const std::string& buildingType,
                          float townGrowth, std::vector<FrontageSlot>& out, int roadFilter = -1);

void buildSegmentTCandidates(const FrontageSlot& slot, float minFrontage, const Vec2& origin,
                             const Vec2& edgeDir, const Vec2& center, std::vector<float>& out);

bool tryPlaceRoadPlot(Town& town, const std::string& buildingType, const DefCache& defs,
                      const PlotConfig& plots, BuildingInstance& out, float targetArea,
                      int townSeed, int maxBuildings, PlacementSearchLog& searchLog,
                      int roadFilter = -1, bool useCellCentroid = false);
