#pragma once

#include "Town.h"

#include <vector>

class DefCache;
struct TerrainAtlas;
struct TownConfig;

bool polygonBuildable(const Vec2 corners[4], const TerrainAtlas& terrain, float edgeStep = 0.5f);
bool pointInPolygon(const Vec2& p, const std::vector<Vec2>& polygon);
void buildRoadPlot(const Vec2& roadStart, const Vec2& edgeDir, const Vec2& inward, float setback,
                   float frontage, float depth, Plot& plot);
Vec2 plotCenter(const Plot& plot);
float maxPlotDepthToRoadHit(const Vec2& roadStart, const Vec2& edgeDir, float frontage,
                            const Vec2& inward, float setback, int hostRoadId, int bankIndex,
                            Town& town);
void invalidateRoadTopologyCaches(Town& town);
bool plotPlacementValid(const Plot& plot, const Town& town, const TerrainAtlas* terrain,
                        float setback, int hostRoadId);
bool plotsOverlap(const Plot& a, const Plot& b);
bool bankHasBuildingOnSide(const Town& town, int roadId, int bankIndex);
bool footprintsOverlap(const BuildingFootprint& a, const BuildingFootprint& b);
bool footprintPlacementValid(const BuildingFootprint& footprint, const Town& town,
                             const TerrainAtlas* terrain, float setback, int hostRoadId);
Vec2 instancePlacementPoint(const BuildingInstance& instance);
bool overlapsInstances(const Plot& plot, const std::vector<BuildingInstance>& instances);
bool footprintOverlapsMains(const BuildingFootprint& footprint, const Town& town,
                            const DefCache& defs);
bool footprintOverlapsMainsOnBank(const BuildingFootprint& footprint, const Town& town,
                                  const DefCache& defs, int roadId, int bankIndex,
                                  const std::vector<int>* otherRoadPlotCandidates = nullptr);
bool segmentIntersectsFootprint(const Vec2& a, const Vec2& b, const BuildingFootprint& fp);
void rebuildMainOccupancyForBank(Town& town, int roadId, int bankIndex);
void rebuildAllMainOccupancyT(Town& town);
void rebuildSecondaryRoadIdList(Town& town);
void collectOtherRoadPlotCandidatesForGap(const Town& town, int roadId, int bankIndex,
                                          const Vec2& origin, const Vec2& edgeDir,
                                          const Vec2& inward, float gapStart, float gapEnd,
                                          float maxDepth, std::vector<int>& out);
float footprintTMin(const BuildingFootprint& footprint, const Vec2& origin, const Vec2& edgeDir);
float footprintTMax(const BuildingFootprint& footprint, const Vec2& origin, const Vec2& edgeDir);
float plotTMin(const Plot& plot, const Vec2& origin, const Vec2& edgeDir);
float plotTMax(const Plot& plot, const Vec2& origin, const Vec2& edgeDir);

struct WallGap {
    int   id        = -1;
    int   roadId    = -1;
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

WallGap wallGapFromSegment(const Road& road, int bankIndex, const RoadFrontageSegment& segment);

struct AlleySegmentCandidate {
    Vec2 start{};
    Vec2 end{};
    int  destRoadId = -1;
};

struct AuxiliaryDemolition {
    int instanceId       = -1;
    int footprintLabelId = -1;
};

struct AlleyProbeCandidate {
    WallGap                            gap;
    std::vector<AlleySegmentCandidate> segments;
    std::vector<AuxiliaryDemolition>   demolitions;
    float                              probeAngleDeg = 0.f;
    Vec2                               probeDir{};
    AlleyPlacementKind                 placementKind = AlleyPlacementKind::Straight;
    float                              turnAngleDeg  = 0.f;
};

enum class AlleyQualityReject {
    None,
    ThinSide,
    BadAngle,
    EndpointSpacing,
    BankParallel,
};

void collectAllPrimaryWallGaps(Town& town, float minGapWidth, std::vector<WallGap>& out);
void collectWallGapsInDistRange(Town& town, float minGapWidth, float maxDistInclusive,
                                std::vector<WallGap>& out);
void collectWallGapsInHopRange(Town& town, float minGapWidth, int minHopInclusive,
                               int maxHopInclusive, const std::vector<int>& junctionHops,
                               std::vector<WallGap>& out);
void clearAlleyGapStateForRoad(Town& town, int roadId);
WallGapKey wallGapKey(const WallGap& gap);
bool isAlleyGapChecked(const Town& town, const WallGap& gap);
void markAlleyGapChecked(Town& town, const WallGap& gap);
bool bankHasUncheckedAlleyGaps(Town& town, int roadId, int bankIndex, float minGapWidth,
                                 float maxDistInclusive);
bool isSecondaryHostGap(const Town& town, const WallGap& gap);
bool wallGapNearExistingSecondary(const Town& town, int roadId, const Vec2& gapPt,
                                  float eps = 5.f);
bool segmentIntersectsMainFootprints(const Vec2& a, const Vec2& b, const Town& town);
bool segmentIntersectsSecondaryFootprints(const Vec2& a, const Vec2& b, const Town& town);
bool alleySegmentBlocked(const Vec2& a, const Vec2& b, float roadHalfWidth, const Town& town,
                         const DefCache& defs);
bool alleySegmentBlockedByMain(const Vec2& a, const Vec2& b, float roadHalfWidth, const Town& town,
                               const DefCache& defs);
void collectAuxiliaryDemolitionsForAlley(const Vec2& a, const Vec2& b, float roadHalfWidth,
                                         const Town& town, std::vector<AuxiliaryDemolition>& out);
void applyAuxiliaryDemolitions(Town& town, const std::vector<AuxiliaryDemolition>& demolitions);
bool footprintOverlapsAlleys(const BuildingFootprint& footprint, const Town& town, float setback,
                             int excludeRoadId = -1);
bool plotOverlapsAlleys(const Plot& plot, const Town& town, float setback, int excludeRoadId = -1);
bool segmentClearsRoadSetback(const Vec2& a, const Vec2& b, const Town& town, int hostRoadId,
                              float setback, int excludeRoadId);
float distancePointToSegment(const Vec2& p, const Vec2& a, const Vec2& b);
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
bool minSideRoadClearanceAlongSegment(const Town& town, const Vec2& a, const Vec2& b,
                                      int hostRoadId, int destRoadId, float minDist,
                                      int sampleCount, float setback);
float alleyCrossingAngleDeg(const Town& town, const Vec2& segmentStart, const Vec2& segmentEnd,
                            int destRoadId);
bool alleyCrossingAngleOk(const Town& town, const Vec2& segmentStart, const Vec2& segmentEnd,
                          int destRoadId, float minAngleDeg);
bool alleyEndpointTooClose(const Town& town, const Vec2& start, const Vec2& end, float minSpacing);
bool alleyBankDirectionOk(const Town& town, const WallGap& gap, const Vec2& alleyDir,
                          float minSepDeg);
bool validateAlleyProbe(const Town& town, const AlleyProbeCandidate& probe, const TownConfig& cfg,
                        float setback, AlleyQualityReject* outReject = nullptr);
