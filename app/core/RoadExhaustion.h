#pragma once

#include "PlacementFloors.h"
#include "Town.h"
#include "TownConfig.h"

enum BankExhausted : std::uint8_t {
    PlotDone  = 1 << 0,
    AlleyDone = 1 << 1,
    GapDone   = 1 << 2,
};

bool bankHas(BankExhausted flag, const RoadSideFrontage& side);
void setBankExhausted(RoadSideFrontage& side, BankExhausted flag);
void clearBankExhausted(RoadSideFrontage& side, BankExhausted flag);
void clearAllBankExhausted(RoadSideFrontage& side);

void clearAllRoadExhaustion(Town& town);
void initRoadExhaustionForSync(Town& town, const PlacementFloors& floors,
                               const TownConfig& townCfg);
void recomputePlotGapDoneForBank(Town& town, int roadId, int bankIndex);
void recomputePlotGapDoneTown(Town& town);

void refreshBankExhaustionAfterCarve(Town& town, int roadId, int bankIndex);
void clearExhaustionAfterAlleyApply(Town& town, int hostRoadId, int newRoadId);

bool bankPlotDone(const RoadSideFrontage& side);
bool bankGapDone(const RoadSideFrontage& side);
bool bankAlleyDone(const RoadSideFrontage& side);

bool roadPlotAndGapExhausted(const Road& road);

bool bankPlotExhaustedVerified(const Town& town, int roadId, int bankIndex);
bool bankGapExhaustedVerified(Town& town, int roadId, int bankIndex);
bool bankAlleyExhaustedVerified(Town& town, int roadId, int bankIndex,
                                 float minAlleyGapWidth, float maxDistInclusive);

bool bankHasSegmentGapFillPotential(const Town& town, int roadId, int bankIndex, float minWidth);
