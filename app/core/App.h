#pragma once

#include "BuildingGrowthQueue.h"
#include "Camera.h"
#include "Config.h"
#include "DefCache.h"
#include "Hud.h"
#include "TerrainAtlas.h"
#include "Town.h"
#include "PlacementFloors.h"
#include "TownConfig.h"

#include <SFML/Graphics.hpp>

class App {
public:
    App(const Config& config, const TownConfig& townConfig, const DefCache& defs,
        TerrainAtlas terrainAtlas, PlacementFloors placementFloors, GrowthConfig growthAuto = {});

    int run();

private:
    sf::Color toColor(const std::array<uint8_t, 3>& rgb) const;
    void drawCenterCross(sf::RenderTexture& target, float cx, float cy) const;
    void buildDiagram();
    void syncBuildingPlacements();
    void tickAutoGrow();
    int  finishAutoGrowAndExit();
    int  effectiveAutoGrowTarget() const;
    void drawFrontageSegmentLabels();
    void drawPlotLabels();
    void drawBuildingLabels();
    void drawIdLabels(const std::vector<FrontageSegmentLabel>& labels);

    Config              config_;
    TownConfig          townConfig_;
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
    GrowthConfig        growthAuto_;
    bool                inAppAutoGrow_      = false;
    sf::Clock           autoGrowClock_;
    bool                autoGrowFinished_   = false;
};
