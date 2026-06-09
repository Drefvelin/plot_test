#include "RoadNetwork.h"

#include "Logger.h"
#include "PlotGeometry.h"
#include "TerrainAtlas.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace {

constexpr float kInteriorCrossEps = 0.03f;
constexpr float kMinSegmentLen    = 0.5f;
constexpr float kJunctionEps      = 0.08f;

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
    bool  snapEnabled   = true;
    float searchRadius  = 8.f;
};

BridgeSettings resolveBridgeSettings(const Config& config) {
    BridgeSettings settings;
    settings.snapEnabled = config.terrain.bridgeSnapEnabled;
    settings.searchRadius =
        config.terrain.bridgeSnapSearchRadius > 0.f ? config.terrain.bridgeSnapSearchRadius : 8.f;
    return settings;
}

bool roadEligibleForBridgeProcessing(const Road& road) {
    if (road.isSecondary || road.isTerrainCorridor || road.isBridge) {
        return false;
    }
    if (road.cellA == -1 && road.cellB == -1) {
        return false;
    }
    return true;
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

    for (float dist = step * 0.5f; dist < len - step * 0.5f; dist += step) {
        const float t = dist / len;
        if (terrain.isBuildable(a + delta * t)) {
            return false;
        }
    }
    return true;
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

bool isJunctionShore(const Vec2& pos, const TerrainAtlas& terrain) {
    const BoundaryFrame frame  = nearestBoundaryFrame(pos, terrain);
    const Vec2          inward = landwardInward(frame, terrain);
    return isShorePoint(pos, inward, terrain);
}

bool shouldStripWaterStub(const Road& road, const TerrainAtlas& terrain) {
    if (!roadEligibleForBridgeProcessing(road)) {
        return false;
    }

    const Vec2 mid = (road.a + road.b) * 0.5f;
    if (terrain.isBuildable(mid)) {
        return false;
    }

    const Vec2  delta = road.b - road.a;
    const float len   = delta.length();
    if (len < 1e-4f) {
        return true;
    }

    const Vec2 dir     = delta * (1.f / len);
    const bool aShore  = isShorePoint(road.a, dir * -1.f, terrain);
    const bool bShore  = isShorePoint(road.b, dir, terrain);
    const bool aWater  = !aShore;
    const bool bWater  = !bShore;

    if (aWater && bWater) {
        return true;
    }
    if (aShore && bWater) {
        return true;
    }
    if (aWater && bShore) {
        return true;
    }

    return true;
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
    bool   found   = segmentEntirelyForbidden(outA, outB, terrain);

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
            if (!segmentEntirelyForbidden(candA, candB, terrain)) {
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
                road.cellA             = -1;
                road.cellB             = -1;
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
            segment.cellA = source.cellA;
            segment.cellB = source.cellB;
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

int splitRoadsAtForbiddenBoundary(Town& town, const TerrainAtlas& terrain) {
    if (!terrain.valid) {
        return 0;
    }

    const int        roadCount = static_cast<int>(town.roads.size());
    std::vector<Road> splitRoads;
    splitRoads.reserve(static_cast<std::size_t>(roadCount) * 2);
    int boundarySplits = 0;

    for (int i = 0; i < roadCount; ++i) {
        const Road& source = town.roads[static_cast<std::size_t>(i)];
        if (!roadEligibleForBridgeProcessing(source)) {
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

            Road segment  = source;
            segment.a     = lerpVec(source.a, source.b, t0);
            segment.b     = lerpVec(source.a, source.b, t1);
            segment.cellA = source.cellA;
            segment.cellB = source.cellB;
            resetRoadFrontage(segment);
            splitRoads.push_back(segment);
        }
    }

    if (boundarySplits == 0 && splitRoads.size() == town.roads.size()) {
        Logger::log("voronoi", "boundary_splits=0");
        return 0;
    }

    for (std::size_t i = 0; i < splitRoads.size(); ++i) {
        splitRoads[i].id = static_cast<int>(i);
    }
    town.roads = std::move(splitRoads);

    Logger::log("voronoi", "boundary_splits=" + std::to_string(boundarySplits));
    return boundarySplits;
}

void resolveBridges(Town& town, const TerrainAtlas& terrain, const Config& config) {
    if (!terrain.valid || !config.terrain.bridgesEnabled) {
        return;
    }

    indexJunctions(town);

    const BridgeSettings settings = resolveBridgeSettings(config);

    std::vector<Road> keptRoads;
    keptRoads.reserve(town.roads.size());
    int waterStubsRemoved = 0;

    for (const Road& road : town.roads) {
        if (shouldStripWaterStub(road, terrain)) {
            ++waterStubsRemoved;
            continue;
        }
        keptRoads.push_back(road);
    }

    for (std::size_t i = 0; i < keptRoads.size(); ++i) {
        keptRoads[i].id = static_cast<int>(i);
    }
    town.roads = std::move(keptRoads);
    indexJunctions(town);

    std::vector<int> shoreJunctions;
    shoreJunctions.reserve(town.junctions.size());

    for (const Junction& junction : town.junctions) {
        if (!isJunctionShore(junction.pos, terrain)) {
            continue;
        }

        bool hasLandRoad = false;
        for (int roadId : junction.roadIds) {
            if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
                continue;
            }
            if (isLandRoad(town.roads[static_cast<std::size_t>(roadId)], terrain)) {
                hasLandRoad = true;
                break;
            }
        }

        if (hasLandRoad) {
            shoreJunctions.push_back(junction.id);
        }
    }

    std::vector<BridgeCandidate> candidates;
    for (std::size_t i = 0; i < shoreJunctions.size(); ++i) {
        const int   ja   = shoreJunctions[i];
        const Vec2& posA = town.junctions[static_cast<std::size_t>(ja)].pos;
        for (std::size_t j = i + 1; j < shoreJunctions.size(); ++j) {
            const int   jb   = shoreJunctions[j];
            const Vec2& posB = town.junctions[static_cast<std::size_t>(jb)].pos;
            if (!areOppositeBanks(posA, posB, terrain)) {
                continue;
            }
            if (!segmentEntirelyForbidden(posA, posB, terrain)) {
                continue;
            }
            candidates.push_back({ja, jb, (posB - posA).length()});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const BridgeCandidate& a, const BridgeCandidate& b) { return a.length < b.length; });

    std::vector<char> junctionMatched(town.junctions.size(), 0);
    std::vector<BridgeCandidate> matchedPairs;
    matchedPairs.reserve(candidates.size());

    for (const BridgeCandidate& candidate : candidates) {
        if (candidate.ja < 0 || candidate.jb < 0
            || candidate.ja >= static_cast<int>(junctionMatched.size())
            || candidate.jb >= static_cast<int>(junctionMatched.size())) {
            continue;
        }
        if (junctionMatched[static_cast<std::size_t>(candidate.ja)] != 0
            || junctionMatched[static_cast<std::size_t>(candidate.jb)] != 0) {
            continue;
        }
        junctionMatched[static_cast<std::size_t>(candidate.ja)] = 1;
        junctionMatched[static_cast<std::size_t>(candidate.jb)] = 1;
        matchedPairs.push_back(candidate);
    }

    int bridgesCreated = 0;
    int bridgesSnapped = 0;

    for (const BridgeCandidate& pair : matchedPairs) {
        const Vec2 origA = town.junctions[static_cast<std::size_t>(pair.ja)].pos;
        const Vec2 origB = town.junctions[static_cast<std::size_t>(pair.jb)].pos;

        Vec2 bridgeA = origA;
        Vec2 bridgeB = origB;
        if (!findBestBridgeChord(origA, origB, terrain, settings, bridgeA, bridgeB)) {
            continue;
        }

        const bool snapped = !nearPoint(bridgeA, origA) || !nearPoint(bridgeB, origB);
        updateJunctionPosition(town, pair.ja, bridgeA);
        updateJunctionPosition(town, pair.jb, bridgeB);

        Road bridge;
        bridge.id        = static_cast<int>(town.roads.size());
        bridge.a         = bridgeA;
        bridge.b         = bridgeB;
        bridge.isBridge  = true;
        bridge.cellA     = -1;
        bridge.cellB     = -1;
        town.roads.push_back(bridge);
        ++bridgesCreated;
        if (snapped) {
            ++bridgesSnapped;
        }
    }

    for (std::size_t i = 0; i < town.roads.size(); ++i) {
        town.roads[i].id = static_cast<int>(i);
    }
    indexJunctions(town);

    Logger::log("voronoi", "water_stubs_removed=" + std::to_string(waterStubsRemoved)
                                + " shore_junctions=" + std::to_string(shoreJunctions.size())
                                + " bridge_pairs_matched=" + std::to_string(matchedPairs.size())
                                + " bridges_created=" + std::to_string(bridgesCreated)
                                + " bridges_snapped=" + std::to_string(bridgesSnapped));
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
