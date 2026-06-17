#pragma once

#include "Config.h"
#include "GrowthRings.h"
#include "PlacementPrep.h"

#include "DefCache.h"
#include "PlacementLogging.h"
#include "Town.h"

#include <string>

struct TerrainAtlas;

struct FrontageSlot {
    int   segmentId  = -1;
    int   roadId     = -1;
    int   bankIndex  = 0;
    float startT     = 0.f;
    float endT       = 0.f;
    float centerDist = 0.f;
    float zoneScore  = 0.f;
    bool  isWallGap  = false;

    float width() const { return endT - startT; }
};

void collectFrontageSlots(const Town& town, const DefCache& defs, const std::string& buildingType,
                          float townGrowth, std::vector<FrontageSlot>& out,
                          const BandFilter& bandFilter, int roadFilter,
                          bool skipPlotExhaustedBanks = true);

void buildSegmentTCandidates(const FrontageSlot& slot, float minFrontage, const Vec2& origin,
                             const Vec2& edgeDir, const Vec2& center, std::vector<float>& out);

bool tryPlaceRoadPlot(Town& town, const std::string& buildingType, const DefCache& defs,
                      const PlotConfig& plots, BuildingInstance& out, const PlacementPrep& prep,
                      int townSeed, int maxBuildings, PlacementSearchLog& searchLog,
                      const TerrainAtlas* terrain = nullptr,
                      const BandFilter& bandFilter = BandFilter::none(), int roadFilter = -1);
