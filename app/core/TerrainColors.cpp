#include "TerrainColors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

std::vector<TerrainColorEntry> loadTerrainColorMap(const std::filesystem::path& path,
                                                   const TerrainCatalog& catalog) {
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
        entry.rgb = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
        entry.id  = catalog.resolveKey(key);
        if (entry.id != kTerrainUnknown) {
            entries.push_back(entry);
        }
    }

    return entries;
}

namespace {

constexpr int kMaxChannelDelta = 32;

int rgbDistanceSq(uint8_t r, uint8_t g, uint8_t b, const std::array<uint8_t, 3>& rgb) {
    const int dr = static_cast<int>(r) - static_cast<int>(rgb[0]);
    const int dg = static_cast<int>(g) - static_cast<int>(rgb[1]);
    const int db = static_cast<int>(b) - static_cast<int>(rgb[2]);
    return dr * dr + dg * dg + db * db;
}

int maxChannelDelta(uint8_t r, uint8_t g, uint8_t b, const std::array<uint8_t, 3>& rgb) {
    return std::max({std::abs(static_cast<int>(r) - static_cast<int>(rgb[0])),
                     std::abs(static_cast<int>(g) - static_cast<int>(rgb[1])),
                     std::abs(static_cast<int>(b) - static_cast<int>(rgb[2]))});
}

bool kindTieBreak(TerrainId candidate, TerrainId incumbent) {
    if (candidate == incumbent) {
        return false;
    }
    return false;
}

}  // namespace

TerrainId classifyTerrainColor(uint8_t r, uint8_t g, uint8_t b,
                               const std::vector<TerrainColorEntry>& colorMap) {
    for (const TerrainColorEntry& entry : colorMap) {
        if (entry.rgb[0] == r && entry.rgb[1] == g && entry.rgb[2] == b) {
            return entry.id;
        }
    }

    TerrainId    bestKind     = kTerrainUnknown;
    int         bestDistSq   = 0;
    int         bestMaxDelta = 0;
    bool        haveBest     = false;

    for (const TerrainColorEntry& entry : colorMap) {
        const int distSq   = rgbDistanceSq(r, g, b, entry.rgb);
        const int maxDelta = maxChannelDelta(r, g, b, entry.rgb);
        if (maxDelta > kMaxChannelDelta) {
            continue;
        }
        if (!haveBest || distSq < bestDistSq
            || (distSq == bestDistSq && kindTieBreak(entry.id, bestKind))) {
            bestKind     = entry.id;
            bestDistSq   = distSq;
            bestMaxDelta = maxDelta;
            haveBest     = true;
        }
    }

    return haveBest ? bestKind : kTerrainUnknown;
}
