#pragma once

#include "config/DefCache.h"
#include "placement/frontier/FrontierManager.h"
#include "placement/orchestration/GrowthRings.h"
#include "terrain/TerrainCatalog.h"
#include "town/Town.h"

#include <unordered_set>

struct FrontageSlot;

bool peekNextTerrainScanSlot(const Town& town, const BuildingDef& def, const TerrainAtlas& terrain,
                             const BandFilter& bandFilter,
                             const std::unordered_set<int>& skipSegmentIds,
                             TerrainScanSlotRef& outRef, FrontageSlot& outSlot, float& outScore);

void rebuildTerrainScanFrontierImpl(Town& town, const TerrainAtlas& terrain,
                                    const TerrainCatalog& catalog,
                                    const TerrainProbeConfig& probes);

void terrainScanFrontierOnPlotCarved(Town& town, int roadId, int bankIndex,
                                     const TerrainAtlas* terrain, const TerrainCatalog* catalog,
                                     const TerrainProbeConfig* probes);
void terrainScanFrontierOnFullRebuild(Town& town, const TerrainAtlas* terrain,
                                      const TerrainCatalog* catalog,
                                      const TerrainProbeConfig* probes);
