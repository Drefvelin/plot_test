// Union-find helpers plus duplicate / degenerate road removal. Public/shared API
// declared in RoadNetworkInternal.h.

#include "roads/RoadNetwork.h"
#include "roads/RoadNetworkInternal.h"

#include "util/Logger.h"
#include "placement/geometry/PlotGeometry.h"
#include "terrain/TerrainAtlas.h"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace roadnet {

int findUnionRoot(std::vector<int>& parent, int x) {
    while (parent[static_cast<std::size_t>(x)] != x) {
        parent[static_cast<std::size_t>(x)] =
            parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(x)])];
        x = parent[static_cast<std::size_t>(x)];
    }
    return x;
}

void uniteUnion(std::vector<int>& parent, int a, int b) {
    a = findUnionRoot(parent, a);
    b = findUnionRoot(parent, b);
    if (a != b) {
        parent[static_cast<std::size_t>(b)] = a;
    }
}

int removeDuplicateAndDegenerateRoads(Town& town) {
    indexJunctions(town);

    struct RoadKeep {
        int   roadIndex = -1;
        float length    = 0.f;
    };
    std::map<std::pair<int, int>, RoadKeep> bestForPair;
    std::vector<bool>                         keepRoad(town.roads.size(), false);
    int                                       removed = 0;

    auto recordPair = [&](int roadIndex, int ja, int jb, float len) {
        if (ja < 0 || jb < 0 || ja == jb) {
            ++removed;
            return;
        }
        const int lo = std::min(ja, jb);
        const int hi = std::max(ja, jb);
        const std::pair<int, int> key{lo, hi};
        RoadKeep&                 slot = bestForPair[key];
        if (slot.roadIndex < 0 || len > slot.length) {
            if (slot.roadIndex >= 0) {
                keepRoad[static_cast<std::size_t>(slot.roadIndex)] = false;
                ++removed;
            }
            slot.roadIndex = roadIndex;
            slot.length    = len;
            keepRoad[static_cast<std::size_t>(roadIndex)] = true;
        } else {
            ++removed;
        }
    };

    for (std::size_t i = 0; i < town.roads.size(); ++i) {
        const Road& road = town.roads[i];
        const float len  = road.length();
        if (len < kMinSegmentLen) {
            ++removed;
            continue;
        }
        recordPair(static_cast<int>(i), road.junctionA, road.junctionB, len);
    }

    std::vector<Road> keptRoads;
    keptRoads.reserve(town.roads.size());
    for (std::size_t i = 0; i < town.roads.size(); ++i) {
        if (keepRoad[i]) {
            keptRoads.push_back(town.roads[i]);
        }
    }
    for (std::size_t i = 0; i < keptRoads.size(); ++i) {
        keptRoads[i].id = static_cast<int>(i);
    }
    town.roads = std::move(keptRoads);
    indexJunctions(town);
    return removed;
}

}  // namespace roadnet
