#pragma once

#include "Town.h"

#include <vector>

class DefCache;

bool pointInPolygon(const Vec2& p, const std::vector<Vec2>& polygon);
bool plotInsideCell(const Plot& plot, const Cell& cell);
void buildRoadPlot(const Vec2& roadStart, const Vec2& edgeDir, const Vec2& inward, float setback,
                   float frontage, float depth, Plot& plot);
Vec2 plotCenter(const Plot& plot);
float maxPlotDepthInCell(const Vec2& roadStart, const Vec2& edgeDir, float frontage,
                         const Vec2& inward, float setback, const Cell& cell);
bool plotsOverlap(const Plot& a, const Plot& b);
bool footprintsOverlap(const BuildingFootprint& a, const BuildingFootprint& b);
bool footprintInsideCell(const BuildingFootprint& footprint, const Cell& cell);
Vec2 instancePlacementPoint(const BuildingInstance& instance);
bool overlapsInstances(const Plot& plot, const std::vector<BuildingInstance>& instances);
bool footprintOverlapsMains(const BuildingFootprint& footprint, const Town& town,
                            const DefCache& defs);
float footprintTMin(const BuildingFootprint& footprint, const Vec2& origin, const Vec2& edgeDir);
float footprintTMax(const BuildingFootprint& footprint, const Vec2& origin, const Vec2& edgeDir);
float plotTMin(const Plot& plot, const Vec2& origin, const Vec2& edgeDir);
float plotTMax(const Plot& plot, const Vec2& origin, const Vec2& edgeDir);
struct RoadWallSpan {
    float tMin = 0.f;
    float tMax = 0.f;

    float width() const { return tMax - tMin; }
};

struct WallGap {
    int   id        = -1;
    int   roadId    = -1;
    int   cellId    = -1;
    int   bankIndex = -1;
    float tMin      = 0.f;
    float tMax      = 0.f;
    Vec2  origin{};
    Vec2  edgeDir{};
    Vec2  inward{};

    float width() const { return tMax - tMin; }
    float gapMidT() const { return (tMin + tMax) * 0.5f; }
    Vec2  gapMidPoint() const { return origin + edgeDir * gapMidT(); }
};

void collectBuildingWallSpansOnSide(const Town& town, int roadId, int cellId, int bankIndex,
                                    const Vec2& origin, const Vec2& edgeDir,
                                    std::vector<RoadWallSpan>& out);
void collectWallGapsOnSide(const Town& town, int roadId, int cellId, int bankIndex,
                           const Vec2& origin, const Vec2& edgeDir, float roadLen,
                           float minGapWidth, std::vector<RoadWallSpan>& outGaps);
void collectAllPrimaryWallGaps(const Town& town, float minGapWidth, std::vector<WallGap>& out);
WallGapKey wallGapKey(const WallGap& gap);
bool isAlleyGapChecked(const Town& town, const WallGap& gap);
void markAlleyGapChecked(Town& town, const WallGap& gap);
bool cellHasUncheckedAlleyGaps(const Town& town, int cellId,
                               const std::vector<WallGap>& allGaps);
bool isSecondaryHostGap(const Town& town, const WallGap& gap);
bool cellHasUncheckedPrimaryAlleyGaps(const Town& town, int cellId,
                                      const std::vector<WallGap>& allGaps);
bool cellHasUncheckedSecondaryHostAlleyGaps(const Town& town, int cellId,
                                            const std::vector<WallGap>& allGaps);
bool wallGapNearExistingSecondary(const Town& town, int cellId, const Vec2& gapPt,
                                  float eps = 5.f);
int secondaryRoadCountInCell(const Town& town, int cellId);
bool roadFrameForCell(const Road& road, int cellId, Vec2& origin, Vec2& farEnd, Vec2& edgeDir);
bool segmentIntersectsMainFootprints(const Vec2& a, const Vec2& b, const Town& town);
bool segmentIntersectsSecondaryFootprints(const Vec2& a, const Vec2& b, const Town& town);
bool alleySegmentBlocked(const Vec2& a, const Vec2& b, float roadHalfWidth, const Town& town,
                         const DefCache& defs);
bool footprintOverlapsAlleys(const BuildingFootprint& footprint, const Town& town, float setback,
                             int excludeRoadId = -1);
bool plotOverlapsAlleys(const Plot& plot, const Town& town, float setback, int excludeRoadId = -1);
bool segmentClearsRoadSetback(const Vec2& a, const Vec2& b, const Town& town, int hostCellId,
                              float setback, int excludeRoadId);
bool segmentsIntersect2D(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1);
bool segmentCrossingParams(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                           float& outT, float& outU);
bool segmentsCrossInInterior(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                             float interiorEps = 0.03f);
bool segmentParallelOverlapMetrics(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                                   float minParallelCos, float& outMinSep, float& outOverlapLen);
bool segmentNearParallelRoad(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                             float minParallelCos, float maxSep, float minOverlapFrac);
float raySegmentHitDist(const Vec2& origin, const Vec2& dir, const Vec2& segA, const Vec2& segB,
                        float maxDist);
bool orientRoadForCell(const Road& road, int cellId, Vec2& a, Vec2& b, Vec2& edgeDir, Vec2& inward,
                       const Cell& cell, float setback);
