#include "PlacementLogging.h"

#include "FrontagePlacement.h"
#include "FrontageZones.h"
#include "Logger.h"
#include "PlotDimensions.h"
#include "PlotGeometry.h"

#include <algorithm>

CellSearchStats& statsFor(PlacementSearchLog& log, int cellId, float centDist) {
    auto it = log.cells.find(cellId);
    if (it == log.cells.end()) {
        CellSearchStats stats;
        stats.cellId   = cellId;
        stats.centDist = centDist;
        it             = log.cells.emplace(cellId, stats).first;
    }
    return it->second;
}

void recordRoadProbe(CellSearchStats& stats, int roadId, float roadLen, float depthCap,
                     DimReject reject, const Vec2& roadPoint, const Vec2& inward, float setback,
                     const Cell& cell) {
    for (const CellSearchStats::RoadProbe& probe : stats.roadProbes) {
        if (probe.roadId == roadId) {
            return;
        }
    }
    if (stats.roadProbes.size() >= 4) {
        return;
    }
    const Vec2 alt = inward * -1.f;
    stats.roadProbes.push_back(
        {roadId, roadLen, depthCap, reject,
         pointInPolygon(roadPoint + inward * setback, cell.boundary),
         pointInPolygon(roadPoint + alt * setback, cell.boundary)});
}

void recordDimReject(CellSearchStats& stats, DimReject reason) {
    ++stats.dimInvalid;
    switch (reason) {
    case DimReject::RoadTooShort:
        ++stats.dimRoadShort;
        break;
    case DimReject::DepthRatioExceeded:
        ++stats.dimRatio;
        break;
    case DimReject::AreaOutOfBand:
        ++stats.dimArea;
        break;
    case DimReject::DepthExceedsCell:
        ++stats.dimDepth;
        break;
    case DimReject::InvalidInput:
        ++stats.dimNoDepth;
        break;
    default:
        break;
    }
}

void logPlacementDecision(const Town& town, const PlacementSearchLog& log,
                          const PlotConfig& plots, const DefCache& defs) {
    Logger::log("placement", "=== placement #" + std::to_string(log.buildingId) + " type="
                             + log.buildingType + " ===");
    Logger::log("placement", "constraints: max_aspect_ratio="
                             + fmt1(plots.maxDepthToFrontRatio) + " frontage_setback="
                             + fmt1(plots.frontageSetback) + " town_center=("
                             + fmt1(town.center.x) + "," + fmt1(town.center.y) + ")");

    if (const SizeBand* plotBand = defs.plotSizeBandForBuilding(log.buildingType)) {
        const float minFrontRatio =
            std::sqrt(plotBand->minArea / plots.maxDepthToFrontRatio);
        std::string needsLog = "plot_needs: min_area=" + fmt1(plotBand->minArea) + " max_area="
                               + fmt1(plotBand->maxArea) + " sampled_target_area="
                               + fmt1(log.targetArea) + " try_orient="
                               + orientationName(log.orientFirst) + " min_frontage_for_ratio="
                               + fmt1(minFrontRatio);
        const BuildingDef* def = defs.building(log.buildingType);
        if (def) {
            for (const auto& [sizeName, _] : def->buildingsOnPlot) {
                if (const SizeBand* buildingBand = defs.buildingSizeBand(sizeName)) {
                    needsLog += " building_" + sizeName + "_area=" + fmt1(buildingBand->minArea)
                                 + "-" + fmt1(buildingBand->maxArea);
                }
            }
        }
        Logger::log("placement", needsLog);
    }

    if (log.chosenCell < 0) {
        Logger::log("placement", "result: FAILED " + log.resultSummary);
        Logger::log("placement", "failure_detail: slots_examined=" + std::to_string(log.slotsExamined)
                                 + " zone_filtered=" + std::to_string(log.zoneFiltered)
                                 + " no_inward=" + std::to_string(log.noInwardSkipped)
                                 + " orient_failed=" + std::to_string(log.orientFailedSkipped)
                                 + " dim_failed_segments=" + std::to_string(log.dimFailedSegments)
                                 + " valid_candidates=" + std::to_string(log.totalValid));
        return;
    }

    Logger::log("placement", "result: PLACED " + log.resultSummary);
    Logger::log("placement",
                "chosen: cell=" + std::to_string(log.chosenCell) + " road="
                    + std::to_string(log.chosenRoad) + " segment="
                    + std::to_string(log.chosenSegment) + " plot_center=("
                    + fmt1(log.chosenCenter.x) + "," + fmt1(log.chosenCenter.y)
                    + ") dist_to_center=" + fmt1(log.chosenDist) + " frontage="
                    + fmt1(log.chosenFrontage) + " depth=" + fmt1(log.chosenDepth) + " area="
                    + fmt1(log.chosenArea) + " depth/front="
                    + fmt1(log.chosenDepth / std::max(log.chosenFrontage, 1e-3f)) + " orientation="
                    + orientationName(log.chosenOrient));
    const Cell& chosenCellRef = town.cells[static_cast<std::size_t>(log.chosenCell)];
    Logger::log("placement", "cell_centroid=(" + fmt1(chosenCellRef.centroid.x) + ","
                             + fmt1(chosenCellRef.centroid.y) + ") cent_dist="
                             + fmt1((chosenCellRef.centroid - town.center).length())
                             + " voronoi_site=(" + fmt1(chosenCellRef.site.x) + ","
                             + fmt1(chosenCellRef.site.y) + ")");
    std::string searchDetails = "search: valid_candidates=" + std::to_string(log.totalValid)
                                + " zone=" + log.zoneType + " growth=" + fmt1(log.townGrowth)
                                + " min_center_dist=" + fmt1(log.zoneBias);
    if (log.zoneType == "rural") {
        searchDetails += " min_junction_hops=" + std::to_string(minJunctionHopsForRural(log.townGrowth))
                          + " min_building_sep=" + fmt1(minBuildingSeparationForRural(log.townGrowth))
                          + " target_center_dist=" + fmt1(ruralTargetCenterDist(town, log.townGrowth))
                          + " max_center_dist=" + fmt1(ruralMaxCenterDist(town, log.townGrowth));
    }
    searchDetails += " (rural targets mid-ring distance; carved on place)";
    Logger::log("placement", searchDetails);
    if (log.layoutRequested > 0) {
        Logger::log("placement", "layout: requested_buildings=" + std::to_string(log.layoutRequested)
                                 + " placed_footprints=" + std::to_string(log.layoutPlaced)
                                 + (log.layoutPlaced < log.layoutRequested
                                        ? " note=some_secondary_footprints_skipped_no_room"
                                        : ""));
    }

    std::vector<const CellSearchStats*> closerBlocked;
    closerBlocked.reserve(log.cells.size());
    for (const auto& [_, stats] : log.cells) {
        if (stats.centDist + 1e-3f < log.chosenDist && stats.valid == 0 && stats.roadsChecked > 0) {
            closerBlocked.push_back(&stats);
        }
    }
    std::sort(closerBlocked.begin(), closerBlocked.end(),
              [](const CellSearchStats* a, const CellSearchStats* b) {
                  return a->centDist < b->centDist;
              });

    if (closerBlocked.empty()) {
        Logger::log("placement",
                    "closer_cells: none with site closer than chosen plot; nearest cells likely "
                    "could not fit this building type under ratio/road/area constraints");
    } else {
        Logger::log("placement", "closer_cells_blocked: count=" + std::to_string(closerBlocked.size())
                                 + " (cell centroid nearer than chosen plot but zero valid placements)");
        const std::size_t limit = std::min<std::size_t>(closerBlocked.size(), 8);
        for (std::size_t i = 0; i < limit; ++i) {
            const CellSearchStats& s = *closerBlocked[i];
            Logger::log("placement",
                        "  cell=" + std::to_string(s.cellId) + " cent_dist="
                            + fmt1(s.centDist) + " roads=" + std::to_string(s.roadsChecked)
                            + " dim_fail=" + std::to_string(s.dimInvalid) + " (road_short="
                            + std::to_string(s.dimRoadShort) + " ratio="
                            + std::to_string(s.dimRatio) + " area=" + std::to_string(s.dimArea)
                            + " depth=" + std::to_string(s.dimDepth) + " no_depth="
                            + std::to_string(s.dimNoDepth) + ") outside_cell="
                            + std::to_string(s.outsideCell) + " overlap="
                            + std::to_string(s.overlap));
        }
        if (closerBlocked.size() > limit) {
            Logger::log("placement", "  ... " + std::to_string(closerBlocked.size() - limit)
                                     + " more blocked closer cells omitted");
        }
    }

    std::vector<const CellSearchStats*> closerButWorse;
    for (const auto& [_, stats] : log.cells) {
        if (stats.valid > 0 && stats.bestValidDist > log.chosenDist + 1e-3f
            && stats.centDist + 1e-3f < log.chosenDist) {
            closerButWorse.push_back(&stats);
        }
    }
    std::sort(closerButWorse.begin(), closerButWorse.end(),
              [](const CellSearchStats* a, const CellSearchStats* b) {
                  return a->centDist < b->centDist;
              });
    if (!closerButWorse.empty()) {
        Logger::log("placement", "closer_cells_with_worse_plots: count="
                                 + std::to_string(closerButWorse.size())
                                 + " (could place here but plot_center was farther than chosen)");
        const std::size_t limit = std::min<std::size_t>(closerButWorse.size(), 4);
        for (std::size_t i = 0; i < limit; ++i) {
            const CellSearchStats& s = *closerButWorse[i];
            Logger::log("placement", "  cell=" + std::to_string(s.cellId) + " cent_dist="
                                     + fmt1(s.centDist) + " best_plot_dist="
                                     + fmt1(s.bestValidDist) + " vs chosen="
                                     + fmt1(log.chosenDist));
        }
    }

    if (!log.cells.empty()) {
        const CellSearchStats* nearest = nullptr;
        for (const auto& [_, stats] : log.cells) {
            if (!nearest || stats.centDist < nearest->centDist) {
                nearest = &stats;
            }
        }
        if (nearest && nearest->valid == 0 && nearest->centDist + 1e-3f < log.chosenDist) {
            std::string primary = "unknown";
            if (nearest->dimNoDepth >= nearest->dimRatio && nearest->dimNoDepth >= nearest->dimRoadShort
                && nearest->dimNoDepth > 0) {
                primary = "depth_cap_zero (road/boundary mismatch or ray found no wall)";
            } else if (nearest->dimRatio >= nearest->dimRoadShort && nearest->dimRatio > 0) {
                primary = "depth_ratio (need wider road frontage for this building size)";
            } else if (nearest->dimRoadShort > 0) {
                primary = "road_too_short for minimum area";
            } else if (nearest->outsideCell > nearest->dimInvalid) {
                primary = "outside_cell (plot corners left polygon)";
            } else if (nearest->overlap > 0) {
                primary = "overlap with existing buildings";
            }

            Logger::log("placement",
                        "nearest_cell cell=" + std::to_string(nearest->cellId) + " cent_dist="
                            + fmt1(nearest->centDist) + " had zero valid plots; primary_blocker="
                            + primary);
            for (const CellSearchStats::RoadProbe& probe : nearest->roadProbes) {
                Logger::log("placement",
                            "  road=" + std::to_string(probe.roadId) + " len="
                                + fmt1(probe.roadLen) + " depth_cap=" + fmt1(probe.depthCap)
                                + " setback_inside=" + (probe.setbackInside ? "yes" : "no")
                                + " alt_side_inside=" + (probe.altSideInside ? "yes" : "no")
                                + " reject=" + rejectName(probe.reject));
            }
        }

        float closestValidDist = std::numeric_limits<float>::max();
        for (const auto& [_, stats] : log.cells) {
            if (stats.valid > 0) {
                closestValidDist = std::min(closestValidDist, stats.bestValidDist);
            }
        }
        if (closestValidDist < std::numeric_limits<float>::max() - 1.f) {
            Logger::log("placement", "closest_valid_plot_dist=" + fmt1(closestValidDist)
                                     + " (global minimum among all cells with any valid slot)");
        }
    }
}

void logSegmentInventory(const Town& town) {
    int count = 0;
    for (const Road& road : town.roads) {
        count += static_cast<int>(road.sideA.segments.size() + road.sideB.segments.size());
    }
    Logger::log("segments", "=== frontage segments after carve: count=" + std::to_string(count)
                                + " ===");
    for (const Road& road : town.roads) {
        for (const RoadSideFrontage* side : {&road.sideA, &road.sideB}) {
            for (const RoadFrontageSegment& segment : side->segments) {
                Logger::log("segments", "segment seg=" + std::to_string(segment.id) + " road="
                                            + std::to_string(road.id) + " cell="
                                            + std::to_string(side->cellId) + " width="
                                            + fmt1(segment.width()) + " center_dist="
                                            + fmt1(segment.centerDist));
            }
        }
    }
}

void logSegmentProbe(int buildingId, const FrontageSlot& slot, const char* result,
                     DimReject reject, float depthCap, float frontageNeed, float areaFit,
                     float slotT) {
    std::string line = "segment_probe: placement #" + std::to_string(buildingId) + " seg="
                       + std::to_string(slot.segmentId) + " road=" + std::to_string(slot.roadId)
                       + " cell=" + std::to_string(slot.cellId) + " width=" + fmt1(slot.width())
                       + " center_dist=" + fmt1(slot.centerDist) + " -> " + result;
    if (slotT >= 0.f) {
        line += " slot_t=" + fmt1(slotT);
    }
    if (reject != DimReject::None) {
        line += " reject=" + std::string(rejectName(reject));
    }
    if (depthCap >= 0.f) {
        line += " depth_cap=" + fmt1(depthCap);
    }
    if (frontageNeed >= 0.f) {
        line += " frontage_need=" + fmt1(frontageNeed);
    }
    if (areaFit >= 0.f) {
        line += " area_fit=" + fmt1(areaFit);
    }
    Logger::log("probe", line);
}

