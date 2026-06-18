#pragma once

#include "DefCache.h"
#include "PlacementLogging.h"
#include "Town.h"

struct FrontageSlot;
struct TerrainAtlas;
struct TerrainPlacementTrace;
struct BandFilter;
struct TerrainAnchorBfsResult;

struct TerrainPlacementTrace {
    int         queueIndex = -1;
    const char* phase      = "";
    int         slotOrder  = 0;
};

void logTerrainTraceBegin(const TerrainPlacementTrace& trace, const BuildingDef& def);
void logTerrainTracePhase(TerrainPlacementTrace& trace, const char* phase);
void logTerrainTracePhaseEnd(const TerrainPlacementTrace& trace, bool success,
                             const std::string& summary);
void logTerrainTraceSlotQueue(const TerrainPlacementTrace& trace,
                              const std::vector<FrontageSlot>& slots,
                              const std::string& buildingType, const DefCache& defs,
                              const TerrainAtlas* terrain, std::size_t maxLogged = 40);
void logTerrainTraceSlotTry(const TerrainPlacementTrace& trace, const FrontageSlot& slot,
                            float zoneScore, const char* result, const char* detail = nullptr);
void logTerrainTraceBorderSummary(const TerrainPlacementTrace& trace, int samplesTotal,
                                  int spanUsedSkip, int noRoadSnap, int dimFail,
                                  int terrainForbidden, int invalidPlot, int overlap,
                                  int alleyOverlap, int borderBandReject, int scoreBeat);
void logTerrainTraceBorderWinner(const TerrainPlacementTrace& trace, const BuildingDef& def,
                                 const TerrainAtlas& terrain, const Plot& plot, int roadId,
                                 int bankIndex, int graphIndex, float edgeDist, float score);
void logTerrainTracePlaced(const TerrainPlacementTrace& trace, const char* path,
                           const BuildingInstance& instance, const BuildingDef& def,
                           const Town& town, const TerrainAtlas* terrain,
                           const PlacementSearchLog* searchLog);
void logTerrainTraceFailed(const TerrainPlacementTrace& trace, const char* reason,
                           const std::string& detail = {});

void logTerrainAnchorStored(int queueIndex, TerrainId prefer, int roadId, const char* source,
                            const TerrainCatalog* catalog = nullptr);

void logTerrainAnchorReplaced(int queueIndex, TerrainId prefer, int prevRoadId, int newRoadId,
                              const char* source, const TerrainCatalog* catalog = nullptr);

void logTerrainAnchorBfs(int queueIndex, TerrainId prefer, int anchorRoadId, int maxRoads,
                         const TerrainAnchorBfsResult& bfs, const Town& town, float suburbanDist,
                         const TerrainCatalog* catalog = nullptr);

void logTerrainAnchorRoadTry(int queueIndex, int roadId, int graphDist, float centerDist,
                             const char* result, const std::string& detail = {});

void logTerrainAnchorRoadFrontier(int queueIndex, int roadId, const Town& town,
                                  float suburbanDist);

void logTerrainScanFrontierHead(int queueIndex, TerrainId prefer, const Town& town,
                                const BandFilter& bandFilter, float suburbanDist,
                                float proximityMaxDist, std::size_t maxLogged = 25,
                                const TerrainCatalog* catalog = nullptr);

void logTerrainSegmentLookup(int queueIndex, int segmentId, const Town& town, int anchorRoadId,
                             const TerrainAnchorBfsResult* bfs);
