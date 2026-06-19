#pragma once

#include "terrain/TerrainCatalog.h"

#include <string>

inline bool terrainIdIsForbidden(TerrainId id, const TerrainCatalog& catalog) {
    return catalog.isForbidden(id);
}

inline const char* terrainIdName(TerrainId id, const TerrainCatalog& catalog) {
    return catalog.name(id);
}
