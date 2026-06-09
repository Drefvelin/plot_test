#pragma once

#include <array>
#include <cstdint>
#include <string>

enum class TerrainKind {
    Unknown,
    Sea,
    River,
    Plains,
    Forest,
    Hills,
    Mountain,
};

inline bool terrainKindIsForbidden(TerrainKind kind) {
    return kind == TerrainKind::Sea || kind == TerrainKind::River;
}

inline const char* terrainKindName(TerrainKind kind) {
    switch (kind) {
    case TerrainKind::Sea:
        return "sea";
    case TerrainKind::River:
        return "river";
    case TerrainKind::Plains:
        return "plains";
    case TerrainKind::Forest:
        return "forest";
    case TerrainKind::Hills:
        return "hills";
    case TerrainKind::Mountain:
        return "mountain";
    default:
        return "unknown";
    }
}

TerrainKind terrainKindFromConfigKey(const std::string& key);
