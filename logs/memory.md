# Simulation memory report

Generated: 2026-06-19 12:10:21 | Buildings placed: 0 | Queue target: 0

> Capacity-based heap estimate. Excludes terrain image rasters, render caches, and debug-only data (alley probe history).

## Totals

| Component | Bytes | Human | % |
|-----------|------:|------|--:|
| Town | 139097 | 135.8 KB | 47.3 |
| TerrainAtlas (geometry) | 132891 | 129.8 KB | 45.2 |
| DefCache | 4080 | 4.0 KB | 1.4 |
| BuildingGrowthQueue | 15870 | 15.5 KB | 5.4 |
| TownConfig | 544 | 544 B | 0.2 |
| Config | 1662 | 1.6 KB | 0.6 |
| **Grand total** | **294144** | **287.2 KB** | **100.0** |

## Town detail

| Struct / field | Bytes | Count |
|----------------|------:|------:|
| roads (fixed fields) | 67152 | 236 |
| roads.sideA/B.segments | 5244 | 437 |
| roads.sideA/B.wallSegments | 5244 | 437 |
| roads.sideA/B.mainOccupancyT | 0 | 0 |
| roads.sideA/B.depthCacheEntries | 0 | 0 |
| junctions (fixed fields) | 10748 | 200 |
| junctions[].roadIds | 1892 | 472 |
| buildingInstances (fixed fields) | 0 | 0 |
| buildingInstances[].plot | 0 | 0 |
| buildingInstances[].footprints | 0 | 0 |
| buildingInstances[].typeId | 0 | 0 |
| frontiers.plot[core] | 0 | 0 |
| frontiers.wall[core] | 0 | 0 |
| frontiers.plot[suburban] | 672 | 24 |
| frontiers.wall[suburban] | 672 | 26 |
| frontiers.plot[rural] | 11376 | 364 |
| frontiers.wall[rural] | 11376 | 393 |
| frontiers.alley | 0 | 0 |
| frontierManager.border.sea | 2688 | 31 |
| frontierManager.border.river | 2688 | 39 |
| frontierManager.scan.plains | 15168 | 326 |
| frontierManager.scan.forest | 2016 | 44 |
| frontierManager.scan.hills | 192 | 6 |
| secondaryRoadRecords | 0 | 0 |
| secondaryRoadIds | 0 | 0 |
| pendingAlleyFills | 0 | 0 |
| checkedAlleyGaps | 0 | 0 |
| alleyCompleteRoadIds | 0 | 0 |
| placementFailedIndices | 0 | 0 |
| placementSkipReasonsSummary | 47 | 1 |
| junctionHopCache | 800 | 0 |
| roadHopCache | 944 | 0 |
| suburbanRoadListCache | 0 | 0 |
| ruralRoadListCache | 0 | 0 |
| ringAvgDistByHop | 80 | 0 |
| town (scalars + flags) | 98 | 0 |

## TerrainAtlas geometry

| Struct / field | Bytes | Count |
|----------------|------:|------:|
| forbiddenPolygons | 43184 | 4350 |
| outlinesByTerrainId[1] | 43184 | 4249 |
| outlinesByTerrainId[2] | 43184 | 4205 |
| outlinesByTerrainId[4] | 1112 | 124 |
| outlinesByTerrainId[5] | 1304 | 120 |
| outlinesByTerrainId[3] | 0 | 0 |
| shoreRoadGraph | 528 | 47 |
| riverRoadGraph | 360 | 35 |

## DefCache detail

| Struct / field | Bytes | Count |
|----------------|------:|------:|
| buildingSizes | 510 | 5 |
| plotSizes | 510 | 5 |
| buildingTemplates | 785 | 5 |
| buildings | 2275 | 8 |

## Excluded (not in totals)

| Item | Bytes | Note |
|------|------:|------|
| terrainAtlas.raster | 2097152 | terrain image |
| terrainAtlas.forbiddenDilated | 1048576 | dilated forbidden mask |
| terrainAtlas.overlayTexture | 4194344 | SFML texture |
| town.alleyProbesByQueueIndex | 0 | debug-only (not in Unreal sim) |
| town.alleyProbeFailMesh | 0 | debug alley overlay |
| town.renderMeshesAndLabels | 2739924 | render-only |
