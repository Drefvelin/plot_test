#include "placement/orchestration/BuildingPlacer.h"

#include "common/RenderPrimitives.h"
#include "placement/orchestration/BuildingGrowthQueue.h"
#include "placement/geometry/BuildingLayout.h"
#include "placement/frontage/FrontageGapFill.h"
#include "placement/frontage/FrontagePlacement.h"
#include "placement/zones/FrontageZones.h"
#include "placement/orchestration/GrowthRings.h"
#include "util/Logger.h"
#include "placement/orchestration/MovableRelocation.h"
#include "placement/logging/PlacementLogging.h"
#include "placement/frontier/PlacementFrontier.h"
#include "placement/frontier/FrontierManager.h"
#include "placement/geometry/PlotGeometry.h"
#include "placement/orchestration/PlacementPrep.h"
#include "util/Profile.h"
#include "roads/RoadExhaustion.h"
#include "roads/SecondaryRoadPlacement.h"
#include "terrain/TerrainAtlas.h"
#include "terrain/Terrain.h"
#include "placement/terrain/TerrainPlacement.h"
#include "common/Units.h"

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

const char* terrainPlacementModeName(TerrainPlacementMode mode) {
    switch (mode) {
    case TerrainPlacementMode::Inside:
        return "inside";
    case TerrainPlacementMode::Proximity:
        return "prox";
    case TerrainPlacementMode::Border:
        return "border";
    default:
        return "?";
    }
}

Vec2 footprintCenter(const BuildingFootprint& footprint) {
    Vec2 center{};
    for (const Vec2& corner : footprint.corners) {
        center = center + corner;
    }
    return center * 0.25f;
}

Vec2 terrainLabelPositionWorld(const BuildingFootprint& footprint) {
    const Vec2 center = footprintCenter(footprint);
    if (footprint.doorEdge < 0 || footprint.doorEdge >= 4) {
        return center;
    }
    const int   i        = footprint.doorEdge;
    const Vec2  doorMid  = (footprint.corners[i] + footprint.corners[(i + 1) % 4]) * 0.5f;
    const Vec2  toCenter = center - doorMid;
    const float len      = toCenter.length();
    if (len < 1e-4f) {
        return center;
    }
    return doorMid + toCenter.normalized() * (len * 0.45f);
}

float doorParallelRotationDeg(const BuildingFootprint& footprint) {
    if (footprint.doorEdge < 0 || footprint.doorEdge >= 4) {
        return 0.f;
    }
    const int  i   = footprint.doorEdge;
    const Vec2 dir = (footprint.corners[(i + 1) % 4] - footprint.corners[i]).normalized();
    float      deg = std::atan2(dir.y, dir.x) * 180.f / 3.14159265f;
    if (deg > 90.f) {
        deg -= 180.f;
    } else if (deg < -90.f) {
        deg += 180.f;
    }
    return deg;
}

void appendPlotOutline(sf::VertexArray& mesh, const Plot& plot, float ppu, float outlineUnits,
                       const sf::Color& color) {
    const float thicknessPx = units::toPixels(outlineUnits, ppu);
    Vec2 pxCorners[4];
    for (int i = 0; i < 4; ++i) {
        pxCorners[i] = {units::toPixels(plot.corners[i].x, ppu),
                        units::toPixels(plot.corners[i].y, ppu)};
    }
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        appendThickSegment(mesh, pxCorners[i], pxCorners[j], thicknessPx, color);
    }
}

void appendFootprintOutline(sf::VertexArray& mesh, const BuildingFootprint& footprint, float ppu,
                            float outlineUnits, const sf::Color& color) {
    const float thicknessPx = units::toPixels(outlineUnits, ppu);
    Vec2        pxCorners[4];
    for (int i = 0; i < 4; ++i) {
        pxCorners[i] = {units::toPixels(footprint.corners[i].x, ppu),
                        units::toPixels(footprint.corners[i].y, ppu)};
    }
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        appendThickSegment(mesh, pxCorners[i], pxCorners[j], thicknessPx, color);
    }
}

void appendTriangle(sf::VertexArray& tris, const Vec2& p0, const Vec2& p1, const Vec2& p2,
                    const sf::Color& color) {
    tris.append({{p0.x, p0.y}, color});
    tris.append({{p1.x, p1.y}, color});
    tris.append({{p2.x, p2.y}, color});
}

void appendArrow(sf::VertexArray& mesh, const Vec2& tailPx, const Vec2& headPx, float shaftThicknessPx,
                 float headLengthPx, const sf::Color& color) {
    const Vec2 delta = headPx - tailPx;
    const float len  = delta.length();
    if (len < 1e-3f) {
        return;
    }

    const Vec2 dir = delta * (1.f / len);
    const Vec2 basePx = headPx - dir * headLengthPx;
    appendThickSegment(mesh, tailPx, basePx, shaftThicknessPx, color);

    const Vec2 wingOffset = perpendicular(dir) * (headLengthPx * 0.55f);
    appendTriangle(mesh, headPx, basePx + wingOffset, basePx - wingOffset, color);
}

void appendDottedLine(sf::VertexArray& mesh, const Vec2& aPx, const Vec2& bPx, float dashLengthPx,
                        float gapLengthPx, float thicknessPx, const sf::Color& color) {
    const Vec2  delta = bPx - aPx;
    const float len   = delta.length();
    if (len < 1e-3f) {
        return;
    }
    const Vec2 dir = delta * (1.f / len);

    float traveled = 0.f;
    while (traveled < len) {
        const float dashEnd = std::min(traveled + dashLengthPx, len);
        appendThickSegment(mesh, aPx + dir * traveled, aPx + dir * dashEnd, thicknessPx, color);
        traveled = dashEnd + gapLengthPx;
    }
}

void rebuildAlleyProbeMesh(Town& town, int queueIndex, float pixelsPerUnit) {
    town.alleyProbeFailMesh.clear();
    town.alleyProbeFailMesh.setPrimitiveType(sf::Triangles);

    if (queueIndex < 0 || queueIndex >= static_cast<int>(town.alleyProbesByQueueIndex.size())) {
        return;
    }

    constexpr float kLineUnits = 0.55f;
    const float     thicknessPx = units::toPixels(kLineUnits, pixelsPerUnit);
    const sf::Color orange(255, 140, 0);
    const sf::Color green(0, 200, 0);

    const std::vector<AlleyProbeLine>& probes =
        town.alleyProbesByQueueIndex[static_cast<std::size_t>(queueIndex)];

    for (const AlleyProbeLine& line : probes) {
        if (line.valid) {
            continue;
        }
        const Vec2 aPx{units::toPixels(line.a.x, pixelsPerUnit),
                       units::toPixels(line.a.y, pixelsPerUnit)};
        const Vec2 bPx{units::toPixels(line.b.x, pixelsPerUnit),
                       units::toPixels(line.b.y, pixelsPerUnit)};
        appendThickSegment(town.alleyProbeFailMesh, aPx, bPx, thicknessPx, orange);
    }

    for (const AlleyProbeLine& line : probes) {
        if (!line.valid) {
            continue;
        }
        const Vec2 aPx{units::toPixels(line.a.x, pixelsPerUnit),
                       units::toPixels(line.a.y, pixelsPerUnit)};
        const Vec2 bPx{units::toPixels(line.b.x, pixelsPerUnit),
                       units::toPixels(line.b.y, pixelsPerUnit)};
        appendThickSegment(town.alleyProbeFailMesh, aPx, bPx, thicknessPx, green);
    }
}

void rebuildFrontageSegmentMesh(Town& town, const PlotConfig& plots, float pixelsPerUnit,
                                const TerrainAtlas* terrain) {
    town.frontageSegmentMesh.clear();
    town.frontageSegmentMesh.setPrimitiveType(sf::Triangles);
    town.frontageInwardArrowMesh.clear();
    town.frontageInwardArrowMesh.setPrimitiveType(sf::Triangles);
    town.frontageSegmentLabels.clear();

    constexpr float kDashUnits      = 3.5f;
    constexpr float kGapUnits       = 2.25f;
    constexpr float kLineUnits      = 1.75f;
    constexpr float kArrowLenUnits  = 5.0f;
    constexpr float kArrowHeadUnits = 1.8f;
    const float     dashPx          = units::toPixels(kDashUnits, pixelsPerUnit);
    const float     gapPx           = units::toPixels(kGapUnits, pixelsPerUnit);
    const float     thicknessPx     = units::toPixels(kLineUnits, pixelsPerUnit);
    const float     arrowShaftPx    = units::toPixels(0.9f, pixelsPerUnit);
    const float     arrowHeadPx     = units::toPixels(kArrowHeadUnits, pixelsPerUnit);
    const sf::Color plotColor(0, 160, 0);
    const sf::Color wallColor(220, 200, 0);
    const sf::Color alleyColor(255, 140, 0);
    const float     minPlotWidth    = town.syncMinPlotFrontage > 0.f
                                          ? town.syncMinPlotFrontage
                                          : std::max(0.5f, plots.frontageSetback);
    const float     minWallWidth    = town.syncMinGapWidth > 0.f
                                          ? town.syncMinGapWidth
                                          : std::max(0.5f, plots.frontageSetback * 2.f);

    struct SegmentKey {
        int roadId    = -1;
        int bankIndex = 0;
        int segmentId = -1;

        bool operator==(const SegmentKey& other) const {
            return roadId == other.roadId && bankIndex == other.bankIndex
                   && segmentId == other.segmentId;
        }
    };

    struct SegmentKeyHash {
        std::size_t operator()(const SegmentKey& key) const {
            return static_cast<std::size_t>(key.roadId * 73856093 ^ key.bankIndex * 19349663
                                            ^ key.segmentId);
        }
    };

    std::unordered_set<SegmentKey, SegmentKeyHash> alleyFrontierSegments;
    for (const AlleyFrontierRef& ref : town.frontierManager.alley) {
        alleyFrontierSegments.insert({ref.roadId, ref.bankIndex, ref.segmentId});
    }

    const auto appendOverlay = [&](const Road& road, int bankIndex,
                                   const RoadFrontageSegment& segment, const sf::Color& color) {
        const RoadSideFrontage* side = road.sideBank(bankIndex);
        if (side == nullptr || side->inward.length() < 1e-4f) {
            return;
        }
        Vec2 origin{};
        Vec2 farEnd{};
        Vec2 edgeDir{};
        if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
            return;
        }
        const Vec2& inward = side->inward;

        const Vec2 roadStart = origin + edgeDir * segment.startT;
        const Vec2 roadEnd   = origin + edgeDir * segment.endT;
        const float midT     = (segment.startT + segment.endT) * 0.5f;
        const Vec2  roadMid  = origin + edgeDir * midT;

        const Vec2 start    = roadStart + inward * plots.frontageSetback;
        const Vec2 end      = roadEnd + inward * plots.frontageSetback;
        const Vec2 labelMid = (start + end) * 0.5f;
        const Vec2 startPx{units::toPixels(start.x, pixelsPerUnit),
                           units::toPixels(start.y, pixelsPerUnit)};
        const Vec2 endPx{units::toPixels(end.x, pixelsPerUnit),
                         units::toPixels(end.y, pixelsPerUnit)};
        const Vec2 labelMidPx{units::toPixels(labelMid.x, pixelsPerUnit),
                              units::toPixels(labelMid.y, pixelsPerUnit)};
        const Vec2 arrowTailPx{units::toPixels(roadMid.x, pixelsPerUnit),
                               units::toPixels(roadMid.y, pixelsPerUnit)};
        const Vec2 arrowHeadPosPx{
            units::toPixels((roadMid + inward * kArrowLenUnits).x, pixelsPerUnit),
            units::toPixels((roadMid + inward * kArrowLenUnits).y, pixelsPerUnit)};

        appendDottedLine(town.frontageSegmentMesh, startPx, endPx, dashPx, gapPx, thicknessPx,
                         color);
        appendArrow(town.frontageInwardArrowMesh, arrowTailPx, arrowHeadPosPx, arrowShaftPx,
                    arrowHeadPx, color);
        town.frontageSegmentLabels.push_back({segment.id, labelMidPx.x, labelMidPx.y});
    };

    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            const RoadSideFrontage* side = road.sideBank(bankIndex);
            if (side == nullptr || side->inward.length() < 1e-4f) {
                continue;
            }

            for (const RoadFrontageSegment& segment : side->segments) {
                if (segment.width() + 1e-3f < minPlotWidth) {
                    continue;
                }
                appendOverlay(road, bankIndex, segment, plotColor);
            }

            for (const RoadFrontageSegment& segment : side->wallSegments) {
                if (segment.width() + 1e-3f < minWallWidth) {
                    continue;
                }
                const SegmentKey key{road.id, bankIndex, segment.id};
                const sf::Color& color =
                    alleyFrontierSegments.count(key) != 0 ? alleyColor : wallColor;
                appendOverlay(road, bankIndex, segment, color);
            }
        }
    }

    if (terrain != nullptr && terrain->valid && town.syncTerrainCatalog != nullptr) {
        const sf::Color borderColor(20, 50, 180);
        const float     outlineStep = std::max(0.5f, town.syncBorderSampleStep * 0.5f);
        for (TerrainId id : town.syncTerrainProbes.borderIds) {
            const std::vector<std::vector<Vec2>>* graphs = terrain->outlineGraphs(id);
            if (graphs == nullptr) {
                continue;
            }
            for (int graphIndex = 0; graphIndex < static_cast<int>(graphs->size()); ++graphIndex) {
                const std::vector<Vec2>& graph =
                    (*graphs)[static_cast<std::size_t>(graphIndex)];
                const float totalLen = polylineGraphLength(graph);
                if (totalLen < outlineStep) {
                    continue;
                }
                Vec2 prevPx;
                bool hasPrev = false;
                for (float arc = 0.f; arc <= totalLen + 1e-3f; arc += outlineStep) {
                    Vec2 pt;
                    Vec2 tangent;
                    if (!samplePolylineGraphAtArc(graph, arc, pt, tangent)) {
                        continue;
                    }
                    const Vec2 px{units::toPixels(pt.x, pixelsPerUnit),
                                  units::toPixels(pt.y, pixelsPerUnit)};
                    if (hasPrev) {
                        appendDottedLine(town.frontageSegmentMesh, prevPx, px, dashPx, gapPx,
                                         thicknessPx, borderColor);
                    }
                    prevPx  = px;
                    hasPrev = true;
                }
            }
        }
    }
}

void rebuildOutlineMesh(Town& town, const DefCache& defs, float pixelsPerUnit, int townSeed) {
    for (BuildingInstance& instance : town.buildingInstances) {
        if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
            if (!instance.footprints.empty()) {
                continue;
            }
        } else if (!instance.footprints.empty()) {
            continue;
        }
        const std::vector<ResolvedBuildingSpec> specs =
            resolveBuildingSpecs(defs, defs.typeName(instance.typeId), instance.id, townSeed);
        layoutBuildingsOnPlot(instance.plot, town, specs, instance.id, townSeed, instance.footprints);
    }

    town.buildingOutlineMesh.clear();
    town.buildingOutlineMesh.setPrimitiveType(sf::Triangles);
    town.terrainBuildingOutlineMesh.clear();
    town.terrainBuildingOutlineMesh.setPrimitiveType(sf::Triangles);
    town.plotLabels.clear();
    town.buildingLabels.clear();
    town.terrainPlotTypeLabels.clear();

    constexpr float kOutlineUnits = 0.35f;
    const auto appendInstancePlotOutlines = [&](sf::VertexArray& mesh, const BuildingInstance& instance,
                                                const sf::Color& color) {
        if (instance.placementMode == BuildingPlacementMode::PlotLot
            || instance.placementMode == BuildingPlacementMode::BorderPlot) {
            appendPlotOutline(mesh, instance.plot, pixelsPerUnit, kOutlineUnits, color);
        } else if (instance.placementMode == BuildingPlacementMode::SegmentGapFill
                   || instance.placementMode == BuildingPlacementMode::BorderBuilding) {
            constexpr float kGapFillOutlineUnits = 0.55f;
            for (const BuildingFootprint& footprint : instance.footprints) {
                appendFootprintOutline(mesh, footprint, pixelsPerUnit, kGapFillOutlineUnits, color);
            }
        }
    };

    for (const BuildingInstance& instance : town.buildingInstances) {
        const BuildingDef* def = defs.building(instance.typeId);
        const sf::Color color =
            def ? sf::Color(def->rgb[0], def->rgb[1], def->rgb[2]) : sf::Color::White;

        appendInstancePlotOutlines(town.buildingOutlineMesh, instance, color);
        if (buildingHasTerrainPlacement(defs, instance.typeId)) {
            appendInstancePlotOutlines(town.terrainBuildingOutlineMesh, instance, color);
        }

        if (instance.placementMode == BuildingPlacementMode::PlotLot
            || instance.placementMode == BuildingPlacementMode::BorderPlot) {
            const Vec2 plotMid = plotCenter(instance.plot);
            town.plotLabels.push_back(
                {static_cast<int>(instance.id), units::toPixels(plotMid.x, pixelsPerUnit),
                 units::toPixels(plotMid.y, pixelsPerUnit)});
        } else if (instance.placementMode == BuildingPlacementMode::SegmentGapFill
                   || instance.placementMode == BuildingPlacementMode::BorderBuilding) {
            if (!instance.footprints.empty()) {
                Vec2 fpMid{};
                for (const Vec2& corner : instance.footprints[0].corners) {
                    fpMid = fpMid + corner;
                }
                fpMid = fpMid * 0.25f;
                town.plotLabels.push_back(
                    {static_cast<int>(instance.id), units::toPixels(fpMid.x, pixelsPerUnit),
                     units::toPixels(fpMid.y, pixelsPerUnit)});
            }
        }

        for (const BuildingFootprint& footprint : instance.footprints) {
            Vec2 fpMid{};
            for (const Vec2& corner : footprint.corners) {
                fpMid = fpMid + corner;
            }
            fpMid = fpMid * 0.25f;
            const int labelId = footprint.mainBuilding
                                    ? static_cast<int>(instance.id)
                                    : (footprint.labelId >= 0 ? footprint.labelId : 0);
            town.buildingLabels.push_back(
                {labelId, units::toPixels(fpMid.x, pixelsPerUnit),
                 units::toPixels(fpMid.y, pixelsPerUnit)});
        }
    }

    refreshBuildingDoorEdges(town, defs);

    for (const BuildingInstance& instance : town.buildingInstances) {
        const BuildingDef* def = defs.building(instance.typeId);
        if (def == nullptr || def->terrain.placement == TerrainPlacementMode::None) {
            continue;
        }

        const BuildingFootprint* mainFootprint = nullptr;
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (footprint.mainBuilding) {
                mainFootprint = &footprint;
                break;
            }
        }
        if (mainFootprint == nullptr || mainFootprint->doorEdge < 0) {
            continue;
        }

        const Vec2 labelPos = terrainLabelPositionWorld(*mainFootprint);
        std::string preferLabel;
        if (def->terrain.preferKinds.size() > 1) {
            for (std::size_t ki = 0; ki < def->terrain.preferKinds.size(); ++ki) {
                if (ki > 0) {
                    preferLabel += '+';
                }
                preferLabel += std::to_string(def->terrain.preferKinds[ki]);
            }
        } else {
            preferLabel = std::to_string(def->terrain.prefer);
        }
        std::string labelText = defs.typeName(instance.typeId) + '\n'
                                + terrainPlacementModeName(def->terrain.placement) + '/'
                                + preferLabel;
        town.terrainPlotTypeLabels.push_back(
            {std::move(labelText), units::toPixels(labelPos.x, pixelsPerUnit),
             units::toPixels(labelPos.y, pixelsPerUnit),
             doorParallelRotationDeg(*mainFootprint)});
    }

    appendBuildingFootprintOutlines(town.buildingOutlineMesh, town, pixelsPerUnit);
    appendBuildingFootprintOutlines(
        town.terrainBuildingOutlineMesh, town, pixelsPerUnit,
        [&](const BuildingInstance& inst) {
            return buildingHasTerrainPlacement(defs, inst.typeId);
        });
}

bool instanceOnAlleyRoad(const BuildingInstance& instance, int roadId) {
    if (roadId < 0) {
        return false;
    }
    if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
        return instance.roadId == roadId;
    }
    if (instance.placementMode == BuildingPlacementMode::PlotLot
        || instance.placementMode == BuildingPlacementMode::BorderPlot) {
        return instance.plot.roadId == roadId;
    }
    if (instance.placementMode == BuildingPlacementMode::BorderBuilding) {
        return instance.roadId == roadId;
    }
    return false;
}

const char* placementModeLabel(BuildingPlacementMode mode) {
    switch (mode) {
    case BuildingPlacementMode::SegmentGapFill:
        return "gap_fill";
    case BuildingPlacementMode::PlotLot:
        return "plot_lot";
    case BuildingPlacementMode::BorderPlot:
        return "border_plot";
    case BuildingPlacementMode::BorderBuilding:
        return "border_building";
    default:
        return "unknown";
    }
}

void trimPlacementState(Town& town, int targetCount) {
    while (!town.buildingInstances.empty()
           && town.buildingInstances.back().id >= targetCount) {
        town.buildingInstances.pop_back();
    }
    if (town.placementQueueCursor > targetCount) {
        town.placementQueueCursor = targetCount;
    }
    auto& failed = town.placementFailedIndices;
    failed.erase(std::remove_if(failed.begin(), failed.end(),
                                [targetCount](int idx) { return idx >= targetCount; }),
                 failed.end());
    town.placementFailureCount = static_cast<int>(failed.size());
}

std::string formatSkipReasonCounts(const std::unordered_map<std::string, int>& counts) {
    std::vector<std::pair<std::string, int>> entries(counts.begin(), counts.end());
    std::sort(entries.begin(), entries.end(),
              [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                  return a.first < b.first;
              });
    std::string out;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        out += entries[i].first + ':' + std::to_string(entries[i].second);
    }
    return out;
}

}  // namespace

void BuildingPlacer::sync(Town& town, const BuildingGrowthQueue& queue, const DefCache& defs,
                          const PlotConfig& plots, const TownConfig& townCfg, const Config& config,
                          const PlacementFloors& floors, float pixelsPerUnit, int townSeed,
                          const TerrainAtlas* terrain) {
    PROFILE_SCOPE(ProfileScopeId::PlacerSync);
    setVerbosePlacementLogs(config.growth.verbosePlacementLogs);
    const int targetCount = queue.activeCount();

    trimPlacementState(town, targetCount);

    ensurePlacementSyncMins(town, floors, townCfg, plots.frontageSetback);
    town.syncTerrainAtlas  = (terrain != nullptr && terrain->valid) ? terrain : nullptr;
    town.syncPixelsPerUnit = pixelsPerUnit;
    const TerrainCatalog* catalog =
        terrain != nullptr && terrain->catalog != nullptr ? terrain->catalog : town.syncTerrainCatalog;
    ensureTownFrontageInitialized(town, plots.frontageSetback, floors, townCfg, terrain, &plots,
                                  catalog, &defs.terrainProbes());

    const std::size_t secondaryBefore = town.secondaryRoadRecords.size();
    bool              secondaryTrimmed = false;
    {
        PROFILE_SCOPE(ProfileScopeId::SecondaryRebuild);
        trimSecondaryRoadRecords(town, targetCount);
        secondaryTrimmed = town.secondaryRoadRecords.size() < secondaryBefore;
        if (secondaryTrimmed) {
            town.checkedAlleyGaps.clear();
        }
        if (static_cast<int>(town.alleyProbesByQueueIndex.size()) < targetCount) {
            town.alleyProbesByQueueIndex.resize(static_cast<std::size_t>(targetCount));
            town.alleyProbesCapacity = targetCount;
        }
        const std::uint64_t secondaryFingerprint = secondaryRoadRecordsFingerprint(town);
        if (secondaryTrimmed) {
            rebuildSecondaryRoadsFromRecords(town, terrain);
        } else if (town.cachedSecondaryRecordsFingerprint == 0
                   && town.secondaryRoadRecords.empty()) {
            town.cachedSecondaryRecordsFingerprint = secondaryFingerprint;
        } else if (secondaryFingerprint != town.cachedSecondaryRecordsFingerprint) {
            rebuildSecondaryRoadsFromRecords(town, terrain);
        }
        syncPendingAlleyFills(town, targetCount);
    }

    logSegmentInventory(town);
    if (verbosePlacementLogs()) {
        logRingState(town);
    }

    const auto& queueTypes = queue.queue();
    int         ringBumps = 0;
    std::unordered_map<std::string, int> skipReasonCounts;
    std::unordered_map<int, std::string> skipReasonByIndex;

    if (town.placementQueueCursor < targetCount) {
        PROFILE_SCOPE(ProfileScopeId::GrowthLoop);
        const std::vector<int>& junctionHops = getJunctionHops(town);
        const int index = town.placementQueueCursor;
        if (index < static_cast<int>(queueTypes.size())) {
        BuildingInstance instance;
        instance.id     = static_cast<std::uint32_t>(index);
        instance.typeId   = defs.typeIdFor(queueTypes[static_cast<std::size_t>(index)]);

        PlacementSearchLog searchLog;
        bool               placed     = false;
        bool               deferIndex = false;
        std::string        skipReason = "ring_no_slot";
        const char*        zone       = zoneTypeForBuilding(defs, defs.typeName(instance.typeId));
        const bool         isRural    = zone && std::strcmp(zone, "rural") == 0;
        const bool         isAny      = zone && std::strcmp(zone, "any") == 0;
        const bool         fillIn     = isGapFillBuildingType(defs, defs.typeName(instance.typeId));

        RoadAttemptMemo roadMemo;
        roadMemo.syncContext(town);
        PlacementPrep prep{};
        {
            PROFILE_SCOPE(ProfileScopeId::PlacementPrep);
            prep = buildPlacementPrep(town, defs, defs.typeName(instance.typeId), index, townSeed,
                                        queue.maxBuildings());
        }

        Logger::log("layout",
                    "ring_attempt: queueIndex=" + std::to_string(index) + " type="
                        + defs.typeName(instance.typeId) + " suburban_max="
                        + std::to_string(town.suburbanMaxHop) + " urban_core="
                        + std::to_string(town.urbanCoreMaxHop) + " phase="
                        + ringPhaseLabel(town.ringPhase));

        if (isAny) {
            placed = tryPlaceAnyOnRoads(town, instance, defs, plots, townSeed,
                                        queue.maxBuildings(), searchLog, terrain, junctionHops,
                                        prep, roadMemo);
            if (!placed) {
                skipReason = "any_no_slot";
            }
        } else if (isRural) {
            placed = tryPlaceRuralOnRoads(town, instance, defs, plots, townSeed,
                                          queue.maxBuildings(), searchLog, terrain, junctionHops,
                                          prep, roadMemo);
            if (!placed) {
                skipReason = "rural_out_of_range";
            }
        } else {
            const int failLimit = townCfg.alleyFillFailLimit;

            if (town.placementBumpIndex != index) {
                town.placementBumpIndex = index;
                town.placementBumpCount = 0;
            }

            const auto shouldTryUrbanCore = [&]() {
                return town.ringPhase == RingPhase::DensifyCore && fillIn
                       && (hasBlockingPendingFills(town, failLimit)
                           || !isUrbanCoreSaturated(town, townCfg, defs, junctionHops));
            };

            if (fillIn) {
                placed = tryFillBlockingPendingAlleys(
                    town, instance, defs, plots, townSeed, queue.maxBuildings(), searchLog,
                    terrain, town.urbanCoreMaxHop, town.suburbanMaxHop, junctionHops, failLimit,
                    prep, roadMemo);
            }

            if (!placed && shouldTryUrbanCore()) {
                placed = tryPlaceInUrbanCore(town, instance, defs, plots, townCfg, townSeed,
                                             queue.maxBuildings(), searchLog, terrain,
                                             junctionHops, prep, roadMemo);
                if (placed && isUrbanCoreSaturated(town, townCfg, defs, junctionHops)) {
                    town.ringPhase = RingPhase::Normal;
                }
            }

            constexpr int kMaxBumpsPerIndex = 32;
            constexpr int kMaxBumpsPerSync  = 3;
            int           bumpsThisSync    = 0;

            while (!placed) {
                if (fillIn && !placed) {
                    placed = tryFillBlockingPendingAlleys(
                        town, instance, defs, plots, townSeed, queue.maxBuildings(), searchLog,
                        terrain, town.urbanCoreMaxHop, town.suburbanMaxHop, junctionHops,
                        failLimit, prep, roadMemo);
                }

                if (!placed) {
                    placed = tryPlaceSuburbanOnRoads(town, instance, defs, plots, townSeed,
                                                     queue.maxBuildings(), searchLog, terrain,
                                                     junctionHops, prep, roadMemo);
                }
                if (placed) {
                    break;
                }

                getJunctionHops(town);
                if (!hasRoadOutsideSuburbanBand(town)) {
                    Logger::log("layout",
                                "ring_band_cap: queueIndex=" + std::to_string(index)
                                    + " suburban_max=" + std::to_string(town.suburbanMaxHop)
                                    + " suburban_dist="
                                    + std::to_string(static_cast<int>(suburbanMaxDist(town)))
                                    + " all_roads_in_band no_bump");
                    break;
                }
                if (town.placementBumpCount >= kMaxBumpsPerIndex) {
                    Logger::log("layout",
                                "ring_bump_cap: queueIndex=" + std::to_string(index)
                                    + " bumps=" + std::to_string(town.placementBumpCount)
                                    + " suburban_max=" + std::to_string(town.suburbanMaxHop));
                    break;
                }

                Logger::log("layout",
                            "ring_band_exhausted: queueIndex=" + std::to_string(index)
                                + " suburban_max=" + std::to_string(town.suburbanMaxHop)
                                + " → bump");
                const float prevSuburbanDist = suburbanMaxDist(town);
                const float prevCoreDist =
                    town.urbanCoreMaxHop >= 0 ? urbanCoreMaxDist(town) : -1.f;
                bumpGrowthRings(town);
                relocateMovableBuildingsAfterRingBump(
                    town, defs, plots, townCfg, townSeed, queue.maxBuildings(), terrain,
                    prevSuburbanDist, prevCoreDist);
                roadMemo.clearFailures();
                ++ringBumps;
                ++town.placementBumpCount;
                ++bumpsThisSync;

                if (fillIn && !placed) {
                    placed = tryFillBlockingPendingAlleys(
                        town, instance, defs, plots, townSeed, queue.maxBuildings(), searchLog,
                        terrain, town.urbanCoreMaxHop, town.suburbanMaxHop, junctionHops,
                        failLimit, prep, roadMemo);
                }

                if (!placed && shouldTryUrbanCore()) {
                    placed = tryPlaceInUrbanCore(town, instance, defs, plots, townCfg, townSeed,
                                                 queue.maxBuildings(), searchLog, terrain,
                                                 junctionHops, prep, roadMemo);
                    if (placed && isUrbanCoreSaturated(town, townCfg, defs, junctionHops)) {
                        town.ringPhase = RingPhase::Normal;
                    }
                }

                if (!placed && bumpsThisSync >= kMaxBumpsPerSync) {
                    deferIndex = true;
                    break;
                }
            }

            if (!placed && !deferIndex) {
                if (town.ringPhase == RingPhase::DensifyCore && fillIn) {
                    skipReason = "urban_core_exhausted";
                    if (isUrbanCoreSaturated(town, townCfg, defs, junctionHops)) {
                        town.ringPhase = RingPhase::Normal;
                    }
                } else {
                    skipReason = "ring_no_slot";
                }
            }
        }

        if (!deferIndex) {
            if (!placed) {
                logPlacementDecision(town, searchLog, plots, defs);
                ++skipReasonCounts[skipReason];
                skipReasonByIndex[index] = skipReason;
                std::string skipLine =
                    "placement_skipped: queueIndex=" + std::to_string(index) + " type="
                    + defs.typeName(instance.typeId) + " reason=" + skipReason + " suburban_max="
                    + std::to_string(town.suburbanMaxHop) + " urban_core="
                    + std::to_string(town.urbanCoreMaxHop);
                if (!isRural && !searchLog.resultSummary.empty()) {
                    skipLine += " " + formatPlacementSearchSummary(searchLog);
                    const BandFilter suburbanFilter = BandFilter::suburban(town);
                    const FrontierBandSet bands = frontierBandsFromDistFilter(
                        town, suburbanFilter.minDistInclusive, suburbanFilter.maxDistInclusive,
                        suburbanFilter.enabled);
                    const PlotFrontierAudit audit = auditPlotFrontier(town, bands);
                    skipLine += " frontier(geom=" + std::to_string(audit.geometryEligible)
                                + " refs=" + std::to_string(audit.frontierRefs)
                                + " unique=" + std::to_string(audit.uniqueFrontierIds)
                                + " core=" + std::to_string(audit.coreRefs)
                                + " suburban=" + std::to_string(audit.suburbanRefs)
                                + " missing=" + std::to_string(audit.missingFromFrontier)
                                + " stale=" + std::to_string(audit.staleFrontierRefs) + ")";
                }
                Logger::log("layout", skipLine);
                town.placementFailedIndices.push_back(index);
                town.placementFailureCount = static_cast<int>(town.placementFailedIndices.size());
            } else {
                logPlacementDecision(town, searchLog, plots, defs);
                town.buildingInstances.push_back(instance);
            }

            ++town.placementQueueCursor;
        }
        }
    }

    if (town.placementQueueCursor >= targetCount) {
        town.placementSkipReasonsSummary = formatSkipReasonCounts(skipReasonCounts);
        std::string summary =
            "ring_summary: target=" + std::to_string(targetCount) + " placed="
            + std::to_string(town.buildingInstances.size()) + " failures="
            + std::to_string(town.placementFailureCount) + " bumps="
            + std::to_string(ringBumps) + " final_suburban_max="
            + std::to_string(town.suburbanMaxHop) + " final_urban_core="
            + std::to_string(town.urbanCoreMaxHop);
        if (!town.placementSkipReasonsSummary.empty()) {
            summary += " skip_reasons=" + town.placementSkipReasonsSummary;
        }
        Logger::log("layout", summary);
        for (int failedIndex : town.placementFailedIndices) {
            if (failedIndex < 0 || failedIndex >= static_cast<int>(queueTypes.size())) {
                continue;
            }
            const std::string failedType =
                queueTypes[static_cast<std::size_t>(failedIndex)];
            const auto reasonIt = skipReasonByIndex.find(failedIndex);
            const std::string reason =
                reasonIt != skipReasonByIndex.end() ? reasonIt->second : "unknown";
            Logger::log("layout",
                        "placement_failures: index=" + std::to_string(failedIndex) + " type="
                            + failedType + " reason=" + reason);
        }
    } else {
        town.placementSkipReasonsSummary.clear();
    }

    const bool deferMeshForAutoExit =
        config.growth.autoExit && config.growth.autoGrow > 0
        && queue.activeCount() < config.growth.autoGrow;
    if (!deferMeshForAutoExit) {
        PROFILE_SCOPE(ProfileScopeId::MeshRebuild);
        {
            PROFILE_SCOPE(ProfileScopeId::MeshOutline);
            rebuildOutlineMesh(town, defs, pixelsPerUnit, townSeed);
        }
        {
            PROFILE_SCOPE(ProfileScopeId::MeshFrontageSeg);
            rebuildFrontageSegmentMesh(town, plots, pixelsPerUnit, terrain);
        }
        int probeDisplayIndex = -1;
        if (town.urbanCoreMaxHop >= 0 && town.placementQueueCursor > 0) {
            probeDisplayIndex = town.placementQueueCursor - 1;
            if (probeDisplayIndex >= targetCount && targetCount > 0) {
                probeDisplayIndex = targetCount - 1;
            }
        }
        {
            PROFILE_SCOPE(ProfileScopeId::MeshAlleyProbe);
            rebuildAlleyProbeMesh(town, probeDisplayIndex, pixelsPerUnit);
        }
        {
            PROFILE_SCOPE(ProfileScopeId::MeshRoad);
            rebuildRoadMesh(town, config.colors.edges, config.colors.secondaryEdges,
                            config.colors.bridge, pixelsPerUnit, terrain,
                            config.terrain.clipRoadsAtWater);
        }
        {
            PROFILE_SCOPE(ProfileScopeId::MeshHopDebug);
            rebuildHopDebugRoadMesh(town, getJunctionHops(town), pixelsPerUnit);
            rebuildHopDebugJunctionMesh(town, getJunctionHops(town), pixelsPerUnit, 1.f);
        }
        {
            PROFILE_SCOPE(ProfileScopeId::MeshJunction);
            indexJunctions(town);
            buildJunctionMesh(town, config.world.pixelsPerUnit, 1.f);
        }
    }

    int gapFillCount = 0;
    int secondaryRoadCount = 0;
    for (const BuildingInstance& inst : town.buildingInstances) {
        if (inst.placementMode == BuildingPlacementMode::SegmentGapFill) {
            ++gapFillCount;
        }
    }
    for (const Road& road : town.roads) {
        if (road.isSecondary) {
            ++secondaryRoadCount;
        }
    }

    Logger::log("render",
                "placed=" + std::to_string(town.buildingInstances.size()) + " target="
                    + std::to_string(targetCount) + " failures="
                    + std::to_string(town.placementFailureCount) + " gap_fill="
                    + std::to_string(gapFillCount) + " secondary_roads="
                    + std::to_string(secondaryRoadCount));
}
