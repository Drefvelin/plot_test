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
    app/core/App.cpp
    app/core/Config.cpp
    app/core/DefCache.cpp
    app/core/TownConfig.cpp
    app/core/BuildingGrowthQueue.cpp
    app/core/BuildingPlacer.cpp
    app/core/PlotGeometry.cpp
    app/core/PlotDimensions.cpp
    app/core/PlacementLogging.cpp
    app/core/FrontageZones.cpp
    app/core/FrontagePlacement.cpp
    app/core/FrontageGapFill.cpp
    app/core/SecondaryRoadPlacement.cpp
    app/core/BuildingLayout.cpp
    app/core/Hud.cpp
    app/core/Logger.cpp
    app/core/Camera.cpp
    app/core/Town.cpp
    app/core/TownBuilder.cpp
    app/core/RoadNetwork.cpp
    app/core/CellSubdivision.cpp
    app/core/PlotGenerator.cpp
    app/core/PlotGeneratorCell.cpp
    app/core/TerrainColors.cpp
    app/core/TerrainBake.cpp
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
