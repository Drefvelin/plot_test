#include "PlacementLogging.h"

#include "FrontagePlacement.h"
#include "FrontageZones.h"
#include "Logger.h"
#include "PlotDimensions.h"
#include "PlotGeometry.h"

#include <algorithm>

namespace {

bool g_verbosePlacementLogs = false;

}  // namespace

void setVerbosePlacementLogs(bool enabled) { g_verbosePlacementLogs = enabled; }

bool verbosePlacementLogs() { return g_verbosePlacementLogs; }

void resetPlacementSearchLog(PlacementSearchLog& log) {
    log.totalValid            = 0;
    log.chosenRoadCandidate   = -1;
    log.chosenRoad            = -1;
    log.chosenSegment         = -1;
    log.slotsExamined         = 0;
    log.zoneFiltered          = 0;
    log.noInwardSkipped       = 0;
    log.orientFailedSkipped   = 0;
    log.dimFailedSegments     = 0;
    log.layoutRequested       = 0;
    log.layoutPlaced          = 0;
    log.resultSummary.clear();
    log.roads.clear();
}

RoadSearchStats& statsFor(PlacementSearchLog& log, int roadId, float centDist) {
    auto it = log.roads.find(roadId);
    if (it == log.roads.end()) {
        RoadSearchStats stats;
        stats.roadId   = roadId;
        stats.centDist = centDist;
        it             = log.roads.emplace(roadId, stats).first;
    }
    return it->second;
}

void recordDimReject(RoadSearchStats& stats, DimReject reason) {
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
    case DimReject::DepthExceedsRoadHit:
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
    if (!g_verbosePlacementLogs) {
        return;
    }
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

    if (log.chosenRoadCandidate < 0) {
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
                "chosen: road_candidate=" + std::to_string(log.chosenRoadCandidate) + " road="
                    + std::to_string(log.chosenRoad) + " segment="
                    + std::to_string(log.chosenSegment) + " plot_center=("
                    + fmt1(log.chosenCenter.x) + "," + fmt1(log.chosenCenter.y)
                    + ") dist_to_center=" + fmt1(log.chosenDist) + " frontage="
                    + fmt1(log.chosenFrontage) + " depth=" + fmt1(log.chosenDepth) + " area="
                    + fmt1(log.chosenArea) + " depth/front="
                    + fmt1(log.chosenDepth / std::max(log.chosenFrontage, 1e-3f)) + " orientation="
                    + orientationName(log.chosenOrient));
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

    std::vector<const RoadSearchStats*> closerBlocked;
    closerBlocked.reserve(log.roads.size());
    for (const auto& [_, stats] : log.roads) {
        if (stats.centDist + 1e-3f < log.chosenDist && stats.valid == 0 && stats.roadsChecked > 0) {
            closerBlocked.push_back(&stats);
        }
    }
    std::sort(closerBlocked.begin(), closerBlocked.end(),
              [](const RoadSearchStats* a, const RoadSearchStats* b) {
                  return a->centDist < b->centDist;
              });

    if (closerBlocked.empty()) {
        Logger::log("placement",
                    "closer_roads: none closer than chosen plot; nearer segments likely could not "
                    "fit this building type under ratio/road/area constraints");
    } else {
        Logger::log("placement", "closer_roads_blocked: count=" + std::to_string(closerBlocked.size())
                                 + " (road segment nearer than chosen plot but zero valid placements)");
        const std::size_t limit = std::min<std::size_t>(closerBlocked.size(), 8);
        for (std::size_t i = 0; i < limit; ++i) {
            const RoadSearchStats& s = *closerBlocked[i];
            Logger::log("placement",
                        "  road_candidate=" + std::to_string(s.roadId) + " center_dist="
                            + fmt1(s.centDist) + " roads=" + std::to_string(s.roadsChecked)
                            + " dim_fail=" + std::to_string(s.dimInvalid) + " (road_short="
                            + std::to_string(s.dimRoadShort) + " ratio="
                            + std::to_string(s.dimRatio) + " area=" + std::to_string(s.dimArea)
                            + " depth=" + std::to_string(s.dimDepth) + " no_depth="
                            + std::to_string(s.dimNoDepth) + ") outside_road_model="
                            + std::to_string(s.outsideRoadModel) + " overlap="
                            + std::to_string(s.overlap));
        }
        if (closerBlocked.size() > limit) {
            Logger::log("placement", "  ... " + std::to_string(closerBlocked.size() - limit)
                                     + " more blocked closer roads omitted");
        }
    }

    std::vector<const RoadSearchStats*> closerButWorse;
    for (const auto& [_, stats] : log.roads) {
        if (stats.valid > 0 && stats.bestValidDist > log.chosenDist + 1e-3f
            && stats.centDist + 1e-3f < log.chosenDist) {
            closerButWorse.push_back(&stats);
        }
    }
    std::sort(closerButWorse.begin(), closerButWorse.end(),
              [](const RoadSearchStats* a, const RoadSearchStats* b) {
                  return a->centDist < b->centDist;
              });
    if (!closerButWorse.empty()) {
        Logger::log("placement", "closer_roads_with_worse_plots: count="
                                 + std::to_string(closerButWorse.size())
                                 + " (could place here but plot_center was farther than chosen)");
        const std::size_t limit = std::min<std::size_t>(closerButWorse.size(), 4);
        for (std::size_t i = 0; i < limit; ++i) {
            const RoadSearchStats& s = *closerButWorse[i];
            Logger::log("placement", "  road_candidate=" + std::to_string(s.roadId) + " center_dist="
                                     + fmt1(s.centDist) + " best_plot_dist="
                                     + fmt1(s.bestValidDist) + " vs chosen="
                                     + fmt1(log.chosenDist));
        }
    }

    if (!log.roads.empty()) {
        const RoadSearchStats* nearest = nullptr;
        for (const auto& [_, stats] : log.roads) {
            if (!nearest || stats.centDist < nearest->centDist) {
                nearest = &stats;
            }
        }
        if (nearest && nearest->valid == 0 && nearest->centDist + 1e-3f < log.chosenDist) {
            std::string primary = "unknown";
            if (nearest->dimNoDepth >= nearest->dimRatio && nearest->dimNoDepth >= nearest->dimRoadShort
                && nearest->dimNoDepth > 0) {
                primary = "depth_cap_zero (no road or outline hit along inward ray)";
            } else if (nearest->dimRatio >= nearest->dimRoadShort && nearest->dimRatio > 0) {
                primary = "depth_ratio (need wider road frontage for this building size)";
            } else if (nearest->dimRoadShort > 0) {
                primary = "road_too_short for minimum area";
            } else if (nearest->outsideRoadModel > nearest->dimInvalid) {
                primary = "outside_road_model (plot failed road-only validation)";
            } else if (nearest->overlap > 0) {
                primary = "overlap with existing buildings";
            }

            Logger::log("placement",
                        "nearest_road_candidate road=" + std::to_string(nearest->roadId) + " center_dist="
                            + fmt1(nearest->centDist) + " had zero valid plots; primary_blocker="
                            + primary);
            for (const RoadSearchStats::RoadProbe& probe : nearest->roadProbes) {
                Logger::log("placement",
                            "  road=" + std::to_string(probe.roadId) + " len="
                                + fmt1(probe.roadLen) + " depth_cap=" + fmt1(probe.depthCap)
                                + " setback_inside=" + (probe.setbackInside ? "yes" : "no")
                                + " alt_side_inside=" + (probe.altSideInside ? "yes" : "no")
                                + " reject=" + rejectName(probe.reject));
            }
        }

        float closestValidDist = std::numeric_limits<float>::max();
        for (const auto& [_, stats] : log.roads) {
            if (stats.valid > 0) {
                closestValidDist = std::min(closestValidDist, stats.bestValidDist);
            }
        }
        if (closestValidDist < std::numeric_limits<float>::max() - 1.f) {
            Logger::log("placement", "closest_valid_plot_dist=" + fmt1(closestValidDist)
                                     + " (global minimum among all roads with any valid slot)");
        }
    }
}

void logSegmentInventory(const Town& town) {
    if (!g_verbosePlacementLogs) {
        return;
    }
    int frontageCount = 0;
    int wallCount     = 0;
    for (const Road& road : town.roads) {
        frontageCount +=
            static_cast<int>(road.sideA.segments.size() + road.sideB.segments.size());
        wallCount += static_cast<int>(road.sideA.wallSegments.size() + road.sideB.wallSegments.size());
    }
    Logger::log("segments", "=== frontage segments after carve: count="
                                + std::to_string(frontageCount) + " wall=" + std::to_string(wallCount)
                                + " ===");
    for (const Road& road : town.roads) {
        const float roadDist = roadCenterDist(town, road.id);
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            const RoadSideFrontage* side = road.sideBank(bankIndex);
            for (const RoadFrontageSegment& segment : side->segments) {
                Logger::log("segments", "segment seg=" + std::to_string(segment.id) + " road="
                                            + std::to_string(road.id) + " bank="
                                            + std::to_string(bankIndex) + " width="
                                            + fmt1(segment.width()) + " road_dist="
                                            + fmt1(roadDist));
            }
            for (const RoadFrontageSegment& segment : side->wallSegments) {
                Logger::log("segments", "wall seg=" + std::to_string(segment.id) + " road="
                                            + std::to_string(road.id) + " bank="
                                            + std::to_string(bankIndex) + " width="
                                            + fmt1(segment.width()) + " road_dist="
                                            + fmt1(roadDist));
            }
        }
    }
}

void logSegmentProbe(int buildingId, const FrontageSlot& slot, const char* result,
                     DimReject reject, float depthCap, float frontageNeed, float areaFit,
                     float slotT) {
    if (!g_verbosePlacementLogs) {
        return;
    }
    std::string line = "segment_probe: placement #" + std::to_string(buildingId) + " seg="
                       + std::to_string(slot.segmentId) + " road=" + std::to_string(slot.roadId)
                       + " bank=" + std::to_string(slot.bankIndex) + " width=" + fmt1(slot.width())
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

std::string formatPlacementSearchSummary(const PlacementSearchLog& log) {
    int dimRoadShort = 0;
    int dimRatio     = 0;
    int dimArea      = 0;
    int dimDepth     = 0;
    int dimNoDepth   = 0;
    int dimInvalid   = 0;
    int overlap      = 0;
    for (const auto& [_, stats] : log.roads) {
        dimRoadShort += stats.dimRoadShort;
        dimRatio += stats.dimRatio;
        dimArea += stats.dimArea;
        dimDepth += stats.dimDepth;
        dimNoDepth += stats.dimNoDepth;
        dimInvalid += stats.dimInvalid;
        overlap += stats.overlap;
    }
    return "search slots=" + std::to_string(log.slotsExamined) + " zone_skip="
           + std::to_string(log.zoneFiltered) + " dim_fail=" + std::to_string(log.dimFailedSegments)
           + " summary=" + log.resultSummary + " rejects(road_short=" + std::to_string(dimRoadShort)
           + " ratio=" + std::to_string(dimRatio) + " area=" + std::to_string(dimArea)
           + " depth=" + std::to_string(dimDepth) + " no_depth=" + std::to_string(dimNoDepth)
           + " invalid=" + std::to_string(dimInvalid) + " overlap=" + std::to_string(overlap)
           + ")";
}

