// Junction indexing and per-road bank-inward assignment. Part of the Town
// subsystem split (data model in Town.h, shared helpers in Town.cpp /
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

#include <algorithm>
#include <cmath>

using namespace townint;

void indexJunctions(Town& town) {
    town.junctions.clear();

    for (Road& road : town.roads) {
        road.junctionA = -1;
        road.junctionB = -1;
    }

    auto findJunction = [&](const Vec2& pos) -> int {
        for (std::size_t i = 0; i < town.junctions.size(); ++i) {
            if (nearPoint(town.junctions[static_cast<std::size_t>(i)].pos, pos)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    auto assignEndpoint = [&](int roadId, int junctionId, bool endpointA) {
        if (roadId < 0 || roadId >= static_cast<int>(town.roads.size()) || junctionId < 0) {
            return;
        }
        Road& road = town.roads[static_cast<std::size_t>(roadId)];
        if (endpointA) {
            road.junctionA = junctionId;
        } else {
            road.junctionB = junctionId;
        }
    };

    auto linkRoadToJunction = [&](const Vec2& pos, int roadId, bool endpointA) {
        int junctionId = findJunction(pos);
        if (junctionId < 0) {
            Junction junction;
            junction.id = static_cast<int>(town.junctions.size());
            junction.pos = pos;
            junction.roadIds.push_back(roadId);
            town.junctions.push_back(junction);
            junctionId = junction.id;
        } else {
            Junction& junction = town.junctions[static_cast<std::size_t>(junctionId)];
            if (std::find(junction.roadIds.begin(), junction.roadIds.end(), roadId)
                == junction.roadIds.end()) {
                junction.roadIds.push_back(roadId);
            }
        }
        assignEndpoint(roadId, junctionId, endpointA);
    };

    for (const Road& road : town.roads) {
        if (road.id < 0) {
            continue;
        }
        linkRoadToJunction(road.a, road.id, true);
        linkRoadToJunction(road.b, road.id, false);
    }

    invalidateJunctionHopCache(town);
}

void assignRoadSideInwards(Town& town, const TerrainAtlas* terrain) {
    constexpr float kBankProbeDist = 2.f;
    for (Road& road : town.roads) {
        if (road.isBridge) {
            road.sideA.inward = Vec2{};
            road.sideB.inward = Vec2{};
            continue;
        }

        const Vec2 dir = (road.b - road.a).normalized();
        if (dir.length() < 1e-4f) {
            road.sideA.inward = Vec2{};
            road.sideB.inward = Vec2{};
            continue;
        }
        const Vec2 left = perpendicular(dir).normalized();

        if (terrain != nullptr && terrain->valid) {
            const Vec2 mid = (road.a + road.b) * 0.5f;
            const bool leftBuildable  = terrain->isBuildable(mid + left * kBankProbeDist);
            const bool rightBuildable = terrain->isBuildable(mid - left * kBankProbeDist);
            road.sideA.inward         = leftBuildable ? left : Vec2{};
            road.sideB.inward         = rightBuildable ? left * -1.f : Vec2{};
            continue;
        }

        road.sideA.inward = left;
        road.sideB.inward = left * -1.f;
    }
}
