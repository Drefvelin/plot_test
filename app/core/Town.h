#pragma once

#include <SFML/Graphics.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "FrontierManager.h"

#include "BuildingTypes.h"
#include "Terrain.h"
#include "TerrainCatalog.h"
#include "Vec2.h"

enum class RingPhase {
    Normal,
    DensifyCore,
};

struct TerrainAtlas;

// Available frontage span on one side of a road.
struct RoadFrontageSegment {
    int   id     = -1;
    float startT = 0.f;
    float endT   = 0.f;

    float width() const { return endT - startT; }
};

struct DepthCacheKey {
    float t        = 0.f;
    float frontage = 0.f;
    float setback  = 0.f;

    bool operator==(const DepthCacheKey& rhs) const {
        return t == rhs.t && frontage == rhs.frontage && setback == rhs.setback;
    }
};

struct DepthCacheKeyHash {
    std::size_t operator()(const DepthCacheKey& key) const noexcept {
        std::size_t h = 0;
        const auto*   bytes = reinterpret_cast<const unsigned char*>(&key.t);
        for (std::size_t i = 0; i < sizeof(float); ++i) {
            h = h * 31u + bytes[i];
        }
        bytes = reinterpret_cast<const unsigned char*>(&key.frontage);
        for (std::size_t i = 0; i < sizeof(float); ++i) {
            h = h * 31u + bytes[i];
        }
        bytes = reinterpret_cast<const unsigned char*>(&key.setback);
        for (std::size_t i = 0; i < sizeof(float); ++i) {
            h = h * 31u + bytes[i];
        }
        return h;
    }
};

struct RoadSideFrontage {
    Vec2                             inward{};
    std::vector<RoadFrontageSegment> segments;
    std::vector<RoadFrontageSegment> wallSegments;
    std::vector<std::pair<float, float>> mainOccupancyT;
    std::uint8_t                     exhausted = 0;
    mutable std::uint32_t              depthCacheTopologyGen = 0;
    mutable std::unordered_map<DepthCacheKey, float, DepthCacheKeyHash> depthCacheEntries;
};

struct Road {
    int   id     = -1;
    Vec2  a;
    Vec2  b;
    bool  isSecondary       = false;
    bool  isTerrainCorridor = false;
    bool  isBridge          = false;
    int   addedAtQueueIndex = -1;
    int   hostRoadId        = -1;
    int   hostBankIndex     = -1;
    int   junctionA         = -1;
    int   junctionB         = -1;
    RoadSideFrontage sideA;
    RoadSideFrontage sideB;

    float length() const { return (b - a).length(); }

    RoadSideFrontage* sideBank(int bankIndex) {
        return bankIndex == 1 ? &sideB : &sideA;
    }

    const RoadSideFrontage* sideBank(int bankIndex) const {
        return bankIndex == 1 ? &sideB : &sideA;
    }
};

enum class AlleyPlacementKind {
    Straight,
    DeadEnd,
    Turn,
};

struct SecondaryRoadRecord {
    Vec2               a{};
    Vec2               b{};
    int                hostRoadId        = -1;
    int                hostBankIndex     = -1;
    int                addedAtQueueIndex = -1;
    bool               isThrough         = false;
    float              probeAngleDeg     = 0.f;
    AlleyPlacementKind kind              = AlleyPlacementKind::Straight;
    float              turnAngleDeg      = 0.f;
};

struct PendingAlleyFill {
    int addedAtQueueIndex    = -1;
    int hostRoadId           = -1;
    int consecutiveFillFails = 0;
};

struct WallGapKey {
    int   roadId    = -1;
    int   bankIndex = -1;
    float tMin      = 0.f;
    float tMax      = 0.f;
};

struct WallGapKeyHash {
    std::size_t operator()(const WallGapKey& key) const noexcept {
        std::size_t h = static_cast<std::size_t>(key.roadId);
        h             = h * 31u + static_cast<std::size_t>(key.bankIndex);
        h             = h * 31u + static_cast<std::size_t>(key.tMin * 100.f);
        h             = h * 31u + static_cast<std::size_t>(key.tMax * 100.f);
        return h;
    }
};

struct WallGapKeyEqual {
    bool operator()(const WallGapKey& lhs, const WallGapKey& rhs) const noexcept {
        return lhs.roadId == rhs.roadId && lhs.bankIndex == rhs.bankIndex
               && std::abs(lhs.tMin - rhs.tMin) < 0.05f && std::abs(lhs.tMax - rhs.tMax) < 0.05f;
    }
};

struct AlleyProbeLine {
    Vec2 a{};
    Vec2 b{};
    bool valid = false;
};

// Road endpoint where one or more roads meet.
struct Junction {
    int                id = -1;
    Vec2               pos{};
    std::vector<int>   roadIds;
};

// Road-facing lot: corners 0–1 on the road (frontage), corners 2–3 inset (depth).
struct Plot {
    int   id      = -1;
    int   roadId  = -1;
    int   roadBank = -1;
    Vec2  corners[4] = {};
    float frontage     = 0.f;
    float depth        = 0.f;
    float area         = 0.f;
    Vec2  outlineTangent{};
    Vec2  outlineInward{};
};

// One building footprint inside a plot (axis-aligned or rotated rectangle).
struct BuildingFootprint {
    Vec2        corners[4] = {};
    std::string sizeCategory;
    bool        mainBuilding = false;
    int         doorEdge     = -1;  // edge index i: segment corners[i] -> corners[(i+1)%4]
    int         labelId      = -1;  // index within the parent plot (0 = main building)
    float       placedShortLen = 0.f;  // width used when the rectangle was built
    float       placedLongLen  = 0.f;  // height used when the rectangle was built
    bool        tmplDoorLong = false;
    bool        tmplDoorShort = false;
    bool        tmplLongFacingMiddle = false;
    bool        tmplEdgePlacement = false;
    bool        tmplMiddlePlacement = false;
    bool        tmplCornerPlacement = false;
    bool        tmplBackEdgePlacement = false;
};

enum class BuildingPlacementMode {
    PlotLot,
    SegmentGapFill,
    BorderPlot,
    BorderBuilding,
};

// One spawned building tied to a plot; removed when the growth slider moves down.
struct BuildingInstance {
    std::uint32_t         id = 0;
    BuildingTypeId        typeId = kInvalidBuildingTypeId;
    BuildingPlacementMode placementMode = BuildingPlacementMode::PlotLot;
    int                   roadId = -1;
    int                   roadBank = -1;  // 0/1 for secondary road banks
    Plot                  plot;
    std::vector<BuildingFootprint> footprints;
};

inline Vec2 polygonCentroid(const std::vector<Vec2>& polygon, const Vec2& fallback = {}) {
    if (polygon.size() < 3) {
        return polygon.empty() ? fallback : polygon[0];
    }

    double signedArea = 0.0;
    double cx         = 0.0;
    double cy         = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Vec2& p0 = polygon[i];
        const Vec2& p1 = polygon[(i + 1) % polygon.size()];
        const double cross =
            static_cast<double>(p0.x) * p1.y - static_cast<double>(p1.x) * p0.y;
        signedArea += cross;
        cx += (static_cast<double>(p0.x) + p1.x) * cross;
        cy += (static_cast<double>(p0.y) + p1.y) * cross;
    }
    signedArea *= 0.5;
    if (std::abs(signedArea) < 1e-8) {
        Vec2 avg{};
        for (const Vec2& p : polygon) {
            avg = avg + p;
        }
        return avg * (1.f / static_cast<float>(polygon.size()));
    }
    return {static_cast<float>(cx / (6.0 * signedArea)),
            static_cast<float>(cy / (6.0 * signedArea))};
}

// Screen-space label anchor (diagram pixels).
struct FrontageSegmentLabel {
    int   id        = -1;
    float centerXPx = 0.f;
    float centerYPx = 0.f;
};

// Rotated text label (diagram pixels); used for terrain plot verification.
struct RotatedTextLabel {
    std::string text;
    float       centerXPx   = 0.f;
    float       centerYPx   = 0.f;
    float       rotationDeg = 0.f;
};

struct WatersideProbeDebug {
    int       junctionId   = -1;
    Vec2      pos{};
    float     probeRadius  = 0.f;
    bool      hitValid     = false;
    Vec2      hitPoint{};
    float     hitDist      = 0.f;
    TerrainId hitKind      = kTerrainUnknown;
    TerrainId junctionKind = kTerrainUnknown;
    bool      isWaterside  = false;
};

struct BridgeBucket {
    int                     bridgeRoadId = -1;
    std::unordered_set<int> roadIds;
    bool                    revealed     = false;
};

struct Town {
    std::vector<Road> roads;
    std::vector<Junction> junctions;
    Vec2              center;
    float             radius   = 0.f;
    float             width    = 0.f;
    float             height   = 0.f;
    int               frontageSegmentIdCounter = 0;
    int               wallSegmentIdCounter     = 0;
    sf::VertexArray   roadMesh{sf::Triangles};
    std::unordered_set<int> watersideJunctionIds;
    float             bridgeWatersideProbeRadius = 0.f;
    std::vector<WatersideProbeDebug> watersideProbeDebug;
    std::vector<BridgeBucket>          bridgeBuckets;
    int                                seedRevealBridgeRoadId = -1;
    sf::VertexArray   junctionMesh{sf::Triangles};
    sf::VertexArray   roadEndProbeMesh{sf::Triangles};
    sf::VertexArray   frontageSegmentMesh{sf::Triangles};
    sf::VertexArray   frontageInwardArrowMesh{sf::Triangles};
    std::vector<BuildingInstance> buildingInstances;
    int               placementQueueCursor = 0;   // next growth-queue index to attempt
    int               placementBumpIndex   = -1;  // queue index for placementBumpCount
    int               placementBumpCount     = 0;   // bumps consumed for current index (persists across syncs)
    int               placementFailureCount = 0;  // skipped slots in [0, active target)
    std::vector<int>  placementFailedIndices;     // queue indices that failed to place
    int               moveFailureCount      = 0;  // movable buildings that could not relocate
    std::uint32_t     relocatingInstanceId  = 0xFFFFFFFFu;  // overlap skip during relocation trial
    int               relocatingHostRoadId  = -1;           // exclude old road during relocation trial
    std::string       placementSkipReasonsSummary;
    int               suburbanMaxHop = 2;
    int               urbanCoreMaxHop = -1;
    RingPhase         ringPhase = RingPhase::Normal;
    std::unordered_set<int> alleyCompleteRoadIds;
    std::vector<SecondaryRoadRecord> secondaryRoadRecords;
    std::vector<int>                 secondaryRoadIds;
    float             syncMinPlotFrontage   = 0.f;
    float             syncMinGapWidth       = 0.f;
    float             syncMinAlleyGapWidth  = 0.f;
    float             syncBorderOutlineProbeMaxDist = 20.f;
    float             syncBorderSampleStep          = 3.f;
    int               syncBorderMaxAttempts         = 32;
    float             syncPixelsPerUnit             = 10.f;
    float             syncFrontageSetback   = 2.f;
    const TerrainCatalog* syncTerrainCatalog = nullptr;
    TerrainProbeConfig    syncTerrainProbes{};
    std::vector<std::vector<AlleyProbeLine>> alleyProbesByQueueIndex;
    std::unordered_set<WallGapKey, WallGapKeyHash, WallGapKeyEqual> checkedAlleyGaps;
    std::vector<PendingAlleyFill>    pendingAlleyFills;
    int               primaryRoadCount = 0;
    sf::VertexArray   alleyProbeFailMesh{sf::Triangles};
    sf::VertexArray   hopDebugRoadMesh{sf::Triangles};
    sf::VertexArray   hopDebugJunctionMesh{sf::Triangles};
    sf::VertexArray   buildingOutlineMesh{sf::Triangles};
    sf::VertexArray   terrainBuildingOutlineMesh{sf::Triangles};
    std::vector<FrontageSegmentLabel> plotLabels;
    std::vector<FrontageSegmentLabel> buildingLabels;
    std::vector<RotatedTextLabel>     terrainPlotTypeLabels;
    sf::VertexArray   bridgeRoadMesh{sf::Triangles};
    sf::VertexArray   bridgeProbeCircleMesh{sf::Triangles};
    sf::VertexArray   bridgeProbeHitMesh{sf::Triangles};
    sf::VertexArray   bridgeCandidateJunctionMesh{sf::Triangles};
    std::vector<RotatedTextLabel> bridgeDebugLabels;
    std::vector<FrontageSegmentLabel> roadLabels;
    std::vector<FrontageSegmentLabel> frontageSegmentLabels;
    std::vector<FrontageSegmentLabel> roadEndProbeLabels;

    mutable bool              junctionHopCacheValid = false;
    mutable std::vector<int>  junctionHopCache;
    mutable std::vector<int>  roadHopCache;
    mutable int               suburbanRoadListMaxHop = -1;
    mutable std::vector<int>  suburbanRoadListCache;
    mutable int               ruralRoadListMaxHop = -1;
    mutable std::vector<int>  ruralRoadListCache;
    mutable std::vector<float> ringAvgDistByHop;
    mutable float              ringMeanSliceWidth = 0.f;
    mutable float              maxObservedRoadDist = 0.f;
    std::uint32_t             roadTopologyGeneration = 1;
    std::uint64_t             cachedSecondaryRecordsFingerprint = 0;
    bool                      frontageInitialized = false;
    int                       alleyProbesCapacity   = 0;
    int                       frontierNotifySuppressDepth = 0;
    FrontierManager           frontierManager;
    std::unordered_map<TerrainId, int> lastTerrainAnchorRoadId;
};

struct PlacementFloors;
struct TownConfig;
struct TerrainAtlas;
struct SecondaryRoadRecord;

void ensurePlacementSyncMins(Town& town, const PlacementFloors& floors, const TownConfig& townCfg,
                             float frontageSetback);
void ensureTownFrontageInitialized(Town& town, float setback, const PlacementFloors& floors,
                                   const TownConfig& townCfg, const TerrainAtlas* terrain = nullptr,
                                   const PlotConfig* plots = nullptr,
                                   const TerrainCatalog* catalog = nullptr,
                                   const TerrainProbeConfig* probes = nullptr);
void restoreBankFrontageFromInstances(Town& town, int roadId, int bankIndex, float frontageSetback);
void restoreRoadFrontageFromInstances(Town& town, int roadId, float frontageSetback);
void restoreBankWallFromInstances(Town& town, int roadId, int bankIndex, float frontageSetback);
void restoreRoadWallFromInstances(Town& town, int roadId, float frontageSetback);
void removeBuildingInstance(Town& town, int instanceId, float frontageSetback,
                            const TerrainAtlas* terrain = nullptr,
                            const PlotConfig* plots     = nullptr);
void applySecondaryRoadRecord(Town& town, const SecondaryRoadRecord& rec,
                              const TerrainAtlas* terrain = nullptr);

std::uint64_t secondaryRoadRecordsFingerprint(const Town& town);

void indexJunctions(Town& town);
void buildJunctionMesh(Town& town, float pixelsPerUnit, float radiusUnits = 1.f);
void assignRoadSideInwards(Town& town, const TerrainAtlas* terrain = nullptr);
void buildSecondaryRoadFrontageSegments(Road& road, Town& town, float frontageSetback);
void buildSecondaryWallSegments(Road& road, Town& town, float frontageSetback);
void trimSecondaryRoadRecords(Town& town, int targetCount);
void rebuildSecondaryRoadsFromRecords(Town& town, const TerrainAtlas* terrain = nullptr);
void removeSecondaryRoadAtQueueIndex(Town& town, int queueIndex);
void buildRoadEndProbeMesh(Town& town, float pixelsPerUnit, float probeLengthUnits = 2.f);
void resetRoadFrontageSegments(Town& town, float frontageSetback, bool resetSegmentIds = false);
void resetWallSegments(Town& town, float frontageSetback, bool resetSegmentIds = false);
void carveRoadFrontageForPlot(Town& town, const Plot& plot, float frontageSetback,
                              const TerrainAtlas* terrain = nullptr, bool notifyFrontier = false);
void carveRoadFrontageForFootprint(Town& town, int roadId, int bankIndex,
                                   const BuildingFootprint& mainFootprint);
void carveRoadWallForFootprint(Town& town, int roadId, int bankIndex,
                               const BuildingFootprint& mainFootprint,
                               const TerrainAtlas* terrain = nullptr, bool notifyFrontier = false);
bool pointInsideTownDisc(const Town& town, const Vec2& p);
bool roadFrameForBank(const Road& road, int bankIndex, Vec2& origin, Vec2& farEnd, Vec2& edgeDir);
void buildBridgeBuckets(Town& town, int maxHops);
void updateBridgeRevealFromBuildings(Town& town);
bool isBridgeRoadRevealed(const Town& town, int bridgeRoadId);
void rebuildRoadMesh(Town& town, const std::array<uint8_t, 3>& primaryColor,
                     const std::array<uint8_t, 3>& secondaryColor,
                     const std::array<uint8_t, 3>& bridgeColor, float pixelsPerUnit,
                     const TerrainAtlas* terrain, bool clipRoadsAtWater);
void buildBridgeDebugView(Town& town, float pixelsPerUnit,
                          const std::array<uint8_t, 3>& bridgeColor);
void appendStripedSegment(sf::VertexArray& tris, const Vec2& a, const Vec2& b, float thickness,
                          const sf::Color& colorA, const sf::Color& colorB, float stripeLength);
void appendJunctionDisc(sf::VertexArray& mesh, const sf::Vector2f& center, float radiusPx,
                          const sf::Color& color, int segments = 24);
