// Road/junction/bridge mesh construction and the bridge debug overlay. Part of
// the Town subsystem split (data model in Town.h, shared helpers in Town.cpp /
// TownInternal.h).

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
#include <string>
#include <utility>
#include <vector>

using namespace townint;

namespace {

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

}  // namespace

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

void buildRoadEndProbeMesh(Town& town, float /*pixelsPerUnit*/, float /*probeLengthUnits*/) {
    town.roadEndProbeMesh.setPrimitiveType(sf::Triangles);
    town.roadEndProbeMesh.clear();
    town.roadEndProbeLabels.clear();
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
