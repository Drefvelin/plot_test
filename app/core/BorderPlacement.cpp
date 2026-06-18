#include "BorderPlacement.h"

#include "BorderFrontier.h"
#include "BuildingLayout.h"
#include "FrontagePlacement.h"
#include "FrontageZones.h"
#include "Logger.h"
#include "PlotDimensions.h"
#include "PlotGeometry.h"
#include "Profile.h"
#include "Terrain.h"
#include "TerrainPlacement.h"
#include "TerrainPlacementLogging.h"

#include <cmath>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace {

struct BorderPlotTryResult {
    bool        ok           = false;
    Plot        plot{};
    float       slotT        = 0.f;
    const char* failReason   = "no_plot_candidate";
    DimReject   lastReject   = DimReject::None;
    int         tries        = 0;
};

struct BorderAttemptDiag {
    const char*          plotReason     = nullptr;
    const char*          buildingReason = nullptr;
    DimReject            dimReject      = DimReject::None;
    int                  plotTries      = 0;
    bool                 plotOk         = false;
    bool                 triedFootprint = false;
    BorderHugRejectStats mainRejectStats{};
    BorderHugRejectStats slotRejectStats{};
};

struct BorderAttemptResult {
    bool              placed = false;
    bool              hasPlot = false;
    Plot              plot{};
    BuildingFootprint mainFootprint{};
    BorderSlotRef     slot{};
    float             slotT = 0.f;
};

void notePlotFail(BorderPlotTryResult& result, const char* reason, DimReject reject = DimReject::None) {
    result.failReason = reason;
    if (reject != DimReject::None) {
        result.lastReject = reject;
    }
}

Vec2 bankInwardFromSlot(const Town& town, const BorderSlotRef& slot) {
    if (slot.base.roadId < 0 || slot.base.roadId >= static_cast<int>(town.roads.size())) {
        return slot.outlineInward * -1.f;
    }
    const Road&             road = town.roads[static_cast<std::size_t>(slot.base.roadId)];
    const RoadSideFrontage* side = road.sideBank(slot.base.bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f) {
        return slot.outlineInward * -1.f;
    }
    return side->inward.normalized();
}

BorderPlotTryResult tryBorderBandPlot(Town& town, const FrontageSlot& slot,
                                      const BorderSlotRef& borderRef, const DefCache& defs,
                                      const std::string& buildingType, const PlotConfig& plots,
                                      float targetArea, int buildingId, const TerrainAtlas& terrain,
                                      const BuildingTerrainRules& rules) {
    BorderPlotTryResult result{};
    if (slot.roadId < 0 || slot.roadId >= static_cast<int>(town.roads.size())) {
        notePlotFail(result, "invalid_slot");
        return result;
    }
    const Road& road = town.roads[static_cast<std::size_t>(slot.roadId)];
    Vec2        origin{};
    Vec2        farEnd{};
    Vec2        edgeDir{};
    if (!roadFrameForBank(road, slot.bankIndex, origin, farEnd, edgeDir)) {
        notePlotFail(result, "invalid_road_frame");
        return result;
    }
    const RoadSideFrontage* side = road.sideBank(slot.bankIndex);
    if (side == nullptr) {
        notePlotFail(result, "no_bank");
        return result;
    }
    const Vec2 sideInward = side->inward.normalized();

    const float minFrontage =
        bandMinFrontage(defs, buildingType, slot.width(), plots.maxDepthToFrontRatio);
    std::vector<float> tCandidates;
    buildSegmentTCandidates(slot, minFrontage, origin, edgeDir, town.center, tCandidates);
    if (tCandidates.empty()) {
        notePlotFail(result, "segment_too_short");
        return result;
    }

    const PlotOrientation orientOrder[] = {PlotOrientation::Horizontal, PlotOrientation::Vertical};
    const SizeBand*       plotBand      = defs.plotSizeBandForBuilding(buildingType);

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
                &reject, plotBand);
            ++result.tries;
            if (!dims.valid || dims.frontage > roomFront + 1e-3f) {
                notePlotFail(result, "dim_fail", reject);
                continue;
            }

            Plot candidate{};
            candidate.id       = buildingId;
            candidate.roadId   = slot.roadId;
            candidate.roadBank = slot.bankIndex;
            buildRoadPlot(roadStart, edgeDir, sideInward, plots.frontageSetback, dims.frontage,
                          dims.depth, candidate);

            if (!polygonBuildable(candidate.corners, terrain)) {
                notePlotFail(result, "terrain_forbidden");
                continue;
            }
            if (!plotPlacementValid(candidate, town, &terrain, plots.frontageSetback, slot.roadId)) {
                notePlotFail(result, "invalid_plot", DimReject::DepthExceedsRoadHit);
                continue;
            }
            if (!plotMeetsBorderBand(candidate, borderRef.terrainId, rules, terrain)) {
                notePlotFail(result, "border_band_fail");
                continue;
            }
            if (overlapsInstances(candidate, town.buildingInstances, town.relocatingInstanceId)) {
                notePlotFail(result, "overlap");
                continue;
            }
            const int excludeAlleyRoadId = road.isSecondary ? slot.roadId : -1;
            if (plotOverlapsAlleys(candidate, town, plots.frontageSetback, excludeAlleyRoadId)) {
                notePlotFail(result, "over_alley");
                continue;
            }

            result.ok   = true;
            result.plot = candidate;
            result.slotT = t;
            return result;
        }
    }

    return result;
}

BorderPlotTryResult tryBorderHugPlot(Town& town, const FrontageSlot& slot,
                                     const BorderSlotRef& borderRef, const DefCache& defs,
                                     const std::string& buildingType, const PlotConfig& plots,
                                     float targetArea, int buildingId, const TerrainAtlas& terrain,
                                     const BuildingTerrainRules& rules) {
    BorderPlotTryResult result{};
    if (slot.roadId < 0 || slot.roadId >= static_cast<int>(town.roads.size())) {
        notePlotFail(result, "invalid_slot");
        return result;
    }
    const Road& road = town.roads[static_cast<std::size_t>(slot.roadId)];
    Vec2        origin{};
    Vec2        farEnd{};
    Vec2        edgeDir{};
    if (!roadFrameForBank(road, slot.bankIndex, origin, farEnd, edgeDir)) {
        notePlotFail(result, "invalid_road_frame");
        return result;
    }
    const RoadSideFrontage* side = road.sideBank(slot.bankIndex);
    if (side == nullptr) {
        notePlotFail(result, "no_bank");
        return result;
    }
    const Vec2 sideInward = side->inward.normalized();

    const SizeBand* plotBand = defs.plotSizeBandForBuilding(buildingType);
    if (plotBand == nullptr) {
        notePlotFail(result, "missing_plot_band");
        return result;
    }

    const float minFrontage =
        bandMinFrontage(defs, buildingType, slot.width(), plots.maxDepthToFrontRatio);
    std::vector<float> tCandidates;
    buildSegmentTCandidates(slot, minFrontage, origin, edgeDir, town.center, tCandidates);
    if (tCandidates.empty()) {
        notePlotFail(result, "segment_too_short");
        return result;
    }

    const PlotOrientation orientOrder[] = {PlotOrientation::Horizontal, PlotOrientation::Vertical};

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
                &reject, plotBand);
            ++result.tries;
            if (!dims.valid || dims.frontage > roomFront + 1e-3f) {
                notePlotFail(result, "dim_fail", reject);
                continue;
            }

            Plot candidate{};
            if (!buildBorderHugPlot(roadStart, edgeDir, sideInward, sideInward, plots.frontageSetback,
                                    dims.frontage, borderRef.terrainId, terrain, *plotBand,
                                    candidate)) {
                notePlotFail(result, "hug_plot_build_fail");
                continue;
            }
            candidate.id       = buildingId;
            candidate.roadId   = slot.roadId;
            candidate.roadBank = slot.bankIndex;

            if (!polygonBuildableHugPlot(candidate.corners, terrain, borderRef.terrainId)) {
                notePlotFail(result, "hug_plot_buildable_fail");
                continue;
            }
            if (!plotPlacementValid(candidate, town, &terrain, plots.frontageSetback, slot.roadId,
                                    &rules)) {
                notePlotFail(result, "invalid_plot", DimReject::DepthExceedsRoadHit);
                continue;
            }
            if (!plotMeetsBorderHug(candidate, borderRef.terrainId, rules, terrain)) {
                notePlotFail(result, "border_hug_fail");
                continue;
            }
            if (overlapsInstances(candidate, town.buildingInstances, town.relocatingInstanceId)) {
                notePlotFail(result, "overlap");
                continue;
            }
            const int excludeAlleyRoadId = road.isSecondary ? slot.roadId : -1;
            if (plotOverlapsAlleys(candidate, town, plots.frontageSetback, excludeAlleyRoadId)) {
                notePlotFail(result, "over_alley");
                continue;
            }

            result.ok    = true;
            result.plot  = candidate;
            result.slotT = t;
            return result;
        }
    }

    return result;
}

void logBorderAttemptFail(int queueIndex, const std::string& typeName, bool hugStyle, int attempt,
                          int maxAttempts, const BorderSlotRef& borderRef,
                          const BorderAttemptDiag& diag, const TerrainCatalog& catalog) {
    std::ostringstream oss;
    oss << "border_attempt_fail: queueIndex=" << queueIndex
        << " type=" << typeName << " attempt=" << (attempt + 1) << "/" << maxAttempts
        << " style=" << (hugStyle ? "hug" : "band") << " road=" << borderRef.base.roadId
        << " bank=" << borderRef.base.bankIndex << " seg=" << borderRef.base.segmentId
        << " prefer=" << terrainIdName(borderRef.terrainId, catalog)
        << " centerDist=" << fmt1(borderRef.base.centerDist)
        << " hitDist=" << fmt1(borderRef.hitDist) << " plotTries=" << diag.plotTries;

    if (diag.plotOk) {
        oss << " plot=ok";
    } else if (diag.plotReason != nullptr) {
        oss << " plot=" << diag.plotReason;
        if (diag.dimReject != DimReject::None) {
            oss << "(" << rejectName(diag.dimReject) << ")";
        }
    }

    if (diag.buildingReason != nullptr) {
        oss << " building=" << diag.buildingReason;
    }
    if (diag.mainRejectStats.primary() != BorderHugReject::None) {
        oss << " main=" << formatBorderHugRejectStats(diag.mainRejectStats);
    }
    if (diag.triedFootprint && diag.slotRejectStats.primary() != BorderHugReject::None) {
        oss << " slot=" << formatBorderHugRejectStats(diag.slotRejectStats);
    }
    if (diag.triedFootprint) {
        oss << " footprint=attempted";
    }

    Logger::log("layout", oss.str());
}

void logBorderAttemptOk(int queueIndex, const std::string& typeName, bool hugStyle, int attempt,
                        int maxAttempts, const BorderSlotRef& borderRef,
                        const BorderAttemptDiag& diag, const TerrainCatalog& catalog) {
    std::ostringstream oss;
    oss << "border_attempt_ok: queueIndex=" << queueIndex << " type=" << typeName << " attempt="
        << (attempt + 1) << "/" << maxAttempts << " style=" << (hugStyle ? "hug" : "band")
        << " mode=border_plot road=" << borderRef.base.roadId << " bank=" << borderRef.base.bankIndex
        << " seg=" << borderRef.base.segmentId << " prefer="
        << terrainIdName(borderRef.terrainId, catalog)
        << " centerDist=" << fmt1(borderRef.base.centerDist)
        << " hitDist=" << fmt1(borderRef.hitDist) << " plotTries=" << diag.plotTries;
    if (diag.mainRejectStats.primary() != BorderHugReject::None) {
        oss << " main=" << formatBorderHugRejectStats(diag.mainRejectStats);
    }
    Logger::log("layout", oss.str());
}

void logBorderFrontierForAttempt(const Town& town, const BuildingDef& def,
                                 const BandFilter& bandFilter,
                                 const std::unordered_set<int>& skipSegmentIds, int queueIndex,
                                 int attempt, int maxAttempts, bool hugStyle,
                                 const BorderSlotRef* chosenRef) {
    if (town.syncTerrainCatalog == nullptr) {
        return;
    }

    const TerrainCatalog& catalog = *town.syncTerrainCatalog;
    const std::vector<TerrainId> preferIds =
        !def.terrain.preferKinds.empty() ? def.terrain.preferKinds
                                         : std::vector<TerrainId>{def.terrain.prefer};

    std::ostringstream preferKinds;
    for (std::size_t i = 0; i < preferIds.size(); ++i) {
        if (i > 0) {
            preferKinds << ',';
        }
        preferKinds << terrainIdName(preferIds[i], catalog);
    }

    int totalSlots  = 0;
    int peekEligible = 0;
    int ord         = 0;

    for (TerrainId kind : preferIds) {
        const std::vector<BorderSlotRef>& bucket = borderSlotsFor(town.frontierManager, catalog, kind);
        totalSlots += static_cast<int>(bucket.size());

        for (const BorderSlotRef& ref : bucket) {
            const float width   = ref.base.endT - ref.base.startT;
            const bool  skipped = skipSegmentIds.count(ref.base.segmentId) != 0;
            const bool  bandOk =
                !bandFilter.enabled || distInFilter(ref.base.centerDist, bandFilter);
            const bool widthOk = width + 1e-3f >= town.syncMinPlotFrontage;
            const bool peekOk  = !skipped && bandOk && widthOk;
            if (peekOk) {
                ++peekEligible;
            }

            const bool chosen =
                chosenRef != nullptr && chosenRef->base.segmentId == ref.base.segmentId
                && chosenRef->base.roadId == ref.base.roadId
                && chosenRef->base.bankIndex == ref.base.bankIndex;

            std::ostringstream seg;
            seg << "border_frontier_seg: queueIndex=" << queueIndex << " attempt=" << (attempt + 1)
                << "/" << maxAttempts << " style=" << (hugStyle ? "hug" : "band") << " ord="
                << (++ord) << " prefer=" << terrainIdName(kind, catalog)
                << " road=" << ref.base.roadId << " bank=" << ref.base.bankIndex
                << " seg=" << ref.base.segmentId << " centerDist=" << fmt1(ref.base.centerDist)
                << " hitDist=" << fmt1(ref.hitDist) << " width=" << fmt1(width) << " band="
                << (bandOk ? 1 : 0) << " skip=" << (skipped ? 1 : 0) << " width_ok="
                << (widthOk ? 1 : 0) << " peek_ok=" << (peekOk ? 1 : 0) << " chosen="
                << (chosen ? 1 : 0);
            Logger::log("layout", seg.str());
        }
    }

    std::ostringstream summary;
    summary << "border_frontier: queueIndex=" << queueIndex << " attempt=" << (attempt + 1) << "/"
            << maxAttempts << " style=" << (hugStyle ? "hug" : "band")
            << " gen=" << town.frontierManager.generation << " prefer=" << preferKinds.str()
            << " band=" << (bandFilter.enabled ? "on" : "off");
    if (bandFilter.enabled) {
        summary << " bandMin=" << fmt1(bandFilter.minDistInclusive)
                << " bandMax=" << fmt1(bandFilter.maxDistInclusive);
    }
    summary << " total=" << totalSlots << " peek_eligible=" << peekEligible;
    if (chosenRef != nullptr) {
        summary << " chosen_road=" << chosenRef->base.roadId
                << " chosen_bank=" << chosenRef->base.bankIndex
                << " chosen_seg=" << chosenRef->base.segmentId;
    } else {
        summary << " chosen=none";
    }
    Logger::log("layout", summary.str());
}

bool tryBorderAttempt(Town& town, const FrontageSlot& slot, const BorderSlotRef& borderRef,
                      const DefCache& defs, const PlotConfig& plots, const PlacementPrep& prep,
                      const ResolvedBuildingSpec& mainSpec, int buildingId, int townSeed,
                      const TerrainAtlas& terrain, const BuildingDef& def, bool hugStyle,
                      BorderAttemptResult& result, BorderAttemptDiag& diag) {
    diag = {};

    const std::string& buildingType = defs.typeName(def.typeId);
    const float        targetArea   = samplePlotTargetArea(defs, buildingType, buildingId, townSeed);

    const BorderPlotTryResult plotTry =
        hugStyle ? tryBorderHugPlot(town, slot, borderRef, defs, buildingType, plots, targetArea,
                                    buildingId, terrain, def.terrain)
                 : tryBorderBandPlot(town, slot, borderRef, defs, buildingType, plots, targetArea,
                                     buildingId, terrain, def.terrain);

    diag.plotReason = plotTry.failReason;
    diag.dimReject  = plotTry.lastReject;
    diag.plotTries  = plotTry.tries;
    diag.plotOk     = plotTry.ok;

    if (slot.roadId < 0 || slot.roadId >= static_cast<int>(town.roads.size())) {
        diag.buildingReason = "invalid_slot";
        return false;
    }
    const Road& road = town.roads[static_cast<std::size_t>(slot.roadId)];
    Vec2        origin{};
    Vec2        farEnd{};
    Vec2        edgeDir{};
    if (!roadFrameForBank(road, slot.bankIndex, origin, farEnd, edgeDir)) {
        diag.buildingReason = "invalid_road_frame";
        return false;
    }
    const RoadSideFrontage* side = road.sideBank(slot.bankIndex);
    if (side == nullptr) {
        diag.buildingReason = "no_bank";
        return false;
    }
    const Vec2 roadStart  = origin + edgeDir * plotTry.slotT;
    const Vec2 bankInward = side->inward.normalized();

    BuildingFootprint mainFootprint{};
    bool              buildingOk = false;
    if (!plotTry.ok) {
        if (hugStyle) {
            diag.triedFootprint = true;
            BuildingFootprint slotPreview{};
            tryMainBorderHugFromSlot(borderRef, plotTry.slotT, roadStart, edgeDir, bankInward,
                                     mainSpec, buildingId, townSeed, def.terrain, terrain, town,
                                     slotPreview, &diag.slotRejectStats);
        }
        diag.buildingReason = "plot_required";
        if (plotTry.failReason != nullptr) {
            diag.plotReason = plotTry.failReason;
        }
    } else {
        buildingOk =
            hugStyle ? tryMainBorderHugFromPlot(plotTry.plot, borderRef, mainSpec, buildingId,
                                                townSeed, def.terrain, terrain, town, mainFootprint,
                                                &diag.mainRejectStats)
                     : tryMainBorderBandOnPlot(plotTry.plot, mainSpec, buildingId, townSeed,
                                               mainFootprint);
        if (!buildingOk) {
            diag.buildingReason =
                hugStyle ? "main_hug_on_plot_fail" : "main_band_on_plot_fail";
        }
    }

    if (!buildingOk) {
        return false;
    }

    result.placed        = true;
    result.hasPlot       = true;
    result.plot          = plotTry.plot;
    result.mainFootprint = mainFootprint;
    result.slot          = borderRef;
    result.slotT         = plotTry.slotT;
    return true;
}

void commitBorderPlacement(Town& town, BuildingInstance& instance, const BorderAttemptResult& attempt,
                           const BuildingDef& def, const DefCache& defs, const PlotConfig& plots,
                           const TerrainAtlas& terrain, const PlacementPrep& prep,
                           const ResolvedBuildingSpec& mainSpec, int townSeed, bool hugStyle) {
    instance.footprints.clear();
    instance.footprints.push_back(attempt.mainFootprint);
    instance.footprints.front().labelId = 0;
    instance.roadId                     = attempt.slot.base.roadId;
    instance.roadBank                   = attempt.slot.base.bankIndex;

    const char* styleTag = hugStyle ? "hug" : "band";

    if (attempt.hasPlot) {
        instance.placementMode = BuildingPlacementMode::BorderPlot;
        instance.plot          = attempt.plot;
        instance.plot.id       = instance.id;

        assignBuildingDoorEdge(instance.footprints.front(), instance.plot, town, mainSpec.rules,
                               instance.id);
        layoutSecondaryBuildingsOnPlot(instance.plot, town, prep.buildingSpecs, instance.id,
                                       townSeed, instance.footprints);

        carveRoadFrontageForPlot(town, instance.plot, plots.frontageSetback, &terrain, true);
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (footprint.mainBuilding) {
                carveRoadWallForFootprint(town, instance.plot.roadId, instance.plot.roadBank,
                                          footprint, &terrain, false);
                break;
            }
        }

        recordTerrainAnchorRoad(town, attempt.slot.terrainId, instance.plot.roadId, instance.id,
                                "border");
        Logger::log("layout",
                    "border_plot_ok: queueIndex=" + std::to_string(instance.id) + " type="
                        + defs.typeName(instance.typeId) + " road="
                        + std::to_string(instance.plot.roadId) + " style=" + styleTag);
    } else {
        instance.placementMode = BuildingPlacementMode::BorderBuilding;
        instance.plot          = Plot{};
        instance.plot.id       = instance.id;

        assignDoorEdgeTowardHint(instance.footprints.front(), bankInwardFromSlot(town, attempt.slot),
                                 mainSpec.rules, instance.id);

        recordTerrainAnchorRoad(town, attempt.slot.terrainId, instance.roadId, instance.id,
                                "border");
        Logger::log("layout",
                    "border_building_ok: queueIndex=" + std::to_string(instance.id) + " type="
                        + defs.typeName(instance.typeId) + " road="
                        + std::to_string(instance.roadId) + " style=" + styleTag);
    }

    if (town.syncTerrainCatalog != nullptr) {
        consumeBorderSlot(town, attempt.slot, *town.syncTerrainCatalog);
    }

    Logger::log("layout",
                "border_place: queueIndex=" + std::to_string(instance.id) + " type="
                    + defs.typeName(instance.typeId) + " road=" + std::to_string(instance.roadId)
                    + " style=" + styleTag + " mode="
                    + (attempt.hasPlot ? "border_plot" : "border_building") + " prefer="
                    + terrainIdName(attempt.slot.terrainId, *town.syncTerrainCatalog));
}

bool runBorderAttemptLoop(Town& town, BuildingInstance& instance, const DefCache& defs,
                          const PlotConfig& plots, const PlacementPrep& prep,
                          const ResolvedBuildingSpec& mainSpec, int townSeed,
                          const TerrainAtlas& terrain, const BuildingDef& def,
                          const BandFilter& bandFilter, bool hugStyle,
                          TerrainPlacementTrace* trace) {
    if (town.syncTerrainCatalog == nullptr) {
        return false;
    }

    std::unordered_set<int> skipSegmentIds;
    int                     attempts = 0;
    const int               maxAttempts = std::max(1, town.syncBorderMaxAttempts);
    const std::string       typeName  = defs.typeName(instance.typeId);

    while (attempts < maxAttempts) {
        BorderSlotRef borderRef{};
        FrontageSlot  slot{};
        if (!peekNextBorderSlot(town, def, terrain, bandFilter, skipSegmentIds, borderRef, slot)) {
            logBorderFrontierForAttempt(town, def, bandFilter, skipSegmentIds, instance.id,
                                        attempts, maxAttempts, hugStyle, nullptr);
            Logger::log("layout",
                        "border_attempt_fail: queueIndex=" + std::to_string(instance.id)
                            + " type=" + typeName + " attempt=" + std::to_string(attempts + 1)
                            + "/" + std::to_string(maxAttempts) + " style="
                            + (hugStyle ? "hug" : "band") + " reason="
                            + (attempts == 0 ? "no_border_slot" : "no_more_border_slots"));
            break;
        }

        BorderAttemptResult attempt{};
        BorderAttemptDiag   diag{};
        attempt.slot = borderRef;
        logBorderFrontierForAttempt(town, def, bandFilter, skipSegmentIds, instance.id, attempts,
                                    maxAttempts, hugStyle, &borderRef);
        if (tryBorderAttempt(town, slot, borderRef, defs, plots, prep, mainSpec, instance.id,
                             townSeed, terrain, def, hugStyle, attempt, diag)) {
            logBorderAttemptOk(instance.id, typeName, hugStyle, attempts, maxAttempts, borderRef,
                               diag, *town.syncTerrainCatalog);
            commitBorderPlacement(town, instance, attempt, def, defs, plots, terrain, prep, mainSpec,
                                  townSeed, hugStyle);
            if (trace != nullptr) {
                logTerrainTraceBorderSummary(*trace, attempts + 1, 0, 0, 0, 0, 0, 0, 0, 0, 1);
            }
            return true;
        }

        logBorderAttemptFail(instance.id, typeName, hugStyle, attempts, maxAttempts, borderRef, diag,
                             *town.syncTerrainCatalog);

        skipSegmentIds.insert(borderRef.base.segmentId);
        ++attempts;
    }

    if (trace != nullptr) {
        logTerrainTraceBorderSummary(*trace, attempts, 0, 0, attempts, 0, 0, 0, 0, 0, 0);
    }
    return false;
}

}  // namespace

bool tryPlaceBorderPlot(Town& town, BuildingInstance& instance, const DefCache& defs,
                        const PlotConfig& plots, const PlacementPrep& prep, int townSeed,
                        const TerrainAtlas& terrain, const BandFilter& bandFilter,
                        TerrainPlacementTrace* trace, bool forceBandStyle) {
    PROFILE_SCOPE(ProfileScopeId::TerrainBorderPlace);
    const BuildingDef* def = defs.building(instance.typeId);
    if (def == nullptr || def->terrain.placement != TerrainPlacementMode::Border
        || !terrain.valid || town.syncTerrainCatalog == nullptr) {
        return false;
    }

    ResolvedBuildingSpec mainSpec = prep.mainSpec;
    if (mainSpec.sizeCategory.empty()) {
        if (!resolveMainBuildingSpec(defs, defs.typeName(instance.typeId), instance.id, townSeed,
                                     mainSpec)) {
            return false;
        }
    }

    const bool hugStyle = !forceBandStyle && def->terrain.borderStyle == BorderStyle::Hug;

    if (runBorderAttemptLoop(town, instance, defs, plots, prep, mainSpec, townSeed, terrain, *def,
                             bandFilter, hugStyle, trace)) {
        return true;
    }

    if (def->terrain.requirement == TerrainRequirement::Loose && def->terrain.borderStyle == BorderStyle::Hug
        && !forceBandStyle
        && runBorderAttemptLoop(town, instance, defs, plots, prep, mainSpec, townSeed, terrain,
                                *def, bandFilter, false, trace)) {
        return true;
    }

    Logger::log("layout",
                "border_fail: queueIndex=" + std::to_string(instance.id) + " type="
                    + defs.typeName(instance.typeId) + " style=" + (hugStyle ? "hug" : "band")
                    + " reason=exhausted attempts=" + std::to_string(town.syncBorderMaxAttempts));
    return false;
}
