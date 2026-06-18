#pragma once

#include "DefCache.h"
#include "FrontierManager.h"
#include "GrowthRings.h"
#include "TerrainCatalog.h"
#include "Town.h"

#include <unordered_set>

struct FrontageSlot;

const std::vector<BorderSlotRef>& borderSlotsFor(const FrontierManager& mgr,
                                                 const TerrainCatalog& catalog, TerrainId id);
std::vector<BorderSlotRef>& borderSlotsFor(FrontierManager& mgr, const TerrainCatalog& catalog,
                                           TerrainId id);

bool peekNextBorderSlot(const Town& town, const BuildingDef& def, const TerrainAtlas& terrain,
                        const BandFilter& bandFilter,
                        const std::unordered_set<int>& skipSegmentIds, BorderSlotRef& outRef,
                        FrontageSlot& outSlot);

void rebuildBorderFrontierImpl(Town& town, const TerrainAtlas& terrain,
                               const TerrainCatalog& catalog,
                               const TerrainProbeConfig& probes);

void borderFrontierOnPlotCarved(Town& town, int roadId, int bankIndex, const TerrainAtlas* terrain,
                                const TerrainCatalog* catalog, const TerrainProbeConfig* probes);

void consumeBorderSlot(Town& town, const BorderSlotRef& ref, const TerrainCatalog& catalog);
