#include "SecondaryRoadPlacement.h"

#include "FrontageZones.h"
#include "PlacementFrontier.h"
#include "TerrainAtlas.h"

#include "Logger.h"
#include "PlotGeometry.h"
#include "Profile.h"
#include "RoadExhaustion.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kMaxAlleyProbesPerIndex = 2000;

void recordAlleyProbe(Town& town, int queueIndex, const AlleyProbeLine& line) {
    if (queueIndex < 0 || queueIndex >= static_cast<int>(town.alleyProbesByQueueIndex.size())) {
        return;
    }
    std::vector<AlleyProbeLine>& probes =
        town.alleyProbesByQueueIndex[static_cast<std::size_t>(queueIndex)];
    if (probes.size() >= kMaxAlleyProbesPerIndex) {
        return;
    }
    probes.push_back(line);
}

constexpr float kPi = 3.14159265358979323846f;
constexpr float kJunctionSnapEps = 0.08f;
constexpr float kThroughRayEps   = 0.1f;
constexpr float kWaterStopInset  = 0.15f;

Vec2 rotateVec(const Vec2& v, float degrees) {
    const float rad = degrees * kPi / 180.f;
    const float c   = std::cos(rad);
    const float s   = std::sin(rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

void buildGapTPositions(const WallGap& gap, float alleysPerUnitLength, std::vector<float>& out) {
    out.clear();
    const int probeCount =
        std::max(1, static_cast<int>(std::ceil(gap.width() * std::max(0.f, alleysPerUnitLength))));
    for (int i = 0; i < probeCount; ++i) {
        const float u = probeCount == 1 ? 0.5f : static_cast<float>(i) / (probeCount - 1);
        out.push_back(gap.tMin + u * gap.width());
    }
}

void buildAngleSamples(float maxAngleDeg, int angleCount, std::vector<float>& out) {
    out.clear();
    if (angleCount <= 1) {
        out.push_back(0.f);
        return;
    }
    for (int i = 0; i < angleCount; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(angleCount - 1);
        out.push_back(-maxAngleDeg + u * (2.f * maxAngleDeg));
    }
}

void buildStubTurnAngleSamples(int angleCount, std::vector<float>& out) {
    out.clear();
    const int count = std::max(19, angleCount * 6);
    if (count <= 1) {
        out.push_back(0.f);
        return;
    }
    for (int i = 0; i < count; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(count - 1);
        out.push_back(-90.f + u * 180.f);
    }
}

void shuffleProbeOrder(std::vector<float>& angles, std::vector<float>& gapTs, int townSeed,
                       int queueIndex, const WallGap& gap) {
    std::seed_seq angleSeed{townSeed, queueIndex, gap.roadId, gap.bankIndex};
    std::mt19937  angleRng(angleSeed);
    std::shuffle(angles.begin(), angles.end(), angleRng);

    std::seed_seq gapSeed{townSeed, queueIndex, gap.roadId, gap.bankIndex, 911};
    std::mt19937  gapRng(gapSeed);
    std::shuffle(gapTs.begin(), gapTs.end(), gapRng);
}

struct RayRoadHit {
    int   roadId = -1;
    float dist   = -1.f;
    Vec2  point{};
};

bool rayToRoadHitExcluding(const Town& town, const Vec2& start, const Vec2& dir, int excludeRoadId,
                           float maxDist, RayRoadHit& out);

struct AlleyProbeViz {
    Vec2 start{};
    Vec2 end{};
    bool valid = false;
};

void fillProbeViz(AlleyProbeViz* outViz, const Vec2& start, const Vec2& end) {
    if (outViz == nullptr) {
        return;
    }
    outViz->start = start;
    outViz->end   = end;
    outViz->valid = true;
}

Vec2 probeVizStart(const AlleyProbeCandidate& probe, const Vec2& firstStart) {
    return probe.segments.empty() ? firstStart : probe.segments.front().start;
}

struct AlleyProbeStats {
    int probes            = 0;
    int badDir            = 0;
    int noRayHit          = 0;
    int tooShort          = 0;
    int tooLong           = 0;
    int blockedByMain     = 0;
    int setbackFail       = 0;
    int chainExhausted    = 0;
    int thinSide          = 0;
    int badAngle          = 0;
    int endpointSpacing   = 0;
    int bankParallel      = 0;
    int depthStubFail     = 0;
    int turnFail          = 0;
    int waterStop         = 0;
};

void appendAlleyProbeStatSummary(const AlleyProbeStats& stats, std::string& out) {
    out += " probes=" + std::to_string(stats.probes);
    if (stats.badDir > 0) {
        out += " bad_dir=" + std::to_string(stats.badDir);
    }
    if (stats.noRayHit > 0) {
        out += " no_ray_hit=" + std::to_string(stats.noRayHit);
    }
    if (stats.tooShort > 0) {
        out += " too_short=" + std::to_string(stats.tooShort);
    }
    if (stats.tooLong > 0) {
        out += " too_long=" + std::to_string(stats.tooLong);
    }
    if (stats.blockedByMain > 0) {
        out += " blocked_by_main=" + std::to_string(stats.blockedByMain);
    }
    if (stats.setbackFail > 0) {
        out += " setback=" + std::to_string(stats.setbackFail);
    }
    if (stats.chainExhausted > 0) {
        out += " chain_exhausted=" + std::to_string(stats.chainExhausted);
    }
    if (stats.thinSide > 0) {
        out += " thin_side=" + std::to_string(stats.thinSide);
    }
    if (stats.badAngle > 0) {
        out += " bad_angle=" + std::to_string(stats.badAngle);
    }
    if (stats.endpointSpacing > 0) {
        out += " endpoint_spacing=" + std::to_string(stats.endpointSpacing);
    }
    if (stats.bankParallel > 0) {
        out += " bank_parallel=" + std::to_string(stats.bankParallel);
    }
    if (stats.depthStubFail > 0) {
        out += " depth_stub_fail=" + std::to_string(stats.depthStubFail);
    }
    if (stats.turnFail > 0) {
        out += " turn_fail=" + std::to_string(stats.turnFail);
    }
    if (stats.waterStop > 0) {
        out += " water_stop=" + std::to_string(stats.waterStop);
    }
}

bool clipSegmentEndAtWater(const TerrainAtlas& terrain, const Vec2& a, const Vec2& b, Vec2& outEnd,
                           float& outLen) {
    const Vec2  delta = b - a;
    const float len   = delta.length();
    if (len < 1e-4f) {
        outEnd = b;
        outLen = len;
        return false;
    }
    if (!terrain.isBuildable(a)) {
        outEnd = b;
        outLen = len;
        return false;
    }

    constexpr float kStep = 0.25f;
    bool            prevLand = true;
    float           prevT    = 0.f;
    for (float dist = kStep; dist <= len + 1e-3f; dist += kStep) {
        const float t    = std::min(dist / len, 1.f);
        const bool  land = terrain.isBuildable(a + delta * t);
        if (prevLand && !land) {
            const float crossT = (prevT + t) * 0.5f;
            outLen             = std::max(0.f, crossT * len - kWaterStopInset);
            outEnd             = a + delta * (outLen / len);
            return true;
        }
        prevLand = land;
        prevT    = t;
    }

    outEnd = b;
    outLen = len;
    return false;
}

enum class AlleyRayTerminus { None, Road, Water };

struct AlleyRayHit {
    Vec2             point{};
    float            dist = -1.f;
    int              roadId = -1;
    AlleyRayTerminus terminus = AlleyRayTerminus::None;
};

bool resolveAlleyRayHit(const Town& town, const TerrainAtlas* terrain, const Vec2& start,
                        const Vec2& dir, int excludeRoadId, float maxDist, AlleyRayHit& out) {
    out = {};

    RayRoadHit roadHit;
    const bool hasRoad =
        rayToRoadHitExcluding(town, start, dir, excludeRoadId, maxDist, roadHit);

    float waterDist = -1.f;
    if (terrain != nullptr && terrain->valid && terrain->isBuildable(start)) {
        Vec2        clippedEnd{};
        float       clippedLen = 0.f;
        const Vec2  rayEnd     = start + dir * maxDist;
        if (clipSegmentEndAtWater(*terrain, start, rayEnd, clippedEnd, clippedLen)
            && clippedLen >= 1.f) {
            waterDist = clippedLen;
        }
    }

    const bool waterWins =
        waterDist >= 1.f && (!hasRoad || waterDist + 1e-3f < roadHit.dist);
    if (waterWins) {
        out.point    = start + dir * waterDist;
        out.dist     = waterDist;
        out.roadId   = -1;
        out.terminus = AlleyRayTerminus::Water;
        return true;
    }

    if (hasRoad) {
        out.point    = roadHit.point;
        out.dist     = roadHit.dist;
        out.roadId   = roadHit.roadId;
        out.terminus = AlleyRayTerminus::Road;
        return true;
    }

    return false;
}

void recordQualityReject(AlleyProbeStats* stats, AlleyQualityReject reject) {
    if (stats == nullptr) {
        return;
    }
    switch (reject) {
    case AlleyQualityReject::ThinSide:
        ++stats->thinSide;
        break;
    case AlleyQualityReject::BadAngle:
        ++stats->badAngle;
        break;
    case AlleyQualityReject::EndpointSpacing:
        ++stats->endpointSpacing;
        break;
    case AlleyQualityReject::BankParallel:
        ++stats->bankParallel;
        break;
    default:
        break;
    }
}

bool rayToRoadHitExcluding(const Town& town, const Vec2& start, const Vec2& dir, int excludeRoadId,
                           float maxDist, RayRoadHit& out) {
    RayRoadHit best;
    const Vec2 rayEnd = start + dir * maxDist;
    for (const Road& road : town.roads) {
        if (road.id == excludeRoadId) {
            continue;
        }
        float tRay = 0.f;
        float uSeg = 0.f;
        if (!segmentCrossingParams(start, rayEnd, road.a, road.b, tRay, uSeg)) {
            continue;
        }
        const float hitDist = tRay * maxDist;
        if (hitDist < 1.f || hitDist > maxDist + 1e-3f) {
            continue;
        }
        if (best.dist >= 0.f && hitDist >= best.dist - 1e-4f) {
            continue;
        }

        Vec2 hitPoint = road.a + (road.b - road.a) * uSeg;
        if ((hitPoint - road.a).length() <= kJunctionSnapEps) {
            hitPoint = road.a;
        } else if ((hitPoint - road.b).length() <= kJunctionSnapEps) {
            hitPoint = road.b;
        }

        best.roadId = road.id;
        best.dist   = hitDist;
        best.point  = hitPoint;
    }
    if (best.roadId < 0) {
        return false;
    }
    out = best;
    return true;
}

struct CachedTurnCandidate {
    AlleyProbeCandidate probe;
    float             turnAngleDeg = 0.f;
    float             score        = -1.f;
    bool              perfect      = false;
};

float scoreTurnCandidate(const Town& town, const AlleyProbeCandidate& probe, float turnAngleDeg,
                         bool perfect) {
    if (probe.segments.size() < 2) {
        return -1.f;
    }
    const AlleySegmentCandidate& terminal = probe.segments.back();
    if (terminal.destRoadId < 0) {
        return -1.f;
    }

    float score = alleyCrossingAngleDeg(town, terminal.start, terminal.end, terminal.destRoadId);
    if (terminal.destRoadId < static_cast<int>(town.roads.size())
        && !town.roads[static_cast<std::size_t>(terminal.destRoadId)].isSecondary) {
        score += 200.f;
    }
    score += (terminal.end - terminal.start).length() * 0.05f;
    if (perfect) {
        score += 500.f;
    }
    score += std::abs(std::abs(turnAngleDeg) - 90.f) * 0.15f;
    return score;
}

bool trySimulateTurnAngle(Town& town, const WallGap& gap, float minLength, float maxLength,
                          float setback, float minSideRoadDist, const DefCache& defs,
                          const TerrainAtlas* terrain, const AlleyProbeCandidate& stubProbe,
                          float turnAngle, AlleyProbeCandidate& outProbe) {
    if (stubProbe.segments.empty()) {
        return false;
    }

    const AlleySegmentCandidate& leg1 = stubProbe.segments.front();
    const Vec2                   gapPoint  = leg1.start;
    const Vec2                   turnPoint = leg1.end;
    const Vec2                   leg1Dir   = (turnPoint - gapPoint).normalized();
    if (leg1Dir.length() < 1e-4f) {
        return false;
    }

    outProbe              = stubProbe;
    outProbe.turnAngleDeg = turnAngle;

    const float spawnInset    = std::max(setback, minSideRoadDist);
    const float roadHalfWidth = spawnInset * 0.5f;
    const float maxDist       = maxLength > 1e-3f ? maxLength : 200.f;
    const Vec2  dir           = rotateVec(leg1Dir, turnAngle).normalized();
    if (dir.length() < 1e-4f) {
        return false;
    }

    Vec2  start         = turnPoint + dir * kThroughRayEps;
    int   excludeRoadId = gap.roadId;
    float totalLen      = 0.f;

    for (int chain = 0; chain < 8; ++chain) {
        const float remaining = maxDist - totalLen;
        AlleyRayHit hit;
        if (!resolveAlleyRayHit(town, terrain, start, dir, excludeRoadId, remaining, hit)) {
            return false;
        }

        const float segmentLen = hit.dist;
        if (segmentLen < minLength) {
            return false;
        }
        if (segmentLen + totalLen > maxDist + 1e-3f) {
            return false;
        }
        if (segmentsCrossInInterior(gapPoint, turnPoint, start, hit.point)) {
            return false;
        }
        if (alleySegmentBlockedByMain(start, hit.point, roadHalfWidth, town, defs)) {
            return false;
        }
        collectAuxiliaryDemolitionsForAlley(start, hit.point, roadHalfWidth, town,
                                            outProbe.demolitions);
        if (!segmentClearsRoadSetback(start, hit.point, town, gap.roadId, spawnInset,
                                      hit.roadId)) {
            return false;
        }

        const Vec2 segmentStart = outProbe.segments.size() == 1 ? turnPoint : start;
        outProbe.segments.push_back({segmentStart, hit.point, hit.roadId});

        if (hit.terminus == AlleyRayTerminus::Water
            || hit.roadId < 0 || hit.roadId >= static_cast<int>(town.roads.size())
            || !town.roads[static_cast<std::size_t>(hit.roadId)].isSecondary) {
            return true;
        }

        totalLen += segmentLen;
        start         = hit.point + dir * kThroughRayEps;
        excludeRoadId = hit.roadId;
    }

    return false;
}

bool selectBestTurnFromStub(Town& town, const WallGap& gap, float gapT, float angleDeg,
                            float minLength, float maxLength, float setback,
                            const TownConfig& townCfg, const DefCache& defs,
                            const TerrainAtlas* terrain, const AlleyProbeCandidate& stubProbe,
                            AlleyProbeCandidate& outProbe, float& outTurnAngleDeg, bool& outPerfect,
                            AlleyProbeStats* stats, int townSeed, int queueIndex,
                            AlleyProbeViz* outViz = nullptr) {
    outPerfect = false;
    if (stubProbe.segments.empty()) {
        return false;
    }

    const Vec2 gapPoint = stubProbe.segments.front().start;

    std::vector<float> turnAngles;
    buildStubTurnAngleSamples(townCfg.alleyAngleCount, turnAngles);

    const int gapTKey  = static_cast<int>(gapT * 1000.f);
    const int angleKey = static_cast<int>(angleDeg * 10.f);
    std::seed_seq turnSeed{townSeed, queueIndex, gap.roadId, gap.bankIndex, gapTKey, angleKey,
                           7331};
    std::mt19937 turnRng(turnSeed);
    std::shuffle(turnAngles.begin(), turnAngles.end(), turnRng);

    const float spawnInset = std::max(setback, townCfg.minAlleySideRoadDist);

    CachedTurnCandidate bestPerfect;
    CachedTurnCandidate bestGood;
    bestPerfect.score = -1.f;
    bestGood.score    = -1.f;

    for (float turnAngle : turnAngles) {
        AlleyProbeCandidate attempt;
        if (!trySimulateTurnAngle(town, gap, minLength, maxLength, setback, spawnInset, defs,
                                  terrain, stubProbe, turnAngle, attempt)) {
            continue;
        }

        AlleyQualityReject reject = AlleyQualityReject::None;
        const bool         perfect =
            validateAlleyProbe(town, attempt, townCfg, spawnInset, &reject);
        const float score = scoreTurnCandidate(town, attempt, turnAngle, perfect);

        CachedTurnCandidate* bucket = perfect ? &bestPerfect : &bestGood;
        if (score > bucket->score) {
            bucket->probe        = std::move(attempt);
            bucket->turnAngleDeg = turnAngle;
            bucket->score        = score;
            bucket->perfect      = perfect;
        }
    }

    const CachedTurnCandidate* chosen =
        bestPerfect.score >= 0.f ? &bestPerfect
                                 : (bestGood.score >= 0.f ? &bestGood : nullptr);
    if (chosen == nullptr) {
        if (stats != nullptr) {
            ++stats->turnFail;
        }
        fillProbeViz(outViz, gapPoint, stubProbe.segments.front().end);
        return false;
    }

    outProbe        = chosen->probe;
    outTurnAngleDeg = chosen->turnAngleDeg;
    outPerfect      = chosen->perfect;
    fillProbeViz(outViz, gapPoint, outProbe.segments.back().end);
    return true;
}

bool simulateProbeThroughAlley(const Town& town, const WallGap& gap, float gapT, float angleDeg,
                               float minLength, float maxLength, float setback,
                               float minSideRoadDist, const DefCache& defs,
                               const TerrainAtlas* terrain, AlleyProbeCandidate& outProbe,
                               AlleyProbeStats* stats = nullptr, AlleyProbeViz* outViz = nullptr) {
    if (stats != nullptr) {
        ++stats->probes;
    }

    outProbe.gap            = gap;
    outProbe.segments.clear();
    outProbe.demolitions.clear();
    outProbe.probeAngleDeg  = angleDeg;

    const float spawnInset  = std::max(setback, minSideRoadDist);
    const float roadHalfWidth = spawnInset * 0.5f;

    const Vec2 gapPoint   = gap.origin + gap.edgeDir * gapT;
    const Vec2 firstStart = gapPoint + gap.inward * spawnInset;
    Vec2       start      = firstStart;
    Vec2       dir        = rotateVec(gap.inward, angleDeg).normalized();
    outProbe.probeDir     = dir;
    if (dir.length() < 1e-4f) {
        if (stats != nullptr) {
            ++stats->badDir;
        }
        const float maxDist = maxLength > 1e-3f ? maxLength : 200.f;
        fillProbeViz(outViz, firstStart, firstStart + gap.inward.normalized() * maxDist);
        return false;
    }

    const float maxDist       = maxLength > 1e-3f ? maxLength : 200.f;
    int         excludeRoadId = gap.roadId;
    float       totalLen      = 0.f;

    for (int chain = 0; chain < 8; ++chain) {
        const float remaining = maxDist - totalLen;
        AlleyRayHit hit;
        if (!resolveAlleyRayHit(town, terrain, start, dir, excludeRoadId, remaining, hit)) {
            if (stats != nullptr) {
                ++stats->noRayHit;
            }
            fillProbeViz(outViz, probeVizStart(outProbe, firstStart),
                         start + dir * std::max(remaining, 1.f));
            return false;
        }

        const float segmentLen = hit.dist;
        if (segmentLen < minLength) {
            if (stats != nullptr) {
                ++stats->tooShort;
            }
            fillProbeViz(outViz, probeVizStart(outProbe, gapPoint), hit.point);
            return false;
        }
        if (segmentLen + totalLen > maxDist + 1e-3f) {
            if (stats != nullptr) {
                ++stats->tooLong;
            }
            fillProbeViz(outViz, probeVizStart(outProbe, gapPoint),
                         start + dir * std::max(remaining, 1.f));
            return false;
        }
        if (alleySegmentBlockedByMain(start, hit.point, roadHalfWidth, town, defs)) {
            if (stats != nullptr) {
                ++stats->blockedByMain;
            }
            fillProbeViz(outViz, probeVizStart(outProbe, gapPoint), hit.point);
            return false;
        }
        collectAuxiliaryDemolitionsForAlley(start, hit.point, roadHalfWidth, town,
                                            outProbe.demolitions);
        if (!segmentClearsRoadSetback(start, hit.point, town, gap.roadId, spawnInset,
                                      hit.roadId)) {
            if (stats != nullptr) {
                ++stats->setbackFail;
            }
            fillProbeViz(outViz, probeVizStart(outProbe, gapPoint), hit.point);
            return false;
        }

        if (hit.terminus == AlleyRayTerminus::Water && stats != nullptr) {
            ++stats->waterStop;
        }

        const Vec2 segmentStart = outProbe.segments.empty() ? gapPoint : start;
        outProbe.segments.push_back({segmentStart, hit.point, hit.roadId});

        if (hit.terminus == AlleyRayTerminus::Water
            || hit.roadId < 0 || hit.roadId >= static_cast<int>(town.roads.size())
            || !town.roads[static_cast<std::size_t>(hit.roadId)].isSecondary) {
            return true;
        }

        totalLen += segmentLen;
        start         = hit.point + dir * kThroughRayEps;
        excludeRoadId = hit.roadId;
    }

    if (stats != nullptr) {
        ++stats->chainExhausted;
    }
    if (!outProbe.segments.empty()) {
        fillProbeViz(outViz, outProbe.segments.front().start, outProbe.segments.back().end);
    } else {
        fillProbeViz(outViz, firstStart, start + dir * std::max(maxDist, 1.f));
    }
    outProbe.segments.clear();
    outProbe.demolitions.clear();
    return false;
}

bool simulateDepthCapStub(Town& town, const WallGap& gap, float gapT, float angleDeg, float minLength,
                            float setback, float minSideRoadDist, const DefCache& defs,
                            const TerrainAtlas* terrain, AlleyProbeCandidate& outProbe,
                            AlleyProbeStats* stats = nullptr, AlleyProbeViz* outViz = nullptr) {
    outProbe.gap           = gap;
    outProbe.segments.clear();
    outProbe.demolitions.clear();
    outProbe.probeAngleDeg = angleDeg;
    outProbe.placementKind = AlleyPlacementKind::DeadEnd;
    outProbe.turnAngleDeg  = 0.f;

    const float spawnInset    = std::max(setback, minSideRoadDist);
    const float roadHalfWidth = spawnInset * 0.5f;
    const Vec2  gapPoint      = gap.origin + gap.edgeDir * gapT;
    const Vec2  inward        = gap.inward.normalized();
    outProbe.probeDir         = inward;
    if (inward.length() < 1e-4f) {
        if (stats != nullptr) {
            ++stats->depthStubFail;
        }
        return false;
    }

    const float frontage = std::max(gap.width(), 1.f);
    const float depthCap =
        maxPlotDepthToRoadHit(gapPoint, gap.edgeDir, frontage, gap.inward, setback, gap.roadId,
                              gap.bankIndex, town);
    if (depthCap < minLength || depthCap <= 1e-3f) {
        if (stats != nullptr) {
            ++stats->depthStubFail;
        }
        fillProbeViz(outViz, gapPoint, gapPoint + inward * std::max(depthCap, minLength));
        return false;
    }

    Vec2  turnPoint = gapPoint + inward * depthCap;
    float stubLen   = depthCap;
    if (terrain != nullptr && terrain->valid) {
        clipSegmentEndAtWater(*terrain, gapPoint, turnPoint, turnPoint, stubLen);
    }
    if (stubLen < minLength || stubLen <= 1e-3f) {
        if (stats != nullptr) {
            ++stats->depthStubFail;
        }
        fillProbeViz(outViz, gapPoint, gapPoint + inward * std::max(depthCap, minLength));
        return false;
    }

    if (alleySegmentBlockedByMain(gapPoint, turnPoint, roadHalfWidth, town, defs)) {
        if (stats != nullptr) {
            ++stats->blockedByMain;
        }
        fillProbeViz(outViz, gapPoint, turnPoint);
        return false;
    }
    collectAuxiliaryDemolitionsForAlley(gapPoint, turnPoint, roadHalfWidth, town,
                                        outProbe.demolitions);
    if (!segmentClearsRoadSetback(gapPoint, turnPoint, town, gap.roadId, spawnInset, -1)) {
        if (stats != nullptr) {
            ++stats->setbackFail;
        }
        fillProbeViz(outViz, gapPoint, turnPoint);
        return false;
    }

    outProbe.segments.push_back({gapPoint, turnPoint, -1});
    if (terrain != nullptr && terrain->valid && stubLen < depthCap - 1e-3f && stats != nullptr) {
        ++stats->waterStop;
    }
    return true;
}

bool applyAlleyProbe(Town& town, const AlleyProbeCandidate& probe, int queueIndex,
                     std::vector<SecondaryRoadRecord>& outRecords) {
    outRecords.clear();
    if (probe.segments.empty()) {
        return false;
    }

    const bool straightThrough = probe.placementKind == AlleyPlacementKind::Straight
                                 && probe.segments.size() > 1;
    const bool isTurn = probe.placementKind == AlleyPlacementKind::Turn;

    for (std::size_t i = 0; i < probe.segments.size(); ++i) {
        const AlleySegmentCandidate& segment = probe.segments[i];
        SecondaryRoadRecord          rec;
        rec.a             = segment.start;
        rec.b             = segment.end;
        rec.probeAngleDeg = probe.probeAngleDeg;
        rec.hostRoadId    = probe.gap.roadId;
        rec.hostBankIndex = probe.gap.bankIndex;

        if (isTurn && i > 0) {
            rec.hostRoadId    = -1;
            rec.hostBankIndex = -1;
            rec.kind          = AlleyPlacementKind::Turn;
            rec.turnAngleDeg  = probe.turnAngleDeg;
            rec.isThrough     = probe.segments.size() > 2;
        } else if (probe.placementKind == AlleyPlacementKind::DeadEnd) {
            rec.kind      = AlleyPlacementKind::DeadEnd;
            rec.isThrough = false;
        } else {
            rec.kind      = AlleyPlacementKind::Straight;
            rec.isThrough = straightThrough;
        }

        outRecords.push_back(rec);
    }

    if (!probe.demolitions.empty()) {
        applyAuxiliaryDemolitions(town, probe.demolitions);
        std::string labels;
        for (const AuxiliaryDemolition& demo : probe.demolitions) {
            if (!labels.empty()) {
                labels += ",";
            }
            labels += std::to_string(demo.footprintLabelId);
        }
        Logger::log("layout",
                    "alley_demolish: queueIndex=" + std::to_string(queueIndex) + " instance="
                        + std::to_string(probe.demolitions.front().instanceId) + " labels="
                        + labels + " count=" + std::to_string(probe.demolitions.size()));
    }

    return true;
}

bool probeThroughAlley(Town& town, const WallGap& gap, float gapT, float angleDeg, float minLength,
                       float maxLength, float setback, const TownConfig& townCfg,
                       const DefCache& defs, const TerrainAtlas* terrain, int queueIndex,
                       std::vector<SecondaryRoadRecord>& outRecords,
                       AlleyProbeStats* stats = nullptr, AlleyProbeViz* outViz = nullptr) {
    AlleyProbeCandidate probe;
    if (!simulateProbeThroughAlley(town, gap, gapT, angleDeg, minLength, maxLength, setback,
                                   townCfg.minAlleySideRoadDist, defs, terrain, probe, stats,
                                   outViz)) {
        return false;
    }

    AlleyQualityReject reject = AlleyQualityReject::None;
    const float spawnInset = std::max(setback, townCfg.minAlleySideRoadDist);
    if (!validateAlleyProbe(town, probe, townCfg, spawnInset, &reject)) {
        recordQualityReject(stats, reject);
        if (outViz != nullptr && !probe.segments.empty()) {
            fillProbeViz(outViz, probe.segments.front().start, probe.segments.back().end);
        }
        return false;
    }

    return applyAlleyProbe(town, probe, queueIndex, outRecords);
}

bool probeAlleyWithFallback(Town& town, const WallGap& gap, float gapT, float angleDeg,
                            float minLength, float maxLength, float setback,
                            const TownConfig& townCfg, const DefCache& defs,
                            const TerrainAtlas* terrain, int queueIndex, int townSeed,
                            std::vector<SecondaryRoadRecord>& outRecords,
                            AlleyProbeStats* stats = nullptr, AlleyProbeViz* outViz = nullptr) {
    if (probeThroughAlley(town, gap, gapT, angleDeg, minLength, maxLength, setback, townCfg,
                          defs, terrain, queueIndex, outRecords, stats, outViz)) {
        return true;
    }

    AlleyProbeCandidate stubProbe;
    if (!simulateDepthCapStub(town, gap, gapT, angleDeg, minLength, setback,
                              townCfg.minAlleySideRoadDist, defs, terrain, stubProbe, stats,
                              outViz)) {
        return false;
    }

    const float spawnInset = std::max(setback, townCfg.minAlleySideRoadDist);
    AlleyQualityReject reject = AlleyQualityReject::None;
    if (!validateAlleyProbe(town, stubProbe, townCfg, spawnInset, &reject)) {
        recordQualityReject(stats, reject);
        if (outViz != nullptr && !stubProbe.segments.empty()) {
            fillProbeViz(outViz, stubProbe.segments.front().start, stubProbe.segments.back().end);
        }
        return false;
    }

    AlleyProbeCandidate turnProbe = stubProbe;
    float               turnAngleDeg = 0.f;
    bool                turnPerfect  = false;
    if (selectBestTurnFromStub(town, gap, gapT, angleDeg, minLength, maxLength, setback, townCfg,
                               defs, terrain, stubProbe, turnProbe, turnAngleDeg, turnPerfect,
                               stats, townSeed, queueIndex, outViz)) {
        turnProbe.placementKind = AlleyPlacementKind::Turn;
        turnProbe.turnAngleDeg  = turnAngleDeg;
        if (!turnPerfect) {
            Logger::log("layout",
                        "alley_turn_partial: queueIndex=" + std::to_string(queueIndex)
                            + " turn_angle=" + std::to_string(turnAngleDeg));
        }
        return applyAlleyProbe(town, turnProbe, queueIndex, outRecords);
    }

    return applyAlleyProbe(town, stubProbe, queueIndex, outRecords);
}

struct GapCandidate {
    WallGap gap;
    float   centerDist = 0.f;
};

std::vector<GapCandidate> collectOrderedRoadGaps(Town& town, float minGapWidth, int forceRoadId,
                                                 float maxDistInclusive) {
    std::vector<GapCandidate> candidates;
    candidates.reserve(town.frontiers.alley.size());
    for (const AlleyFrontierRef& ref : town.frontiers.alley) {
        if (ref.centerDist > maxDistInclusive + 1e-3f) {
            continue;
        }
        if (forceRoadId >= 0 && ref.roadId != forceRoadId) {
            continue;
        }
        if (ref.tMax - ref.tMin + 1e-3f < minGapWidth) {
            continue;
        }
        if (ref.roadId < 0 || ref.roadId >= static_cast<int>(town.roads.size())) {
            continue;
        }
        const Road&             road = town.roads[static_cast<std::size_t>(ref.roadId)];
        const RoadSideFrontage* side = road.sideBank(ref.bankIndex);
        if (side == nullptr) {
            continue;
        }
        for (const RoadFrontageSegment& segment : side->wallSegments) {
            if (segment.id != ref.segmentId) {
                continue;
            }
            const WallGap gap = wallGapFromSegment(road, ref.bankIndex, segment);
            if (isAlleyGapChecked(town, gap)) {
                continue;
            }
            if (!bankHasBuildingOnSide(town, gap.roadId, gap.bankIndex)) {
                continue;
            }
            candidates.push_back({gap, ref.centerDist});
            break;
        }
    }
    return candidates;
}

}  // namespace

int resolveSecondaryRoadId(const Town& town, int addedAtQueueIndex) {
    for (const Road& road : town.roads) {
        if (road.isSecondary && road.addedAtQueueIndex == addedAtQueueIndex) {
            return road.id;
        }
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
}

bool hasBlockingPendingFills(const Town& town, int failLimit) {
    return std::any_of(town.pendingAlleyFills.begin(), town.pendingAlleyFills.end(),
                       [failLimit](const PendingAlleyFill& pending) {
                           return pending.consecutiveFillFails < failLimit;
                       });
}

int frontPendingAlleyIndex(const Town& town, int failLimit) {
    for (std::size_t i = 0; i < town.pendingAlleyFills.size(); ++i) {
        if (town.pendingAlleyFills[i].consecutiveFillFails < failLimit) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int pendingAlleyIndexByQueueIndex(const Town& town, int addedAtQueueIndex) {
    for (std::size_t i = 0; i < town.pendingAlleyFills.size(); ++i) {
        if (town.pendingAlleyFills[i].addedAtQueueIndex == addedAtQueueIndex) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void recordAlleyFillSuccess(Town& town, int pendingIndex) {
    if (pendingIndex < 0 || pendingIndex >= static_cast<int>(town.pendingAlleyFills.size())) {
        return;
    }
    town.pendingAlleyFills.erase(town.pendingAlleyFills.begin() + pendingIndex);
}

void recordAlleyFillFailure(Town& town, int pendingIndex, int failLimit) {
    if (pendingIndex < 0 || pendingIndex >= static_cast<int>(town.pendingAlleyFills.size())) {
        return;
    }
    PendingAlleyFill& pending = town.pendingAlleyFills[static_cast<std::size_t>(pendingIndex)];
    ++pending.consecutiveFillFails;
    if (pending.consecutiveFillFails >= failLimit) {
        town.pendingAlleyFills.erase(town.pendingAlleyFills.begin() + pendingIndex);
    }
}

void enqueuePendingAlleyFill(Town& town, int addedAtQueueIndex, int hostRoadId) {
    if (pendingAlleyIndexByQueueIndex(town, addedAtQueueIndex) >= 0) {
        return;
    }
    town.pendingAlleyFills.push_back({addedAtQueueIndex, hostRoadId, 0});
}

bool tryAddSecondaryRoad(Town& town, int queueIndex, float setback, const TownConfig& townCfg,
                         const DefCache& defs, PlacementSearchLog& /*searchLog*/, int& outRoadId,
                         int forceRoadId, const std::vector<int>& /*junctionHops*/, int townSeed,
                         const TerrainAtlas* terrain) {
    PROFILE_SCOPE(ProfileScopeId::AlleyProbe);
    outRoadId = -1;

    if (town.urbanCoreMaxHop < 0) {
        return false;
    }

    const float coreMaxDist = urbanCoreMaxDist(town);

    std::vector<WallGap> allWallGaps;
    collectWallGapsInDistRange(town, townCfg.minWallGapForAlley, coreMaxDist, allWallGaps);
    int checkedSkipped = 0;
    for (const WallGap& gap : allWallGaps) {
        if (isAlleyGapChecked(town, gap)) {
            ++checkedSkipped;
        }
    }

    int undevelopedSkipped = 0;
    for (const WallGap& gap : allWallGaps) {
        if (isAlleyGapChecked(town, gap)) {
            continue;
        }
        if (!bankHasBuildingOnSide(town, gap.roadId, gap.bankIndex)) {
            ++undevelopedSkipped;
        }
    }

    const std::vector<GapCandidate> gaps =
        collectOrderedRoadGaps(town, townCfg.minWallGapForAlley, forceRoadId, coreMaxDist);
    std::vector<float> angleSamples;
    buildAngleSamples(townCfg.maxAlleyAngleDeg, townCfg.alleyAngleCount, angleSamples);

    Logger::log("layout",
                "alley_diag: begin queueIndex=" + std::to_string(queueIndex) + " force_road="
                    + std::to_string(forceRoadId) + " wall_gaps=" + std::to_string(allWallGaps.size())
                    + " candidates=" + std::to_string(gaps.size()) + " checked_skipped="
                    + std::to_string(checkedSkipped) + " undeveloped_bank_skipped="
                    + std::to_string(undevelopedSkipped) + " min_gap="
                    + std::to_string(townCfg.minWallGapForAlley) + " min_len="
                    + std::to_string(townCfg.minAlleyLength) + " max_len="
                    + std::to_string(townCfg.maxAlleyLength) + " angles="
                    + std::to_string(angleSamples.size()));

    if (gaps.empty()) {
        Logger::log("layout",
                    "alley_diag: fail queueIndex=" + std::to_string(queueIndex)
                        + " reason=no_candidates wall_gaps=" + std::to_string(allWallGaps.size())
                        + " checked_skipped=" + std::to_string(checkedSkipped));
        return false;
    }

    AlleyProbeStats totalStats;
    int             gapsTried     = 0;
    int             gapsMarkedDead = 0;

    for (const GapCandidate& candidate : gaps) {
        const WallGap& gap = candidate.gap;
        ++gapsTried;
        std::vector<float> gapTs;
        buildGapTPositions(gap, townCfg.alleysPerUnitLength, gapTs);

        std::vector<float> shuffledAngles = angleSamples;
        std::vector<float> shuffledGapTs  = gapTs;
        shuffleProbeOrder(shuffledAngles, shuffledGapTs, townSeed, queueIndex, gap);

        bool            anyProbeValid = false;
        AlleyProbeStats gapStats;

        for (float gapT : shuffledGapTs) {
            for (float angle : shuffledAngles) {
                std::vector<SecondaryRoadRecord> records;
                AlleyProbeViz                    failedViz;
                if (!probeAlleyWithFallback(town, gap, gapT, angle, townCfg.minAlleyLength,
                                            townCfg.maxAlleyLength, setback, townCfg, defs, terrain,
                                            queueIndex, townSeed, records, &gapStats, &failedViz)) {
                    if (queueIndex >= 0 && failedViz.valid) {
                        recordAlleyProbe(town, queueIndex,
                                         {failedViz.start, failedViz.end, false});
                    }
                    continue;
                }

                for (SecondaryRoadRecord& rec : records) {
                    rec.addedAtQueueIndex = queueIndex;
                    town.secondaryRoadRecords.push_back(rec);
                    applySecondaryRoadRecord(town, rec, terrain);
                }

                outRoadId = -1;
                for (const Road& road : town.roads) {
                    if (road.isSecondary && road.addedAtQueueIndex == queueIndex) {
                        outRoadId = road.id;
                    }
                }

                enqueuePendingAlleyFill(town, queueIndex, gap.roadId);
                markAlleyGapChecked(town, gap);
                anyProbeValid = true;
                clearExhaustionAfterAlleyApply(town, gap.roadId, outRoadId);

                if (queueIndex >= 0) {
                    recordAlleyProbe(town, queueIndex, {records.front().a, records.back().b, true});
                }

                std::string alleyLog =
                    "alley_added: queueIndex=" + std::to_string(queueIndex) + " host_road="
                    + std::to_string(gap.roadId) + " bank=" + std::to_string(gap.bankIndex)
                    + " gap_width=" + std::to_string(gap.width()) + " center_dist="
                    + std::to_string(candidate.centerDist) + " segments="
                    + std::to_string(records.size());
                bool loggedThrough = false;
                for (const SecondaryRoadRecord& rec : records) {
                    if (rec.kind == AlleyPlacementKind::Turn) {
                        alleyLog += " turn=1 turn_angle=" + std::to_string(rec.turnAngleDeg);
                        break;
                    }
                    if (rec.kind == AlleyPlacementKind::DeadEnd) {
                        alleyLog += " dead_end=1";
                        break;
                    }
                    if (rec.isThrough && !loggedThrough) {
                        alleyLog += " through=1";
                        loggedThrough = true;
                    }
                }
                Logger::log("layout", alleyLog);
                if (loggedThrough) {
                    Logger::log("layout",
                                "alley_through: queueIndex=" + std::to_string(queueIndex)
                                    + " segments=" + std::to_string(records.size()));
                }
                return outRoadId >= 0;
            }
        }

        totalStats.probes += gapStats.probes;
        totalStats.badDir += gapStats.badDir;
        totalStats.noRayHit += gapStats.noRayHit;
        totalStats.tooShort += gapStats.tooShort;
        totalStats.tooLong += gapStats.tooLong;
        totalStats.blockedByMain += gapStats.blockedByMain;
        totalStats.setbackFail += gapStats.setbackFail;
        totalStats.chainExhausted += gapStats.chainExhausted;
        totalStats.thinSide += gapStats.thinSide;
        totalStats.badAngle += gapStats.badAngle;
        totalStats.endpointSpacing += gapStats.endpointSpacing;
        totalStats.bankParallel += gapStats.bankParallel;
        totalStats.depthStubFail += gapStats.depthStubFail;
        totalStats.turnFail += gapStats.turnFail;

        if (!anyProbeValid) {
            markAlleyGapChecked(town, gap);
            if (!bankHasUncheckedAlleyGaps(town, gap.roadId, gap.bankIndex,
                                           townCfg.minWallGapForAlley, coreMaxDist)) {
                if (RoadSideFrontage* side =
                        town.roads[static_cast<std::size_t>(gap.roadId)].sideBank(gap.bankIndex)) {
                    setBankExhausted(*side, AlleyDone);
                }
            }
            ++gapsMarkedDead;
            std::string failDetail;
            appendAlleyProbeStatSummary(gapStats, failDetail);
            Logger::log("layout",
                        "alley_diag: gap_exhausted queueIndex=" + std::to_string(queueIndex)
                            + " road=" + std::to_string(gap.roadId) + " bank="
                            + std::to_string(gap.bankIndex) + " width="
                            + std::to_string(gap.width()) + " center_dist="
                            + std::to_string(candidate.centerDist) + failDetail);
        }
    }

    std::string summary;
    appendAlleyProbeStatSummary(totalStats, summary);
    Logger::log("layout",
                "alley_diag: fail queueIndex=" + std::to_string(queueIndex) + " gaps_tried="
                    + std::to_string(gapsTried) + " gaps_marked_dead="
                    + std::to_string(gapsMarkedDead) + summary);

    return false;
}

bool removeSecondaryRecordsBlockedByMainFootprint(Town& town, const BuildingFootprint& footprint,
                                                  int hostRoadId, const TerrainAtlas* terrain) {
    constexpr float kAlleySetback = 0.5f;
    const std::size_t             before = town.secondaryRoadRecords.size();
    town.secondaryRoadRecords.erase(
        std::remove_if(town.secondaryRoadRecords.begin(), town.secondaryRoadRecords.end(),
                       [&](const SecondaryRoadRecord& rec) {
                           return segmentIntersectsFootprint(rec.a, rec.b, footprint)
                                  || footprintOverlapsAlleys(footprint, town, kAlleySetback,
                                                             hostRoadId);
                       }),
        town.secondaryRoadRecords.end());
    if (town.secondaryRoadRecords.size() == before) {
        return false;
    }

    Logger::log("layout",
                "alley_records_removed: host_road=" + std::to_string(hostRoadId) + " removed="
                    + std::to_string(before - town.secondaryRoadRecords.size()));
    town.checkedAlleyGaps.clear();
    rebuildSecondaryRoadsFromRecords(town, terrain);
    return true;
}

bool removeAlleysThroughSecondaryBuildings(Town& town) {
    PROFILE_SCOPE(ProfileScopeId::SyncAlleyCleanup);
    bool removed = false;
    for (const BuildingInstance& instance : town.buildingInstances) {
        if (instance.placementMode != BuildingPlacementMode::SegmentGapFill
            || instance.footprints.empty()) {
            continue;
        }
        removed = removeSecondaryRecordsBlockedByMainFootprint(town, instance.footprints[0],
                                                               instance.roadId, nullptr)
                  || removed;
    }
    return removed;
}
