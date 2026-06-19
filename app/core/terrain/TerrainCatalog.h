#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

using TerrainId = std::uint16_t;

constexpr TerrainId kTerrainUnknown = 0;

enum class TerrainOutlineClass {
    None,
    Water,
    Land,
};

struct TerrainTypeDef {
    TerrainId           id         = kTerrainUnknown;
    std::string         key;
    std::array<uint8_t, 3> rgb{};
    bool                forbidden  = false;
    TerrainOutlineClass outlineClass = TerrainOutlineClass::None;
};

class TerrainCatalog {
public:
    static TerrainCatalog load(const std::filesystem::path& path);

    std::size_t count() const { return types_.size(); }

    TerrainId   idAt(std::size_t denseIndex) const;
    std::size_t denseIndex(TerrainId id) const;
    bool        contains(TerrainId id) const;

    const TerrainTypeDef* find(TerrainId id) const;
    const TerrainTypeDef* findByKey(const std::string& key) const;

    TerrainId resolveKey(const std::string& key) const;
    std::vector<TerrainId> expandPreferToken(const std::string& token) const;
    void appendPreferId(std::vector<TerrainId>& out, TerrainId id) const;

    const char* name(TerrainId id) const;
    bool        isForbidden(TerrainId id) const;
    TerrainOutlineClass outlineClass(TerrainId id) const;

    const std::vector<TerrainTypeDef>& types() const { return types_; }

private:
    std::vector<TerrainTypeDef>              types_;
    std::unordered_map<TerrainId, std::size_t> idToDense_;
    std::unordered_map<std::string, TerrainId> keyToId_;
    std::unordered_map<std::string, std::vector<TerrainId>> preferAliases_;
};

struct TerrainProbeConfig {
    std::vector<TerrainId> borderIds;
    std::vector<TerrainId> scanIds;
    std::vector<TerrainId> scanInsideIds;
    float                  minBorderFrontage = 8.f;
};

class DefCache;
TerrainProbeConfig buildTerrainProbeConfig(const TerrainCatalog& catalog, const DefCache& defs);
