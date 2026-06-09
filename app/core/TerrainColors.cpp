#include "TerrainColors.h"

#include <fstream>
#include <sstream>

TerrainKind terrainKindFromConfigKey(const std::string& key) {
    if (key == "sea") {
        return TerrainKind::Sea;
    }
    if (key == "river") {
        return TerrainKind::River;
    }
    if (key == "plains") {
        return TerrainKind::Plains;
    }
    if (key == "forest") {
        return TerrainKind::Forest;
    }
    if (key == "hills") {
        return TerrainKind::Hills;
    }
    if (key == "mountain") {
        return TerrainKind::Mountain;
    }
    return TerrainKind::Unknown;
}

std::vector<TerrainColorEntry> loadTerrainColorMap(const std::filesystem::path& path) {
    std::vector<TerrainColorEntry> entries;
    std::ifstream                  file(path);
    if (!file) {
        return entries;
    }

    std::string line;
    while (std::getline(file, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, eq);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
            key.pop_back();
        }
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) {
            key.erase(key.begin());
        }

        std::string value = line.substr(eq + 1);
        int         r = 0;
        int         g = 0;
        int         b = 0;
        char        comma = 0;
        std::istringstream iss(value);
        if (!(iss >> r >> comma >> g >> comma >> b)) {
            continue;
        }

        TerrainColorEntry entry;
        entry.rgb  = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
        entry.kind = terrainKindFromConfigKey(key);
        if (entry.kind != TerrainKind::Unknown) {
            entries.push_back(entry);
        }
    }

    return entries;
}

TerrainKind classifyTerrainColor(uint8_t r, uint8_t g, uint8_t b,
                                 const std::vector<TerrainColorEntry>& colorMap) {
    for (const TerrainColorEntry& entry : colorMap) {
        if (entry.rgb[0] == r && entry.rgb[1] == g && entry.rgb[2] == b) {
            return entry.kind;
        }
    }
    return TerrainKind::Unknown;
}
