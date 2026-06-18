#include "App.h"

#include "BuildingPlacer.h"
#include "Logger.h"
#include "MemoryReport.h"
#include "Profile.h"
#include "TerrainPlacement.h"
#include "TownBuilder.h"
#include "Units.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_set>

App::App(const Config& config, const TownConfig& townConfig, const TerrainCatalog& terrainCatalog,
         const DefCache& defs, TerrainAtlas terrainAtlas, PlacementFloors placementFloors,
         GrowthConfig growthAuto)
    : config_(config),
      townConfig_(townConfig),
      terrainCatalog_(terrainCatalog),
      defs_(defs),
      placementFloors_(placementFloors),
      terrainAtlas_(std::move(terrainAtlas)),
      growthQueue_(townConfig, config.town.seed),
      window_(sf::VideoMode(config.window.width, config.window.height), config.window.title,
              sf::Style::Titlebar | sf::Style::Close),
      camera_(config.renderWidth(), config.renderHeight(), config.world.pixelsPerUnit, window_),
      hud_(window_, growthQueue_),
      growthAuto_(growthAuto) {
    if (growthAuto_.autoExit) {
        window_.setVerticalSyncEnabled(false);
    } else {
        window_.setVerticalSyncEnabled(true);
    }
    labelFontLoaded_ =
        labelFont_.loadFromFile("C:/Windows/Fonts/arial.ttf")
        || labelFont_.loadFromFile("C:/Windows/Fonts/segoeui.ttf");

    if (terrainAtlas_.valid) {
        terrainOverlayMode_ = TerrainOverlayMode::TerrainAndDebug;
    } else {
        terrainOverlayMode_ = TerrainOverlayMode::Off;
    }

    hud_.setTerrainControls(&terrainOverlayMode_, terrainAtlas_.valid);
    hud_.setZoneTintControl(&hopZoneTintEnabled_);
    hud_.setBiomePlotControl(&showBiomePlots_);
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
    town_ = TownBuilder::build(config_, terrainAtlas_.valid ? &terrainAtlas_ : nullptr,
                               placementFloors_, townConfig_, &terrainCatalog_,
                               &defs_.terrainProbes());

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

}

void App::drawIdLabels(const std::vector<FrontageSegmentLabel>& labels) {
    drawIdLabels(labels, nullptr);
}

void App::drawIdLabels(const std::vector<FrontageSegmentLabel>& labels,
                       const std::unordered_set<int>* onlyIds) {
    if (!labelFontLoaded_) {
        return;
    }

    for (const FrontageSegmentLabel& label : labels) {
        if (onlyIds != nullptr && onlyIds->find(label.id) == onlyIds->end()) {
            continue;
        }
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

void App::drawRoadLabels() {
    drawIdLabels(town_.roadLabels);
}

void App::drawTerrainPlotTypeLabels() {
    if (!labelFontLoaded_) {
        return;
    }

    for (const RotatedTextLabel& label : town_.terrainPlotTypeLabels) {
        sf::Text plotText;
        plotText.setFont(labelFont_);
        plotText.setString(label.text);
        plotText.setCharacterSize(10);
        plotText.setFillColor(sf::Color::Black);
        plotText.setOutlineColor(sf::Color(255, 255, 255, 220));
        plotText.setOutlineThickness(1.5f);
        const sf::FloatRect bounds = plotText.getLocalBounds();
        plotText.setOrigin(bounds.left + bounds.width * 0.5f,
                           bounds.top + bounds.height * 0.5f);
        plotText.setPosition(label.centerXPx, label.centerYPx);
        plotText.setRotation(label.rotationDeg);
        window_.draw(plotText);
    }
}

std::unordered_set<int> App::terrainBuildingLabelIds() const {
    std::unordered_set<int> ids;
    for (const BuildingInstance& instance : town_.buildingInstances) {
        if (!buildingHasTerrainPlacement(defs_, instance.typeId)) {
            continue;
        }
        ids.insert(static_cast<int>(instance.id));
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (!footprint.mainBuilding && footprint.labelId >= 0) {
                ids.insert(footprint.labelId);
            }
        }
    }
    return ids;
}

int App::effectiveAutoGrowTarget() const {
    if (growthAuto_.autoGrow > 0) {
        return std::min(growthAuto_.autoGrow, growthQueue_.maxBuildings());
    }
    if (inAppAutoGrow_) {
        return growthQueue_.maxBuildings();
    }
    return 0;
}

void App::tickAutoGrow() {
    const int target = effectiveAutoGrowTarget();
    if (target <= 0 || autoGrowFinished_) {
        return;
    }

    const int active = growthQueue_.activeCount();
    if (active >= target) {
        if (growthAuto_.autoExit && town_.placementQueueCursor >= target) {
            finishAutoGrowAndExit();
        }
        return;
    }

    if (growthAuto_.autoGrowMs > 0) {
        if (autoGrowClock_.getElapsedTime().asMilliseconds()
            < static_cast<sf::Int64>(growthAuto_.autoGrowMs)) {
            return;
        }
        autoGrowClock_.restart();
    }

    const int nextCount = active + 1;
    growthQueue_.setActiveCount(std::min(nextCount, target));
}

int App::finishAutoGrowAndExit() {
    if (autoGrowFinished_) {
        return town_.placementFailureCount;
    }
    autoGrowFinished_ = true;

    const int target   = effectiveAutoGrowTarget();
    const int placed   = static_cast<int>(town_.buildingInstances.size());
    const int failures = town_.placementFailureCount;
    std::string summary = "ring_summary: target=" + std::to_string(target) + " placed="
                          + std::to_string(placed) + " failures=" + std::to_string(failures)
                          + " final_suburban_max=" + std::to_string(town_.suburbanMaxHop)
                          + " final_urban_core=" + std::to_string(town_.urbanCoreMaxHop);
    if (!town_.placementSkipReasonsSummary.empty()) {
        summary += " skip_reasons=" + town_.placementSkipReasonsSummary;
    }

    std::cout << summary << std::endl;
    Logger::log("layout", summary);
    Logger::log("layout", "auto_grow_done: exit_code=" + std::to_string(failures));
    Logger::log("app", "auto_grow exit target=" + std::to_string(target) + " placed="
                           + std::to_string(placed) + " failures=" + std::to_string(failures));
    if (Profile::enabled()) {
        Profile::report();
    }
    Logger::flush();

    writeMemoryReportOnce();
    window_.close();
    return failures;
}

void App::writeMemoryReportOnce() {
    if (memoryReportWritten_) {
        return;
    }
    memoryReportWritten_ = true;
    MemoryReport::writeMarkdown(Logger::directory(), town_, terrainAtlas_, defs_, growthQueue_,
                                townConfig_, config_);
}

void App::syncBuildingPlacements() {
    BuildingPlacer::sync(town_, growthQueue_, defs_, config_.plots, townConfig_, config_,
                         placementFloors_, config_.world.pixelsPerUnit, config_.town.seed,
                         terrainAtlas_.valid ? &terrainAtlas_ : nullptr);
    hud_.setPlacementFailures(town_.placementFailureCount, town_.moveFailureCount,
                              static_cast<int>(town_.buildingInstances.size()), town_);
}

int App::run() {
    Logger::log("app", "window opened " + std::to_string(config_.window.width) + "x"
                           + std::to_string(config_.window.height));

    if (Profile::enabled() && growthAuto_.autoGrow > 0 && growthAuto_.autoExit) {
        Profile::reset();
    }

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
                writeMemoryReportOnce();
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
                    switch (terrainOverlayMode_) {
                    case TerrainOverlayMode::TerrainAndDebug:
                        terrainOverlayMode_ = TerrainOverlayMode::DebugOnly;
                        break;
                    case TerrainOverlayMode::DebugOnly:
                        terrainOverlayMode_ = TerrainOverlayMode::Off;
                        break;
                    default:
                        terrainOverlayMode_ = TerrainOverlayMode::TerrainAndDebug;
                        break;
                    }
                }
            }

            if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::P) {
                showBiomePlots_ = !showBiomePlots_;
            }

            if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Z) {
                hopZoneTintEnabled_ = !hopZoneTintEnabled_;
            }

            if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::G) {
                inAppAutoGrow_ = !inAppAutoGrow_;
                autoGrowClock_.restart();
                Logger::log("app",
                            std::string("auto_grow toggled ") + (inAppAutoGrow_ ? "on" : "off")
                                + " target="
                                + std::to_string(inAppAutoGrow_ ? growthQueue_.maxBuildings() : 0));
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

        tickAutoGrow();
        if (growthQueue_.activeCount() != lastActiveCount_) {
            lastActiveCount_ = growthQueue_.activeCount();
        }

        if (town_.placementQueueCursor < growthQueue_.activeCount()) {
            syncBuildingPlacements();
        }

        const int autoTarget = effectiveAutoGrowTarget();
        if (growthAuto_.autoExit && autoTarget > 0 && growthQueue_.activeCount() >= autoTarget
            && town_.placementQueueCursor >= autoTarget) {
            const int exitCode = finishAutoGrowAndExit();
            return exitCode;
        }

        window_.clear(background);
        const bool skipDrawForAutoExit =
            growthAuto_.autoExit && growthAuto_.autoGrow > 0
            && town_.placementQueueCursor < effectiveAutoGrowTarget();
        if (!skipDrawForAutoExit) {
        window_.setView(camera_.getView());
        if (terrainAtlas_.valid
            && terrainOverlayMode_ == TerrainOverlayMode::TerrainAndDebug) {
            window_.draw(terrainSprite_);
        } else {
            window_.draw(diagramSprite_);
        }
        if (hopZoneTintEnabled_) {
            window_.draw(town_.hopDebugRoadMesh);
            window_.draw(town_.hopDebugJunctionMesh);
        } else {
            window_.draw(town_.roadMesh);
            window_.draw(town_.junctionMesh);
        }
        drawRoadLabels();
        if (!showBiomePlots_) {
            window_.draw(town_.frontageSegmentMesh);
            window_.draw(town_.frontageInwardArrowMesh);
            drawFrontageSegmentLabels();
            window_.draw(town_.alleyProbeFailMesh);
            window_.draw(town_.buildingOutlineMesh);
            drawPlotLabels();
            drawBuildingLabels();
        } else {
            window_.draw(town_.terrainBuildingOutlineMesh);
            const std::unordered_set<int> terrainIds = terrainBuildingLabelIds();
            drawIdLabels(town_.plotLabels, &terrainIds);
            drawIdLabels(town_.buildingLabels, &terrainIds);
            drawTerrainPlotTypeLabels();
        }
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
        }
        window_.setView(window_.getDefaultView());
        hud_.draw(window_);
        window_.display();
    }

    Logger::log("app", "shutdown");
    return 0;
}
