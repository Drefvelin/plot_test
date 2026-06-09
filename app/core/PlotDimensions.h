#pragma once

#include "DefCache.h"
#include "Town.h"

#include <string>

enum class DimReject {
    None,
    MissingBand,
    InvalidInput,
    RoadTooShort,
    DepthRatioExceeded,
    AreaOutOfBand,
    DepthExceedsCell,
};

enum class PlotOrientation {
    Horizontal,
    Vertical,
};

struct PlotDimensions {
    float frontage = 0.f;
    float depth    = 0.f;
    float area     = 0.f;
    bool  valid    = false;
};

constexpr float kBuildingAspectMax = 2.f;  // neither side more than 2x the other

bool aspectRatioOk(float frontage, float depth, float maxRatio = kBuildingAspectMax);

const char* orientationName(PlotOrientation orient);
std::string fmt1(float value);
void sampleOrientationOrder(int buildingId, int townSeed, PlotOrientation& first,
                            PlotOrientation& second);
PlotDimensions computePlotDimensionsForRoad(const DefCache& defs, const std::string& buildingType,
                                            float targetArea, PlotOrientation orient,
                                            const Vec2& roadStart, const Vec2& edgeDir,
                                            float maxFrontage, const Vec2& inward, const Cell& cell,
                                            float maxDepthToFrontRatio, float frontageSetback,
                                            DimReject* rejectOut = nullptr,
                                            const SizeBand* plotAreaBand = nullptr);
const char* rejectName(DimReject reason);
