// Shared internal helpers for the Town subsystem plus the broadly-used public
// geometry/mesh helpers. The larger public functions live in the topical split
// files (TownJunctions, TownFrontage, TownCarving, TownSecondary,
// TownBridgeBuckets, TownMeshes). Internal helpers are declared in
// TownInternal.h (namespace townint).

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
#include <cstring>
#include <deque>
#include <unordered_set>

namespace townint {

namespace {
constexpr float kRoadSplitEndpointEps  = 0.08f;
constexpr float kRoadSplitOnSegmentEps = 0.05f;
}  // namespace

void carveSegmentSpan(std::vector<RoadFrontageSegment>& segments, const Vec2& origin,
                      const Vec2& edgeDir, float usedStart, float usedEnd,
                      int& nextSegmentId) {
    constexpr float kEps = 0.08f;
    std::vector<RoadFrontageSegment> remaining;
    remaining.reserve(segments.size() + 1);

    for (const RoadFrontageSegment& segment : segments) {
        if (usedEnd <= segment.startT + kEps || usedStart >= segment.endT - kEps) {
            remaining.push_back(segment);
            continue;
        }
        if (usedStart > segment.startT + kEps) {
            RoadFrontageSegment left{segment.id, segment.startT, usedStart};
            if (left.width() > kEps) {
                remaining.push_back(left);
            }
        }
        if (usedEnd < segment.endT - kEps) {
            RoadFrontageSegment right{nextSegmentId++, usedEnd, segment.endT};
            if (right.width() > kEps) {
                remaining.push_back(right);
            }
        }
    }

    segments = std::move(remaining);
}

void carveSideFrontage(RoadSideFrontage& side, const Vec2& origin, const Vec2& edgeDir,
                       float usedStart, float usedEnd, int& nextSegmentId) {
    carveSegmentSpan(side.segments, origin, edgeDir, usedStart, usedEnd, nextSegmentId);
}

float plotTMinLocal(const Plot& plot, const Vec2& origin, const Vec2& edgeDir) {
    const float t0 = (plot.corners[0] - origin).dot(edgeDir);
    const float t1 = (plot.corners[1] - origin).dot(edgeDir);
    return std::min(t0, t1);
}

float plotTMaxLocal(const Plot& plot, const Vec2& origin, const Vec2& edgeDir) {
    const float t0 = (plot.corners[0] - origin).dot(edgeDir);
    const float t1 = (plot.corners[1] - origin).dot(edgeDir);
    return std::max(t0, t1);
}

void carveSideWall(RoadSideFrontage& side, const Vec2& origin, const Vec2& edgeDir, float usedStart,
                   float usedEnd, int& nextSegmentId) {
    carveSegmentSpan(side.wallSegments, origin, edgeDir, usedStart, usedEnd, nextSegmentId);
}

void initWallSegmentOnSide(RoadSideFrontage& side, const Vec2& origin, const Vec2& edgeDir,
                           float setback, float roadLen, int& nextWallId) {
    if (side.inward.length() < 1e-4f || roadLen < setback * 2.f + 0.5f) {
        return;
    }
    RoadFrontageSegment segment;
    segment.id     = nextWallId++;
    segment.startT = setback;
    segment.endT   = roadLen - setback;
    side.wallSegments.push_back(segment);
}

void splitSideWallAtT(RoadSideFrontage& headSide, RoadSideFrontage& tailSide, float splitT,
                      const Vec2& headOrigin, const Vec2& headEdgeDir, const Vec2& tailOrigin,
                      const Vec2& tailEdgeDir, int& nextSegmentId) {
    constexpr float kEps = 0.08f;
    std::vector<RoadFrontageSegment> headRemaining;
    std::vector<RoadFrontageSegment> tailRemaining;
    headRemaining.reserve(headSide.wallSegments.size());
    tailRemaining.reserve(headSide.wallSegments.size());

    for (const RoadFrontageSegment& segment : headSide.wallSegments) {
        if (segment.endT <= splitT + kEps) {
            headRemaining.push_back(segment);
            continue;
        }
        if (segment.startT >= splitT - kEps) {
            RoadFrontageSegment tailSeg = segment;
            tailSeg.startT -= splitT;
            tailSeg.endT -= splitT;
            if (tailSeg.width() > kEps) {
                tailRemaining.push_back(tailSeg);
            }
            continue;
        }
        RoadFrontageSegment headSeg{segment.id, segment.startT, splitT};
        if (headSeg.width() > kEps) {
            headRemaining.push_back(headSeg);
        }
        RoadFrontageSegment tailSeg{nextSegmentId++, 0.f, segment.endT - splitT};
        if (tailSeg.width() > kEps) {
            tailRemaining.push_back(tailSeg);
        }
    }

    headSide.wallSegments = std::move(headRemaining);
    tailSide.wallSegments = std::move(tailRemaining);
}

void splitSideFrontageAtT(RoadSideFrontage& headSide, RoadSideFrontage& tailSide, float splitT,
                          const Vec2& headOrigin, const Vec2& headEdgeDir, const Vec2& tailOrigin,
                          const Vec2& tailEdgeDir, int& nextSegmentId) {
    constexpr float kEps = 0.08f;
    std::vector<RoadFrontageSegment> headRemaining;
    std::vector<RoadFrontageSegment> tailRemaining;
    headRemaining.reserve(headSide.segments.size());
    tailRemaining.reserve(headSide.segments.size());

    for (const RoadFrontageSegment& segment : headSide.segments) {
        if (segment.endT <= splitT + kEps) {
            headRemaining.push_back(segment);
            continue;
        }
        if (segment.startT >= splitT - kEps) {
            RoadFrontageSegment tailSeg = segment;
            tailSeg.startT -= splitT;
            tailSeg.endT -= splitT;
            if (tailSeg.width() > kEps) {
                tailRemaining.push_back(tailSeg);
            }
            continue;
        }
        RoadFrontageSegment headSeg{segment.id, segment.startT, splitT};
        if (headSeg.width() > kEps) {
            headRemaining.push_back(headSeg);
        }
        RoadFrontageSegment tailSeg{nextSegmentId++, 0.f, segment.endT - splitT};
        if (tailSeg.width() > kEps) {
            tailRemaining.push_back(tailSeg);
        }
    }

    headSide.segments = std::move(headRemaining);
    tailSide.inward   = headSide.inward;
    tailSide.segments = std::move(tailRemaining);
}

void splitRoadAtPoint(Town& town, int roadId, const Vec2& point) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road& road = town.roads[static_cast<std::size_t>(roadId)];
    if ((point - road.a).length() < kRoadSplitEndpointEps
        || (point - road.b).length() < kRoadSplitEndpointEps) {
        return;
    }

    Road tail = road;
    road.b    = point;
    tail.a    = point;
    tail.id   = static_cast<int>(town.roads.size());

    if (road.isSecondary) {
        road.sideA = RoadSideFrontage{};
        road.sideB = RoadSideFrontage{};
        tail.sideA = RoadSideFrontage{};
        tail.sideB = RoadSideFrontage{};
    } else {
        for (int bank = 0; bank < 2; ++bank) {
            RoadSideFrontage* headSide = road.sideBank(bank);
            if (headSide == nullptr || headSide->inward.length() < 1e-4f) {
                continue;
            }
            Vec2 headOrigin{};
            Vec2 headFar{};
            Vec2 headEdgeDir{};
            Vec2 tailOrigin{};
            Vec2 tailFar{};
            Vec2 tailEdgeDir{};
            if (!roadFrameForBank(road, bank, headOrigin, headFar, headEdgeDir)
                || !roadFrameForBank(tail, bank, tailOrigin, tailFar, tailEdgeDir)) {
                continue;
            }
            const float splitT = (point - headOrigin).dot(headEdgeDir);
            RoadSideFrontage tailSide{};
            splitSideFrontageAtT(*headSide, tailSide, splitT, headOrigin, headEdgeDir, tailOrigin,
                                 tailEdgeDir, town.frontageSegmentIdCounter);
            splitSideWallAtT(*headSide, tailSide, splitT, headOrigin, headEdgeDir, tailOrigin,
                             tailEdgeDir, town.wallSegmentIdCounter);
            *tail.sideBank(bank) = std::move(tailSide);
        }
    }

    town.roads.push_back(tail);
}

int findRoadInteriorSplitTarget(const Town& town, const Vec2& point, int excludeRoadId) {
    for (const Road& road : town.roads) {
        if (road.id < 0 || road.id == excludeRoadId) {
            continue;
        }
        if ((point - road.a).length() <= kRoadSplitEndpointEps
            || (point - road.b).length() <= kRoadSplitEndpointEps) {
            continue;
        }
        if (distancePointToSegment(point, road.a, road.b) <= kRoadSplitOnSegmentEps) {
            return road.id;
        }
    }
    return -1;
}

void splitRoadsAtAlleyEndpoints(Town& town) {
    for (int pass = 0; pass < 256; ++pass) {
        bool split = false;
        for (const Road& alley : town.roads) {
            if (!alley.isSecondary || alley.id < 0) {
                continue;
            }
            const int destTarget = findRoadInteriorSplitTarget(town, alley.b, alley.id);
            if (destTarget >= 0) {
                splitRoadAtPoint(town, destTarget, alley.b);
                split = true;
                break;
            }
            if (alley.hostRoadId >= 0 && alley.hostRoadId != alley.id
                && alley.hostRoadId < static_cast<int>(town.roads.size())) {
                const Road& host = town.roads[static_cast<std::size_t>(alley.hostRoadId)];
                if (!host.isSecondary
                    && (alley.a - host.a).length() > kRoadSplitEndpointEps
                    && (alley.a - host.b).length() > kRoadSplitEndpointEps
                    && distancePointToSegment(alley.a, host.a, host.b) <= kRoadSplitOnSegmentEps) {
                    splitRoadAtPoint(town, alley.hostRoadId, alley.a);
                    split = true;
                    break;
                }
            }
        }
        if (!split) {
            break;
        }
    }
}

void applySecondaryRoadRecordImpl(Town& town, const SecondaryRoadRecord& rec,
                                  const TerrainAtlas* terrain) {
    constexpr float kSetback = 2.f;

    if (rec.hostRoadId >= 0 && rec.hostRoadId < static_cast<int>(town.roads.size())) {
        const Road& host = town.roads[static_cast<std::size_t>(rec.hostRoadId)];
        if (!host.isSecondary && (rec.a - host.a).length() > kRoadSplitEndpointEps
            && (rec.a - host.b).length() > kRoadSplitEndpointEps
            && distancePointToSegment(rec.a, host.a, host.b) <= kRoadSplitOnSegmentEps) {
            splitRoadAtPoint(town, rec.hostRoadId, rec.a);
        }
    }

    Road alley;
    alley.id                 = static_cast<int>(town.roads.size());
    alley.a                  = rec.a;
    alley.b                  = rec.b;
    alley.hostRoadId         = rec.hostRoadId;
    alley.hostBankIndex      = rec.hostBankIndex;
    alley.isSecondary        = true;
    alley.addedAtQueueIndex  = rec.addedAtQueueIndex;
    town.roads.push_back(alley);
    const int alleyRoadId = alley.id;

    assignRoadSideInwards(town, terrain);
    buildSecondaryRoadFrontageSegments(town.roads[static_cast<std::size_t>(alleyRoadId)], town,
                                     kSetback);
    buildSecondaryWallSegments(town.roads[static_cast<std::size_t>(alleyRoadId)], town, kSetback);

    splitRoadsAtAlleyEndpoints(town);
    indexJunctions(town);
    invalidateRoadTopologyCaches(town);

    town.cachedSecondaryRecordsFingerprint = secondaryRoadRecordsFingerprint(town);
    rebuildSecondaryRoadIdList(town);

    std::unordered_set<int> affectedRoads;
    const auto addRoad = [&](int roadId) {
        if (roadId >= 0 && roadId < static_cast<int>(town.roads.size())) {
            affectedRoads.insert(roadId);
        }
    };
    const auto considerEndpoint = [&](const Vec2& pt) {
        for (const Road& road : town.roads) {
            if (road.id == alleyRoadId) {
                continue;
            }
            if ((pt - road.a).length() <= kRoadSplitEndpointEps
                || (pt - road.b).length() <= kRoadSplitEndpointEps) {
                addRoad(road.id);
            }
        }
    };

    addRoad(rec.hostRoadId);
    addRoad(alleyRoadId);
    considerEndpoint(rec.a);
    considerEndpoint(rec.b);
    for (int roadId : affectedRoads) {
        notifyRoadFrontierRefresh(town, roadId, terrain, nullptr);
    }
}

void initBankPlotSegment(Road& road, int bankIndex, float setback, Town& town) {
    RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f || road.isBridge) {
        return;
    }
    const float len = road.length();
    if (len < setback * 2.f + 0.5f) {
        side->segments.clear();
        return;
    }
    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDir{};
    if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
        return;
    }
    side->segments.clear();
    RoadFrontageSegment segment;
    segment.id     = town.frontageSegmentIdCounter++;
    segment.startT = setback;
    segment.endT   = len - setback;
    side->segments.push_back(segment);
}

void initBankWallSegment(Road& road, int bankIndex, float setback, Town& town) {
    RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f || road.isBridge) {
        return;
    }
    const float len = road.length();
    side->wallSegments.clear();
    if (len < setback * 2.f + 0.5f) {
        return;
    }
    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDir{};
    if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
        return;
    }
    RoadFrontageSegment segment;
    segment.id         = town.wallSegmentIdCounter++;
    segment.startT     = setback;
    segment.endT       = len - setback;
    side->wallSegments.push_back(segment);
}

bool instanceUsesRoadBank(const BuildingInstance& inst, int roadId, int bankIndex) {
    if (inst.placementMode == BuildingPlacementMode::SegmentGapFill) {
        return inst.roadId == roadId && inst.roadBank == bankIndex;
    }
    if (inst.placementMode == BuildingPlacementMode::PlotLot) {
        return inst.plot.roadId == roadId && inst.plot.roadBank == bankIndex;
    }
    if (inst.placementMode == BuildingPlacementMode::BorderPlot) {
        return inst.plot.roadId == roadId && inst.plot.roadBank == bankIndex;
    }
    return false;
}

int otherJunctionOnRoad(const Road& road, int junctionId) {
    if (road.junctionA == junctionId) {
        return road.junctionB;
    }
    if (road.junctionB == junctionId) {
        return road.junctionA;
    }
    return -1;
}

void collectBridgeBucketRoadsFromEndpoint(const Town& town, int bridgeRoadId,
                                          int startJunctionId, int maxHops,
                                          std::unordered_set<int>& out) {
    if (startJunctionId < 0 || startJunctionId >= static_cast<int>(town.junctions.size())
        || maxHops <= 0) {
        return;
    }

    std::vector<int> hopDist(town.junctions.size(), -1);
    std::deque<int>  queue;
    hopDist[static_cast<std::size_t>(startJunctionId)] = 0;
    queue.push_back(startJunctionId);

    while (!queue.empty()) {
        const int curJunctionId = queue.front();
        queue.pop_front();
        const int curHops = hopDist[static_cast<std::size_t>(curJunctionId)];

        const Junction& junction =
            town.junctions[static_cast<std::size_t>(curJunctionId)];
        for (int roadId : junction.roadIds) {
            if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())
                || roadId == bridgeRoadId) {
                continue;
            }
            const Road& road = town.roads[static_cast<std::size_t>(roadId)];
            if (road.isBridge) {
                continue;
            }

            out.insert(roadId);

            const int nextJunctionId = otherJunctionOnRoad(road, curJunctionId);
            if (nextJunctionId < 0
                || hopDist[static_cast<std::size_t>(nextJunctionId)] >= 0) {
                continue;
            }
            const int nextHops = curHops + 1;
            if (nextHops > maxHops) {
                continue;
            }
            hopDist[static_cast<std::size_t>(nextJunctionId)] = nextHops;
            queue.push_back(nextJunctionId);
        }
    }
}

void collectBridgeBucketRoads(const Town& town, int bridgeRoadId, int maxHops,
                              std::unordered_set<int>& out) {
    if (bridgeRoadId < 0 || bridgeRoadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    const Road& bridge = town.roads[static_cast<std::size_t>(bridgeRoadId)];
    if (!bridge.isBridge) {
        return;
    }

    collectBridgeBucketRoadsFromEndpoint(town, bridgeRoadId, bridge.junctionA, maxHops, out);
    collectBridgeBucketRoadsFromEndpoint(town, bridgeRoadId, bridge.junctionB, maxHops, out);
}

int buildingInstanceRoadId(const BuildingInstance& instance) {
    if (instance.roadId >= 0) {
        return instance.roadId;
    }
    return instance.plot.roadId;
}

}  // namespace townint

void appendStripedSegment(sf::VertexArray& tris, const Vec2& a, const Vec2& b, float thickness,
                          const sf::Color& colorA, const sf::Color& colorB, float stripeLength) {
    const Vec2  delta = b - a;
    const float len   = delta.length();
    if (len < 1e-3f || stripeLength < 1e-3f) {
        return;
    }
    const Vec2 dir = delta * (1.f / len);

    float traveled = 0.f;
    bool  useA     = true;
    while (traveled < len - 1e-3f) {
        const float stripeEnd = std::min(traveled + stripeLength, len);
        appendThickSegment(tris, a + dir * traveled, a + dir * stripeEnd, thickness,
                           useA ? colorA : colorB);
        traveled = stripeEnd;
        useA     = !useA;
    }
}

void appendJunctionDisc(sf::VertexArray& mesh, const sf::Vector2f& center, float radiusPx,
                        const sf::Color& color, int segments) {
    for (int i = 0; i < segments; ++i) {
        const float a0 = static_cast<float>(i) / segments * 2.f * 3.14159265f;
        const float a1 = static_cast<float>(i + 1) / segments * 2.f * 3.14159265f;
        mesh.append({center, color});
        mesh.append(
            {{center.x + std::cos(a0) * radiusPx, center.y + std::sin(a0) * radiusPx}, color});
        mesh.append(
            {{center.x + std::cos(a1) * radiusPx, center.y + std::sin(a1) * radiusPx}, color});
    }
}

bool pointInsideTownDisc(const Town& town, const Vec2& p) {
    return (p - town.center).length() <= town.radius + 1e-3f;
}

bool roadFrameForBank(const Road& road, int bankIndex, Vec2& origin, Vec2& farEnd, Vec2& edgeDir) {
    const float len = road.length();
    if (len < 1e-6f) {
        return false;
    }
    if (bankIndex == 1) {
        origin = road.b;
        farEnd = road.a;
        edgeDir = (road.a - road.b) * (1.f / len);
    } else {
        origin = road.a;
        farEnd = road.b;
        edgeDir = (road.b - road.a) * (1.f / len);
    }
    return true;
}
