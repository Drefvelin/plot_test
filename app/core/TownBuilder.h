#pragma once

#include "Config.h"
#include "PlacementFloors.h"
#include "Town.h"

struct TerrainAtlas;

class TownBuilder {
public:
    static Town build(const Config& config, const TerrainAtlas* terrain,
                      const PlacementFloors& floors, const TownConfig& townCfg);
};
