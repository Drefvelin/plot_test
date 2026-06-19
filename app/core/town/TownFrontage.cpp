// Frontage / wall segment builders, sync-min setup, frontage initialization, and
// restore-from-instances. Part of the Town subsystem split (data model in
// Town.h, shared helpers in Town.cpp / TownInternal.h).

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

void buildSecondaryRoadFrontageSegments(Road& road, Town& town, float setback) {
    const float len = road.length();
    if (len < setback * 2.f + 0.5f) {
        return;
    }

    for (int bank = 0; bank < 2; ++bank) {
        RoadSideFrontage* side = road.sideBank(bank);
        if (side->inward.length() < 1e-4f) {
            continue;
        }

        Vec2 origin{};
        Vec2 farEnd{};
        Vec2 edgeDir{};
        if (!roadFrameForBank(road, bank, origin, farEnd, edgeDir)) {
            continue;
        }

        RoadFrontageSegment segment;
        segment.id = town.frontageSegmentIdCounter++;
        segment.startT = setback;
        segment.endT = len - setback;
        side->segments.push_back(segment);
    }
}

void resetRoadFrontageSegments(Town& town, float frontageSetback, bool resetSegmentIds) {
    if (resetSegmentIds) {
        town.frontageSegmentIdCounter = 0;
    }

    for (Road& road : town.roads) {
        road.sideA.segments.clear();
        road.sideB.segments.clear();
        if (road.isBridge) {
            continue;
        }

        const float len = road.length();
        if (len < frontageSetback * 2.f + 0.5f) {
            continue;
        }

        for (int bank = 0; bank < 2; ++bank) {
            RoadSideFrontage* side = road.sideBank(bank);
            if (side->inward.length() < 1e-4f) {
                continue;
            }

            Vec2 origin{};
            Vec2 farEnd{};
            Vec2 edgeDir{};
            if (!roadFrameForBank(road, bank, origin, farEnd, edgeDir)) {
                continue;
            }

            RoadFrontageSegment segment;
            segment.id = town.frontageSegmentIdCounter++;
            segment.startT = frontageSetback;
            segment.endT = len - frontageSetback;
            side->segments.push_back(segment);
        }
    }
}

void buildSecondaryWallSegments(Road& road, Town& town, float setback) {
    const float len = road.length();
    if (len < setback * 2.f + 0.5f) {
        return;
    }

    for (int bank = 0; bank < 2; ++bank) {
        RoadSideFrontage* side = road.sideBank(bank);
        Vec2              origin{};
        Vec2              farEnd{};
        Vec2              edgeDir{};
        if (!roadFrameForBank(road, bank, origin, farEnd, edgeDir)) {
            continue;
        }
        initWallSegmentOnSide(*side, origin, edgeDir, setback, len, town.wallSegmentIdCounter);
    }
}

void resetWallSegments(Town& town, float frontageSetback, bool resetSegmentIds) {
    if (resetSegmentIds) {
        town.wallSegmentIdCounter = 0;
    }

    for (Road& road : town.roads) {
        road.sideA.wallSegments.clear();
        road.sideB.wallSegments.clear();
        if (road.isBridge) {
            continue;
        }

        const float len = road.length();
        if (len < frontageSetback * 2.f + 0.5f) {
            continue;
        }

        for (int bank = 0; bank < 2; ++bank) {
            RoadSideFrontage* side = road.sideBank(bank);
            Vec2              origin{};
            Vec2              farEnd{};
            Vec2              edgeDir{};
            if (!roadFrameForBank(road, bank, origin, farEnd, edgeDir)) {
                continue;
            }
            initWallSegmentOnSide(*side, origin, edgeDir, frontageSetback, len,
                                  town.wallSegmentIdCounter);
        }
    }
}

void ensurePlacementSyncMins(Town& town, const PlacementFloors& floors, const TownConfig& townCfg,
                             float frontageSetback) {
    town.syncMinPlotFrontage  = floors.minPlotFrontage;
    town.syncMinPlotDepth     = floors.minPlotDepth;
    town.syncMinGapWidth      = floors.minGapWidth;
    town.syncMinAlleyGapWidth = townCfg.minWallGapForAlley;
    town.syncBorderOutlineProbeMaxDist = townCfg.borderOutlineProbeMaxDist;
    town.syncBorderSampleStep          = townCfg.borderOutlineSampleStep;
    town.syncBorderMaxAttempts         = townCfg.borderMaxAttempts;
    town.syncFrontageSetback           = frontageSetback;
}

void ensureTownFrontageInitialized(Town& town, float setback, const PlacementFloors& floors,
                                   const TownConfig& townCfg, const TerrainAtlas* terrain,
                                   const PlotConfig* plots, const TerrainCatalog* catalog,
                                   const TerrainProbeConfig* probes) {
    if (town.frontageInitialized) {
        return;
    }
    if (catalog != nullptr) {
        town.syncTerrainCatalog = catalog;
    } else if (terrain != nullptr && terrain->catalog != nullptr) {
        town.syncTerrainCatalog = terrain->catalog;
    }
    if (probes != nullptr) {
        town.syncTerrainProbes = *probes;
    }
    town.syncTerrainAtlas = (terrain != nullptr && terrain->valid) ? terrain : nullptr;
    ensurePlacementSyncMins(town, floors, townCfg, setback);
    resetRoadFrontageSegments(town, setback, true);
    resetWallSegments(town, setback, true);
    clearAllRoadExhaustion(town);
    recomputePlotGapDoneTown(town);
    PlacementEvent event;
    event.type = PlacementEventType::FullRebuild;
    notifyPlacementFrontier(town, event, terrain, town.syncTerrainCatalog, &town.syncTerrainProbes,
                            plots);
    rebuildAllMainOccupancyT(town);
    rebuildSecondaryRoadIdList(town);
    town.frontageInitialized              = true;
    town.cachedSecondaryRecordsFingerprint = secondaryRoadRecordsFingerprint(town);
}

void restoreBankFrontageFromInstances(Town& town, int roadId, int bankIndex, float frontageSetback) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road& road = town.roads[static_cast<std::size_t>(roadId)];
    initBankPlotSegment(road, bankIndex, frontageSetback, town);
    initBankWallSegment(road, bankIndex, frontageSetback, town);
    clearAlleyGapStateForRoad(town, roadId);

    for (const BuildingInstance& inst : town.buildingInstances) {
        if (!instanceUsesRoadBank(inst, roadId, bankIndex)) {
            continue;
        }
        if (inst.placementMode == BuildingPlacementMode::SegmentGapFill) {
            if (!inst.footprints.empty()) {
                carveRoadFrontageForFootprint(town, inst.roadId, inst.roadBank, inst.footprints[0]);
                carveRoadWallForFootprint(town, inst.roadId, inst.roadBank, inst.footprints[0]);
            }
        } else {
            carveRoadFrontageForPlot(town, inst.plot, frontageSetback);
            for (const BuildingFootprint& footprint : inst.footprints) {
                if (footprint.mainBuilding) {
                    carveRoadWallForFootprint(town, inst.plot.roadId, inst.plot.roadBank, footprint);
                }
            }
        }
    }

    refreshBankExhaustionAfterCarve(town, roadId, bankIndex);
    rebuildMainOccupancyForBank(town, roadId, bankIndex);
    frontierRefreshPlotBank(town, roadId, bankIndex);
    frontierRefreshWallBank(town, roadId, bankIndex);
    frontierRefreshAlleyBank(town, roadId, bankIndex, town.syncMinAlleyGapWidth);
}

void restoreRoadFrontageFromInstances(Town& town, int roadId, float frontageSetback) {
    for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
        restoreBankFrontageFromInstances(town, roadId, bankIndex, frontageSetback);
    }
}

void restoreBankWallFromInstances(Town& town, int roadId, int bankIndex, float frontageSetback) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road& road = town.roads[static_cast<std::size_t>(roadId)];
    initBankWallSegment(road, bankIndex, frontageSetback, town);

    for (const BuildingInstance& inst : town.buildingInstances) {
        if (!instanceUsesRoadBank(inst, roadId, bankIndex)) {
            continue;
        }
        if (inst.placementMode == BuildingPlacementMode::SegmentGapFill) {
            if (!inst.footprints.empty()) {
                carveRoadWallForFootprint(town, inst.roadId, inst.roadBank, inst.footprints[0]);
            }
        } else {
            for (const BuildingFootprint& footprint : inst.footprints) {
                if (footprint.mainBuilding) {
                    carveRoadWallForFootprint(town, inst.plot.roadId, inst.plot.roadBank, footprint);
                }
            }
        }
    }
}

void restoreRoadWallFromInstances(Town& town, int roadId, float frontageSetback) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    clearAlleyGapStateForRoad(town, roadId);
    for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
        restoreBankWallFromInstances(town, roadId, bankIndex, frontageSetback);
        refreshBankExhaustionAfterCarve(town, roadId, bankIndex);
        rebuildMainOccupancyForBank(town, roadId, bankIndex);
        frontierRefreshWallBank(town, roadId, bankIndex);
        frontierRefreshAlleyBank(town, roadId, bankIndex, town.syncMinAlleyGapWidth);
    }
}
