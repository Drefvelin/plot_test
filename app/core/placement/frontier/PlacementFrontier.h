#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

struct Town;

struct FrontageSlot;
struct RoadFrontageSegment;
struct WallGap;

enum class FrontierBand : std::uint8_t {
    Core = 0,
    Suburban,
    Rural,
    Count
};

struct FrontierRef {
    int   roadId     = -1;
    int   bankIndex  = 0;
    int   segmentId  = -1;
    float centerDist = 0.f;
    float startT     = 0.f;
    float endT       = 0.f;
};

struct AlleyFrontierRef {
    int   roadId     = -1;
    int   bankIndex  = 0;
    int   segmentId  = -1;
    float tMin       = 0.f;
    float tMax       = 0.f;
    float centerDist = 0.f;
};

struct FrontierBandSet {
    bool core      = false;
    bool suburban  = false;
    bool rural     = false;

    static FrontierBandSet townRing();
    static FrontierBandSet coreOnly();
    static FrontierBandSet ruralOnly();
};

FrontierBand      classifyFrontierBand(float centerDist, const Town& town);
FrontierBand      roadFrontierBand(const Town& town, int roadId);
FrontierBandSet   frontierBandsFromDistFilter(const Town& town, float minDistInclusive,
                                              float maxDistInclusive, bool filterEnabled);

void rebuildPlacementFrontier(Town& town);
void frontierExtendBands(Town& town, float prevSuburbanMaxDist, float prevCoreMaxDist);
void frontierRefreshRoad(Town& town, int roadId);
void frontierRefreshPlotBank(Town& town, int roadId, int bankIndex);
void frontierRefreshWallBank(Town& town, int roadId, int bankIndex);
void frontierRefreshAlleyBank(Town& town, int roadId, int bankIndex, float minAlleyGapWidth);

bool peekClosestPlotSlot(const Town& town, const FrontierBandSet& bands, float minWidth,
                         const std::unordered_set<int>& skipSegmentIds, FrontierRef& outRef,
                         FrontageSlot& outSlot);
bool peekClosestPlotRef(const Town& town, const FrontierBandSet& bands, float minWidth,
                        const std::unordered_set<int>& skipSegmentIds, FrontierRef& outRef);
bool peekClosestWallGapRef(const Town& town, const FrontierBandSet& bands, float minWidth,
                           const std::unordered_set<int>& skipSegmentIds, FrontierRef& outRef);
bool fillFrontageSlotFromRef(const Town& town, const FrontierRef& ref, bool wallBucket,
                             FrontageSlot& outSlot);

bool peekClosestWallGapSlot(const Town& town, const FrontierBandSet& bands, float minWidth,
                            const std::unordered_set<int>& skipSegmentIds, FrontierRef& outRef,
                            FrontageSlot& outSlot);

bool peekClosestAlleyGap(const Town& town, float maxDistInclusive, float minGapWidth,
                         const std::unordered_set<int>& skipSegmentIds, AlleyFrontierRef& outRef,
                         WallGap& outGap);
void consumeAlleyGap(Town& town, const AlleyFrontierRef& ref);
void frontierRemoveAlleyGap(Town& town, int roadId, int bankIndex, float tMin, float tMax);

bool placementFrontierHasUncheckedAlleyInCore(const Town& town, float minGapWidth,
                                              float maxDistInclusive);

struct PlotFrontierAudit {
    int geometryEligible   = 0;
    int frontierRefs       = 0;
    int uniqueFrontierIds  = 0;
    int missingFromFrontier = 0;
    int staleFrontierRefs  = 0;
    int coreRefs           = 0;
    int suburbanRefs       = 0;
    int ruralRefs          = 0;
};

PlotFrontierAudit auditPlotFrontier(const Town& town, const FrontierBandSet& bands);

bool resolveFrontierRefSegment(const Town& town, const FrontierRef& ref, bool wallBucket,
                               RoadFrontageSegment& outSegment);
bool resolveAlleyFrontierRefSegment(const Town& town, const AlleyFrontierRef& ref,
                                      RoadFrontageSegment& outSegment);
