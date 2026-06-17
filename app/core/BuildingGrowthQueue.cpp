#include "BuildingGrowthQueue.h"
#include "DefCache.h"

#include <algorithm>
#include <random>

BuildingGrowthQueue::BuildingGrowthQueue(const TownConfig& townConfig, int seed) {
    for (const auto& [buildingType, count] : townConfig.buildingCounts) {
        for (int i = 0; i < count; ++i) {
            queue_.push_back(buildingType);
        }
    }

    std::mt19937 rng(static_cast<unsigned>(seed));
    std::shuffle(queue_.begin(), queue_.end(), rng);
}

void BuildingGrowthQueue::setActiveCount(int count) {
    activeCount_ = std::clamp(count, 0, maxBuildings());
}

std::vector<std::string> BuildingGrowthQueue::activeBuildings() const {
    if (activeCount_ <= 0) {
        return {};
    }
    return {queue_.begin(), queue_.begin() + activeCount_};
}

std::string BuildingGrowthQueue::nextBuildingType() const {
    if (activeCount_ >= maxBuildings()) {
        return {};
    }
    return queue_[static_cast<std::size_t>(activeCount_)];
}

bool isGapFillBuildingType(const DefCache& defs, const std::string& buildingType) {
    const BuildingDef* def = defs.building(buildingType);
    if (!def || !def->fillIn) {
        return false;
    }
    return def->type == "residential" || def->type == "urban";
}
