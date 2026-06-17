#include "BuildingPlacer.h"

#include "BuildingGrowthQueue.h"
#include "BuildingLayout.h"
#include "FrontageGapFill.h"
#include "FrontagePlacement.h"
#include "FrontageZones.h"
#include "GrowthRings.h"
#include "Logger.h"
#include "PlacementLogging.h"
#include "PlotGeometry.h"
#include "PlacementPrep.h"
#include "Profile.h"
#include "RoadExhaustion.h"
#include "SecondaryRoadPlacement.h"
#include "TerrainAtlas.h"
#include "Units.h"

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cstring>
#include <string>

namespace {

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

void rebuildFrontageSegmentMesh(Town& town, const PlotConfig& plots, float pixelsPerUnit) {
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
    const sf::Color segmentColor(0, 160, 0);
    const sf::Color arrowColor(0, 160, 0);
    const float     minGapWidth     = std::max(0.5f, plots.frontageSetback * 2.f);

    std::vector<WallGap> wallGaps;
    collectAllPrimaryWallGaps(town, minGapWidth, wallGaps);

    for (const WallGap& gap : wallGaps) {
        if (gap.inward.length() < 1e-4f) {
            continue;
        }

        const Vec2& inward = gap.inward;
        const Vec2 roadStart = gap.origin + gap.edgeDir * gap.tMin;
        const Vec2 roadEnd   = gap.origin + gap.edgeDir * gap.tMax;
        const float midT     = gap.gapMidT();
        const Vec2  roadMid  = gap.origin + gap.edgeDir * midT;

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
                         segmentColor);
        appendArrow(town.frontageInwardArrowMesh, arrowTailPx, arrowHeadPosPx, arrowShaftPx,
                    arrowHeadPx, arrowColor);
        town.frontageSegmentLabels.push_back({gap.id, labelMidPx.x, labelMidPx.y});
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
            resolveBuildingSpecs(defs, instance.buildingType, instance.id, townSeed);
        layoutBuildingsOnPlot(instance.plot, town, specs, instance.id, townSeed, instance.footprints);
    }

    town.buildingOutlineMesh.clear();
    town.buildingOutlineMesh.setPrimitiveType(sf::Triangles);
    town.plotLabels.clear();
    town.buildingLabels.clear();

    constexpr float kOutlineUnits = 0.35f;
    for (const BuildingInstance& instance : town.buildingInstances) {
        const BuildingDef* def = defs.building(instance.buildingType);
        const sf::Color color =
            def ? sf::Color(def->rgb[0], def->rgb[1], def->rgb[2]) : sf::Color::White;

        if (instance.placementMode == BuildingPlacementMode::PlotLot) {
            appendPlotOutline(town.buildingOutlineMesh, instance.plot, pixelsPerUnit, kOutlineUnits,
                              color);

            const Vec2 plotMid = plotCenter(instance.plot);
            town.plotLabels.push_back(
                {instance.id, units::toPixels(plotMid.x, pixelsPerUnit),
                 units::toPixels(plotMid.y, pixelsPerUnit)});
        } else if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
            constexpr float kGapFillOutlineUnits = 0.55f;
            for (const BuildingFootprint& footprint : instance.footprints) {
                appendFootprintOutline(town.buildingOutlineMesh, footprint, pixelsPerUnit,
                                       kGapFillOutlineUnits, color);
            }

            if (!instance.footprints.empty()) {
                Vec2 fpMid{};
                for (const Vec2& corner : instance.footprints[0].corners) {
                    fpMid = fpMid + corner;
                }
                fpMid = fpMid * 0.25f;
                town.plotLabels.push_back(
                    {instance.id, units::toPixels(fpMid.x, pixelsPerUnit),
                     units::toPixels(fpMid.y, pixelsPerUnit)});
            }
        }

        for (const BuildingFootprint& footprint : instance.footprints) {
            Vec2 fpMid{};
            for (const Vec2& corner : footprint.corners) {
                fpMid = fpMid + corner;
            }
            fpMid = fpMid * 0.25f;
            const int labelId =
                footprint.labelId >= 0 ? footprint.labelId : 0;
            town.buildingLabels.push_back(
                {labelId, units::toPixels(fpMid.x, pixelsPerUnit),
                 units::toPixels(fpMid.y, pixelsPerUnit)});
        }
    }

    refreshBuildingDoorEdges(town, defs);
    appendBuildingFootprintOutlines(town, pixelsPerUnit);
}

bool instanceOnAlleyRoad(const BuildingInstance& instance, int roadId) {
    if (roadId < 0) {
        return false;
    }
    if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
        return instance.roadId == roadId;
    }
    if (instance.placementMode == BuildingPlacementMode::PlotLot) {
        return instance.plot.roadId == roadId;
    }
    return false;
}

const char* placementModeLabel(BuildingPlacementMode mode) {
    switch (mode) {
    case BuildingPlacementMode::SegmentGapFill:
        return "gap_fill";
    case BuildingPlacementMode::PlotLot:
        return "plot_lot";
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

}  // namespace

void BuildingPlacer::sync(Town& town, const BuildingGrowthQueue& queue, const DefCache& defs,
                          const PlotConfig& plots, const TownConfig& townCfg, const Config& config,
                          const PlacementFloors& floors, float pixelsPerUnit, int townSeed,
                          const TerrainAtlas* terrain, int maxIndicesPerSync) {
    PROFILE_SCOPE(ProfileScopeId::PlacerSync);
    const int targetCount = queue.activeCount();

    trimPlacementState(town, targetCount);

    const std::size_t secondaryBefore = town.secondaryRoadRecords.size();
    {
        PROFILE_SCOPE(ProfileScopeId::SecondaryRebuild);
        trimSecondaryRoadRecords(town, targetCount);
        if (town.secondaryRoadRecords.size() < secondaryBefore) {
            town.checkedAlleyGaps.clear();
        }
        town.alleyProbesByQueueIndex.assign(static_cast<std::size_t>(targetCount), {});
        rebuildSecondaryRoadsFromRecords(town, terrain);
        syncPendingAlleyFills(town, targetCount);
    }

    const auto reapplyFrontageCarves = [&]() {
        PROFILE_SCOPE(ProfileScopeId::FrontageCarve);
        resetRoadFrontageSegments(town, plots.frontageSetback);
        for (const BuildingInstance& inst : town.buildingInstances) {
            if (inst.placementMode == BuildingPlacementMode::SegmentGapFill) {
                if (!inst.footprints.empty()) {
                    carveRoadFrontageForFootprint(town, inst.roadId, inst.roadBank,
                                                  inst.footprints[0]);
                }
            } else {
                carveRoadFrontageForPlot(town, inst.plot, plots.frontageSetback);
            }
        }
    };

    reapplyFrontageCarves();
    initRoadExhaustionForSync(town, floors, townCfg);
    logSegmentInventory(town);
    logRingState(town);

    const auto& queueTypes = queue.queue();
    int         ringBumps    = 0;
    int         indicesProcessed = 0;

    while (town.placementQueueCursor < targetCount) {
        PROFILE_SCOPE(ProfileScopeId::GrowthLoop);
        const std::vector<int>& junctionHops = getJunctionHops(town);
        const int index = town.placementQueueCursor;
        if (index >= static_cast<int>(queueTypes.size())) {
            break;
        }

        BuildingInstance instance;
        instance.id           = index;
        instance.buildingType = queueTypes[static_cast<std::size_t>(index)];

        PlacementSearchLog searchLog;
        bool               placed     = false;
        bool               deferIndex = false;
        std::string        skipReason = "ring_no_slot";
        const char*        zone       = zoneTypeForBuilding(defs, instance.buildingType);
        const bool         isRural    = zone && std::strcmp(zone, "rural") == 0;
        const bool         fillIn     = isGapFillBuildingType(defs, instance.buildingType);

        RoadAttemptMemo roadMemo;
        roadMemo.syncContext(town);
        const PlacementPrep prep =
            buildPlacementPrep(town, defs, instance.buildingType, index, townSeed,
                               queue.maxBuildings());

        Logger::log("layout",
                    "ring_attempt: queueIndex=" + std::to_string(index) + " type="
                        + instance.buildingType + " suburban_max="
                        + std::to_string(town.suburbanMaxHop) + " urban_core="
                        + std::to_string(town.urbanCoreMaxHop) + " phase="
                        + ringPhaseLabel(town.ringPhase));

        if (isRural) {
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
            const int     maxBumpsPerSync =
                (maxIndicesPerSync > 0) ? 3 : kMaxBumpsPerIndex;
            int bumpsThisSync = 0;

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
                bumpGrowthRings(town);
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

                if (!placed && maxIndicesPerSync > 0 && bumpsThisSync >= maxBumpsPerSync) {
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

        if (deferIndex) {
            break;
        }

        if (!placed) {
            logPlacementDecision(town, searchLog, plots, defs);
            Logger::log("layout",
                        "placement_skipped: queueIndex=" + std::to_string(index) + " type="
                            + instance.buildingType + " reason=" + skipReason + " suburban_max="
                            + std::to_string(town.suburbanMaxHop) + " urban_core="
                            + std::to_string(town.urbanCoreMaxHop));
            town.placementFailedIndices.push_back(index);
            town.placementFailureCount = static_cast<int>(town.placementFailedIndices.size());
        } else {
            logPlacementDecision(town, searchLog, plots, defs);
            town.buildingInstances.push_back(instance);
            if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
                invalidateWallSpanCacheForBank(town, instance.roadId, instance.roadBank);
            } else {
                invalidateWallSpanCacheForBank(town, instance.plot.roadId, instance.plot.roadBank);
            }
        }

        ++town.placementQueueCursor;
        ++indicesProcessed;
        if (maxIndicesPerSync > 0 && indicesProcessed >= maxIndicesPerSync) {
            break;
        }
    }

    if (town.placementQueueCursor >= targetCount) {
        Logger::log("layout",
                    "ring_summary: target=" + std::to_string(targetCount) + " placed="
                        + std::to_string(town.buildingInstances.size()) + " failures="
                        + std::to_string(town.placementFailureCount) + " bumps="
                        + std::to_string(ringBumps) + " final_suburban_max="
                        + std::to_string(town.suburbanMaxHop) + " final_urban_core="
                        + std::to_string(town.urbanCoreMaxHop));
    }

    if (removeAlleysThroughSecondaryBuildings(town)) {
        syncPendingAlleyFills(town, targetCount);
        reapplyFrontageCarves();
    }

    {
        PROFILE_SCOPE(ProfileScopeId::MeshRebuild);
        rebuildOutlineMesh(town, defs, pixelsPerUnit, townSeed);
        rebuildFrontageSegmentMesh(town, plots, pixelsPerUnit);
        int probeDisplayIndex = -1;
        if (town.urbanCoreMaxHop >= 0 && town.placementQueueCursor > 0) {
            probeDisplayIndex = town.placementQueueCursor - 1;
            if (probeDisplayIndex >= targetCount && targetCount > 0) {
                probeDisplayIndex = targetCount - 1;
            }
        }
        rebuildAlleyProbeMesh(town, probeDisplayIndex, pixelsPerUnit);
        rebuildRoadMesh(town, config.colors.edges, config.colors.secondaryEdges,
                        config.colors.bridge, pixelsPerUnit, terrain, config.terrain.clipRoadsAtWater);
        rebuildHopDebugRoadMesh(town, getJunctionHops(town), pixelsPerUnit);
        rebuildHopDebugJunctionMesh(town, getJunctionHops(town), pixelsPerUnit, 1.f);
        indexJunctions(town);
        buildJunctionMesh(town, config.world.pixelsPerUnit, 1.f);
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
