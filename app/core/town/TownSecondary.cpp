// Secondary-road record fingerprinting, rebuild, removal, and application. Part
// of the Town subsystem split (data model in Town.h, shared helpers in Town.cpp
// / TownInternal.h).

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
#include <cstring>

using namespace townint;

void trimSecondaryRoadRecords(Town& town, int targetCount) {
    town.secondaryRoadRecords.erase(
        std::remove_if(town.secondaryRoadRecords.begin(), town.secondaryRoadRecords.end(),
                       [targetCount](const SecondaryRoadRecord& rec) {
                           return rec.addedAtQueueIndex >= targetCount;
                       }),
        town.secondaryRoadRecords.end());
}

std::uint64_t secondaryRoadRecordsFingerprint(const Town& town) {
    const auto floatBits = [](float value) -> std::uint64_t {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };

    std::uint64_t hash = 14695981039346656037ULL;
    for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
        const auto mix = [&hash](std::uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ULL;
        };
        mix(static_cast<std::uint64_t>(rec.hostRoadId));
        mix(static_cast<std::uint64_t>(rec.hostBankIndex));
        mix(static_cast<std::uint64_t>(rec.addedAtQueueIndex));
        mix(floatBits(rec.a.x));
        mix(floatBits(rec.a.y));
        mix(floatBits(rec.b.x));
        mix(floatBits(rec.b.y));
    }
    return hash;
}

void rebuildSecondaryRoadsFromRecords(Town& town, const TerrainAtlas* terrain) {
    town.roads.erase(std::remove_if(town.roads.begin(), town.roads.end(),
                                    [](const Road& road) { return road.isSecondary; }),
                     town.roads.end());

    for (std::size_t i = 0; i < town.roads.size(); ++i) {
        town.roads[i].id = static_cast<int>(i);
    }
    town.primaryRoadCount = static_cast<int>(town.roads.size());

    for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
        Road road;
        road.id = static_cast<int>(town.roads.size());
        road.a = rec.a;
        road.b = rec.b;
        road.hostRoadId = rec.hostRoadId;
        road.hostBankIndex = rec.hostBankIndex;
        road.isSecondary = true;
        road.addedAtQueueIndex = rec.addedAtQueueIndex;
        town.roads.push_back(road);
    }

    assignRoadSideInwards(town, terrain);
    for (Road& road : town.roads) {
        if (road.isSecondary) {
            buildSecondaryRoadFrontageSegments(road, town, 2.f);
            buildSecondaryWallSegments(road, town, 2.f);
        }
    }
    splitRoadsAtAlleyEndpoints(town);
    indexJunctions(town);
    invalidateRoadTopologyCaches(town);
    town.cachedSecondaryRecordsFingerprint = secondaryRoadRecordsFingerprint(town);
    rebuildSecondaryRoadIdList(town);
    PlacementEvent event;
    event.type = PlacementEventType::TopologyChanged;
    notifyPlacementFrontier(town, event, terrain, town.syncTerrainCatalog, &town.syncTerrainProbes,
                            nullptr);
}

void removeSecondaryRoadAtQueueIndex(Town& town, int queueIndex) {
    town.secondaryRoadRecords.erase(
        std::remove_if(town.secondaryRoadRecords.begin(), town.secondaryRoadRecords.end(),
                       [queueIndex](const SecondaryRoadRecord& rec) {
                           return rec.addedAtQueueIndex == queueIndex;
                       }),
        town.secondaryRoadRecords.end());
    rebuildSecondaryRoadsFromRecords(town);
}

void applySecondaryRoadRecord(Town& town, const SecondaryRoadRecord& rec,
                              const TerrainAtlas* terrain) {
    applySecondaryRoadRecordImpl(town, rec, terrain);
}
