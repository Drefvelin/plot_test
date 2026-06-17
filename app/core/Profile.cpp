#include "Profile.h"

#include "Logger.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct ProfileBucket {
    ProfileScopeId id;
    std::uint64_t  nanoseconds = 0;
    std::uint64_t  count       = 0;
};

std::array<std::uint64_t, static_cast<std::size_t>(ProfileScopeId::Count)> g_nanoseconds{};
std::array<std::uint64_t, static_cast<std::size_t>(ProfileScopeId::Count)> g_counts{};

}  // namespace

bool Profile::enabled_ = false;

const char* profileScopeName(ProfileScopeId id) {
    switch (id) {
    case ProfileScopeId::TownBuild:
        return "TownBuild";
    case ProfileScopeId::PlacerSync:
        return "PlacerSync";
    case ProfileScopeId::SecondaryRebuild:
        return "SecondaryRebuild";
    case ProfileScopeId::FrontageCarve:
        return "FrontageCarve";
    case ProfileScopeId::GrowthLoop:
        return "GrowthLoop";
    case ProfileScopeId::JunctionHops:
        return "JunctionHops";
    case ProfileScopeId::PlacePlot:
        return "PlacePlot";
    case ProfileScopeId::PlaceGapFill:
        return "PlaceGapFill";
    case ProfileScopeId::AlleyProbe:
        return "AlleyProbe";
    case ProfileScopeId::MeshRebuild:
        return "MeshRebuild";
    case ProfileScopeId::TerrainBake:
        return "TerrainBake";
    case ProfileScopeId::SyncAlleyCleanup:
        return "SyncAlleyCleanup";
    case ProfileScopeId::PlacementPrep:
        return "PlacementPrep";
    case ProfileScopeId::RingBump:
        return "RingBump";
    case ProfileScopeId::GapFillCollect:
        return "GapFillCollect";
    case ProfileScopeId::GapFillTrySlot:
        return "GapFillTrySlot";
    case ProfileScopeId::PlotTrySegment:
        return "PlotTrySegment";
    case ProfileScopeId::PlotLayout:
        return "PlotLayout";
    case ProfileScopeId::FrontageWallCarve:
        return "FrontageWallCarve";
    case ProfileScopeId::MeshOutline:
        return "MeshOutline";
    case ProfileScopeId::MeshFrontageSeg:
        return "MeshFrontageSeg";
    case ProfileScopeId::MeshAlleyProbe:
        return "MeshAlleyProbe";
    case ProfileScopeId::MeshRoad:
        return "MeshRoad";
    case ProfileScopeId::MeshHopDebug:
        return "MeshHopDebug";
    case ProfileScopeId::MeshJunction:
        return "MeshJunction";
    default:
        return "Unknown";
    }
}

bool Profile::enabled() { return enabled_; }

void Profile::setEnabled(bool enabled) { enabled_ = enabled; }

void Profile::reset() {
    g_nanoseconds.fill(0);
    g_counts.fill(0);
}

void Profile::addSample(ProfileScopeId id, std::uint64_t nanoseconds) {
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= g_nanoseconds.size()) {
        return;
    }
    g_nanoseconds[index] += nanoseconds;
    ++g_counts[index];
}

void Profile::report() {
    if (!enabled_) {
        return;
    }

    std::uint64_t totalNs = 0;
    for (std::size_t i = 0; i < g_nanoseconds.size(); ++i) {
        totalNs += g_nanoseconds[i];
    }

    std::vector<ProfileBucket> buckets;
    buckets.reserve(g_nanoseconds.size());
    for (std::size_t i = 0; i < g_nanoseconds.size(); ++i) {
        if (g_nanoseconds[i] == 0) {
            continue;
        }
        buckets.push_back({static_cast<ProfileScopeId>(i), g_nanoseconds[i], g_counts[i]});
    }

    std::sort(buckets.begin(), buckets.end(),
              [](const ProfileBucket& a, const ProfileBucket& b) {
                  return a.nanoseconds > b.nanoseconds;
              });

    const double totalMs = static_cast<double>(totalNs) / 1'000'000.0;
    {
        const std::string header =
            "profile_summary: total_ms=" + std::to_string(totalMs) + " scopes="
            + std::to_string(buckets.size());
        std::cout << header << std::endl;
        Logger::log("profile", header);
    }

    for (const ProfileBucket& bucket : buckets) {
        const double ms  = static_cast<double>(bucket.nanoseconds) / 1'000'000.0;
        const double pct = totalNs > 0
                               ? (static_cast<double>(bucket.nanoseconds) * 100.0
                                  / static_cast<double>(totalNs))
                               : 0.0;
        const std::string line = "profile: " + std::string(profileScopeName(bucket.id))
                                 + " ms=" + std::to_string(ms) + " calls="
                                 + std::to_string(bucket.count) + " pct="
                                 + std::to_string(pct) + "%";
        std::cout << line << std::endl;
        Logger::log("profile", line);
    }

    Logger::flush();
}

ProfileScope::ProfileScope(ProfileScopeId id) : id_(id) {
    if (!Profile::enabled()) {
        return;
    }
    active_ = true;
    start_  = std::chrono::steady_clock::now();
}

ProfileScope::~ProfileScope() {
    if (!active_) {
        return;
    }
    const auto end = std::chrono::steady_clock::now();
    const auto ns  = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count());
    Profile::addSample(id_, ns);
}
