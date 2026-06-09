#include "CellSubdivision.h"

#include "Logger.h"
#include "PlotGeometry.h"
#include "TerrainAtlas.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace {

constexpr float kJunctionEps = 0.08f;

bool nearPoint(const Vec2& a, const Vec2& b, float eps = kJunctionEps) {
    return (a - b).length() <= eps;
}

int findJunctionIndex(const Vec2& p, const std::vector<Junction>& junctions) {
    for (std::size_t i = 0; i < junctions.size(); ++i) {
        if (nearPoint(junctions[i].pos, p)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

struct OutEdge {
    int   toJ    = -1;
    int   roadId = -1;
    float angle  = 0.f;
};

struct FaceStep {
    int fromJ  = -1;
    int toJ    = -1;
    int roadId = -1;
};

struct ExtractedFace {
    std::vector<int>      junctionRing;
    std::vector<FaceStep> steps;
};

std::vector<std::vector<OutEdge>> buildJunctionAdjacency(const Town& town) {
    std::vector<std::vector<OutEdge>> adjacency(town.junctions.size());

    for (const Road& road : town.roads) {
        if (road.id < 0) {
            continue;
        }
        const int ja = findJunctionIndex(road.a, town.junctions);
        const int jb = findJunctionIndex(road.b, town.junctions);
        if (ja < 0 || jb < 0 || ja == jb) {
            continue;
        }

        const Vec2& pa = town.junctions[static_cast<std::size_t>(ja)].pos;
        const Vec2& pb = town.junctions[static_cast<std::size_t>(jb)].pos;
        const Vec2  ab = pb - pa;
        const float angleAB = std::atan2(ab.y, ab.x);
        const float angleBA = std::atan2(-ab.y, -ab.x);

        adjacency[static_cast<std::size_t>(ja)].push_back({jb, road.id, angleAB});
        adjacency[static_cast<std::size_t>(jb)].push_back({ja, road.id, angleBA});
    }

    for (std::vector<OutEdge>& edges : adjacency) {
        std::sort(edges.begin(), edges.end(),
                  [](const OutEdge& a, const OutEdge& b) { return a.angle < b.angle; });
    }
    return adjacency;
}

int findOutgoingIndex(const std::vector<OutEdge>& edges, int toJ) {
    for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
        if (edges[static_cast<std::size_t>(i)].toJ == toJ) {
            return i;
        }
    }
    return -1;
}

std::vector<int> canonicalFaceKey(const std::vector<int>& ring) {
    if (ring.empty()) {
        return {};
    }
    std::vector<int> key = ring;
    if (key.size() > 1 && key.front() == key.back()) {
        key.pop_back();
    }
    if (key.empty()) {
        return key;
    }

    int minIdx = 0;
    for (int i = 1; i < static_cast<int>(key.size()); ++i) {
        if (key[static_cast<std::size_t>(i)] < key[static_cast<std::size_t>(minIdx)]) {
            minIdx = i;
        }
    }

    std::vector<int> rotated;
    rotated.reserve(key.size());
    for (std::size_t i = 0; i < key.size(); ++i) {
        rotated.push_back(key[(static_cast<std::size_t>(minIdx) + i) % key.size()]);
    }
    return rotated;
}

bool extractFace(int startFromJ, int startToJ, const std::vector<std::vector<OutEdge>>& adjacency,
                 ExtractedFace& out) {
    out.junctionRing.clear();
    out.steps.clear();

    int curJ  = startFromJ;
    int nextJ = startToJ;
    out.junctionRing.push_back(curJ);

    for (int guard = 0; guard < 4096; ++guard) {
        const std::vector<OutEdge>& edges =
            adjacency[static_cast<std::size_t>(nextJ)];
        if (edges.empty()) {
            return false;
        }

        const int incomingIdx = findOutgoingIndex(edges, curJ);
        if (incomingIdx < 0) {
            return false;
        }

        const int nextIdx =
            (incomingIdx - 1 + static_cast<int>(edges.size())) % static_cast<int>(edges.size());
        const OutEdge& nextEdge = edges[static_cast<std::size_t>(nextIdx)];

        out.steps.push_back({curJ, nextJ, nextEdge.roadId});
        out.junctionRing.push_back(nextJ);

        curJ  = nextJ;
        nextJ = nextEdge.toJ;

        if (curJ == startFromJ && nextJ == startToJ) {
            if (out.junctionRing.size() >= 4) {
                out.junctionRing.pop_back();
            }
            return out.junctionRing.size() >= 3;
        }
    }

    return false;
}

std::vector<Vec2> facePolygon(const ExtractedFace& face, const std::vector<Junction>& junctions) {
    std::vector<Vec2> polygon;
    polygon.reserve(face.junctionRing.size());
    for (int junctionId : face.junctionRing) {
        if (junctionId >= 0 && junctionId < static_cast<int>(junctions.size())) {
            polygon.push_back(junctions[static_cast<std::size_t>(junctionId)].pos);
        }
    }
    return polygon;
}

bool insideTownDisc(const Vec2& p, const Town& town, float slack = 0.5f) {
    return (p - town.center).length() <= town.radius + slack;
}

int findVoronoiParentId(const Vec2& centroid,
                        const std::vector<VoronoiCellSnapshot>& voronoiSnapshot) {
    for (const VoronoiCellSnapshot& parent : voronoiSnapshot) {
        if (parent.boundary.size() >= 3 && pointInPolygon(centroid, parent.boundary)) {
            return parent.id;
        }
    }

    int   bestId     = -1;
    float bestDistSq = 1e30f;
    for (const VoronoiCellSnapshot& parent : voronoiSnapshot) {
        const float dx = centroid.x - parent.site.x;
        const float dy = centroid.y - parent.site.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestDistSq) {
            bestDistSq = d2;
            bestId     = parent.id;
        }
    }
    return bestId;
}

void assignRoadCellsFromFace(int cellId, const ExtractedFace& face, const Town& town,
                             std::vector<Road>& roads) {
    for (const FaceStep& step : face.steps) {
        if (step.roadId < 0 || step.roadId >= static_cast<int>(roads.size())) {
            continue;
        }
        Road& road = roads[static_cast<std::size_t>(step.roadId)];
        if (step.fromJ < 0 || step.toJ < 0 || step.fromJ >= static_cast<int>(town.junctions.size())
            || step.toJ >= static_cast<int>(town.junctions.size())) {
            continue;
        }

        const Vec2& fromPos = town.junctions[static_cast<std::size_t>(step.fromJ)].pos;
        const Vec2& toPos   = town.junctions[static_cast<std::size_t>(step.toJ)].pos;

        if (nearPoint(road.a, fromPos) && nearPoint(road.b, toPos)) {
            if (road.cellA < 0) {
                road.cellA = cellId;
            }
        } else if (nearPoint(road.a, toPos) && nearPoint(road.b, fromPos)) {
            if (road.cellB < 0) {
                road.cellB = cellId;
            }
        }
    }
}

}  // namespace

void subdivideCellsFromRoadGraph(Town& town, const TerrainAtlas& terrain,
                                 const std::vector<VoronoiCellSnapshot>& voronoiSnapshot) {
    const int cellsBefore = static_cast<int>(town.cells.size());
    const int roadsBefore = static_cast<int>(town.roads.size());

    if (town.junctions.empty() || town.roads.empty()) {
        Logger::log("voronoi", "subdivide skipped: empty graph");
        return;
    }

    for (Road& road : town.roads) {
        if (!road.isSecondary) {
            road.cellA = -1;
            road.cellB = -1;
        }
    }

    const std::vector<std::vector<OutEdge>> adjacency = buildJunctionAdjacency(town);
    std::set<std::pair<int, int>>           visitedDirected;
    std::set<std::vector<int>>              seenFaceKeys;
    std::vector<ExtractedFace>              faces;

    for (int fromJ = 0; fromJ < static_cast<int>(adjacency.size()); ++fromJ) {
        for (const OutEdge& edge : adjacency[static_cast<std::size_t>(fromJ)]) {
            const auto directedKey = std::make_pair(fromJ, edge.toJ);
            if (visitedDirected.count(directedKey) != 0) {
                continue;
            }

            ExtractedFace face;
            if (!extractFace(fromJ, edge.toJ, adjacency, face)) {
                continue;
            }

            for (const FaceStep& step : face.steps) {
                visitedDirected.insert({step.fromJ, step.toJ});
            }

            const std::vector<int> key = canonicalFaceKey(face.junctionRing);
            if (key.size() < 3 || seenFaceKeys.count(key) != 0) {
                continue;
            }
            seenFaceKeys.insert(key);
            faces.push_back(std::move(face));
        }
    }

    std::vector<Cell> newCells;
    newCells.reserve(faces.size());

    int discardedForbidden = 0;
    int discardedOutside   = 0;

    for (ExtractedFace& face : faces) {
        const std::vector<Vec2> polygon = facePolygon(face, town.junctions);
        if (polygon.size() < 3) {
            continue;
        }

        const Vec2 centroid = polygonCentroid(polygon);
        if (!insideTownDisc(centroid, town)) {
            ++discardedOutside;
            continue;
        }
        if (terrain.valid && terrain.isForbidden(centroid)) {
            ++discardedForbidden;
            continue;
        }

        Cell cell;
        cell.id              = static_cast<int>(newCells.size());
        cell.boundary        = polygon;
        cell.centroid        = centroid;
        cell.voronoiParentId = findVoronoiParentId(centroid, voronoiSnapshot);
        if (cell.voronoiParentId >= 0
            && cell.voronoiParentId < static_cast<int>(voronoiSnapshot.size())) {
            cell.site = voronoiSnapshot[static_cast<std::size_t>(cell.voronoiParentId)].site;
        } else {
            cell.site = centroid;
        }

        std::set<int> uniqueRoadIds;
        for (const FaceStep& step : face.steps) {
            if (step.roadId >= 0) {
                uniqueRoadIds.insert(step.roadId);
            }
        }
        cell.roadIds.assign(uniqueRoadIds.begin(), uniqueRoadIds.end());

        assignRoadCellsFromFace(cell.id, face, town, town.roads);
        newCells.push_back(std::move(cell));
    }

    town.cells = std::move(newCells);

    Logger::log("voronoi",
                "subdivide faces=" + std::to_string(faces.size()) + " cells_before="
                    + std::to_string(cellsBefore) + " cells_after="
                    + std::to_string(town.cells.size()) + " roads=" + std::to_string(roadsBefore)
                    + " discarded_outside=" + std::to_string(discardedOutside)
                    + " discarded_forbidden=" + std::to_string(discardedForbidden));
}
