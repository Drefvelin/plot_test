// Waterside junction merging and bridge resolution. Public API declared in
// RoadNetwork.h; shared helpers in RoadNetworkInternal.h.

#include "roads/RoadNetwork.h"
#include "roads/RoadNetworkInternal.h"

#include "util/Logger.h"
#include "placement/geometry/PlotGeometry.h"
#include "terrain/TerrainAtlas.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace roadnet;

void mergeWatersideJunctions(Town& town, const TerrainAtlas& terrain, const Config& config) {
    const float mergeDist = config.terrain.shoreJunctionMergeDist;
    if (!terrain.valid || mergeDist <= 0.f) {
        return;
    }

    indexJunctions(town);

    std::vector<int> shoreIds;
    shoreIds.reserve(town.watersideJunctionIds.size());
    for (int junctionId : town.watersideJunctionIds) {
        if (junctionId >= 0 && junctionId < static_cast<int>(town.junctions.size())
            && junctionHasLandRoad(town, junctionId, terrain)) {
            shoreIds.push_back(junctionId);
        }
    }

    if (shoreIds.size() < 2) {
        collectWatersideJunctionIds(town, terrain, config.terrain.bridgeWatersideMaxDist);
        return;
    }

    (void)resolveBridgeSettings(config);

    const int junctionCount = static_cast<int>(town.junctions.size());
    std::vector<int> parent(static_cast<std::size_t>(junctionCount));
    for (int i = 0; i < junctionCount; ++i) {
        parent[static_cast<std::size_t>(i)] = i;
    }

    const float mergeDistSq = mergeDist * mergeDist;
    for (std::size_t i = 0; i < shoreIds.size(); ++i) {
        const Vec2& posI = town.junctions[static_cast<std::size_t>(shoreIds[i])].pos;
        for (std::size_t j = i + 1; j < shoreIds.size(); ++j) {
            const Vec2 delta = posI - town.junctions[static_cast<std::size_t>(shoreIds[j])].pos;
            if (delta.dot(delta) <= mergeDistSq) {
                uniteUnion(parent, shoreIds[i], shoreIds[j]);
            }
        }
    }

    std::vector<std::vector<int>> byRoot(static_cast<std::size_t>(junctionCount));
    for (int shoreId : shoreIds) {
        byRoot[static_cast<std::size_t>(findUnionRoot(parent, shoreId))].push_back(shoreId);
    }

    int mergedJunctions = 0;
    int clustersMerged  = 0;
    for (int root = 0; root < junctionCount; ++root) {
        const std::vector<int>& cluster = byRoot[static_cast<std::size_t>(root)];
        if (cluster.size() < 2) {
            continue;
        }
        ++clustersMerged;

        Vec2 centroid{0.f, 0.f};
        for (int junctionId : cluster) {
            const Vec2& pos = town.junctions[static_cast<std::size_t>(junctionId)].pos;
            centroid.x += pos.x;
            centroid.y += pos.y;
        }
        const float invCount = 1.f / static_cast<float>(cluster.size());
        centroid.x *= invCount;
        centroid.y *= invCount;

        for (int junctionId : cluster) {
            updateJunctionPosition(town, junctionId, centroid);
        }
        mergedJunctions += static_cast<int>(cluster.size()) - 1;

        std::ostringstream members;
        for (std::size_t i = 0; i < cluster.size(); ++i) {
            if (i > 0) {
                members << ',';
            }
            members << cluster[i];
        }
        Logger::log("bridge",
                    "merge_cluster members=" + members.str() + " centroid="
                        + std::to_string(centroid.x) + "," + std::to_string(centroid.y)
                        + " roads=" + junctionRoadIds(town, cluster[0]));
    }

    if (clustersMerged == 0) {
        Logger::log("bridge", "merge none merge_dist=" + std::to_string(mergeDist));
        return;
    }

    indexJunctions(town);
    const int removedRoads = removeDuplicateAndDegenerateRoads(town);

    Logger::log("voronoi",
                "shore_junction_merge clusters=" + std::to_string(clustersMerged)
                    + " merged_junctions=" + std::to_string(mergedJunctions) + " removed_roads="
                    + std::to_string(removedRoads) + " merge_dist="
                    + std::to_string(mergeDist));
    Logger::log("bridge",
                "merge_summary clusters=" + std::to_string(clustersMerged)
                    + " merged_junctions=" + std::to_string(mergedJunctions) + " removed_roads="
                    + std::to_string(removedRoads) + " merge_dist="
                    + std::to_string(mergeDist));

    collectWatersideJunctionIds(town, terrain, config.terrain.bridgeWatersideMaxDist);
}

void resolveBridges(Town& town, const TerrainAtlas& terrain, const Config& config) {
    if (!terrain.valid || !config.terrain.bridgesEnabled) {
        return;
    }

    Logger::log("bridge", "resolve_start max_span=" + std::to_string(config.terrain.bridgeMaxSpan)
                              + " merge_dist="
                              + std::to_string(config.terrain.shoreJunctionMergeDist)
                              + " waterside_junctions="
                              + std::to_string(town.watersideJunctionIds.size()));

    indexJunctions(town);

    const BridgeSettings settings = resolveBridgeSettings(config);

    std::vector<ShoreJunction> shoreJunctions;
    shoreJunctions.reserve(town.watersideJunctionIds.size());

    for (int junctionId : town.watersideJunctionIds) {
        if (junctionId < 0 || junctionId >= static_cast<int>(town.junctions.size())) {
            continue;
        }
        if (!junctionHasLandRoad(town, junctionId, terrain)) {
            continue;
        }
        const Junction& junction = town.junctions[static_cast<std::size_t>(junctionId)];
        const WaterBodyRef waterBody =
            nearestWatersideWaterBody(junction.pos, terrain, config.terrain.bridgeWatersideMaxDist);
        if (!waterBody.valid) {
            Logger::log("bridge",
                        "candidate_skip j=" + std::to_string(junctionId) + " reason=no_water_body");
            continue;
        }
        shoreJunctions.push_back({junctionId, waterBody});
        Logger::log("bridge",
                    "candidate j=" + std::to_string(junctionId) + " pos="
                        + junctionPosText(town, junctionId) + " roads="
                        + junctionRoadIds(town, junctionId) + " water="
                        + std::to_string(waterBody.graphIndex));
    }

    int shoreSea   = 0;
    int shoreRiver = 0;
    for (const ShoreJunction& shore : shoreJunctions) {
        if (terrain.catalog != nullptr && shore.waterBody.kind == terrain.catalog->resolveKey("sea")) {
            ++shoreSea;
        } else if (terrain.catalog != nullptr
                   && shore.waterBody.kind == terrain.catalog->resolveKey("river")) {
            ++shoreRiver;
        }
    }

    std::vector<BridgeCandidate> candidates;
    candidates.reserve(shoreJunctions.size() * shoreJunctions.size());
    int rejectedBody     = 0;
    int rejectedSpan     = 0;
    int rejectedSameSide = 0;
    for (std::size_t i = 0; i < shoreJunctions.size(); ++i) {
        const int   ja   = shoreJunctions[i].junctionId;
        const Vec2& posA = town.junctions[static_cast<std::size_t>(ja)].pos;
        for (std::size_t j = i + 1; j < shoreJunctions.size(); ++j) {
            const int   jb   = shoreJunctions[j].junctionId;
            const Vec2& posB = town.junctions[static_cast<std::size_t>(jb)].pos;
            if (!shoresMayBridge(shoreJunctions[i], shoreJunctions[j], posA, posB, terrain)) {
                ++rejectedBody;
                continue;
            }
            const float span = (posB - posA).length();
            if (span > settings.maxSpan || span < kMinSegmentLen) {
                ++rejectedSpan;
                continue;
            }
            if (junctionsConnectedWithinHops(town, ja, jb, kBridgeSameSideMaxHops)) {
                ++rejectedSameSide;
                continue;
            }
            candidates.push_back({ja, jb, span});
            Logger::log("bridge",
                        "pair_ok ja=" + std::to_string(ja) + " jb=" + std::to_string(jb)
                            + " span=" + std::to_string(span) + " roads_ja="
                            + junctionRoadIds(town, ja) + " roads_jb=" + junctionRoadIds(town, jb));
        }
    }

    Logger::log("bridge",
                "pair_summary valid=" + std::to_string(candidates.size()) + " reject_body="
                    + std::to_string(rejectedBody) + " reject_span="
                    + std::to_string(rejectedSpan) + " reject_same_side="
                    + std::to_string(rejectedSameSide));

    std::sort(candidates.begin(), candidates.end(),
              [](const BridgeCandidate& a, const BridgeCandidate& b) { return a.length < b.length; });

    const ShoreBridgeLookup lookup = buildShoreBridgeLookup(shoreJunctions,
                                                              static_cast<int>(town.junctions.size()));

    std::vector<char> junctionMatched(town.junctions.size(), 0);

    int bridgesCreated = 0;
    int bridgesSnapped = 0;
    int rejectedSnap   = 0;
    int deferred       = 0;

    constexpr int kMaxBridgePasses = 64;
    for (int pass = 0; pass < kMaxBridgePasses; ++pass) {
        bool progress = false;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const BridgeCandidate& candidate = candidates[i];
            if (candidate.ja < 0 || candidate.jb < 0
                || candidate.ja >= static_cast<int>(junctionMatched.size())
                || candidate.jb >= static_cast<int>(junctionMatched.size())) {
                continue;
            }
            if (junctionMatched[static_cast<std::size_t>(candidate.ja)] != 0
                || junctionMatched[static_cast<std::size_t>(candidate.jb)] != 0) {
                continue;
            }

            if (endpointHasBetterAvailablePartner(candidate.jb, candidate.ja, junctionMatched,
                                                  shoreJunctions, town, terrain, settings, lookup)
                || endpointHasBetterAvailablePartner(candidate.ja, candidate.jb, junctionMatched,
                                                     shoreJunctions, town, terrain, settings,
                                                     lookup)
                || endpointHasBetterSameBankForPartner(candidate.jb, candidate.ja, shoreJunctions,
                                                     town)
                || endpointHasBetterSameBankForPartner(candidate.ja, candidate.jb, shoreJunctions,
                                                       town)) {
                ++deferred;
                std::string reason =
                    findBetterAvailablePartnerReason(candidate.jb, candidate.ja, junctionMatched,
                                                     shoreJunctions, town, terrain, settings,
                                                     lookup);
                if (reason.empty()) {
                    reason = findBetterAvailablePartnerReason(
                        candidate.ja, candidate.jb, junctionMatched, shoreJunctions, town, terrain,
                        settings, lookup);
                }
                if (reason.empty()) {
                    reason = findBetterSameBankReason(candidate.jb, candidate.ja, shoreJunctions,
                                                      town);
                }
                if (reason.empty()) {
                    reason = findBetterSameBankReason(candidate.ja, candidate.jb, shoreJunctions,
                                                      town);
                }
                Logger::log("bridge",
                            "defer pass=" + std::to_string(pass) + " ja="
                                + std::to_string(candidate.ja) + " jb="
                                + std::to_string(candidate.jb) + " span="
                                + std::to_string(candidate.length) + " " + reason);
                continue;
            }

            const Vec2 origA = town.junctions[static_cast<std::size_t>(candidate.ja)].pos;
            const Vec2 origB = town.junctions[static_cast<std::size_t>(candidate.jb)].pos;

            Vec2 bridgeA = origA;
            Vec2 bridgeB = origB;
            if (!findBestBridgeChord(origA, origB, terrain, settings, bridgeA, bridgeB)) {
                ++rejectedSnap;
                Logger::log("bridge",
                            "snap_fail ja=" + std::to_string(candidate.ja) + " jb="
                                + std::to_string(candidate.jb) + " span="
                                + std::to_string(candidate.length));
                continue;
            }

            junctionMatched[static_cast<std::size_t>(candidate.ja)] = 1;
            junctionMatched[static_cast<std::size_t>(candidate.jb)] = 1;

            const bool snapped = !nearPoint(bridgeA, origA) || !nearPoint(bridgeB, origB);
            updateJunctionPosition(town, candidate.ja, bridgeA);
            updateJunctionPosition(town, candidate.jb, bridgeB);

            Road bridge;
            bridge.id       = static_cast<int>(town.roads.size());
            bridge.a        = bridgeA;
            bridge.b        = bridgeB;
            bridge.isBridge = true;
            town.roads.push_back(bridge);
            ++bridgesCreated;
            if (snapped) {
                ++bridgesSnapped;
            }
            Logger::log("bridge",
                        "placed road_id=" + std::to_string(bridge.id) + " ja="
                            + std::to_string(candidate.ja) + " jb="
                            + std::to_string(candidate.jb) + " span="
                            + std::to_string(candidate.length) + " snapped="
                            + std::to_string(snapped ? 1 : 0) + " roads_ja="
                            + junctionRoadIds(town, candidate.ja) + " roads_jb="
                            + junctionRoadIds(town, candidate.jb));
            progress = true;
        }
        if (!progress) {
            break;
        }
    }

    for (std::size_t i = 0; i < town.roads.size(); ++i) {
        town.roads[i].id = static_cast<int>(i);
    }
    indexJunctions(town);

    Logger::log("voronoi",
                "bridge_candidates=" + std::to_string(candidates.size()) + " created="
                    + std::to_string(bridgesCreated) + " snapped="
                    + std::to_string(bridgesSnapped) + " max_span="
                    + std::to_string(settings.maxSpan) + " shore_junctions="
                    + std::to_string(shoreJunctions.size()) + " shore_sea="
                    + std::to_string(shoreSea) + " shore_river="
                    + std::to_string(shoreRiver) + " reject_body="
                    + std::to_string(rejectedBody) + " reject_span="
                    + std::to_string(rejectedSpan) + " reject_same_side="
                    + std::to_string(rejectedSameSide) + " reject_snap="
                    + std::to_string(rejectedSnap) + " deferred="
                    + std::to_string(deferred));
    Logger::log("bridge",
                "resolve_summary candidates=" + std::to_string(candidates.size()) + " created="
                    + std::to_string(bridgesCreated) + " snapped="
                    + std::to_string(bridgesSnapped) + " shore_junctions="
                    + std::to_string(shoreJunctions.size()) + " reject_snap="
                    + std::to_string(rejectedSnap) + " deferred=" + std::to_string(deferred));

    collectWatersideJunctionIds(town, terrain, config.terrain.bridgeWatersideMaxDist);
}
