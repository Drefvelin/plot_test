#pragma once

#include "Config.h"
#include "Town.h"

struct TerrainAtlas;

class TownBuilder {
public:
    static Town build(const Config& config, const TerrainAtlas* terrain = nullptr);
};
