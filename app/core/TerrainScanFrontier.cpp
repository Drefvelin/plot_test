#include "TerrainScanFrontier.h"

#include "FrontagePlacement.h"
#include "FrontageZones.h"
#include "PlotGeometry.h"
#include "Profile.h"
#include "TerrainPlacement.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>

namespace {

constexpr float kDistEps               = 1e-3f;
constexpr float kMaxProximityBootstrap = 25.f;
constexpr float kNoOutlineDist         = 1e30f;

float frontierRefWidth(const Town& town, const FrontierRef& ref) {
    if (ref.roadId < 0 || ref.roadId >= static_cast<int>(town.roads.size())) {
        return 0.f;
    }
    const Road&             road = town.roads[static_cast<std::size_t>(ref.roadId)];
    RoadFrontageSegment segment;
    segment.startT = ref.startT;
    segment.endT   = ref.endT;
    return (segment.endT - segment.startT);
}

void fillFrontageSlotFromRefFields(const Town& town, const FrontierRef& ref, FrontageSlot& slot) {
    slot.roadId     = ref.roadId;
    slot.bankIndex  = ref.bankIndex;
    slot.segmentId  = ref.segmentId;
    slot.centerDist = ref.centerDist;
    if (ref.roadId < 0 || ref.roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    const Road&             road = town.roads[static_cast<std::size_t>(ref.roadId)];
    RoadFrontageSegment segment;
    segment.startT = ref.startT;
    segment.endT   = ref.endT;
    slot.startT    = segment.startT;
    slot.endT      = segment.endT;
}

bool distInBand(float centerDist, const BandFilter& filter) {
    if (!filter.enabled) {
        return true;
    }
    return distInFilter(centerDist, filter);
}

bool slotRefLess(const TerrainScanSlotRef& lhs, const TerrainScanSlotRef& rhs) {
    const float lhsEdge = lhs.edgeDist;
    const float rhsEdge = rhs.edgeDist;
    if (std::abs(lhsEdge - rhsEdge) > kDistEps) {
        return lhsEdge < rhsEdge;
    }
    return lhs.base.centerDist + kDistEps < rhs.base.centerDist;
}

void insertScanSlot(std::vector<TerrainScanSlotRef>& bucket, TerrainScanSlotRef ref) {
    const auto it = std::lower_bound(bucket.begin(), bucket.end(), ref, slotRefLess);
    bucket.insert(it, ref);
}

bool segmentMidpointFromRef(const Town& town, const FrontierRef& ref, Vec2& out) {
    FrontageSlot slot;
    fillFrontageSlotFromRefFields(town, ref, slot);
    return segmentMidpoint(town, slot, out);
}

bool segmentInsideEligible(const Vec2& midpoint, TerrainId prefer, const TerrainAtlas& terrain) {
    if (!terrain.valid) {
        return false;
    }
    return terrainKindMatchesPrefer(terrain.sample(midpoint), prefer);
}

bool scanInsideBootstrapOnly(const TerrainProbeConfig& probes, TerrainId kind) {
    for (TerrainId id : probes.scanInsideIds) {
        if (id == kind) {
            return true;
        }
    }
    return false;
}

void addTerrainScanForBank(Town& town, int roadId, int bankIndex, const TerrainAtlas& terrain,
                           const TerrainCatalog& catalog, const TerrainProbeConfig& probes) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    const Road&             road = town.roads[static_cast<std::size_t>(roadId)];
    const RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f || road.isBridge) {
        return;
    }

    for (const RoadFrontageSegment& segment : side->segments) {
        if ((segment.endT - segment.startT) + kDistEps < town.syncMinPlotFrontage) {
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
            continue;
        }

        for (TerrainId kind : probes.scanIds) {
            const bool  insideOnly = scanInsideBootstrapOnly(probes, kind);
            float       edgeDist   = distToPreferEdge(midpoint, kind, terrain);
            const bool  noOutline  = edgeDist >= kNoOutlineDist * 0.5f;

            if (insideOnly) {
                // Majority land (e.g. plains) has no outline graph; inside farms still need slots.
                if (noOutline) {
                    edgeDist = 0.f;
                }
            } else if (noOutline) {
                continue;
            }

            TerrainScanSlotRef ref;
            ref.base           = base;
            ref.edgeDist       = edgeDist;
            ref.insideEligible = segmentInsideEligible(midpoint, kind, terrain);

            if (insideOnly) {
                if (!ref.insideEligible) {
                    continue;
                }
            } else if (edgeDist > kMaxProximityBootstrap + kDistEps) {
                continue;
            }

            insertScanSlot(scanSlotsFor(town.frontierManager, catalog, kind), ref);
        }
    }
}

void removeTerrainScanForBank(Town& town, int roadId, int bankIndex,
                              const TerrainCatalog& catalog) {
    const auto eraseBank = [roadId, bankIndex](std::vector<TerrainScanSlotRef>& bucket) {
        bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                    [roadId, bankIndex](const TerrainScanSlotRef& ref) {
                                        return ref.base.roadId == roadId
                                               && ref.base.bankIndex == bankIndex;
                                    }),
                     bucket.end());
    };

    for (std::vector<TerrainScanSlotRef>& bucket : town.frontierManager.scan) {
        eraseBank(bucket);
    }
}

void refreshTerrainScanBank(Town& town, int roadId, int bankIndex, const TerrainAtlas& terrain,
                            const TerrainCatalog& catalog, const TerrainProbeConfig& probes) {
    removeTerrainScanForBank(town, roadId, bankIndex, catalog);
    addTerrainScanForBank(town, roadId, bankIndex, terrain, catalog, probes);
}

}  // namespace

void rebuildTerrainScanFrontierImpl(Town& town, const TerrainAtlas& terrain,
                                    const TerrainCatalog& catalog,
                                    const TerrainProbeConfig& probes) {
    PROFILE_SCOPE(ProfileScopeId::RebuildTerrainScanFrontier);
    ensureFrontierBucketSize(town.frontierManager, catalog.count());
    for (std::vector<TerrainScanSlotRef>& bucket : town.frontierManager.scan) {
        bucket.clear();
    }

    if (!terrain.valid) {
        ++town.frontierManager.generation;
        return;
    }

    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            addTerrainScanForBank(town, road.id, bankIndex, terrain, catalog, probes);
        }
    }

    ++town.frontierManager.generation;
}

bool peekNextTerrainScanSlot(const Town& town, const BuildingDef& def, const TerrainAtlas& terrain,
                             const BandFilter& bandFilter,
                             const std::unordered_set<int>& skipSegmentIds,
                             TerrainScanSlotRef& outRef, FrontageSlot& outSlot, float& outScore) {
    PROFILE_SCOPE(ProfileScopeId::TerrainScanPeek);
    if (town.syncTerrainCatalog == nullptr) {
        return false;
    }

    const TerrainId prefer = def.terrain.prefer;
    const std::vector<TerrainScanSlotRef>& bucket =
        scanSlotsFor(town.frontierManager, *town.syncTerrainCatalog, prefer);
    if (bucket.empty()) {
        return false;
    }

    struct ScanCandidate {
        TerrainScanSlotRef ref;
        FrontageSlot       slot;
        float              score = 0.f;
    };
    std::vector<ScanCandidate> eligible;

    for (const TerrainScanSlotRef& ref : bucket) {
        if (skipSegmentIds.count(ref.base.segmentId) != 0) {
            continue;
        }
        if (town.relocatingHostRoadId >= 0 && ref.base.roadId == town.relocatingHostRoadId) {
            continue;
        }
        if (!distInBand(ref.base.centerDist, bandFilter)) {
            continue;
        }
        if (frontierRefWidth(town, ref.base) + kDistEps < town.syncMinPlotFrontage) {
            continue;
        }

        FrontageSlot slot;
        fillFrontageSlotFromRefFields(town, ref.base, slot);

        if (effectiveTerrainMode(def, terrain) == TerrainPlacementMode::Inside) {
            if (!ref.insideEligible) {
                continue;
            }
        } else if (effectiveTerrainMode(def, terrain) == TerrainPlacementMode::Proximity) {
            if (ref.edgeDist > def.terrain.proximityMaxDist + kDistEps) {
                continue;
            }
        }

        if (!segmentWithinProximityMax(town, slot, def, terrain)) {
            continue;
        }

        const float terrainPart = segmentTerrainScore(town, slot, def, terrain);
        if (terrainPart >= 1e8f) {
            continue;
        }
        eligible.push_back({ref, slot, terrainPart + ref.base.centerDist * 0.001f});
    }

    if (eligible.empty()) {
        return false;
    }

    const ScanCandidate* pick = &eligible[0];
    if (town.relocatingInstanceId != 0xFFFFFFFFu && eligible.size() > 1) {
        std::seed_seq seed{static_cast<int>(town.relocatingInstanceId), town.suburbanMaxHop,
                           static_cast<int>(prefer), 3181};
        std::mt19937 rng(seed);
        std::shuffle(eligible.begin(), eligible.end(), rng);
        pick = &eligible[0];
    } else {
        pick = &(*std::min_element(eligible.begin(), eligible.end(),
                                   [](const ScanCandidate& a, const ScanCandidate& b) {
                                       return a.score + kDistEps < b.score;
                                   }));
    }

    outRef   = pick->ref;
    outSlot  = pick->slot;
    outScore = pick->score;
    return true;
}

void terrainScanFrontierOnPlotCarved(Town& town, int roadId, int bankIndex,
                                     const TerrainAtlas* terrain, const TerrainCatalog* catalog,
                                     const TerrainProbeConfig* probes) {
    if (terrain == nullptr || !terrain->valid || catalog == nullptr || probes == nullptr) {
        return;
    }
    refreshTerrainScanBank(town, roadId, bankIndex, *terrain, *catalog, *probes);
}

void terrainScanFrontierOnFullRebuild(Town& town, const TerrainAtlas* terrain,
                                      const TerrainCatalog* catalog,
                                      const TerrainProbeConfig* probes) {
    if (terrain == nullptr || !terrain->valid || catalog == nullptr || probes == nullptr) {
        return;
    }
    rebuildTerrainScanFrontierImpl(town, *terrain, *catalog, *probes);
}
