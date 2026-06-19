#pragma once

#include "config/DefCache.h"
#include "terrain/Terrain.h"
#include "town/Town.h"

#include <SFML/Graphics.hpp>

#include <string>
#include <functional>
#include <vector>

struct TerrainAtlas;
struct BuildingTerrainRules;
struct BorderSlotRef;

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

enum class BorderHugReject {
    None,
    TooSmall,
    BorderMaxDist,
    RoadCross,
    Overlap,
    FrontageTooWide,
    SegmentTFit,
};

struct BorderHugRejectStats {
    int tooSmall        = 0;
    int borderMaxDist   = 0;
    int roadCross       = 0;
    int overlap         = 0;
    int frontageTooWide = 0;
    int segmentTFit     = 0;

    void record(BorderHugReject reason);
    BorderHugReject primary() const;
};

const char* borderHugRejectName(BorderHugReject reason);
std::string formatBorderHugRejectStats(const BorderHugRejectStats& stats);

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

bool tryMainBorderBandOnPlot(const Plot& plot, const ResolvedBuildingSpec& spec, int buildingId,
                             int townSeed, BuildingFootprint& out);

bool tryMainBorderHugFromPlot(const Plot& plot, const BorderSlotRef& slot,
                              const ResolvedBuildingSpec& spec, int buildingId, int townSeed,
                              const BuildingTerrainRules& rules, const TerrainAtlas& terrain,
                              const Town& town, BuildingFootprint& out,
                              BorderHugRejectStats* rejectStats = nullptr);

bool tryMainBorderHugFromSlot(const BorderSlotRef& slot, float roadT, const Vec2& roadStart,
                              const Vec2& edgeDir, const Vec2& bankInward,
                              const ResolvedBuildingSpec& spec, int buildingId, int townSeed,
                              const BuildingTerrainRules& rules, const TerrainAtlas& terrain,
                              const Town& town, BuildingFootprint& out,
                              BorderHugRejectStats* rejectStats = nullptr);

void assignDoorEdgeTowardHint(BuildingFootprint& footprint, const Vec2& faceHint,
                              const BuildingTemplateRules& rules, int buildingId);

bool layoutSecondaryBuildingsOnPlot(const Plot& plot, const Town& town,
                                    const std::vector<ResolvedBuildingSpec>& specs, int buildingId,
                                    int townSeed, std::vector<BuildingFootprint>& ioFootprints);

void assignBuildingDoorEdge(BuildingFootprint& footprint, const Plot& plot, const Town& town,
                            const BuildingTemplateRules& rules, int buildingId);

void refreshBuildingDoorEdges(Town& town, const DefCache& defs);
void assignGapFillDoorEdge(BuildingFootprint& footprint, int roadId, int bankIndex, const Town& town);
void removeSecondariesOverlappingMain(Town& town, const BuildingFootprint& newMain, int sourcePlotId);
void appendBuildingFootprintOutlines(
    sf::VertexArray& mesh, const Town& town, float pixelsPerUnit,
    const std::function<bool(const BuildingInstance&)>& includeInstance = {});
void appendBuildingFootprintOutlines(Town& town, float pixelsPerUnit);
