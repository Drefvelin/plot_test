#pragma once

#include <SFML/Graphics.hpp>

#include <cmath>
#include <unordered_set>
#include <vector>

struct TerrainAtlas;

struct Vec2 {
    float x = 0.f;
    float y = 0.f;

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }

    float dot(const Vec2& o) const { return x * o.x + y * o.y; }
    float length() const { return std::sqrt(x * x + y * y); }

    Vec2 normalized() const {
        const float len = length();
        if (len < 1e-6f) {
            return {};
        }
        return {x / len, y / len};
    }
};

inline Vec2 perpendicular(const Vec2& v) { return {-v.y, v.x}; }

// Shared boundary segment between two cells. Geometry is stored in world units.
// Available frontage span on one side of a road (one Voronoi cell).
struct RoadFrontageSegment {
    int   id         = -1;
    float startT     = 0.f;
    float endT       = 0.f;
    float centerDist = 0.f;

    float width() const { return endT - startT; }
};

struct RoadSideFrontage {
    int                              cellId = -1;
    Vec2                             inward{};  // perpendicular into cell, from edge probe
    std::vector<RoadFrontageSegment> segments;
};

struct Road {
    int   id     = -1;
    Vec2  a;
    Vec2  b;
    int   cellA  = -1;
    int   cellB  = -1;
    bool  isSecondary       = false;
    bool  isTerrainCorridor = false;
    bool  isBridge          = false;
    int   addedAtQueueIndex = -1;
    int   hostCellId        = -1;
    RoadSideFrontage sideA;
    RoadSideFrontage sideB;

    float length() const { return (b - a).length(); }

    bool isSameCellSecondary() const {
        return isSecondary && cellA >= 0 && cellA == cellB;
    }

    RoadSideFrontage* sideBank(int bankIndex) {
        return bankIndex == 1 ? &sideB : &sideA;
    }

    const RoadSideFrontage* sideBank(int bankIndex) const {
        return bankIndex == 1 ? &sideB : &sideA;
    }

    RoadSideFrontage* sideForCell(int cellId) {
        if (cellId == cellA) {
            return &sideA;
        }
        if (cellId == cellB) {
            return &sideB;
        }
        return nullptr;
    }

    const RoadSideFrontage* sideForCell(int cellId) const {
        if (cellId == cellA) {
            return &sideA;
        }
        if (cellId == cellB) {
            return &sideB;
        }
        return nullptr;
    }

    RoadSideFrontage* sideForPlacement(int cellId, int bankIndex) {
        if (isSameCellSecondary()) {
            return sideBank(bankIndex);
        }
        return sideForCell(cellId);
    }

    const RoadSideFrontage* sideForPlacement(int cellId, int bankIndex) const {
        if (isSameCellSecondary()) {
            return sideBank(bankIndex);
        }
        return sideForCell(cellId);
    }
};

struct SecondaryRoadRecord {
    Vec2 a{};
    Vec2 b{};
    int  hostCellId        = -1;
    int  addedAtQueueIndex  = -1;
    bool isThrough          = false;
};

struct PendingAlleyFill {
    int addedAtQueueIndex    = -1;
    int hostCellId           = -1;
    int consecutiveFillFails = 0;
};

enum class AlleyCellState {
    Pending,
    Expanding,
    Finished,
};

struct WallGapKey {
    int   roadId    = -1;
    int   cellId    = -1;
    int   bankIndex = -1;
    float tMin      = 0.f;
    float tMax      = 0.f;
};

struct WallGapKeyHash {
    std::size_t operator()(const WallGapKey& key) const noexcept {
        std::size_t h = static_cast<std::size_t>(key.roadId);
        h             = h * 31u + static_cast<std::size_t>(key.cellId);
        h             = h * 31u + static_cast<std::size_t>(key.bankIndex);
        h             = h * 31u + static_cast<std::size_t>(key.tMin * 100.f);
        h             = h * 31u + static_cast<std::size_t>(key.tMax * 100.f);
        return h;
    }
};

struct WallGapKeyEqual {
    bool operator()(const WallGapKey& lhs, const WallGapKey& rhs) const noexcept {
        return lhs.roadId == rhs.roadId && lhs.cellId == rhs.cellId
               && lhs.bankIndex == rhs.bankIndex
               && std::abs(lhs.tMin - rhs.tMin) < 0.05f && std::abs(lhs.tMax - rhs.tMax) < 0.05f;
    }
};

struct AlleyProbeLine {
    Vec2 a{};
    Vec2 b{};
    bool valid = false;
};

// Road endpoint where one or more roads meet.
struct Junction {
    int                id = -1;
    Vec2               pos{};
    std::vector<int>   roadIds;
};

// Road-facing lot: corners 0–1 on the road (frontage), corners 2–3 inset (depth).
struct Plot {
    int   id      = -1;
    int   cellId  = -1;
    int   roadId  = -1;
    int   roadBank = -1;  // 0/1 for secondary road banks; -1 = use sideForCell
    Vec2  corners[4] = {};
    float frontage     = 0.f;
    float depth        = 0.f;
    float area         = 0.f;
};

// One building footprint inside a plot (axis-aligned or rotated rectangle).
struct BuildingFootprint {
    Vec2        corners[4] = {};
    std::string sizeCategory;
    bool        mainBuilding = false;
    int         doorEdge     = -1;  // edge index i: segment corners[i] -> corners[(i+1)%4]
    int         labelId      = -1;  // index within the parent plot (0 = main building)
    float       placedShortLen = 0.f;  // width used when the rectangle was built
    float       placedLongLen  = 0.f;  // height used when the rectangle was built
    bool        tmplDoorLong = false;
    bool        tmplDoorShort = false;
    bool        tmplLongFacingMiddle = false;
    bool        tmplEdgePlacement = false;
    bool        tmplMiddlePlacement = false;
    bool        tmplCornerPlacement = false;
};

enum class BuildingPlacementMode {
    PlotLot,
    SegmentGapFill,
};

// One spawned building tied to a plot; removed when the growth slider moves down.
struct BuildingInstance {
    int                   id = -1;  // index in the growth queue
    std::string           buildingType;
    BuildingPlacementMode placementMode = BuildingPlacementMode::PlotLot;
    int                   roadId = -1;
    int                   cellId = -1;
    int                   roadBank = -1;  // 0/1 for secondary road banks
    Plot                  plot;
    std::vector<BuildingFootprint> footprints;
};

// One Voronoi cell with its bordering roads and subdivided plots.
struct Cell {
    int                id = -1;
    Vec2               site;      // Voronoi generator site
    Vec2               centroid;  // geometric center of the clipped boundary polygon
    std::vector<Vec2>  boundary;
    std::vector<int>   roadIds;
    std::vector<Plot>  plots;
    AlleyCellState     alleyState = AlleyCellState::Pending;
    int                voronoiParentId = -1;
};

inline Vec2 polygonCentroid(const std::vector<Vec2>& polygon, const Vec2& fallback = {}) {
    if (polygon.size() < 3) {
        return polygon.empty() ? fallback : polygon[0];
    }

    double signedArea = 0.0;
    double cx         = 0.0;
    double cy         = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Vec2& p0 = polygon[i];
        const Vec2& p1 = polygon[(i + 1) % polygon.size()];
        const double cross =
            static_cast<double>(p0.x) * p1.y - static_cast<double>(p1.x) * p0.y;
        signedArea += cross;
        cx += (static_cast<double>(p0.x) + p1.x) * cross;
        cy += (static_cast<double>(p0.y) + p1.y) * cross;
    }
    signedArea *= 0.5;
    if (std::abs(signedArea) < 1e-8) {
        Vec2 avg{};
        for (const Vec2& p : polygon) {
            avg = avg + p;
        }
        return avg * (1.f / static_cast<float>(polygon.size()));
    }
    return {static_cast<float>(cx / (6.0 * signedArea)),
            static_cast<float>(cy / (6.0 * signedArea))};
}

// Screen-space label anchor (diagram pixels).
struct FrontageSegmentLabel {
    int   id        = -1;
    float centerXPx = 0.f;
    float centerYPx = 0.f;
};

struct Town {
    std::vector<Cell> cells;
    std::vector<Road> roads;
    std::vector<Junction> junctions;
    Vec2              center;
    float             radius   = 0.f;
    float             width    = 0.f;
    float             height   = 0.f;
    int               frontageSegmentIdCounter = 0;
    sf::VertexArray   roadMesh{sf::Triangles};
    sf::VertexArray   junctionMesh{sf::Triangles};
    sf::VertexArray   cellCentroidMesh{sf::Triangles};
    sf::VertexArray   cellSiteMesh{sf::Triangles};
    sf::VertexArray   roadEndProbeMesh{sf::Triangles};
    sf::VertexArray   frontageSegmentMesh{sf::Triangles};
    sf::VertexArray   frontageInwardArrowMesh{sf::Triangles};
    std::vector<BuildingInstance> buildingInstances;
    std::vector<SecondaryRoadRecord> secondaryRoadRecords;
    std::vector<std::vector<AlleyProbeLine>> alleyProbesByQueueIndex;
    std::unordered_set<WallGapKey, WallGapKeyHash, WallGapKeyEqual> checkedAlleyGaps;
    int                              activeAlleyCellId = -1;
    std::vector<PendingAlleyFill>    pendingAlleyFills;
    int               primaryRoadCount = 0;
    sf::VertexArray   alleyProbeFailMesh{sf::Triangles};
    sf::VertexArray   buildingOutlineMesh{sf::Triangles};
    std::vector<FrontageSegmentLabel> plotLabels;
    std::vector<FrontageSegmentLabel> buildingLabels;
    std::vector<FrontageSegmentLabel> frontageSegmentLabels;
    std::vector<FrontageSegmentLabel> cellCentroidLabels;
    std::vector<FrontageSegmentLabel> roadEndProbeLabels;

    std::size_t plotCount() const;
};

// Build cell.boundary by chaining this cell's road segments (no Voronoi library).
void rebuildCellBoundaryFromRoads(Cell& cell, const std::vector<Road>& roads,
                                   const std::vector<Junction>& junctions);
void rebuildAllCellBoundaries(Town& town, int& boundaryOk, int& boundaryFail);

// True if p lies inside the cell polygon formed from its roads.
bool pointInCell(const Vec2& p, const Cell& cell, const std::vector<Road>& roads,
                 const std::vector<Junction>& junctions = {});

// Uses stored cell.boundary when available (same test as setback probes).
bool pointInCellBoundary(const Vec2& p, const Cell& cell, const std::vector<Road>& roads);

inline bool pointInCell(const Vec2& p, const Cell& cell, const Town& town) {
    return pointInCell(p, cell, town.roads, town.junctions);
}

void indexJunctions(Town& town);
void buildJunctionMesh(Town& town, float pixelsPerUnit, float radiusUnits = 1.f);
void buildCellCentroidMesh(Town& town, float pixelsPerUnit, float radiusUnits = 1.f);
void buildCellSiteMesh(Town& town, float pixelsPerUnit, float radiusUnits = 1.f);
void assignRoadSideInwards(Town& town);
void assignSecondaryRoadInwards(Road& road, const Town& town);
void buildSecondaryRoadFrontageSegments(Road& road, Town& town, float frontageSetback);
void syncAlleyCellStates(Town& town);
void trimSecondaryRoadRecords(Town& town, int targetCount);
void rebuildSecondaryRoadsFromRecords(Town& town);
void removeSecondaryRoadAtQueueIndex(Town& town, int queueIndex);
void buildRoadEndProbeMesh(Town& town, float pixelsPerUnit, float probeLengthUnits = 2.f);
void resetRoadFrontageSegments(Town& town, float frontageSetback);
void carveRoadFrontageForPlot(Town& town, const Plot& plot, float frontageSetback);
void carveRoadFrontageForFootprint(Town& town, int roadId, int cellId,
                                   const BuildingFootprint& mainFootprint);
bool validSetbackProbe(const Vec2& roadPoint, const Vec2& inward, float setback, const Cell& cell,
                       const std::vector<Road>& roads, int excludeRoadId = -1);
void rebuildRoadMesh(Town& town, const std::array<uint8_t, 3>& primaryColor,
                     const std::array<uint8_t, 3>& secondaryColor,
                     const std::array<uint8_t, 3>& bridgeColor, float pixelsPerUnit,
                     const TerrainAtlas* terrain, bool clipRoadsAtWater);
