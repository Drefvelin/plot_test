#include "PlotDimensions.h"

#include "PlotGeometry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>

const char* orientationName(PlotOrientation orient) {
    return orient == PlotOrientation::Horizontal ? "horizontal" : "vertical";
}

bool aspectRatioOk(float frontage, float depth, float maxRatio) {
    return depth <= maxRatio * frontage + 1e-3f && frontage <= maxRatio * depth + 1e-3f;
}

bool satisfiesOrientation(float frontage, float depth, PlotOrientation orient) {
    if (orient == PlotOrientation::Horizontal) {
        return frontage + 1e-3f >= depth;
    }
    return depth > frontage + 1e-3f;
}

float sampleTargetArea(const DefCache& defs, const std::string& buildingType, int buildingId,
                       int townSeed) {
    const SizeBand* band = defs.sizeBandForBuilding(buildingType);
    if (!band) {
        return 0.f;
    }
    if (band->maxArea <= band->minArea + 1e-3f) {
        return band->minArea;
    }
    const std::seed_seq seed{townSeed, buildingId};
    std::mt19937                          rng(seed);
    std::uniform_real_distribution<float> areaDist(band->minArea, band->maxArea);
    return areaDist(rng);
}

void sampleOrientationOrder(int buildingId, int townSeed, PlotOrientation& first,
                            PlotOrientation& second) {
    const std::seed_seq seed{townSeed, buildingId, 7919};
    std::mt19937                       rng(seed);
    std::uniform_int_distribution<int> coin(0, 1);
    if (coin(rng) == 0) {
        first  = PlotOrientation::Horizontal;
        second = PlotOrientation::Vertical;
    } else {
        first  = PlotOrientation::Vertical;
        second = PlotOrientation::Horizontal;
    }
}

PlotDimensions computePlotDimensionsAtArea(const SizeBand& band, float area, float availableDepth,
                                           float maxFrontage, float maxDepthToFrontRatio,
                                           PlotOrientation orient, DimReject* rejectOut = nullptr) {
    PlotDimensions dims;
    auto fail = [&](DimReject reason) {
        if (rejectOut) {
            *rejectOut = reason;
        }
        return dims;
    };

    if (area < band.minArea - 1e-3f || area > band.maxArea + 1e-3f || availableDepth < 1e-3f
        || maxFrontage < 1e-3f || maxDepthToFrontRatio < 1e-3f) {
        return fail(DimReject::InvalidInput);
    }

    const float sqrtArea        = std::sqrt(area);
    const float ratioCapFront   = std::sqrt(area * maxDepthToFrontRatio);
    const float ratioCapDepth   = std::sqrt(area / maxDepthToFrontRatio);
    float       frontage      = 0.f;
    float       depth         = 0.f;

    if (orient == PlotOrientation::Horizontal) {
        const float minFrontage = std::max({1.f, sqrtArea, ratioCapDepth, area / availableDepth});
        const float maxFrontageForRatio = ratioCapFront;
        const float maxFrontageFeasible = std::min(maxFrontage, maxFrontageForRatio);

        if (minFrontage > maxFrontageFeasible + 1e-3f) {
            return fail(DimReject::DepthRatioExceeded);
        }

        frontage = maxFrontageFeasible;
        depth    = area / frontage;
    } else {
        const float minFrontage = std::max({1.f, ratioCapDepth, area / availableDepth});
        const float maxFrontageForOrient = sqrtArea - 1e-3f;
        const float maxFrontageForRatio  = ratioCapFront;
        const float maxFrontageFeasible =
            std::min({maxFrontage, maxFrontageForRatio, maxFrontageForOrient});

        if (minFrontage > maxFrontageFeasible + 1e-3f) {
            return fail(DimReject::DepthRatioExceeded);
        }

        frontage = minFrontage;
        depth    = area / frontage;
    }

    if (depth > availableDepth + 1e-3f) {
        return fail(DimReject::DepthExceedsCell);
    }
    if (!aspectRatioOk(frontage, depth, maxDepthToFrontRatio)) {
        return fail(DimReject::DepthRatioExceeded);
    }
    if (!satisfiesOrientation(frontage, depth, orient)) {
        return fail(DimReject::DepthRatioExceeded);
    }
    if (frontage > maxFrontage + 1e-3f) {
        return fail(DimReject::RoadTooShort);
    }

    const float finalArea = frontage * depth;
    if (finalArea < band.minArea - 1e-3f || finalArea > band.maxArea + 1e-3f) {
        return fail(DimReject::AreaOutOfBand);
    }

    if (rejectOut) {
        *rejectOut = DimReject::None;
    }
    dims.frontage = frontage;
    dims.depth    = depth;
    dims.area     = finalArea;
    dims.valid    = true;
    return dims;
}

PlotDimensions fitPlotDimensions(const DefCache& defs, const std::string& buildingType,
                                 float targetArea, float availableDepth, float maxFrontage,
                                 float maxDepthToFrontRatio, PlotOrientation orient,
                                 DimReject* rejectOut = nullptr,
                                 const SizeBand* plotAreaBand = nullptr) {
    PlotDimensions dims;
    const SizeBand* band = plotAreaBand ? plotAreaBand : defs.sizeBandForBuilding(buildingType);
    if (!band) {
        if (rejectOut) {
            *rejectOut = DimReject::MissingBand;
        }
        return dims;
    }

    float lo = band->minArea;
    float hi = std::min(targetArea, band->maxArea);
    if (hi < lo + 1e-3f) {
        return computePlotDimensionsAtArea(*band, lo, availableDepth, maxFrontage,
                                           maxDepthToFrontRatio, orient, rejectOut);
    }

    PlotDimensions best;
    DimReject      bestReject = DimReject::InvalidInput;
    for (int i = 0; i < 18; ++i) {
        const float mid = (lo + hi) * 0.5f;
        DimReject   reject = DimReject::None;
        const PlotDimensions candidate =
            computePlotDimensionsAtArea(*band, mid, availableDepth, maxFrontage,
                                        maxDepthToFrontRatio, orient, &reject);
        if (candidate.valid) {
            lo   = mid;
            best = candidate;
        } else {
            hi = mid;
            bestReject = reject;
        }
    }

    if (best.valid) {
        if (rejectOut) {
            *rejectOut = DimReject::None;
        }
        return best;
    }

    DimReject finalReject = DimReject::InvalidInput;
    return computePlotDimensionsAtArea(*band, lo, availableDepth, maxFrontage, maxDepthToFrontRatio,
                                     orient, rejectOut ? rejectOut : &finalReject);
}

PlotDimensions computePlotDimensions(const DefCache& defs, const std::string& buildingType,
                                   float targetArea, float availableDepth, float maxFrontage,
                                   float maxDepthToFrontRatio, PlotOrientation orient,
                                   DimReject* rejectOut) {
    return fitPlotDimensions(defs, buildingType, targetArea, availableDepth, maxFrontage,
                             maxDepthToFrontRatio, orient, rejectOut);
}

PlotDimensions computePlotDimensionsForRoad(const DefCache& defs, const std::string& buildingType,
                                            float targetArea, PlotOrientation orient,
                                            const Vec2& roadStart, const Vec2& edgeDir,
                                            float maxFrontage, const Vec2& inward, const Cell& cell,
                                            float maxDepthToFrontRatio, float frontageSetback,
                                            DimReject* rejectOut, const SizeBand* plotAreaBand) {
    const SizeBand* band = plotAreaBand ? plotAreaBand : defs.sizeBandForBuilding(buildingType);
    if (!band) {
        PlotDimensions dims;
        if (rejectOut) {
            *rejectOut = DimReject::MissingBand;
        }
        return dims;
    }

    const float probeFront = std::min(maxFrontage, std::sqrt(band->maxArea));
    const float depthCap   = maxPlotDepthInCell(roadStart, edgeDir, probeFront, inward,
                                                frontageSetback, cell);
    PlotDimensions dims =
        fitPlotDimensions(defs, buildingType, targetArea, depthCap, maxFrontage,
                          maxDepthToFrontRatio, orient, rejectOut, plotAreaBand);
    if (!dims.valid) {
        return dims;
    }

    const float actualDepthCap = maxPlotDepthInCell(roadStart, edgeDir, dims.frontage, inward,
                                                    frontageSetback, cell);
    return fitPlotDimensions(defs, buildingType, targetArea, actualDepthCap, maxFrontage,
                             maxDepthToFrontRatio, orient, rejectOut, plotAreaBand);
}

const char* rejectName(DimReject reason) {
    switch (reason) {
    case DimReject::MissingBand:
        return "missing_band";
    case DimReject::InvalidInput:
        return "no_depth_cap";
    case DimReject::RoadTooShort:
        return "road_too_short";
    case DimReject::DepthRatioExceeded:
        return "depth_ratio";
    case DimReject::AreaOutOfBand:
        return "area_out_of_band";
    case DimReject::DepthExceedsCell:
        return "depth_exceeds_cell";
    default:
        return "ok";
    }
}

std::string fmt1(float value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << value;
    return oss.str();
}

