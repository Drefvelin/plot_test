#include "placement/frontier/BorderFrontier.h"

#include "placement/frontage/FrontagePlacement.h"
#include "placement/zones/FrontageZones.h"
#include "placement/frontier/FrontierSlotUtils.h"
#include "util/Logger.h"
#include "placement/geometry/PlotDimensions.h"
#include "placement/geometry/PlotGeometry.h"
#include "util/Profile.h"
#include "terrain/Terrain.h"
#include "placement/terrain/TerrainPlacement.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace {
constexpr float kRayEps = 1e-3f;
}

struct BorderOutlineRayTry {
    bool          ok               = false;
    OutlineRayHit hit{};
    TerrainId     kind             = kTerrainUnknown;
    const char*   failReason       = nullptr;
    float         outlineDist      = -1.f;
    float         blockingRoadDist = -1.f;
};

float borderOutlineProbeMaxDist(const Town& town) {
    return std::max({town.width, town.height, town.radius * 2.f, 512.f}) + 64.f;
}

float nearestBlockingRoadDist(const Town& town, const Vec2& from, const Vec2& unitDir,
                              float maxDist, int sourceRoadId) {
    float nearest = maxDist + 1.f;
    for (const Road& road : town.roads) {
        if (road.id == sourceRoadId) {
            continue;
        }
        const float t = raySegmentHitDist(from, unitDir, road.a, road.b, maxDist);
        if (t > kRayEps && t < nearest) {
            nearest = t;
        }
    }
    return nearest;
}

BorderOutlineRayTry tryBorderOutlineRay(const Town& town, const Vec2& from, const Vec2& dir,
                                        const TerrainProbeConfig& probes,
                                        const TerrainAtlas& terrain, int sourceRoadId) {
    BorderOutlineRayTry result{};
    if (!terrain.valid || dir.length() < 1e-4f || probes.borderIds.empty()) {
        result.failReason = probes.borderIds.empty() ? "no_border_probe_ids" : "invalid_ray";
        return result;
    }

    const Vec2  unitDir  = dir.normalized();
    const float probeMax = borderOutlineProbeMaxDist(town);

    for (TerrainId kind : probes.borderIds) {
        if (!terrain.hasOutline(kind)) {
            continue;
        }
        OutlineRayHit hit{};
        if (!rayHitPreferOutline(from, dir, kind, terrain, probeMax, hit)) {
            continue;
        }
        if (!result.ok || hit.dist + kRayEps < result.hit.dist) {
            result.ok          = true;
            result.hit         = hit;
            result.kind        = kind;
            result.outlineDist = hit.dist;
        }
    }

    if (!result.ok) {
        result.failReason = "no_outline_hit";
        return result;
    }

    const float roadDist = nearestBlockingRoadDist(town, from, unitDir, probeMax, sourceRoadId);
    if (roadDist < result.hit.dist - kRayEps) {
        result.ok               = false;
        result.failReason       = "blocked_by_road";
        result.blockingRoadDist = roadDist;
        return result;
    }

    return result;
}

namespace {

constexpr float kDistEps = 1e-3f;

bool borderSlotRefLess(const BorderSlotRef& lhs, const BorderSlotRef& rhs) {
    return lhs.base.centerDist + kDistEps < rhs.base.centerDist;
}

void insertBorderSlot(std::vector<BorderSlotRef>& bucket, BorderSlotRef ref) {
    const auto it = std::lower_bound(bucket.begin(), bucket.end(), ref, borderSlotRefLess);
    bucket.insert(it, ref);
}

bool computeOutlineInwardAtHit(const Vec2& point, const Vec2& tangent, const TerrainAtlas& terrain,
                               Vec2& inwardOut) {
    if (!terrain.valid) {
        return false;
    }
    const Vec2 t = tangent.length() > 1e-4f ? tangent.normalized() : Vec2{1.f, 0.f};
    const Vec2 a = perpendicular(t);
    const Vec2 b = a * -1.f;
    if (terrain.isBuildable(point + a * 3.f)) {
        inwardOut = a;
        return true;
    }
    if (terrain.isBuildable(point + b * 3.f)) {
        inwardOut = b;
        return true;
    }
    return false;
}

const char* fillBorderHitMetadataChecked(TerrainId kind, const OutlineRayHit& hit,
                                         const TerrainAtlas& terrain, BorderSlotRef& ref) {
    const std::vector<std::vector<Vec2>>* graphs = terrain.outlineGraphs(kind);
    if (graphs == nullptr) {
        return "no_outline_graphs";
    }
    if (hit.graphIndex < 0 || hit.graphIndex >= static_cast<int>(graphs->size())) {
        return "invalid_graph_index";
    }
    const std::vector<Vec2>& graph = (*graphs)[static_cast<std::size_t>(hit.graphIndex)];
    Vec2                     tangent{};
    if (!samplePolylineGraphAtArc(graph, hit.polylineT, ref.outlinePoint, tangent)) {
        ref.outlinePoint = hit.point;
        tangent          = perpendicular(ref.outlinePoint).normalized();
    }
    ref.outlineTangent = tangent.length() > 1e-4f ? tangent.normalized() : Vec2{1.f, 0.f};
    if (!computeOutlineInwardAtHit(ref.outlinePoint, ref.outlineTangent, terrain,
                                   ref.outlineInward)) {
        return "outline_inward_not_buildable";
    }
    ref.terrainId  = kind;
    ref.graphIndex = hit.graphIndex;
    ref.arcT       = hit.polylineT;
    ref.hitDist    = hit.dist;
    return nullptr;
}

bool fillBorderHitMetadata(TerrainId kind, const OutlineRayHit& hit, const TerrainAtlas& terrain,
                           BorderSlotRef& ref) {
    return fillBorderHitMetadataChecked(kind, hit, terrain, ref) == nullptr;
}

struct BorderBankScanStats {
    int added    = 0;
    int rejected = 0;
};

void logBorderFrontierSegment(const Town& town, int generation, int roadId, int bankIndex,
                              const RoadFrontageSegment& segment, const char* event,
                              const std::string& detail) {
    std::ostringstream oss;
    oss << "border_frontier_" << event << ": gen=" << generation << " road=" << roadId
        << " bank=" << bankIndex << " seg=" << segment.id
        << " roadDist=" << fmt1(roadCenterDist(town, roadId))
        << " width=" << fmt1(segment.endT - segment.startT);
    if (!detail.empty()) {
        oss << ' ' << detail;
    }
    Logger::log("layout", oss.str());
}

BorderBankScanStats addBorderForBankImpl(Town& town, int roadId, int bankIndex,
                                         const TerrainAtlas& terrain, const TerrainCatalog& catalog,
                                         const TerrainProbeConfig& probes, int generation) {
    BorderBankScanStats stats{};
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return stats;
    }
    const Road&             road = town.roads[static_cast<std::size_t>(roadId)];
    const RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr) {
        return stats;
    }
    if (road.isBridge) {
        for (const RoadFrontageSegment& segment : side->segments) {
            logBorderFrontierSegment(town, generation, roadId, bankIndex, segment, "reject",
                                     "reason=bank_bridge");
            ++stats.rejected;
        }
        return stats;
    }
    if (side->inward.length() < 1e-4f) {
        for (const RoadFrontageSegment& segment : side->segments) {
            logBorderFrontierSegment(town, generation, roadId, bankIndex, segment, "reject",
                                     "reason=bank_no_inward");
            ++stats.rejected;
        }
        return stats;
    }

    const Vec2 inward = side->inward.normalized();

    for (const RoadFrontageSegment& segment : side->segments) {
        const float width = segment.endT - segment.startT;
        if (width + kDistEps < town.syncMinPlotFrontage) {
            logBorderFrontierSegment(
                town, generation, roadId, bankIndex, segment, "reject",
                "reason=segment_too_short minFrontage=" + fmt1(town.syncMinPlotFrontage));
            ++stats.rejected;
            continue;
        }

        FrontierRef base;
        base.roadId     = roadId;
        base.bankIndex  = bankIndex;
        base.segmentId  = segment.id;
        base.centerDist = roadCenterDist(town, roadId);
        base.startT     = segment.startT;
        base.endT       = segment.endT;

        Vec2 midpoint{};
        if (!segmentMidpointFromRef(town, base, midpoint)) {
            logBorderFrontierSegment(town, generation, roadId, bankIndex, segment, "reject",
                                     "reason=midpoint_fail");
            ++stats.rejected;
            continue;
        }

        const BorderOutlineRayTry outlineTry =
            tryBorderOutlineRay(town, midpoint, inward, probes, terrain, roadId);
        if (!outlineTry.ok) {
            std::ostringstream detail;
            detail << "reason=" << outlineTry.failReason;
            if (outlineTry.outlineDist >= 0.f) {
                detail << " outlineDist=" << fmt1(outlineTry.outlineDist);
            }
            if (outlineTry.blockingRoadDist >= 0.f) {
                detail << " roadDist=" << fmt1(outlineTry.blockingRoadDist);
            }
            logBorderFrontierSegment(town, generation, roadId, bankIndex, segment, "reject",
                                     detail.str());
            ++stats.rejected;
            continue;
        }

        BorderSlotRef ref;
        ref.base = base;
        if (const char* metaFail = fillBorderHitMetadataChecked(outlineTry.kind, outlineTry.hit,
                                                                terrain, ref)) {
            std::ostringstream detail;
            detail << "reason=hit_metadata_fail detail=" << metaFail << " kind="
                   << terrainIdName(outlineTry.kind, catalog)
                   << " hitDist=" << fmt1(outlineTry.hit.dist);
            logBorderFrontierSegment(town, generation, roadId, bankIndex, segment, "reject",
                                     detail.str());
            ++stats.rejected;
            continue;
        }

        insertBorderSlot(borderSlotsFor(town.frontierManager, catalog, outlineTry.kind), ref);
        logBorderFrontierSegment(
            town, generation, roadId, bankIndex, segment, "add",
            "prefer=" + std::string(terrainIdName(outlineTry.kind, catalog))
                + " hitDist=" + fmt1(ref.hitDist));
        ++stats.added;
    }

    return stats;
}

void addBorderForBank(Town& town, int roadId, int bankIndex, const TerrainAtlas& terrain,
                      const TerrainCatalog& catalog, const TerrainProbeConfig& probes) {
    addBorderForBankImpl(town, roadId, bankIndex, terrain, catalog, probes,
                         static_cast<int>(town.frontierManager.generation));
}

void removeBorderForBank(Town& town, int roadId, int bankIndex) {
    for (std::vector<BorderSlotRef>& bucket : town.frontierManager.border) {
        bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                    [roadId, bankIndex](const BorderSlotRef& ref) {
                                        return ref.base.roadId == roadId
                                               && ref.base.bankIndex == bankIndex;
                                    }),
                     bucket.end());
    }
}

void refreshBorderBank(Town& town, int roadId, int bankIndex, const TerrainAtlas& terrain,
                       const TerrainCatalog& catalog, const TerrainProbeConfig& probes) {
    removeBorderForBank(town, roadId, bankIndex);
    const BorderBankScanStats stats =
        addBorderForBankImpl(town, roadId, bankIndex, terrain, catalog, probes,
                             static_cast<int>(town.frontierManager.generation));
    Logger::log("layout",
                "border_frontier_refresh: gen=" + std::to_string(town.frontierManager.generation)
                    + " road=" + std::to_string(roadId) + " bank=" + std::to_string(bankIndex)
                    + " added=" + std::to_string(stats.added)
                    + " rejected=" + std::to_string(stats.rejected));
}

}  // namespace

const std::vector<BorderSlotRef>& borderSlotsFor(const FrontierManager& mgr,
                                                 const TerrainCatalog& catalog, TerrainId id) {
    const std::size_t idx = catalog.denseIndex(id);
    if (idx >= mgr.border.size()) {
        static const std::vector<BorderSlotRef> empty;
        return empty;
    }
    return mgr.border[idx];
}

std::vector<BorderSlotRef>& borderSlotsFor(FrontierManager& mgr, const TerrainCatalog& catalog,
                                           TerrainId id) {
    const std::size_t idx = catalog.denseIndex(id);
    if (idx >= mgr.border.size()) {
        static std::vector<BorderSlotRef> empty;
        return empty;
    }
    return mgr.border[idx];
}

void rebuildBorderFrontierImpl(Town& town, const TerrainAtlas& terrain,
                               const TerrainCatalog& catalog, const TerrainProbeConfig& probes) {
    PROFILE_SCOPE(ProfileScopeId::RebuildBorderFrontier);
    ensureFrontierBucketSize(town.frontierManager, catalog.count());
    for (std::vector<BorderSlotRef>& bucket : town.frontierManager.border) {
        bucket.clear();
    }

    if (!terrain.valid) {
        ++town.frontierManager.generation;
        Logger::log("layout",
                    "border_frontier_rebuild: gen=" + std::to_string(town.frontierManager.generation)
                        + " reason=terrain_invalid added=0 rejected=0");
        return;
    }

    const int               logGen = static_cast<int>(town.frontierManager.generation) + 1;
    BorderBankScanStats     total{};
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            const BorderBankScanStats bank =
                addBorderForBankImpl(town, road.id, bankIndex, terrain, catalog, probes, logGen);
            total.added += bank.added;
            total.rejected += bank.rejected;
        }
    }

    town.frontierManager.generation = static_cast<std::uint32_t>(logGen);
    Logger::log("layout",
                "border_frontier_rebuild: gen=" + std::to_string(logGen)
                    + " trigger=full added=" + std::to_string(total.added)
                    + " rejected=" + std::to_string(total.rejected));
}

bool peekNextBorderSlot(const Town& town, const BuildingDef& def, const TerrainAtlas& terrain,
                        const BandFilter& bandFilter, const std::unordered_set<int>& skipSegmentIds,
                        BorderSlotRef& outRef, FrontageSlot& outSlot) {
    if (town.syncTerrainCatalog == nullptr) {
        return false;
    }

    const std::vector<TerrainId> preferIds =
        !def.terrain.preferKinds.empty() ? def.terrain.preferKinds
                                         : std::vector<TerrainId>{def.terrain.prefer};

    bool  found     = false;
    float bestDist  = 1e30f;

    for (TerrainId kind : preferIds) {
        const std::vector<BorderSlotRef>& bucket =
            borderSlotsFor(town.frontierManager, *town.syncTerrainCatalog, kind);
        for (const BorderSlotRef& ref : bucket) {
            if (skipSegmentIds.count(ref.base.segmentId) != 0) {
                continue;
            }
            if (!distInFilter(ref.base.centerDist, bandFilter)) {
                continue;
            }
            if (frontierRefWidth(town, ref.base) + kDistEps < town.syncMinPlotFrontage) {
                continue;
            }
            if (!found || ref.base.centerDist + kDistEps < bestDist) {
                found    = true;
                bestDist = ref.base.centerDist;
                outRef   = ref;
            }
        }
    }

    if (!found) {
        return false;
    }

    fillFrontageSlotFromRefFields(town, outRef.base, outSlot);
    return true;
}

void borderFrontierOnPlotCarved(Town& town, int roadId, int bankIndex, const TerrainAtlas* terrain,
                                const TerrainCatalog* catalog, const TerrainProbeConfig* probes) {
    if (terrain == nullptr || !terrain->valid || catalog == nullptr || probes == nullptr) {
        return;
    }
    refreshBorderBank(town, roadId, bankIndex, *terrain, *catalog, *probes);
}

void consumeBorderSlot(Town& town, const BorderSlotRef& ref, const TerrainCatalog& catalog) {
    for (TerrainId kind : {ref.terrainId}) {
        std::vector<BorderSlotRef>& bucket = borderSlotsFor(town.frontierManager, catalog, kind);
        bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                    [&ref](const BorderSlotRef& entry) {
                                        return entry.base.segmentId == ref.base.segmentId;
                                    }),
                     bucket.end());
    }
}
