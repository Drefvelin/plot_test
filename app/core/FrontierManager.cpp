#include "FrontierManager.h"

#include "BorderFrontier.h"
#include "Config.h"
#include "FrontageZones.h"
#include "GrowthRings.h"
#include "PlacementFrontier.h"
#include "Profile.h"
#include "TerrainAtlas.h"
#include "TerrainScanFrontier.h"
#include "Town.h"

namespace {

void roadPlotOnBankChanged(Town& town, int roadId, int bankIndex) {
    frontierRefreshPlotBank(town, roadId, bankIndex);
    frontierRefreshWallBank(town, roadId, bankIndex);
    frontierRefreshAlleyBank(town, roadId, bankIndex, town.syncMinAlleyGapWidth);
}

void onPlotCarved(Town& town, const PlacementEvent& event, const TerrainAtlas* terrain,
                  const TerrainCatalog* catalog, const TerrainProbeConfig* probes,
                  const PlotConfig* /*plots*/) {
    roadPlotOnBankChanged(town, event.roadId, event.bankIndex);
    if (terrain != nullptr && terrain->valid && catalog != nullptr && probes != nullptr) {
        terrainScanFrontierOnPlotCarved(town, event.roadId, event.bankIndex, terrain, catalog,
                                        probes);
        borderFrontierOnPlotCarved(town, event.roadId, event.bankIndex, terrain, catalog, probes);
    }
}

void onFullRebuild(Town& town, const TerrainAtlas* terrain, const TerrainCatalog* catalog,
                   const TerrainProbeConfig* probes, const PlotConfig* /*plots*/) {
    {
        PROFILE_SCOPE(ProfileScopeId::RebuildPlacementFrontier);
        rebuildPlacementFrontier(town);
    }
    if (terrain != nullptr && terrain->valid && catalog != nullptr && probes != nullptr) {
        rebuildTerrainScanFrontier(town, *terrain, *catalog, *probes);
        rebuildBorderFrontierImpl(town, *terrain, *catalog, *probes);
    }
}

void onTopologyBulkRebuild(Town& town, const TerrainAtlas* terrain, const TerrainCatalog* catalog,
                           const TerrainProbeConfig* probes, const PlotConfig* /*plots*/) {
    {
        PROFILE_SCOPE(ProfileScopeId::RebuildPlacementFrontier);
        rebuildPlacementFrontier(town);
    }
    if (terrain != nullptr && terrain->valid && catalog != nullptr && probes != nullptr) {
        rebuildTerrainScanFrontier(town, *terrain, *catalog, *probes);
        rebuildBorderFrontierImpl(town, *terrain, *catalog, *probes);
    }
}

}  // namespace

bool findNearestRoadBankToPoint(const Town& town, const Vec2& p, NearestRoadBankSnap& out) {
    auto nearestOnSeg = [](const Vec2& pt, const Vec2& a, const Vec2& b) {
        const Vec2  ab    = b - a;
        const float lenSq = ab.x * ab.x + ab.y * ab.y;
        if (lenSq < 1e-8f) {
            return a;
        }
        const float t = std::clamp(((pt.x - a.x) * ab.x + (pt.y - a.y) * ab.y) / lenSq, 0.f, 1.f);
        return Vec2{a.x + ab.x * t, a.y + ab.y * t};
    };
    out = {};
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            const RoadSideFrontage* side = road.sideBank(bankIndex);
            if (side == nullptr || side->inward.length() < 1e-4f) {
                continue;
            }
            Vec2 origin{};
            Vec2 farEnd{};
            Vec2 edgeDir{};
            if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
                continue;
            }
            const Vec2  nearest = nearestOnSeg(p, origin, farEnd);
            const float dist      = (p - nearest).length();
            if (dist + 1e-3f >= out.dist) {
                continue;
            }
            const Vec2 inward = side->inward.normalized();
            if ((p - nearest).dot(inward) < -1.f) {
                continue;
            }
            out.valid      = true;
            out.roadId     = road.id;
            out.bankIndex  = bankIndex;
            out.roadStart  = nearest;
            out.edgeDir    = edgeDir.normalized();
            out.bankInward = inward;
            out.roadT      = (nearest - origin).dot(out.edgeDir);
            out.dist       = dist;
        }
    }
    return out.valid;
}

const std::vector<TerrainScanSlotRef>& scanSlotsFor(const FrontierManager& mgr,
                                                    const TerrainCatalog& catalog, TerrainId id) {
    const std::size_t idx = catalog.denseIndex(id);
    if (idx >= mgr.scan.size()) {
        static const std::vector<TerrainScanSlotRef> empty;
        return empty;
    }
    return mgr.scan[idx];
}

std::vector<TerrainScanSlotRef>& scanSlotsFor(FrontierManager& mgr, const TerrainCatalog& catalog,
                                              TerrainId id) {
    const std::size_t idx = catalog.denseIndex(id);
    if (idx >= mgr.scan.size()) {
        static std::vector<TerrainScanSlotRef> empty;
        return empty;
    }
    return mgr.scan[idx];
}

void ensureFrontierBucketSize(FrontierManager& mgr, std::size_t catalogCount) {
    if (mgr.scan.size() != catalogCount) {
        mgr.scan.assign(catalogCount, {});
    }
    if (mgr.border.size() != catalogCount) {
        mgr.border.assign(catalogCount, {});
    }
}

void rebuildTerrainScanFrontier(Town& town, const TerrainAtlas& terrain,
                                const TerrainCatalog& catalog,
                                const TerrainProbeConfig& probes) {
    rebuildTerrainScanFrontierImpl(town, terrain, catalog, probes);
}

void pushFrontierNotifySuppress(Town& town) { ++town.frontierNotifySuppressDepth; }

void popFrontierNotifySuppress(Town& town) {
    if (town.frontierNotifySuppressDepth > 0) {
        --town.frontierNotifySuppressDepth;
    }
}

bool frontierNotifySuppressed(const Town& town) { return town.frontierNotifySuppressDepth > 0; }

void notifyRoadFrontierRefresh(Town& town, int roadId, const TerrainAtlas* terrain,
                               const TerrainCatalog* catalog, const TerrainProbeConfig* probes,
                               const PlotConfig* plots) {
    if (roadId < 0) {
        return;
    }
    for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
        PlacementEvent ev;
        ev.type      = PlacementEventType::PlotCarved;
        ev.roadId    = roadId;
        ev.bankIndex = bankIndex;
        ev.carveTMin = 0.f;
        ev.carveTMax = 1e9f;
        notifyPlacementFrontier(town, ev, terrain, catalog, probes, plots);
    }
}

void notifyPlacementFrontier(Town& town, const PlacementEvent& event, const TerrainAtlas* terrain,
                             const TerrainCatalog* catalog, const TerrainProbeConfig* probes,
                             const PlotConfig* plots) {
    if (frontierNotifySuppressed(town)) {
        return;
    }

    switch (event.type) {
    case PlacementEventType::FullRebuild:
        onFullRebuild(town, terrain, catalog, probes, plots);
        break;

    case PlacementEventType::TopologyChanged:
        onTopologyBulkRebuild(town, terrain, catalog, probes, plots);
        break;

    case PlacementEventType::PlotCarved:
        if (event.roadId >= 0) {
            onPlotCarved(town, event, terrain, catalog, probes, plots);
        }
        break;

    case PlacementEventType::RingExtended:
        frontierExtendBands(town, event.prevSuburbanDist, event.prevCoreDist);
        break;

    case PlacementEventType::InstanceRemoved:
        if (event.roadId >= 0) {
            roadPlotOnBankChanged(town, event.roadId, event.bankIndex);
            if (terrain != nullptr && terrain->valid && catalog != nullptr && probes != nullptr) {
                onPlotCarved(town, event, terrain, catalog, probes, plots);
            }
        }
        break;
    }
}
