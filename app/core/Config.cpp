#include "Config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::array<uint8_t, 3> readColor(const YAML::Node& node,
                                 const std::array<uint8_t, 3>& fallback) {
    if (node && node.IsSequence() && node.size() >= 3) {
        return {node[0].as<uint8_t>(), node[1].as<uint8_t>(), node[2].as<uint8_t>()};
    }
    return fallback;
}

}  // namespace

int VoronoiConfig::siteCount() const {
    constexpr int kBaseSitesAtScale1 = 200;
    return std::max(3, static_cast<int>(std::round(kBaseSitesAtScale1 * scale)));
}

std::filesystem::path Config::resolveProjectRoot(const std::filesystem::path& exeDir) {
    auto dir = exeDir;
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(dir / "app" / "config" / "config.yml")) {
            return dir;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) {
            break;
        }
        dir = dir.parent_path();
    }
    return exeDir;
}

std::filesystem::path Config::resolveConfigPath() {
    const auto exeDir = std::filesystem::path([]() {
#ifdef _WIN32
        wchar_t buffer[MAX_PATH];
        const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();
#else
        return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
    }());

    const auto besideExe = exeDir / "config" / "config.yml";
    if (std::filesystem::exists(besideExe)) {
        return besideExe;
    }

    const auto projectRoot = resolveProjectRoot(exeDir);
    const auto inApp = projectRoot / "app" / "config" / "config.yml";
    if (std::filesystem::exists(inApp)) {
        return inApp;
    }

    return besideExe;
}

Config Config::load(const std::filesystem::path& path) {
    Config config;
    config.sourcePath = path;

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const std::exception& e) {
        std::cerr << "[Config] Could not load '" << path.string() << "': " << e.what()
                  << "\n[Config] Using defaults.\n";
        return config;
    }

    if (auto w = root["window"]) {
        if (w["width"]) config.window.width = w["width"].as<int>();
        if (w["height"]) config.window.height = w["height"].as<int>();
        if (w["title"]) config.window.title = w["title"].as<std::string>();
    }

    if (auto d = root["diagram"]) {
        if (d["width"]) config.diagram.width = d["width"].as<float>();
        if (d["height"]) config.diagram.height = d["height"].as<float>();
        if (d["radius"]) config.diagram.radius = d["radius"].as<float>();
    }

    if (auto w = root["world"]) {
        if (w["pixels_per_unit"]) config.world.pixelsPerUnit = w["pixels_per_unit"].as<float>();
    } else if (auto d = root["diagram"]) {
        // Legacy alias
        if (d["render_scale"]) config.world.pixelsPerUnit = d["render_scale"].as<float>();
    }

    if (auto p = root["plots"]) {
        if (p["min_area"]) config.plots.minArea = p["min_area"].as<float>();
        if (p["max_area"]) config.plots.maxArea = p["max_area"].as<float>();
        if (p["max_depth_to_front_ratio"]) {
            config.plots.maxDepthToFrontRatio = p["max_depth_to_front_ratio"].as<float>();
        }
        if (p["frontage_setback"]) {
            config.plots.frontageSetback = p["frontage_setback"].as<float>();
        }
        if (p["terrain_anchor_max_roads"]) {
            config.plots.terrainAnchorMaxRoads = p["terrain_anchor_max_roads"].as<int>();
        }
    }

    if (auto v = root["voronoi"]) {
        if (v["scale"]) {
            config.voronoi.scale = v["scale"].as<float>();
        } else if (v["site_count"]) {
            // Legacy: treat absolute site_count as scale relative to 200 sites at 1.0
            config.voronoi.scale = v["site_count"].as<float>() / 200.f;
        }
        if (v["seed"]) config.voronoi.seed = v["seed"].as<int>();
    }

    if (auto t = root["town"]) {
        if (t["seed"]) config.town.seed = t["seed"].as<int>();
    }

    if (auto c = root["colors"]) {
        config.colors.inside = readColor(c["inside"], config.colors.inside);
        config.colors.outside = readColor(c["outside"], config.colors.outside);
        config.colors.edges = readColor(c["edges"], config.colors.edges);
        config.colors.secondaryEdges =
            readColor(c["secondary_edges"], config.colors.secondaryEdges);
        config.colors.bridge = readColor(c["bridge"], config.colors.bridge);
    }

    if (auto d = root["debug"]) {
        config.debug.highlightColor =
            readColor(d["highlight_color"], config.debug.highlightColor);
    }

    if (auto t = root["terrain"]) {
        if (t["image"]) {
            config.terrain.imagePath = t["image"].as<std::string>();
        }
        if (t["colors"]) {
            config.terrain.colorsPath = t["colors"].as<std::string>();
        }
        if (t["grid_size"]) {
            config.terrain.gridSize = t["grid_size"].as<int>();
        }
        if (t["simplify_tolerance"]) {
            config.terrain.simplifyTolerance = t["simplify_tolerance"].as<float>();
        }
        if (t["water_inset"]) {
            config.terrain.waterInset = t["water_inset"].as<float>();
        }
        if (t["shore_inset"]) {
            config.terrain.shoreInset = t["shore_inset"].as<float>();
        }
        if (t["river_inset"]) {
            config.terrain.riverInset = t["river_inset"].as<float>();
        } else if (t["shore_inset"]) {
            config.terrain.riverInset = config.terrain.shoreInset;
        }
        if (t["contour_width"]) {
            config.terrain.contourWidth = t["contour_width"].as<float>();
        }
        if (t["clip_roads_at_water"]) {
            config.terrain.clipRoadsAtWater = t["clip_roads_at_water"].as<bool>();
        }
        if (t["corridor_roads_enabled"]) {
            config.terrain.corridorRoadsEnabled = t["corridor_roads_enabled"].as<bool>();
        }
        if (t["shore_road_inset"]) {
            config.terrain.shoreRoadInset = t["shore_road_inset"].as<float>();
        }
        if (t["river_road_inset"]) {
            config.terrain.riverRoadInset = t["river_road_inset"].as<float>();
        }
        if (t["corridor_edge_spacing"]) {
            config.terrain.corridorEdgeSpacing = t["corridor_edge_spacing"].as<float>();
        }
        if (t["corridor_parallel_probe_offset"]) {
            config.terrain.corridorParallelProbeOffset =
                t["corridor_parallel_probe_offset"].as<float>();
        }
        if (t["corridor_parallel_cos"]) {
            config.terrain.corridorParallelCos = t["corridor_parallel_cos"].as<float>();
        }
        if (t["bridges_enabled"]) {
            config.terrain.bridgesEnabled = t["bridges_enabled"].as<bool>();
        }
        if (t["bridge_snap_enabled"]) {
            config.terrain.bridgeSnapEnabled = t["bridge_snap_enabled"].as<bool>();
        }
        if (t["bridge_snap_search_radius"]) {
            config.terrain.bridgeSnapSearchRadius = t["bridge_snap_search_radius"].as<float>();
        }
        if (t["bridge_max_span"]) {
            config.terrain.bridgeMaxSpan = t["bridge_max_span"].as<float>();
        }
        if (t["shore_junction_merge_dist"]) {
            config.terrain.shoreJunctionMergeDist = t["shore_junction_merge_dist"].as<float>();
        }
        if (t["bridge_waterside_max_dist"]) {
            config.terrain.bridgeWatersideMaxDist = t["bridge_waterside_max_dist"].as<float>();
        }
        if (t["bridge_bucket_hops"]) {
            config.terrain.bridgeBucketHops = std::max(0, t["bridge_bucket_hops"].as<int>());
        }
    }

    if (auto l = root["logging"]) {
        if (l["directory"]) config.logging.directory = l["directory"].as<std::string>();
        if (l["flush_interval_ms"]) {
            config.logging.flushIntervalMs = std::max(0, l["flush_interval_ms"].as<int>());
        }
        if (auto files = l["files"]) {
            for (const auto& entry : files) {
                LogFileConfig file;
                if (entry["channel"]) file.channel = entry["channel"].as<std::string>();
                if (entry["filename"]) file.filename = entry["filename"].as<std::string>();
                if (!file.channel.empty() && !file.filename.empty()) {
                    config.logging.files.push_back(file);
                }
            }
        }
    }

    if (auto g = root["growth"]) {
        if (g["auto_grow"]) {
            config.growth.autoGrow = std::max(0, g["auto_grow"].as<int>());
        }
        if (g["auto_grow_ms"]) {
            config.growth.autoGrowMs = std::max(0, g["auto_grow_ms"].as<int>());
        }
        if (g["auto_exit"]) {
            config.growth.autoExit = g["auto_exit"].as<bool>();
        }
        if (g["profile"]) {
            config.growth.profile = g["profile"].as<bool>();
        }
        if (g["verbose_placement_logs"]) {
            config.growth.verbosePlacementLogs = g["verbose_placement_logs"].as<bool>();
        }
    }

    return config;
}
