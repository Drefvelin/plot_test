#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>

struct SizeBand {
    std::string name;
    float       minArea = 0.f;
    float       maxArea = 0.f;

    bool contains(float area) const { return area >= minArea && area <= maxArea; }
};

struct CountRange {
    int minCount = 0;
    int maxCount = 0;

    bool isFixed() const { return minCount == maxCount; }
    bool isValid() const { return minCount > 0 && maxCount >= minCount; }
};

struct BuildingTemplateRules {
    bool longFacingMiddle = false;
    bool doorLong         = false;
    bool doorShort        = false;
    bool edgePlacement    = false;
    bool middlePlacement  = false;
    bool cornerPlacement  = false;
    bool diagonalAllowed  = false;
};

struct BuildingTemplate {
    std::string           name;
    SizeBand              size;
    BuildingTemplateRules rules;
};

struct BuildingDef {
    std::string name;
    std::string sizeCategory;  // largest plot-size category in the mix (drives lot sizing)
    std::string type;          // urban, residential, rural
    std::array<uint8_t, 3> rgb = {128, 128, 128};
    bool fillIn        = true;   // may be placed via segment gap-fill in the final growth band
    bool allowPlotFill = true;   // gap-fill may overlap this lot's plot polygon (yard)

    // How many buildings of each size category sit on one plot (e.g. small: 1, tiny: 2-3).
    std::unordered_map<std::string, CountRange> buildingsOnPlot;
};

class DefCache {
public:
    DefCache() = default;
    explicit DefCache(const std::filesystem::path& path);

    static DefCache load(const std::filesystem::path& path);
    static std::filesystem::path resolveBuildingsPath();

    const SizeBand* buildingSizeBand(const std::string& category) const;
    const BuildingTemplate* buildingTemplate(const std::string& category) const;
    const SizeBand* plotSizeBand(const std::string& category) const;

    // Plot lot band for a building type (largest plot_sizes entry in its mix).
    const SizeBand* plotSizeBandForBuilding(const std::string& buildingType) const;

    const BuildingDef* building(const std::string& buildingType) const;
    const std::string* buildingCategory(const std::string& buildingType) const;

    // Backward-compatible aliases (plot lot bands).
    const SizeBand* sizeBand(const std::string& category) const { return plotSizeBand(category); }
    const SizeBand* sizeBandForBuilding(const std::string& buildingType) const {
        return plotSizeBandForBuilding(buildingType);
    }

    const CountRange* plotBuildingCount(const std::string& buildingType,
                                        const std::string& sizeCategory) const;

    bool plotFitsBuilding(float plotArea, const std::string& buildingType) const;

    const std::unordered_map<std::string, SizeBand>& buildingSizes() const {
        return buildingSizes_;
    }
    const std::unordered_map<std::string, BuildingTemplate>& buildingTemplates() const {
        return buildingTemplates_;
    }
    const std::unordered_map<std::string, SizeBand>& plotSizes() const { return plotSizes_; }
    const std::unordered_map<std::string, SizeBand>& sizes() const { return plotSizes_; }

    const std::unordered_map<std::string, BuildingDef>& buildings() const { return buildings_; }

private:
    std::unordered_map<std::string, SizeBand>         buildingSizes_;
    std::unordered_map<std::string, BuildingTemplate> buildingTemplates_;
    std::unordered_map<std::string, SizeBand>         plotSizes_;
    std::unordered_map<std::string, BuildingDef> buildings_;
};
