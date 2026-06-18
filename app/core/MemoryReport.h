#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

class BuildingGrowthQueue;
class DefCache;
struct Config;
struct TerrainAtlas;
struct Town;
struct TownConfig;

struct MemoryStat {
    std::size_t bytes = 0;
    std::size_t count = 0;
};

struct MemoryLine {
    std::string path;
    MemoryStat  stat;
    std::string note;
};

struct MemoryReportResult {
    std::vector<MemoryLine> totals;
    std::vector<MemoryLine> townDetail;
    std::vector<MemoryLine> terrainDetail;
    std::vector<MemoryLine> defCacheDetail;
    std::vector<MemoryLine> excluded;
    std::size_t             grandTotal = 0;
};

class MemoryReport {
public:
    static MemoryReportResult build(const Town& town, const TerrainAtlas& terrain,
                                    const DefCache& defs, const BuildingGrowthQueue& queue,
                                    const TownConfig& townConfig, const Config& config);

    static bool writeMarkdown(const std::filesystem::path& logDirectory, const Town& town,
                              const TerrainAtlas& terrain, const DefCache& defs,
                              const BuildingGrowthQueue& queue, const TownConfig& townConfig,
                              const Config& config);
};
