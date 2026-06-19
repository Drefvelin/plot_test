// Terrain corridor road emission and culling of Voronoi roads that run parallel
// to corridor chains. Public API declared in RoadNetwork.h; shared helpers in
// RoadNetworkInternal.h (defined in RoadNetwork.cpp).

#include "roads/RoadNetwork.h"
#include "roads/RoadNetworkInternal.h"

#include "util/Logger.h"
#include "placement/geometry/PlotGeometry.h"
#include "terrain/TerrainAtlas.h"

#include <set>
#include <string>
#include <vector>

using namespace roadnet;

void appendCorridorRoads(Town& town, const TerrainAtlas& atlas, const Config& config) {
    if (!config.terrain.corridorRoadsEnabled || !atlas.valid) {
        return;
    }

    int emitted      = 0;
    int skippedShort = 0;

    const auto emitGraph = [&](const std::vector<std::vector<Vec2>>& graph) {
        for (const std::vector<Vec2>& polyline : graph) {
            if (polyline.size() < 2) {
                continue;
            }
            for (std::size_t i = 1; i < polyline.size(); ++i) {
                const Vec2  a      = polyline[i - 1];
                const Vec2  b      = polyline[i];
                const float segLen = (b - a).length();
                if (segLen < kMinSegmentLen) {
                    ++skippedShort;
                    continue;
                }

                Road road;
                road.id                = static_cast<int>(town.roads.size());
                road.a                 = a;
                road.b                 = b;
                road.isTerrainCorridor = true;
                town.roads.push_back(road);
                ++emitted;
            }
        }
    };

    emitGraph(atlas.shoreRoadGraph);
    emitGraph(atlas.riverRoadGraph);

    Logger::log("voronoi", "corridor_roads emitted=" + std::to_string(emitted) + " skipped_short="
                                + std::to_string(skippedShort));
}

void cullVoronoiRoadsParallelToCorridors(Town& town, const Config& config) {
    const ParallelCullSettings     settings = resolveParallelCullSettings(config);
    const std::vector<CorridorChain> chains = buildCorridorChains(town);

    std::set<int> culledRoadIds;
    for (const CorridorChain& chain : chains) {
        probeChainForParallelRoads(chain, town, settings, culledRoadIds);
    }

    if (culledRoadIds.empty()) {
        Logger::log("voronoi", "culled_parallel_voronoi=0 probe_offset="
                                    + std::to_string(settings.probeOffset));
        return;
    }

    std::vector<Road> kept;
    kept.reserve(town.roads.size());
    for (const Road& road : town.roads) {
        if (culledRoadIds.count(road.id) == 0) {
            kept.push_back(road);
        }
    }

    for (std::size_t i = 0; i < kept.size(); ++i) {
        kept[i].id = static_cast<int>(i);
    }
    town.roads = std::move(kept);
    indexJunctions(town);

    Logger::log("voronoi", "culled_parallel_voronoi=" + std::to_string(culledRoadIds.size())
                                + " corridor_chains=" + std::to_string(chains.size())
                                + " probe_offset=" + std::to_string(settings.probeOffset));
}
