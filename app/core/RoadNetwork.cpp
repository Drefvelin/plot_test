#include "RoadNetwork.h"

#include "Logger.h"
#include "PlotGeometry.h"
#include "TerrainAtlas.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace {

constexpr float kInteriorCrossEps = 0.03f;
constexpr float kMinSegmentLen    = 0.5f;
constexpr float kJunctionEps      = 0.08f;
constexpr int   kBridgeSameSideMaxHops = 8;

struct ParallelCullSettings {
    float probeOffset   = 4.f;
    float parallelCos   = 0.98f;
    float sampleSpacing = 5.f;
};

struct CorridorLink {
    int toJ    = -1;
    int roadId = -1;
};

struct CorridorChain {
    std::vector<int> junctions;
    std::vector<int> roadIds;
};

Vec2 lerpVec(const Vec2& a, const Vec2& b, float t) {
    return a + (b - a) * t;
}

void resetRoadFrontage(Road& road) {
    road.sideA = RoadSideFrontage{};
    road.sideB = RoadSideFrontage{};
}

bool nearParam(float a, float b, float eps = 1e-4f) {
    return std::abs(a - b) <= eps;
}

bool nearPoint(const Vec2& a, const Vec2& b, float eps = kJunctionEps) {
    return (a - b).length() <= eps;
}

void uniqueSortedParams(std::vector<float>& params) {
    std::sort(params.begin(), params.end());
    std::vector<float> unique;
    unique.reserve(params.size());
    for (float t : params) {
        if (unique.empty() || !nearParam(unique.back(), t, 1e-3f)) {
            unique.push_back(t);
        }
    }
    params = std::move(unique);
}

ParallelCullSettings resolveParallelCullSettings(const Config& config) {
    ParallelCullSettings settings;
    settings.probeOffset =
        config.terrain.corridorParallelProbeOffset > 0.f
            ? config.terrain.corridorParallelProbeOffset
            : 4.f;
    settings.parallelCos =
        config.terrain.corridorParallelCos > 0.f ? config.terrain.corridorParallelCos : 0.98f;
    settings.sampleSpacing = std::min(5.f, settings.probeOffset);
    return settings;
}

int findJunctionIndex(const Vec2& p, const std::vector<Junction>& junctions) {
    for (std::size_t i = 0; i < junctions.size(); ++i) {
        if (nearPoint(junctions[i].pos, p)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<std::vector<CorridorLink>> buildCorridorAdjacency(const Town& town) {
    std::vector<std::vector<CorridorLink>> adjacency(town.junctions.size());

    for (const Road& road : town.roads) {
        if (!road.isTerrainCorridor || road.id < 0) {
            continue;
        }
        const int ja = findJunctionIndex(road.a, town.junctions);
        const int jb = findJunctionIndex(road.b, town.junctions);
        if (ja < 0 || jb < 0 || ja == jb) {
            continue;
        }
        adjacency[static_cast<std::size_t>(ja)].push_back({jb, road.id});
        adjacency[static_cast<std::size_t>(jb)].push_back({ja, road.id});
    }
    return adjacency;
}

bool roadIncidentToJunction(const Road& road, int junctionId, const Town& town) {
    if (junctionId < 0 || junctionId >= static_cast<int>(town.junctions.size())) {
        return false;
    }
    const Vec2& jpos = town.junctions[static_cast<std::size_t>(junctionId)].pos;
    return nearPoint(road.a, jpos) || nearPoint(road.b, jpos);
}

std::vector<CorridorChain> buildCorridorChains(const Town& town) {
    const std::vector<std::vector<CorridorLink>> adjacency = buildCorridorAdjacency(town);
    std::set<std::pair<int, int>>                visitedDirected;
    std::set<std::vector<int>>                   seenJunctionKeys;
    std::vector<CorridorChain>                   chains;

    const auto extendFrom = [&](int fromJ, int toJ, int roadId) {
        if (visitedDirected.count({fromJ, toJ}) != 0) {
            return;
        }

        CorridorChain chain;
        chain.junctions.push_back(fromJ);
        chain.junctions.push_back(toJ);
        chain.roadIds.push_back(roadId);
        visitedDirected.insert({fromJ, toJ});
        visitedDirected.insert({toJ, fromJ});

        int prevJ = fromJ;
        int curJ  = toJ;
        while (true) {
            const std::vector<CorridorLink>& links =
                adjacency[static_cast<std::size_t>(curJ)];
            int nextJ    = -1;
            int nextRoad = -1;
            int optionCount = 0;
            for (const CorridorLink& link : links) {
                if (link.toJ == prevJ) {
                    continue;
                }
                ++optionCount;
                nextJ    = link.toJ;
                nextRoad = link.roadId;
            }
            if (optionCount != 1 || nextJ < 0) {
                break;
            }
            if (visitedDirected.count({curJ, nextJ}) != 0) {
                break;
            }
            chain.junctions.push_back(nextJ);
            chain.roadIds.push_back(nextRoad);
            visitedDirected.insert({curJ, nextJ});
            visitedDirected.insert({nextJ, curJ});
            prevJ = curJ;
            curJ  = nextJ;
        }

        prevJ = fromJ;
        curJ  = toJ;
        while (true) {
            const std::vector<CorridorLink>& links =
                adjacency[static_cast<std::size_t>(curJ)];
            int nextJ    = -1;
            int nextRoad = -1;
            int optionCount = 0;
            for (const CorridorLink& link : links) {
                if (link.toJ == prevJ) {
                    continue;
                }
                ++optionCount;
                nextJ    = link.toJ;
                nextRoad = link.roadId;
            }
            if (optionCount != 1 || nextJ < 0) {
                break;
            }
            if (visitedDirected.count({curJ, nextJ}) != 0) {
                break;
            }
            chain.junctions.insert(chain.junctions.begin(), nextJ);
            chain.roadIds.insert(chain.roadIds.begin(), nextRoad);
            visitedDirected.insert({curJ, nextJ});
            visitedDirected.insert({nextJ, curJ});
            prevJ = curJ;
            curJ  = nextJ;
        }

        if (chain.junctions.size() < 2) {
            return;
        }

        std::vector<int> key = chain.junctions;
        if (key.front() > key.back()) {
            std::reverse(key.begin(), key.end());
        }
        if (seenJunctionKeys.count(key) != 0) {
            return;
        }
        seenJunctionKeys.insert(key);
        chains.push_back(std::move(chain));
    };

    for (const Road& road : town.roads) {
        if (!road.isTerrainCorridor || road.id < 0) {
            continue;
        }
        const int ja = findJunctionIndex(road.a, town.junctions);
        const int jb = findJunctionIndex(road.b, town.junctions);
        if (ja < 0 || jb < 0) {
            continue;
        }
        extendFrom(ja, jb, road.id);
    }

    return chains;
}

struct ChainSample {
    Vec2 pos;
    Vec2 tangent;
};

std::vector<ChainSample> sampleCorridorChain(const CorridorChain& chain, const Town& town,
                                             float spacing) {
    std::vector<ChainSample> samples;
    if (chain.junctions.size() < 2 || spacing <= 1e-4f) {
        return samples;
    }

    float accumulated = 0.f;
    for (std::size_t i = 1; i < chain.junctions.size(); ++i) {
        const Vec2& a =
            town.junctions[static_cast<std::size_t>(chain.junctions[i - 1])].pos;
        const Vec2& b = town.junctions[static_cast<std::size_t>(chain.junctions[i])].pos;
        const Vec2  delta = b - a;
        const float len   = delta.length();
        if (len < 1e-4f) {
            continue;
        }
        const Vec2 tangent = delta * (1.f / len);

        while (accumulated < len) {
            samples.push_back({a + tangent * accumulated, tangent});
            accumulated += spacing;
        }
        accumulated -= len;
    }

    if (samples.empty() && chain.junctions.size() >= 2) {
        const Vec2& a =
            town.junctions[static_cast<std::size_t>(chain.junctions.front())].pos;
        const Vec2& b =
            town.junctions[static_cast<std::size_t>(chain.junctions.back())].pos;
        const Vec2 delta = b - a;
        if (delta.length() > 1e-4f) {
            samples.push_back({a + delta * 0.5f, delta.normalized()});
        }
    }
    return samples;
}

float pointDistanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2  ab    = b - a;
    const float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 1e-8f) {
        return (p - a).length();
    }
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.f, 1.f);
    const Vec2  proj{a.x + ab.x * t, a.y + ab.y * t};
    return (p - proj).length();
}

bool segmentsParallel(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                      float minParallelCos) {
    const Vec2 dirA = (a1 - a0).normalized();
    const Vec2 dirB = (b1 - b0).normalized();
    if (dirA.length() < 1e-4f || dirB.length() < 1e-4f) {
        return false;
    }
    return std::abs(dirA.dot(dirB)) >= minParallelCos;
}

void probeChainForParallelRoads(const CorridorChain& chain, const Town& town,
                                const ParallelCullSettings& settings,
                                std::set<int>& culledRoadIds) {
    if (chain.junctions.size() < 2) {
        return;
    }

    const int startJ = chain.junctions.front();
    const int endJ   = chain.junctions.back();
    const std::vector<ChainSample> samples =
        sampleCorridorChain(chain, town, settings.sampleSpacing);
    const float halfWin = std::max(4.f, settings.sampleSpacing * 0.5f);

    for (const ChainSample& sample : samples) {
        const Vec2 perp = perpendicular(sample.tangent);
        for (float sign : {1.f, -1.f}) {
            const Vec2 probeCenter = sample.pos + perp * (settings.probeOffset * sign);
            const Vec2 probeA      = probeCenter - sample.tangent * halfWin;
            const Vec2 probeB      = probeCenter + sample.tangent * halfWin;

            for (const Road& road : town.roads) {
                if (road.id < 0 || road.isTerrainCorridor || road.isSecondary) {
                    continue;
                }
                if (roadIncidentToJunction(road, startJ, town)
                    || roadIncidentToJunction(road, endJ, town)) {
                    continue;
                }

                const bool hitProbe =
                    segmentsIntersect2D(probeA, probeB, road.a, road.b)
                    || pointDistanceToSegment(probeCenter, road.a, road.b)
                           <= settings.probeOffset * 0.5f;
                if (!hitProbe) {
                    continue;
                }
                if (!segmentsParallel(probeA, probeB, road.a, road.b, settings.parallelCos)) {
                    continue;
                }
                if (segmentNearParallelRoad(sample.pos - sample.tangent * halfWin,
                                            sample.pos + sample.tangent * halfWin, road.a, road.b,
                                            settings.parallelCos, settings.probeOffset * 2.5f,
                                            0.2f)) {
                    culledRoadIds.insert(road.id);
                }
            }
        }
    }
}

struct BridgeSettings {
    bool  snapEnabled    = true;
    float searchRadius   = 8.f;
    float maxSpan        = 80.f;
    float outlineMaxDist = 6.f;
};

BridgeSettings resolveBridgeSettings(const Config& config) {
    BridgeSettings settings;
    settings.snapEnabled = config.terrain.bridgeSnapEnabled;
    settings.searchRadius =
        config.terrain.bridgeSnapSearchRadius > 0.f ? config.terrain.bridgeSnapSearchRadius : 8.f;
    settings.maxSpan =
        config.terrain.bridgeMaxSpan > 0.f ? config.terrain.bridgeMaxSpan : 80.f;
    settings.outlineMaxDist =
        config.terrain.bridgeOutlineMaxDist > 0.f ? config.terrain.bridgeOutlineMaxDist : 6.f;
    return settings;
}

void collectBoundarySplitParams(const Vec2& a, const Vec2& b, const TerrainAtlas& terrain,
                                std::vector<float>& params);

void updateJunctionPosition(Town& town, int junctionId, const Vec2& newPos);

bool roadEligibleForWaterSanitize(const Road& road) {
    if (road.isBridge || road.isSecondary) {
        return false;
    }
    if (road.length() < kMinSegmentLen) {
        return false;
    }
    return true;
}

int boundarySplitRoadsAtWater(Town& town, const TerrainAtlas& terrain) {
    const int         roadCount = static_cast<int>(town.roads.size());
    std::vector<Road> splitRoads;
    splitRoads.reserve(static_cast<std::size_t>(roadCount) * 2);
    int boundarySplits = 0;

    for (int i = 0; i < roadCount; ++i) {
        const Road& source = town.roads[static_cast<std::size_t>(i)];
        if (!roadEligibleForWaterSanitize(source)) {
            splitRoads.push_back(source);
            continue;
        }

        const float len = source.length();
        if (len < 1e-4f) {
            splitRoads.push_back(source);
            continue;
        }

        std::vector<float> params;
        collectBoundarySplitParams(source.a, source.b, terrain, params);
        boundarySplits += static_cast<int>(params.size()) - 2;

        for (std::size_t p = 1; p < params.size(); ++p) {
            const float t0 = params[p - 1];
            const float t1 = params[p];
            if ((t1 - t0) * len < kMinSegmentLen) {
                continue;
            }

            Road segment = source;
            segment.a    = lerpVec(source.a, source.b, t0);
            segment.b    = lerpVec(source.a, source.b, t1);
            resetRoadFrontage(segment);
            splitRoads.push_back(segment);
        }
    }

    for (std::size_t i = 0; i < splitRoads.size(); ++i) {
        splitRoads[i].id = static_cast<int>(i);
    }
    town.roads = std::move(splitRoads);
    return boundarySplits;
}

bool findFirstBuildableAlongRoad(const Vec2& endpoint, const Vec2& otherEnd,
                                 const TerrainAtlas& terrain, Vec2& outLand) {
    if (terrain.isBuildable(endpoint)) {
        outLand = endpoint;
        return true;
    }

    const Vec2  delta = otherEnd - endpoint;
    const float len   = delta.length();
    if (len < 1e-4f) {
        return false;
    }

    const Vec2 dir = delta * (1.f / len);
    constexpr float kStep = 0.25f;
    for (float dist = kStep; dist <= len + 1e-3f; dist += kStep) {
        const Vec2 point = endpoint + dir * dist;
        if (terrain.isBuildable(point)) {
            outLand = point;
            return true;
        }
    }
    return false;
}

int snapNonBuildableJunctions(Town& town, const TerrainAtlas& terrain) {
    int snapped = 0;

    for (const Junction& junction : town.junctions) {
        if (terrain.isBuildable(junction.pos)) {
            continue;
        }

        Vec2  bestLand{};
        float bestDistSq = 1e30f;
        bool  found      = false;

        for (int roadId : junction.roadIds) {
            if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
                continue;
            }
            const Road& road = town.roads[static_cast<std::size_t>(roadId)];
            if (road.isBridge) {
                continue;
            }

            Vec2 landPoint{};
            if (nearPoint(road.a, junction.pos)) {
                if (!findFirstBuildableAlongRoad(road.a, road.b, terrain, landPoint)) {
                    continue;
                }
            } else if (nearPoint(road.b, junction.pos)) {
                if (!findFirstBuildableAlongRoad(road.b, road.a, terrain, landPoint)) {
                    continue;
                }
            } else {
                continue;
            }

            const float distSq = (landPoint - junction.pos).length() * (landPoint - junction.pos).length();
            if (!found || distSq < bestDistSq) {
                found      = true;
                bestDistSq = distSq;
                bestLand   = landPoint;
            }
        }

        if (found) {
            updateJunctionPosition(town, junction.id, bestLand);
            ++snapped;
        }
    }

    return snapped;
}

void collectBoundarySplitParams(const Vec2& a, const Vec2& b, const TerrainAtlas& terrain,
                                std::vector<float>& params) {
    params = {0.f, 1.f};
    const Vec2  delta = b - a;
    const float len   = delta.length();
    if (len < 1e-4f) {
        return;
    }

    constexpr float kStep = 0.25f;
    bool            prevLand = terrain.isBuildable(a);
    float           prevT    = 0.f;
    for (float dist = kStep; dist <= len + 1e-3f; dist += kStep) {
        const float t     = std::min(dist / len, 1.f);
        const bool  land  = terrain.isBuildable(a + delta * t);
        if (land != prevLand) {
            const float crossT = (prevT + t) * 0.5f;
            if (crossT > kInteriorCrossEps && crossT < 1.f - kInteriorCrossEps) {
                params.push_back(crossT);
            }
        }
        prevLand = land;
        prevT    = t;
    }

    for (const std::vector<Vec2>& polyline : terrain.forbiddenPolygons) {
        if (polyline.size() < 2) {
            continue;
        }
        for (std::size_t i = 1; i < polyline.size(); ++i) {
            float tSeg = 0.f;
            float u    = 0.f;
            if (!segmentCrossingParams(a, b, polyline[i - 1], polyline[i], tSeg, u)) {
                continue;
            }
            if (tSeg > kInteriorCrossEps && tSeg < 1.f - kInteriorCrossEps) {
                params.push_back(tSeg);
            }
        }
    }

    uniqueSortedParams(params);
}

bool segmentEntirelyForbidden(const Vec2& a, const Vec2& b, const TerrainAtlas& terrain,
                              float step = 0.25f) {
    const Vec2  delta = b - a;
    const float len   = delta.length();
    if (len < 1e-4f) {
        return false;
    }

    int nonBuildable = 0;
    int samples      = 0;
    for (float dist = step * 0.5f; dist < len - step * 0.5f; dist += step) {
        const float t = dist / len;
        ++samples;
        if (!terrain.isBuildable(a + delta * t)) {
            ++nonBuildable;
        }
    }
    if (samples == 0) {
        return false;
    }
    return nonBuildable == samples;
}

bool segmentInteriorMostlyWater(const Vec2& a, const Vec2& b, const TerrainAtlas& terrain,
                                float step = 0.25f) {
    const Vec2  delta = b - a;
    const float len   = delta.length();
    if (len < 1e-4f) {
        return false;
    }

    int nonBuildable = 0;
    int samples      = 0;
    for (float dist = step * 0.5f; dist < len - step * 0.5f; dist += step) {
        const float t = dist / len;
        ++samples;
        if (!terrain.isBuildable(a + delta * t)) {
            ++nonBuildable;
        }
    }
    if (samples == 0) {
        return false;
    }
    return nonBuildable >= samples - 1;
}

struct BoundaryFrame {
    Vec2 point{};
    Vec2 tangent{};
};

BoundaryFrame nearestBoundaryFrame(const Vec2& p, const TerrainAtlas& terrain) {
    BoundaryFrame best;
    float         bestDistSq = 1e30f;

    for (const std::vector<Vec2>& polyline : terrain.forbiddenPolygons) {
        if (polyline.size() < 2) {
            continue;
        }
        for (std::size_t i = 1; i < polyline.size(); ++i) {
            const Vec2& a = polyline[i - 1];
            const Vec2& b = polyline[i];
            const Vec2  ab    = b - a;
            const float lenSq = ab.x * ab.x + ab.y * ab.y;
            if (lenSq < 1e-8f) {
                continue;
            }
            const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.f, 1.f);
            const Vec2  proj{a.x + ab.x * t, a.y + ab.y * t};
            const float distSq = (p - proj).length() * (p - proj).length();
            if (distSq < bestDistSq) {
                bestDistSq     = distSq;
                best.point     = proj;
                best.tangent   = ab.normalized();
            }
        }
    }

    if (best.tangent.length() < 1e-4f) {
        best.point   = p;
        best.tangent = {1.f, 0.f};
    }
    return best;
}

constexpr float kOppositeBankEps = 0.1f;

bool isShorePoint(const Vec2& p, const Vec2& towardLand, const TerrainAtlas& terrain) {
    if (terrain.isBuildable(p)) {
        return true;
    }
    const Vec2 dir = towardLand.normalized();
    if (dir.length() < 1e-4f) {
        return false;
    }
    constexpr float kProbe = 0.4f;
    return terrain.isBuildable(p + dir * kProbe);
}

Vec2 landwardInward(const BoundaryFrame& frame, const TerrainAtlas& terrain) {
    Vec2 inward = perpendicular(frame.tangent);
    if (!terrain.isBuildable(frame.point + inward * 0.4f)) {
        inward = inward * -1.f;
    }
    return inward;
}

bool isLandRoad(const Road& road, const TerrainAtlas& terrain) {
    if (road.isSecondary || road.isBridge) {
        return false;
    }
    return terrain.isBuildable((road.a + road.b) * 0.5f);
}

struct WaterBodyRef {
    TerrainId   kind       = kTerrainUnknown;
    std::size_t graphIndex = 0;
    bool        valid      = false;

    bool sameWaterBodyAs(const WaterBodyRef& other) const {
        return valid && other.valid && kind == other.kind;
    }
};

struct OutlineHit {
    std::size_t graphIndex = 0;
    Vec2        point{};
    Vec2        tangent{};
    float       dist = 1e30f;
    bool        valid = false;
};

OutlineHit nearestOutlineFrame(const Vec2& p, const TerrainAtlas& terrain, TerrainId kind) {
    OutlineHit best;
    const std::vector<std::vector<Vec2>>* graphs = terrain.outlineGraphs(kind);
    if (graphs == nullptr) {
        return best;
    }

    for (std::size_t g = 0; g < graphs->size(); ++g) {
        const std::vector<Vec2>& graph = (*graphs)[g];
        if (graph.size() < 2) {
            continue;
        }
        for (std::size_t i = 1; i < graph.size(); ++i) {
            const Vec2& a = graph[i - 1];
            const Vec2& b = graph[i];
            const float dist = distancePointToSegment(p, a, b);
            if (dist >= best.dist) {
                continue;
            }

            const Vec2  ab    = b - a;
            const float lenSq = ab.x * ab.x + ab.y * ab.y;
            if (lenSq < 1e-8f) {
                continue;
            }
            const float t =
                std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.f, 1.f);
            best.graphIndex = g;
            best.point      = {a.x + ab.x * t, a.y + ab.y * t};
            best.tangent    = ab.normalized();
            best.dist       = dist;
            best.valid      = true;
        }
    }
    return best;
}

WaterBodyRef classifyJunctionWaterBody(const Vec2& pos, const TerrainAtlas& terrain,
                                       float outlineMaxDist) {
    WaterBodyRef ref;
    if (terrain.catalog == nullptr) {
        return ref;
    }

    const TerrainId seaId   = terrain.catalog->resolveKey("sea");
    const TerrainId riverId = terrain.catalog->resolveKey("river");

    const OutlineHit seaHit   = nearestOutlineFrame(pos, terrain, seaId);
    const OutlineHit riverHit = nearestOutlineFrame(pos, terrain, riverId);

    const OutlineHit* chosen = nullptr;
    if (riverHit.valid && riverHit.dist <= outlineMaxDist) {
        chosen = &riverHit;
    } else if (seaHit.valid && seaHit.dist <= outlineMaxDist) {
        chosen = &seaHit;
    }
    if (chosen == nullptr) {
        return ref;
    }

    ref.kind       = (chosen == &seaHit) ? seaId : riverId;
    ref.graphIndex = chosen->graphIndex;
    ref.valid      = true;
    return ref;
}

bool junctionHasLandRoad(const Town& town, int junctionId, const TerrainAtlas& terrain) {
    if (junctionId < 0 || junctionId >= static_cast<int>(town.junctions.size())) {
        return false;
    }
    for (int roadId : town.junctions[static_cast<std::size_t>(junctionId)].roadIds) {
        if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
            continue;
        }
        if (isLandRoad(town.roads[static_cast<std::size_t>(roadId)], terrain)) {
            return true;
        }
    }
    return false;
}

struct ShoreJunction {
    int          junctionId = -1;
    WaterBodyRef waterBody{};
};

Vec2 shoreLandwardFromOutline(const Vec2& junctionPos, const OutlineHit& hit,
                              const TerrainAtlas& terrain) {
    const Vec2 toJunction = junctionPos - hit.point;
    if (toJunction.length() > 0.1f) {
        return toJunction.normalized();
    }

    Vec2 inward = perpendicular(hit.tangent);
    if (!terrain.isBuildable(hit.point + inward * 0.4f)) {
        inward = inward * -1.f;
    }
    return inward;
}

bool areOppositeBanksForWater(const Vec2& posA, const Vec2& posB, TerrainId waterKind,
                              const TerrainAtlas& terrain) {
    const OutlineHit hitA = nearestOutlineFrame(posA, terrain, waterKind);
    const OutlineHit hitB = nearestOutlineFrame(posB, terrain, waterKind);
    if (!hitA.valid || !hitB.valid) {
        return false;
    }

    const Vec2 inwardA = shoreLandwardFromOutline(posA, hitA, terrain);
    const Vec2 inwardB = shoreLandwardFromOutline(posB, hitB, terrain);

    Vec2 ab = posB - posA;
    const float len = ab.length();
    if (len < 1e-4f) {
        return false;
    }
    ab = ab * (1.f / len);

    return ab.dot(inwardA) < -kOppositeBankEps && (ab * -1.f).dot(inwardB) < -kOppositeBankEps;
}

bool areOppositeBanks(const Vec2& posA, const Vec2& posB, const TerrainAtlas& terrain) {
    const BoundaryFrame frameA  = nearestBoundaryFrame(posA, terrain);
    const BoundaryFrame frameB  = nearestBoundaryFrame(posB, terrain);
    const Vec2          inwardA = landwardInward(frameA, terrain);
    const Vec2          inwardB = landwardInward(frameB, terrain);

    Vec2 ab = posB - posA;
    const float len = ab.length();
    if (len < 1e-4f) {
        return false;
    }
    ab = ab * (1.f / len);

    return ab.dot(inwardA) < -kOppositeBankEps && (ab * -1.f).dot(inwardB) < -kOppositeBankEps;
}

void updateJunctionPosition(Town& town, int junctionId, const Vec2& newPos) {
    if (junctionId < 0 || junctionId >= static_cast<int>(town.junctions.size())) {
        return;
    }

    const Vec2 oldPos = town.junctions[static_cast<std::size_t>(junctionId)].pos;
    town.junctions[static_cast<std::size_t>(junctionId)].pos = newPos;

    for (int roadId : town.junctions[static_cast<std::size_t>(junctionId)].roadIds) {
        if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
            continue;
        }
        Road& road = town.roads[static_cast<std::size_t>(roadId)];
        if (nearPoint(road.a, oldPos)) {
            road.a = newPos;
        }
        if (nearPoint(road.b, oldPos)) {
            road.b = newPos;
        }
    }
}

struct BridgeCandidate {
    int   ja     = -1;
    int   jb     = -1;
    float length = 0.f;
};

int otherJunctionOnRoad(const Town& town, const Road& road, int junctionId) {
    if (junctionId < 0 || junctionId >= static_cast<int>(town.junctions.size())) {
        return -1;
    }
    const Vec2& jpos = town.junctions[static_cast<std::size_t>(junctionId)].pos;
    if (nearPoint(road.a, jpos)) {
        return findJunctionIndex(road.b, town.junctions);
    }
    if (nearPoint(road.b, jpos)) {
        return findJunctionIndex(road.a, town.junctions);
    }
    return -1;
}

bool junctionsConnectedWithinHops(const Town& town, int fromId, int toId, int maxHops) {
    if (fromId < 0 || toId < 0 || fromId >= static_cast<int>(town.junctions.size())
        || toId >= static_cast<int>(town.junctions.size())) {
        return false;
    }
    if (fromId == toId) {
        return true;
    }

    std::vector<int> hopDist(static_cast<std::size_t>(town.junctions.size()), -1);
    std::deque<int>  queue;
    hopDist[static_cast<std::size_t>(fromId)] = 0;
    queue.push_back(fromId);

    while (!queue.empty()) {
        const int cur = queue.front();
        queue.pop_front();
        const int curHops = hopDist[static_cast<std::size_t>(cur)];
        if (curHops >= maxHops) {
            continue;
        }

        for (int roadId : town.junctions[static_cast<std::size_t>(cur)].roadIds) {
            if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
                continue;
            }
            const Road& road = town.roads[static_cast<std::size_t>(roadId)];
            if (road.isBridge) {
                continue;
            }
            const int next = otherJunctionOnRoad(town, road, cur);
            if (next < 0 || hopDist[static_cast<std::size_t>(next)] >= 0) {
                continue;
            }
            const int nextHops = curHops + 1;
            hopDist[static_cast<std::size_t>(next)] = nextHops;
            if (next == toId) {
                return true;
            }
            if (nextHops < maxHops) {
                queue.push_back(next);
            }
        }
    }

    return false;
}

WaterBodyRef classifyShoreBridgeCandidate(const Junction& junction, const Town& town,
                                          const TerrainAtlas& terrain,
                                          const BridgeSettings& settings) {
    WaterBodyRef ref =
        classifyJunctionWaterBody(junction.pos, terrain, settings.outlineMaxDist);
    if (ref.valid) {
        return ref;
    }
    if (!junctionHasLandRoad(town, junction.id, terrain)) {
        return ref;
    }
    // Sanitize insets shore stubs off the outline; allow a wider search.
    const float extendedDist = std::max(settings.outlineMaxDist * 3.f, 24.f);
    return classifyJunctionWaterBody(junction.pos, terrain, extendedDist);
}

bool shoresMayBridge(const ShoreJunction& a, const ShoreJunction& b, const Vec2& posA,
                     const Vec2& posB, const TerrainAtlas& terrain) {
    if (!a.waterBody.valid || !b.waterBody.valid) {
        return false;
    }
    if (a.waterBody.sameWaterBodyAs(b.waterBody)) {
        return true;
    }
    if (terrain.catalog == nullptr) {
        return false;
    }
    const TerrainId riverId = terrain.catalog->resolveKey("river");
    const TerrainId seaId   = terrain.catalog->resolveKey("sea");
    return areOppositeBanksForWater(posA, posB, riverId, terrain)
           || areOppositeBanksForWater(posA, posB, seaId, terrain);
}

bool findBestBridgeChord(const Vec2& shoreA, const Vec2& shoreB, const TerrainAtlas& terrain,
                         const BridgeSettings& settings, Vec2& outA, Vec2& outB) {
    const BoundaryFrame frameA = nearestBoundaryFrame(shoreA, terrain);
    const BoundaryFrame frameB = nearestBoundaryFrame(shoreB, terrain);
    Vec2              inwardA  = perpendicular(frameA.tangent);
    Vec2              inwardB  = perpendicular(frameB.tangent);
    if (!terrain.isBuildable(frameA.point + inwardA * 0.4f)) {
        inwardA = inwardA * -1.f;
    }
    if (!terrain.isBuildable(frameB.point + inwardB * 0.4f)) {
        inwardB = inwardB * -1.f;
    }

    outA           = shoreA;
    outB           = shoreB;
    float  bestLen = (outB - outA).length();
    bool   found   = segmentInteriorMostlyWater(outA, outB, terrain);

    if (!settings.snapEnabled) {
        return found;
    }

    constexpr float kSlideStep = 0.5f;
    for (float slideA = -settings.searchRadius; slideA <= settings.searchRadius + 1e-3f;
         slideA += kSlideStep) {
        const Vec2 candA = frameA.point + frameA.tangent * slideA;
        if (!isShorePoint(candA, inwardA, terrain)) {
            continue;
        }
        for (float slideB = -settings.searchRadius; slideB <= settings.searchRadius + 1e-3f;
             slideB += kSlideStep) {
            const Vec2 candB = frameB.point + frameB.tangent * slideB;
            if (!isShorePoint(candB, inwardB, terrain)) {
                continue;
            }
            if (!segmentInteriorMostlyWater(candA, candB, terrain)) {
                continue;
            }
            const float len = (candB - candA).length();
            if (!found || len < bestLen) {
                found   = true;
                bestLen = len;
                outA    = candA;
                outB    = candB;
            }
        }
    }

    return found;
}

struct ShoreBridgeLookup {
    std::vector<const ShoreJunction*> byJunctionId;
};

ShoreBridgeLookup buildShoreBridgeLookup(const std::vector<ShoreJunction>& shoreJunctions,
                                         int junctionCount) {
    ShoreBridgeLookup lookup;
    lookup.byJunctionId.assign(static_cast<std::size_t>(junctionCount), nullptr);
    for (const ShoreJunction& shore : shoreJunctions) {
        if (shore.junctionId >= 0 && shore.junctionId < junctionCount) {
            lookup.byJunctionId[static_cast<std::size_t>(shore.junctionId)] = &shore;
        }
    }
    return lookup;
}

bool isValidBridgePair(int ja, int jb, const Town& town, const TerrainAtlas& terrain,
                       const BridgeSettings& settings, const ShoreBridgeLookup& lookup) {
    if (ja < 0 || jb < 0 || ja == jb || ja >= static_cast<int>(lookup.byJunctionId.size())
        || jb >= static_cast<int>(lookup.byJunctionId.size())) {
        return false;
    }
    const ShoreJunction* shoreA = lookup.byJunctionId[static_cast<std::size_t>(ja)];
    const ShoreJunction* shoreB = lookup.byJunctionId[static_cast<std::size_t>(jb)];
    if (shoreA == nullptr || shoreB == nullptr) {
        return false;
    }

    const Vec2& posA = town.junctions[static_cast<std::size_t>(ja)].pos;
    const Vec2& posB = town.junctions[static_cast<std::size_t>(jb)].pos;
    if (!shoresMayBridge(*shoreA, *shoreB, posA, posB, terrain)) {
        return false;
    }
    const float span = (posB - posA).length();
    if (span > settings.maxSpan || span < kMinSegmentLen) {
        return false;
    }
    if (junctionsConnectedWithinHops(town, ja, jb, kBridgeSameSideMaxHops)) {
        return false;
    }
    return segmentInteriorMostlyWater(posA, posB, terrain);
}

float bridgePairSpan(const Town& town, int ja, int jb) {
    const Vec2& posA = town.junctions[static_cast<std::size_t>(ja)].pos;
    const Vec2& posB = town.junctions[static_cast<std::size_t>(jb)].pos;
    return (posB - posA).length();
}

bool endpointHasBetterPendingPartner(int endpoint, int currentPartner, std::size_t currentIdx,
                                     const std::vector<BridgeCandidate>& candidates,
                                     const std::vector<char>& matched, const Town& town,
                                     const TerrainAtlas& terrain, const BridgeSettings& settings,
                                     const ShoreBridgeLookup& lookup) {
    if (endpoint < 0 || currentPartner < 0) {
        return false;
    }

    const float currentSep = bridgePairSpan(town, endpoint, currentPartner);

    for (std::size_t j = currentIdx + 1; j < candidates.size(); ++j) {
        const BridgeCandidate& future = candidates[j];
        int                    other  = -1;
        if (future.ja == endpoint) {
            other = future.jb;
        } else if (future.jb == endpoint) {
            other = future.ja;
        } else {
            continue;
        }
        if (other == currentPartner || other < 0 || other >= static_cast<int>(matched.size())
            || matched[static_cast<std::size_t>(other)] != 0) {
            continue;
        }
        if (!isValidBridgePair(endpoint, other, town, terrain, settings, lookup)) {
            continue;
        }
        if (bridgePairSpan(town, endpoint, other) < currentSep - 1e-3f) {
            return true;
        }
    }
    return false;
}

}  // namespace

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

void sanitizeRoadGraphAtWater(Town& town, const TerrainAtlas& terrain) {
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
}

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

void mergeWatersideJunctions(Town& town, const TerrainAtlas& terrain, const Config& config) {
    const float mergeDist = config.terrain.shoreJunctionMergeDist;
    if (!terrain.valid || mergeDist <= 0.f) {
        return;
    }

    indexJunctions(town);

    const BridgeSettings settings = resolveBridgeSettings(config);

    std::vector<int> shoreIds;
    shoreIds.reserve(town.junctions.size());
    for (const Junction& junction : town.junctions) {
        const WaterBodyRef waterBody =
            classifyShoreBridgeCandidate(junction, town, terrain, settings);
        if (!waterBody.valid || !junctionHasLandRoad(town, junction.id, terrain)) {
            continue;
        }
        shoreIds.push_back(junction.id);
    }

    if (shoreIds.size() < 2) {
        return;
    }

    const int junctionCount = static_cast<int>(town.junctions.size());
    std::vector<int> parent(static_cast<std::size_t>(junctionCount));
    for (int i = 0; i < junctionCount; ++i) {
        parent[static_cast<std::size_t>(i)] = i;
    }

    const float mergeDistSq = mergeDist * mergeDist;
    for (std::size_t i = 0; i < shoreIds.size(); ++i) {
        const Vec2& posI = town.junctions[static_cast<std::size_t>(shoreIds[i])].pos;
        for (std::size_t j = i + 1; j < shoreIds.size(); ++j) {
            const Vec2 delta = posI - town.junctions[static_cast<std::size_t>(shoreIds[j])].pos;
            if (delta.dot(delta) <= mergeDistSq) {
                uniteUnion(parent, shoreIds[i], shoreIds[j]);
            }
        }
    }

    std::vector<std::vector<int>> byRoot(static_cast<std::size_t>(junctionCount));
    for (int shoreId : shoreIds) {
        byRoot[static_cast<std::size_t>(findUnionRoot(parent, shoreId))].push_back(shoreId);
    }

    int mergedJunctions = 0;
    int clustersMerged  = 0;
    for (int root = 0; root < junctionCount; ++root) {
        const std::vector<int>& cluster = byRoot[static_cast<std::size_t>(root)];
        if (cluster.size() < 2) {
            continue;
        }
        ++clustersMerged;

        Vec2 centroid{0.f, 0.f};
        for (int junctionId : cluster) {
            const Vec2& pos = town.junctions[static_cast<std::size_t>(junctionId)].pos;
            centroid.x += pos.x;
            centroid.y += pos.y;
        }
        const float invCount = 1.f / static_cast<float>(cluster.size());
        centroid.x *= invCount;
        centroid.y *= invCount;

        for (int junctionId : cluster) {
            updateJunctionPosition(town, junctionId, centroid);
        }
        mergedJunctions += static_cast<int>(cluster.size()) - 1;
    }

    if (clustersMerged == 0) {
        return;
    }

    indexJunctions(town);
    const int removedRoads = removeDuplicateAndDegenerateRoads(town);

    Logger::log("voronoi",
                "shore_junction_merge clusters=" + std::to_string(clustersMerged)
                    + " merged_junctions=" + std::to_string(mergedJunctions) + " removed_roads="
                    + std::to_string(removedRoads) + " merge_dist="
                    + std::to_string(mergeDist));
}

void resolveBridges(Town& town, const TerrainAtlas& terrain, const Config& config) {
    if (!terrain.valid || !config.terrain.bridgesEnabled) {
        return;
    }

    indexJunctions(town);
    town.bridgeCandidateJunctionIds.clear();

    const BridgeSettings settings = resolveBridgeSettings(config);

    std::vector<ShoreJunction> shoreJunctions;
    shoreJunctions.reserve(town.junctions.size());

    for (const Junction& junction : town.junctions) {
        const WaterBodyRef waterBody =
            classifyShoreBridgeCandidate(junction, town, terrain, settings);
        if (!waterBody.valid) {
            continue;
        }
        if (!junctionHasLandRoad(town, junction.id, terrain)) {
            continue;
        }
        shoreJunctions.push_back({junction.id, waterBody});
        town.bridgeCandidateJunctionIds.insert(junction.id);
    }

    int shoreSea   = 0;
    int shoreRiver = 0;
    for (const ShoreJunction& shore : shoreJunctions) {
        if (terrain.catalog != nullptr && shore.waterBody.kind == terrain.catalog->resolveKey("sea")) {
            ++shoreSea;
        } else if (terrain.catalog != nullptr
                   && shore.waterBody.kind == terrain.catalog->resolveKey("river")) {
            ++shoreRiver;
        }
    }

    std::vector<BridgeCandidate> candidates;
    candidates.reserve(shoreJunctions.size() * shoreJunctions.size());
    int rejectedBody     = 0;
    int rejectedSpan     = 0;
    int rejectedSameSide = 0;
    int rejectedNotWater = 0;
    for (std::size_t i = 0; i < shoreJunctions.size(); ++i) {
        const int   ja   = shoreJunctions[i].junctionId;
        const Vec2& posA = town.junctions[static_cast<std::size_t>(ja)].pos;
        for (std::size_t j = i + 1; j < shoreJunctions.size(); ++j) {
            const int   jb   = shoreJunctions[j].junctionId;
            const Vec2& posB = town.junctions[static_cast<std::size_t>(jb)].pos;
            if (!shoresMayBridge(shoreJunctions[i], shoreJunctions[j], posA, posB, terrain)) {
                ++rejectedBody;
                continue;
            }
            const float span = (posB - posA).length();
            if (span > settings.maxSpan || span < kMinSegmentLen) {
                ++rejectedSpan;
                continue;
            }
            if (junctionsConnectedWithinHops(town, ja, jb, kBridgeSameSideMaxHops)) {
                ++rejectedSameSide;
                continue;
            }
            if (!segmentInteriorMostlyWater(posA, posB, terrain)) {
                ++rejectedNotWater;
                continue;
            }
            candidates.push_back({ja, jb, span});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const BridgeCandidate& a, const BridgeCandidate& b) { return a.length < b.length; });

    const ShoreBridgeLookup lookup = buildShoreBridgeLookup(shoreJunctions,
                                                              static_cast<int>(town.junctions.size()));

    std::vector<char> junctionMatched(town.junctions.size(), 0);

    int bridgesCreated = 0;
    int bridgesSnapped = 0;
    int rejectedSnap   = 0;
    int deferred       = 0;

    constexpr int kMaxBridgePasses = 64;
    for (int pass = 0; pass < kMaxBridgePasses; ++pass) {
        bool progress = false;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const BridgeCandidate& candidate = candidates[i];
            if (candidate.ja < 0 || candidate.jb < 0
                || candidate.ja >= static_cast<int>(junctionMatched.size())
                || candidate.jb >= static_cast<int>(junctionMatched.size())) {
                continue;
            }
            if (junctionMatched[static_cast<std::size_t>(candidate.ja)] != 0
                || junctionMatched[static_cast<std::size_t>(candidate.jb)] != 0) {
                continue;
            }

            if (endpointHasBetterPendingPartner(candidate.jb, candidate.ja, i, candidates,
                                                junctionMatched, town, terrain, settings, lookup)
                || endpointHasBetterPendingPartner(candidate.ja, candidate.jb, i, candidates,
                                                   junctionMatched, town, terrain, settings,
                                                   lookup)) {
                ++deferred;
                continue;
            }

            const Vec2 origA = town.junctions[static_cast<std::size_t>(candidate.ja)].pos;
            const Vec2 origB = town.junctions[static_cast<std::size_t>(candidate.jb)].pos;

            Vec2 bridgeA = origA;
            Vec2 bridgeB = origB;
            if (!findBestBridgeChord(origA, origB, terrain, settings, bridgeA, bridgeB)) {
                ++rejectedSnap;
                continue;
            }

            junctionMatched[static_cast<std::size_t>(candidate.ja)] = 1;
            junctionMatched[static_cast<std::size_t>(candidate.jb)] = 1;

            const bool snapped = !nearPoint(bridgeA, origA) || !nearPoint(bridgeB, origB);
            updateJunctionPosition(town, candidate.ja, bridgeA);
            updateJunctionPosition(town, candidate.jb, bridgeB);

            Road bridge;
            bridge.id       = static_cast<int>(town.roads.size());
            bridge.a        = bridgeA;
            bridge.b        = bridgeB;
            bridge.isBridge = true;
            town.roads.push_back(bridge);
            ++bridgesCreated;
            if (snapped) {
                ++bridgesSnapped;
            }
            progress = true;
        }
        if (!progress) {
            break;
        }
    }

    for (std::size_t i = 0; i < town.roads.size(); ++i) {
        town.roads[i].id = static_cast<int>(i);
    }
    indexJunctions(town);

    Logger::log("voronoi",
                "bridge_candidates=" + std::to_string(candidates.size()) + " created="
                    + std::to_string(bridgesCreated) + " snapped="
                    + std::to_string(bridgesSnapped) + " max_span="
                    + std::to_string(settings.maxSpan) + " shore_junctions="
                    + std::to_string(shoreJunctions.size()) + " shore_sea="
                    + std::to_string(shoreSea) + " shore_river="
                    + std::to_string(shoreRiver) + " reject_body="
                    + std::to_string(rejectedBody) + " reject_span="
                    + std::to_string(rejectedSpan) + " reject_same_side="
                    + std::to_string(rejectedSameSide) + " reject_not_water="
                    + std::to_string(rejectedNotWater) + " reject_snap="
                    + std::to_string(rejectedSnap) + " deferred="
                    + std::to_string(deferred));
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
