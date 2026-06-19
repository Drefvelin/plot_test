#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "Units.h"

struct WindowConfig {
    int         width  = 1280;
    int         height = 720;
    std::string title  = "Voronoi Plot";
};

struct DiagramConfig {
    float width  = 1024.f;
    float height = 1024.f;
    float radius = 512.f;
};

struct WorldConfig {
    float pixelsPerUnit = units::kDefaultPixelsPerUnit;
};

struct PlotConfig {
    float minArea              = 50.f;
    float maxArea              = 200.f;
    float maxDepthToFrontRatio = 1.5f;  // neither side may exceed ratio * the other
    float frontageSetback      = 2.f;   // front row inset from road, into cell
    int   terrainAnchorMaxRoads = 4;    // BFS road cap for terrain_anchor fallback
};

struct VoronoiConfig {
    float scale = 1.0f;
    int   seed  = 42;

    int siteCount() const;
};

struct TownSettings {
    int seed = 12345;
};

struct ColorConfig {
    std::array<uint8_t, 3> inside          = {255, 255, 255};
    std::array<uint8_t, 3> outside         = {140, 140, 140};
    std::array<uint8_t, 3> edges           = {0, 0, 0};
    std::array<uint8_t, 3> secondaryEdges  = {0, 160, 0};
    std::array<uint8_t, 3> bridge          = {139, 90, 43};
};

struct LogFileConfig {
    std::string channel;
    std::string filename;
};

struct LoggingConfig {
    std::string directory       = "logs";
    int         flushIntervalMs = 2000;  // 0 = flush only on shutdown / explicit flush()
    std::vector<LogFileConfig> files;
};

struct DebugConfig {
    std::array<uint8_t, 3> highlightColor = {128, 128, 128};
};

struct TerrainConfig {
    std::string imagePath           = "assets/terrain.png";
    std::string colorsPath          = "app/config/terrain.txt";
    std::string terrainsPath        = "app/config/terrains.yml";
    int         gridSize            = 128;
    float       simplifyTolerance   = 2.f;
    float       waterInset          = 2.f;
    float       shoreInset          = 2.f;
    float       riverInset          = 2.f;
    float       contourWidth        = 1.f;
    bool        clipRoadsAtWater    = true;
    bool        corridorRoadsEnabled = false;
    float       shoreRoadInset      = 0.f;
    float       riverRoadInset      = 0.f;
    float       corridorEdgeSpacing = 0.f;
    float       corridorParallelProbeOffset = 0.f;
    float       corridorParallelCos       = 0.f;
    bool        bridgesEnabled            = false;
    bool        bridgeSnapEnabled         = false;
    float       bridgeSnapSearchRadius    = 0.f;
    float       bridgeMaxSpan             = 80.f;
    float       bridgeOutlineMaxDist      = 6.f;
    float       shoreJunctionMergeDist    = 2.f;
};

struct GrowthConfig {
    int  autoGrow   = 0;
    int  autoGrowMs = 50;
    bool autoExit   = false;
    bool profile    = false;
    bool verbosePlacementLogs = false;
};

struct Config {
    WindowConfig  window;
    DiagramConfig diagram;
    WorldConfig   world;
    PlotConfig    plots;
    VoronoiConfig voronoi;
    TownSettings  town;
    ColorConfig   colors;
    LoggingConfig logging;
    DebugConfig   debug;
    TerrainConfig terrain;
    GrowthConfig  growth;

    std::filesystem::path sourcePath;

    float renderWidth() const { return diagram.width * world.pixelsPerUnit; }
    float renderHeight() const { return diagram.height * world.pixelsPerUnit; }
    float renderRadius() const { return diagram.radius * world.pixelsPerUnit; }

    static Config load(const std::filesystem::path& path);
    static std::filesystem::path resolveConfigPath();
    static std::filesystem::path resolveProjectRoot(const std::filesystem::path& exeDir);
};
