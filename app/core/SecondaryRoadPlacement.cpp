#include "SecondaryRoadPlacement.h"

#include "DefCache.h"
#include "Logger.h"
#include "PlotGeometry.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kInteriorCrossEps = 0.03f;

float cross2D(const Vec2& a, const Vec2& b) {
    return a.x * b.y - a.y * b.x;
}

float acuteAngleDegBetweenSegments(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1) {
    const Vec2 d1 = (a1 - a0).normalized();
    const Vec2 d2 = (b1 - b0).normalized();
    if (d1.length() < 1e-4f || d2.length() < 1e-4f) {
        return 90.f;
    }
    float dot = std::abs(d1.dot(d2));
    dot       = std::min(1.f, dot);
    return std::acos(dot) * 180.f / kPi;
}

bool alleyCrossingsMeetMinAngle(const Vec2& start, const Vec2& end, const Town& town,
                                int hostCellId, float minAngleDeg) {
    for (const Road& road : town.roads) {
        if (!road.isSecondary || road.hostCellId != hostCellId) {
            continue;
        }
        if (!segmentsCrossInInterior(start, end, road.a, road.b, kInteriorCrossEps)) {
            continue;
        }
        const float angle = acuteAngleDegBetweenSegments(start, end, road.a, road.b);
        if (angle + 1e-3f < minAngleDeg) {
            return false;
        }
    }
    return true;
}

bool alleyEndpointsMeetMinSpacing(const Vec2& start, const Vec2& end, const Town& town,
                                  int hostCellId, float minSpacing) {
    if (minSpacing <= 1e-3f) {
        return true;
    }

    for (const Road& road : town.roads) {
        if (!road.isSecondary || road.hostCellId != hostCellId) {
            continue;
        }
        const float dStartA = (start - road.a).length();
        const float dStartB = (start - road.b).length();
        const float dEndA   = (end - road.a).length();
        const float dEndB   = (end - road.b).length();
        if (dStartA < minSpacing - 1e-3f || dStartB < minSpacing - 1e-3f || dEndA < minSpacing - 1e-3f
            || dEndB < minSpacing - 1e-3f) {
            return false;
        }
    }
    return true;
}

struct AlleyProbeResult {
    Vec2  start{};
    Vec2  end{};
    float gapT     = 0.f;
    float angleDeg = 0.f;
    float length   = 0.f;
    bool  valid    = false;
};

Vec2 rotateVec(const Vec2& v, float degrees) {
    const float rad = degrees * kPi / 180.f;
    const float c   = std::cos(rad);
    const float s   = std::sin(rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

float rayToThroughRoadHit(const Town& town, int hostCellId, const Vec2& start, const Vec2& dir,
                          int excludeRoadId, float maxDist, int* outDestRoadId = nullptr) {
    float best     = -1.f;
    int   bestRoad = -1;
    for (const Road& road : town.roads) {
        if (road.id == excludeRoadId) {
            continue;
        }
        if (road.isSecondary) {
            if (road.hostCellId != hostCellId) {
                continue;
            }
        } else if (road.cellA != hostCellId && road.cellB != hostCellId) {
            continue;
        }
        const float hit = raySegmentHitDist(start, dir, road.a, road.b, maxDist);
        if (hit > 1.f && (best < 0.f || hit < best)) {
            best     = hit;
            bestRoad = road.id;
        }
    }
    if (outDestRoadId != nullptr) {
        *outDestRoadId = bestRoad;
    }
    return best;
}

bool roadInHostCell(const Road& road, int hostCellId) {
    if (road.isSecondary) {
        return road.hostCellId == hostCellId;
    }
    return road.cellA == hostCellId || road.cellB == hostCellId;
}

float rayToNearestRoadHit(const Town& town, int hostCellId, const Vec2& start, const Vec2& dir,
                          float maxDist, int excludeRoadIdA, int excludeRoadIdB) {
    float best = maxDist;
    for (const Road& road : town.roads) {
        if (road.id == excludeRoadIdA || road.id == excludeRoadIdB) {
            continue;
        }
        if (!roadInHostCell(road, hostCellId)) {
            continue;
        }
        const float hit = raySegmentHitDist(start, dir, road.a, road.b, maxDist);
        if (hit > 1.f && hit < best) {
            best = hit;
        }
    }
    return best;
}

float polygonArea2D(const std::vector<Vec2>& points) {
    if (points.size() < 3) {
        return 0.f;
    }
    float sum = 0.f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vec2& a = points[i];
        const Vec2& b = points[(i + 1) % points.size()];
        sum += cross2D(a, b);
    }
    return std::abs(sum) * 0.5f;
}

float alleyCreatedArea(const WallGap& gap, const Vec2& gapPt, const Vec2& start, const Vec2& end,
                       const Town& town, int destRoadId) {
    const float gapWidth = gap.width();
    const Vec2  gapLeft  = gapPt - gap.edgeDir * (gapWidth * 0.5f);
    const Vec2  gapRight = gapPt + gap.edgeDir * (gapWidth * 0.5f);

    if (destRoadId < 0 || destRoadId >= static_cast<int>(town.roads.size())) {
        return std::abs(cross2D(start - gapPt, end - gapPt)) * 0.5f;
    }

    const Road& destRoad = town.roads[static_cast<std::size_t>(destRoadId)];
    const Vec2  destTan  = (destRoad.b - destRoad.a).normalized();
    if (destTan.length() < 1e-4f) {
        return std::abs(cross2D(start - gapPt, end - gapPt)) * 0.5f;
    }

    const Vec2 endLeft  = end - destTan * (gapWidth * 0.5f);
    const Vec2 endRight = end + destTan * (gapWidth * 0.5f);
    return polygonArea2D({gapLeft, gapRight, endRight, endLeft});
}

bool alleyMeetsSideRoadClearance(const Town& town, int hostCellId, const Vec2& start, const Vec2& end,
                                 int sourceRoadId, int destRoadId, float minSideDist,
                                 int sampleCount) {
    if (minSideDist <= 1e-3f) {
        return true;
    }

    const Vec2  ab  = end - start;
    const float len = ab.length();
    if (len < 1e-3f) {
        return false;
    }

    const Vec2 dir  = ab * (1.f / len);
    const Vec2 left = perpendicular(dir).normalized();
    const int  samples = std::max(2, sampleCount);

    for (int i = 1; i < samples; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(samples);
        const Vec2  p = start + dir * (len * u);

        for (const int sideSign : {1, -1}) {
            const Vec2  perpDir = left * static_cast<float>(sideSign);
            const float hit =
                rayToNearestRoadHit(town, hostCellId, p, perpDir, 200.f, sourceRoadId, destRoadId);
            if (hit + 1e-3f < minSideDist) {
                return false;
            }
        }
    }
    return true;
}

float rayToCellBoundaryHit(const Cell& cell, const Vec2& start, const Vec2& dir, float maxDist) {
    float best = maxDist;
    if (cell.boundary.size() < 3) {
        return best;
    }
    for (std::size_t i = 0; i < cell.boundary.size(); ++i) {
        const Vec2& a = cell.boundary[i];
        const Vec2& b = cell.boundary[(i + 1) % cell.boundary.size()];
        const float hit = raySegmentHitDist(start, dir, a, b, maxDist);
        if (hit > 1.f && hit < best) {
            best = hit;
        }
    }
    return best;
}

float cellRayLimit(const Cell& cell, const Vec2& start, const Vec2& dir, float configMax) {
    const float boundaryReach = rayToCellBoundaryHit(cell, start, dir, 200.f);
    if (configMax > 1e-3f) {
        return std::min(boundaryReach, configMax);
    }
    return boundaryReach;
}

void buildGapTPositions(const WallGap& gap, float alleysPerUnitLength, std::vector<float>& out) {
    out.clear();
    const float width = gap.width();
    if (width < 1e-3f) {
        return;
    }

    const int probeCount =
        std::max(1, static_cast<int>(std::ceil(width * std::max(0.f, alleysPerUnitLength))));
    out.reserve(static_cast<std::size_t>(probeCount));
    for (int i = 0; i < probeCount; ++i) {
        const float u = probeCount == 1 ? 0.5f : static_cast<float>(i) / (probeCount - 1);
        out.push_back(gap.tMin + u * width);
    }
}

void buildAngleSamples(float maxAngleDeg, int angleCount, std::vector<float>& out) {
    out.clear();
    if (angleCount <= 1) {
        out.push_back(0.f);
        return;
    }
    out.reserve(static_cast<std::size_t>(angleCount));
    for (int i = 0; i < angleCount; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(angleCount - 1);
        out.push_back(-maxAngleDeg + u * (2.f * maxAngleDeg));
    }
}

bool probeThroughAlley(const Town& town, const WallGap& gap, float gapT, float angleDeg,
                       float minLength, float maxLength, float setback,
                       float minCrossingAngleDeg, float minEndpointSpacing, float minCreatedArea,
                       float minSideRoadDist, int sideRoadSampleCount, const DefCache& defs,
                       AlleyProbeResult& out) {
    out = {};
    if (gap.cellId < 0 || gap.cellId >= static_cast<int>(town.cells.size())) {
        return false;
    }

    const Cell& cell = town.cells[static_cast<std::size_t>(gap.cellId)];
    const Vec2  baseInward =
        gap.inward.length() > 1e-4f ? gap.inward.normalized() : Vec2{};
    if (baseInward.length() < 1e-4f) {
        return false;
    }

    const Vec2 gapPt = gap.origin + gap.edgeDir * gapT;
    const Vec2 dir   = rotateVec(baseInward, angleDeg).normalized();
    const Vec2 start = gapPt + dir * setback;

    out.gapT     = gapT;
    out.angleDeg = angleDeg;
    out.start    = start;

    if (!pointInCellBoundary(start, cell, town.roads)) {
        out.end = start + dir * std::max(minLength, 8.f);
        return false;
    }

    const float maxDist = cellRayLimit(cell, start, dir, maxLength);
    int         destRoadId = -1;
    const float throughHit =
        rayToThroughRoadHit(town, gap.cellId, start, dir, gap.roadId, maxDist, &destRoadId);
    if (throughHit <= 1.f) {
        out.end = start + dir * maxDist;
        return false;
    }

    if (throughHit < minLength - 1e-3f) {
        out.end = start + dir * throughHit;
        return false;
    }

    const Vec2 end = start + dir * throughHit;
    out.end        = end;
    out.length     = throughHit;

    if (alleySegmentBlocked(start, end, setback, town, defs)) {
        return false;
    }

    if (!alleyCrossingsMeetMinAngle(start, end, town, gap.cellId, minCrossingAngleDeg)) {
        return false;
    }

    if (!alleyEndpointsMeetMinSpacing(start, end, town, gap.cellId, minEndpointSpacing)) {
        return false;
    }

    if (minCreatedArea > 1e-3f) {
        const float createdArea =
            alleyCreatedArea(gap, gapPt, start, end, town, destRoadId);
        if (createdArea + 1e-3f < minCreatedArea) {
            return false;
        }
    }

    if (!alleyMeetsSideRoadClearance(town, gap.cellId, start, end, gap.roadId, destRoadId,
                                     minSideRoadDist, sideRoadSampleCount)) {
        return false;
    }

    out.valid = true;
    return true;
}

void appendProbeLine(const Town& town, const WallGap& gap, float gapT, float angleDeg,
                     float maxLength, float setback, float minCrossingAngleDeg,
                     float minEndpointSpacing, float minCreatedArea, float minSideRoadDist,
                     int sideRoadSampleCount, const DefCache& defs, bool valid,
                     std::vector<AlleyProbeLine>& probeLines) {
    AlleyProbeResult probe;
    probeThroughAlley(town, gap, gapT, angleDeg, 0.f, maxLength, setback, minCrossingAngleDeg,
                      minEndpointSpacing, minCreatedArea, minSideRoadDist, sideRoadSampleCount,
                      defs, probe);

    AlleyProbeLine line;
    line.a     = probe.start;
    line.b     = probe.end;
    line.valid = valid;
    if ((line.b - line.a).length() < 0.5f) {
        const Vec2 gapPt = gap.origin + gap.edgeDir * gapT;
        line.a           = gapPt;
        line.b           = gapPt + gap.inward.normalized() * 8.f;
    }
    if ((line.b - line.a).length() > 0.5f) {
        probeLines.push_back(line);
    }
}

struct CellCandidate {
    int   cellId     = -1;
    float centerDist = 0.f;
    bool  expanding  = false;
};

bool cellHasBlockingPendingFills(const Town& town, int cellId, int failLimit) {
    for (const PendingAlleyFill& pending : town.pendingAlleyFills) {
        if (pending.hostCellId == cellId && pending.consecutiveFillFails < failLimit) {
            return true;
        }
    }
    return false;
}

void markCellsWithNoUncheckedGaps(Town& town, const std::vector<WallGap>& allGaps, int failLimit) {
    for (Cell& cell : town.cells) {
        if (cell.alleyState == AlleyCellState::Finished) {
            continue;
        }
        if (cellHasBlockingPendingFills(town, cell.id, failLimit)) {
            continue;
        }
        if (!cellHasUncheckedPrimaryAlleyGaps(town, cell.id, allGaps)
            && !cellHasUncheckedSecondaryHostAlleyGaps(town, cell.id, allGaps)) {
            cell.alleyState = AlleyCellState::Finished;
        }
    }
}

int pickCellForAlley(Town& town, const std::vector<WallGap>& allGaps, int failLimit) {
    markCellsWithNoUncheckedGaps(town, allGaps, failLimit);

    std::vector<CellCandidate> candidates;
    candidates.reserve(town.cells.size());

    for (const WallGap& gap : allGaps) {
        if (gap.cellId < 0 || gap.cellId >= static_cast<int>(town.cells.size())) {
            continue;
        }
        if (isAlleyGapChecked(town, gap)) {
            continue;
        }

        const Cell& cell = town.cells[static_cast<std::size_t>(gap.cellId)];
        if (cell.alleyState == AlleyCellState::Finished) {
            continue;
        }

        const float centerDist = (cell.centroid - town.center).length();
        const bool  expanding  = cell.alleyState == AlleyCellState::Expanding;
        const auto  existing =
            std::find_if(candidates.begin(), candidates.end(),
                         [gap](const CellCandidate& c) { return c.cellId == gap.cellId; });
        if (existing == candidates.end()) {
            candidates.push_back({gap.cellId, centerDist, expanding});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const CellCandidate& lhs,
                                                         const CellCandidate& rhs) {
        if (lhs.expanding != rhs.expanding) {
            return lhs.expanding > rhs.expanding;
        }
        return lhs.centerDist < rhs.centerDist;
    });

    return candidates.empty() ? -1 : candidates.front().cellId;
}

int pickNextActiveAlleyCell(Town& town, const std::vector<WallGap>& allGaps, int failLimit,
                            int excludeCellId = -1) {
    markCellsWithNoUncheckedGaps(town, allGaps, failLimit);

    auto findBestCell = [&](bool primaryPhase, int excludeCellId) -> int {
        int   bestCellId = -1;
        float bestDist   = 0.f;
        for (const WallGap& gap : allGaps) {
            if (gap.cellId < 0 || gap.cellId >= static_cast<int>(town.cells.size())) {
                continue;
            }
            if (excludeCellId >= 0 && gap.cellId == excludeCellId) {
                continue;
            }
            if (isAlleyGapChecked(town, gap)) {
                continue;
            }
            const bool onSecondary = isSecondaryHostGap(town, gap);
            if (primaryPhase) {
                if (onSecondary) {
                    continue;
                }
            } else if (!onSecondary) {
                continue;
            }
            const Cell& cell = town.cells[static_cast<std::size_t>(gap.cellId)];
            if (cell.alleyState == AlleyCellState::Finished) {
                continue;
            }
            if (cellHasBlockingPendingFills(town, gap.cellId, failLimit)) {
                continue;
            }
            const float dist = (cell.centroid - town.center).length();
            if (bestCellId < 0 || dist < bestDist) {
                bestCellId = gap.cellId;
                bestDist   = dist;
            }
        }
        return bestCellId;
    };

    const int primaryCell = findBestCell(true, excludeCellId);
    if (primaryCell >= 0) {
        return primaryCell;
    }
    return findBestCell(false, excludeCellId);
}

bool pickNextUncheckedGap(const std::vector<WallGap>& allGaps, int cellId, const Town& town,
                          WallGap& out) {
    const bool preferPrimary = cellHasUncheckedPrimaryAlleyGaps(town, cellId, allGaps);

    std::vector<const WallGap*> unchecked;
    unchecked.reserve(allGaps.size());
    for (const WallGap& gap : allGaps) {
        if (gap.cellId != cellId || isAlleyGapChecked(town, gap)) {
            continue;
        }
        const bool onSecondary = isSecondaryHostGap(town, gap);
        if (preferPrimary && onSecondary) {
            continue;
        }
        if (!preferPrimary && !onSecondary) {
            continue;
        }
        unchecked.push_back(&gap);
    }

    if (unchecked.empty()) {
        return false;
    }

    std::sort(unchecked.begin(), unchecked.end(), [&](const WallGap* lhs, const WallGap* rhs) {
        return (lhs->gapMidPoint() - town.center).length()
             < (rhs->gapMidPoint() - town.center).length();
    });

    out = *unchecked.front();
    return true;
}

bool probeWallGap(Town& town, const WallGap& gap, float setback, const TownConfig& townCfg,
                  const DefCache& defs, std::vector<AlleyProbeLine>& probeLines,
                  AlleyProbeResult& bestOut) {
    std::vector<float> gapTs;
    std::vector<float> angles;
    buildGapTPositions(gap, townCfg.alleysPerUnitLength, gapTs);
    buildAngleSamples(townCfg.maxAlleyAngleDeg, townCfg.alleyAngleCount, angles);

    for (const float gapT : gapTs) {
        for (const float angleDeg : angles) {
            AlleyProbeResult probe;
            const bool valid = probeThroughAlley(town, gap, gapT, angleDeg, townCfg.minAlleyLength,
                                                 townCfg.maxAlleyLength, setback,
                                                 townCfg.minAlleyCrossingAngleDeg,
                                                 townCfg.minAlleyEndpointSpacing,
                                                 townCfg.minAlleyCreatedArea,
                                                 townCfg.minAlleySideRoadDist,
                                                 townCfg.alleySideRoadSampleCount, defs, probe);
            appendProbeLine(town, gap, gapT, angleDeg, townCfg.maxAlleyLength, setback,
                            townCfg.minAlleyCrossingAngleDeg, townCfg.minAlleyEndpointSpacing,
                            townCfg.minAlleyCreatedArea, townCfg.minAlleySideRoadDist,
                            townCfg.alleySideRoadSampleCount, defs, valid, probeLines);

            if (valid) {
                bestOut       = probe;
                bestOut.valid = true;
                markAlleyGapChecked(town, gap);
                return true;
            }
        }
    }

    markAlleyGapChecked(town, gap);
    return false;
}

}  // namespace

bool tryAddSecondaryRoad(Town& town, int queueIndex, float setback, const TownConfig& townCfg,
                         const DefCache& defs, PlacementSearchLog& searchLog, int& outRoadId,
                         int forceCellId) {
    outRoadId = -1;
    (void)searchLog;

    const float minGapWidth = std::max(townCfg.minWallGapForAlley, setback * 2.f);
    const int   failLimit   = townCfg.alleyFillFailLimit;

    if (static_cast<int>(town.alleyProbesByQueueIndex.size()) <= queueIndex) {
        town.alleyProbesByQueueIndex.resize(static_cast<std::size_t>(queueIndex + 1));
    }
    std::vector<AlleyProbeLine>& probeLines =
        town.alleyProbesByQueueIndex[static_cast<std::size_t>(queueIndex)];
    probeLines.clear();

    std::vector<WallGap> allGaps;
    collectAllPrimaryWallGaps(town, minGapWidth, allGaps);
    if (allGaps.empty()) {
        Logger::log("layout", "secondary_road_fail: queueIndex=" + std::to_string(queueIndex)
                                  + " reason=no_wall_gaps min_gap=" + std::to_string(minGapWidth));
        return false;
    }

    int cellId = forceCellId;
    if (cellId < 0) {
        cellId = pickCellForAlley(town, allGaps, failLimit);
    } else {
        markCellsWithNoUncheckedGaps(town, allGaps, failLimit);
        if (cellId >= static_cast<int>(town.cells.size())
            || town.cells[static_cast<std::size_t>(cellId)].alleyState == AlleyCellState::Finished
            || cellHasBlockingPendingFills(town, cellId, failLimit)) {
            Logger::log("layout", "secondary_road_fail: queueIndex=" + std::to_string(queueIndex)
                                      + " cell=" + std::to_string(cellId)
                                      + " reason=forced_cell_unavailable");
            return false;
        }
    }

    if (cellId < 0) {
        Logger::log("layout", "secondary_road_fail: queueIndex=" + std::to_string(queueIndex)
                                  + " reason=all_cells_finished");
        return false;
    }

    WallGap gap{};
    if (!pickNextUncheckedGap(allGaps, cellId, town, gap)) {
        if (!cellHasBlockingPendingFills(town, cellId, failLimit)
            && !cellHasUncheckedPrimaryAlleyGaps(town, cellId, allGaps)
            && !cellHasUncheckedSecondaryHostAlleyGaps(town, cellId, allGaps)) {
            town.cells[static_cast<std::size_t>(cellId)].alleyState = AlleyCellState::Finished;
            if (town.activeAlleyCellId == cellId) {
                town.activeAlleyCellId = -1;
            }
        }
        Logger::log("layout", "secondary_road_fail: queueIndex=" + std::to_string(queueIndex)
                                  + " cell=" + std::to_string(cellId)
                                  + " reason=no_unchecked_gaps");
        return false;
    }

    AlleyProbeResult best{};
    const bool       placed =
        probeWallGap(town, gap, setback, townCfg, defs, probeLines, best);

    if (!placed) {
        if (!cellHasUncheckedPrimaryAlleyGaps(town, cellId, allGaps)
            && !cellHasUncheckedSecondaryHostAlleyGaps(town, cellId, allGaps)
            && !cellHasBlockingPendingFills(town, cellId, failLimit)) {
            town.cells[static_cast<std::size_t>(cellId)].alleyState = AlleyCellState::Finished;
            if (town.activeAlleyCellId == cellId) {
                town.activeAlleyCellId = -1;
            }
        }
        Logger::log("layout",
                    "secondary_road_fail: queueIndex=" + std::to_string(queueIndex) + " cell="
                        + std::to_string(cellId) + " gap_id=" + std::to_string(gap.id)
                        + " reason=no_valid_probe probes=" + std::to_string(probeLines.size()));
        return false;
    }

    SecondaryRoadRecord record;
    record.a                 = best.start;
    record.b                 = best.end;
    record.hostCellId        = cellId;
    record.addedAtQueueIndex = queueIndex;
    record.isThrough         = true;
    town.secondaryRoadRecords.push_back(record);

    Road road;
    road.id                = static_cast<int>(town.roads.size());
    road.a                 = record.a;
    road.b                 = record.b;
    road.cellA             = cellId;
    road.cellB             = cellId;
    road.hostCellId        = cellId;
    road.isSecondary       = true;
    road.addedAtQueueIndex = queueIndex;
    town.roads.push_back(road);

    assignSecondaryRoadInwards(town.roads.back(), town);
    buildSecondaryRoadFrontageSegments(town.roads.back(), town, setback);

    town.cells[static_cast<std::size_t>(cellId)].alleyState = AlleyCellState::Expanding;
    town.activeAlleyCellId                                    = cellId;
    outRoadId                                                 = road.id;

    PendingAlleyFill pending;
    pending.addedAtQueueIndex    = queueIndex;
    pending.hostCellId           = cellId;
    pending.consecutiveFillFails = 0;
    town.pendingAlleyFills.push_back(pending);

    Logger::log("layout",
                "secondary_road: queueIndex=" + std::to_string(queueIndex) + " cell="
                    + std::to_string(cellId) + " gap_id=" + std::to_string(gap.id) + " roadId="
                    + std::to_string(outRoadId) + " host="
                    + (isSecondaryHostGap(town, gap) ? "alley" : "primary") + " length="
                    + std::to_string(best.length) + " gap_t=" + std::to_string(best.gapT)
                    + " angle=" + std::to_string(best.angleDeg) + " probes="
                    + std::to_string(probeLines.size()) + " checked_gaps="
                    + std::to_string(town.checkedAlleyGaps.size()));
    Logger::log("layout", "alley_pending: queueIndex=" + std::to_string(queueIndex) + " road="
                              + std::to_string(outRoadId) + " cell=" + std::to_string(cellId)
                              + " fails=0");
    return true;
}

int resolveSecondaryRoadId(const Town& town, int addedAtQueueIndex) {
    for (std::size_t i = 0; i < town.secondaryRoadRecords.size(); ++i) {
        if (town.secondaryRoadRecords[i].addedAtQueueIndex != addedAtQueueIndex) {
            continue;
        }
        return town.primaryRoadCount + static_cast<int>(i);
    }
    return -1;
}

void syncPendingAlleyFills(Town& town, int targetCount) {
    town.pendingAlleyFills.erase(
        std::remove_if(town.pendingAlleyFills.begin(), town.pendingAlleyFills.end(),
                       [targetCount](const PendingAlleyFill& pending) {
                           return pending.addedAtQueueIndex >= targetCount;
                       }),
        town.pendingAlleyFills.end());

    town.pendingAlleyFills.erase(
        std::remove_if(town.pendingAlleyFills.begin(), town.pendingAlleyFills.end(),
                       [&](const PendingAlleyFill& pending) {
                           for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
                               if (rec.addedAtQueueIndex == pending.addedAtQueueIndex) {
                                   return false;
                               }
                           }
                           return true;
                       }),
        town.pendingAlleyFills.end());

    if (town.activeAlleyCellId >= 0
        && town.activeAlleyCellId < static_cast<int>(town.cells.size())) {
        const Cell& cell = town.cells[static_cast<std::size_t>(town.activeAlleyCellId)];
        if (cell.alleyState == AlleyCellState::Finished) {
            bool hasPendingInCell = false;
            for (const PendingAlleyFill& pending : town.pendingAlleyFills) {
                if (pending.hostCellId == town.activeAlleyCellId) {
                    hasPendingInCell = true;
                    break;
                }
            }
            if (!hasPendingInCell) {
                town.activeAlleyCellId = -1;
            }
        }
    } else {
        town.activeAlleyCellId = -1;
    }

    if (town.activeAlleyCellId < 0 && !town.pendingAlleyFills.empty()) {
        town.activeAlleyCellId = town.pendingAlleyFills.front().hostCellId;
    }
}

bool hasBlockingPendingFills(const Town& town, int failLimit) {
    if (town.activeAlleyCellId < 0) {
        return false;
    }
    return cellHasBlockingPendingFills(town, town.activeAlleyCellId, failLimit);
}

int frontPendingAlleyIndex(const Town& town, int failLimit) {
    if (town.activeAlleyCellId < 0) {
        return -1;
    }
    for (int i = 0; i < static_cast<int>(town.pendingAlleyFills.size()); ++i) {
        const PendingAlleyFill& pending =
            town.pendingAlleyFills[static_cast<std::size_t>(i)];
        if (pending.hostCellId == town.activeAlleyCellId
            && pending.consecutiveFillFails < failLimit) {
            return i;
        }
    }
    return -1;
}

int pendingAlleyIndexByQueueIndex(const Town& town, int addedAtQueueIndex) {
    for (int i = 0; i < static_cast<int>(town.pendingAlleyFills.size()); ++i) {
        if (town.pendingAlleyFills[static_cast<std::size_t>(i)].addedAtQueueIndex
            == addedAtQueueIndex) {
            return i;
        }
    }
    return -1;
}

void recordAlleyFillSuccess(Town& town, int pendingIndex) {
    if (pendingIndex < 0 || pendingIndex >= static_cast<int>(town.pendingAlleyFills.size())) {
        return;
    }
    PendingAlleyFill& pending =
        town.pendingAlleyFills[static_cast<std::size_t>(pendingIndex)];
    pending.consecutiveFillFails = 0;
    const int roadId = resolveSecondaryRoadId(town, pending.addedAtQueueIndex);
    Logger::log("layout", "alley_fill_ok: queueIndex=" + std::to_string(pending.addedAtQueueIndex)
                              + " road=" + std::to_string(roadId) + " cell="
                              + std::to_string(pending.hostCellId));
}

void recordAlleyFillFailure(Town& town, int pendingIndex, int failLimit) {
    if (pendingIndex < 0 || pendingIndex >= static_cast<int>(town.pendingAlleyFills.size())) {
        return;
    }
    PendingAlleyFill& pending =
        town.pendingAlleyFills[static_cast<std::size_t>(pendingIndex)];
    ++pending.consecutiveFillFails;
    const int roadId = resolveSecondaryRoadId(town, pending.addedAtQueueIndex);
    Logger::log("layout",
                "alley_pending: queueIndex=" + std::to_string(pending.addedAtQueueIndex) + " road="
                    + std::to_string(roadId) + " cell=" + std::to_string(pending.hostCellId)
                    + " fails=" + std::to_string(pending.consecutiveFillFails));

    if (pending.consecutiveFillFails < failLimit) {
        return;
    }

    Logger::log("layout",
                "alley_fill_exhausted: queueIndex=" + std::to_string(pending.addedAtQueueIndex)
                    + " road=" + std::to_string(roadId) + " cell="
                    + std::to_string(pending.hostCellId) + " fails="
                    + std::to_string(pending.consecutiveFillFails)
                    + " (non-blocking; center-out gap fill continues)");
}

void enqueuePendingAlleyFill(Town& town, int addedAtQueueIndex, int hostCellId) {
    if (pendingAlleyIndexByQueueIndex(town, addedAtQueueIndex) >= 0) {
        return;
    }
    PendingAlleyFill pending;
    pending.addedAtQueueIndex    = addedAtQueueIndex;
    pending.hostCellId           = hostCellId;
    pending.consecutiveFillFails = 0;
    town.pendingAlleyFills.push_back(pending);
    town.activeAlleyCellId = hostCellId;
}

int pickAlternateAlleyCell(Town& town, float setback, const TownConfig& townCfg, int excludeCellId) {
    if (excludeCellId < 0) {
        return -1;
    }

    const float minGapWidth = std::max(townCfg.minWallGapForAlley, setback * 2.f);
    const int   failLimit   = townCfg.alleyFillFailLimit;

    std::vector<WallGap> allGaps;
    collectAllPrimaryWallGaps(town, minGapWidth, allGaps);
    return pickNextActiveAlleyCell(town, allGaps, failLimit, excludeCellId);
}

void ensureActiveAlleyCell(Town& town, float setback, const TownConfig& townCfg) {
    const float minGapWidth = std::max(townCfg.minWallGapForAlley, setback * 2.f);
    const int   failLimit   = townCfg.alleyFillFailLimit;

    std::vector<WallGap> allGaps;
    collectAllPrimaryWallGaps(town, minGapWidth, allGaps);

    if (town.activeAlleyCellId >= 0
        && town.activeAlleyCellId < static_cast<int>(town.cells.size())) {
        if (cellHasBlockingPendingFills(town, town.activeAlleyCellId, failLimit)) {
            Logger::log("layout",
                        "alley_active_cell: cell=" + std::to_string(town.activeAlleyCellId));
            return;
        }
        const Cell& cell = town.cells[static_cast<std::size_t>(town.activeAlleyCellId)];
        if (cell.alleyState != AlleyCellState::Finished
            && (cellHasUncheckedPrimaryAlleyGaps(town, town.activeAlleyCellId, allGaps)
                || cellHasUncheckedSecondaryHostAlleyGaps(town, town.activeAlleyCellId, allGaps))) {
            Logger::log("layout",
                        "alley_active_cell: cell=" + std::to_string(town.activeAlleyCellId));
            return;
        }
    }

    const int nextCell = pickNextActiveAlleyCell(town, allGaps, failLimit);
    town.activeAlleyCellId = nextCell;
    if (nextCell >= 0) {
        Logger::log("layout", "alley_active_cell: cell=" + std::to_string(nextCell));
    }
}

bool removeAlleysThroughSecondaryBuildings(Town& town) {
    const std::size_t before = town.secondaryRoadRecords.size();
    town.secondaryRoadRecords.erase(
        std::remove_if(town.secondaryRoadRecords.begin(), town.secondaryRoadRecords.end(),
                       [&](const SecondaryRoadRecord& rec) {
                           if (!segmentIntersectsSecondaryFootprints(rec.a, rec.b, town)) {
                               return false;
                           }
                           Logger::log("layout",
                                       "alley_removed: queueIndex="
                                           + std::to_string(rec.addedAtQueueIndex) + " cell="
                                           + std::to_string(rec.hostCellId)
                                           + " reason=through_secondary_building");
                           return true;
                       }),
        town.secondaryRoadRecords.end());

    if (town.secondaryRoadRecords.size() == before) {
        return false;
    }

    town.pendingAlleyFills.erase(
        std::remove_if(town.pendingAlleyFills.begin(), town.pendingAlleyFills.end(),
                       [&](const PendingAlleyFill& pending) {
                           for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
                               if (rec.addedAtQueueIndex == pending.addedAtQueueIndex) {
                                   return false;
                               }
                           }
                           return true;
                       }),
        town.pendingAlleyFills.end());

    rebuildSecondaryRoadsFromRecords(town);
    syncAlleyCellStates(town);

    if (town.activeAlleyCellId >= 0) {
        bool hasPendingInActive = false;
        for (const PendingAlleyFill& pending : town.pendingAlleyFills) {
            if (pending.hostCellId == town.activeAlleyCellId) {
                hasPendingInActive = true;
                break;
            }
        }
        if (!hasPendingInActive) {
            town.activeAlleyCellId = -1;
        }
    }
    return true;
}
