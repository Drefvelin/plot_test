#include "placement/orchestration/PlacementPrep.h"

#include "placement/orchestration/BuildingGrowthQueue.h"
#include "placement/zones/FrontageZones.h"

#include <cstring>

void RoadAttemptMemo::syncContext(const Town& town) {
    if (topologyGen != town.roadTopologyGeneration || instanceCount != static_cast<int>(town.buildingInstances.size())
        || suburbanMaxHop != town.suburbanMaxHop || ringPhase != town.ringPhase) {
        topologyGen    = town.roadTopologyGeneration;
        instanceCount  = static_cast<int>(town.buildingInstances.size());
        suburbanMaxHop = town.suburbanMaxHop;
        ringPhase      = town.ringPhase;
        failedRoadIds.clear();
    }
}

void RoadAttemptMemo::clearFailures() {
    failedRoadIds.clear();
}

bool RoadAttemptMemo::shouldSkipRoad(int roadId) const {
    return failedRoadIds.count(roadId) != 0;
}

void RoadAttemptMemo::recordFailure(int roadId) {
    failedRoadIds.insert(roadId);
}

PlacementPrep buildPlacementPrep(const Town& town, const DefCache& defs,
                                 const std::string& buildingType, int buildingId, int townSeed,
                                 int maxBuildings) {
    PlacementPrep prep;
    prep.targetArea   = samplePlotTargetArea(defs, buildingType, buildingId, townSeed);
    prep.plotAreaRange = computePlotAreaBand(defs, buildingType, buildingId, townSeed);
    prep.plotAreaBand.name    = "plot";
    prep.plotAreaBand.minArea = prep.plotAreaRange.minArea;
    prep.plotAreaBand.maxArea = prep.plotAreaRange.maxArea;
    prep.buildingSpecs = resolveBuildingSpecs(defs, buildingType, buildingId, townSeed);
    sampleOrientationOrder(buildingId, townSeed, prep.orientFirst, prep.orientSecond);
    prep.townGrowth =
        maxBuildings > 0
            ? static_cast<float>(town.buildingInstances.size()) / static_cast<float>(maxBuildings)
            : 0.f;
    prep.zoneType = zoneTypeForBuilding(defs, buildingType);
    prep.zoneBias = zoneBiasForType(prep.zoneType, prep.townGrowth);

    if (isGapFillBuildingType(defs, buildingType)
        && resolveMainBuildingSpec(defs, buildingType, buildingId, townSeed, prep.mainSpec)) {
        const SizeBand* sizeBand = defs.buildingSizeBand(prep.mainSpec.sizeCategory);
        if (sizeBand != nullptr) {
            prep.gapFillReady    = true;
            prep.minSegmentWidth = std::max(2.f, std::sqrt(sizeBand->minArea / kBuildingAspectMax));
        }
    }

    return prep;
}
