#pragma once

#include "Config.h"
#include "DefCache.h"
#include "PlotDimensions.h"
#include "Town.h"

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

struct CellSearchStats {
    int   cellId        = -1;
    float centDist      = 0.f;
    int   roadsChecked  = 0;
    int   dimInvalid    = 0;
    int   dimRoadShort  = 0;
    int   dimRatio      = 0;
    int   dimArea       = 0;
    int   dimDepth      = 0;
    int   dimNoDepth    = 0;
    int   outsideCell   = 0;
    int   overlap       = 0;
    int   valid         = 0;
    float bestValidDist = std::numeric_limits<float>::max();

    struct RoadProbe {
        int       roadId        = -1;
        float     roadLen       = 0.f;
        float     depthCap      = 0.f;
        DimReject reject        = DimReject::None;
        bool      setbackInside = false;
        bool      altSideInside = false;
    };
    std::vector<RoadProbe> roadProbes;
};

struct PlacementSearchLog {
    int         buildingId = -1;
    std::string buildingType;
    int         totalValid = 0;
    int         chosenCell = -1;
    int         chosenRoad = -1;
    int         chosenSegment = -1;
    float       chosenDist = 0.f;
    float       chosenFrontage = 0.f;
    float       chosenDepth = 0.f;
    float       chosenArea = 0.f;
    float       targetArea = 0.f;
    PlotOrientation orientFirst  = PlotOrientation::Horizontal;
    PlotOrientation chosenOrient = PlotOrientation::Horizontal;
    Vec2        chosenCenter{};
    float       townGrowth = 0.f;
    float       zoneBias = 0.f;
    float       chosenZoneScore = 0.f;
    std::string zoneType;
    int         slotsExamined = 0;
    int         zoneFiltered = 0;
    int         noInwardSkipped = 0;
    int         orientFailedSkipped = 0;
    int         dimFailedSegments = 0;
    int         layoutRequested = 0;
    int         layoutPlaced = 0;
    std::string resultSummary;
    std::unordered_map<int, CellSearchStats> cells;
};

struct FrontageSlot;

CellSearchStats& statsFor(PlacementSearchLog& log, int cellId, float centDist);
void recordDimReject(CellSearchStats& stats, DimReject reason);
void logPlacementDecision(const Town& town, const PlacementSearchLog& log, const PlotConfig& plots,
                          const DefCache& defs);
void logSegmentInventory(const Town& town);
void logSegmentProbe(int buildingId, const FrontageSlot& slot, const char* result,
                     DimReject reject = DimReject::None, float depthCap = -1.f,
                     float frontageNeed = -1.f, float areaFit = -1.f, float slotT = -1.f);
