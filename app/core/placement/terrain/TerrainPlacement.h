#pragma once

#include "config/DefCache.h"
#include "terrain/TerrainAtlas.h"
#include "town/Town.h"

#include <vector>

struct FrontageSlot;

constexpr float kTerrainScoreWeight = 3.f;

TerrainPlacementMode effectiveTerrainMode(const BuildingDef& def, const TerrainAtlas& atlas);

bool terrainKindMatchesPrefer(TerrainId sampled, TerrainId prefer);

bool terrainKindMatchesPrefer(TerrainId sampled, const std::vector<TerrainId>& kinds);

bool plotMeetsInsideMin(const Plot& plot, const TerrainAtlas& terrain, TerrainId prefer);

bool segmentWithinProximityMax(const Town& town, const FrontageSlot& slot, const BuildingDef& def,
                               const TerrainAtlas& atlas);

bool usesTerrainPlacementScan(const DefCache& defs, const std::string& buildingType,
                              const TerrainAtlas* terrain);

bool isTerrainRequirementStrict(const BuildingDef& def);

bool buildingHasTerrainPlacement(const DefCache& defs, const std::string& buildingType);
bool buildingHasTerrainPlacement(const DefCache& defs, BuildingTypeId typeId);

void recordTerrainAnchorRoad(Town& town, TerrainId prefer, int roadId, int queueIndex = -1,
                             const char* source = nullptr);

int terrainAnchorRoadFor(const Town& town, TerrainId prefer);

struct TerrainAnchorBfsResult {
    std::vector<int> selected;
    std::vector<int> visitOrder;
    std::vector<int> visitDist;
};

TerrainAnchorBfsResult collectRoadsNearTerrainAnchor(const Town& town, int anchorRoadId,
                                                     int maxRoads);

int roadIdForSegment(const Town& town, int segmentId);

float terrainScoreForPlot(const Plot& plot, const BuildingDef& def, const TerrainAtlas& atlas);

float terrainScoreForPoint(const Vec2& point, const BuildingDef& def, const TerrainAtlas& atlas);

const std::vector<std::vector<Vec2>>* outlineGraphsForPrefer(const TerrainAtlas& atlas,
                                                              TerrainId prefer);

float distToPreferEdge(const Vec2& point, TerrainId prefer, const TerrainAtlas& atlas);

float distToPreferEdge(const Vec2& point, const std::vector<TerrainId>& kinds,
                       const TerrainAtlas& atlas);

constexpr float kBorderHugEdgeEpsilon = 1.0f;

struct OutlineSnap {
    Vec2        point{};
    int         graphIndex = -1;
    int         edgeIndex  = -1;
    float       edgeT      = 0.f;
    float       polylineT  = 0.f;
    Vec2        tangent{};
    Vec2        featureInward{};
    bool        valid      = false;
};

bool snapToPreferOutline(const Vec2& query, TerrainId prefer, const TerrainAtlas& terrain,
                         const Vec2& landReference, OutlineSnap& out);

bool snapToPreferOutline(const Vec2& query, const std::vector<TerrainId>& kinds,
                         const TerrainAtlas& terrain, const Vec2& landReference, OutlineSnap& out);

bool projectToPreferOutline(const Vec2& from, const Vec2& dir, TerrainId prefer,
                            const TerrainAtlas& terrain, float maxDist, Vec2& outPoint);

bool projectToPreferOutline(const Vec2& from, const Vec2& dir,
                            const std::vector<TerrainId>& kinds, const TerrainAtlas& terrain,
                            float maxDist, Vec2& outPoint);

struct OutlineRayHit {
    Vec2        point{};
    float       dist     = 0.f;
    int         graphIndex = -1;
    int         edgeIndex  = -1;
    float       edgeT      = 0.f;
    float       polylineT  = 0.f;
    bool        valid      = false;
};

bool rayHitPreferOutline(const Vec2& from, const Vec2& dir, TerrainId prefer,
                         const TerrainAtlas& terrain, float maxDist, OutlineRayHit& out);

float polylineGraphLength(const std::vector<Vec2>& graph);
bool  samplePolylineGraphAtArc(const std::vector<Vec2>& graph, float arcT, Vec2& outPoint,
                               Vec2& outTangent);

bool plotMeetsBorderHug(const Plot& plot, TerrainId kind, const BuildingTerrainRules& rules,
                        const TerrainAtlas& terrain, float epsilon = kBorderHugEdgeEpsilon);

bool plotMeetsBorderHug(const Plot& plot, const BuildingTerrainRules& rules,
                        const TerrainAtlas& terrain, float epsilon = kBorderHugEdgeEpsilon);

bool plotMeetsBorderBand(const Plot& plot, TerrainId kind, const BuildingTerrainRules& rules,
                         const TerrainAtlas& atlas);

bool plotMeetsBorderBand(const Plot& plot, const BuildingTerrainRules& rules,
                         const TerrainAtlas& terrain);

bool footprintMeetsBorderHug(const BuildingFootprint& footprint, TerrainId kind,
                             const BuildingTerrainRules& rules, const TerrainAtlas& terrain,
                             float epsilon = kBorderHugEdgeEpsilon);

bool footprintMeetsBorderBand(const BuildingFootprint& footprint, TerrainId kind,
                              const BuildingTerrainRules& rules, const TerrainAtlas& terrain);

float segmentTerrainScore(const Town& town, const FrontageSlot& slot, const BuildingDef& def,
                          const TerrainAtlas& atlas);
