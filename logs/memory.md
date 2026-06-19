# Simulation memory report

Generated: 2026-06-19 13:46:54 | Buildings placed: 194 | Queue target: 194

> Capacity-based heap estimate. Excludes terrain image rasters, render caches, and debug-only data (alley probe history).

## Totals

| Component | Bytes | Human | % |
|-----------|------:|------|--:|
| Town | 247361 | 241.6 KB | 61.5 |
| TerrainAtlas (geometry) | 132891 | 129.8 KB | 33.0 |
| DefCache | 4080 | 4.0 KB | 1.0 |
| BuildingGrowthQueue | 15870 | 15.5 KB | 3.9 |
| TownConfig | 544 | 544 B | 0.1 |
| Config | 1756 | 1.7 KB | 0.4 |
| **Grand total** | **402502** | **393.1 KB** | **100.0** |

## Town detail

| Struct / field | Bytes | Count |
|----------------|------:|------:|
| roads (fixed fields) | 42572 | 272 |
| roads.sideA/B.segments | 7488 | 532 |
| roads.sideA/B.wallSegments | 8628 | 667 |
| roads.sideA/B.mainOccupancyT | 1248 | 39 |
| roads.sideA/B.depthCacheEntries | 61120 | 1910 |
| junctions (fixed fields) | 10464 | 227 |
| junctions[].roadIds | 2176 | 544 |
| buildingInstances (fixed fields) | 0 | 194 |
| buildingInstances[].plot | 13968 | 194 |
| buildingInstances[].footprints | 35232 | 351 |
| buildingInstances[].typeId | 388 | 194 |
| frontiers.plot[core] | 2256 | 72 |
| frontiers.wall[core] | 5064 | 145 |
| frontiers.plot[suburban] | 1512 | 58 |
| frontiers.wall[suburban] | 3384 | 112 |
| frontiers.plot[rural] | 11376 | 264 |
| frontiers.wall[rural] | 11376 | 303 |
| frontiers.alley | 1008 | 1 |
| frontierManager.border.sea | 2688 | 27 |
| frontierManager.border.river | 2688 | 36 |
| frontierManager.scan.plains | 15168 | 317 |
| frontierManager.scan.forest | 2016 | 42 |
| frontierManager.scan.hills | 192 | 6 |
| secondaryRoadRecords | 836 | 16 |
| secondaryRoadIds | 76 | 16 |
| pendingAlleyFills | 12 | 0 |
| checkedAlleyGaps | 1472 | 46 |
| alleyCompleteRoadIds | 0 | 0 |
| placementFailedIndices | 0 | 0 |
| placementSkipReasonsSummary | 47 | 1 |
| junctionHopCache | 1216 | 227 |
| roadHopCache | 1444 | 272 |
| suburbanRoadListCache | 0 | 0 |
| ruralRoadListCache | 0 | 0 |
| ringAvgDistByHop | 148 | 28 |
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
| town.alleyProbesByQueueIndex | 43344 | debug-only (not in Unreal sim) |
| town.alleyProbeFailMesh | 0 | debug alley overlay |
| town.renderMeshesAndLabels | 3248180 | render-only |
