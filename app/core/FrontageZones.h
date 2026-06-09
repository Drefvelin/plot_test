#pragma once

#include "DefCache.h"
#include "Town.h"

#include <string>
#include <vector>

struct FrontageSlot;

std::vector<int> computeJunctionHopDistances(const Town& town);
int minJunctionHopsForRural(float townGrowth);
float minBuildingSeparationForRural(float townGrowth);
float ruralTargetCenterDist(const Town& town, float townGrowth);
float ruralMaxCenterDist(const Town& town, float townGrowth);
float zoneBiasForType(const char* zone, float townGrowth);
float scoreSegmentForZone(const Town& town, const FrontageSlot& slot, const char* zone,
                          float townGrowth, const std::vector<int>& junctionHops);
const char* zoneTypeForBuilding(const DefCache& defs, const std::string& buildingType);
float bandMinFrontage(const DefCache& defs, const std::string& buildingType, float segWidth,
                      float maxDepthToFrontRatio);
