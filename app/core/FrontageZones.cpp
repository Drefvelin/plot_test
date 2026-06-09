#include "FrontageZones.h"

#include "FrontagePlacement.h"
#include "PlotGeometry.h"

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

int findJunctionAt(const Vec2& p, const std::vector<Junction>& junctions, float eps = 0.08f) {
    for (std::size_t i = 0; i < junctions.size(); ++i) {
        if ((junctions[static_cast<std::size_t>(i)].pos - p).length() <= eps) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

std::vector<int> computeJunctionHopDistances(const Town& town) {
    std::vector<int> hops(town.junctions.size(), -1);
    if (town.junctions.empty()) {
        return hops;
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
    hops[static_cast<std::size_t>(startJunction)] = 0;
    queue.push_back(startJunction);

    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
        const int          curJ     = queue[qi];
        const Junction&    junction = town.junctions[static_cast<std::size_t>(curJ)];
        const int          curHops  = hops[static_cast<std::size_t>(curJ)];

        for (const int roadId : junction.roadIds) {
            if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
                continue;
            }
            const Road& road = town.roads[static_cast<std::size_t>(roadId)];
            const int   otherJ =
                findJunctionAt((junction.pos - road.a).length() <= (junction.pos - road.b).length()
                                   ? road.b
                                   : road.a,
                               town.junctions);
            if (otherJ < 0 || hops[static_cast<std::size_t>(otherJ)] >= 0) {
                continue;
            }
            hops[static_cast<std::size_t>(otherJ)] = curHops + 1;
            queue.push_back(otherJ);
        }
    }

    return hops;
}

int segmentMinJunctionHops(const Town& town, int roadId, const std::vector<int>& junctionHops) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return 999;
    }
    const Road& road = town.roads[static_cast<std::size_t>(roadId)];
    const int   ja   = findJunctionAt(road.a, town.junctions);
    const int   jb   = findJunctionAt(road.b, town.junctions);

    int hops = 999;
    if (ja >= 0 && ja < static_cast<int>(junctionHops.size()) && junctionHops[static_cast<std::size_t>(ja)] >= 0) {
        hops = std::min(hops, junctionHops[static_cast<std::size_t>(ja)]);
    }
    if (jb >= 0 && jb < static_cast<int>(junctionHops.size()) && junctionHops[static_cast<std::size_t>(jb)] >= 0) {
        hops = std::min(hops, junctionHops[static_cast<std::size_t>(jb)]);
    }
    return hops;
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
    if (!roadFrameForCell(road, slot.cellId, origin, farEnd, edgeDir)) {
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

float scoreSegmentForZone(const Town& town, const FrontageSlot& slot, const char* zone,
                          float townGrowth, const std::vector<int>& junctionHops) {
    if (slot.centerDist + 1e-3f < minCenterDistForZone(zone, townGrowth)) {
        return 1e9f;
    }

    if (std::strcmp(zone, "rural") == 0) {
        const int hops = segmentMinJunctionHops(town, slot.roadId, junctionHops);
        if (hops < minJunctionHopsForRural(townGrowth)) {
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

        if (slot.centerDist > ruralMaxCenterDist(town, townGrowth) + 1e-3f) {
            return 1e9f;
        }

        const float targetDist    = ruralTargetCenterDist(town, townGrowth);
        const float distFromTarget = std::abs(slot.centerDist - targetDist);
        return distFromTarget - buildingDist * 0.08f - static_cast<float>(hops) * 2.f;
    }

    return slot.centerDist;
}

float bandMinFrontage(const DefCache& defs, const std::string& buildingType, float /*segWidth*/,
                      float maxDepthToFrontRatio) {
    const SizeBand* band = defs.sizeBandForBuilding(buildingType);
    if (!band) {
        return 0.f;
    }
    return std::max(1.f, std::sqrt(band->minArea / maxDepthToFrontRatio));
}

