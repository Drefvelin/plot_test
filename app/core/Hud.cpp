#include "Hud.h"

#include "TerrainAtlas.h"

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

void Hud::layout(const sf::RenderWindow& window) {
    updateLayout(window.getSize());
}

void Hud::updateLayout(const sf::Vector2u& windowSize) {
    trackLeft_  = kMargin;
    trackTop_   = kMargin + 18.f;
    trackWidth_ = std::max(100.f, static_cast<float>(windowSize.x) - kMargin * 2.f);

    terrainToggleTop_  = 4.f;
    terrainToggleLeft_ = static_cast<float>(windowSize.x) - kMargin - toggleWidth_;
}

void Hud::cycleTerrainOverlayMode() const {
    if (overlayMode_ == nullptr || !terrainAtlasValid_) {
        return;
    }
    const int mode = static_cast<int>(*overlayMode_);
    *overlayMode_  = static_cast<TerrainOverlayMode>((mode + 1) % 3);
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

    if (fontLoaded_) {
        sf::Text label;
        label.setFont(font_);
        label.setCharacterSize(14);
        label.setFillColor(sf::Color(230, 230, 235));
        label.setPosition(kMargin, 4.f);
        label.setString("Town growth: " + std::to_string(active) + " / " + std::to_string(maxCount)
                          + " buildings");

        const std::string nextType = growthQueue_.nextBuildingType();
        if (!nextType.empty()) {
            label.setString(label.getString() + "  |  next: " + nextType);
        } else if (maxCount > 0 && active >= maxCount) {
            label.setString(label.getString() + "  |  complete");
        }

        window.draw(label);
    }

    window.setView(previous);
}
