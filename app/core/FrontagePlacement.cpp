#include "FrontagePlacement.h"

#include "BuildingLayout.h"
#include "FrontageZones.h"
#include "GrowthRings.h"
#include "Logger.h"
#include "PlotDimensions.h"
#include "PlotGeometry.h"
#include "RoadExhaustion.h"
#include "PlacementFrontier.h"
#include "Profile.h"
#include "TerrainAtlas.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <limits>
#include <vector>

float slotTNearestCenter(const Vec2& origin, const Vec2& edgeDir, float frontage, float gapStart,
                         float gapEnd, const Vec2& center) {
    const float tMin = gapStart;
    const float tMax = gapEnd - frontage;
    if (tMax < tMin - 1e-3f) {
        return -1.f;
    }
    const float alongCenter = (center - origin).dot(edgeDir);
    return std::clamp(alongCenter - frontage * 0.5f, tMin, tMax);
}

void buildSegmentTCandidates(const FrontageSlot& slot, float minFrontage, const Vec2& origin,
                             const Vec2& edgeDir, const Vec2& center, std::vector<float>& out) {
    out.clear();
    const float tMax = slot.endT - minFrontage;
    if (tMax < slot.startT - 1e-3f) {
        return;
    }

    const float centerT =
        slotTNearestCenter(origin, edgeDir, minFrontage, slot.startT, slot.endT, center);
    if (centerT >= 0.f) {
        out.push_back(centerT);
    }

    const float step = std::max(minFrontage * 0.35f, 0.75f);
    for (float t = slot.startT; t <= tMax + 1e-3f; t += step) {
        out.push_back(t);
    }
    out.push_back(tMax);

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end(),
                          [](float lhs, float rhs) { return std::abs(lhs - rhs) < 0.08f; }),
              out.end());

    std::sort(out.begin(), out.end(), [&](float lhs, float rhs) {
        const Vec2 midL = origin + edgeDir * (lhs + minFrontage * 0.5f);
        const Vec2 midR = origin + edgeDir * (rhs + minFrontage * 0.5f);
        return (midL - center).length() < (midR - center).length();
    });
}

struct SegmentPlacementResult {
    bool            placed = false;
    Plot            plot{};
    PlotDimensions  dims{};
    PlotOrientation orient   = PlotOrientation::Horizontal;
    float           slotT    = 0.f;
    DimReject       lastReject = DimReject::InvalidInput;
    float           lastDepthCap = 0.f;
    const char*     failReason   = "dim_fail";
    int             tries        = 0;
};

SegmentPlacementResult trySegmentPositions(Town& town, const FrontageSlot& slot,
                                           const std::string& buildingType, const DefCache& defs,
                                           const PlotConfig& plots, const Vec2& origin,
                                           const Vec2& edgeDir, const Vec2& sideInward,
                                           float targetArea, const PlotOrientation* orientOrder,
                                           int buildingId, const Vec2& sortCenter,
                                           const TerrainAtlas* terrain,
                                           const SizeBand* plotAreaBand = nullptr) {
    SegmentPlacementResult result;
    const float minFrontage =
        bandMinFrontage(defs, buildingType, slot.width(), plots.maxDepthToFrontRatio);

    std::vector<float> tCandidates;
    buildSegmentTCandidates(slot, minFrontage, origin, edgeDir, sortCenter, tCandidates);
    if (tCandidates.empty()) {
        result.failReason = "segment_too_short";
        return result;
    }

    for (const float t : tCandidates) {
        const float roomFront = slot.endT - t;
        if (roomFront < minFrontage - 1e-3f) {
            continue;
        }

        for (int oi = 0; oi < 2; ++oi) {
            const PlotOrientation orient = orientOrder[oi];
            DimReject             reject = DimReject::None;
            const Vec2            roadStart = origin + edgeDir * t;
            PlotDimensions        dims      = computePlotDimensionsForRoad(
                defs, buildingType, targetArea, orient, roadStart, edgeDir, roomFront, sideInward,
                slot.roadId, slot.bankIndex, town, plots.maxDepthToFrontRatio, plots.frontageSetback,
                &reject, plotAreaBand);
            ++result.tries;

            if (!dims.valid || dims.frontage > roomFront + 1e-3f) {
                result.lastReject = reject;
                const SizeBand* band = defs.sizeBandForBuilding(buildingType);
                if (band) {
                    const float probeFront = std::min(roomFront, std::sqrt(band->maxArea));
                    result.lastDepthCap    = maxPlotDepthToRoadHit(
                        roadStart, edgeDir, probeFront, sideInward, plots.frontageSetback,
                        slot.roadId, slot.bankIndex, town);
                }
                continue;
            }

            Plot candidate{};
            candidate.id       = buildingId;
            candidate.roadId = slot.roadId;
            candidate.roadBank = slot.bankIndex;
            buildRoadPlot(roadStart, edgeDir, sideInward, plots.frontageSetback, dims.frontage,
                          dims.depth, candidate);

            if (terrain != nullptr && terrain->valid
                && !polygonBuildable(candidate.corners, *terrain)) {
                result.failReason = "terrain_forbidden";
                logSegmentProbe(buildingId, slot, result.failReason, DimReject::None, -1.f,
                                dims.frontage, dims.area, t);
                continue;
            }

            if (!plotPlacementValid(candidate, town, terrain, plots.frontageSetback, slot.roadId)) {
                result.failReason = "invalid_plot";
                result.lastReject = DimReject::DepthExceedsRoadHit;
                result.lastDepthCap = maxPlotDepthToRoadHit(
                    roadStart, edgeDir, dims.frontage, sideInward, plots.frontageSetback,
                    slot.roadId, slot.bankIndex, town);
                logSegmentProbe(buildingId, slot, result.failReason, result.lastReject,
                                result.lastDepthCap, dims.frontage, dims.area, t);
                continue;
            }
            if (overlapsInstances(candidate, town.buildingInstances)) {
                result.failReason = "overlap";
                logSegmentProbe(buildingId, slot, result.failReason, DimReject::None, -1.f,
                                dims.frontage, dims.area, t);
                continue;
            }
            const int excludeAlleyRoadId =
                slot.roadId >= 0 && slot.roadId < static_cast<int>(town.roads.size())
                && town.roads[static_cast<std::size_t>(slot.roadId)].isSecondary
                    ? slot.roadId
                    : -1;
            if (plotOverlapsAlleys(candidate, town, plots.frontageSetback, excludeAlleyRoadId)) {
                result.failReason = "over_alley";
                logSegmentProbe(buildingId, slot, result.failReason, DimReject::None, -1.f,
                                dims.frontage, dims.area, t);
                continue;
            }

            result.placed = true;
            result.plot   = candidate;
            result.dims   = dims;
            result.orient = orient;
            result.slotT  = t;
            return result;
        }
    }

    return result;
}

void collectFrontageSlots(const Town& town, const DefCache& defs,
                          const std::string& buildingType, float townGrowth,
                          std::vector<FrontageSlot>& out, const BandFilter& bandFilter,
                          int roadFilter, bool skipPlotExhaustedBanks) {
    out.clear();
    const char* zone = zoneTypeForBuilding(defs, buildingType);
    for (const Road& road : town.roads) {
        if (roadFilter >= 0 && road.id != roadFilter) {
            continue;
        }
        if (bandFilter.enabled) {
            const float dist = roadMidpointCenterDist(town, road);
            if (!distInFilter(dist, bandFilter)) {
                continue;
            }
        }
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            const RoadSideFrontage* side = road.sideBank(bankIndex);
            if (skipPlotExhaustedBanks
                && bankPlotExhaustedVerified(town, road.id, bankIndex)) {
                continue;
            }
            for (const RoadFrontageSegment& segment : side->segments) {
                if (segment.width() < 1.f) {
                    continue;
                }
                FrontageSlot slot;
                slot.segmentId  = segment.id;
                slot.roadId     = road.id;
                slot.bankIndex  = bankIndex;
                slot.startT     = segment.startT;
                slot.endT       = segment.endT;
                slot.centerDist = segment.centerDist;
                slot.zoneScore  = scoreSegmentForZone(town, slot, zone, townGrowth);
                out.push_back(slot);
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const FrontageSlot& lhs, const FrontageSlot& rhs) {
        if (std::abs(lhs.zoneScore - rhs.zoneScore) > 1e-3f) {
            return lhs.zoneScore < rhs.zoneScore;
        }
        if (std::abs(lhs.centerDist - rhs.centerDist) > 1e-3f) {
            return lhs.centerDist < rhs.centerDist;
        }
        if (lhs.roadId != rhs.roadId) {
            return lhs.roadId < rhs.roadId;
        }
        return lhs.startT < rhs.startT;
    });
}

bool tryPlaceRoadPlot(Town& town, const std::string& buildingType, const DefCache& defs,
                      const PlotConfig& plots, BuildingInstance& out, const PlacementPrep& prep,
                      int townSeed, int maxBuildings, PlacementSearchLog& searchLog,
                      const TerrainAtlas* terrain, const BandFilter& bandFilter, int roadFilter) {
    PROFILE_SCOPE(ProfileScopeId::PlacePlot);
    const PlotOrientation orientOrder[2] = {prep.orientFirst, prep.orientSecond};

    searchLog.buildingId   = out.id;
    searchLog.buildingType = buildingType;
    searchLog.targetArea   = prep.targetArea;
    searchLog.orientFirst  = prep.orientFirst;
    searchLog.totalValid   = 0;
    searchLog.townGrowth   = prep.townGrowth;
    searchLog.zoneBias     = prep.zoneBias;
    searchLog.zoneType     = prep.zoneType;

    const bool logProbes = verbosePlacementLogs() && roadFilter < 0;

    const bool          useFrontier = roadFilter < 0;
    const FrontierBandSet frontierBands =
        frontierBandsFromDistFilter(town, bandFilter.minDistInclusive, bandFilter.maxDistInclusive,
                                    bandFilter.enabled);
    const float minPlotWidth = town.syncMinPlotFrontage;

    std::vector<FrontageSlot> collectedSlots;
    if (!useFrontier) {
        collectFrontageSlots(town, defs, buildingType, prep.townGrowth, collectedSlots, bandFilter,
                             roadFilter);
    }

    std::unordered_set<int> skippedSegments;
    std::size_t             collectedIndex = 0;

    while (true) {
        FrontageSlot slot{};
        if (useFrontier) {
            FrontierRef ref;
            if (!peekClosestPlotRef(town, frontierBands, minPlotWidth, skippedSegments, ref)) {
                break;
            }
            if (!fillFrontageSlotFromRef(town, ref, false, slot)) {
                skippedSegments.insert(ref.segmentId);
                continue;
            }
            skippedSegments.insert(ref.segmentId);
            const char* zone = zoneTypeForBuilding(defs, buildingType);
            slot.zoneScore   = scoreSegmentForZone(town, slot, zone, prep.townGrowth);
        } else {
            if (collectedIndex >= collectedSlots.size()) {
                break;
            }
            slot = collectedSlots[collectedIndex++];
        }

        if (slot.roadId < 0 || slot.roadId >= static_cast<int>(town.roads.size())) {
            continue;
        }

        ++searchLog.slotsExamined;

        const Road& road = town.roads[static_cast<std::size_t>(slot.roadId)];
        RoadSearchStats& roadStats = statsFor(searchLog, slot.roadId, slot.centerDist);

        const RoadSideFrontage* sideData = road.sideBank(slot.bankIndex);
        if (sideData == nullptr || sideData->inward.length() < 1e-4f) {
            ++searchLog.noInwardSkipped;
            if (logProbes) {
                logSegmentProbe(out.id, slot, "no_inward");
            }
            continue;
        }
        if (!useFrontier && slot.zoneScore > 1e8f) {
            ++searchLog.zoneFiltered;
            if (logProbes) {
                logSegmentProbe(out.id, slot, "zone_inner");
            }
            continue;
        }

        Vec2 a{};
        Vec2 b{};
        Vec2 edgeDir{};
        if (!roadFrameForBank(road, slot.bankIndex, a, b, edgeDir)) {
            ++searchLog.orientFailedSkipped;
            if (logProbes) {
                logSegmentProbe(out.id, slot, "orient_failed");
            }
            continue;
        }

        ++roadStats.roadsChecked;

        const Vec2 sortCenter = town.center;
        SegmentPlacementResult attempt{};
        {
            PROFILE_SCOPE(ProfileScopeId::PlotTrySegment);
            attempt = trySegmentPositions(
                town, slot, buildingType, defs, plots, a, edgeDir, sideData->inward, prep.targetArea,
                orientOrder, out.id, sortCenter, terrain, &prep.plotAreaBand);
        }

        if (!attempt.placed) {
            ++searchLog.dimFailedSegments;
            recordDimReject(roadStats, attempt.lastReject);
            const float frontageNeed =
                bandMinFrontage(defs, buildingType, slot.width(), plots.maxDepthToFrontRatio);
            if (logProbes) {
                logSegmentProbe(out.id, slot, attempt.failReason, attempt.lastReject,
                                attempt.lastDepthCap, frontageNeed, -1.f);
                Logger::log("probe", "segment_slide: placement #" + std::to_string(out.id) + " seg="
                                         + std::to_string(slot.segmentId) + " tries="
                                         + std::to_string(attempt.tries) + " result="
                                         + attempt.failReason);
            }
            continue;
        }

        if (logProbes) {
            logSegmentProbe(out.id, slot, "placed", DimReject::None, attempt.dims.depth,
                            attempt.dims.frontage, attempt.dims.area, attempt.slotT);
        }

        out.id            = attempt.plot.id;
        out.buildingType  = buildingType;
        out.placementMode = BuildingPlacementMode::PlotLot;
        out.roadId        = slot.roadId;
        out.roadBank      = slot.bankIndex;
        out.plot          = attempt.plot;
        {
            PROFILE_SCOPE(ProfileScopeId::PlotLayout);
            if (!layoutBuildingsOnPlot(out.plot, town, prep.buildingSpecs, out.id, townSeed,
                                       out.footprints)) {
                ++searchLog.dimFailedSegments;
                if (logProbes) {
                    logSegmentProbe(out.id, slot, "layout_main_failed", DimReject::None, -1.f, -1.f,
                                    -1.f);
                    Logger::log("probe", "segment_slide: placement #" + std::to_string(out.id)
                                             + " seg=" + std::to_string(slot.segmentId)
                                             + " result=layout_main_failed (plot fits but main "
                                               "building footprint does not)");
                }
                continue;
            }
        }

        bool footprintTerrainOk = true;
        for (const BuildingFootprint& footprint : out.footprints) {
            if (footprintPlacementValid(footprint, town, terrain, plots.frontageSetback,
                                        slot.roadId)) {
                continue;
            }
            if (terrain != nullptr && terrain->valid
                && !polygonBuildable(footprint.corners, *terrain)) {
                if (logProbes) {
                    logSegmentProbe(out.id, slot, "terrain_forbidden", DimReject::None, -1.f, -1.f,
                                    -1.f);
                }
                Logger::log("layout",
                            "terrain_reject: queueIndex=" + std::to_string(out.id) + " road="
                                + std::to_string(slot.roadId) + " reason=footprint");
            } else if (logProbes) {
                logSegmentProbe(out.id, slot, "layout_main_failed", DimReject::None, -1.f, -1.f,
                                -1.f);
            }
            footprintTerrainOk = false;
            break;
        }
        if (!footprintTerrainOk) {
            ++searchLog.dimFailedSegments;
            out.footprints.clear();
            continue;
        }

        ++roadStats.valid;
        ++searchLog.totalValid;
        roadStats.bestValidDist = std::min(
            roadStats.bestValidDist, (plotCenter(attempt.plot) - town.center).length());

        searchLog.layoutRequested = static_cast<int>(prep.buildingSpecs.size());
        searchLog.layoutPlaced  = static_cast<int>(out.footprints.size());

        carveRoadFrontageForPlot(town, attempt.plot, plots.frontageSetback);
        for (const BuildingFootprint& footprint : out.footprints) {
            if (footprint.mainBuilding) {
                carveRoadWallForFootprint(town, slot.roadId, slot.bankIndex, footprint);
            }
        }

        searchLog.chosenRoadCandidate = slot.roadId;
        searchLog.chosenRoad      = slot.roadId;
        searchLog.chosenSegment   = slot.segmentId;
        searchLog.chosenZoneScore = slot.zoneScore;
        searchLog.chosenDist      = (plotCenter(attempt.plot) - town.center).length();
        searchLog.chosenFrontage  = attempt.dims.frontage;
        searchLog.chosenDepth     = attempt.dims.depth;
        searchLog.chosenArea      = attempt.dims.area;
        searchLog.chosenOrient    = attempt.orient;
        searchLog.chosenCenter    = plotCenter(attempt.plot);
        searchLog.resultSummary =
            (useFrontier ? "frontier_closest_plot seg=" : "first_valid_segment_in_zone_order seg=")
            + std::to_string(slot.segmentId) + " road=" + std::to_string(slot.roadId)
            + " zone_score=" + fmt1(slot.zoneScore) + " center_dist=" + fmt1(slot.centerDist)
            + " orient=" + orientationName(attempt.orient)
            + (searchLog.layoutPlaced < searchLog.layoutRequested
                   ? " layout_partial=" + std::to_string(searchLog.layoutPlaced) + "/"
                         + std::to_string(searchLog.layoutRequested)
                   : "");
        return true;
    }

    if (searchLog.totalValid > 0) {
        searchLog.resultSummary =
            "unexpected: had valid_candidates=" + std::to_string(searchLog.totalValid)
            + " but none selected (internal error)";
    } else if (searchLog.slotsExamined == 0) {
        searchLog.resultSummary = "no_frontage_segments_available";
    } else if (searchLog.zoneFiltered == searchLog.slotsExamined) {
        searchLog.resultSummary =
            "all_segments_filtered_by_zone (growth=" + fmt1(searchLog.townGrowth) + " zone="
            + searchLog.zoneType + " min_center_dist=" + fmt1(searchLog.zoneBias) + ")";
    } else if (searchLog.dimFailedSegments > 0 && searchLog.zoneFiltered + searchLog.noInwardSkipped
                                                    + searchLog.orientFailedSkipped
                                               == searchLog.slotsExamined - searchLog.dimFailedSegments) {
        searchLog.resultSummary =
            "segments_reached_but_plot_geometry_failed (dim/orientation/cell/overlap)";
    } else {
        searchLog.resultSummary =
            "no_valid_plot_on_any_eligible_segment (zone_filtered=" + std::to_string(searchLog.zoneFiltered)
            + " dim_failed=" + std::to_string(searchLog.dimFailedSegments) + ")";
    }

    return false;
}
