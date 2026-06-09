#include "PlotGenerator.h"

#include "Logger.h"
#include "PlotGeneratorCell.h"

void PlotGenerator::generate(Town& town, const Config& config) {
    int nextPlotId = 0;
    std::size_t cellsWithPlots = 0;

    for (auto& cell : town.cells) {
        cell.plots.clear();

        for (const int roadId : cell.roadIds) {
            nextPlotId = generateRoadPlots(cell, roadId, town, config.plots, nextPlotId);
        }

        if (!cell.plots.empty()) {
            ++cellsWithPlots;
        }
    }

    Logger::log("voronoi", "plots generated total=" + std::to_string(town.plotCount()) + " cells_with_plots="
                                + std::to_string(cellsWithPlots) + " min_area="
                                + std::to_string(config.plots.minArea) + " max_area="
                                + std::to_string(config.plots.maxArea));
}
