#include "App.h"

#include "BuildingPlacer.h"
#include "Logger.h"
#include "TownBuilder.h"
#include "Units.h"

#include <algorithm>
#include <cmath>
#include <string>

App::App(const Config& config, const TownConfig& townConfig, const DefCache& defs,
         TerrainAtlas terrainAtlas)
    : config_(config),
      townConfig_(townConfig),
      defs_(defs),
      terrainAtlas_(std::move(terrainAtlas)),
      growthQueue_(townConfig, config.town.seed),
      window_(sf::VideoMode(config.window.width, config.window.height), config.window.title,
              sf::Style::Titlebar | sf::Style::Close),
      camera_(config.renderWidth(), config.renderHeight(), config.world.pixelsPerUnit, window_),
      hud_(window_, growthQueue_) {
    window_.setVerticalSyncEnabled(true);
    labelFontLoaded_ =
        labelFont_.loadFromFile("C:/Windows/Fonts/arial.ttf")
        || labelFont_.loadFromFile("C:/Windows/Fonts/segoeui.ttf");

    if (terrainAtlas_.valid) {
        terrainOverlayMode_ = TerrainOverlayMode::TerrainAndDebug;
    } else {
        terrainOverlayMode_ = TerrainOverlayMode::Off;
    }

    hud_.setTerrainControls(&terrainOverlayMode_, terrainAtlas_.valid);
}

sf::Color App::toColor(const std::array<uint8_t, 3>& rgb) const {
    return sf::Color(rgb[0], rgb[1], rgb[2]);
}

void App::drawCenterCross(sf::RenderTexture& target, float cx, float cy) const {
    constexpr float kCrossUnits = 3.2f;
    const float ppu = config_.world.pixelsPerUnit;
    const float crossSize = units::toPixels(kCrossUnits, ppu);
    const float thickness = units::toPixels(0.3f, ppu);
    const sf::Color purple(128, 0, 200);

    sf::RectangleShape horizontal({crossSize, thickness});
    horizontal.setOrigin(crossSize * 0.5f, thickness * 0.5f);
    horizontal.setPosition(cx, cy);
    horizontal.setFillColor(purple);

    sf::RectangleShape vertical({thickness, crossSize});
    vertical.setOrigin(thickness * 0.5f, crossSize * 0.5f);
    vertical.setPosition(cx, cy);
    vertical.setFillColor(purple);

    target.draw(horizontal);
    target.draw(vertical);
}

void App::buildDiagram() {
    town_ = TownBuilder::build(config_, terrainAtlas_.valid ? &terrainAtlas_ : nullptr);

    const unsigned texW = static_cast<unsigned>(config_.renderWidth());
    const unsigned texH = static_cast<unsigned>(config_.renderHeight());
    diagramTexture_.create(texW, texH);
    diagramTexture_.clear(toColor(config_.colors.outside));

    const float renderRadius = config_.renderRadius();
    sf::CircleShape circle(renderRadius);
    circle.setOrigin(renderRadius, renderRadius);
    circle.setPosition(config_.renderWidth() * 0.5f, config_.renderHeight() * 0.5f);
    circle.setFillColor(toColor(config_.colors.inside));
    circle.setOutlineThickness(0.f);

    sf::RenderStates states;
    diagramTexture_.draw(circle, states);
    drawCenterCross(diagramTexture_, config_.renderWidth() * 0.5f, config_.renderHeight() * 0.5f);
    diagramTexture_.display();

    diagramSprite_.setTexture(diagramTexture_.getTexture(), true);

    if (terrainAtlas_.valid) {
        terrainSprite_.setTexture(terrainAtlas_.overlayTexture, true);
        const sf::Vector2u texSize = terrainAtlas_.overlayTexture.getSize();
        if (texSize.x > 0 && texSize.y > 0) {
            terrainSprite_.setScale(config_.renderWidth() / static_cast<float>(texSize.x),
                                    config_.renderHeight() / static_cast<float>(texSize.y));
        }
    }

    Logger::log("render", "background texture built " + std::to_string(texW) + "x"
                               + std::to_string(texH) + " roads=" + std::to_string(town_.roads.size())
                               + " pixels_per_unit=" + std::to_string(config_.world.pixelsPerUnit)
                               + " terrain=" + (terrainAtlas_.valid ? "yes" : "no"));

    buildCellHighlight();
}

void App::buildCellHighlight() {
    cellHighlightActive_ = false;

    const int cellId = config_.debug.highlightCellId;
    if (cellId == -1) {
        return;
    }
    if (cellId >= 0 && cellId >= static_cast<int>(town_.cells.size())) {
        return;
    }

    const unsigned texW = static_cast<unsigned>(config_.renderWidth());
    const unsigned texH = static_cast<unsigned>(config_.renderHeight());
    const float    ppu  = config_.world.pixelsPerUnit;
    const sf::Color fill  = toColor(config_.debug.highlightColor);

    sf::Image image;
    image.create(texW, texH, sf::Color::Transparent);

    auto paintCell = [&](const Cell& cell) {
        if (cell.boundary.size() < 3) {
            return false;
        }

        float minPx = static_cast<float>(texW);
        float minPy = static_cast<float>(texH);
        float maxPx = 0.f;
        float maxPy = 0.f;
        for (const Vec2& v : cell.boundary) {
            const float px = units::toPixels(v.x, ppu);
            const float py = units::toPixels(v.y, ppu);
            minPx = std::min(minPx, px);
            minPy = std::min(minPy, py);
            maxPx = std::max(maxPx, px);
            maxPy = std::max(maxPy, py);
        }

        const unsigned x0 = static_cast<unsigned>(std::max(0.f, std::floor(minPx)));
        const unsigned y0 = static_cast<unsigned>(std::max(0.f, std::floor(minPy)));
        const unsigned x1 =
            static_cast<unsigned>(std::min(static_cast<float>(texW), std::ceil(maxPx)));
        const unsigned y1 =
            static_cast<unsigned>(std::min(static_cast<float>(texH), std::ceil(maxPy)));

        for (unsigned y = y0; y < y1; ++y) {
            for (unsigned x = x0; x < x1; ++x) {
                const Vec2 world{units::toUnits(static_cast<float>(x) + 0.5f, ppu),
                                 units::toUnits(static_cast<float>(y) + 0.5f, ppu)};
                if (pointInCellBoundary(world, cell, town_.roads)) {
                    image.setPixel(x, y, fill);
                }
            }
        }
        return true;
    };

    if (cellId == -2) {
        int paintedCells = 0;
        for (const Cell& cell : town_.cells) {
            if (paintCell(cell)) {
                ++paintedCells;
            }
        }

        if (paintedCells == 0) {
            Logger::log("render", "cell highlight skipped: no cells with boundaries");
            return;
        }

        cellHighlightTexture_.loadFromImage(image);
        cellHighlightSprite_.setTexture(cellHighlightTexture_, true);
        cellHighlightActive_ = true;

        Logger::log("render", "cell highlight built all cells=" + std::to_string(paintedCells)
                                   + " size=" + std::to_string(texW) + "x"
                                   + std::to_string(texH));
        return;
    }

    if (!paintCell(town_.cells[static_cast<std::size_t>(cellId)])) {
        Logger::log("render", "cell highlight skipped: cell=" + std::to_string(cellId)
                                   + " has no boundary");
        return;
    }

    cellHighlightTexture_.loadFromImage(image);
    cellHighlightSprite_.setTexture(cellHighlightTexture_, true);
    cellHighlightActive_ = true;

    Logger::log("render", "cell highlight built cell=" + std::to_string(cellId) + " size="
                               + std::to_string(texW) + "x" + std::to_string(texH));
}

void App::drawIdLabels(const std::vector<FrontageSegmentLabel>& labels) {
    if (!labelFontLoaded_) {
        return;
    }

    for (const FrontageSegmentLabel& label : labels) {
        const std::string text = std::to_string(label.id);

        sf::Text idText;
        idText.setFont(labelFont_);
        idText.setString(text);
        idText.setCharacterSize(12);
        idText.setFillColor(sf::Color::Black);
        const sf::FloatRect textBounds = idText.getLocalBounds();
        const float       boxW         = std::max(22.f, textBounds.width + 10.f);
        const float       boxH         = 18.f;

        sf::RectangleShape box({boxW, boxH});
        box.setOrigin(boxW * 0.5f, boxH * 0.5f);
        box.setPosition(label.centerXPx, label.centerYPx);
        box.setFillColor(sf::Color::White);
        box.setOutlineColor(sf::Color::Black);
        box.setOutlineThickness(1.5f);

        idText.setOrigin(textBounds.left + textBounds.width * 0.5f,
                         textBounds.top + textBounds.height * 0.5f + 1.f);
        idText.setPosition(label.centerXPx, label.centerYPx);

        window_.draw(box);
        window_.draw(idText);
    }
}

void App::drawFrontageSegmentLabels() {
    drawIdLabels(town_.frontageSegmentLabels);
}

void App::drawPlotLabels() {
    drawIdLabels(town_.plotLabels);
}

void App::drawBuildingLabels() {
    drawIdLabels(town_.buildingLabels);
}

void App::syncBuildingPlacements() {
    BuildingPlacer::sync(town_, growthQueue_, defs_, config_.plots, townConfig_, config_,
                         config_.world.pixelsPerUnit, config_.town.seed,
                         terrainAtlas_.valid ? &terrainAtlas_ : nullptr);
}

int App::run() {
    Logger::log("app", "window opened " + std::to_string(config_.window.width) + "x"
                           + std::to_string(config_.window.height));

    buildDiagram();
    syncBuildingPlacements();
    lastActiveCount_ = growthQueue_.activeCount();
    Logger::log("app", "town growth queue max=" + std::to_string(growthQueue_.maxBuildings())
                           + " seed=" + std::to_string(config_.town.seed));

    const sf::Color background = toColor(config_.colors.outside);
    bool panLogged = false;

    while (window_.isOpen()) {
        sf::Event event{};
        while (window_.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                window_.close();
            }

            if (event.type == sf::Event::Resized) {
                hud_.layout(window_);
            }

            if (hud_.handleEvent(event, window_)) {
                continue;
            }

            if (event.type == sf::Event::KeyPressed && terrainAtlas_.valid) {
                if (event.key.code == sf::Keyboard::T) {
                    const int mode = static_cast<int>(terrainOverlayMode_);
                    terrainOverlayMode_ = static_cast<TerrainOverlayMode>((mode + 1) % 3);
                }
            }

            if (event.type == sf::Event::MouseButtonPressed
                && event.mouseButton.button == sf::Mouse::Right && !panLogged) {
                Logger::log("render", "pan started");
                panLogged = true;
            }
            if (event.type == sf::Event::MouseButtonReleased
                && event.mouseButton.button == sf::Mouse::Right && panLogged) {
                Logger::log("render", "pan ended");
                panLogged = false;
            }

            camera_.handleEvent(event, window_);
        }

        if (growthQueue_.activeCount() != lastActiveCount_) {
            lastActiveCount_ = growthQueue_.activeCount();
            syncBuildingPlacements();
        }

        window_.clear(background);
        window_.setView(camera_.getView());
        if (terrainAtlas_.valid
            && terrainOverlayMode_ == TerrainOverlayMode::TerrainAndDebug) {
            window_.draw(terrainSprite_);
        } else {
            window_.draw(diagramSprite_);
        }
        if (cellHighlightActive_) {
            window_.draw(cellHighlightSprite_);
        }
        window_.draw(town_.roadMesh);
        window_.draw(town_.junctionMesh);
        window_.draw(town_.cellCentroidMesh);
        drawIdLabels(town_.cellCentroidLabels);
        window_.draw(town_.cellSiteMesh);
        window_.draw(town_.frontageSegmentMesh);
        window_.draw(town_.frontageInwardArrowMesh);
        drawFrontageSegmentLabels();
        window_.draw(town_.alleyProbeFailMesh);
        window_.draw(town_.buildingOutlineMesh);
        drawPlotLabels();
        drawBuildingLabels();
        const bool drawDebug =
            terrainAtlas_.valid
            && (terrainOverlayMode_ == TerrainOverlayMode::TerrainAndDebug
                || terrainOverlayMode_ == TerrainOverlayMode::DebugOnly);
        if (drawDebug) {
            window_.draw(terrainAtlas_.debugForbiddenMesh);
            window_.draw(terrainAtlas_.debugRiverMesh);
            window_.draw(terrainAtlas_.debugShoreMesh);
            window_.draw(terrainAtlas_.debugForestMesh);
            window_.draw(terrainAtlas_.debugHillsMesh);
        }
        window_.setView(window_.getDefaultView());
        hud_.draw(window_);
        window_.display();
    }

    Logger::log("app", "shutdown");
    return 0;
}
