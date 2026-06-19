#pragma once

#include <SFML/Graphics.hpp>

class Camera {
public:
    Camera(float worldWidth, float worldHeight, float pixelsPerUnit, const sf::RenderWindow& window);

    void handleEvent(const sf::Event& event, const sf::RenderWindow& window);

    const sf::View& getView() const { return view_; }

private:
    sf::View     view_;
    bool         isPanning_    = false;
    sf::Vector2i lastMousePos_ = {};
    float        zoomLevel_    = 1.f;
    float        minZoom_      = 0.1f;
    float        maxZoom_      = 10.f;

    static constexpr float kZoomInFactor  = 0.85f;
    static constexpr float kZoomOutFactor = 1.f / kZoomInFactor;
};
