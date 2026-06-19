#include "RoadExhaustion.h"

#include "FrontageGapFill.h"
#include "PlotGeometry.h"
#include "Town.h"

#include <algorithm>

bool bankHas(BankExhausted flag, const RoadSideFrontage& side) {
    return (side.exhausted & static_cast<std::uint8_t>(flag)) != 0;
}

void setBankExhausted(RoadSideFrontage& side, BankExhausted flag) {
    side.exhausted |= static_cast<std::uint8_t>(flag);
}

void clearBankExhausted(RoadSideFrontage& side, BankExhausted flag) {
    side.exhausted &= static_cast<std::uint8_t>(~static_cast<std::uint8_t>(flag));
}

void clearAllBankExhausted(RoadSideFrontage& side) {
    side.exhausted = 0;
}

bool bankPlotDone(const RoadSideFrontage& side) {
    return bankHas(PlotDone, side);
}

bool bankGapDone(const RoadSideFrontage& side) {
    return bankHas(GapDone, side);
}

bool bankAlleyDone(const RoadSideFrontage& side) {
    return bankHas(AlleyDone, side);
}

bool roadPlotAndGapExhausted(const Road& road) {
    return bankPlotDone(road.sideA) && bankPlotDone(road.sideB) && bankGapDone(road.sideA)
           && bankGapDone(road.sideB);
}

bool bankPlotExhaustedVerified(const Town& town, int roadId, int bankIndex) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return true;
    }
    const RoadSideFrontage* side =
        town.roads[static_cast<std::size_t>(roadId)].sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f) {
        return true;
    }
    if (!bankPlotDone(*side)) {
        return false;
    }
    for (const RoadFrontageSegment& segment : side->segments) {
        if (segment.width() + 1e-3f >= town.syncMinPlotFrontage) {
            return false;
        }
    }
    return true;
}

bool bankGapExhaustedVerified(Town& town, int roadId, int bankIndex) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return true;
    }
    const RoadSideFrontage* side =
        town.roads[static_cast<std::size_t>(roadId)].sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f) {
        return true;
    }
    if (!bankGapDone(*side)) {
        return false;
    }
    return !bankHasMainWallGapAtLeast(town, roadId, bankIndex, town.syncMinGapWidth);
}

bool bankAlleyExhaustedVerified(Town& town, int roadId, int bankIndex,
                                 float minAlleyGapWidth, float maxDistInclusive) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size()) || minAlleyGapWidth <= 0.f) {
        return true;
    }
    const RoadSideFrontage* side =
        town.roads[static_cast<std::size_t>(roadId)].sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f) {
        return true;
    }
    if (!bankAlleyDone(*side)) {
        return false;
    }
    return !bankHasUncheckedAlleyGaps(town, roadId, bankIndex, minAlleyGapWidth, maxDistInclusive);
}

bool bankHasSegmentGapFillPotential(const Town& town, int roadId, int bankIndex, float minWidth) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size()) || minWidth <= 0.f) {
        return false;
    }
    const RoadSideFrontage* side =
        town.roads[static_cast<std::size_t>(roadId)].sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f) {
        return false;
    }
    for (const RoadFrontageSegment& segment : side->segments) {
        if (segment.width() + 1e-3f >= minWidth) {
            return true;
        }
    }
    return false;
}

void clearAllRoadExhaustion(Town& town) {
    for (Road& road : town.roads) {
        clearAllBankExhausted(road.sideA);
        clearAllBankExhausted(road.sideB);
    }
}

void initRoadExhaustionForSync(Town& town, const PlacementFloors& floors,
                               const TownConfig& townCfg) {
    town.syncMinPlotFrontage  = floors.minPlotFrontage;
    town.syncMinPlotDepth     = floors.minPlotDepth;
    town.syncMinGapWidth      = floors.minGapWidth;
    town.syncMinAlleyGapWidth = townCfg.minWallGapForAlley;
    clearAllRoadExhaustion(town);
    recomputePlotGapDoneTown(town);
}

void clearExhaustionAfterAlleyApply(Town& town, int hostRoadId, int newRoadId) {
    if (hostRoadId >= 0 && hostRoadId < static_cast<int>(town.roads.size())) {
        clearAllBankExhausted(town.roads[static_cast<std::size_t>(hostRoadId)].sideA);
        clearAllBankExhausted(town.roads[static_cast<std::size_t>(hostRoadId)].sideB);
        recomputePlotGapDoneForBank(town, hostRoadId, 0);
        recomputePlotGapDoneForBank(town, hostRoadId, 1);
        clearAlleyGapStateForRoad(town, hostRoadId);
    }
    if (newRoadId >= 0 && newRoadId < static_cast<int>(town.roads.size())) {
        clearAllBankExhausted(town.roads[static_cast<std::size_t>(newRoadId)].sideA);
        clearAllBankExhausted(town.roads[static_cast<std::size_t>(newRoadId)].sideB);
        recomputePlotGapDoneForBank(town, newRoadId, 0);
        recomputePlotGapDoneForBank(town, newRoadId, 1);
        clearAlleyGapStateForRoad(town, newRoadId);
    }
}

void recomputePlotGapDoneForBank(Town& town, int roadId, int bankIndex) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road&             road = town.roads[static_cast<std::size_t>(roadId)];
    RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr) {
        return;
    }

    clearBankExhausted(*side, PlotDone);
    clearBankExhausted(*side, GapDone);

    if (side->inward.length() < 1e-4f) {
        setBankExhausted(*side, PlotDone);
        setBankExhausted(*side, GapDone);
        if (town.syncMinAlleyGapWidth > 0.f) {
            setBankExhausted(*side, AlleyDone);
        }
        return;
    }

    bool anyPlotSlot = false;
    for (const RoadFrontageSegment& segment : side->segments) {
        if (segment.width() + 1e-3f >= town.syncMinPlotFrontage) {
            anyPlotSlot = true;
            break;
        }
    }
    if (!anyPlotSlot) {
        setBankExhausted(*side, PlotDone);
    }

    if (!bankHasMainWallGapAtLeast(town, roadId, bankIndex, town.syncMinGapWidth)) {
        setBankExhausted(*side, GapDone);
    }

    if (town.syncMinAlleyGapWidth > 0.f
        && !bankHasMainWallGapAtLeast(town, roadId, bankIndex, town.syncMinAlleyGapWidth)) {
        setBankExhausted(*side, AlleyDone);
    }
}

void recomputePlotGapDoneTown(Town& town) {
    for (Road& road : town.roads) {
        if (road.id < 0) {
            continue;
        }
        recomputePlotGapDoneForBank(town, road.id, 0);
        recomputePlotGapDoneForBank(town, road.id, 1);
    }
}

void refreshBankExhaustionAfterCarve(Town& town, int roadId, int bankIndex) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    RoadSideFrontage* side = town.roads[static_cast<std::size_t>(roadId)].sideBank(bankIndex);
    if (side == nullptr) {
        return;
    }
    clearAllBankExhausted(*side);
    recomputePlotGapDoneForBank(town, roadId, bankIndex);
}
