#include "Hud.h"



#include "FrontageZones.h"



#include <algorithm>

#include <cmath>



namespace {



constexpr float kMargin = 12.f;



bool pointInRect(float x, float y, float left, float top, float width, float height) {

    return x >= left && x <= left + width && y >= top && y <= top + height;

}



void drawToggleButton(sf::RenderWindow& window, float x, float y, float width, float height,

                      const std::string& label, bool active, bool enabled, const sf::Font* font,

                      bool appendStateSuffix = true) {

    sf::RectangleShape button({width, height});

    button.setPosition(x, y);

    button.setFillColor(enabled ? (active ? sf::Color(90, 150, 90) : sf::Color(70, 70, 76))

                                : sf::Color(55, 55, 58));

    button.setOutlineColor(sf::Color(110, 110, 118));

    button.setOutlineThickness(1.f);

    window.draw(button);



    if (font != nullptr) {

        sf::Text text;

        text.setFont(*font);

        text.setCharacterSize(12);

        text.setFillColor(enabled ? sf::Color(230, 230, 235) : sf::Color(120, 120, 125));

        text.setString(appendStateSuffix ? label + (active ? ": on" : ": off") : label);

        const sf::FloatRect bounds = text.getLocalBounds();

        text.setPosition(x + (width - bounds.width) * 0.5f - bounds.left,

                         y + (height - bounds.height) * 0.5f - bounds.top - 1.f);

        window.draw(text);

    }

}



}  // namespace



Hud::Hud(const sf::RenderWindow& window, BuildingGrowthQueue& growthQueue)

    : growthQueue_(growthQueue) {

    fontLoaded_ = font_.loadFromFile("C:/Windows/Fonts/segoeui.ttf")

                  || font_.loadFromFile("C:/Windows/Fonts/arial.ttf");

    layout(window);

}



void Hud::setTerrainControls(TerrainOverlayMode* overlayMode, bool atlasValid) {

    overlayMode_       = overlayMode;

    terrainAtlasValid_ = atlasValid;

}



void Hud::setZoneTintControl(bool* zoneTintEnabled) {

    zoneTintEnabled_ = zoneTintEnabled;

}

void Hud::setBiomePlotControl(bool* biomePlotsEnabled) {
    biomePlotsEnabled_ = biomePlotsEnabled;
}

void Hud::setBridgeDebugControl(bool* bridgeDebugEnabled, bool bridgesEnabled) {
    bridgeDebugEnabled_ = bridgeDebugEnabled;
    bridgesEnabled_     = bridgesEnabled;
}

void Hud::setPlacementFailures(int count, int moveFailures, int placedCount, const Town& town) {

    placementFailures_ = count;

    moveFailures_        = moveFailures;

    placedCount_       = placedCount;

    placementTown_     = &town;

}



void Hud::layout(const sf::RenderWindow& window) {

    updateLayout(window.getSize());

}



void Hud::updateLayout(const sf::Vector2u& windowSize) {

    trackLeft_  = kMargin;

    trackTop_   = kMargin + 18.f;

    trackWidth_ = std::max(100.f, static_cast<float>(windowSize.x) - kMargin * 2.f);



    terrainToggleTop_  = 4.f;

    terrainToggleLeft_ = static_cast<float>(windowSize.x) - kMargin - toggleWidth_;

    zoneToggleTop_     = terrainToggleTop_;

    zoneToggleLeft_    = terrainToggleLeft_ - toggleGap_ - toggleWidth_;

    biomeToggleTop_  = zoneToggleTop_;
    biomeToggleLeft_ = zoneToggleLeft_ - toggleGap_ - toggleWidth_;
    bridgeToggleTop_  = biomeToggleTop_;
    bridgeToggleLeft_ = biomeToggleLeft_ - toggleGap_ - toggleWidth_;

}



void Hud::cycleTerrainOverlayMode() const {
    if (overlayMode_ == nullptr || !terrainAtlasValid_) {
        return;
    }
    switch (*overlayMode_) {
    case TerrainOverlayMode::TerrainAndDebug:
        *overlayMode_ = TerrainOverlayMode::DebugOnly;
        break;
    case TerrainOverlayMode::DebugOnly:
        *overlayMode_ = TerrainOverlayMode::Off;
        break;
    default:
        *overlayMode_ = TerrainOverlayMode::TerrainAndDebug;
        break;
    }
}



void Hud::toggleBiomePlots() const {
    if (biomePlotsEnabled_ == nullptr) {
        return;
    }
    *biomePlotsEnabled_ = !*biomePlotsEnabled_;
}

void Hud::toggleBridgeDebug() const {
    if (bridgeDebugEnabled_ == nullptr || !bridgesEnabled_) {
        return;
    }
    *bridgeDebugEnabled_ = !*bridgeDebugEnabled_;
}

bool Hud::bridgeDebugToggleHitTest(float x, float y) const {
    return pointInRect(x, y, bridgeToggleLeft_, bridgeToggleTop_, toggleWidth_, toggleHeight_);
}

void Hud::toggleZoneTint() const {

    if (zoneTintEnabled_ == nullptr) {

        return;

    }

    *zoneTintEnabled_ = !*zoneTintEnabled_;

}

bool Hud::biomePlotToggleHitTest(float x, float y) const {
    return pointInRect(x, y, biomeToggleLeft_, biomeToggleTop_, toggleWidth_, toggleHeight_);
}

int Hud::countFromPixelX(int pixelX) const {

    if (growthQueue_.maxBuildings() <= 0) {

        return 0;

    }

    const float t =

        std::clamp((static_cast<float>(pixelX) - trackLeft_) / trackWidth_, 0.f, 1.f);

    return static_cast<int>(std::round(t * static_cast<float>(growthQueue_.maxBuildings())));

}



void Hud::setCountFromPixelX(int pixelX) {

    growthQueue_.setActiveCount(countFromPixelX(pixelX));

}



bool Hud::terrainToggleHitTest(float x, float y) const {

    return pointInRect(x, y, terrainToggleLeft_, terrainToggleTop_, toggleWidth_, toggleHeight_);

}



bool Hud::zoneToggleHitTest(float x, float y) const {

    return pointInRect(x, y, zoneToggleLeft_, zoneToggleTop_, toggleWidth_, toggleHeight_);

}



bool Hud::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {

    if (event.type == sf::Event::Resized) {

        updateLayout(window.getSize());

        return false;

    }



    const auto withinSlider = [&](int x, int y) {

        return pointInRect(static_cast<float>(x), static_cast<float>(y), trackLeft_,

                           trackTop_ - handleRadius_, trackWidth_, trackHeight_ + handleRadius_ * 2.f);

    };



    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {

        if (bridgesEnabled_ && bridgeDebugToggleHitTest(static_cast<float>(event.mouseButton.x),
                                                       static_cast<float>(event.mouseButton.y))) {
            toggleBridgeDebug();
            return true;
        }

        if (biomePlotToggleHitTest(static_cast<float>(event.mouseButton.x),
                                   static_cast<float>(event.mouseButton.y))) {
            toggleBiomePlots();
            return true;
        }

        if (zoneToggleHitTest(static_cast<float>(event.mouseButton.x),

                              static_cast<float>(event.mouseButton.y))) {

            toggleZoneTint();

            return true;

        }

        if (terrainAtlasValid_ && terrainToggleHitTest(static_cast<float>(event.mouseButton.x),

                                                       static_cast<float>(event.mouseButton.y))) {

            cycleTerrainOverlayMode();

            return true;

        }

        if (withinSlider(event.mouseButton.x, event.mouseButton.y)) {

            draggingSlider_ = true;

            setCountFromPixelX(event.mouseButton.x);

            return true;

        }

    }



    if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {

        if (draggingSlider_) {

            draggingSlider_ = false;

            return true;

        }

    }



    if (event.type == sf::Event::MouseMoved && draggingSlider_) {

        setCountFromPixelX(event.mouseMove.x);

        return true;

    }



    return false;

}



void Hud::draw(sf::RenderWindow& window) const {

    const sf::View previous = window.getView();

    window.setView(window.getDefaultView());



    sf::RectangleShape bar({static_cast<float>(window.getSize().x), barHeight_});

    bar.setPosition(0.f, 0.f);

    bar.setFillColor(sf::Color(40, 40, 44, 230));

    window.draw(bar);



    sf::RectangleShape track({trackWidth_, trackHeight_});

    track.setPosition(trackLeft_, trackTop_);

    track.setFillColor(sf::Color(70, 70, 76));

    track.setOutlineColor(sf::Color(110, 110, 118));

    track.setOutlineThickness(1.f);

    window.draw(track);



    const int maxCount = growthQueue_.maxBuildings();

    const int active   = growthQueue_.activeCount();

    const float fillRatio =

        maxCount > 0 ? static_cast<float>(active) / static_cast<float>(maxCount) : 0.f;



    sf::RectangleShape fill({trackWidth_ * fillRatio, trackHeight_});

    fill.setPosition(trackLeft_, trackTop_);

    fill.setFillColor(sf::Color(90, 150, 90));

    window.draw(fill);



    const float handleX =

        trackLeft_ + (maxCount > 0 ? fillRatio * trackWidth_ : 0.f);

    const float handleY = trackTop_ + trackHeight_ * 0.5f;



    sf::CircleShape handle(handleRadius_);

    handle.setOrigin(handleRadius_, handleRadius_);

    handle.setPosition(handleX, handleY);

    handle.setFillColor(sf::Color(210, 210, 220));

    handle.setOutlineColor(sf::Color(40, 40, 48));

    handle.setOutlineThickness(2.f);

    window.draw(handle);



    const sf::Font* fontPtr = fontLoaded_ ? &font_ : nullptr;

    const std::string terrainLabel =

        std::string("Terrain: ")

        + (overlayMode_ != nullptr ? terrainOverlayModeLabel(*overlayMode_) : "off");

    drawToggleButton(window, terrainToggleLeft_, terrainToggleTop_, toggleWidth_, toggleHeight_,

                     terrainLabel,

                     overlayMode_ != nullptr && *overlayMode_ != TerrainOverlayMode::Off,

                     terrainAtlasValid_, fontPtr, false);



    const bool zonesOn = zoneTintEnabled_ != nullptr && *zoneTintEnabled_;

    drawToggleButton(window, zoneToggleLeft_, zoneToggleTop_, toggleWidth_, toggleHeight_, "Zones",

                     zonesOn, true, fontPtr);

    const bool biomeOn = biomePlotsEnabled_ != nullptr && *biomePlotsEnabled_;
    drawToggleButton(window, biomeToggleLeft_, biomeToggleTop_, toggleWidth_, toggleHeight_,
                     "Biome plots", biomeOn, true, fontPtr);
    const bool bridgeOn = bridgeDebugEnabled_ != nullptr && *bridgeDebugEnabled_;
    drawToggleButton(window, bridgeToggleLeft_, bridgeToggleTop_, toggleWidth_, toggleHeight_,
                     "Bridges", bridgeOn, bridgesEnabled_, fontPtr);

    if (fontLoaded_) {

        sf::Text label;

        label.setFont(font_);

        label.setCharacterSize(14);

        label.setFillColor(sf::Color(230, 230, 235));

        label.setPosition(kMargin, 4.f);

        label.setString(std::to_string(placedCount_) + " placed / " + std::to_string(maxCount)

                          + " target");



        const std::string nextType = growthQueue_.nextBuildingType();

        if (!nextType.empty()) {

            label.setString(label.getString() + "  |  next: " + nextType);

        } else if (maxCount > 0 && active >= maxCount) {

            label.setString(label.getString() + "  |  complete");

        }



        window.draw(label);



        sf::Text failureLabel;

        failureLabel.setFont(font_);

        failureLabel.setCharacterSize(13);

        failureLabel.setFillColor(placementFailures_ > 0 ? sf::Color(220, 80, 80)

                                                         : sf::Color(120, 180, 120));

        failureLabel.setPosition(kMargin, trackTop_ + trackHeight_ + 4.f);

        failureLabel.setString("Failures: " + std::to_string(placementFailures_));

        window.draw(failureLabel);



        sf::Text moveFailureLabel;

        moveFailureLabel.setFont(font_);

        moveFailureLabel.setCharacterSize(13);

        moveFailureLabel.setFillColor(moveFailures_ > 0 ? sf::Color(220, 80, 80)

                                                        : sf::Color(120, 180, 120));

        moveFailureLabel.setPosition(kMargin, trackTop_ + trackHeight_ + 18.f);

        moveFailureLabel.setString("Move failures: " + std::to_string(moveFailures_));

        window.draw(moveFailureLabel);



        sf::Text bandLabel;

        bandLabel.setFont(font_);

        bandLabel.setCharacterSize(12);

        bandLabel.setFillColor(sf::Color(180, 180, 190));

        bandLabel.setPosition(kMargin, trackTop_ + trackHeight_ + 36.f);

        if (placementTown_ != nullptr) {
            bandLabel.setString(formatPlacementBandDistRanges(*placementTown_));
        }

        window.draw(bandLabel);

    }



    window.setView(previous);

}

