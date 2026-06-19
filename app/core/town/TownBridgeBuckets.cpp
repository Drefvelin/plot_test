// Bridge bucket construction, reveal tracking, and reveal queries. Part of the
// Town subsystem split (data model in Town.h, shared helpers in Town.cpp /
// TownInternal.h).

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

#include <string>
#include <unordered_set>

using namespace townint;

void buildBridgeBuckets(Town& town, int maxHops) {
    town.bridgeBuckets.clear();
    town.seedRevealBridgeRoadId = -1;

    float seedDist = 1e30f;
    for (const Road& road : town.roads) {
        if (!road.isBridge) {
            continue;
        }

        BridgeBucket bucket;
        bucket.bridgeRoadId = road.id;
        collectBridgeBucketRoads(town, road.id, maxHops, bucket.roadIds);
        town.bridgeBuckets.push_back(std::move(bucket));

        const Vec2 mid = (road.a + road.b) * 0.5f;
        const float  d = (mid - town.center).length();
        if (d < seedDist) {
            seedDist                    = d;
            town.seedRevealBridgeRoadId = road.id;
        }
    }

    for (BridgeBucket& bucket : town.bridgeBuckets) {
        if (bucket.bridgeRoadId == town.seedRevealBridgeRoadId) {
            bucket.revealed = true;
        }
    }

    Logger::log("bridge",
                "bridge_buckets count=" + std::to_string(town.bridgeBuckets.size()) + " hops="
                    + std::to_string(maxHops) + " seed_b="
                    + std::to_string(town.seedRevealBridgeRoadId));
}

void updateBridgeRevealFromBuildings(Town& town) {
    for (BridgeBucket& bucket : town.bridgeBuckets) {
        if (bucket.revealed) {
            continue;
        }
        for (const BuildingInstance& instance : town.buildingInstances) {
            const int roadId = buildingInstanceRoadId(instance);
            if (roadId >= 0 && bucket.roadIds.count(roadId) != 0) {
                bucket.revealed = true;
                Logger::log("bridge",
                            "bridge_revealed b=" + std::to_string(bucket.bridgeRoadId)
                                + " trigger_road=" + std::to_string(roadId));
                break;
            }
        }
    }
}

bool isBridgeRoadRevealed(const Town& town, int bridgeRoadId) {
    for (const BridgeBucket& bucket : town.bridgeBuckets) {
        if (bucket.bridgeRoadId == bridgeRoadId) {
            return bucket.revealed;
        }
    }
    return true;
}
