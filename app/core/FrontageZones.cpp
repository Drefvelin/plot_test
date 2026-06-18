#include "FrontageZones.h"

#include "FrontagePlacement.h"
#include "GrowthRings.h"
#include "PlacementFrontier.h"
#include "PlotGeometry.h"
#include "Profile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

constexpr float kResidentialZoneBiasScale = 15.f;
constexpr float kRuralMinCenterBase       = 35.f;
constexpr float kRuralMinCenterGrowth     = 90.f;
constexpr float kRuralMinBuildingSepBase  = 12.f;
constexpr float kRuralMinBuildingSepGrowth = 50.f;

namespace {

int segmentMinJunctionHopsImpl(const Town& town, int roadId, const std::vector<int>& junctionHops) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return 999;
    }
    const Road& road = town.roads[static_cast<std::size_t>(roadId)];

    int hops = 999;
    if (road.junctionA >= 0 && road.junctionA < static_cast<int>(junctionHops.size())
        && junctionHops[static_cast<std::size_t>(road.junctionA)] >= 0) {
        hops = std::min(hops, junctionHops[static_cast<std::size_t>(road.junctionA)]);
    }
    if (road.junctionB >= 0 && road.junctionB < static_cast<int>(junctionHops.size())
        && junctionHops[static_cast<std::size_t>(road.junctionB)] >= 0) {
        hops = std::min(hops, junctionHops[static_cast<std::size_t>(road.junctionB)]);
    }
    return hops;
}

void rebuildRingDistanceStats(Town& town) {
    int maxHop = 0;
    for (const int hops : town.junctionHopCache) {
        if (hops >= 0) {
            maxHop = std::max(maxHop, hops);
        }
    }
    for (const int hops : town.roadHopCache) {
        if (hops >= 0 && hops < 999) {
            maxHop = std::max(maxHop, hops);
        }
    }

    std::vector<std::vector<float>> buckets(static_cast<std::size_t>(maxHop + 1));
    for (std::size_t ji = 0; ji < town.junctions.size(); ++ji) {
        const int hops = town.junctionHopCache[ji];
        if (hops < 0 || hops > maxHop) {
            continue;
        }
        const float dist = (town.junctions[ji].pos - town.center).length();
        buckets[static_cast<std::size_t>(hops)].push_back(dist);
    }

    town.maxObservedRoadDist = 0.f;
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        if (road.id < 0 || road.id >= static_cast<int>(town.roadHopCache.size())) {
            continue;
        }
        const int hops = town.roadHopCache[static_cast<std::size_t>(road.id)];
        const float dist = roadMidpointCenterDist(town, road);
        town.maxObservedRoadDist = std::max(town.maxObservedRoadDist, dist);
        if (hops >= 0 && hops <= maxHop) {
            buckets[static_cast<std::size_t>(hops)].push_back(dist);
        }
    }

    town.ringAvgDistByHop.assign(static_cast<std::size_t>(maxHop + 1), -1.f);
    for (int h = 0; h <= maxHop; ++h) {
        const std::vector<float>& samples = buckets[static_cast<std::size_t>(h)];
        if (samples.empty()) {
            continue;
        }
        float sum = 0.f;
        for (const float dist : samples) {
            sum += dist;
        }
        town.ringAvgDistByHop[static_cast<std::size_t>(h)] = sum / static_cast<float>(samples.size());
    }

    float sliceSum   = 0.f;
    int   sliceCount = 0;
    for (int h = 0; h < maxHop; ++h) {
        const float cur = town.ringAvgDistByHop[static_cast<std::size_t>(h)];
        const float nxt = town.ringAvgDistByHop[static_cast<std::size_t>(h + 1)];
        if (cur >= 0.f && nxt >= 0.f) {
            sliceSum += nxt - cur;
            ++sliceCount;
        }
    }
    town.ringMeanSliceWidth =
        sliceCount > 0 ? sliceSum / static_cast<float>(sliceCount)
                       : std::max(town.radius, 1.f) / static_cast<float>(std::max(1, maxHop));

    if (!town.ringAvgDistByHop.empty() && town.ringAvgDistByHop[0] < 0.f) {
        town.ringAvgDistByHop[0] = 0.f;
    }
    for (std::size_t h = 1; h < town.ringAvgDistByHop.size(); ++h) {
        if (town.ringAvgDistByHop[h] < 0.f) {
            town.ringAvgDistByHop[h] =
                town.ringAvgDistByHop[h - 1] + town.ringMeanSliceWidth;
        }
    }
}

void rebuildJunctionHopCache(Town& town) {
    PROFILE_SCOPE(ProfileScopeId::JunctionHops);

    town.junctionHopCache.assign(town.junctions.size(), -1);
    if (town.junctions.empty()) {
        town.roadHopCache.clear();
        town.junctionHopCacheValid = true;
        town.ringAvgDistByHop.clear();
        town.ringMeanSliceWidth    = 0.f;
        town.maxObservedRoadDist   = 0.f;
        town.suburbanRoadListMaxHop = -1;
        town.suburbanRoadListCache.clear();
        town.ruralRoadListMaxHop = -1;
        town.ruralRoadListCache.clear();
        return;
    }

    int startJunction = 0;
    float bestDist    = 1e9f;
    for (std::size_t i = 0; i < town.junctions.size(); ++i) {
        const float dist = (town.junctions[i].pos - town.center).length();
        if (dist < bestDist) {
            bestDist      = dist;
            startJunction = static_cast<int>(i);
        }
    }

    std::vector<int> queue;
    town.junctionHopCache[static_cast<std::size_t>(startJunction)] = 0;
    queue.push_back(startJunction);

    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
        const int       curJ     = queue[qi];
        const Junction& junction = town.junctions[static_cast<std::size_t>(curJ)];
        const int       curHops  = town.junctionHopCache[static_cast<std::size_t>(curJ)];

        for (const int roadId : junction.roadIds) {
            if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
                continue;
            }
            const Road& road   = town.roads[static_cast<std::size_t>(roadId)];
            const int   otherJ = (road.junctionA == curJ)   ? road.junctionB
                                 : (road.junctionB == curJ) ? road.junctionA
                                                            : -1;
            if (otherJ < 0 || otherJ >= static_cast<int>(town.junctionHopCache.size())
                || town.junctionHopCache[static_cast<std::size_t>(otherJ)] >= 0) {
                continue;
            }
            town.junctionHopCache[static_cast<std::size_t>(otherJ)] = curHops + 1;
            queue.push_back(otherJ);
        }
    }

    town.roadHopCache.assign(town.roads.size(), 999);
    for (const Road& road : town.roads) {
        if (road.id < 0 || road.id >= static_cast<int>(town.roadHopCache.size())) {
            continue;
        }
        town.roadHopCache[static_cast<std::size_t>(road.id)] =
            segmentMinJunctionHopsImpl(town, road.id, town.junctionHopCache);
    }

    town.junctionHopCacheValid = true;
    rebuildRingDistanceStats(town);
    town.suburbanRoadListMaxHop = -1;
    town.suburbanRoadListCache.clear();
    town.ruralRoadListMaxHop = -1;
    town.ruralRoadListCache.clear();
}

}  // namespace

float roadMidpointCenterDist(const Town& town, const Road& road) {
    const Vec2 mid = {(road.a.x + road.b.x) * 0.5f, (road.a.y + road.b.y) * 0.5f};
    return (mid - town.center).length();
}

float roadCenterDist(const Town& town, int roadId) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return -1.f;
    }
    return roadMidpointCenterDist(town, town.roads[static_cast<std::size_t>(roadId)]);
}

void invalidateJunctionHopCache(Town& town) {
    town.junctionHopCacheValid = false;
    town.junctionHopCache.clear();
    town.roadHopCache.clear();
    town.ringAvgDistByHop.clear();
    town.ringMeanSliceWidth  = 0.f;
    town.maxObservedRoadDist = 0.f;
    town.suburbanRoadListMaxHop = -1;
    town.suburbanRoadListCache.clear();
    town.ruralRoadListMaxHop = -1;
    town.ruralRoadListCache.clear();
}

const std::vector<int>& getJunctionHops(const Town& town) {
    if (!town.junctionHopCacheValid) {
        rebuildJunctionHopCache(const_cast<Town&>(town));
    }
    return town.junctionHopCache;
}

int getRoadHop(const Town& town, int roadId) {
    getJunctionHops(town);
    if (roadId < 0 || roadId >= static_cast<int>(town.roadHopCache.size())) {
        return 999;
    }
    return town.roadHopCache[static_cast<std::size_t>(roadId)];
}

std::vector<int> computeJunctionHopDistances(const Town& town) {
    return getJunctionHops(town);
}

int segmentMinJunctionHops(const Town& town, int roadId, const std::vector<int>& junctionHops) {
    return segmentMinJunctionHopsImpl(town, roadId, junctionHops);
}

float nearestBuildingDist(const Town& town, const Vec2& point) {
    float best = 1e9f;
    for (const BuildingInstance& instance : town.buildingInstances) {
        best = std::min(best, (instancePlacementPoint(instance) - point).length());
    }
    return best;
}

bool segmentMidpoint(const Town& town, const FrontageSlot& slot, Vec2& out) {
    if (slot.roadId < 0 || slot.roadId >= static_cast<int>(town.roads.size())) {
        return false;
    }
    const Road& road = town.roads[static_cast<std::size_t>(slot.roadId)];
    Vec2        origin{};
    Vec2        farEnd{};
    Vec2        edgeDir{};
    if (!roadFrameForBank(road, slot.bankIndex, origin, farEnd, edgeDir)) {
        return false;
    }
    out = origin + edgeDir * ((slot.startT + slot.endT) * 0.5f);
    return true;
}

const char* zoneTypeForBuilding(const DefCache& defs, const std::string& buildingType) {
    const BuildingDef* def = defs.building(buildingType);
    if (!def || def->type.empty()) {
        return "residential";
    }
    return def->type.c_str();
}

float minCenterDistForZone(const char* zone, float townGrowth) {
    if (std::strcmp(zone, "urban") == 0) {
        return 0.f;
    }
    if (std::strcmp(zone, "rural") == 0) {
        return kRuralMinCenterBase + kRuralMinCenterGrowth * townGrowth;
    }
    return kResidentialZoneBiasScale * townGrowth;
}

int minJunctionHopsForRural(float townGrowth) {
    if (townGrowth < 0.2f) {
        return 0;
    }
    if (townGrowth < 0.55f) {
        return 1;
    }
    return 2;
}

float minBuildingSeparationForRural(float townGrowth) {
    return kRuralMinBuildingSepBase + kRuralMinBuildingSepGrowth * townGrowth;
}

float ruralTargetCenterDist(const Town& town, float townGrowth) {
    const float radius = std::max(town.radius, 1.f);
    return radius * (0.24f + 0.34f * townGrowth);
}

float ruralMaxCenterDist(const Town& town, float townGrowth) {
    const float radius = std::max(town.radius, 1.f);
    return radius * (0.40f + 0.44f * townGrowth);
}

float zoneBiasForType(const char* zone, float townGrowth) {
    return minCenterDistForZone(zone, townGrowth);
}

int roadHop(const Town& town, int roadId, const std::vector<int>& /*junctionHops*/) {
    return getRoadHop(town, roadId);
}

float ringDistAtHop(const Town& town, int hop) {
    getJunctionHops(town);
    if (hop < 0) {
        return 0.f;
    }
    if (!town.ringAvgDistByHop.empty()
        && hop < static_cast<int>(town.ringAvgDistByHop.size())) {
        return town.ringAvgDistByHop[static_cast<std::size_t>(hop)];
    }
    if (town.ringAvgDistByHop.empty()) {
        return 0.f;
    }
    const int lastHop = static_cast<int>(town.ringAvgDistByHop.size()) - 1;
    return town.ringAvgDistByHop[static_cast<std::size_t>(lastHop)]
           + static_cast<float>(hop - lastHop) * town.ringMeanSliceWidth;
}

float suburbanMaxDist(const Town& town) {
    return ringDistAtHop(town, town.suburbanMaxHop);
}

float urbanCoreMaxDist(const Town& town) {
    if (town.urbanCoreMaxHop < 0) {
        return -1.f;
    }
    return ringDistAtHop(town, town.urbanCoreMaxHop);
}

bool hasRoadOutsideSuburbanBand(const Town& town) {
    const float suburbanDist = suburbanMaxDist(town);
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        if (roadMidpointCenterDist(town, road) > suburbanDist + 1e-3f) {
            return true;
        }
    }
    return false;
}

PlacementBand classifyPlacementBandByDist(float dist, const Town& town) {
    if (dist < 0.f) {
        return PlacementBand::Unknown;
    }
    const float suburbanDist = suburbanMaxDist(town);
    if (dist > suburbanDist + 1e-3f) {
        return PlacementBand::Rural;
    }
    if (town.urbanCoreMaxHop >= 0 && dist <= urbanCoreMaxDist(town) + 1e-3f) {
        return PlacementBand::Core;
    }
    return PlacementBand::Suburban;
}

sf::Color placementBandColor(PlacementBand band) {
    switch (band) {
    case PlacementBand::Core:
        return sf::Color(220, 70, 70);
    case PlacementBand::Suburban:
        return sf::Color(120, 190, 255);
    case PlacementBand::Rural:
        return sf::Color(150, 100, 50);
    default:
        return sf::Color(100, 100, 100);
    }
}

std::string formatPlacementBandDistRanges(const Town& town) {
    const int suburbanDist = static_cast<int>(std::lround(suburbanMaxDist(town)));
    std::string out;
    if (town.urbanCoreMaxHop >= 0) {
        const int coreDist = static_cast<int>(std::lround(urbanCoreMaxDist(town)));
        out += "core 0–" + std::to_string(coreDist);
        if (coreDist < suburbanDist) {
            out += "  suburban " + std::to_string(coreDist) + "–" + std::to_string(suburbanDist);
        }
    } else {
        out += "suburban 0–" + std::to_string(suburbanDist);
    }
    out += "  rural " + std::to_string(suburbanDist) + "+";
    return out;
}

float scoreSegmentForZone(const Town& town, const FrontageSlot& slot, const char* zone,
                          float townGrowth) {
    const float roadDist = roadCenterDist(town, slot.roadId);
    if (roadDist < 0.f) {
        return 1e9f;
    }

    if (roadDist + 1e-3f < minCenterDistForZone(zone, townGrowth)) {
        return 1e9f;
    }

    if (std::strcmp(zone, "rural") == 0) {
        if (slot.roadId < 0 || slot.roadId >= static_cast<int>(town.roads.size())) {
            return 1e9f;
        }
        if (roadDist <= suburbanMaxDist(town) + 1e-3f) {
            return 1e9f;
        }

        Vec2 midpoint{};
        if (!segmentMidpoint(town, slot, midpoint)) {
            return 1e9f;
        }

        const float buildingDist = nearestBuildingDist(town, midpoint);
        if (!town.buildingInstances.empty()
            && buildingDist + 1e-3f < minBuildingSeparationForRural(townGrowth)) {
            return 1e9f;
        }

        if (roadDist > ruralMaxCenterDist(town, townGrowth) + 1e-3f) {
            return 1e9f;
        }

        const float targetDist     = ruralTargetCenterDist(town, townGrowth);
        const float distFromTarget = std::abs(roadDist - targetDist);
        const int   hops           = getRoadHop(town, slot.roadId);
        return distFromTarget - buildingDist * 0.08f - static_cast<float>(hops) * 2.f;
    }

    return roadDist;
}

float bandMinFrontage(const DefCache& defs, const std::string& buildingType, float /*segWidth*/,
                      float maxDepthToFrontRatio) {
    const SizeBand* band = defs.sizeBandForBuilding(buildingType);
    if (!band) {
        return 0.f;
    }
    return std::max(1.f, std::sqrt(band->minArea / maxDepthToFrontRatio));
}

int instanceHostRoadId(const BuildingInstance& inst) {
    if (inst.placementMode == BuildingPlacementMode::SegmentGapFill) {
        return inst.roadId;
    }
    if (inst.placementMode == BuildingPlacementMode::BorderBuilding && inst.roadId >= 0) {
        return inst.roadId;
    }
    if (inst.plot.roadId >= 0) {
        return inst.plot.roadId;
    }
    return inst.roadId;
}

namespace {

FrontierBand classifyBandAt(float centerDist, float suburbanMaxDist, float coreMaxDist,
                            bool coreEnabled) {
    constexpr float kDistEps = 1e-3f;
    if (centerDist > suburbanMaxDist + kDistEps) {
        return FrontierBand::Rural;
    }
    if (coreEnabled && centerDist <= coreMaxDist + kDistEps) {
        return FrontierBand::Core;
    }
    return FrontierBand::Suburban;
}

}  // namespace

bool roadBandChanged(const Town& town, const Road& road, float prevSuburbanDist,
                     float prevCoreDist) {
    const float midDist        = roadMidpointCenterDist(town, road);
    const bool  prevCoreEnabled = prevCoreDist >= 0.f;
    const FrontierBand oldBand =
        classifyBandAt(midDist, prevSuburbanDist, prevCoreDist, prevCoreEnabled);
    const FrontierBand newBand = classifyFrontierBand(midDist, town);
    return oldBand != newBand;
}

bool buildingCompatibleWithRoadBand(const DefCache& defs, const BuildingInstance& inst,
                                    const Town& town, int roadId) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return false;
    }
    const BuildingDef* def = defs.building(inst.typeId);
    if (def == nullptr) {
        return true;
    }
    const std::string& typeName = defs.typeName(inst.typeId);
    const char*        zone     = zoneTypeForBuilding(defs, typeName);
    if (zone != nullptr && std::strcmp(zone, "any") == 0) {
        return true;
    }

    const float dist =
        roadMidpointCenterDist(town, town.roads[static_cast<std::size_t>(roadId)]);
    if (zone != nullptr && std::strcmp(zone, "rural") == 0) {
        return distInFilter(dist, BandFilter::rural(town));
    }
    return distInFilter(dist, BandFilter::suburban(town));
}
