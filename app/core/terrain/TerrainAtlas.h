#pragma once

#include "config/Config.h"
#include "terrain/Terrain.h"
#include "town/Town.h"

#include <SFML/Graphics.hpp>

#include <filesystem>
#include <unordered_map>
#include <vector>

enum class TerrainOverlayMode {
    TerrainAndDebug,
    DebugOnly,
    HopDebug,
    Off,
};

struct TerrainAtlas {
    bool valid = false;
    const TerrainCatalog* catalog = nullptr;

    int   sourceW   = 0;
    int   sourceH   = 0;
    float diagramW  = 0.f;
    float diagramH  = 0.f;
    std::vector<TerrainId>   raster;
    std::vector<uint8_t>     forbiddenDilated;

    float waterInset = 0.f;
    float shoreInset = 0.f;

    std::vector<std::vector<Vec2>> forbiddenPolygons;
    std::unordered_map<TerrainId, std::vector<std::vector<Vec2>>> outlinesByTerrainId;
    TerrainId                      majorityLandId = kTerrainUnknown;
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
    TerrainId sample(Vec2 worldPos) const;
    const std::vector<std::vector<Vec2>>* outlineGraphs(TerrainId id) const;
    bool hasOutline(TerrainId id) const;
    bool hasRegionOutline(TerrainId id) const;
    float distToRegionEdge(Vec2 worldPos, TerrainId id) const;
};

TerrainAtlas bakeTerrain(const Config& config, const TerrainCatalog& catalog,
                         const std::filesystem::path& projectRoot);

void buildTerrainDebugMeshes(TerrainAtlas& atlas, float pixelsPerUnit, float contourWidthWorld);

std::vector<std::pair<float, float>> clipRoadSegmentToLand(const Vec2& a, const Vec2& b,
                                                            const TerrainAtlas& terrain);

const char* terrainOverlayModeLabel(TerrainOverlayMode mode);
