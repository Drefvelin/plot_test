#pragma once

#include "placement/frontier/PlacementFrontier.h"
#include "terrain/TerrainCatalog.h"
#include "common/Vec2.h"

#include <cstdint>
#include <vector>

struct TerrainAtlas;
struct BandFilter;
struct Plot;
struct PlotConfig;
struct Town;

enum class PlacementEventType : std::uint8_t {
    FullRebuild,
    PlotCarved,
    RingExtended,
    TopologyChanged,
    InstanceRemoved,
};

struct PlacementEvent {
    PlacementEventType type = PlacementEventType::FullRebuild;
    int                roadId     = -1;
    int                bankIndex  = 0;
    int                instanceId = -1;
    float              carveTMin  = 0.f;
    float              carveTMax  = 0.f;
    float              prevSuburbanDist = 0.f;
    float              prevCoreDist     = -1.f;
    bool               wasBorderPlot    = false;
};

struct TerrainScanSlotRef {
    FrontierRef base;
    float       edgeDist       = 1e30f;
    bool        insideEligible = false;
};

struct BorderSlotRef {
    FrontierRef base;
    TerrainId   terrainId      = kTerrainUnknown;
    Vec2        outlinePoint{};
    Vec2        outlineTangent{};
    Vec2        outlineInward{};
    float       hitDist        = 0.f;
    int         graphIndex     = -1;
    float       arcT           = 0.f;
};

struct FrontierManager {
    std::vector<FrontierRef>      plot[3];
    std::vector<FrontierRef>      wall[3];
    std::vector<AlleyFrontierRef> alley;
    std::vector<std::vector<TerrainScanSlotRef>> scan;
    std::vector<std::vector<BorderSlotRef>>      border;
    std::uint32_t                                generation = 0;
};

const std::vector<TerrainScanSlotRef>& scanSlotsFor(const FrontierManager& mgr,
                                                    const TerrainCatalog& catalog, TerrainId id);
std::vector<TerrainScanSlotRef>& scanSlotsFor(FrontierManager& mgr, const TerrainCatalog& catalog,
                                              TerrainId id);

void ensureFrontierBucketSize(FrontierManager& mgr, std::size_t catalogCount);

void rebuildTerrainScanFrontier(Town& town, const TerrainAtlas& terrain,
                                const TerrainCatalog& catalog,
                                const TerrainProbeConfig& probes);

struct NearestRoadBankSnap {
    bool  valid      = false;
    int   roadId     = -1;
    int   bankIndex  = 0;
    Vec2  roadStart{};
    Vec2  edgeDir{};
    Vec2  bankInward{};
    float roadT      = 0.f;
    float dist       = 1e30f;
};

bool findNearestRoadBankToPoint(const Town& town, const Vec2& p, NearestRoadBankSnap& out);

void pushFrontierNotifySuppress(Town& town);
void popFrontierNotifySuppress(Town& town);
bool frontierNotifySuppressed(const Town& town);

void notifyPlacementFrontier(Town& town, const PlacementEvent& event,
                             const TerrainAtlas* terrain = nullptr,
                             const TerrainCatalog* catalog = nullptr,
                             const TerrainProbeConfig* probes = nullptr,
                             const PlotConfig* plots       = nullptr);

void notifyRoadFrontierRefresh(Town& town, int roadId, const TerrainAtlas* terrain = nullptr,
                               const TerrainCatalog* catalog = nullptr,
                               const TerrainProbeConfig* probes = nullptr,
                               const PlotConfig* plots = nullptr);
