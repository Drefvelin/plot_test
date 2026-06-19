#include "placement/terrain/TerrainPlacement.h"

#include "placement/frontage/FrontagePlacement.h"
#include "placement/zones/FrontageZones.h"
#include "placement/geometry/PlotGeometry.h"
#include "placement/logging/TerrainPlacementLogging.h"

#include <algorithm>
#include <cmath>
#include <vector>





namespace {





constexpr float kNoOutlineDist = 1e30f;
constexpr TerrainId kSeaId = 1;
constexpr TerrainId kRiverId = 2;
constexpr TerrainId kPlainsId = 3;
constexpr TerrainId kForestId = 4;
constexpr TerrainId kHillsId = 5;
constexpr TerrainId kMountainId = 6;





float polylineLengthTo(const std::vector<Vec2>& graph, std::size_t edgeIndex, float edgeT) {


    float total = 0.f;


    for (std::size_t i = 1; i <= edgeIndex && i < graph.size(); ++i) {


        total += (graph[i] - graph[i - 1]).length();


    }


    if (edgeIndex + 1 < graph.size()) {


        total += (graph[edgeIndex + 1] - graph[edgeIndex]).length() * edgeT;


    }


    return total;


}





bool raySegmentHit(const Vec2& origin, const Vec2& dir, const Vec2& a, const Vec2& b,


                   float maxDist, float& outDist, Vec2& outPoint) {


    const Vec2  seg   = b - a;


    const float denom = dir.x * seg.y - dir.y * seg.x;


    if (std::abs(denom) < 1e-8f) {


        return false;


    }


    const Vec2  ao    = a - origin;


    const float t     = (ao.x * seg.y - ao.y * seg.x) / denom;


    const float u     = (ao.x * dir.y - ao.y * dir.x) / denom;


    if (t < 1e-4f || t > maxDist + 1e-3f || u < -1e-4f || u > 1.f + 1e-4f) {


        return false;


    }


    outDist   = t;


    outPoint  = origin + dir * t;


    return true;


}





float distToPolylineGraphs(const Vec2& p, const std::vector<std::vector<Vec2>>& graphs) {


    float best = kNoOutlineDist;


    for (const std::vector<Vec2>& decaGraph : graphs) {


        const std::vector<Vec2> graph = decaGraph;


        if (graph.size() < 2) {


            continue;


        }


        for (std::size_t i = 1; i < graph.size(); ++i) {


            const float dist = distancePointToSegment(p, graph[i - 1], graph[i]);


            if (dist < best) {


                best = dist;


                if (best <= 0.f) {


                    return 0.f;


                }


            }


        }


    }


    return best;


}





}  // namespace

float polylineGraphLength(const std::vector<Vec2>& graph) {
    if (graph.size() < 2) {
        return 0.f;
    }
    float total = 0.f;
    for (std::size_t i = 1; i < graph.size(); ++i) {
        total += (graph[i] - graph[i - 1]).length();
    }
    return total;
}

bool samplePolylineGraphAtArc(const std::vector<Vec2>& graph, float arcT, Vec2& outPoint,
                              Vec2& outTangent) {
    if (graph.size() < 2) {
        return false;
    }
    float remaining = std::max(0.f, arcT);
    for (std::size_t i = 1; i < graph.size(); ++i) {
        const Vec2  a = graph[i - 1];
        const Vec2  b = graph[i];
        const Vec2  ab = b - a;
        const float len = ab.length();
        if (len < 1e-4f) {
            continue;
        }
        if (remaining <= len + 1e-4f) {
            const float t = std::clamp(remaining / len, 0.f, 1.f);
            outPoint    = a + ab * t;
            outTangent  = ab * (1.f / len);
            return true;
        }
        remaining -= len;
    }
    outPoint   = graph.back();
    const Vec2 ab = graph.back() - graph[graph.size() - 2];
    if (ab.length() < 1e-4f) {
        outTangent = {1.f, 0.f};
    } else {
        outTangent = ab.normalized();
    }
    return true;
}

TerrainPlacementMode effectiveTerrainMode(const BuildingDef& def, const TerrainAtlas& atlas) {


    if (def.terrain.placement == TerrainPlacementMode::Proximity


        && def.terrain.prefer != kTerrainUnknown
        && def.terrain.prefer == atlas.majorityLandId) {


        return TerrainPlacementMode::Inside;


    }


    return def.terrain.placement;


}





bool terrainKindMatchesPrefer(TerrainId sampled, TerrainId prefer) {


    if (sampled == prefer) {


        return true;


    }


    if (prefer == kHillsId && (sampled == kMountainId || sampled == kHillsId)) {


        return true;


    }


    return false;


}

bool terrainKindMatchesPrefer(TerrainId sampled, const std::vector<TerrainId>& kinds) {
    for (TerrainId kind : kinds) {
        if (terrainKindMatchesPrefer(sampled, kind)) {
            return true;
        }
    }
    return false;
}





const std::vector<std::vector<Vec2>>* outlineGraphsForPrefer(const TerrainAtlas& atlas,
                                                              TerrainId prefer) {
    return atlas.outlineGraphs(prefer);
}





float distToPreferEdge(const Vec2& point, TerrainId prefer, const TerrainAtlas& atlas) {


    if (!atlas.valid) {


        return kNoOutlineDist;


    }


    const std::vector<std::vector<Vec2>>* graphs = outlineGraphsForPrefer(atlas, prefer);


    if (graphs == nullptr || graphs->empty()) {


        return kNoOutlineDist;


    }


    return distToPolylineGraphs(point, *graphs);


}

float distToPreferEdge(const Vec2& point, const std::vector<TerrainId>& kinds,
                       const TerrainAtlas& atlas) {
    if (!atlas.valid || kinds.empty()) {
        return kNoOutlineDist;
    }
    float best = kNoOutlineDist;
    for (TerrainId kind : kinds) {
        const float dist = distToPreferEdge(point, kind, atlas);
        if (dist < best) {
            best = dist;
        }
    }
    return best;
}





bool snapToPreferOutline(const Vec2& query, TerrainId prefer, const TerrainAtlas& terrain,


                         const Vec2& landReference, OutlineSnap& out) {


    out = {};


    if (!terrain.valid) {


        return false;


    }


    const std::vector<std::vector<Vec2>>* graphs = outlineGraphsForPrefer(terrain, prefer);


    if (graphs == nullptr) {


        return false;


    }





    float bestDist = kNoOutlineDist;


    for (int graphIndex = 0; graphIndex < static_cast<int>(graphs->size()); ++graphIndex) {


        const std::vector<Vec2> graph =


            (*graphs)[static_cast<std::size_t>(graphIndex)];


        if (graph.size() < 2) {


            continue;


        }


        for (std::size_t edgeIndex = 1; edgeIndex < graph.size(); ++edgeIndex) {


            const Vec2& a = graph[edgeIndex - 1];


            const Vec2& b = graph[edgeIndex];


            const Vec2  ab    = b - a;


            const float lenSq = ab.x * ab.x + ab.y * ab.y;


            if (lenSq < 1e-8f) {


                continue;


            }


            const float t = std::clamp(((query.x - a.x) * ab.x + (query.y - a.y) * ab.y) / lenSq,


                                       0.f, 1.f);


            const Vec2  proj{a.x + ab.x * t, a.y + ab.y * t};


            const float dist = (query - proj).length();


            if (dist >= bestDist - 1e-4f) {


                continue;


            }


            bestDist       = dist;


            out.point      = proj;


            out.graphIndex = graphIndex;


            out.edgeIndex  = static_cast<int>(edgeIndex - 1);


            out.edgeT      = t;


            out.polylineT  = polylineLengthTo(graph, edgeIndex - 1, t);


            out.tangent    = ab.normalized();


            const Vec2 toLand = landReference - proj;


            if (toLand.length() >= 1e-4f) {


                out.featureInward = toLand.normalized() * -1.f;


            } else {


                const Vec2 n{-out.tangent.y, out.tangent.x};


                out.featureInward = n;


            }


            out.valid = true;


        }


    }


    return out.valid;


}

bool snapToPreferOutline(const Vec2& query, const std::vector<TerrainId>& kinds,
                         const TerrainAtlas& terrain, const Vec2& landReference,
                         OutlineSnap& out) {
    out = {};
    if (!terrain.valid || kinds.empty()) {
        return false;
    }
    OutlineSnap best{};
    float       bestDist = kNoOutlineDist;
    for (TerrainId kind : kinds) {
        OutlineSnap candidate{};
        if (!snapToPreferOutline(query, kind, terrain, landReference, candidate)) {
            continue;
        }
        const float dist = (query - candidate.point).length();
        if (dist < bestDist - 1e-4f) {
            bestDist = dist;
            best     = candidate;
        }
    }
    if (!best.valid) {
        return false;
    }
    out = best;
    return true;
}





bool projectToPreferOutline(const Vec2& from, const Vec2& dir, TerrainId prefer,


                            const TerrainAtlas& terrain, float maxDist, Vec2& outPoint) {


    outPoint = {};


    if (!terrain.valid || dir.length() < 1e-4f) {


        return false;


    }


    const Vec2 unitDir = dir.normalized();


    const std::vector<std::vector<Vec2>>* graphs = outlineGraphsForPrefer(terrain, prefer);


    if (graphs == nullptr) {


        return false;


    }





    float bestDist = maxDist + 1.f;


    bool  found    = false;


    for (const std::vector<Vec2>& decaGraph : *graphs) {


        const std::vector<Vec2> graph = decaGraph;


        if (graph.size() < 2) {


            continue;


        }


        for (std::size_t i = 1; i < graph.size(); ++i) {


            float hitDist = 0.f;


            Vec2  hit{};


            if (!raySegmentHit(from, unitDir, graph[i - 1], graph[i], maxDist, hitDist, hit)) {


                continue;


            }


            if (hitDist < bestDist - 1e-4f) {


                bestDist = hitDist;


                outPoint = hit;


                found    = true;


            }


        }


    }


    return found;


}

bool projectToPreferOutline(const Vec2& from, const Vec2& dir,
                            const std::vector<TerrainId>& kinds, const TerrainAtlas& terrain,
                            float maxDist, Vec2& outPoint) {
    outPoint = {};
    if (!terrain.valid || dir.length() < 1e-4f || kinds.empty()) {
        return false;
    }
    float  bestDist = maxDist + 1.f;
    Vec2   bestPoint{};
    bool   found    = false;
    for (TerrainId kind : kinds) {
        Vec2 hit{};
        if (!projectToPreferOutline(from, dir, kind, terrain, maxDist, hit)) {
            continue;
        }
        const float dist = (hit - from).length();
        if (dist < bestDist - 1e-4f) {
            bestDist = dist;
            bestPoint = hit;
            found     = true;
        }
    }
    if (!found) {
        return false;
    }
    outPoint = bestPoint;
    return true;
}

bool rayHitPreferOutline(const Vec2& from, const Vec2& dir, TerrainId prefer,
                         const TerrainAtlas& terrain, float maxDist, OutlineRayHit& out) {
    out = {};
    if (!terrain.valid || dir.length() < 1e-4f) {
        return false;
    }

    const Vec2 unitDir = dir.normalized();
    const std::vector<std::vector<Vec2>>* graphs = outlineGraphsForPrefer(terrain, prefer);
    if (graphs == nullptr) {
        return false;
    }

    float bestDist = maxDist + 1.f;
    bool  found    = false;

    for (int graphIndex = 0; graphIndex < static_cast<int>(graphs->size()); ++graphIndex) {
        const std::vector<Vec2> graph = (*graphs)[static_cast<std::size_t>(graphIndex)];
        if (graph.size() < 2) {
            continue;
        }
        for (std::size_t edgeIndex = 1; edgeIndex < graph.size(); ++edgeIndex) {
            float hitDist = 0.f;
            Vec2  hit{};
            if (!raySegmentHit(from, unitDir, graph[edgeIndex - 1], graph[edgeIndex], maxDist,
                               hitDist, hit)) {
                continue;
            }
            if (hitDist >= bestDist - 1e-4f) {
                continue;
            }
            const Vec2  ab    = graph[edgeIndex] - graph[edgeIndex - 1];
            const float lenSq = ab.x * ab.x + ab.y * ab.y;
            const float edgeT = lenSq > 1e-8f
                                    ? std::clamp(((hit.x - graph[edgeIndex - 1].x) * ab.x
                                                  + (hit.y - graph[edgeIndex - 1].y) * ab.y)
                                                     / lenSq,
                                                 0.f, 1.f)
                                    : 0.f;
            bestDist           = hitDist;
            out.point          = hit;
            out.dist           = hitDist;
            out.graphIndex     = graphIndex;
            out.edgeIndex      = static_cast<int>(edgeIndex - 1);
            out.edgeT          = edgeT;
            out.polylineT      = polylineLengthTo(graph, edgeIndex - 1, edgeT);
            out.valid          = true;
            found              = true;
        }
    }

    return found;
}





bool plotMeetsBorderHug(const Plot& plot, TerrainId kind, const BuildingTerrainRules& rules,


                        const TerrainAtlas& terrain, float epsilon) {


    if (!terrain.valid || rules.placement != TerrainPlacementMode::Border


        || rules.borderStyle != BorderStyle::Hug) {


        return true;


    }


    for (int i = 2; i < 4; ++i) {


        const float edgeDist =


            distToPreferEdge(plot.corners[i], kind, terrain);


        if (edgeDist >= kNoOutlineDist * 0.5f || edgeDist > epsilon + 1e-3f) {


            return false;


        }


    }


    return true;


}

bool plotMeetsBorderHug(const Plot& plot, const BuildingTerrainRules& rules,
                        const TerrainAtlas& terrain, float epsilon) {
    return plotMeetsBorderHug(plot, rules.prefer, rules, terrain, epsilon);
}





bool plotMeetsInsideMin(const Plot& plot, const TerrainAtlas& terrain, TerrainId prefer) {


    if (!terrain.valid || prefer == kTerrainUnknown) {


        return true;


    }


    for (int i = 0; i < 4; ++i) {


        const Vec2 corner = plot.corners[i];


        if (!terrain.isBuildable(corner)) {


            continue;


        }


        if (terrainKindMatchesPrefer(terrain.sample(corner), prefer)) {


            return true;


        }


    }


    return false;


}





float terrainScoreForPlot(const Plot& plot, const BuildingDef& def, const TerrainAtlas& atlas) {


    if (!atlas.valid || def.terrain.placement == TerrainPlacementMode::None) {


        return 0.f;


    }





    const TerrainPlacementMode mode = effectiveTerrainMode(def, atlas);


    if (mode == TerrainPlacementMode::Inside) {


        int matching = 0;


        for (int i = 0; i < 4; ++i) {


            if (terrainKindMatchesPrefer(atlas.sample(plot.corners[i]),


                                         def.terrain.prefer)) {


                ++matching;


            }


        }


        return static_cast<float>(4 - matching);


    }





    if (mode == TerrainPlacementMode::Proximity) {


        const Vec2 mid = plotCenter(plot);


        const float dist = distToPreferEdge(mid, def.terrain.prefer, atlas);


        if (dist >= kNoOutlineDist * 0.5f) {


            return def.terrain.proximityMaxDist;


        }


        return dist;


    }





    if (mode == TerrainPlacementMode::Border) {


        const Vec2 backMid =


            (plot.corners[2] + plot.corners[3]) * 0.5f;


        return distToPreferEdge(backMid, def.terrain.prefer, atlas);


    }





    return 0.f;


}





float terrainScoreForPoint(const Vec2& point, const BuildingDef& def, const TerrainAtlas& atlas) {


    if (!atlas.valid || def.terrain.placement == TerrainPlacementMode::None) {


        return 0.f;


    }


    const TerrainPlacementMode mode = effectiveTerrainMode(def, atlas);


    if (mode == TerrainPlacementMode::Proximity) {


        const float dist = distToPreferEdge(point, def.terrain.prefer, atlas);


        if (dist >= kNoOutlineDist * 0.5f) {


            return def.terrain.proximityMaxDist;


        }


        return dist;


    }


    return 0.f;


}





float segmentTerrainScore(const Town& town, const FrontageSlot& slot, const BuildingDef& def,


                          const TerrainAtlas& atlas) {


    Vec2 midpoint{};


    if (!segmentMidpoint(town, slot, midpoint)) {


        return 0.f;


    }


    return terrainScoreForPoint(midpoint, def, atlas) * kTerrainScoreWeight;


}





bool plotMeetsBorderBand(const Plot& plot, TerrainId kind, const BuildingTerrainRules& rules,


                         const TerrainAtlas& terrain) {


    if (!terrain.valid || rules.placement != TerrainPlacementMode::Border) {


        return true;


    }


    const Vec2  backMid =


        (plot.corners[2] + plot.corners[3]) * 0.5f;


    const float edgeDist = distToPreferEdge(backMid, kind, terrain);


    if (edgeDist >= kNoOutlineDist * 0.5f) {


        return false;


    }


    return edgeDist >= rules.borderMinDist - 1e-3f && edgeDist <= rules.borderMaxDist + 1e-3f;


}

bool plotMeetsBorderBand(const Plot& plot, const BuildingTerrainRules& rules,
                         const TerrainAtlas& terrain) {
    return plotMeetsBorderBand(plot, rules.prefer, rules, terrain);
}

bool footprintMeetsBorderHug(const BuildingFootprint& footprint, TerrainId kind,
                             const BuildingTerrainRules& rules, const TerrainAtlas& terrain,
                             float epsilon) {
    if (!terrain.valid || rules.placement != TerrainPlacementMode::Border
        || rules.borderStyle != BorderStyle::Hug) {
        return true;
    }
    const Vec2  backMid  = (footprint.corners[2] + footprint.corners[3]) * 0.5f;
    const float edgeDist = distToPreferEdge(backMid, kind, terrain);
    return edgeDist <= epsilon + 1e-3f;
}

bool footprintMeetsBorderBand(const BuildingFootprint& footprint, TerrainId kind,
                              const BuildingTerrainRules& rules, const TerrainAtlas& terrain) {
    if (!terrain.valid || rules.placement != TerrainPlacementMode::Border) {
        return true;
    }
    const Vec2  backMid  = (footprint.corners[2] + footprint.corners[3]) * 0.5f;
    const float edgeDist = distToPreferEdge(backMid, kind, terrain);
    constexpr float kFar = 1e29f;
    if (edgeDist >= kFar * 0.5f) {
        return false;
    }
    return edgeDist >= rules.borderMinDist - 1e-3f && edgeDist <= rules.borderMaxDist + 1e-3f;
}

bool segmentWithinProximityMax(const Town& town, const FrontageSlot& slot, const BuildingDef& def,


                               const TerrainAtlas& atlas) {


    if (!atlas.valid) {


        return true;


    }


    const TerrainPlacementMode mode = effectiveTerrainMode(def, atlas);


    if (mode != TerrainPlacementMode::Proximity) {


        return true;


    }


    Vec2 midpoint{};


    if (!segmentMidpoint(town, slot, midpoint)) {


        return false;


    }


    const float dist = distToPreferEdge(midpoint, def.terrain.prefer, atlas);


    if (dist >= kNoOutlineDist * 0.5f) {


        return false;


    }


    return dist <= def.terrain.proximityMaxDist + 1e-3f;


}





bool usesTerrainPlacementScan(const DefCache& defs, const std::string& buildingType,


                              const TerrainAtlas* terrain) {


    if (terrain == nullptr || !terrain->valid) {


        return false;


    }


    const BuildingDef* def = defs.building(buildingType);


    return def != nullptr && def->terrain.placement != TerrainPlacementMode::None;


}





bool isTerrainRequirementStrict(const BuildingDef& def) {


    return def.terrain.requirement == TerrainRequirement::Strict;


}





void recordTerrainAnchorRoad(Town& town, TerrainId prefer, int roadId, int queueIndex,
                             const char* source) {
    if (prefer == kTerrainUnknown || roadId < 0) {
        return;
    }
    const int prev = terrainAnchorRoadFor(town, prefer);
    town.lastTerrainAnchorRoadId[prefer] = roadId;
    logTerrainAnchorStored(queueIndex, prefer, roadId, source);
    if (prev >= 0 && prev != roadId) {
        logTerrainAnchorReplaced(queueIndex, prefer, prev, roadId, source);
    }
}

int terrainAnchorRoadFor(const Town& town, TerrainId prefer) {
    if (prefer == kTerrainUnknown) {
        return -1;
    }
    const auto it = town.lastTerrainAnchorRoadId.find(prefer);
    if (it == town.lastTerrainAnchorRoadId.end()) {
        return -1;
    }
    return it->second;
}

TerrainAnchorBfsResult collectRoadsNearTerrainAnchor(const Town& town, int anchorRoadId,
                                                     int maxRoads) {
    TerrainAnchorBfsResult result;
    if (anchorRoadId < 0 || anchorRoadId >= static_cast<int>(town.roads.size()) || maxRoads <= 0) {
        return result;
    }

    std::vector<int> roadDist(town.roads.size(), -1);
    std::vector<int> queue;
    queue.reserve(town.roads.size());

    const auto enqueueRoad = [&](int roadId, int dist) {
        if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
            return;
        }
        if (roadDist[static_cast<std::size_t>(roadId)] >= 0) {
            return;
        }
        roadDist[static_cast<std::size_t>(roadId)] = dist;
        queue.push_back(roadId);
    };

    enqueueRoad(anchorRoadId, 0);

    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
        const int roadId = queue[qi];
        const int dist   = roadDist[static_cast<std::size_t>(roadId)];
        result.visitOrder.push_back(roadId);
        result.visitDist.push_back(dist);

        if (roadId != anchorRoadId) {
            result.selected.push_back(roadId);
        }

        if (static_cast<int>(result.selected.size()) >= maxRoads) {
            continue;
        }

        const Road& road = town.roads[static_cast<std::size_t>(roadId)];
        for (int junctionId : {road.junctionA, road.junctionB}) {
            if (junctionId < 0 || junctionId >= static_cast<int>(town.junctions.size())) {
                continue;
            }
            for (int neighborRoadId :
                 town.junctions[static_cast<std::size_t>(junctionId)].roadIds) {
                if (neighborRoadId == roadId) {
                    continue;
                }
                if (roadDist[static_cast<std::size_t>(neighborRoadId)] >= 0) {
                    continue;
                }
                roadDist[static_cast<std::size_t>(neighborRoadId)] = dist + 1;
                queue.push_back(neighborRoadId);
            }
        }
    }

    if (static_cast<int>(result.selected.size()) > maxRoads) {
        result.selected.resize(static_cast<std::size_t>(maxRoads));
    }

    return result;
}

int roadIdForSegment(const Town& town, int segmentId) {
    if (segmentId < 0) {
        return -1;
    }
    for (const Road& road : town.roads) {
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            const RoadSideFrontage* side = road.sideBank(bankIndex);
            if (side == nullptr) {
                continue;
            }
            for (const RoadFrontageSegment& segment : side->segments) {
                if (segment.id == segmentId) {
                    return road.id;
                }
            }
        }
    }
    return -1;
}





bool buildingHasTerrainPlacement(const DefCache& defs, const std::string& buildingType) {


    const BuildingDef* def = defs.building(buildingType);


    return def != nullptr && def->terrain.placement != TerrainPlacementMode::None;


}





bool buildingHasTerrainPlacement(const DefCache& defs, BuildingTypeId typeId) {


    const BuildingDef* def = defs.building(typeId);


    return def != nullptr && def->terrain.placement != TerrainPlacementMode::None;


}


