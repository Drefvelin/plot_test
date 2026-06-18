#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

struct TownConfig {
    std::unordered_map<std::string, int> buildingCounts;
    int initialSuburbanMaxHops = 2;
    float minWallGapForAlley   = 4.f;
    float minAlleyLength       = 12.f;
    float maxAlleyLength       = 0.f;  // 0 = no cap
    float maxAlleyAngleDeg     = 20.f;  // max deviation from perpendicular inward
    float alleysPerUnitLength  = 0.25f;
    int   alleyAngleCount      = 5;
    float minAlleyCrossingAngleDeg   = 80.f;
    float minAlleyBankAngleSepDeg    = 25.f;
    float minAlleyEndpointSpacing  = 20.f;
    float minAlleyCreatedArea      = 0.f;  // 0 = no minimum
    float minAlleySideRoadDist     = 0.f;  // 0 = no minimum
    int   alleySideRoadSampleCount = 5;
    int   alleyFillFailLimit       = 5;
    float borderOutlineProbeMaxDist = 20.f;
    float borderOutlineSampleStep   = 3.f;
    int   borderMaxAttempts         = 32;

    int totalBuildings() const;
    int countFor(const std::string& buildingType) const;

    static TownConfig load(const std::filesystem::path& path);
    static std::filesystem::path resolveTownPath();
};
