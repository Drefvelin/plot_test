# Simulation memory report

Generated: 2026-06-19 00:00:14 | Buildings placed: 194 | Queue target: 194

> Capacity-based heap estimate. Excludes terrain image rasters, render caches, and debug-only data (alley probe history).

## Totals

| Component | Bytes | Human | % |
|-----------|------:|------|--:|
| Town | 277079 | 270.6 KB | 64.1 |
| TerrainAtlas (geometry) | 132891 | 129.8 KB | 30.8 |
| DefCache | 4080 | 4.0 KB | 0.9 |
| BuildingGrowthQueue | 15870 | 15.5 KB | 3.7 |
| TownConfig | 544 | 544 B | 0.1 |
| Config | 1646 | 1.6 KB | 0.4 |
| **Grand total** | **432110** | **422.0 KB** | **100.0** |

## Town detail

| Struct / field | Bytes | Count |
|----------------|------:|------:|
| roads (fixed fields) | 117494 | 321 |
| roads.sideA/B.segments | 7944 | 577 |
| roads.sideA/B.wallSegments | 8952 | 701 |
| roads.sideA/B.mainOccupancyT | 1232 | 20 |
| roads.sideA/B.depthCacheEntries | 5280 | 165 |
| junctions (fixed fields) | 16392 | 275 |
| junctions[].roadIds | 2568 | 642 |
| buildingInstances (fixed fields) | 0 | 194 |
| buildingInstances[].plot | 13968 | 194 |
| buildingInstances[].footprints | 34176 | 340 |
| buildingInstances[].typeId | 388 | 194 |
| frontiers.plot[core] | 3384 | 98 |
| frontiers.wall[core] | 5064 | 192 |
| frontiers.plot[suburban] | 2256 | 43 |
| frontiers.wall[suburban] | 5064 | 72 |
| frontiers.plot[rural] | 11376 | 294 |
| frontiers.wall[rural] | 11376 | 329 |
| frontiers.alley | 1512 | 15 |
| frontierManager.border.sea | 2688 | 27 |
| frontierManager.border.river | 2688 | 38 |
| frontierManager.scan.plains | 15168 | 334 |
| frontierManager.scan.forest | 2016 | 41 |
| frontierManager.scan.hills | 192 | 5 |
| secondaryRoadRecords | 1232 | 21 |
| secondaryRoadIds | 112 | 21 |
| pendingAlleyFills | 12 | 1 |
| checkedAlleyGaps | 1088 | 34 |
| alleyCompleteRoadIds | 0 | 0 |
| placementFailedIndices | 0 | 0 |
| placementSkipReasonsSummary | 47 | 1 |
| junctionHopCache | 1456 | 275 |
| roadHopCache | 1684 | 321 |
| suburbanRoadListCache | 0 | 0 |
| ruralRoadListCache | 0 | 0 |
| ringAvgDistByHop | 172 | 31 |
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
| town.alleyProbesByQueueIndex | 42608 | debug-only (not in Unreal sim) |
| town.alleyProbeFailMesh | 720 | debug alley overlay |
| town.renderMeshesAndLabels | 3615716 | render-only |
