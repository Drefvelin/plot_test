#pragma once

#include "DefCache.h"
#include "Town.h"

#include <SFML/Graphics.hpp>

#include <string>

struct FrontageSlot;
struct Road;

enum class PlacementBand {
    Unknown,
    Core,
    Suburban,
    Rural,
};

float         roadMidpointCenterDist(const Town& town, const Road& road);
float         ringDistAtHop(const Town& town, int hop);
float         suburbanMaxDist(const Town& town);
float         urbanCoreMaxDist(const Town& town);
bool          hasRoadOutsideSuburbanBand(const Town& town);
PlacementBand classifyPlacementBandByDist(float dist, const Town& town);
sf::Color     placementBandColor(PlacementBand band);
std::string   formatPlacementBandDistRanges(const Town& town);

void invalidateJunctionHopCache(Town& town);
const std::vector<int>& getJunctionHops(const Town& town);
int getRoadHop(const Town& town, int roadId);

std::vector<int> computeJunctionHopDistances(const Town& town);
int segmentMinJunctionHops(const Town& town, int roadId, const std::vector<int>& junctionHops);
int roadHop(const Town& town, int roadId, const std::vector<int>& junctionHops);

int minJunctionHopsForRural(float townGrowth);
float minBuildingSeparationForRural(float townGrowth);
float ruralTargetCenterDist(const Town& town, float townGrowth);
float ruralMaxCenterDist(const Town& town, float townGrowth);
float zoneBiasForType(const char* zone, float townGrowth);
float scoreSegmentForZone(const Town& town, const FrontageSlot& slot, const char* zone,
                          float townGrowth);
const char* zoneTypeForBuilding(const DefCache& defs, const std::string& buildingType);
float bandMinFrontage(const DefCache& defs, const std::string& buildingType, float segWidth,
                      float maxDepthToFrontRatio);
