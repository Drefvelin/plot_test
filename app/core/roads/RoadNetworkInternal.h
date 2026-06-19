#pragma once

// Private shared internals for the RoadNetwork subsystem. Not part of the public
// API (RoadNetwork.h). Holds the helper structs/constants and the helper
// declarations needed by the split topical implementation files
// (CorridorCull, RoadIntersectionSplit, WaterSanitize, BridgeResolve,
// RoadDedup). Definitions live in RoadNetwork.cpp (shared helpers) and the
// topical files.

#include "config/Config.h"
#include "terrain/TerrainCatalog.h"
#include "town/Town.h"
#include "common/Vec2.h"

#include <set>
#include <string>
#include <vector>

struct TerrainAtlas;

namespace roadnet {

constexpr float kInteriorCrossEps      = 0.03f;
constexpr float kMinSegmentLen         = 0.5f;
constexpr int   kBridgeSameSideMaxHops = 8;

struct ParallelCullSettings {
    float probeOffset   = 4.f;
    float parallelCos   = 0.98f;
    float sampleSpacing = 5.f;
};

struct CorridorChain {
    std::vector<int> junctions;
    std::vector<int> roadIds;
};

struct BridgeSettings {
    bool  snapEnabled  = true;
    float searchRadius = 8.f;
    float maxSpan      = 80.f;
};

struct WaterBodyRef {
    TerrainId   kind       = kTerrainUnknown;
    std::size_t graphIndex = 0;
    bool        valid      = false;

    bool sameWaterBodyAs(const WaterBodyRef& other) const {
        return valid && other.valid && kind == other.kind;
    }
};

struct ShoreJunction {
    int          junctionId = -1;
    WaterBodyRef waterBody{};
};

struct BridgeCandidate {
    int   ja     = -1;
    int   jb     = -1;
    float length = 0.f;
};

struct ShoreBridgeLookup {
    std::vector<const ShoreJunction*> byJunctionId;
};

// Shared helpers (defined in RoadNetwork.cpp unless noted).
void resetRoadFrontage(Road& road);
void uniqueSortedParams(std::vector<float>& params);

ParallelCullSettings       resolveParallelCullSettings(const Config& config);
std::vector<CorridorChain> buildCorridorChains(const Town& town);
void probeChainForParallelRoads(const CorridorChain& chain, const Town& town,
                                const ParallelCullSettings& settings, std::set<int>& culledRoadIds);

int  boundarySplitRoadsAtWater(Town& town, const TerrainAtlas& terrain);
int  snapNonBuildableJunctions(Town& town, const TerrainAtlas& terrain);
void collectWatersideJunctionIds(Town& town, const TerrainAtlas& terrain, float radius);
bool junctionHasLandRoad(const Town& town, int junctionId, const TerrainAtlas& terrain);

BridgeSettings resolveBridgeSettings(const Config& config);
void           updateJunctionPosition(Town& town, int junctionId, const Vec2& newPos);
std::string    junctionRoadIds(const Town& town, int junctionId);
std::string    junctionPosText(const Town& town, int junctionId);
WaterBodyRef nearestWatersideWaterBody(const Vec2& pos, const TerrainAtlas& terrain, float radius);
bool shoresMayBridge(const ShoreJunction& a, const ShoreJunction& b, const Vec2& posA,
                     const Vec2& posB, const TerrainAtlas& terrain);
bool junctionsConnectedWithinHops(const Town& town, int fromId, int toId, int maxHops);
ShoreBridgeLookup buildShoreBridgeLookup(const std::vector<ShoreJunction>& shoreJunctions,
                                         int junctionCount);
bool endpointHasBetterAvailablePartner(int endpoint, int currentPartner,
                                       const std::vector<char>&          matched,
                                       const std::vector<ShoreJunction>& shoreJunctions,
                                       const Town& town, const TerrainAtlas& terrain,
                                       const BridgeSettings& settings,
                                       const ShoreBridgeLookup& lookup);
bool endpointHasBetterSameBankForPartner(int endpoint, int currentPartner,
                                         const std::vector<ShoreJunction>& shoreJunctions,
                                         const Town& town);
std::string findBetterAvailablePartnerReason(int endpoint, int currentPartner,
                                             const std::vector<char>&          matched,
                                             const std::vector<ShoreJunction>& shoreJunctions,
                                             const Town& town, const TerrainAtlas& terrain,
                                             const BridgeSettings&    settings,
                                             const ShoreBridgeLookup& lookup);
std::string findBetterSameBankReason(int endpoint, int currentPartner,
                                     const std::vector<ShoreJunction>& shoreJunctions,
                                     const Town& town);
bool findBestBridgeChord(const Vec2& shoreA, const Vec2& shoreB, const TerrainAtlas& terrain,
                         const BridgeSettings& settings, Vec2& outA, Vec2& outB);

// Defined in RoadDedup.cpp.
int  findUnionRoot(std::vector<int>& parent, int x);
void uniteUnion(std::vector<int>& parent, int a, int b);
int  removeDuplicateAndDegenerateRoads(Town& town);

}  // namespace roadnet
