#include "PlotGeneratorCell.h"

#include <algorithm>
#include <cmath>

int generateRoadPlots(Cell& cell, int roadId, const Town& town, const PlotConfig& plots,
                      int nextPlotId) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return nextPlotId;
    }
    const Road& road = town.roads[static_cast<std::size_t>(roadId)];

    Vec2 a{};
    Vec2 b{};
    if (road.cellA == cell.id) {
        a = road.a;
        b = road.b;
    } else if (road.cellB == cell.id) {
        a = road.b;
        b = road.a;
    } else {
        return nextPlotId;
    }

    const Vec2 edgeDir = (b - a).normalized();
    if (edgeDir.length() < 1e-4f) {
        return nextPlotId;
    }

    const Vec2 mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    Vec2 inward = perpendicular(edgeDir);
    if (inward.dot(cell.centroid - mid) < 0.f) {
        inward = inward * -1.f;
    }

    const float roadLen = (b - a).length();
    if (roadLen < 1e-3f) {
        return nextPlotId;
    }

    float depth = inward.dot(cell.centroid - a);
    depth = std::max(depth, 1.f);

    const float targetArea = (plots.minArea + plots.maxArea) * 0.5f;
    float frontage = targetArea / depth;
    frontage = std::clamp(frontage, plots.minArea / depth, plots.maxArea / depth);

    const int plotCountRaw = std::max(1, static_cast<int>(std::floor(roadLen / frontage)));
    const int plotCount = std::min(plotCountRaw, 256);
    frontage = roadLen / static_cast<float>(plotCount);

    float area = frontage * depth;
    if (area < plots.minArea) {
        depth = plots.minArea / frontage;
        area = frontage * depth;
    } else if (area > plots.maxArea) {
        depth = plots.maxArea / frontage;
        area = frontage * depth;
    }

    for (int i = 0; i < plotCount; ++i) {
        const float t0 = frontage * static_cast<float>(i);
        Plot plot;
        plot.id = nextPlotId++;
        plot.cellId = cell.id;
        plot.roadId = roadId;
        plot.frontage = frontage;
        plot.depth = depth;
        plot.area = area;
        plot.corners[0] = a + edgeDir * t0;
        plot.corners[1] = a + edgeDir * (t0 + frontage);
        plot.corners[2] = plot.corners[1] + inward * depth;
        plot.corners[3] = plot.corners[0] + inward * depth;
        cell.plots.push_back(plot);
    }

    return nextPlotId;
}
