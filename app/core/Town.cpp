#include "Town.h"

#include "RoadExhaustion.h"
#include "PlotGeometry.h"
#include "FrontageZones.h"
#include "TerrainAtlas.h"
#include "Units.h"

#include <algorithm>
#include <cmath>

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

float segmentCenterDist(const Vec2& origin, const Vec2& edgeDir, float startT, float endT,
                        const Vec2& center) {
    return (origin + edgeDir * ((startT + endT) * 0.5f) - center).length();
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

void recomputeSegmentDist(RoadFrontageSegment& segment, const Vec2& origin, const Vec2& edgeDir,
                          const Vec2& center) {
    segment.centerDist = segmentCenterDist(origin, edgeDir, segment.startT, segment.endT, center);
}

void carveSideFrontage(RoadSideFrontage& side, const Vec2& origin, const Vec2& edgeDir,
                       float usedStart, float usedEnd, const Vec2& center, int& nextSegmentId) {
    constexpr float kEps = 0.08f;
    std::vector<RoadFrontageSegment> remaining;
    remaining.reserve(side.segments.size() + 1);

    for (const RoadFrontageSegment& segment : side.segments) {
        if (usedEnd <= segment.startT + kEps || usedStart >= segment.endT - kEps) {
            remaining.push_back(segment);
            continue;
        }
        if (usedStart > segment.startT + kEps) {
            RoadFrontageSegment left{nextSegmentId++, segment.startT, usedStart, 0.f};
            recomputeSegmentDist(left, origin, edgeDir, center);
            if (left.width() > kEps) {
                remaining.push_back(left);
            }
        }
        if (usedEnd < segment.endT - kEps) {
            RoadFrontageSegment right{nextSegmentId++, usedEnd, segment.endT, 0.f};
            recomputeSegmentDist(right, origin, edgeDir, center);
            if (right.width() > kEps) {
                remaining.push_back(right);
            }
        }
    }

    side.segments = std::move(remaining);
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

    for (const Junction& junction : town.junctions) {
        const sf::Vector2f center{units::toPixels(junction.pos.x, pixelsPerUnit),
                                  units::toPixels(junction.pos.y, pixelsPerUnit)};
        appendJunctionDisc(town.junctionMesh, center, radiusPx, sf::Color(255, 0, 0));
    }
}

void assignRoadSideInwards(Town& town, const TerrainAtlas* terrain) {
    constexpr float kBankProbeDist = 2.f;
    for (Road& road : town.roads) {
        road.sideA = RoadSideFrontage{};
        road.sideB = RoadSideFrontage{};
        if (road.isBridge) {
            continue;
        }

        const Vec2 dir = (road.b - road.a).normalized();
        if (dir.length() < 1e-4f) {
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
        segment.centerDist = segmentCenterDist(origin, edgeDir, segment.startT, segment.endT,
                                               town.center);
        side->segments.push_back(segment);
    }
}

void resetRoadFrontageSegments(Town& town, float frontageSetback) {
    town.frontageSegmentIdCounter = 0;

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
            segment.centerDist = segmentCenterDist(origin, edgeDir, segment.startT, segment.endT,
                                                   town.center);
            side->segments.push_back(segment);
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

void splitSideFrontageAtT(RoadSideFrontage& headSide, RoadSideFrontage& tailSide, float splitT,
                            const Vec2& headOrigin, const Vec2& headEdgeDir,
                            const Vec2& tailOrigin, const Vec2& tailEdgeDir, const Vec2& center,
                            int& nextSegmentId) {
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
            recomputeSegmentDist(tailSeg, tailOrigin, tailEdgeDir, center);
            if (tailSeg.width() > kEps) {
                tailRemaining.push_back(tailSeg);
            }
            continue;
        }
        RoadFrontageSegment headSeg{segment.id, segment.startT, splitT, 0.f};
        recomputeSegmentDist(headSeg, headOrigin, headEdgeDir, center);
        if (headSeg.width() > kEps) {
            headRemaining.push_back(headSeg);
        }
        RoadFrontageSegment tailSeg{nextSegmentId++, 0.f, segment.endT - splitT, 0.f};
        recomputeSegmentDist(tailSeg, tailOrigin, tailEdgeDir, center);
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
                                 tailEdgeDir, town.center, town.frontageSegmentIdCounter);
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

}  // namespace

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
        }
    }
    splitRoadsAtAlleyEndpoints(town);
    indexJunctions(town);
    invalidateRoadTopologyCaches(town);
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

void buildRoadEndProbeMesh(Town& town, float /*pixelsPerUnit*/, float /*probeLengthUnits*/) {
    town.roadEndProbeMesh.setPrimitiveType(sf::Triangles);
    town.roadEndProbeMesh.clear();
    town.roadEndProbeLabels.clear();
}

void carveRoadFrontageForPlot(Town& town, const Plot& plot, float /*frontageSetback*/) {
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

    carveSideFrontage(*side, origin, edgeDir, plotTMinLocal(plot, origin, edgeDir),
                      plotTMaxLocal(plot, origin, edgeDir), town.center,
                      town.frontageSegmentIdCounter);
    clearAlleyGapStateForRoad(town, plot.roadId);
    refreshBankExhaustionAfterCarve(town, plot.roadId, plot.roadBank);
    invalidateWallSpanCacheForBank(town, plot.roadId, plot.roadBank);
}

void carveRoadFrontageForFootprint(Town& town, int roadId, int bankIndex,
                                   const BuildingFootprint& mainFootprint) {
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

    carveSideFrontage(*side, origin, edgeDir, usedStart, usedEnd, town.center,
                      town.frontageSegmentIdCounter);
    clearAlleyGapStateForRoad(town, roadId);
    refreshBankExhaustionAfterCarve(town, roadId, bankIndex);
    invalidateWallSpanCacheForBank(town, roadId, bankIndex);
}

void rebuildRoadMesh(Town& town, const std::array<uint8_t, 3>& primaryColor,
                     const std::array<uint8_t, 3>& secondaryColor,
                     const std::array<uint8_t, 3>& bridgeColor, float pixelsPerUnit,
                     const TerrainAtlas* terrain, bool clipRoadsAtWater) {
    const sf::Color primarySf(primaryColor[0], primaryColor[1], primaryColor[2]);
    const sf::Color secondarySf(secondaryColor[0], secondaryColor[1], secondaryColor[2]);
    const sf::Color bridgeSf(bridgeColor[0], bridgeColor[1], bridgeColor[2]);
    const float     thickness = 1.f * pixelsPerUnit;

    town.roadMesh.setPrimitiveType(sf::Triangles);
    town.roadMesh.clear();

    const bool clip = terrain != nullptr && terrain->valid && clipRoadsAtWater;
    for (const Road& road : town.roads) {
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
}
