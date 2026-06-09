#include "BuildingPlacer.h"

#include "BuildingGrowthQueue.h"
#include "BuildingLayout.h"
#include "FrontageGapFill.h"
#include "FrontagePlacement.h"
#include "Logger.h"
#include "PlacementLogging.h"
#include "PlotGeometry.h"
#include "SecondaryRoadPlacement.h"
#include "TerrainAtlas.h"
#include "Units.h"

#include <SFML/Graphics.hpp>

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
        if (gap.cellId < 0 || gap.cellId >= static_cast<int>(town.cells.size())) {
            continue;
        }
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

}  // namespace

void BuildingPlacer::sync(Town& town, const BuildingGrowthQueue& queue, const DefCache& defs,
                          const PlotConfig& plots, const TownConfig& townCfg, const Config& config,
                          float pixelsPerUnit, int townSeed, const TerrainAtlas* terrain) {
    const int targetCount = queue.activeCount();

    while (static_cast<int>(town.buildingInstances.size()) > targetCount) {
        town.buildingInstances.pop_back();
    }

    const std::size_t secondaryBefore = town.secondaryRoadRecords.size();
    trimSecondaryRoadRecords(town, targetCount);
    if (town.secondaryRoadRecords.size() < secondaryBefore) {
        town.checkedAlleyGaps.clear();
    }
    syncAlleyCellStates(town);
    town.alleyProbesByQueueIndex.assign(static_cast<std::size_t>(targetCount), {});
    rebuildSecondaryRoadsFromRecords(town);
    syncPendingAlleyFills(town, targetCount);

    const auto reapplyFrontageCarves = [&]() {
        resetRoadFrontageSegments(town, plots.frontageSetback);
        for (const BuildingInstance& inst : town.buildingInstances) {
            if (inst.placementMode == BuildingPlacementMode::SegmentGapFill) {
                if (!inst.footprints.empty()) {
                    carveRoadFrontageForFootprint(town, inst.roadId, inst.cellId,
                                                  inst.footprints[0]);
                }
            } else {
                carveRoadFrontageForPlot(town, inst.plot, plots.frontageSetback);
            }
        }
    };

    reapplyFrontageCarves();
    logSegmentInventory(town);

    const auto& queueTypes = queue.queue();
    int                    failed     = 0;

    while (static_cast<int>(town.buildingInstances.size()) < targetCount) {
        const int index = static_cast<int>(town.buildingInstances.size());
        if (index >= static_cast<int>(queueTypes.size())) {
            break;
        }

        BuildingInstance instance;
        instance.id           = index;
        instance.buildingType = queueTypes[static_cast<std::size_t>(index)];

        PlacementSearchLog searchLog;
        bool               placed = false;
        if (queue.isSegmentGapFillIndex(index) && isGapFillBuildingType(defs, instance.buildingType)) {
            const int   failLimit  = townCfg.alleyFillFailLimit;
            const float targetArea = samplePlotTargetArea(defs, instance.buildingType, instance.id,
                                                         townSeed);

            const auto tryFillOnAlleyRoad = [&](int roadId) -> bool {
                if (roadId < 0) {
                    return false;
                }
                return tryPlaceSegmentMain(town, instance.buildingType, defs, plots, instance,
                                           townSeed, queue.maxBuildings(), searchLog, roadId, true)
                    || tryPlaceRoadPlot(town, instance.buildingType, defs, plots, instance,
                                        targetArea, townSeed, queue.maxBuildings(), searchLog,
                                        roadId, true);
            };

            const auto tryAlleyAddAndFill = [&](int cellId) -> bool {
                if (cellId < 0) {
                    return false;
                }
                int newRoadIdLocal = -1;
                tryAddSecondaryRoad(town, index, plots.frontageSetback, townCfg, defs, searchLog,
                                    newRoadIdLocal, cellId);
                if (newRoadIdLocal < 0) {
                    return false;
                }
                return tryFillOnAlleyRoad(newRoadIdLocal);
            };

            int pendingIndex        = -1;
            int alleyRoadIdForPending = -1;

            if (hasBlockingPendingFills(town, failLimit)) {
                pendingIndex = frontPendingAlleyIndex(town, failLimit);
                if (pendingIndex >= 0) {
                    alleyRoadIdForPending = resolveSecondaryRoadId(
                        town,
                        town.pendingAlleyFills[static_cast<std::size_t>(pendingIndex)]
                            .addedAtQueueIndex);
                    placed = tryFillOnAlleyRoad(alleyRoadIdForPending);
                }
            } else {
                ensureActiveAlleyCell(town, plots.frontageSetback, townCfg);
                const int primaryCell = town.activeAlleyCellId;
                placed                = tryAlleyAddAndFill(primaryCell);

                pendingIndex = pendingAlleyIndexByQueueIndex(town, index);
                if (pendingIndex >= 0) {
                    alleyRoadIdForPending = resolveSecondaryRoadId(town, index);
                }

                if (!placed && primaryCell >= 0
                    && !hasBlockingPendingFills(town, failLimit)) {
                    const int secondCell =
                        pickAlternateAlleyCell(town, plots.frontageSetback, townCfg, primaryCell);
                    if (secondCell >= 0) {
                        Logger::log("layout",
                                    "alley_second_cell: queueIndex=" + std::to_string(index)
                                        + " primary_cell=" + std::to_string(primaryCell)
                                        + " second_cell=" + std::to_string(secondCell));
                        placed = tryAlleyAddAndFill(secondCell);
                        pendingIndex = pendingAlleyIndexByQueueIndex(town, index);
                        if (pendingIndex >= 0) {
                            alleyRoadIdForPending = resolveSecondaryRoadId(town, index);
                        }
                    }
                }
            }

            if (!placed) {
                placed = tryPlaceSegmentMain(town, instance.buildingType, defs, plots, instance,
                                             townSeed, queue.maxBuildings(), searchLog, -1, true);
            }
            if (!placed) {
                placed = tryPlaceRoadPlot(town, instance.buildingType, defs, plots, instance,
                                          targetArea, townSeed, queue.maxBuildings(), searchLog);
                if (placed) {
                    Logger::log("layout",
                                "plot_fallback: queueIndex=" + std::to_string(index) + " type="
                                    + instance.buildingType);
                }
            }

            if (pendingIndex >= 0 && alleyRoadIdForPending >= 0) {
                if (placed && instanceOnAlleyRoad(instance, alleyRoadIdForPending)) {
                    recordAlleyFillSuccess(town, pendingIndex);
                } else {
                    recordAlleyFillFailure(town, pendingIndex, failLimit);
                }
            }
        } else {
            const float targetArea =
                samplePlotTargetArea(defs, instance.buildingType, instance.id, townSeed);
            placed = tryPlaceRoadPlot(town, instance.buildingType, defs, plots, instance, targetArea,
                                      townSeed, queue.maxBuildings(), searchLog);
        }

        if (!placed) {
            logPlacementDecision(town, searchLog, plots, defs);
            if (queue.isSegmentGapFillIndex(index)
                && isGapFillBuildingType(defs, instance.buildingType)) {
                Logger::log("layout",
                            "placement_exhausted: queueIndex=" + std::to_string(index) + " type="
                                + instance.buildingType
                                + " (alley fill, second cell, gap fill, and plot fallback all failed)");
            }
            ++failed;
            break;
        }

        logPlacementDecision(town, searchLog, plots, defs);
        town.buildingInstances.push_back(instance);
    }

    if (removeAlleysThroughSecondaryBuildings(town)) {
        syncPendingAlleyFills(town, targetCount);
        reapplyFrontageCarves();
    }

    rebuildOutlineMesh(town, defs, pixelsPerUnit, townSeed);
    rebuildFrontageSegmentMesh(town, plots, pixelsPerUnit);
    int probeDisplayIndex = static_cast<int>(town.buildingInstances.size());
    if (probeDisplayIndex >= targetCount && targetCount > 0) {
        probeDisplayIndex = targetCount - 1;
    }
    if (probeDisplayIndex < 0
        || !queue.isSegmentGapFillIndex(probeDisplayIndex)
        || probeDisplayIndex >= static_cast<int>(queue.queue().size())
        || !isGapFillBuildingType(defs, queue.queue()[static_cast<std::size_t>(probeDisplayIndex)])) {
        probeDisplayIndex = -1;
    }
    rebuildAlleyProbeMesh(town, probeDisplayIndex, pixelsPerUnit);
    rebuildRoadMesh(town, config.colors.edges, config.colors.secondaryEdges, config.colors.bridge,
                    pixelsPerUnit, terrain, config.terrain.clipRoadsAtWater);
    indexJunctions(town);
    buildJunctionMesh(town, config.world.pixelsPerUnit, 1.f);

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

    Logger::log("render", "building instances=" + std::to_string(town.buildingInstances.size()) + "/"
                               + std::to_string(targetCount) + " gap_fill=" + std::to_string(gapFillCount)
                               + " secondary_roads=" + std::to_string(secondaryRoadCount)
                               + (failed > 0 ? " failed=" + std::to_string(failed) : ""));
}
