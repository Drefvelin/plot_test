#include "render/Camera.h"

Camera::Camera(float worldWidth, float worldHeight, float pixelsPerUnit,
               const sf::RenderWindow& window) {
    const auto winSize = window.getSize();
    view_.setSize(static_cast<float>(winSize.x) * pixelsPerUnit,
                  static_cast<float>(winSize.y) * pixelsPerUnit);
    view_.setCenter(worldWidth * 0.5f, worldHeight * 0.5f);
}

void Camera::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    if (event.type == sf::Event::MouseButtonPressed) {
        if (event.mouseButton.button == sf::Mouse::Right) {
            isPanning_    = true;
            lastMousePos_ = {event.mouseButton.x, event.mouseButton.y};
        }
    }

    if (event.type == sf::Event::MouseButtonReleased) {
        if (event.mouseButton.button == sf::Mouse::Right) {
            isPanning_ = false;
        }
    }

    if (event.type == sf::Event::MouseMoved && isPanning_) {
        const sf::Vector2i current{event.mouseMove.x, event.mouseMove.y};
        const sf::Vector2i delta = current - lastMousePos_;
        const auto winSize = window.getSize();
        const float wx = -static_cast<float>(delta.x) * view_.getSize().x
                         / static_cast<float>(winSize.x);
        const float wy = -static_cast<float>(delta.y) * view_.getSize().y
                         / static_cast<float>(winSize.y);
        view_.move(wx, wy);
        lastMousePos_ = current;
    }

    if (event.type == sf::Event::MouseWheelScrolled) {
        const float factor = (event.mouseWheelScroll.delta > 0.f) ? kZoomInFactor : kZoomOutFactor;
        const float newZoom = zoomLevel_ * factor;
        if (newZoom < minZoom_ || newZoom > maxZoom_) {
            return;
        }

        const sf::Vector2i pixel{event.mouseWheelScroll.x, event.mouseWheelScroll.y};
        const sf::Vector2f worldBefore = window.mapPixelToCoords(pixel, view_);

        zoomLevel_ = newZoom;
        view_.zoom(factor);

        const sf::Vector2f worldAfter = window.mapPixelToCoords(pixel, view_);
        view_.move(worldBefore - worldAfter);
    }
}
