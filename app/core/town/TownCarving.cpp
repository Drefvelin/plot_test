// Frontage / wall carving for plots and footprints, and building-instance
// removal. Part of the Town subsystem split (data model in Town.h, shared
// helpers in Town.cpp / TownInternal.h).

#include "town/Town.h"
#include "town/TownInternal.h"

#include "common/RenderPrimitives.h"
#include "util/Logger.h"
#include "util/Profile.h"
#include "placement/orchestration/PlacementFloors.h"
#include "placement/frontier/PlacementFrontier.h"
#include "placement/frontier/FrontierManager.h"
#include "roads/RoadExhaustion.h"
#include "placement/geometry/PlotGeometry.h"
#include "placement/zones/FrontageZones.h"
#include "config/Config.h"
#include "terrain/TerrainAtlas.h"
#include "config/TownConfig.h"
#include "common/Units.h"

#include <algorithm>
#include <cmath>

using namespace townint;

void removeBuildingInstance(Town& town, int instanceId, float frontageSetback,
                            const TerrainAtlas* terrain, const PlotConfig* plots) {
    const auto it =
        std::find_if(town.buildingInstances.begin(), town.buildingInstances.end(),
                     [instanceId](const BuildingInstance& inst) { return inst.id == instanceId; });
    if (it == town.buildingInstances.end()) {
        return;
    }

    int  roadId       = -1;
    int  bankIdx      = 0;
    bool wasBorderPlot = it->placementMode == BuildingPlacementMode::BorderPlot
                         || it->placementMode == BuildingPlacementMode::BorderBuilding;
    if (it->placementMode == BuildingPlacementMode::SegmentGapFill) {
        roadId  = it->roadId;
        bankIdx = it->roadBank;
    } else if (it->placementMode == BuildingPlacementMode::PlotLot
               || it->placementMode == BuildingPlacementMode::BorderPlot) {
        roadId  = it->plot.roadId;
        bankIdx = it->plot.roadBank;
    } else if (it->placementMode == BuildingPlacementMode::BorderBuilding && it->roadId >= 0) {
        roadId  = it->roadId;
        bankIdx = it->roadBank;
    }

    town.buildingInstances.erase(it);

    if (roadId >= 0) {
        restoreBankFrontageFromInstances(town, roadId, bankIdx, frontageSetback);
    }

    {
        PlacementEvent event;
        event.type          = PlacementEventType::InstanceRemoved;
        event.roadId        = roadId;
        event.bankIndex     = bankIdx;
        event.instanceId    = instanceId;
        event.wasBorderPlot = wasBorderPlot;
        notifyPlacementFrontier(town, event, terrain, town.syncTerrainCatalog, &town.syncTerrainProbes,
                                plots);
    }
}

void carveRoadFrontageForPlot(Town& town, const Plot& plot, float /*frontageSetback*/,
                              const TerrainAtlas* terrain, bool notifyFrontier) {
    PROFILE_SCOPE(ProfileScopeId::FrontageCarve);
    if (plot.roadId < 0 || plot.roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road& road = town.roads[static_cast<std::size_t>(plot.roadId)];
    RoadSideFrontage* side = road.sideBank(plot.roadBank);

    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDir{};
    if (!roadFrameForBank(road, plot.roadBank, origin, farEnd, edgeDir)) {
        return;
    }

    const float tMin = plotTMinLocal(plot, origin, edgeDir);
    const float tMax = plotTMaxLocal(plot, origin, edgeDir);
    carveSideFrontage(*side, origin, edgeDir, tMin, tMax, town.frontageSegmentIdCounter);
    clearAlleyGapStateForRoad(town, plot.roadId);
    refreshBankExhaustionAfterCarve(town, plot.roadId, plot.roadBank);
    if (notifyFrontier) {
        PlacementEvent event;
        event.type       = PlacementEventType::PlotCarved;
        event.roadId     = plot.roadId;
        event.bankIndex  = plot.roadBank;
        event.carveTMin  = tMin;
        event.carveTMax  = tMax;
        event.wasBorderPlot = plot.outlineTangent.length() > 1e-4f;
        notifyPlacementFrontier(town, event, terrain, town.syncTerrainCatalog, &town.syncTerrainProbes,
                            nullptr);
    } else {
        frontierRefreshPlotBank(town, plot.roadId, plot.roadBank);
    }
    rebuildMainOccupancyForBank(town, plot.roadId, plot.roadBank);
}

void carveRoadFrontageForPlot(Town& town, const Plot& plot, float frontageSetback) {
    carveRoadFrontageForPlot(town, plot, frontageSetback, nullptr, false);
}

void carveRoadFrontageForFootprint(Town& town, int roadId, int bankIndex,
                                   const BuildingFootprint& mainFootprint) {
    PROFILE_SCOPE(ProfileScopeId::FrontageCarve);
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road& road = town.roads[static_cast<std::size_t>(roadId)];
    RoadSideFrontage* side = road.sideBank(bankIndex);

    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDir{};
    if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
        return;
    }

    float usedStart = 1e9f;
    float usedEnd   = -1e9f;
    for (const Vec2& corner : mainFootprint.corners) {
        const float t = (corner - origin).dot(edgeDir);
        usedStart = std::min(usedStart, t);
        usedEnd = std::max(usedEnd, t);
    }

    carveSideFrontage(*side, origin, edgeDir, usedStart, usedEnd, town.frontageSegmentIdCounter);
    clearAlleyGapStateForRoad(town, roadId);
    refreshBankExhaustionAfterCarve(town, roadId, bankIndex);
    frontierRefreshPlotBank(town, roadId, bankIndex);
    rebuildMainOccupancyForBank(town, roadId, bankIndex);
}

void carveRoadWallForFootprint(Town& town, int roadId, int bankIndex,
                               const BuildingFootprint& mainFootprint, const TerrainAtlas* terrain,
                               bool notifyFrontier) {
    PROFILE_SCOPE(ProfileScopeId::FrontageWallCarve);
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road&             road = town.roads[static_cast<std::size_t>(roadId)];
    RoadSideFrontage* side = road.sideBank(bankIndex);

    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDir{};
    if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
        return;
    }

    float usedStart = 1e9f;
    float usedEnd   = -1e9f;
    for (const Vec2& corner : mainFootprint.corners) {
        const float t = (corner - origin).dot(edgeDir);
        usedStart     = std::min(usedStart, t);
        usedEnd       = std::max(usedEnd, t);
    }

    carveSideWall(*side, origin, edgeDir, usedStart, usedEnd, town.wallSegmentIdCounter);
    clearAlleyGapStateForRoad(town, roadId);
    refreshBankExhaustionAfterCarve(town, roadId, bankIndex);
    if (notifyFrontier) {
        PlacementEvent event;
        event.type      = PlacementEventType::PlotCarved;
        event.roadId    = roadId;
        event.bankIndex = bankIndex;
        event.carveTMin = usedStart;
        event.carveTMax = usedEnd;
        notifyPlacementFrontier(town, event, terrain, town.syncTerrainCatalog, &town.syncTerrainProbes,
                            nullptr);
    } else {
        frontierRefreshWallBank(town, roadId, bankIndex);
        frontierRefreshAlleyBank(town, roadId, bankIndex, town.syncMinAlleyGapWidth);
    }
}

void carveRoadWallForFootprint(Town& town, int roadId, int bankIndex,
                               const BuildingFootprint& mainFootprint) {
    carveRoadWallForFootprint(town, roadId, bankIndex, mainFootprint, nullptr, false);
}
