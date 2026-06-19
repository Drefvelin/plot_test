#pragma once

#include <unordered_set>

#include "placement/orchestration/BuildingGrowthQueue.h"
#include "render/Camera.h"
#include "config/Config.h"
#include "config/DefCache.h"
#include "render/Hud.h"
#include "terrain/TerrainAtlas.h"
#include "terrain/TerrainCatalog.h"
#include "town/Town.h"
#include "placement/orchestration/PlacementFloors.h"
#include "config/TownConfig.h"

#include <SFML/Graphics.hpp>

class App {
public:
    App(const Config& config, const TownConfig& townConfig, const TerrainCatalog& terrainCatalog,
        const DefCache& defs, TerrainAtlas terrainAtlas, PlacementFloors placementFloors,
        GrowthConfig growthAuto = {});

    int run();

private:
    void drawCenterCross(sf::RenderTexture& target, float cx, float cy) const;
    void buildDiagram();
    void syncBuildingPlacements();
    void tickAutoGrow();
    int  finishAutoGrowAndExit();
    void writeMemoryReportOnce();
    int  effectiveAutoGrowTarget() const;
    void drawFrontageSegmentLabels();
    void drawPlotLabels();
    void drawBuildingLabels();
    void drawRoadLabels();
    void drawBridgeDebugLabels();
    void drawTerrainPlotTypeLabels();
    void drawRotatedTextLabels(const std::vector<RotatedTextLabel>& labels, unsigned characterSize);
    void drawIdLabels(const std::vector<FrontageSegmentLabel>& labels);
    void drawIdLabels(const std::vector<FrontageSegmentLabel>& labels,
                      const std::unordered_set<int>* onlyIds);
    std::unordered_set<int> terrainBuildingLabelIds() const;

    Config              config_;
    TownConfig          townConfig_;
    TerrainCatalog      terrainCatalog_;
    DefCache            defs_;
    PlacementFloors     placementFloors_;
    TerrainAtlas        terrainAtlas_;
    Town                town_;
    BuildingGrowthQueue growthQueue_;
    sf::RenderWindow    window_;
    sf::RenderTexture   diagramTexture_;
    sf::Sprite          diagramSprite_;
    sf::Sprite          terrainSprite_;
    sf::Font            labelFont_;
    bool                labelFontLoaded_ = false;
    Camera              camera_;
    Hud                 hud_;
    int                 lastActiveCount_ = -1;
    TerrainOverlayMode  terrainOverlayMode_ = TerrainOverlayMode::TerrainAndDebug;
    bool                hopZoneTintEnabled_ = false;
    bool                showBiomePlots_     = false;
    bool                showBridgeDebug_    = false;
    GrowthConfig        growthAuto_;
    bool                inAppAutoGrow_      = false;
    sf::Clock           autoGrowClock_;
    bool                autoGrowFinished_   = false;
    bool                memoryReportWritten_ = false;
};
