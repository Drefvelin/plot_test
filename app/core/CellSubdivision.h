#pragma once

#include "Town.h"

#include <vector>

struct TerrainAtlas;

struct VoronoiCellSnapshot {
    int               id = -1;
    Vec2              site;
    std::vector<Vec2> boundary;
};

void subdivideCellsFromRoadGraph(Town& town, const TerrainAtlas& terrain,
                                 const std::vector<VoronoiCellSnapshot>& voronoiSnapshot);
