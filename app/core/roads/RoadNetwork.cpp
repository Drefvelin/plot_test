#include "roads/RoadNetwork.h"
#include "roads/RoadNetworkInternal.h"

#include "util/Logger.h"
#include "placement/geometry/PlotGeometry.h"
#include "terrain/TerrainAtlas.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace roadnet {

struct CorridorLink {
    int toJ    = -1;
    int roadId = -1;
};

void resetRoadFrontage(Road& road) {
    road.sideA = RoadSideFrontage{};
    road.sideB = RoadSideFrontage{};
}

bool nearParam(float a, float b, float eps = 1e-4f) {
    return std::abs(a - b) <= eps;
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
                    || distancePointToSegment(probeCenter, road.a, road.b)
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

BridgeSettings resolveBridgeSettings(const Config& config) {
    BridgeSettings settings;
    settings.snapEnabled = config.terrain.bridgeSnapEnabled;
    settings.searchRadius =
        config.terrain.bridgeSnapSearchRadius > 0.f ? config.terrain.bridgeSnapSearchRadius : 8.f;
    settings.maxSpan =
        config.terrain.bridgeMaxSpan > 0.f ? config.terrain.bridgeMaxSpan : 80.f;
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

struct ForbiddenRasterHit {
    TerrainId kind  = kTerrainUnknown;
    Vec2      point{};
    float     dist  = 1e30f;
    bool      valid = false;
};

ForbiddenRasterHit probeNearestForbiddenTerrain(const TerrainAtlas& terrain, const Vec2& pos,
                                                float radius) {
    ForbiddenRasterHit hit;
    if (!terrain.valid || terrain.catalog == nullptr || radius <= 0.f) {
        return hit;
    }

    const auto trySample = [&](const Vec2& samplePos, float dist) {
        const TerrainId kind = terrain.sample(samplePos);
        if (!terrain.catalog->isForbidden(kind)) {
            return;
        }
        if (!hit.valid || dist < hit.dist) {
            hit.valid = true;
            hit.dist  = dist;
            hit.kind  = kind;
            hit.point = samplePos;
        }
    };

    trySample(pos, 0.f);
    if (hit.valid) {
        return hit;
    }

    constexpr float kStep = 0.25f;
    const float     radiusSq = radius * radius;
    for (float dx = -radius; dx <= radius + 1e-3f; dx += kStep) {
        for (float dy = -radius; dy <= radius + 1e-3f; dy += kStep) {
            const float distSq = dx * dx + dy * dy;
            if (distSq > radiusSq + 1e-3f) {
                continue;
            }
            trySample({pos.x + dx, pos.y + dy}, std::sqrt(distSq));
        }
    }

    if (hit.dist > radius) {
        hit.valid = false;
    }
    return hit;
}

WaterBodyRef nearestWatersideWaterBody(const Vec2& pos, const TerrainAtlas& terrain, float radius) {
    WaterBodyRef ref;
    if (terrain.catalog == nullptr || radius <= 0.f) {
        return ref;
    }

    const ForbiddenRasterHit hit = probeNearestForbiddenTerrain(terrain, pos, radius);
    if (!hit.valid) {
        return ref;
    }

    ref.kind  = hit.kind;
    ref.valid = true;
    return ref;
}

std::string junctionRoadIds(const Town& town, int junctionId) {
    if (junctionId < 0 || junctionId >= static_cast<int>(town.junctions.size())) {
        return "-";
    }
    std::ostringstream oss;
    const Junction& junction = town.junctions[static_cast<std::size_t>(junctionId)];
    for (std::size_t i = 0; i < junction.roadIds.size(); ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << junction.roadIds[static_cast<std::size_t>(i)];
    }
    if (junction.roadIds.empty()) {
        oss << '-';
    }
    return oss.str();
}

std::string junctionPosText(const Town& town, int junctionId) {
    if (junctionId < 0 || junctionId >= static_cast<int>(town.junctions.size())) {
        return "?,?";
    }
    const Vec2& pos = town.junctions[static_cast<std::size_t>(junctionId)].pos;
    return std::to_string(pos.x) + "," + std::to_string(pos.y);
}

std::string vec2Text(const Vec2& v) {
    return std::to_string(v.x) + "," + std::to_string(v.y);
}

void collectWatersideJunctionIds(Town& town, const TerrainAtlas& terrain, float radius) {
    town.watersideJunctionIds.clear();
    town.watersideProbeDebug.clear();
    town.bridgeWatersideProbeRadius = radius;
    if (!terrain.valid) {
        return;
    }

    indexJunctions(town);

    Logger::log("bridge",
                "waterside_collect radius=" + std::to_string(radius) + " junctions="
                    + std::to_string(town.junctions.size()) + " probe_step=0.25");

    for (const Junction& junction : town.junctions) {
        const ForbiddenRasterHit hit =
            probeNearestForbiddenTerrain(terrain, junction.pos, radius);
        const TerrainId junctionKind = terrain.sample(junction.pos);

        WatersideProbeDebug debug{};
        debug.junctionId   = junction.id;
        debug.pos          = junction.pos;
        debug.probeRadius  = radius;
        debug.hitValid     = hit.valid;
        debug.hitPoint     = hit.point;
        debug.hitDist      = hit.dist;
        debug.hitKind      = hit.kind;
        debug.junctionKind = junctionKind;
        debug.isWaterside  = hit.valid;
        town.watersideProbeDebug.push_back(debug);

        if (!hit.valid) {
            continue;
        }

        town.watersideJunctionIds.insert(junction.id);
        Logger::log("bridge",
                    "waterside j=" + std::to_string(junction.id) + " pos="
                        + vec2Text(junction.pos) + " roads=" + junctionRoadIds(town, junction.id)
                        + " radius=" + std::to_string(radius) + " raster_dist="
                        + std::to_string(hit.dist) + " hit_kind="
                        + (terrain.catalog != nullptr ? terrain.catalog->name(hit.kind)
                                                      : "unknown")
                        + " hit_point=" + vec2Text(hit.point) + " junction_sample="
                        + (terrain.catalog != nullptr ? terrain.catalog->name(junctionKind)
                                                      : "unknown"));
    }
    Logger::log("bridge", "waterside_count=" + std::to_string(town.watersideJunctionIds.size()));
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
    return true;
}

float bridgePairSpan(const Town& town, int ja, int jb) {
    const Vec2& posA = town.junctions[static_cast<std::size_t>(ja)].pos;
    const Vec2& posB = town.junctions[static_cast<std::size_t>(jb)].pos;
    return (posB - posA).length();
}

bool endpointHasBetterAvailablePartner(int endpoint, int currentPartner,
                                     const std::vector<char>& matched,
                                     const std::vector<ShoreJunction>& shoreJunctions,
                                     const Town& town, const TerrainAtlas& terrain,
                                     const BridgeSettings& settings,
                                     const ShoreBridgeLookup& lookup) {
    if (endpoint < 0 || currentPartner < 0) {
        return false;
    }

    const float currentSep = bridgePairSpan(town, endpoint, currentPartner);

    for (const ShoreJunction& shore : shoreJunctions) {
        const int jk = shore.junctionId;
        if (jk == endpoint || jk == currentPartner) {
            continue;
        }
        if (jk < 0 || jk >= static_cast<int>(matched.size())
            || matched[static_cast<std::size_t>(jk)] != 0) {
            continue;
        }
        if (!isValidBridgePair(endpoint, jk, town, terrain, settings, lookup)) {
            continue;
        }
        if (bridgePairSpan(town, endpoint, jk) < currentSep - 1e-3f) {
            return true;
        }
    }
    return false;
}

bool endpointHasBetterSameBankForPartner(int endpoint, int currentPartner,
                                         const std::vector<ShoreJunction>& shoreJunctions,
                                         const Town& town) {
    if (endpoint < 0 || currentPartner < 0) {
        return false;
    }

    const float endpointSep = bridgePairSpan(town, endpoint, currentPartner);

    for (const ShoreJunction& shore : shoreJunctions) {
        const int jk = shore.junctionId;
        if (jk == endpoint || jk == currentPartner) {
            continue;
        }
        if (!junctionsConnectedWithinHops(town, endpoint, jk, kBridgeSameSideMaxHops)) {
            continue;
        }
        if (bridgePairSpan(town, jk, currentPartner) < endpointSep - 1e-3f) {
            return true;
        }
    }
    return false;
}

std::string findBetterAvailablePartnerReason(int endpoint, int currentPartner,
                                             const std::vector<char>& matched,
                                             const std::vector<ShoreJunction>& shoreJunctions,
                                             const Town& town, const TerrainAtlas& terrain,
                                             const BridgeSettings& settings,
                                             const ShoreBridgeLookup& lookup) {
    if (endpoint < 0 || currentPartner < 0) {
        return {};
    }
    const float currentSep = bridgePairSpan(town, endpoint, currentPartner);
    int         bestJk     = -1;
    float       bestSep    = currentSep;
    for (const ShoreJunction& shore : shoreJunctions) {
        const int jk = shore.junctionId;
        if (jk == endpoint || jk == currentPartner) {
            continue;
        }
        if (jk < 0 || jk >= static_cast<int>(matched.size())
            || matched[static_cast<std::size_t>(jk)] != 0) {
            continue;
        }
        if (!isValidBridgePair(endpoint, jk, town, terrain, settings, lookup)) {
            continue;
        }
        const float sep = bridgePairSpan(town, endpoint, jk);
        if (sep < bestSep - 1e-3f) {
            bestSep = sep;
            bestJk  = jk;
        }
    }
    if (bestJk < 0) {
        return {};
    }
    return "closer_partner jk=" + std::to_string(bestJk) + " span=" + std::to_string(bestSep)
           + " roads=" + junctionRoadIds(town, bestJk);
}

std::string findBetterSameBankReason(int endpoint, int currentPartner,
                                     const std::vector<ShoreJunction>& shoreJunctions,
                                     const Town& town) {
    if (endpoint < 0 || currentPartner < 0) {
        return {};
    }
    const float endpointSep = bridgePairSpan(town, endpoint, currentPartner);
    int         bestJk      = -1;
    float       bestSep     = endpointSep;
    for (const ShoreJunction& shore : shoreJunctions) {
        const int jk = shore.junctionId;
        if (jk == endpoint || jk == currentPartner) {
            continue;
        }
        if (!junctionsConnectedWithinHops(town, endpoint, jk, kBridgeSameSideMaxHops)) {
            continue;
        }
        const float sep = bridgePairSpan(town, jk, currentPartner);
        if (sep < bestSep - 1e-3f) {
            bestSep = sep;
            bestJk  = jk;
        }
    }
    if (bestJk < 0) {
        return {};
    }
    return "same_bank jk=" + std::to_string(bestJk) + " span=" + std::to_string(bestSep)
           + " roads=" + junctionRoadIds(town, bestJk);
}

std::string bridgePairRejectReason(int ja, int jb, const Town& town, const TerrainAtlas& terrain,
                                   const BridgeSettings& settings,
                                   const ShoreBridgeLookup& lookup) {
    if (ja < 0 || jb < 0 || ja == jb || ja >= static_cast<int>(lookup.byJunctionId.size())
        || jb >= static_cast<int>(lookup.byJunctionId.size())) {
        return "bounds";
    }
    const ShoreJunction* shoreA = lookup.byJunctionId[static_cast<std::size_t>(ja)];
    const ShoreJunction* shoreB = lookup.byJunctionId[static_cast<std::size_t>(jb)];
    if (shoreA == nullptr || shoreB == nullptr) {
        return "not_shore";
    }
    const Vec2& posA = town.junctions[static_cast<std::size_t>(ja)].pos;
    const Vec2& posB = town.junctions[static_cast<std::size_t>(jb)].pos;
    if (!shoresMayBridge(*shoreA, *shoreB, posA, posB, terrain)) {
        return "body";
    }
    const float span = (posB - posA).length();
    if (span > settings.maxSpan || span < kMinSegmentLen) {
        return "span";
    }
    if (junctionsConnectedWithinHops(town, ja, jb, kBridgeSameSideMaxHops)) {
        return "same_side";
    }
    return {};
}

}  // namespace roadnet
