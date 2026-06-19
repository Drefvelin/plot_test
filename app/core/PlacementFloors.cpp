#include "PlacementFloors.h"

#include "PlotDimensions.h"

#include <algorithm>
#include <cmath>
#include <limits>

PlacementFloors PlacementFloors::fromDefs(const DefCache& defs, float maxDepthToFrontRatio) {
    PlacementFloors floors;

    float minPlotArea = std::numeric_limits<float>::max();
    for (const auto& [name, band] : defs.plotSizes()) {
        (void)name;
        if (band.minArea > 0.f) {
            minPlotArea = std::min(minPlotArea, band.minArea);
        }
    }
    if (minPlotArea < std::numeric_limits<float>::max() && maxDepthToFrontRatio > 1e-6f) {
        floors.minPlotFrontage = std::max(1.f, std::sqrt(minPlotArea / maxDepthToFrontRatio));
    }
    floors.minPlotDepth = minPlotDepthForSmallestPlot(defs, maxDepthToFrontRatio);

    float minBuildingArea = std::numeric_limits<float>::max();
    for (const auto& [name, band] : defs.buildingSizes()) {
        (void)name;
        if (band.minArea > 0.f) {
            minBuildingArea = std::min(minBuildingArea, band.minArea);
        }
    }
    if (minBuildingArea < std::numeric_limits<float>::max()) {
        floors.minGapWidth = std::max(2.f, std::sqrt(minBuildingArea / kBuildingAspectMax));
    }

    return floors;
}
