// Splits roads at interior crossings so the Voronoi graph becomes planar. Public
// API declared in RoadNetwork.h; shared helpers in RoadNetworkInternal.h.

#include "roads/RoadNetwork.h"
#include "roads/RoadNetworkInternal.h"

#include "util/Logger.h"
#include "placement/geometry/PlotGeometry.h"
#include "terrain/TerrainAtlas.h"

#include <vector>

using namespace roadnet;

void splitRoadsAtIntersections(Town& town, float endpointEps) {
    const int roadCount = static_cast<int>(town.roads.size());
    if (roadCount == 0) {
        return;
    }

    std::vector<std::vector<float>> splitParams(static_cast<std::size_t>(roadCount));
    for (int i = 0; i < roadCount; ++i) {
        splitParams[static_cast<std::size_t>(i)] = {0.f, 1.f};
    }

    for (int i = 0; i < roadCount; ++i) {
        const Road& roadA = town.roads[static_cast<std::size_t>(i)];
        for (int j = i + 1; j < roadCount; ++j) {
            const Road& roadB = town.roads[static_cast<std::size_t>(j)];
            float       tA    = 0.f;
            float       tB    = 0.f;
            if (!segmentCrossingParams(roadA.a, roadA.b, roadB.a, roadB.b, tA, tB)) {
                continue;
            }
            if (tA > kInteriorCrossEps && tA < 1.f - kInteriorCrossEps && tB > kInteriorCrossEps
                && tB < 1.f - kInteriorCrossEps) {
                splitParams[static_cast<std::size_t>(i)].push_back(tA);
                splitParams[static_cast<std::size_t>(j)].push_back(tB);
            }
        }
    }

    std::vector<Road> splitRoads;
    splitRoads.reserve(static_cast<std::size_t>(roadCount) * 2);

    for (int i = 0; i < roadCount; ++i) {
        const Road& source = town.roads[static_cast<std::size_t>(i)];
        const float len    = source.length();
        if (len < 1e-4f) {
            continue;
        }

        std::vector<float> params = splitParams[static_cast<std::size_t>(i)];
        uniqueSortedParams(params);

        for (std::size_t p = 1; p < params.size(); ++p) {
            const float t0 = params[p - 1];
            const float t1 = params[p];
            if ((t1 - t0) * len < kMinSegmentLen) {
                continue;
            }

            Road segment  = source;
            segment.a     = lerpVec(source.a, source.b, t0);
            segment.b     = lerpVec(source.a, source.b, t1);
            resetRoadFrontage(segment);
            splitRoads.push_back(segment);
        }
    }

    for (std::size_t i = 0; i < splitRoads.size(); ++i) {
        splitRoads[i].id = static_cast<int>(i);
    }
    town.roads = std::move(splitRoads);

    (void)endpointEps;
}
