#pragma once

#include "Config.h"
#include "Terrain.h"
#include "Town.h"

#include <SFML/Graphics.hpp>

#include <filesystem>
#include <vector>

enum class TerrainOverlayMode {
    TerrainAndDebug,
    DebugOnly,
    HopDebug,
    Off,
};

struct TerrainAtlas {
    bool valid = false;

    int   sourceW   = 0;
    int   sourceH   = 0;
    float diagramW  = 0.f;
    float diagramH  = 0.f;
    std::vector<TerrainKind> raster;
    std::vector<uint8_t>     forbiddenDilated;

    float waterInset = 0.f;
    float shoreInset = 0.f;

    std::vector<std::vector<Vec2>> forbiddenPolygons;
    std::vector<std::vector<Vec2>> riverOutlines;
    std::vector<std::vector<Vec2>> seaOutlines;
    std::vector<std::vector<Vec2>> forestPolygons;
    std::vector<std::vector<Vec2>> hillsPolygons;
    std::vector<std::vector<Vec2>> shoreRoadGraph;
    std::vector<std::vector<Vec2>> riverRoadGraph;

    sf::Texture overlayTexture;

    sf::VertexArray debugForbiddenMesh{sf::Lines};
    sf::VertexArray debugRiverMesh{sf::Lines};
    sf::VertexArray debugShoreMesh{sf::Lines};
    sf::VertexArray debugForestMesh{sf::Lines};
    sf::VertexArray debugHillsMesh{sf::Lines};

    bool isForbidden(Vec2 worldPos) const;
    bool isBuildable(Vec2 worldPos) const;
    TerrainKind sample(Vec2 worldPos) const;
};

TerrainAtlas bakeTerrain(const Config& config, const std::filesystem::path& projectRoot);

void buildTerrainDebugMeshes(TerrainAtlas& atlas, float pixelsPerUnit, float contourWidthWorld);

std::vector<std::pair<float, float>> clipRoadSegmentToLand(const Vec2& a, const Vec2& b,
                                                            const TerrainAtlas& terrain);

const char* terrainOverlayModeLabel(TerrainOverlayMode mode);
