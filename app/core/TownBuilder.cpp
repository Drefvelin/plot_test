#include "TownBuilder.h"

#include "Logger.h"
#include "Profile.h"
#include "PlacementFloors.h"
#include "TownConfig.h"
#include "RoadNetwork.h"
#include "TerrainAtlas.h"
#include "Units.h"

#define JC_VORONOI_IMPLEMENTATION
#include "third_party/jc_voronoi.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <utility>
#include <vector>

namespace {

Vec2 toVec2(const jcv_point& p) { return {p.x, p.y}; }

Vec2 toUnits(const jcv_point& p, float pixelsPerUnit) {
    return {units::toUnits(p.x, pixelsPerUnit), units::toUnits(p.y, pixelsPerUnit)};
}

sf::Color toColor(const std::array<uint8_t, 3>& rgb) {
    return sf::Color(rgb[0], rgb[1], rgb[2]);
}

bool clipToBox(Vec2& a, Vec2& b, float minX, float minY, float maxX, float maxY) {
    auto clip = [](float p, float q, float& t0, float& t1) -> bool {
        if (std::abs(p) < 1e-8f) {
            return q >= 0.f;
        }
        const float r = q / p;
        if (p < 0.f) {
            if (r > t1) return false;
            if (r > t0) t0 = r;
        } else {
            if (r < t0) return false;
            if (r < t1) t1 = r;
        }
        return true;
    };

    float t0 = 0.f;
    float t1 = 1.f;
    const Vec2 d{b.x - a.x, b.y - a.y};

    if (!clip(-d.x, a.x - minX, t0, t1)) return false;
    if (!clip(d.x, maxX - a.x, t0, t1)) return false;
    if (!clip(-d.y, a.y - minY, t0, t1)) return false;
    if (!clip(d.y, maxY - a.y, t0, t1)) return false;

    const Vec2 endA{a.x + d.x * t1, a.y + d.y * t1};
    const Vec2 startA{a.x + d.x * t0, a.y + d.y * t0};
    a = startA;
    b = endA;
    return true;
}

bool clipToDisc(Vec2& a, Vec2& b, const Vec2& center, float radius) {
    const float r2 = radius * radius;
    auto inside = [&](const Vec2& p) {
        const float dx = p.x - center.x;
        const float dy = p.y - center.y;
        return dx * dx + dy * dy <= r2 + 1e-4f;
    };

    const bool inA = inside(a);
    const bool inB = inside(b);
    if (inA && inB) {
        return true;
    }

    const Vec2 d{b.x - a.x, b.y - a.y};
    const Vec2 f{a.x - center.x, a.y - center.y};
    const float A = d.x * d.x + d.y * d.y;
    const float B = 2.f * (f.x * d.x + f.y * d.y);
    const float C = f.x * f.x + f.y * f.y - r2;

    float disc = B * B - 4.f * A * C;
    if (disc < 0.f) {
        return false;
    }

    disc = std::sqrt(disc);
    const float inv2A = 1.f / (2.f * A);
    const float t1 = (-B - disc) * inv2A;
    const float t2 = (-B + disc) * inv2A;

    auto pointAt = [&](float t) { return Vec2{a.x + d.x * t, a.y + d.y * t}; };

    if (inA && !inB) {
        const float tExit = (t1 >= 0.f && t1 <= 1.f) ? t1 : t2;
        if (tExit < 0.f || tExit > 1.f) return false;
        b = pointAt(tExit);
        return true;
    }
    if (!inA && inB) {
        const float tEnter = (t1 >= 0.f && t1 <= 1.f) ? t1 : t2;
        if (tEnter < 0.f || tEnter > 1.f) return false;
        a = pointAt(tEnter);
        return true;
    }

    float tEnter = std::min(t1, t2);
    float tExit  = std::max(t1, t2);
    tEnter = std::max(0.f, tEnter);
    tExit  = std::min(1.f, tExit);
    if (tEnter > tExit) {
        return false;
    }
    a = pointAt(tEnter);
    b = pointAt(tExit);
    return true;
}

std::vector<jcv_point> generateSites(const Config& config, const Vec2& centerPx, float radiusPx) {
    std::vector<jcv_point> sites;
    sites.reserve(static_cast<std::size_t>(config.voronoi.siteCount()));

    std::mt19937 rng(static_cast<unsigned>(config.voronoi.seed));
    std::uniform_real_distribution<float> angleDist(0.f, 2.f * 3.14159265358979323846f);
    std::uniform_real_distribution<float> radiusDist(0.f, 1.f);

    const int siteCount = config.voronoi.siteCount();
    for (int i = 0; i < siteCount; ++i) {
        const float t = angleDist(rng);
        const float r = radiusPx * std::sqrt(radiusDist(rng));
        sites.push_back({centerPx.x + r * std::cos(t), centerPx.y + r * std::sin(t)});
    }

    return sites;
}

void appendThickSegment(sf::VertexArray& tris, const Vec2& a, const Vec2& b, float thickness,
                        const sf::Color& color) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-4f) {
        return;
    }
    const float len = std::sqrt(lenSq);
    const float nx = -dy / len * thickness * 0.5f;
    const float ny = dx / len * thickness * 0.5f;

    tris.append({{a.x + nx, a.y + ny}, color});
    tris.append({{a.x - nx, a.y - ny}, color});
    tris.append({{b.x + nx, b.y + ny}, color});

    tris.append({{b.x + nx, b.y + ny}, color});
    tris.append({{a.x - nx, a.y - ny}, color});
    tris.append({{b.x - nx, b.y - ny}, color});
}

void buildRoadMesh(Town& town, const Config& config, const TerrainAtlas* terrain) {
    rebuildRoadMesh(town, config.colors.edges, config.colors.secondaryEdges, config.colors.bridge,
                    config.world.pixelsPerUnit, terrain, config.terrain.clipRoadsAtWater);
}

using EndpointKey = std::pair<int, int>;

EndpointKey quantizePoint(const Vec2& p) {
    return {static_cast<int>(std::round(p.x * 1000.f)),
            static_cast<int>(std::round(p.y * 1000.f))};
}

std::pair<EndpointKey, EndpointKey> normalizedEndpointKey(const Vec2& a, const Vec2& b) {
    EndpointKey lo = quantizePoint(a);
    EndpointKey hi = quantizePoint(b);
    if (hi < lo) {
        std::swap(lo, hi);
    }
    return {lo, hi};
}

int findOrCreateBoundaryRoad(Town& town,
                             std::map<std::pair<EndpointKey, EndpointKey>, int>& roadByEndpoints,
                             const Vec2& aUnits, const Vec2& bUnits) {
    const auto key = normalizedEndpointKey(aUnits, bUnits);
    const auto existing = roadByEndpoints.find(key);
    if (existing != roadByEndpoints.end()) {
        return existing->second;
    }

    Road road;
    road.id    = static_cast<int>(town.roads.size());
    road.a     = aUnits;
    road.b     = bUnits;
    town.roads.push_back(road);
    roadByEndpoints[key] = road.id;
    return road.id;
}

int syncBoundaryRoadsFromDiagram(Town& town, const jcv_diagram& diagram, float renderW,
                                 float renderH, const Vec2& centerPx, float radiusPx,
                                 float pixelsPerUnit) {
    std::map<std::pair<EndpointKey, EndpointKey>, int> roadByEndpoints;
    for (const Road& road : town.roads) {
        roadByEndpoints[normalizedEndpointKey(road.a, road.b)] = road.id;
    }

    int addedRoads = 0;
    const jcv_site* sites = jcv_diagram_get_sites(&diagram);
    for (int i = 0; i < diagram.numsites; ++i) {
        const jcv_site* site = &sites[i];
        if (site->index < 0) {
            continue;
        }

        for (const jcv_graphedge* graphEdge = site->edges; graphEdge; graphEdge = graphEdge->next) {
            Vec2 aPx = toVec2(graphEdge->pos[0]);
            Vec2 bPx = toVec2(graphEdge->pos[1]);
            if ((bPx - aPx).length() < 1e-4f) {
                continue;
            }
            if (!clipToBox(aPx, bPx, 0.f, 0.f, renderW, renderH)) {
                continue;
            }
            if (!clipToDisc(aPx, bPx, centerPx, radiusPx)) {
                continue;
            }

            const Vec2 aUnits{units::toUnits(aPx.x, pixelsPerUnit),
                              units::toUnits(aPx.y, pixelsPerUnit)};
            const Vec2 bUnits{units::toUnits(bPx.x, pixelsPerUnit),
                              units::toUnits(bPx.y, pixelsPerUnit)};
            const int before = static_cast<int>(town.roads.size());
            findOrCreateBoundaryRoad(town, roadByEndpoints, aUnits, bUnits);
            if (static_cast<int>(town.roads.size()) > before) {
                ++addedRoads;
            }
        }
    }

    return addedRoads;
}

}  // namespace

Town TownBuilder::build(const Config& config, const TerrainAtlas* terrain,
                        const PlacementFloors& floors, const TownConfig& townCfg,
                        const TerrainCatalog* catalog, const TerrainProbeConfig* probes) {
    PROFILE_SCOPE(ProfileScopeId::TownBuild);

    Town town;
    town.lastTerrainAnchorRoadId.clear();
    town.placementQueueCursor = 0;
    town.placementBumpIndex   = -1;
    town.placementBumpCount   = 0;
    town.placementFailureCount = 0;
    town.placementFailedIndices.clear();
    town.moveFailureCount     = 0;
    town.relocatingInstanceId = 0xFFFFFFFFu;
    town.relocatingHostRoadId = -1;
    town.suburbanMaxHop = std::max(0, townCfg.initialSuburbanMaxHops);
    town.urbanCoreMaxHop = -1;
    town.ringPhase = RingPhase::Normal;
    town.alleyCompleteRoadIds.clear();
    const float ppu = config.world.pixelsPerUnit;
    const float renderW = config.renderWidth();
    const float renderH = config.renderHeight();
    const Vec2 centerPx{renderW * 0.5f, renderH * 0.5f};
    const float radiusPx = config.renderRadius();

    town.center = {units::toUnits(centerPx.x, ppu), units::toUnits(centerPx.y, ppu)};
    town.radius = config.diagram.radius;
    town.width = config.diagram.width;
    town.height = config.diagram.height;

    const auto sitesPx = generateSites(config, centerPx, radiusPx);

    jcv_rect rect{};
    rect.min.x = 0.f;
    rect.min.y = 0.f;
    rect.max.x = static_cast<jcv_real>(renderW);
    rect.max.y = static_cast<jcv_real>(renderH);

    jcv_diagram diagram{};
    jcv_diagram_generate(static_cast<int>(sitesPx.size()), sitesPx.data(), &rect, nullptr, &diagram);

    std::map<std::pair<int, int>, int> roadIndex;

    const jcv_edge* edge = jcv_diagram_get_edges(&diagram);
    while (edge) {
        if (!edge->sites[0] || !edge->sites[1]) {
            edge = jcv_diagram_get_next_edge(edge);
            continue;
        }

        Vec2 a = toVec2(edge->pos[0]);
        Vec2 b = toVec2(edge->pos[1]);

        if (clipToBox(a, b, 0.f, 0.f, renderW, renderH) && clipToDisc(a, b, centerPx, radiusPx)) {
            const int siteA = edge->sites[0]->index;
            const int siteB = edge->sites[1]->index;
            if (siteA < 0 || siteB < 0 || siteA >= diagram.numsites || siteB >= diagram.numsites) {
                edge = jcv_diagram_get_next_edge(edge);
                continue;
            }
            const int lo = std::min(siteA, siteB);
            const int hi = std::max(siteA, siteB);
            const auto key = std::make_pair(lo, hi);

            int roadId = -1;
            const auto existing = roadIndex.find(key);
            if (existing != roadIndex.end()) {
                roadId = existing->second;
            } else {
                Road road;
                roadId = static_cast<int>(town.roads.size());
                road.id = roadId;
                road.a = {units::toUnits(a.x, ppu), units::toUnits(a.y, ppu)};
                road.b = {units::toUnits(b.x, ppu), units::toUnits(b.y, ppu)};
                town.roads.push_back(road);
                roadIndex[key] = roadId;
            }
        }

        edge = jcv_diagram_get_next_edge(edge);
    }

    const int boundaryRoadsAdded =
        syncBoundaryRoadsFromDiagram(town, diagram, renderW, renderH, centerPx, radiusPx, ppu);

    jcv_diagram_free(&diagram);

    indexJunctions(town);

    const int roadsBeforeCorridors = static_cast<int>(town.roads.size());
    if (terrain != nullptr && config.terrain.corridorRoadsEnabled) {
        appendCorridorRoads(town, *terrain, config);
    }
    const int roadsAfterCorridors = static_cast<int>(town.roads.size());
    splitRoadsAtIntersections(town);
    const int roadsAfterSplit = static_cast<int>(town.roads.size());

    if (terrain != nullptr && config.terrain.bridgesEnabled) {
        splitRoadsAtForbiddenBoundary(town, *terrain);
        indexJunctions(town);
        resolveBridges(town, *terrain, config);
    }

    indexJunctions(town);
    cullVoronoiRoadsParallelToCorridors(town, config);

    town.primaryRoadCount = 0;
    for (const Road& road : town.roads) {
        if (!road.isSecondary) {
            ++town.primaryRoadCount;
        }
    }

    buildRoadMesh(town, config, terrain);
    assignRoadSideInwards(town, terrain);
    buildJunctionMesh(town, ppu, 1.f);
    buildRoadEndProbeMesh(town, ppu, config.plots.frontageSetback);

    Logger::log("voronoi", "scale=" + std::to_string(config.voronoi.scale) + " seed="
                                + std::to_string(config.voronoi.seed) + " roads="
                                + std::to_string(town.roads.size()) + " boundary_roads_added="
                                + std::to_string(boundaryRoadsAdded) + " junctions="
                                + std::to_string(town.junctions.size()) + " roads_before_corridors="
                                + std::to_string(roadsBeforeCorridors) + " roads_after_corridors="
                                + std::to_string(roadsAfterCorridors) + " roads_after_split="
                                + std::to_string(roadsAfterSplit) + " primary_roads="
                                + std::to_string(town.primaryRoadCount) + " boundary_source=roads");

    ensureTownFrontageInitialized(town, config.plots.frontageSetback, floors, townCfg, terrain,
                                  &config.plots, catalog, probes);

    return town;
}
