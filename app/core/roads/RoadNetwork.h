#pragma once

#include "config/Config.h"
#include "town/Town.h"

struct TerrainAtlas;

void appendCorridorRoads(Town& town, const TerrainAtlas& atlas, const Config& config);
void splitRoadsAtIntersections(Town& town, float endpointEps = 0.08f);
void sanitizeRoadGraphAtWater(Town& town, const TerrainAtlas& terrain, const Config& config);
void mergeWatersideJunctions(Town& town, const TerrainAtlas& terrain, const Config& config);
void resolveBridges(Town& town, const TerrainAtlas& terrain, const Config& config);
void cullVoronoiRoadsParallelToCorridors(Town& town, const Config& config);
