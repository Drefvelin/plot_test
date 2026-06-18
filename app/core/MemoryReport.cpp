#include "MemoryReport.h"

#include "BuildingGrowthQueue.h"
#include "Config.h"
#include "DefCache.h"
#include "FrontierManager.h"
#include "PlacementFrontier.h"
#include "Logger.h"
#include "BuildingTypes.h"
#include "TerrainAtlas.h"
#include "TerrainCatalog.h"
#include "Town.h"
#include "TownConfig.h"
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/VertexArray.hpp>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

MemoryStat operator+(MemoryStat a, const MemoryStat& b) {
    a.bytes += b.bytes;
    a.count += b.count;
    return a;
}

MemoryStat& operator+=(MemoryStat& a, const MemoryStat& b) {
    a.bytes += b.bytes;
    a.count += b.count;
    return a;
}

template <typename T>
MemoryStat vectorStorage(const std::vector<T>& vec) {
    return {vec.capacity() * sizeof(T), vec.size()};
}

MemoryStat stringStorage(const std::string& s) {
    return {sizeof(std::string) + s.capacity(), 1};
}

MemoryStat stringsInVector(const std::vector<std::string>& vec) {
    MemoryStat total = vectorStorage(vec);
    for (const std::string& s : vec) {
        total += stringStorage(s);
    }
    return total;
}

MemoryStat nestedVec2Polygons(const std::vector<std::vector<Vec2>>& polys) {
    MemoryStat total = vectorStorage(polys);
    for (const std::vector<Vec2>& ring : polys) {
        total += vectorStorage(ring);
    }
    return total;
}

MemoryStat intSetStorage(const std::unordered_set<int>& set) {
    return {set.size() * (sizeof(int) + sizeof(void*)), set.size()};
}

MemoryStat depthCacheStorage(
    const std::unordered_map<DepthCacheKey, float, DepthCacheKeyHash>& map) {
    return {map.size() * (sizeof(DepthCacheKey) + sizeof(float) + sizeof(void*) * 2), map.size()};
}

MemoryStat wallGapSetStorage(
    const std::unordered_set<WallGapKey, WallGapKeyHash, WallGapKeyEqual>& set) {
    return {set.size() * (sizeof(WallGapKey) + sizeof(void*) * 2), set.size()};
}

MemoryStat sizeBandStorage(const SizeBand& band) {
    MemoryStat stat = stringStorage(band.name);
    stat.bytes += sizeof(float) * 2;
    return stat;
}

MemoryStat buildingTemplateStorage(const BuildingTemplate& tmpl) {
    MemoryStat stat = stringStorage(tmpl.name);
    stat += sizeBandStorage(tmpl.size);
    stat.bytes += sizeof(BuildingTemplateRules);
    return stat;
}

MemoryStat buildingDefStorage(const BuildingDef& def) {
    MemoryStat stat = stringStorage(def.name);
    stat += stringStorage(def.sizeCategory);
    stat += stringStorage(def.type);
    stat.bytes += sizeof(BuildingTypeId) + sizeof(std::array<uint8_t, 3>) + sizeof(bool) * 2;
    for (const auto& [key, range] : def.buildingsOnPlot) {
        stat += stringStorage(key);
        stat.bytes += sizeof(CountRange);
    }
    return stat;
}

MemoryStat vertexArrayStorage(const sf::VertexArray& mesh) {
    return {static_cast<std::size_t>(mesh.getVertexCount()) * sizeof(sf::Vertex),
            static_cast<std::size_t>(mesh.getVertexCount())};
}

MemoryStat textureStorage(const sf::Texture& texture) {
    const auto size = texture.getSize();
    const std::size_t pixels =
        static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y);
    return {pixels * 4 + sizeof(sf::Texture), pixels > 0 ? std::size_t{1} : std::size_t{0}};
}

MemoryStat roadSideFrontageStorage(const RoadSideFrontage& bank) {
    MemoryStat stat;
    stat += vectorStorage(bank.segments);
    stat += vectorStorage(bank.wallSegments);
    stat += vectorStorage(bank.mainOccupancyT);
    stat += depthCacheStorage(bank.depthCacheEntries);
    stat.bytes += sizeof(Vec2) + sizeof(std::uint8_t) + sizeof(std::uint32_t);
    return stat;
}

void roadsStorage(const std::vector<Road>& roads, MemoryStat& outShell, MemoryStat& outSegments,
                  MemoryStat& outWallSegments, MemoryStat& outOccupancy,
                  MemoryStat& outDepthCache) {
    outShell = {roads.capacity() * sizeof(Road), roads.size()};
    outSegments = {};
    outWallSegments = {};
    outOccupancy = {};
    outDepthCache = {};

    MemoryStat nestedTotal;
    for (const Road& road : roads) {
        nestedTotal += roadSideFrontageStorage(road.sideA);
        nestedTotal += roadSideFrontageStorage(road.sideB);

        outSegments += vectorStorage(road.sideA.segments);
        outSegments += vectorStorage(road.sideB.segments);
        outWallSegments += vectorStorage(road.sideA.wallSegments);
        outWallSegments += vectorStorage(road.sideB.wallSegments);
        outOccupancy += vectorStorage(road.sideA.mainOccupancyT);
        outOccupancy += vectorStorage(road.sideB.mainOccupancyT);
        outDepthCache += depthCacheStorage(road.sideA.depthCacheEntries);
        outDepthCache += depthCacheStorage(road.sideB.depthCacheEntries);
    }

    outSegments.count = 0;
    outWallSegments.count = 0;
    outOccupancy.count = 0;
    outDepthCache.count = 0;
    for (const Road& road : roads) {
        outSegments.count += road.sideA.segments.size() + road.sideB.segments.size();
        outWallSegments.count += road.sideA.wallSegments.size() + road.sideB.wallSegments.size();
        outOccupancy.count +=
            road.sideA.mainOccupancyT.size() + road.sideB.mainOccupancyT.size();
        outDepthCache.count += road.sideA.depthCacheEntries.size()
                               + road.sideB.depthCacheEntries.size();
    }

    if (nestedTotal.bytes >= outShell.bytes) {
        outShell.bytes = 0;
    } else {
        outShell.bytes -= nestedTotal.bytes;
    }
}

void junctionsStorage(const std::vector<Junction>& junctions, MemoryStat& outShell,
                      MemoryStat& outRoadIds) {
    outShell = vectorStorage(junctions);
    outRoadIds = {};
    for (const Junction& junction : junctions) {
        outRoadIds += vectorStorage(junction.roadIds);
    }
    outRoadIds.count = 0;
    for (const Junction& junction : junctions) {
        outRoadIds.count += junction.roadIds.size();
    }
    if (outRoadIds.bytes >= outShell.bytes) {
        outShell.bytes = 0;
    } else {
        outShell.bytes -= outRoadIds.bytes;
    }
}

void buildingInstancesStorage(const std::vector<BuildingInstance>& instances, MemoryStat& outShell,
                              MemoryStat& outPlot, MemoryStat& outFootprints,
                              MemoryStat& outTypeIds) {
    outShell = vectorStorage(instances);
    outPlot = {};
    outFootprints = {};
    outTypeIds = {};
    for (const BuildingInstance& instance : instances) {
        outPlot.bytes += sizeof(Plot);
        outPlot.count += 1;
        outFootprints += vectorStorage(instance.footprints);
        outTypeIds.bytes += sizeof(BuildingTypeId);
        outTypeIds.count += 1;
        outShell.bytes += sizeof(std::uint32_t) + sizeof(BuildingTypeId) + sizeof(BuildingPlacementMode)
                           + sizeof(int) * 2;
    }
    const MemoryStat nested = outPlot + outFootprints + outTypeIds;
    if (nested.bytes >= outShell.bytes) {
        outShell.bytes = 0;
    } else {
        outShell.bytes -= nested.bytes;
    }
}

MemoryStat placementFrontierStorage(const FrontierManager& frontiers,
                                    std::vector<MemoryLine>& detail,
                                    const TerrainCatalog* catalog = nullptr) {
    MemoryStat total;
    const char* bandNames[] = {"core", "suburban", "rural"};
    for (int i = 0; i < 3; ++i) {
        const MemoryStat plotStat = vectorStorage(frontiers.plot[i]);
        detail.push_back({"frontiers.plot[" + std::string(bandNames[i]) + "]", plotStat, {}});
        total += plotStat;

        const MemoryStat wallStat = vectorStorage(frontiers.wall[i]);
        detail.push_back({"frontiers.wall[" + std::string(bandNames[i]) + "]", wallStat, {}});
        total += wallStat;
    }

    const MemoryStat alleyStat = vectorStorage(frontiers.alley);
    detail.push_back({"frontiers.alley", alleyStat, {}});
    total += alleyStat;

    for (std::size_t i = 0; i < frontiers.border.size(); ++i) {
        const MemoryStat borderStat = vectorStorage(frontiers.border[i]);
        if (borderStat.bytes == 0 && borderStat.count == 0) {
            continue;
        }
        const std::string label =
            catalog != nullptr
                ? ("frontierManager.border." + std::string(catalog->name(catalog->idAt(i))))
                : ("frontierManager.border[" + std::to_string(i) + "]");
        detail.push_back({label, borderStat, {}});
        total += borderStat;
    }

    for (std::size_t i = 0; i < frontiers.scan.size(); ++i) {
        const MemoryStat scanStat = vectorStorage(frontiers.scan[i]);
        if (scanStat.bytes == 0 && scanStat.count == 0) {
            continue;
        }
        const std::string label =
            catalog != nullptr
                ? ("frontierManager.scan." + std::string(catalog->name(catalog->idAt(i))))
                : ("frontierManager.scan[" + std::to_string(i) + "]");
        detail.push_back({label, scanStat, {}});
        total += scanStat;
    }

    total.bytes += sizeof(int) + sizeof(std::uint32_t);
    return total;
}

MemoryStat alleyProbesStorage(const std::vector<std::vector<AlleyProbeLine>>& probes) {
    MemoryStat stat = vectorStorage(probes);
    for (const std::vector<AlleyProbeLine>& bucket : probes) {
        stat += vectorStorage(bucket);
    }
    stat.count = 0;
    for (const std::vector<AlleyProbeLine>& bucket : probes) {
        stat.count += bucket.size();
    }
    return stat;
}

MemoryStat townRenderExcluded(const Town& town) {
    MemoryStat stat;
    stat += vertexArrayStorage(town.roadMesh);
    stat += vertexArrayStorage(town.junctionMesh);
    stat += vertexArrayStorage(town.roadEndProbeMesh);
    stat += vertexArrayStorage(town.frontageSegmentMesh);
    stat += vertexArrayStorage(town.frontageInwardArrowMesh);
    stat += vertexArrayStorage(town.hopDebugRoadMesh);
    stat += vertexArrayStorage(town.hopDebugJunctionMesh);
    stat += vertexArrayStorage(town.buildingOutlineMesh);
    stat += vertexArrayStorage(town.terrainBuildingOutlineMesh);
    stat += vectorStorage(town.plotLabels);
    stat += vectorStorage(town.buildingLabels);
    MemoryStat terrainLabels = vectorStorage(town.terrainPlotTypeLabels);
    for (const RotatedTextLabel& label : town.terrainPlotTypeLabels) {
        terrainLabels.bytes += label.text.capacity() + 1;
    }
    stat += terrainLabels;
    stat += vectorStorage(town.roadLabels);
    stat += vectorStorage(town.frontageSegmentLabels);
    stat += vectorStorage(town.roadEndProbeLabels);
    return stat;
}

MemoryStat configStorage(const Config& config) {
    MemoryStat stat;
    stat += stringStorage(config.window.title);
    stat += stringStorage(config.logging.directory);
    stat += stringStorage(config.terrain.imagePath);
    stat += stringStorage(config.terrain.colorsPath);
    for (const LogFileConfig& file : config.logging.files) {
        stat += stringStorage(file.channel);
        stat += stringStorage(file.filename);
    }
    stat.bytes += sizeof(Config);
    return stat;
}

MemoryStat townConfigStorage(const TownConfig& townConfig) {
    MemoryStat stat;
    stat.bytes += sizeof(TownConfig);
    for (const auto& [key, count] : townConfig.buildingCounts) {
        stat += stringStorage(key);
        stat.bytes += sizeof(int);
        (void)count;
    }
    stat.count = townConfig.buildingCounts.size();
    return stat;
}

MemoryStat defCacheStorage(const DefCache& defs, std::vector<MemoryLine>& detail) {
    MemoryStat total;

    MemoryStat buildingSizes;
    for (const auto& [key, band] : defs.buildingSizes()) {
        buildingSizes += stringStorage(key);
        buildingSizes += sizeBandStorage(band);
    }
    buildingSizes.count = defs.buildingSizes().size();
    detail.push_back({"buildingSizes", buildingSizes, {}});
    total += buildingSizes;

    MemoryStat plotSizes;
    for (const auto& [key, band] : defs.plotSizes()) {
        plotSizes += stringStorage(key);
        plotSizes += sizeBandStorage(band);
    }
    plotSizes.count = defs.plotSizes().size();
    detail.push_back({"plotSizes", plotSizes, {}});
    total += plotSizes;

    MemoryStat buildingTemplates;
    for (const auto& [key, tmpl] : defs.buildingTemplates()) {
        buildingTemplates += stringStorage(key);
        buildingTemplates += buildingTemplateStorage(tmpl);
    }
    buildingTemplates.count = defs.buildingTemplates().size();
    detail.push_back({"buildingTemplates", buildingTemplates, {}});
    total += buildingTemplates;

    MemoryStat buildings;
    for (const auto& [key, def] : defs.buildings()) {
        buildings += stringStorage(key);
        buildings += buildingDefStorage(def);
    }
    buildings.count = defs.buildings().size();
    detail.push_back({"buildings", buildings, {}});
    total += buildings;

    return total;
}

MemoryStat terrainGeometryStorage(const TerrainAtlas& terrain, std::vector<MemoryLine>& detail) {
    MemoryStat total;

    const MemoryStat forbidden = nestedVec2Polygons(terrain.forbiddenPolygons);
    detail.push_back({"forbiddenPolygons", forbidden, {}});
    total += forbidden;

    for (const auto& [terrainId, outlines] : terrain.outlinesByTerrainId) {
        const MemoryStat stat = nestedVec2Polygons(outlines);
        detail.push_back(
            {"outlinesByTerrainId[" + std::to_string(terrainId) + "]", stat, {}});
        total += stat;
    }

    const MemoryStat shoreGraph = nestedVec2Polygons(terrain.shoreRoadGraph);
    detail.push_back({"shoreRoadGraph", shoreGraph, {}});
    total += shoreGraph;

    const MemoryStat riverGraph = nestedVec2Polygons(terrain.riverRoadGraph);
    detail.push_back({"riverRoadGraph", riverGraph, {}});
    total += riverGraph;

    total.bytes += sizeof(int) * 4 + sizeof(float) * 4 + sizeof(TerrainId) + sizeof(bool);
    return total;
}

std::size_t sumDetailBytes(const std::vector<MemoryLine>& rows) {
    std::size_t total = 0;
    for (const MemoryLine& row : rows) {
        total += row.stat.bytes;
    }
    return total;
}

std::string formatBytes(std::size_t bytes) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    if (bytes >= 1024 * 1024) {
        oss << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024) {
        oss << (static_cast<double>(bytes) / 1024.0) << " KB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}

std::string timestampNow() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void writeDetailTable(std::ostream& out, const std::vector<MemoryLine>& rows) {
    out << "| Struct / field | Bytes | Count |\n";
    out << "|----------------|------:|------:|\n";
    for (const MemoryLine& row : rows) {
        out << "| " << row.path << " | " << row.stat.bytes << " | " << row.stat.count << " |\n";
    }
}

void writeTotalsTable(std::ostream& out, const std::vector<MemoryLine>& rows,
                      std::size_t grandTotal) {
    out << "| Component | Bytes | Human | % |\n";
    out << "|-----------|------:|------|--:|\n";
    for (const MemoryLine& row : rows) {
        const double pct =
            grandTotal > 0 ? (100.0 * static_cast<double>(row.stat.bytes) / grandTotal) : 0.0;
        out << "| " << row.path << " | " << row.stat.bytes << " | " << formatBytes(row.stat.bytes)
            << " | " << std::fixed << std::setprecision(1) << pct << " |\n";
    }
}

}  // namespace

MemoryReportResult MemoryReport::build(const Town& town, const TerrainAtlas& terrain,
                                       const DefCache& defs, const BuildingGrowthQueue& queue,
                                       const TownConfig& townConfig, const Config& config) {
    MemoryReportResult result;

    MemoryStat roadShell, roadSegments, roadWallSegments, roadOccupancy, roadDepthCache;
    roadsStorage(town.roads, roadShell, roadSegments, roadWallSegments, roadOccupancy,
                 roadDepthCache);

    MemoryStat junctionShell, junctionRoadIds;
    junctionsStorage(town.junctions, junctionShell, junctionRoadIds);

    MemoryStat instanceShell, instancePlot, instanceFootprints, instanceTypes;
    buildingInstancesStorage(town.buildingInstances, instanceShell, instancePlot, instanceFootprints,
                             instanceTypes);

    std::vector<MemoryLine> frontierDetail;
    const MemoryStat frontierTotal =
        placementFrontierStorage(town.frontierManager, frontierDetail, town.syncTerrainCatalog);

    result.townDetail.push_back({"roads (fixed fields)", roadShell, {}});
    result.townDetail.push_back({"roads.sideA/B.segments", roadSegments, {}});
    result.townDetail.push_back({"roads.sideA/B.wallSegments", roadWallSegments, {}});
    result.townDetail.push_back({"roads.sideA/B.mainOccupancyT", roadOccupancy, {}});
    result.townDetail.push_back({"roads.sideA/B.depthCacheEntries", roadDepthCache, {}});
    result.townDetail.push_back({"junctions (fixed fields)", junctionShell, {}});
    result.townDetail.push_back({"junctions[].roadIds", junctionRoadIds, {}});
    result.townDetail.push_back({"buildingInstances (fixed fields)", instanceShell, {}});
    result.townDetail.push_back({"buildingInstances[].plot", instancePlot, {}});
    result.townDetail.push_back({"buildingInstances[].footprints", instanceFootprints, {}});
    result.townDetail.push_back({"buildingInstances[].typeId", instanceTypes, {}});
    result.townDetail.insert(result.townDetail.end(), frontierDetail.begin(), frontierDetail.end());

    result.townDetail.push_back({"secondaryRoadRecords", vectorStorage(town.secondaryRoadRecords),
                                 {}});
    result.townDetail.push_back({"secondaryRoadIds", vectorStorage(town.secondaryRoadIds), {}});
    result.townDetail.push_back({"pendingAlleyFills", vectorStorage(town.pendingAlleyFills), {}});
    result.townDetail.push_back({"checkedAlleyGaps", wallGapSetStorage(town.checkedAlleyGaps), {}});
    result.townDetail.push_back(
        {"alleyCompleteRoadIds", intSetStorage(town.alleyCompleteRoadIds), {}});
    result.townDetail.push_back(
        {"placementFailedIndices", vectorStorage(town.placementFailedIndices), {}});
    result.townDetail.push_back(
        {"placementSkipReasonsSummary", stringStorage(town.placementSkipReasonsSummary), {}});
    result.townDetail.push_back({"junctionHopCache", vectorStorage(town.junctionHopCache), {}});
    result.townDetail.push_back({"roadHopCache", vectorStorage(town.roadHopCache), {}});
    result.townDetail.push_back(
        {"suburbanRoadListCache", vectorStorage(town.suburbanRoadListCache), {}});
    result.townDetail.push_back({"ruralRoadListCache", vectorStorage(town.ruralRoadListCache), {}});
    result.townDetail.push_back({"ringAvgDistByHop", vectorStorage(town.ringAvgDistByHop), {}});

    MemoryStat townScalars;
    townScalars.bytes += sizeof(Vec2) + sizeof(float) * 4 + sizeof(int) * 8 + sizeof(RingPhase)
                         + sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(bool) * 2
                         + sizeof(float) * 6;
    result.townDetail.push_back({"town (scalars + flags)", townScalars, {}});

    const MemoryStat terrainTotal = terrainGeometryStorage(terrain, result.terrainDetail);
    const MemoryStat defCacheTotal = defCacheStorage(defs, result.defCacheDetail);
    const MemoryStat queueTotal = stringsInVector(queue.queue());
    const MemoryStat townCfgTotal = townConfigStorage(townConfig);
    const MemoryStat configTotal = configStorage(config);

    const std::size_t townTotalBytes = sumDetailBytes(result.townDetail);

    result.totals.push_back({"Town", {townTotalBytes, town.buildingInstances.size()}, {}});
    result.totals.push_back({"TerrainAtlas (geometry)", terrainTotal, {}});
    result.totals.push_back({"DefCache", defCacheTotal, {}});
    result.totals.push_back({"BuildingGrowthQueue", queueTotal, {}});
    result.totals.push_back({"TownConfig", townCfgTotal, {}});
    result.totals.push_back({"Config", configTotal, {}});

    result.grandTotal =
        townTotalBytes + terrainTotal.bytes + defCacheTotal.bytes + queueTotal.bytes
        + townCfgTotal.bytes + configTotal.bytes;

    result.excluded.push_back(
        MemoryLine{"terrainAtlas.raster", vectorStorage(terrain.raster), "terrain image"});
    result.excluded.push_back(MemoryLine{"terrainAtlas.forbiddenDilated",
                                         vectorStorage(terrain.forbiddenDilated),
                                         "dilated forbidden mask"});
    result.excluded.push_back(
        MemoryLine{"terrainAtlas.overlayTexture", textureStorage(terrain.overlayTexture),
                   "SFML texture"});
    result.excluded.push_back(MemoryLine{"town.alleyProbesByQueueIndex",
                                         alleyProbesStorage(town.alleyProbesByQueueIndex),
                                         "debug-only (not in Unreal sim)"});
    result.excluded.push_back(MemoryLine{"town.alleyProbeFailMesh",
                                         vertexArrayStorage(town.alleyProbeFailMesh),
                                         "debug alley overlay"});
    result.excluded.push_back(
        MemoryLine{"town.renderMeshesAndLabels", townRenderExcluded(town), "render-only"});

    return result;
}

bool MemoryReport::writeMarkdown(const std::filesystem::path& logDirectory, const Town& town,
                                 const TerrainAtlas& terrain, const DefCache& defs,
                                 const BuildingGrowthQueue& queue, const TownConfig& townConfig,
                                 const Config& config) {
    const MemoryReportResult report = build(town, terrain, defs, queue, townConfig, config);

    std::filesystem::create_directories(logDirectory);
    const auto path = logDirectory / "memory.md";

    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }

    out << "# Simulation memory report\n\n";
    out << "Generated: " << timestampNow() << " | Buildings placed: "
        << town.buildingInstances.size() << " | Queue target: " << queue.activeCount() << "\n\n";
    out << "> Capacity-based heap estimate. Excludes terrain image rasters, render caches, and debug-only data (alley probe history).\n\n";

    out << "## Totals\n\n";
    writeTotalsTable(out, report.totals, report.grandTotal);
    out << "| **Grand total** | **" << report.grandTotal << "** | **"
        << formatBytes(report.grandTotal) << "** | **100.0** |\n\n";

    out << "## Town detail\n\n";
    writeDetailTable(out, report.townDetail);

    out << "\n## TerrainAtlas geometry\n\n";
    writeDetailTable(out, report.terrainDetail);

    out << "\n## DefCache detail\n\n";
    writeDetailTable(out, report.defCacheDetail);

    out << "\n## Excluded (not in totals)\n\n";
    out << "| Item | Bytes | Note |\n";
    out << "|------|------:|------|\n";
    for (const MemoryLine& row : report.excluded) {
        out << "| " << row.path << " | " << row.stat.bytes << " | " << row.note << " |\n";
    }

    out.close();

    Logger::log("app", "memory_report: total=" + std::to_string(report.grandTotal) + " bytes path="
                           + path.string());
    return true;
}
