#include "Town.h"

#include "Logger.h"
#include "Profile.h"
#include "PlacementFloors.h"
#include "PlacementFrontier.h"
#include "FrontierManager.h"
#include "RoadExhaustion.h"
#include "PlotGeometry.h"
#include "FrontageZones.h"
#include "Config.h"
#include "TerrainAtlas.h"
#include "TownConfig.h"
#include "Units.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <unordered_set>

namespace {

bool nearPoint(const Vec2& a, const Vec2& b, float eps = 0.08f) {
    return (a - b).length() <= eps;
}

void appendThickSegment(sf::VertexArray& tris, const Vec2& a, const Vec2& b, float thickness,
                        const sf::Color& color) {
    const Vec2  delta = b - a;
    const float len   = delta.length();
    if (len < 1e-4f) {
        return;
    }

    const float nx = -delta.y / len * thickness * 0.5f;
    const float ny = delta.x / len * thickness * 0.5f;

    tris.append({{a.x + nx, a.y + ny}, color});
    tris.append({{a.x - nx, a.y - ny}, color});
    tris.append({{b.x + nx, b.y + ny}, color});

    tris.append({{b.x + nx, b.y + ny}, color});
    tris.append({{a.x - nx, a.y - ny}, color});
    tris.append({{b.x - nx, b.y - ny}, color});
}

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

}  // namespace

namespace {

void appendThickSegmentPublic(sf::VertexArray& tris, const Vec2& a, const Vec2& b, float thickness,
                              const sf::Color& color) {
    const Vec2  delta = b - a;
    const float len   = delta.length();
    if (len < 1e-4f) {
        return;
    }
    const float nx = -delta.y / len * thickness * 0.5f;
    const float ny = delta.x / len * thickness * 0.5f;
    tris.append({{a.x + nx, a.y + ny}, color});
    tris.append({{a.x - nx, a.y - ny}, color});
    tris.append({{b.x + nx, b.y + ny}, color});
    tris.append({{b.x + nx, b.y + ny}, color});
    tris.append({{a.x - nx, a.y - ny}, color});
    tris.append({{b.x - nx, b.y - ny}, color});
}

}  // namespace

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
        appendThickSegmentPublic(tris, a + dir * traveled, a + dir * stripeEnd, thickness,
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

void appendCircleRing(sf::VertexArray& mesh, const sf::Vector2f& center, float radiusPx,
                      float thicknessPx, const sf::Color& color, int segments = 48) {
    sf::Vector2f prev{center.x + radiusPx, center.y};
    for (int i = 1; i <= segments; ++i) {
        const float    a = static_cast<float>(i) / static_cast<float>(segments) * 2.f
                        * 3.14159265f;
        const sf::Vector2f next{center.x + std::cos(a) * radiusPx,
                                center.y + std::sin(a) * radiusPx};
        appendThickSegment(mesh, {prev.x, prev.y}, {next.x, next.y}, thicknessPx, color);
        prev = next;
    }
}

void appendHitCross(sf::VertexArray& mesh, const sf::Vector2f& center, float armPx,
                    float thicknessPx, const sf::Color& color) {
    appendThickSegment(mesh, {center.x - armPx, center.y - armPx}, {center.x + armPx, center.y + armPx},
                       thicknessPx, color);
    appendThickSegment(mesh, {center.x - armPx, center.y + armPx}, {center.x + armPx, center.y - armPx},
                       thicknessPx, color);
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

void buildJunctionMesh(Town& town, float pixelsPerUnit, float radiusUnits) {
    const float radiusPx = units::toPixels(radiusUnits, pixelsPerUnit);

    town.junctionMesh.setPrimitiveType(sf::Triangles);
    town.junctionMesh.clear();

    const sf::Color kBridgeCandidatePurple(128, 0, 200);
    for (const Junction& junction : town.junctions) {
        const sf::Vector2f center{units::toPixels(junction.pos.x, pixelsPerUnit),
                                  units::toPixels(junction.pos.y, pixelsPerUnit)};
        const bool isCandidate =
            town.watersideJunctionIds.count(junction.id) != 0;
        appendJunctionDisc(town.junctionMesh, center, radiusPx,
                             isCandidate ? kBridgeCandidatePurple : sf::Color(255, 0, 0));
    }
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

void buildSecondaryRoadFrontageSegments(Road& road, Town& town, float setback) {
    const float len = road.length();
    if (len < setback * 2.f + 0.5f) {
        return;
    }

    for (int bank = 0; bank < 2; ++bank) {
        RoadSideFrontage* side = road.sideBank(bank);
        if (side->inward.length() < 1e-4f) {
            continue;
        }

        Vec2 origin{};
        Vec2 farEnd{};
        Vec2 edgeDir{};
        if (!roadFrameForBank(road, bank, origin, farEnd, edgeDir)) {
            continue;
        }

        RoadFrontageSegment segment;
        segment.id = town.frontageSegmentIdCounter++;
        segment.startT = setback;
        segment.endT = len - setback;
        side->segments.push_back(segment);
    }
}

void resetRoadFrontageSegments(Town& town, float frontageSetback, bool resetSegmentIds) {
    if (resetSegmentIds) {
        town.frontageSegmentIdCounter = 0;
    }

    for (Road& road : town.roads) {
        road.sideA.segments.clear();
        road.sideB.segments.clear();
        if (road.isBridge) {
            continue;
        }

        const float len = road.length();
        if (len < frontageSetback * 2.f + 0.5f) {
            continue;
        }

        for (int bank = 0; bank < 2; ++bank) {
            RoadSideFrontage* side = road.sideBank(bank);
            if (side->inward.length() < 1e-4f) {
                continue;
            }

            Vec2 origin{};
            Vec2 farEnd{};
            Vec2 edgeDir{};
            if (!roadFrameForBank(road, bank, origin, farEnd, edgeDir)) {
                continue;
            }

            RoadFrontageSegment segment;
            segment.id = town.frontageSegmentIdCounter++;
            segment.startT = frontageSetback;
            segment.endT = len - frontageSetback;
            side->segments.push_back(segment);
        }
    }
}

void buildSecondaryWallSegments(Road& road, Town& town, float setback) {
    const float len = road.length();
    if (len < setback * 2.f + 0.5f) {
        return;
    }

    for (int bank = 0; bank < 2; ++bank) {
        RoadSideFrontage* side = road.sideBank(bank);
        Vec2              origin{};
        Vec2              farEnd{};
        Vec2              edgeDir{};
        if (!roadFrameForBank(road, bank, origin, farEnd, edgeDir)) {
            continue;
        }
        initWallSegmentOnSide(*side, origin, edgeDir, setback, len, town.wallSegmentIdCounter);
    }
}

void resetWallSegments(Town& town, float frontageSetback, bool resetSegmentIds) {
    if (resetSegmentIds) {
        town.wallSegmentIdCounter = 0;
    }

    for (Road& road : town.roads) {
        road.sideA.wallSegments.clear();
        road.sideB.wallSegments.clear();
        if (road.isBridge) {
            continue;
        }

        const float len = road.length();
        if (len < frontageSetback * 2.f + 0.5f) {
            continue;
        }

        for (int bank = 0; bank < 2; ++bank) {
            RoadSideFrontage* side = road.sideBank(bank);
            Vec2              origin{};
            Vec2              farEnd{};
            Vec2              edgeDir{};
            if (!roadFrameForBank(road, bank, origin, farEnd, edgeDir)) {
                continue;
            }
            initWallSegmentOnSide(*side, origin, edgeDir, frontageSetback, len,
                                  town.wallSegmentIdCounter);
        }
    }
}

void trimSecondaryRoadRecords(Town& town, int targetCount) {
    town.secondaryRoadRecords.erase(
        std::remove_if(town.secondaryRoadRecords.begin(), town.secondaryRoadRecords.end(),
                       [targetCount](const SecondaryRoadRecord& rec) {
                           return rec.addedAtQueueIndex >= targetCount;
                       }),
        town.secondaryRoadRecords.end());
}

namespace {

constexpr float kRoadSplitEndpointEps = 0.08f;
constexpr float kRoadSplitOnSegmentEps = 0.05f;

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

}  // namespace

std::uint64_t secondaryRoadRecordsFingerprint(const Town& town) {
    const auto floatBits = [](float value) -> std::uint64_t {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };

    std::uint64_t hash = 14695981039346656037ULL;
    for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
        const auto mix = [&hash](std::uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ULL;
        };
        mix(static_cast<std::uint64_t>(rec.hostRoadId));
        mix(static_cast<std::uint64_t>(rec.hostBankIndex));
        mix(static_cast<std::uint64_t>(rec.addedAtQueueIndex));
        mix(floatBits(rec.a.x));
        mix(floatBits(rec.a.y));
        mix(floatBits(rec.b.x));
        mix(floatBits(rec.b.y));
    }
    return hash;
}

void rebuildSecondaryRoadsFromRecords(Town& town, const TerrainAtlas* terrain) {
    town.roads.erase(std::remove_if(town.roads.begin(), town.roads.end(),
                                    [](const Road& road) { return road.isSecondary; }),
                     town.roads.end());

    for (std::size_t i = 0; i < town.roads.size(); ++i) {
        town.roads[i].id = static_cast<int>(i);
    }
    town.primaryRoadCount = static_cast<int>(town.roads.size());

    for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
        Road road;
        road.id = static_cast<int>(town.roads.size());
        road.a = rec.a;
        road.b = rec.b;
        road.hostRoadId = rec.hostRoadId;
        road.hostBankIndex = rec.hostBankIndex;
        road.isSecondary = true;
        road.addedAtQueueIndex = rec.addedAtQueueIndex;
        town.roads.push_back(road);
    }

    assignRoadSideInwards(town, terrain);
    for (Road& road : town.roads) {
        if (road.isSecondary) {
            buildSecondaryRoadFrontageSegments(road, town, 2.f);
            buildSecondaryWallSegments(road, town, 2.f);
        }
    }
    splitRoadsAtAlleyEndpoints(town);
    indexJunctions(town);
    invalidateRoadTopologyCaches(town);
    town.cachedSecondaryRecordsFingerprint = secondaryRoadRecordsFingerprint(town);
    rebuildSecondaryRoadIdList(town);
    PlacementEvent event;
    event.type = PlacementEventType::TopologyChanged;
    notifyPlacementFrontier(town, event, terrain, town.syncTerrainCatalog, &town.syncTerrainProbes,
                            nullptr);
}

void removeSecondaryRoadAtQueueIndex(Town& town, int queueIndex) {
    town.secondaryRoadRecords.erase(
        std::remove_if(town.secondaryRoadRecords.begin(), town.secondaryRoadRecords.end(),
                       [queueIndex](const SecondaryRoadRecord& rec) {
                           return rec.addedAtQueueIndex == queueIndex;
                       }),
        town.secondaryRoadRecords.end());
    rebuildSecondaryRoadsFromRecords(town);
}

void ensurePlacementSyncMins(Town& town, const PlacementFloors& floors, const TownConfig& townCfg,
                             float frontageSetback) {
    town.syncMinPlotFrontage  = floors.minPlotFrontage;
    town.syncMinGapWidth      = floors.minGapWidth;
    town.syncMinAlleyGapWidth = townCfg.minWallGapForAlley;
    town.syncBorderOutlineProbeMaxDist = townCfg.borderOutlineProbeMaxDist;
    town.syncBorderSampleStep          = townCfg.borderOutlineSampleStep;
    town.syncBorderMaxAttempts         = townCfg.borderMaxAttempts;
    town.syncFrontageSetback           = frontageSetback;
}

void ensureTownFrontageInitialized(Town& town, float setback, const PlacementFloors& floors,
                                   const TownConfig& townCfg, const TerrainAtlas* terrain,
                                   const PlotConfig* plots, const TerrainCatalog* catalog,
                                   const TerrainProbeConfig* probes) {
    if (town.frontageInitialized) {
        return;
    }
    if (catalog != nullptr) {
        town.syncTerrainCatalog = catalog;
    } else if (terrain != nullptr && terrain->catalog != nullptr) {
        town.syncTerrainCatalog = terrain->catalog;
    }
    if (probes != nullptr) {
        town.syncTerrainProbes = *probes;
    }
    town.syncTerrainAtlas = (terrain != nullptr && terrain->valid) ? terrain : nullptr;
    ensurePlacementSyncMins(town, floors, townCfg, setback);
    resetRoadFrontageSegments(town, setback, true);
    resetWallSegments(town, setback, true);
    clearAllRoadExhaustion(town);
    recomputePlotGapDoneTown(town);
    PlacementEvent event;
    event.type = PlacementEventType::FullRebuild;
    notifyPlacementFrontier(town, event, terrain, town.syncTerrainCatalog, &town.syncTerrainProbes,
                            plots);
    rebuildAllMainOccupancyT(town);
    rebuildSecondaryRoadIdList(town);
    town.frontageInitialized              = true;
    town.cachedSecondaryRecordsFingerprint = secondaryRoadRecordsFingerprint(town);
}

namespace {

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

}  // namespace

void restoreBankFrontageFromInstances(Town& town, int roadId, int bankIndex, float frontageSetback) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road& road = town.roads[static_cast<std::size_t>(roadId)];
    initBankPlotSegment(road, bankIndex, frontageSetback, town);
    initBankWallSegment(road, bankIndex, frontageSetback, town);
    clearAlleyGapStateForRoad(town, roadId);

    for (const BuildingInstance& inst : town.buildingInstances) {
        if (!instanceUsesRoadBank(inst, roadId, bankIndex)) {
            continue;
        }
        if (inst.placementMode == BuildingPlacementMode::SegmentGapFill) {
            if (!inst.footprints.empty()) {
                carveRoadFrontageForFootprint(town, inst.roadId, inst.roadBank, inst.footprints[0]);
                carveRoadWallForFootprint(town, inst.roadId, inst.roadBank, inst.footprints[0]);
            }
        } else {
            carveRoadFrontageForPlot(town, inst.plot, frontageSetback);
            for (const BuildingFootprint& footprint : inst.footprints) {
                if (footprint.mainBuilding) {
                    carveRoadWallForFootprint(town, inst.plot.roadId, inst.plot.roadBank, footprint);
                }
            }
        }
    }

    refreshBankExhaustionAfterCarve(town, roadId, bankIndex);
    rebuildMainOccupancyForBank(town, roadId, bankIndex);
    frontierRefreshPlotBank(town, roadId, bankIndex);
    frontierRefreshWallBank(town, roadId, bankIndex);
    frontierRefreshAlleyBank(town, roadId, bankIndex, town.syncMinAlleyGapWidth);
}

void restoreRoadFrontageFromInstances(Town& town, int roadId, float frontageSetback) {
    for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
        restoreBankFrontageFromInstances(town, roadId, bankIndex, frontageSetback);
    }
}

void restoreBankWallFromInstances(Town& town, int roadId, int bankIndex, float frontageSetback) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road& road = town.roads[static_cast<std::size_t>(roadId)];
    initBankWallSegment(road, bankIndex, frontageSetback, town);

    for (const BuildingInstance& inst : town.buildingInstances) {
        if (!instanceUsesRoadBank(inst, roadId, bankIndex)) {
            continue;
        }
        if (inst.placementMode == BuildingPlacementMode::SegmentGapFill) {
            if (!inst.footprints.empty()) {
                carveRoadWallForFootprint(town, inst.roadId, inst.roadBank, inst.footprints[0]);
            }
        } else {
            for (const BuildingFootprint& footprint : inst.footprints) {
                if (footprint.mainBuilding) {
                    carveRoadWallForFootprint(town, inst.plot.roadId, inst.plot.roadBank, footprint);
                }
            }
        }
    }
}

void restoreRoadWallFromInstances(Town& town, int roadId, float frontageSetback) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    clearAlleyGapStateForRoad(town, roadId);
    for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
        restoreBankWallFromInstances(town, roadId, bankIndex, frontageSetback);
        refreshBankExhaustionAfterCarve(town, roadId, bankIndex);
        rebuildMainOccupancyForBank(town, roadId, bankIndex);
        frontierRefreshWallBank(town, roadId, bankIndex);
        frontierRefreshAlleyBank(town, roadId, bankIndex, town.syncMinAlleyGapWidth);
    }
}

void removeBuildingInstance(Town& town, int instanceId, float frontageSetback,
                            const TerrainAtlas* terrain, const PlotConfig* plots) {
    const auto it =
        std::find_if(town.buildingInstances.begin(), town.buildingInstances.end(),
                     [instanceId](const BuildingInstance& inst) { return inst.id == instanceId; });
    if (it == town.buildingInstances.end()) {
        return;
    }

    int  roadId       = -1;
    int  bankIdx      = 0;
    bool wasBorderPlot = it->placementMode == BuildingPlacementMode::BorderPlot
                         || it->placementMode == BuildingPlacementMode::BorderBuilding;
    if (it->placementMode == BuildingPlacementMode::SegmentGapFill) {
        roadId  = it->roadId;
        bankIdx = it->roadBank;
    } else if (it->placementMode == BuildingPlacementMode::PlotLot
               || it->placementMode == BuildingPlacementMode::BorderPlot) {
        roadId  = it->plot.roadId;
        bankIdx = it->plot.roadBank;
    } else if (it->placementMode == BuildingPlacementMode::BorderBuilding && it->roadId >= 0) {
        roadId  = it->roadId;
        bankIdx = it->roadBank;
    }

    town.buildingInstances.erase(it);

    if (roadId >= 0) {
        restoreBankFrontageFromInstances(town, roadId, bankIdx, frontageSetback);
    }

    {
        PlacementEvent event;
        event.type          = PlacementEventType::InstanceRemoved;
        event.roadId        = roadId;
        event.bankIndex     = bankIdx;
        event.instanceId    = instanceId;
        event.wasBorderPlot = wasBorderPlot;
        notifyPlacementFrontier(town, event, terrain, town.syncTerrainCatalog, &town.syncTerrainProbes,
                                plots);
    }
}

void applySecondaryRoadRecord(Town& town, const SecondaryRoadRecord& rec,
                              const TerrainAtlas* terrain) {
    applySecondaryRoadRecordImpl(town, rec, terrain);
}

void buildRoadEndProbeMesh(Town& town, float /*pixelsPerUnit*/, float /*probeLengthUnits*/) {
    town.roadEndProbeMesh.setPrimitiveType(sf::Triangles);
    town.roadEndProbeMesh.clear();
    town.roadEndProbeLabels.clear();
}

void carveRoadFrontageForPlot(Town& town, const Plot& plot, float /*frontageSetback*/,
                              const TerrainAtlas* terrain, bool notifyFrontier) {
    PROFILE_SCOPE(ProfileScopeId::FrontageCarve);
    if (plot.roadId < 0 || plot.roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road& road = town.roads[static_cast<std::size_t>(plot.roadId)];
    RoadSideFrontage* side = road.sideBank(plot.roadBank);

    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDir{};
    if (!roadFrameForBank(road, plot.roadBank, origin, farEnd, edgeDir)) {
        return;
    }

    const float tMin = plotTMinLocal(plot, origin, edgeDir);
    const float tMax = plotTMaxLocal(plot, origin, edgeDir);
    carveSideFrontage(*side, origin, edgeDir, tMin, tMax, town.frontageSegmentIdCounter);
    clearAlleyGapStateForRoad(town, plot.roadId);
    refreshBankExhaustionAfterCarve(town, plot.roadId, plot.roadBank);
    if (notifyFrontier) {
        PlacementEvent event;
        event.type       = PlacementEventType::PlotCarved;
        event.roadId     = plot.roadId;
        event.bankIndex  = plot.roadBank;
        event.carveTMin  = tMin;
        event.carveTMax  = tMax;
        event.wasBorderPlot = plot.outlineTangent.length() > 1e-4f;
        notifyPlacementFrontier(town, event, terrain, town.syncTerrainCatalog, &town.syncTerrainProbes,
                            nullptr);
    } else {
        frontierRefreshPlotBank(town, plot.roadId, plot.roadBank);
    }
    rebuildMainOccupancyForBank(town, plot.roadId, plot.roadBank);
}

void carveRoadFrontageForPlot(Town& town, const Plot& plot, float frontageSetback) {
    carveRoadFrontageForPlot(town, plot, frontageSetback, nullptr, false);
}

void carveRoadFrontageForFootprint(Town& town, int roadId, int bankIndex,
                                   const BuildingFootprint& mainFootprint) {
    PROFILE_SCOPE(ProfileScopeId::FrontageCarve);
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road& road = town.roads[static_cast<std::size_t>(roadId)];
    RoadSideFrontage* side = road.sideBank(bankIndex);

    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDir{};
    if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
        return;
    }

    float usedStart = 1e9f;
    float usedEnd   = -1e9f;
    for (const Vec2& corner : mainFootprint.corners) {
        const float t = (corner - origin).dot(edgeDir);
        usedStart = std::min(usedStart, t);
        usedEnd = std::max(usedEnd, t);
    }

    carveSideFrontage(*side, origin, edgeDir, usedStart, usedEnd, town.frontageSegmentIdCounter);
    clearAlleyGapStateForRoad(town, roadId);
    refreshBankExhaustionAfterCarve(town, roadId, bankIndex);
    frontierRefreshPlotBank(town, roadId, bankIndex);
    rebuildMainOccupancyForBank(town, roadId, bankIndex);
}

void carveRoadWallForFootprint(Town& town, int roadId, int bankIndex,
                               const BuildingFootprint& mainFootprint, const TerrainAtlas* terrain,
                               bool notifyFrontier) {
    PROFILE_SCOPE(ProfileScopeId::FrontageWallCarve);
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road&             road = town.roads[static_cast<std::size_t>(roadId)];
    RoadSideFrontage* side = road.sideBank(bankIndex);

    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDir{};
    if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
        return;
    }

    float usedStart = 1e9f;
    float usedEnd   = -1e9f;
    for (const Vec2& corner : mainFootprint.corners) {
        const float t = (corner - origin).dot(edgeDir);
        usedStart     = std::min(usedStart, t);
        usedEnd       = std::max(usedEnd, t);
    }

    carveSideWall(*side, origin, edgeDir, usedStart, usedEnd, town.wallSegmentIdCounter);
    clearAlleyGapStateForRoad(town, roadId);
    refreshBankExhaustionAfterCarve(town, roadId, bankIndex);
    if (notifyFrontier) {
        PlacementEvent event;
        event.type      = PlacementEventType::PlotCarved;
        event.roadId    = roadId;
        event.bankIndex = bankIndex;
        event.carveTMin = usedStart;
        event.carveTMax = usedEnd;
        notifyPlacementFrontier(town, event, terrain, town.syncTerrainCatalog, &town.syncTerrainProbes,
                            nullptr);
    } else {
        frontierRefreshWallBank(town, roadId, bankIndex);
        frontierRefreshAlleyBank(town, roadId, bankIndex, town.syncMinAlleyGapWidth);
    }
}

void carveRoadWallForFootprint(Town& town, int roadId, int bankIndex,
                               const BuildingFootprint& mainFootprint) {
    carveRoadWallForFootprint(town, roadId, bankIndex, mainFootprint, nullptr, false);
}

namespace {

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

}  // namespace

void buildBridgeBuckets(Town& town, int maxHops) {
    town.bridgeBuckets.clear();
    town.seedRevealBridgeRoadId = -1;

    float seedDist = 1e30f;
    for (const Road& road : town.roads) {
        if (!road.isBridge) {
            continue;
        }

        BridgeBucket bucket;
        bucket.bridgeRoadId = road.id;
        collectBridgeBucketRoads(town, road.id, maxHops, bucket.roadIds);
        town.bridgeBuckets.push_back(std::move(bucket));

        const Vec2 mid = (road.a + road.b) * 0.5f;
        const float  d = (mid - town.center).length();
        if (d < seedDist) {
            seedDist                    = d;
            town.seedRevealBridgeRoadId = road.id;
        }
    }

    for (BridgeBucket& bucket : town.bridgeBuckets) {
        if (bucket.bridgeRoadId == town.seedRevealBridgeRoadId) {
            bucket.revealed = true;
        }
    }

    Logger::log("bridge",
                "bridge_buckets count=" + std::to_string(town.bridgeBuckets.size()) + " hops="
                    + std::to_string(maxHops) + " seed_b="
                    + std::to_string(town.seedRevealBridgeRoadId));
}

void updateBridgeRevealFromBuildings(Town& town) {
    for (BridgeBucket& bucket : town.bridgeBuckets) {
        if (bucket.revealed) {
            continue;
        }
        for (const BuildingInstance& instance : town.buildingInstances) {
            const int roadId = buildingInstanceRoadId(instance);
            if (roadId >= 0 && bucket.roadIds.count(roadId) != 0) {
                bucket.revealed = true;
                Logger::log("bridge",
                            "bridge_revealed b=" + std::to_string(bucket.bridgeRoadId)
                                + " trigger_road=" + std::to_string(roadId));
                break;
            }
        }
    }
}

bool isBridgeRoadRevealed(const Town& town, int bridgeRoadId) {
    for (const BridgeBucket& bucket : town.bridgeBuckets) {
        if (bucket.bridgeRoadId == bridgeRoadId) {
            return bucket.revealed;
        }
    }
    return true;
}

void rebuildRoadMesh(Town& town, const std::array<uint8_t, 3>& primaryColor,
                     const std::array<uint8_t, 3>& secondaryColor,
                     const std::array<uint8_t, 3>& bridgeColor, float pixelsPerUnit,
                     const TerrainAtlas* terrain, bool clipRoadsAtWater) {
    const sf::Color primarySf(primaryColor[0], primaryColor[1], primaryColor[2]);
    const sf::Color secondarySf(secondaryColor[0], secondaryColor[1], secondaryColor[2]);
    const sf::Color bridgeSf(bridgeColor[0], bridgeColor[1], bridgeColor[2]);
    const float     thickness = 1.f * pixelsPerUnit;

    updateBridgeRevealFromBuildings(town);

    town.roadMesh.setPrimitiveType(sf::Triangles);
    town.roadMesh.clear();
    town.roadLabels.clear();

    const bool clip = terrain != nullptr && terrain->valid && clipRoadsAtWater;
    for (const Road& road : town.roads) {
        if (road.isBridge && !isBridgeRoadRevealed(town, road.id)) {
            continue;
        }

        const Vec2 mid = (road.a + road.b) * 0.5f;
        town.roadLabels.push_back(
            {road.id, mid.x * pixelsPerUnit, mid.y * pixelsPerUnit});

        const sf::Color& color =
            road.isBridge ? bridgeSf : (road.isSecondary ? secondarySf : primarySf);

        if (road.isBridge || !clip) {
            appendThickSegment(town.roadMesh, {road.a.x * pixelsPerUnit, road.a.y * pixelsPerUnit},
                               {road.b.x * pixelsPerUnit, road.b.y * pixelsPerUnit}, thickness,
                               color);
            continue;
        }

        const std::vector<std::pair<float, float>> intervals =
            clipRoadSegmentToLand(road.a, road.b, *terrain);
        const Vec2 delta = road.b - road.a;
        for (const auto& interval : intervals) {
            const Vec2 landA = road.a + delta * interval.first;
            const Vec2 landB = road.a + delta * interval.second;
            appendThickSegment(town.roadMesh,
                               {landA.x * pixelsPerUnit, landA.y * pixelsPerUnit},
                               {landB.x * pixelsPerUnit, landB.y * pixelsPerUnit}, thickness,
                               color);
        }
    }

    buildBridgeDebugView(town, pixelsPerUnit, bridgeColor);
}

void buildBridgeDebugView(Town& town, float pixelsPerUnit,
                          const std::array<uint8_t, 3>& bridgeColor) {
    const sf::Color bridgeSf(bridgeColor[0], bridgeColor[1], bridgeColor[2]);
    const sf::Color kBridgeCandidatePurple(128, 0, 200);
    const sf::Color kProbeCircle(255, 255, 255, 180);
    const sf::Color kMissJunction(255, 64, 64);
    const sf::Color kHitMark(255, 0, 0);
    const float     bridgeThickness = 2.5f * pixelsPerUnit;
    const float     junctionRadius  = units::toPixels(1.25f, pixelsPerUnit);
    const float     probeRadiusPx =
        units::toPixels(town.bridgeWatersideProbeRadius, pixelsPerUnit);
    const float ringThickness = std::max(1.5f, pixelsPerUnit * 0.15f);
    const float hitArmPx      = std::max(4.f, pixelsPerUnit * 0.45f);

    town.bridgeRoadMesh.setPrimitiveType(sf::Triangles);
    town.bridgeRoadMesh.clear();
    town.bridgeProbeCircleMesh.setPrimitiveType(sf::Triangles);
    town.bridgeProbeCircleMesh.clear();
    town.bridgeProbeHitMesh.setPrimitiveType(sf::Triangles);
    town.bridgeProbeHitMesh.clear();
    town.bridgeCandidateJunctionMesh.setPrimitiveType(sf::Triangles);
    town.bridgeCandidateJunctionMesh.clear();
    town.bridgeDebugLabels.clear();

    for (const Road& road : town.roads) {
        if (!road.isBridge) {
            continue;
        }
        if (!isBridgeRoadRevealed(town, road.id)) {
            continue;
        }

        const Vec2 mid = (road.a + road.b) * 0.5f;
        appendThickSegment(town.bridgeRoadMesh,
                           {road.a.x * pixelsPerUnit, road.a.y * pixelsPerUnit},
                           {road.b.x * pixelsPerUnit, road.b.y * pixelsPerUnit}, bridgeThickness,
                           bridgeSf);

        RotatedTextLabel label;
        label.text        = "B" + std::to_string(road.id);
        label.centerXPx   = mid.x * pixelsPerUnit;
        label.centerYPx   = mid.y * pixelsPerUnit;
        label.rotationDeg = 0.f;
        town.bridgeDebugLabels.push_back(label);
    }

    const auto drawProbe = [&](const WatersideProbeDebug& probe) {
        const sf::Vector2f center{units::toPixels(probe.pos.x, pixelsPerUnit),
                                  units::toPixels(probe.pos.y, pixelsPerUnit)};
        if (probe.probeRadius > 0.f) {
            appendCircleRing(town.bridgeProbeCircleMesh, center, probeRadiusPx, ringThickness,
                             kProbeCircle);
        }
        if (probe.hitValid) {
            const sf::Vector2f hitCenter{units::toPixels(probe.hitPoint.x, pixelsPerUnit),
                                         units::toPixels(probe.hitPoint.y, pixelsPerUnit)};
            appendHitCross(town.bridgeProbeHitMesh, hitCenter, hitArmPx, ringThickness, kHitMark);
        }

        appendJunctionDisc(town.bridgeCandidateJunctionMesh, center, junctionRadius,
                           probe.isWaterside ? kBridgeCandidatePurple : kMissJunction);

        std::string text = "J" + std::to_string(probe.junctionId);
        if (probe.junctionId >= 0
            && probe.junctionId < static_cast<int>(town.junctions.size())) {
            const Junction& junction =
                town.junctions[static_cast<std::size_t>(probe.junctionId)];
            if (!junction.roadIds.empty()) {
                text += " R";
                for (std::size_t i = 0; i < junction.roadIds.size(); ++i) {
                    if (i > 0) {
                        text += ',';
                    }
                    text += std::to_string(junction.roadIds[i]);
                }
            }
        }

        RotatedTextLabel label;
        label.text        = text;
        label.centerXPx   = center.x;
        label.centerYPx   = center.y - 16.f;
        label.rotationDeg = 0.f;
        town.bridgeDebugLabels.push_back(label);
    };

    if (!town.watersideProbeDebug.empty()) {
        for (const WatersideProbeDebug& probe : town.watersideProbeDebug) {
            drawProbe(probe);
        }
    } else {
        for (const Junction& junction : town.junctions) {
            WatersideProbeDebug probe{};
            probe.junctionId  = junction.id;
            probe.pos         = junction.pos;
            probe.probeRadius = town.bridgeWatersideProbeRadius;
            probe.isWaterside = town.watersideJunctionIds.count(junction.id) != 0;
            drawProbe(probe);
        }
    }
}
