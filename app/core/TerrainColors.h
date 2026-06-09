#pragma once

#include "Terrain.h"

#include <array>
#include <filesystem>
#include <vector>

struct TerrainColorEntry {
    std::array<uint8_t, 3> rgb{};
    TerrainKind            kind = TerrainKind::Unknown;
};

std::vector<TerrainColorEntry> loadTerrainColorMap(const std::filesystem::path& path);

TerrainKind classifyTerrainColor(uint8_t r, uint8_t g, uint8_t b,
                                 const std::vector<TerrainColorEntry>& colorMap);
