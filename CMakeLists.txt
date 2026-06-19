cmake_minimum_required(VERSION 3.20)
project(PlotApp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)

FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG        2.6.1
    GIT_SHALLOW    TRUE
)
set(SFML_BUILD_AUDIO   OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SFML)

FetchContent_Declare(
    yaml_cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    GIT_SHALLOW    TRUE
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml_cpp)

add_executable(PlotApp
    app/core/main.cpp
    app/core/common/Geometry.cpp
    app/core/common/RenderPrimitives.cpp
    app/core/config/YamlUtil.cpp
    app/core/config/Config.cpp
    app/core/config/DefCache.cpp
    app/core/config/TownConfig.cpp
    app/core/render/App.cpp
    app/core/render/Hud.cpp
    app/core/render/Camera.cpp
    app/core/util/Logger.cpp
    app/core/util/MemoryReport.cpp
    app/core/util/Profile.cpp
    app/core/terrain/TerrainColors.cpp
    app/core/terrain/TerrainCatalog.cpp
    app/core/terrain/TerrainBake.cpp
    app/core/town/Town.cpp
    app/core/town/TownJunctions.cpp
    app/core/town/TownFrontage.cpp
    app/core/town/TownCarving.cpp
    app/core/town/TownSecondary.cpp
    app/core/town/TownBridgeBuckets.cpp
    app/core/town/TownMeshes.cpp
    app/core/generation/TownBuilder.cpp
    app/core/roads/RoadNetwork.cpp
    app/core/roads/CorridorCull.cpp
    app/core/roads/RoadIntersectionSplit.cpp
    app/core/roads/WaterSanitize.cpp
    app/core/roads/RoadDedup.cpp
    app/core/roads/BridgeResolve.cpp
    app/core/roads/SecondaryRoadPlacement.cpp
    app/core/roads/RoadExhaustion.cpp
    app/core/placement/orchestration/BuildingGrowthQueue.cpp
    app/core/placement/orchestration/BuildingPlacer.cpp
    app/core/placement/orchestration/GrowthRings.cpp
    app/core/placement/orchestration/MovableRelocation.cpp
    app/core/placement/orchestration/PlacementFloors.cpp
    app/core/placement/orchestration/PlacementPrep.cpp
    app/core/placement/zones/FrontageZones.cpp
    app/core/placement/frontage/FrontagePlacement.cpp
    app/core/placement/frontage/FrontageGapFill.cpp
    app/core/placement/frontier/PlacementFrontier.cpp
    app/core/placement/frontier/FrontierManager.cpp
    app/core/placement/frontier/FrontierSlotUtils.cpp
    app/core/placement/frontier/TerrainScanFrontier.cpp
    app/core/placement/frontier/BorderFrontier.cpp
    app/core/placement/terrain/TerrainPlacement.cpp
    app/core/placement/terrain/BorderPlacement.cpp
    app/core/placement/geometry/PlotGeometry.cpp
    app/core/placement/geometry/PlotDimensions.cpp
    app/core/placement/geometry/BuildingLayout.cpp
    app/core/placement/logging/PlacementLogging.cpp
    app/core/placement/logging/TerrainPlacementLogging.cpp
)

target_link_libraries(PlotApp PRIVATE
    sfml-graphics
    sfml-window
    sfml-system
    yaml-cpp
)

target_include_directories(PlotApp PRIVATE app/core)

add_custom_command(TARGET PlotApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/app/config"
        "$<TARGET_FILE_DIR:PlotApp>/config"
    COMMENT "Copying app/config to output directory"
)

add_custom_command(TARGET PlotApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PlotApp>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:PlotApp>/assets"
    COMMENT "Copying assets to output directory"
)
