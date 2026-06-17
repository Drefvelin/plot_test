#pragma once

#include "BuildingLayout.h"
#include "DefCache.h"
#include "PlotDimensions.h"
#include "Town.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

struct RoadAttemptMemo {
    std::uint32_t           topologyGen    = 0;
    int                     instanceCount  = 0;
    int                     suburbanMaxHop   = -1;
    RingPhase               ringPhase      = RingPhase::Normal;
    std::unordered_set<int> failedRoadIds;

    void syncContext(const Town& town);
    void clearFailures();
    bool shouldSkipRoad(int roadId) const;
    void recordFailure(int roadId);
};

struct PlacementPrep {
    float                              targetArea     = 0.f;
    PlotAreaBand                       plotAreaRange{};
    SizeBand                           plotAreaBand{};
    std::vector<ResolvedBuildingSpec>  buildingSpecs;
    PlotOrientation                    orientFirst    = PlotOrientation::Horizontal;
    PlotOrientation                    orientSecond   = PlotOrientation::Vertical;
    float                              townGrowth     = 0.f;
    float                              zoneBias       = 0.f;
    const char*                        zoneType       = nullptr;
    bool                               gapFillReady   = false;
    ResolvedBuildingSpec               mainSpec{};
    float                              minSegmentWidth = 0.f;
};

PlacementPrep buildPlacementPrep(const Town& town, const DefCache& defs,
                               const std::string& buildingType, int buildingId, int townSeed,
                               int maxBuildings);
