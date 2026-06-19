#include "config/DefCache.h"

#include "config/Config.h"
#include "config/YamlUtil.h"

#include <algorithm>
#include <yaml-cpp/yaml.h>

#include <iostream>
#include <string>

namespace {

std::array<uint8_t, 3> readRgb(const YAML::Node& node, const std::array<uint8_t, 3>& fallback) {
    return yamlutil::readRgb(node, fallback);
}

TerrainPlacementMode parseTerrainPlacement(const std::string& s) {
    if (s == "inside") {
        return TerrainPlacementMode::Inside;
    }
    if (s == "proximity") {
        return TerrainPlacementMode::Proximity;
    }
    if (s == "border") {
        return TerrainPlacementMode::Border;
    }
    return TerrainPlacementMode::None;
}

TerrainRequirement parseTerrainRequirement(const std::string& s) {
    if (s == "strict") {
        return TerrainRequirement::Strict;
    }
    return TerrainRequirement::Loose;
}

BorderStyle parseBorderStyle(const std::string& s) {
    if (s == "hug") {
        return BorderStyle::Hug;
    }
    return BorderStyle::Band;
}

void appendPreferKind(const TerrainCatalog& catalog, std::vector<TerrainId>& out, TerrainId kind) {
    if (kind == kTerrainUnknown) {
        return;
    }
    catalog.appendPreferId(out, kind);
}

void parsePreferToken(const TerrainCatalog& catalog, const std::string& token,
                      std::vector<TerrainId>& out) {
    const std::vector<TerrainId> expanded = catalog.expandPreferToken(token);
    for (TerrainId id : expanded) {
        appendPreferKind(catalog, out, id);
    }
}

void parsePreferNode(const TerrainCatalog& catalog, const YAML::Node& node,
                     std::vector<TerrainId>& out) {
    if (!node) {
        return;
    }
    if (node.IsSequence()) {
        for (const YAML::Node& item : node) {
            parsePreferToken(catalog, item.as<std::string>(), out);
        }
        return;
    }
    parsePreferToken(catalog, node.as<std::string>(), out);
}

BuildingTerrainRules parseBuildingTerrain(const TerrainCatalog& catalog, const YAML::Node& node) {
    BuildingTerrainRules rules;
    if (!node || !node.IsMap()) {
        return rules;
    }
    if (node["prefer"]) {
        parsePreferNode(catalog, node["prefer"], rules.preferKinds);
        if (!rules.preferKinds.empty()) {
            rules.prefer = rules.preferKinds.front();
        }
    }
    if (node["placement"]) {
        rules.placement = parseTerrainPlacement(node["placement"].as<std::string>());
    }
    if (node["requirement"]) {
        rules.requirement = parseTerrainRequirement(node["requirement"].as<std::string>());
    }
    if (node["proximity_max_dist"]) {
        rules.proximityMaxDist = node["proximity_max_dist"].as<float>();
    }
    if (node["border_min_dist"]) {
        rules.borderMinDist = node["border_min_dist"].as<float>();
    }
    if (node["border_max_dist"]) {
        rules.borderMaxDist = node["border_max_dist"].as<float>();
    }
    if (node["border_style"]) {
        rules.borderStyle = parseBorderStyle(node["border_style"].as<std::string>());
    }
    if (node["border_overhang_dist"]) {
        rules.borderOverhangDist = node["border_overhang_dist"].as<float>();
    }
    return rules;
}

CountRange parseCountRange(const YAML::Node& node) {
    CountRange range;
    if (!node) {
        return range;
    }

    if (node.IsMap()) {
        if (node["min"]) {
            range.minCount = node["min"].as<int>();
        }
        if (node["max"]) {
            range.maxCount = node["max"].as<int>();
        } else if (node["min"]) {
            range.maxCount = range.minCount;
        }
        return range;
    }

    if (node.IsSequence() && node.size() >= 2) {
        range.minCount = node[0].as<int>();
        range.maxCount = node[1].as<int>();
        return range;
    }

    if (node.IsScalar()) {
        try {
            range.minCount = range.maxCount = node.as<int>();
            return range;
        } catch (...) {
        }

        const std::string text = node.as<std::string>();
        const auto        dash = text.find('-');
        if (dash != std::string::npos) {
            range.minCount = std::stoi(text.substr(0, dash));
            range.maxCount = std::stoi(text.substr(dash + 1));
        } else {
            range.minCount = range.maxCount = std::stoi(text);
        }
    }

    return range;
}

void loadBuildingsOnPlot(const YAML::Node& node, BuildingDef& def) {
    if (!node || !node.IsMap()) {
        return;
    }

    for (const auto& entry : node) {
        const std::string sizeName = entry.first.as<std::string>();
        const CountRange  range    = parseCountRange(entry.second);
        if (range.isValid()) {
            def.buildingsOnPlot[sizeName] = range;
        }
    }
}

void loadSizeBands(const YAML::Node& node, std::unordered_map<std::string, SizeBand>& out) {
    if (!node) {
        return;
    }
    for (const auto& entry : node) {
        const std::string name = entry.first.as<std::string>();
        SizeBand          band;
        band.name = name;
        if (entry.second["min_area"]) {
            band.minArea = entry.second["min_area"].as<float>();
        }
        if (entry.second["max_area"]) {
            band.maxArea = entry.second["max_area"].as<float>();
        }
        out[name] = band;
    }
}

BuildingTemplateRules parseTemplateRules(const YAML::Node& node) {
    BuildingTemplateRules rules;
    if (!node || !node.IsSequence()) {
        return rules;
    }
    for (const auto& cond : node) {
        const std::string name = cond.as<std::string>();
        if (name == "long_facing_middle") {
            rules.longFacingMiddle = true;
        } else if (name == "door_long") {
            rules.doorLong = true;
        } else if (name == "door_short") {
            rules.doorShort = true;
        } else if (name == "edge_placement") {
            rules.edgePlacement = true;
        } else if (name == "middle_placement") {
            rules.middlePlacement = true;
        } else if (name == "corner_placement") {
            rules.cornerPlacement = true;
        } else if (name == "back_edge_placement") {
            rules.backEdgePlacement = true;
        } else if (name == "diagonal_allowed") {
            rules.diagonalAllowed = true;
        }
    }
    return rules;
}

void loadBuildingTemplates(const YAML::Node& node,
                           std::unordered_map<std::string, BuildingTemplate>& templates,
                           std::unordered_map<std::string, SizeBand>& sizes) {
    if (!node) {
        return;
    }
    for (const auto& entry : node) {
        BuildingTemplate tmpl;
        tmpl.name = entry.first.as<std::string>();
        if (entry.second["size"]) {
            const YAML::Node& sizeNode = entry.second["size"];
            tmpl.size.name = tmpl.name;
            if (sizeNode["min_area"]) {
                tmpl.size.minArea = sizeNode["min_area"].as<float>();
            }
            if (sizeNode["max_area"]) {
                tmpl.size.maxArea = sizeNode["max_area"].as<float>();
            }
        }
        tmpl.rules = parseTemplateRules(entry.second["conditions"]);
        templates[tmpl.name] = tmpl;
        sizes[tmpl.name]     = tmpl.size;
    }
}

void finalizeBuildingDef(BuildingDef& def, const std::unordered_map<std::string, SizeBand>& plotSizes) {
    if (!def.sizeCategory.empty() || def.buildingsOnPlot.empty()) {
        return;
    }

    float bestMax = -1.f;
    for (const auto& [sizeName, _] : def.buildingsOnPlot) {
        const auto it = plotSizes.find(sizeName);
        if (it != plotSizes.end() && it->second.maxArea > bestMax) {
            bestMax            = it->second.maxArea;
            def.sizeCategory = sizeName;
        }
    }
}

}  // namespace

DefCache DefCache::load(const std::filesystem::path& path, const TerrainCatalog& catalog) {
    DefCache cache(path, catalog);
    cache.terrainProbes_ = buildTerrainProbeConfig(catalog, cache);
    return cache;
}

std::filesystem::path DefCache::resolveBuildingsPath() {
    const auto configPath = Config::resolveConfigPath();
    const auto besideConfig = configPath.parent_path() / "buildings.yml";
    if (std::filesystem::exists(besideConfig)) {
        return besideConfig;
    }

    const auto projectRoot = Config::resolveProjectRoot(yamlutil::executableDir());
    return projectRoot / "app" / "config" / "buildings.yml";
}

DefCache::DefCache(const std::filesystem::path& path, const TerrainCatalog& catalog) {
    YAML::Node root;
    if (!yamlutil::loadYamlFile(path, "DefCache", root)) {
        return;
    }

    loadBuildingTemplates(root["building_templates"], buildingTemplates_, buildingSizes_);
    loadSizeBands(root["building_sizes"], buildingSizes_);
    loadSizeBands(root["plot_sizes"], plotSizes_);
    if (plotSizes_.empty()) {
        loadSizeBands(root["sizes"], plotSizes_);
    }
    if (buildingSizes_.empty() && !plotSizes_.empty()) {
        for (const auto& [name, plotBand] : plotSizes_) {
            SizeBand buildingBand;
            buildingBand.name    = name;
            buildingBand.minArea = plotBand.minArea * 0.2f;
            buildingBand.maxArea = plotBand.maxArea * 0.2f;
            buildingSizes_[name] = buildingBand;
        }
    }

    if (auto buildings = root["buildings"]) {
        for (const auto& entry : buildings) {
            const std::string id = entry.first.as<std::string>();
            BuildingDef def;
            def.name = id;

            const YAML::Node& node = entry.second;
            if (node.IsScalar()) {
                def.sizeCategory = node.as<std::string>();
                def.buildingsOnPlot[def.sizeCategory] = CountRange{1, 1};
            } else {
                if (node["size"]) {
                    def.sizeCategory = node["size"].as<std::string>();
                    if (def.buildingsOnPlot.empty()) {
                        def.buildingsOnPlot[def.sizeCategory] = CountRange{1, 1};
                    }
                }
                if (node["type"]) {
                    def.type = node["type"].as<std::string>();
                }
                if (node["fill_in"]) {
                    def.fillIn = node["fill_in"].as<bool>();
                } else if (node["fill"]) {
                    def.fillIn = node["fill"].as<bool>();
                }
                if (node["allow_plot_fill"]) {
                    def.allowPlotFill = node["allow_plot_fill"].as<bool>();
                }
                if (node["movable"]) {
                    def.movable = node["movable"].as<bool>();
                }
                if (node["terrain"]) {
                    def.terrain = parseBuildingTerrain(catalog, node["terrain"]);
                }
                def.rgb = readRgb(node["rgb"], def.rgb);
                loadBuildingsOnPlot(node["buildings"], def);
            }

            finalizeBuildingDef(def, plotSizes_);
            buildings_[id] = def;
        }
    }
    assignTypeIds();
}

TerrainProbeConfig buildTerrainProbeConfig(const TerrainCatalog& catalog, const DefCache& defs) {
    TerrainProbeConfig probe{};
    std::vector<TerrainId> border;
    std::vector<TerrainId> scan;

    for (const auto& [_, def] : defs.buildings()) {
        const TerrainPlacementMode mode = def.terrain.placement;
        if (mode == TerrainPlacementMode::None || def.terrain.preferKinds.empty()) {
            continue;
        }
        for (TerrainId id : def.terrain.preferKinds) {
            if (mode == TerrainPlacementMode::Border) {
                catalog.appendPreferId(border, id);
            } else {
                catalog.appendPreferId(scan, id);
            }
        }
    }
    std::vector<TerrainId> scanInside;
    for (const auto& [_, def] : defs.buildings()) {
        if (def.terrain.placement != TerrainPlacementMode::Inside) {
            continue;
        }
        for (TerrainId id : def.terrain.preferKinds) {
            catalog.appendPreferId(scanInside, id);
        }
    }

    probe.borderIds      = std::move(border);
    probe.scanIds        = std::move(scan);
    probe.scanInsideIds  = std::move(scanInside);

    float maxLongSide = 0.f;
    for (const auto& [_, def] : defs.buildings()) {
        if (def.terrain.placement != TerrainPlacementMode::Border) {
            continue;
        }
        for (const auto& [category, _] : def.buildingsOnPlot) {
            const SizeBand* band = defs.buildingSizeBand(category);
            if (band != nullptr) {
                const float side = std::sqrt(band->maxArea * 2.f);
                maxLongSide      = std::max(maxLongSide, side);
            }
        }
    }
    if (maxLongSide > 1e-3f) {
        probe.minBorderFrontage = std::max(6.f, maxLongSide);
    }
    return probe;
}

const SizeBand* DefCache::buildingSizeBand(const std::string& category) const {
    const auto tmplIt = buildingTemplates_.find(category);
    if (tmplIt != buildingTemplates_.end()) {
        return &tmplIt->second.size;
    }
    const auto it = buildingSizes_.find(category);
    if (it == buildingSizes_.end()) {
        return nullptr;
    }
    return &it->second;
}

const BuildingTemplate* DefCache::buildingTemplate(const std::string& category) const {
    const auto it = buildingTemplates_.find(category);
    if (it == buildingTemplates_.end()) {
        return nullptr;
    }
    return &it->second;
}

const SizeBand* DefCache::plotSizeBand(const std::string& category) const {
    const auto it = plotSizes_.find(category);
    if (it == plotSizes_.end()) {
        return nullptr;
    }
    return &it->second;
}

const BuildingDef* DefCache::building(const std::string& buildingType) const {
    const auto it = buildings_.find(buildingType);
    if (it == buildings_.end()) {
        return nullptr;
    }
    return &it->second;
}

void DefCache::assignTypeIds() {
    typeNames_.clear();
    std::vector<std::string> sorted;
    sorted.reserve(buildings_.size());
    for (const auto& [name, _] : buildings_) {
        sorted.push_back(name);
    }
    std::sort(sorted.begin(), sorted.end());
    typeNames_.resize(sorted.size());
    for (BuildingTypeId i = 0; i < static_cast<BuildingTypeId>(sorted.size()); ++i) {
        typeNames_[i] = sorted[static_cast<std::size_t>(i)];
        buildings_[sorted[static_cast<std::size_t>(i)]].typeId = i;
    }
}

BuildingTypeId DefCache::typeIdFor(const std::string& buildingType) const {
    const BuildingDef* def = building(buildingType);
    return def != nullptr ? def->typeId : kInvalidBuildingTypeId;
}

const std::string& DefCache::typeName(BuildingTypeId typeId) const {
    static const std::string kEmpty;
    if (typeId >= typeNames_.size()) {
        return kEmpty;
    }
    return typeNames_[typeId];
}

const BuildingDef* DefCache::building(BuildingTypeId typeId) const {
    if (typeId >= typeNames_.size()) {
        return nullptr;
    }
    return building(typeNames_[typeId]);
}

const std::string* DefCache::buildingCategory(const std::string& buildingType) const {
    const BuildingDef* def = building(buildingType);
    if (!def || def->sizeCategory.empty()) {
        return nullptr;
    }
    return &def->sizeCategory;
}

const SizeBand* DefCache::plotSizeBandForBuilding(const std::string& buildingType) const {
    const BuildingDef* def = building(buildingType);
    if (!def) {
        return nullptr;
    }

    const SizeBand* best = nullptr;
    for (const auto& [sizeName, _] : def->buildingsOnPlot) {
        const SizeBand* band = plotSizeBand(sizeName);
        if (band != nullptr && (best == nullptr || band->maxArea > best->maxArea)) {
            best = band;
        }
    }
    return best;
}

const SizeBand* DefCache::plotSizeBandForBuilding(BuildingTypeId typeId) const {
    return plotSizeBandForBuilding(typeName(typeId));
}

const CountRange* DefCache::plotBuildingCount(const std::string& buildingType,
                                              const std::string& sizeCategory) const {
    const BuildingDef* def = building(buildingType);
    if (!def) {
        return nullptr;
    }
    const auto it = def->buildingsOnPlot.find(sizeCategory);
    if (it == def->buildingsOnPlot.end()) {
        return nullptr;
    }
    return &it->second;
}

bool DefCache::plotFitsBuilding(float plotArea, const std::string& buildingType) const {
    const SizeBand* band = plotSizeBandForBuilding(buildingType);
    if (!band) {
        return false;
    }
    return band->contains(plotArea);
}
