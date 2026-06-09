#pragma once

#include "DefCache.h"
#include "Town.h"

#include <SFML/Graphics.hpp>

#include <string>
#include <vector>

struct ResolvedBuildingSpec {
    std::string           sizeCategory;
    float                 area = 0.f;
    bool                  isMain = false;
    BuildingTemplateRules rules;
};

struct PlotAreaBand {
    float minArea = 0.f;
    float maxArea = 0.f;
};

PlotAreaBand computePlotAreaBand(const DefCache& defs, const std::string& buildingType,
                                 int buildingId, int townSeed);

float samplePlotTargetArea(const DefCache& defs, const std::string& buildingType, int buildingId,
                           int townSeed);

std::vector<ResolvedBuildingSpec> resolveBuildingSpecs(const DefCache& defs,
                                                       const std::string& buildingType,
                                                       int buildingId, int townSeed);

bool resolveMainBuildingSpec(const DefCache& defs, const std::string& buildingType, int buildingId,
                             int townSeed, ResolvedBuildingSpec& out);

void copyTemplateRulesToFootprint(const BuildingTemplateRules& rules, BuildingFootprint& footprint);

bool layoutBuildingsOnPlot(const Plot& plot, const Town& town,
                           const std::vector<ResolvedBuildingSpec>& specs, int buildingId,
                           int townSeed, std::vector<BuildingFootprint>& out);

void refreshBuildingDoorEdges(Town& town, const DefCache& defs);
void assignGapFillDoorEdge(BuildingFootprint& footprint, int roadId, int cellId, const Town& town);
void removeSecondariesOverlappingMain(Town& town, const BuildingFootprint& newMain, int sourcePlotId);
void appendBuildingFootprintOutlines(Town& town, float pixelsPerUnit);
