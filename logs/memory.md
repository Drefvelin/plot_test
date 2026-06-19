# Simulation memory report

Generated: 2026-06-19 13:24:00 | Buildings placed: 2 | Queue target: 2

> Capacity-based heap estimate. Excludes terrain image rasters, render caches, and debug-only data (alley probe history).

## Totals

| Component | Bytes | Human | % |
|-----------|------:|------|--:|
| Town | 141215 | 137.9 KB | 47.7 |
| TerrainAtlas (geometry) | 132891 | 129.8 KB | 44.8 |
| DefCache | 4080 | 4.0 KB | 1.4 |
| BuildingGrowthQueue | 15870 | 15.5 KB | 5.4 |
| TownConfig | 544 | 544 B | 0.2 |
| Config | 1756 | 1.7 KB | 0.6 |
| **Grand total** | **296356** | **289.4 KB** | **100.0** |

## Town detail

| Struct / field | Bytes | Count |
|----------------|------:|------:|
| roads (fixed fields) | 68534 | 241 |
| roads.sideA/B.segments | 5268 | 439 |
| roads.sideA/B.wallSegments | 5268 | 439 |
| roads.sideA/B.mainOccupancyT | 8 | 1 |
| roads.sideA/B.depthCacheEntries | 192 | 6 |
| junctions (fixed fields) | 10712 | 203 |
| junctions[].roadIds | 1928 | 482 |
| buildingInstances (fixed fields) | 0 | 2 |
| buildingInstances[].plot | 144 | 2 |
| buildingInstances[].footprints | 288 | 3 |
| buildingInstances[].typeId | 4 | 2 |
| frontiers.plot[core] | 0 | 0 |
| frontiers.wall[core] | 0 | 0 |
| frontiers.plot[suburban] | 672 | 21 |
| frontiers.wall[suburban] | 672 | 23 |
| frontiers.plot[rural] | 11376 | 370 |
| frontiers.wall[rural] | 11376 | 395 |
| frontiers.alley | 0 | 0 |
| frontierManager.border.sea | 2688 | 30 |
| frontierManager.border.river | 2688 | 41 |
| frontierManager.scan.plains | 15168 | 329 |
| frontierManager.scan.forest | 2016 | 44 |
| frontierManager.scan.hills | 192 | 6 |
| secondaryRoadRecords | 0 | 0 |
| secondaryRoadIds | 0 | 0 |
| pendingAlleyFills | 0 | 0 |
| checkedAlleyGaps | 0 | 0 |
| alleyCompleteRoadIds | 0 | 0 |
| placementFailedIndices | 0 | 0 |
| placementSkipReasonsSummary | 47 | 1 |
| junctionHopCache | 812 | 203 |
| roadHopCache | 964 | 241 |
| suburbanRoadListCache | 0 | 0 |
| ruralRoadListCache | 0 | 0 |
| ringAvgDistByHop | 100 | 25 |
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
| town.alleyProbesByQueueIndex | 48 | debug-only (not in Unreal sim) |
| town.alleyProbeFailMesh | 0 | debug alley overlay |
| town.renderMeshesAndLabels | 2751384 | render-only |
