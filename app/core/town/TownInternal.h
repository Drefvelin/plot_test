#pragma once

// Private shared internals for the Town subsystem. Not part of the public data
// model (Town.h). Declares the internal helper functions (defined in Town.cpp)
// that the split topical implementation files (TownJunctions, TownFrontage,
// TownCarving, TownSecondary, TownBridgeBuckets, TownMeshes) need to call.

#include "town/Town.h"

#include <unordered_set>

namespace townint {

// Frontage / wall span helpers.
void  carveSideFrontage(RoadSideFrontage& side, const Vec2& origin, const Vec2& edgeDir,
                        float usedStart, float usedEnd, int& nextSegmentId);
float plotTMinLocal(const Plot& plot, const Vec2& origin, const Vec2& edgeDir);
float plotTMaxLocal(const Plot& plot, const Vec2& origin, const Vec2& edgeDir);
void  carveSideWall(RoadSideFrontage& side, const Vec2& origin, const Vec2& edgeDir,
                    float usedStart, float usedEnd, int& nextSegmentId);
void  initWallSegmentOnSide(RoadSideFrontage& side, const Vec2& origin, const Vec2& edgeDir,
                            float setback, float roadLen, int& nextWallId);
void  initBankPlotSegment(Road& road, int bankIndex, float setback, Town& town);
void  initBankWallSegment(Road& road, int bankIndex, float setback, Town& town);
bool  instanceUsesRoadBank(const BuildingInstance& inst, int roadId, int bankIndex);

// Road splitting / secondary record application.
void splitRoadsAtAlleyEndpoints(Town& town);
void applySecondaryRoadRecordImpl(Town& town, const SecondaryRoadRecord& rec,
                                  const TerrainAtlas* terrain);

// Bridge bucket traversal.
void collectBridgeBucketRoads(const Town& town, int bridgeRoadId, int maxHops,
                              std::unordered_set<int>& out);
int  buildingInstanceRoadId(const BuildingInstance& instance);

}  // namespace townint
