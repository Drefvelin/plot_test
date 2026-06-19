#pragma once

#include <chrono>
#include <cstdint>

enum class ProfileScopeId : std::uint8_t {
    TownBuild,
    PlacerSync,
    SecondaryRebuild,
    FrontageCarve,
    GrowthLoop,
    JunctionHops,
    PlacePlot,
    PlaceGapFill,
    AlleyProbe,
    MeshRebuild,
    TerrainBake,
    // PlacerSync breakdown
    SyncAlleyCleanup,
    PlacementPrep,
    RingBump,
    MovableRelocate,
    // PlaceGapFill / PlacePlot breakdown
    GapFillCollect,
    GapFillTrySlot,
    PlotTrySegment,
    PlotLayout,
    FrontageWallCarve,
    // MeshRebuild breakdown
    MeshOutline,
    MeshFrontageSeg,
    MeshAlleyProbe,
    MeshRoad,
    MeshHopDebug,
    MeshJunction,
    TerrainBorderPlace,
    BorderCandidateLayout,
    TerrainScanPeek,
    TerrainScanTrySlot,
    RebuildPlacementFrontier,
    RebuildTerrainScanFrontier,
    RebuildBorderFrontier,
    Count,
};

const char* profileScopeName(ProfileScopeId id);

class Profile {
public:
    static bool enabled();
    static void setEnabled(bool enabled);
    static void reset();
    static void report();

    static void addSample(ProfileScopeId id, std::uint64_t nanoseconds);

private:
    static bool enabled_;
};

class ProfileScope {
public:
    explicit ProfileScope(ProfileScopeId id);
    ~ProfileScope();

    ProfileScope(const ProfileScope&)            = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    ProfileScopeId                           id_;
    bool                                     active_ = false;
    std::chrono::steady_clock::time_point    start_{};
};

#define PROFILE_SCOPE(id) ProfileScope _profile_scope_##__LINE__(id)
