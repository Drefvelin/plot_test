#pragma once

#include "BuildingGrowthQueue.h"
#include "TerrainAtlas.h"

#include <SFML/Graphics.hpp>

class Hud {
public:
    Hud(const sf::RenderWindow& window, BuildingGrowthQueue& growthQueue);

    void setTerrainControls(TerrainOverlayMode* overlayMode, bool atlasValid);

    void layout(const sf::RenderWindow& window);
    bool handleEvent(const sf::Event& event, const sf::RenderWindow& window);
    void draw(sf::RenderWindow& window) const;

    bool isCapturingMouse() const { return draggingSlider_; }

private:
    int  countFromPixelX(int pixelX) const;
    void setCountFromPixelX(int pixelX);
    void updateLayout(const sf::Vector2u& windowSize);
    bool terrainToggleHitTest(float x, float y) const;
    void cycleTerrainOverlayMode() const;

    BuildingGrowthQueue& growthQueue_;
    sf::Font font_;
    bool                 fontLoaded_ = false;
    bool                 draggingSlider_ = false;

    TerrainOverlayMode* overlayMode_       = nullptr;
    bool                terrainAtlasValid_ = false;

    float trackLeft_   = 0.f;
    float trackTop_    = 0.f;
    float trackWidth_  = 0.f;
    float trackHeight_ = 24.f;
    float handleRadius_ = 10.f;
    float barHeight_   = 48.f;

    float terrainToggleLeft_ = 0.f;
    float terrainToggleTop_  = 0.f;
    float toggleWidth_       = 120.f;
    float toggleHeight_      = 22.f;
    float toggleGap_         = 8.f;
};
