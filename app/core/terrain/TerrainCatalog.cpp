#include "terrain/TerrainCatalog.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <iostream>

namespace {

TerrainOutlineClass parseOutlineClass(const std::string& s) {
    if (s == "water") {
        return TerrainOutlineClass::Water;
    }
    if (s == "land") {
        return TerrainOutlineClass::Land;
    }
    return TerrainOutlineClass::None;
}

}  // namespace

TerrainCatalog TerrainCatalog::load(const std::filesystem::path& path) {
    TerrainCatalog catalog;
    YAML::Node     root = YAML::LoadFile(path.string());

    if (root["terrains"] && root["terrains"].IsSequence()) {
        for (const YAML::Node& node : root["terrains"]) {
            TerrainTypeDef def{};
            def.id  = node["id"].as<TerrainId>();
            def.key = node["key"].as<std::string>();
            if (node["rgb"] && node["rgb"].IsSequence() && node["rgb"].size() >= 3) {
                def.rgb = {node["rgb"][0].as<uint8_t>(), node["rgb"][1].as<uint8_t>(),
                           node["rgb"][2].as<uint8_t>()};
            }
            if (node["forbidden"]) {
                def.forbidden = node["forbidden"].as<bool>();
            }
            if (node["outline"]) {
                def.outlineClass = parseOutlineClass(node["outline"].as<std::string>());
            }
            if (def.id == kTerrainUnknown || def.key.empty()) {
                continue;
            }
            if (catalog.idToDense_.count(def.id) > 0) {
                std::cerr << "TerrainCatalog: duplicate id " << def.id << '\n';
                continue;
            }
            catalog.idToDense_[def.id] = catalog.types_.size();
            catalog.keyToId_[def.key]  = def.id;
            catalog.types_.push_back(def);
        }
    }

    if (root["prefer_aliases"] && root["prefer_aliases"].IsMap()) {
        for (const auto& entry : root["prefer_aliases"]) {
            const std::string aliasKey = entry.first.as<std::string>();
            std::vector<TerrainId> ids;
            if (entry.second.IsSequence()) {
                for (const YAML::Node& item : entry.second) {
                    const TerrainId id = catalog.resolveKey(item.as<std::string>());
                    if (id != kTerrainUnknown) {
                        catalog.appendPreferId(ids, id);
                    }
                }
            } else if (entry.second.IsScalar()) {
                const TerrainId id = catalog.resolveKey(entry.second.as<std::string>());
                if (id != kTerrainUnknown) {
                    catalog.appendPreferId(ids, id);
                }
            }
            if (!ids.empty()) {
                catalog.preferAliases_[aliasKey] = std::move(ids);
            }
        }
    }

    return catalog;
}

TerrainId TerrainCatalog::idAt(std::size_t denseIndex) const {
    if (denseIndex >= types_.size()) {
        return kTerrainUnknown;
    }
    return types_[denseIndex].id;
}

std::size_t TerrainCatalog::denseIndex(TerrainId id) const {
    const auto found = idToDense_.find(id);
    if (found == idToDense_.end()) {
        return types_.size();
    }
    return found->second;
}

bool TerrainCatalog::contains(TerrainId id) const {
    return idToDense_.find(id) != idToDense_.end();
}

const TerrainTypeDef* TerrainCatalog::find(TerrainId id) const {
    const std::size_t idx = denseIndex(id);
    if (idx >= types_.size()) {
        return nullptr;
    }
    return &types_[idx];
}

const TerrainTypeDef* TerrainCatalog::findByKey(const std::string& key) const {
    const auto found = keyToId_.find(key);
    if (found == keyToId_.end()) {
        return nullptr;
    }
    return find(found->second);
}

TerrainId TerrainCatalog::resolveKey(const std::string& key) const {
    const auto found = keyToId_.find(key);
    if (found == keyToId_.end()) {
        return kTerrainUnknown;
    }
    return found->second;
}

std::vector<TerrainId> TerrainCatalog::expandPreferToken(const std::string& token) const {
    const auto alias = preferAliases_.find(token);
    if (alias != preferAliases_.end()) {
        return alias->second;
    }
    std::vector<TerrainId> out;
    appendPreferId(out, resolveKey(token));
    return out;
}

void TerrainCatalog::appendPreferId(std::vector<TerrainId>& out, TerrainId id) const {
    if (id == kTerrainUnknown) {
        return;
    }
    if (std::find(out.begin(), out.end(), id) != out.end()) {
        return;
    }
    out.push_back(id);
}

const char* TerrainCatalog::name(TerrainId id) const {
    const TerrainTypeDef* def = find(id);
    if (def == nullptr) {
        return "unknown";
    }
    return def->key.c_str();
}

bool TerrainCatalog::isForbidden(TerrainId id) const {
    const TerrainTypeDef* def = find(id);
    return def != nullptr && def->forbidden;
}

TerrainOutlineClass TerrainCatalog::outlineClass(TerrainId id) const {
    const TerrainTypeDef* def = find(id);
    if (def == nullptr) {
        return TerrainOutlineClass::None;
    }
    return def->outlineClass;
}
