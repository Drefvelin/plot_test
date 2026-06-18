#include "TerrainPlacementLogging.h"

#include "FrontagePlacement.h"
#include "FrontageZones.h"
#include "FrontierManager.h"
#include "GrowthRings.h"
#include "Logger.h"
#include "PlacementFrontier.h"
#include "PlotDimensions.h"
#include "PlotGeometry.h"
#include "Terrain.h"
#include "TerrainPlacement.h"
#include <sstream>
#include <string>
#include <unordered_set>

namespace {

const char* terrainRequirementName(TerrainRequirement req) {
    return req == TerrainRequirement::Strict ? "strict" : "loose";
}

const char* terrainPlacementModeName(TerrainPlacementMode mode) {
    switch (mode) {
    case TerrainPlacementMode::Inside:
        return "inside";
    case TerrainPlacementMode::Proximity:
        return "prox";
    case TerrainPlacementMode::Border:
        return "border";
    default:
        return "none";
    }
}

const char* instancePlacementModeName(BuildingPlacementMode mode) {
    switch (mode) {
    case BuildingPlacementMode::BorderPlot:
        return "border_plot";
    case BuildingPlacementMode::BorderBuilding:
        return "border_building";
    case BuildingPlacementMode::SegmentGapFill:
        return "gap_fill";
    case BuildingPlacementMode::PlotLot:
        return "plot_lot";
    default:
        return "unknown";
    }
}

std::string formatVec2(const Vec2& v) {
    return "(" + fmt1(v.x) + "," + fmt1(v.y) + ")";
}

std::string formatPlotCorners(const Plot& plot) {
    std::ostringstream out;
    out << "c0=" << formatVec2(plot.corners[0])
        << " c1=" << formatVec2(plot.corners[1])
        << " c2=" << formatVec2(plot.corners[2])
        << " c3=" << formatVec2(plot.corners[3]);
    return out.str();
}

void logTerrain(const std::string& line) { Logger::log("terrain_place", line); }

const char* terrainLabel(TerrainId id, const TerrainCatalog* catalog) {
    return catalog != nullptr ? terrainIdName(id, *catalog) : "unknown";
}

}  // namespace

void logTerrainTraceBegin(const TerrainPlacementTrace& trace, const BuildingDef& def) {
    logTerrain("event=begin queueIndex=" + std::to_string(trace.queueIndex) + " type="
               + def.name + " requirement=" + terrainRequirementName(def.terrain.requirement)
               + " placement=" + terrainPlacementModeName(def.terrain.placement) + " prefer="
               + terrainLabel(def.terrain.prefer, nullptr));
}

void logTerrainTracePhase(TerrainPlacementTrace& trace, const char* phase) {
    trace.phase     = phase;
    trace.slotOrder = 0;
    logTerrain("event=phase_start queueIndex=" + std::to_string(trace.queueIndex) + " phase="
               + phase);
}

void logTerrainTracePhaseEnd(const TerrainPlacementTrace& trace, bool success,
                             const std::string& summary) {
    logTerrain("event=phase_end queueIndex=" + std::to_string(trace.queueIndex) + " phase="
               + trace.phase + " success=" + (success ? "1" : "0") + " summary=" + summary);
}

void logTerrainTraceSlotQueue(const TerrainPlacementTrace& trace,
                              const std::vector<FrontageSlot>& slots,
                              const std::string& buildingType, const DefCache& defs,
                              const TerrainAtlas* terrain, std::size_t maxLogged) {
    logTerrain("event=slot_queue queueIndex=" + std::to_string(trace.queueIndex) + " phase="
               + trace.phase + " count=" + std::to_string(slots.size()));

    const BuildingDef* def = defs.building(buildingType);
    const std::size_t  limit = std::min(slots.size(), maxLogged);
    for (std::size_t i = 0; i < limit; ++i) {
        const FrontageSlot& slot = slots[i];
        std::string         line = "event=slot_rank queueIndex=" + std::to_string(trace.queueIndex)
                           + " phase=" + trace.phase + " rank=" + std::to_string(i) + " road="
                           + std::to_string(slot.roadId) + " bank=" + std::to_string(slot.bankIndex)
                           + " seg=" + std::to_string(slot.segmentId) + " width="
                           + fmt1(slot.width()) + " center_dist=" + fmt1(slot.centerDist)
                           + " zone_score=" + fmt1(slot.zoneScore);
        (void)def;
        (void)terrain;
        logTerrain(line);
    }
    if (slots.size() > limit) {
        logTerrain("event=slot_rank queueIndex=" + std::to_string(trace.queueIndex) + " phase="
                   + trace.phase + " note=omitted_" + std::to_string(slots.size() - limit)
                   + "_lower_rank_slots");
    }
}

void logTerrainTraceSlotTry(const TerrainPlacementTrace& trace, const FrontageSlot& slot,
                            float zoneScore, const char* result, const char* detail) {
    std::string line =
        "event=slot_try queueIndex=" + std::to_string(trace.queueIndex) + " phase=" + trace.phase
        + " order=" + std::to_string(trace.slotOrder) + " road=" + std::to_string(slot.roadId)
        + " bank=" + std::to_string(slot.bankIndex) + " seg=" + std::to_string(slot.segmentId)
        + " width=" + fmt1(slot.width()) + " center_dist=" + fmt1(slot.centerDist)
        + " zone_score=" + fmt1(zoneScore) + " result=" + result;
    if (detail != nullptr && detail[0] != '\0') {
        line += " detail=";
        line += detail;
    }
    logTerrain(line);
}

void logTerrainTraceBorderSummary(const TerrainPlacementTrace& trace, int samplesTotal,
                                  int spanUsedSkip, int noRoadSnap, int dimFail,
                                  int terrainForbidden, int invalidPlot, int overlap,
                                  int alleyOverlap, int borderBandReject, int scoreBeat) {
    logTerrain("event=border_search queueIndex=" + std::to_string(trace.queueIndex)
               + " samples=" + std::to_string(samplesTotal) + " skip_span_used="
               + std::to_string(spanUsedSkip) + " no_road_snap=" + std::to_string(noRoadSnap)
               + " dim_fail=" + std::to_string(dimFail) + " terrain_forbidden="
               + std::to_string(terrainForbidden) + " invalid_plot=" + std::to_string(invalidPlot)
               + " overlap=" + std::to_string(overlap) + " over_alley="
               + std::to_string(alleyOverlap) + " border_band=" + std::to_string(borderBandReject)
               + " score_beat=" + std::to_string(scoreBeat));
}

void logTerrainTraceBorderWinner(const TerrainPlacementTrace& trace, const BuildingDef& def,
                                 const TerrainAtlas& terrain, const Plot& plot, int roadId,
                                 int bankIndex, int graphIndex, float edgeDist, float score) {
    const Vec2 center = plotCenter(plot);
    logTerrain("event=border_winner queueIndex=" + std::to_string(trace.queueIndex) + " road="
               + std::to_string(roadId) + " bank=" + std::to_string(bankIndex) + " graph="
               + std::to_string(graphIndex) + " edge_dist=" + fmt1(edgeDist) + " score="
               + fmt1(score) + " plot_center=" + formatVec2(center) + " plot_area="
               + fmt1(plot.area) + " " + formatPlotCorners(plot) + " prefer="
               + terrainLabel(def.terrain.prefer, terrain.catalog));
}

void logTerrainTracePlaced(const TerrainPlacementTrace& trace, const char* path,
                           const BuildingInstance& instance, const BuildingDef& def,
                           const Town& town, const TerrainAtlas* terrain,
                           const PlacementSearchLog* searchLog) {
    const Vec2  center     = plotCenter(instance.plot);
    const float centerDist = (center - town.center).length();
    std::string line =
        "event=placed queueIndex=" + std::to_string(trace.queueIndex) + " type=" + def.name
        + " path=" + path + " placement_mode=" + instancePlacementModeName(instance.placementMode)
        + " road=" + std::to_string(instance.plot.roadId) + " bank="
        + std::to_string(instance.plot.roadBank) + " plot_center=" + formatVec2(center)
        + " dist_from_town_center=" + fmt1(centerDist) + " plot_area="
        + fmt1(instance.plot.area) + " " + formatPlotCorners(instance.plot);

    if (terrain != nullptr && terrain->valid) {
        const float plotTerrainScore = terrainScoreForPlot(instance.plot, def, *terrain);
        const Vec2    backMid =
            (instance.plot.corners[2] + instance.plot.corners[3]) * 0.5f;
        const float edgeDist = distToPreferEdge(backMid, def.terrain.prefer, *terrain);
        line += " terrain_score=" + fmt1(plotTerrainScore) + " edge_dist=" + fmt1(edgeDist)
                + " prefer=" + terrainLabel(def.terrain.prefer, terrain->catalog);
    }

    if (searchLog != nullptr) {
        line += " search_summary=" + searchLog->resultSummary;
        if (searchLog->chosenSegment >= 0) {
            line += " chosen_seg=" + std::to_string(searchLog->chosenSegment) + " chosen_zone_score="
                    + fmt1(searchLog->chosenZoneScore);
        }
        line += " slots_examined=" + std::to_string(searchLog->slotsExamined) + " dim_failed="
                + std::to_string(searchLog->dimFailedSegments);
    }

    logTerrain(line);
}

void logTerrainTraceFailed(const TerrainPlacementTrace& trace, const char* reason,
                           const std::string& detail) {
    std::string line = "event=failed queueIndex=" + std::to_string(trace.queueIndex) + " phase="
                       + trace.phase + " reason=" + reason;
    if (!detail.empty()) {
        line += " detail=" + detail;
    }
    logTerrain(line);
}

void logTerrainAnchorStored(int queueIndex, TerrainId prefer, int roadId, const char* source,
                            const TerrainCatalog* catalog) {
    logTerrain("event=anchor_stored queueIndex=" + std::to_string(queueIndex) + " prefer="
               + terrainLabel(prefer, catalog) + " road=" + std::to_string(roadId) + " source="
               + (source != nullptr ? source : "?"));
}

void logTerrainAnchorReplaced(int queueIndex, TerrainId prefer, int prevRoadId, int newRoadId,
                              const char* source, const TerrainCatalog* catalog) {
    logTerrain("event=anchor_replace queueIndex=" + std::to_string(queueIndex) + " prefer="
               + terrainLabel(prefer, catalog) + " prev_road=" + std::to_string(prevRoadId)
               + " new_road=" + std::to_string(newRoadId) + " source="
               + (source != nullptr ? source : "?"));
}

void logTerrainAnchorBfs(int queueIndex, TerrainId prefer, int anchorRoadId, int maxRoads,
                         const TerrainAnchorBfsResult& bfs, const Town& town, float suburbanDist,
                         const TerrainCatalog* catalog) {
    logTerrain("event=anchor_bfs_begin queueIndex=" + std::to_string(queueIndex) + " prefer="
               + terrainLabel(prefer, catalog) + " anchor_road=" + std::to_string(anchorRoadId)
               + " max_roads=" + std::to_string(maxRoads) + " visit_count="
               + std::to_string(bfs.visitOrder.size()) + " selected_count="
               + std::to_string(bfs.selected.size()) + " suburban_dist=" + fmt1(suburbanDist));

    std::unordered_set<int> selectedSet(bfs.selected.begin(), bfs.selected.end());
    for (std::size_t i = 0; i < bfs.visitOrder.size(); ++i) {
        const int   roadId    = bfs.visitOrder[i];
        const int   graphDist = bfs.visitDist[i];
        const float centerDist =
            roadId >= 0 && roadId < static_cast<int>(town.roads.size())
                ? roadMidpointCenterDist(town, town.roads[static_cast<std::size_t>(roadId)])
                : -1.f;
        const bool ruralOk = centerDist > suburbanDist + 1e-3f;
        const bool isAnchor = roadId == anchorRoadId;
        const bool selected = selectedSet.count(roadId) != 0;

        std::string flags;
        if (isAnchor) {
            flags += "anchor,";
        }
        if (selected) {
            flags += "try,";
        } else if (!isAnchor && graphDist > 0) {
            flags += "cap_skip,";
        }
        flags += ruralOk ? "rural_ok" : "rural_skip";

        logTerrain("event=anchor_bfs_visit queueIndex=" + std::to_string(queueIndex) + " order="
                   + std::to_string(i) + " road=" + std::to_string(roadId) + " graph_dist="
                   + std::to_string(graphDist) + " center_dist=" + fmt1(centerDist) + " flags="
                   + flags);
    }

    std::ostringstream selectedLine;
    selectedLine << "event=anchor_bfs_selected queueIndex=" << queueIndex << " roads=";
    for (std::size_t i = 0; i < bfs.selected.size(); ++i) {
        if (i > 0) {
            selectedLine << ',';
        }
        selectedLine << bfs.selected[i];
    }
    logTerrain(selectedLine.str());
}

void logTerrainAnchorRoadTry(int queueIndex, int roadId, int graphDist, float centerDist,
                             const char* result, const std::string& detail) {
    std::string line = "event=anchor_road_try queueIndex=" + std::to_string(queueIndex) + " road="
                       + std::to_string(roadId) + " graph_dist=" + std::to_string(graphDist)
                       + " center_dist=" + fmt1(centerDist) + " result=" + result;
    if (!detail.empty()) {
        line += " detail=" + detail;
    }
    logTerrain(line);
}

void logTerrainAnchorRoadFrontier(int queueIndex, int roadId, const Town& town,
                                  float suburbanDist) {
    std::size_t plotCount = 0;
    std::size_t scanCount = 0;
    std::ostringstream plotSegs;
    std::ostringstream scanSegs;

    const char* bandNames[] = {"core", "suburban", "rural"};
    for (int bi = 0; bi < 3; ++bi) {
        for (const FrontierRef& ref : town.frontierManager.plot[static_cast<std::size_t>(bi)]) {
            if (ref.roadId != roadId) {
                continue;
            }
            ++plotCount;
            if (plotCount <= 12) {
                if (plotCount > 1) {
                    plotSegs << ';';
                }
                plotSegs << "seg" << ref.segmentId << "@t" << fmt1(ref.startT) << "-"
                         << fmt1(ref.endT) << "(" << bandNames[bi] << ",w="
                         << fmt1(ref.endT - ref.startT) << ",d=" << fmt1(ref.centerDist) << ")";
            }
        }
    }

    for (const std::vector<TerrainScanSlotRef>& bucket : town.frontierManager.scan) {
        for (const TerrainScanSlotRef& ref : bucket) {
            if (ref.base.roadId != roadId) {
                continue;
            }
            ++scanCount;
            if (scanCount <= 12) {
                if (scanCount > 1) {
                    scanSegs << ';';
                }
                scanSegs << "seg" << ref.base.segmentId << "(edge=" << fmt1(ref.edgeDist) << ",d="
                           << fmt1(ref.base.centerDist) << ",w="
                           << fmt1(ref.base.endT - ref.base.startT) << ")";
            }
        }
    }

    logTerrain("event=anchor_road_frontier queueIndex=" + std::to_string(queueIndex) + " road="
               + std::to_string(roadId) + " plot_slots=" + std::to_string(plotCount)
               + " scan_hills_slots=" + std::to_string(scanCount) + " suburban_dist="
               + fmt1(suburbanDist) + " plot=" + plotSegs.str() + " scan_hills=" + scanSegs.str());
}

namespace {

bool distInRuralBand(float dist, float suburbanDist) { return dist > suburbanDist + 1e-3f; }

bool distInBandFilter(float dist, const BandFilter& filter) {
    if (!filter.enabled) {
        return true;
    }
    if (filter.exclusiveMin) {
        if (dist <= filter.minDistInclusive + 1e-3f) {
            return false;
        }
    } else if (dist + 1e-3f < filter.minDistInclusive) {
        return false;
    }
    return dist <= filter.maxDistInclusive + 1e-3f;
}

}  // namespace

void logTerrainScanFrontierHead(int queueIndex, TerrainId prefer, const Town& town,
                                const BandFilter& bandFilter, float suburbanDist,
                                float proximityMaxDist, std::size_t maxLogged,
                                const TerrainCatalog* catalog) {
    if (catalog == nullptr) {
        logTerrain("event=scan_bucket_head queueIndex=" + std::to_string(queueIndex) + " prefer="
                   + terrainLabel(prefer, catalog) + " bucket_size=? proximity_max="
                   + fmt1(proximityMaxDist) + " suburban_dist=" + fmt1(suburbanDist)
                   + " note=no_catalog");
        return;
    }

    const std::vector<TerrainScanSlotRef>& bucket =
        scanSlotsFor(town.frontierManager, *catalog, prefer);

    const std::size_t total = bucket.size();
    logTerrain("event=scan_bucket_head queueIndex=" + std::to_string(queueIndex) + " prefer="
               + terrainLabel(prefer, catalog) + " bucket_size=" + std::to_string(total)
               + " proximity_max=" + fmt1(proximityMaxDist) + " suburban_dist="
               + fmt1(suburbanDist));

    if (bucket.empty()) {
        return;
    }

    std::size_t logged = 0;
    for (std::size_t i = 0; i < bucket.size() && logged < maxLogged; ++i) {
        const TerrainScanSlotRef& ref = bucket[i];
        const float             dist  = ref.base.centerDist;
        const bool              bandOk = distInBandFilter(dist, bandFilter);
        const bool              proxOk = ref.edgeDist <= proximityMaxDist + 1e-3f;
        const bool              widthOk =
            ref.base.endT - ref.base.startT + 1e-3f >= town.syncMinPlotFrontage;

        logTerrain("event=scan_bucket_rank queueIndex=" + std::to_string(queueIndex) + " rank="
                   + std::to_string(i) + " road=" + std::to_string(ref.base.roadId) + " bank="
                   + std::to_string(ref.base.bankIndex) + " seg=" + std::to_string(ref.base.segmentId)
                   + " edge_dist=" + fmt1(ref.edgeDist) + " center_dist=" + fmt1(dist)
                   + " width=" + fmt1(ref.base.endT - ref.base.startT) + " band_ok="
                   + (bandOk ? "1" : "0") + " prox_ok=" + (proxOk ? "1" : "0") + " width_ok="
                   + (widthOk ? "1" : "0"));
        ++logged;
    }

    if (bucket.size() > maxLogged) {
        logTerrain("event=scan_bucket_rank queueIndex=" + std::to_string(queueIndex)
                   + " note=omitted_" + std::to_string(bucket.size() - maxLogged)
                   + "_lower_rank_slots");
    }
}

void logTerrainSegmentLookup(int queueIndex, int segmentId, const Town& town, int anchorRoadId,
                             const TerrainAnchorBfsResult* bfs) {
    const int roadId = roadIdForSegment(town, segmentId);
    if (roadId < 0) {
        logTerrain("event=segment_lookup queueIndex=" + std::to_string(queueIndex) + " seg="
                   + std::to_string(segmentId) + " road=missing");
        return;
    }

    int graphDist = -1;
    bool selected = false;
    if (bfs != nullptr) {
        for (std::size_t i = 0; i < bfs->visitOrder.size(); ++i) {
            if (bfs->visitOrder[i] == roadId) {
                graphDist = bfs->visitDist[i];
                break;
            }
        }
        selected = std::find(bfs->selected.begin(), bfs->selected.end(), roadId) != bfs->selected.end();
    }

    logTerrain("event=segment_lookup queueIndex=" + std::to_string(queueIndex) + " seg="
               + std::to_string(segmentId) + " road=" + std::to_string(roadId) + " anchor_road="
               + std::to_string(anchorRoadId) + " graph_dist_from_anchor="
               + std::to_string(graphDist) + " anchor_selected="
               + (selected ? "1" : "0"));
}
