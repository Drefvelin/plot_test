#pragma once

#include "DefCache.h"
#include "TownConfig.h"

#include <string>
#include <vector>

class BuildingGrowthQueue {
public:
    BuildingGrowthQueue(const TownConfig& townConfig, int seed);

    int maxBuildings() const { return static_cast<int>(queue_.size()); }
    int activeCount() const { return activeCount_; }

    void setActiveCount(int count);
    const std::vector<std::string>& queue() const { return queue_; }
    std::vector<std::string> activeBuildings() const;
    std::string nextBuildingType() const;

    bool isSegmentGapFillIndex(int queueIndex) const;
    int segmentGapFillCount() const { return segmentGapFillCount_; }

private:
    std::vector<std::string> queue_;
    int                      activeCount_         = 0;
    int                      segmentGapFillCount_ = 0;
};

bool isGapFillBuildingType(const DefCache& defs, const std::string& buildingType);
