// Clips the road graph to land, splits across water, snaps non-buildable
// junctions, and records waterside junctions. Public API declared in
// RoadNetwork.h; shared helpers in RoadNetworkInternal.h.

#include "roads/RoadNetwork.h"
#include "roads/RoadNetworkInternal.h"

#include "util/Logger.h"
#include "placement/geometry/PlotGeometry.h"
#include "terrain/TerrainAtlas.h"

#include <string>
#include <utility>
#include <vector>

using namespace roadnet;

void sanitizeRoadGraphAtWater(Town& town, const TerrainAtlas& terrain, const Config& config) {
    if (!terrain.valid) {
        return;
    }

    const int boundarySplits = boundarySplitRoadsAtWater(town, terrain);

    int removed         = 0;
    int splitMultiland  = 0;
    std::vector<Road> keptRoads;
    keptRoads.reserve(town.roads.size());

    for (const Road& source : town.roads) {
        if (source.isBridge) {
            keptRoads.push_back(source);
            continue;
        }

        const std::vector<std::pair<float, float>> intervals =
            clipRoadSegmentToLand(source.a, source.b, terrain);
        if (intervals.empty()) {
            ++removed;
            continue;
        }

        const Vec2 delta = source.b - source.a;
        const float len  = delta.length();
        if (len < 1e-4f) {
            if (terrain.isBuildable(source.a)) {
                keptRoads.push_back(source);
            } else {
                ++removed;
            }
            continue;
        }

        if (intervals.size() > 1) {
            splitMultiland += static_cast<int>(intervals.size()) - 1;
        }

        for (const auto& interval : intervals) {
            Road segment = source;
            segment.a    = source.a + delta * interval.first;
            segment.b    = source.a + delta * interval.second;
            resetRoadFrontage(segment);
            if (segment.length() < kMinSegmentLen) {
                ++removed;
                continue;
            }
            keptRoads.push_back(segment);
        }
    }

    for (std::size_t i = 0; i < keptRoads.size(); ++i) {
        keptRoads[i].id = static_cast<int>(i);
    }
    town.roads = std::move(keptRoads);

    indexJunctions(town);

    const int junctionsSnapped = snapNonBuildableJunctions(town, terrain);
    if (junctionsSnapped > 0) {
        indexJunctions(town);
    }

    Logger::log("voronoi",
                "water_sanitize boundary_splits=" + std::to_string(boundarySplits) + " removed="
                    + std::to_string(removed) + " split_multiland=" + std::to_string(splitMultiland)
                    + " junctions_snapped=" + std::to_string(junctionsSnapped));
    Logger::log("bridge",
                "sanitize boundary_splits=" + std::to_string(boundarySplits) + " removed="
                    + std::to_string(removed) + " split_multiland=" + std::to_string(splitMultiland)
                    + " junctions_snapped=" + std::to_string(junctionsSnapped));

    collectWatersideJunctionIds(town, terrain, config.terrain.bridgeWatersideMaxDist);
}
