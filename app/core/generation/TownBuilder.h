#pragma once

#include "config/Config.h"
#include "placement/orchestration/PlacementFloors.h"
#include "town/Town.h"

struct TerrainAtlas;

class TownBuilder {
public:
    static Town build(const Config& config, const TerrainAtlas* terrain,
                      const PlacementFloors& floors, const TownConfig& townCfg,
                      const TerrainCatalog* catalog = nullptr,
                      const TerrainProbeConfig* probes = nullptr);
};
