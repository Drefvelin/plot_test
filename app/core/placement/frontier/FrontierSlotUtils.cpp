#include "placement/frontier/FrontierSlotUtils.h"

#include "placement/frontage/FrontagePlacement.h"
#include "placement/frontier/PlacementFrontier.h"
#include "town/Town.h"

float frontierRefWidth(const Town& town, const FrontierRef& ref) {
    if (ref.roadId < 0 || ref.roadId >= static_cast<int>(town.roads.size())) {
        return 0.f;
    }
    return ref.endT - ref.startT;
}

void fillFrontageSlotFromRefFields(const Town& town, const FrontierRef& ref, FrontageSlot& slot) {
    slot.roadId     = ref.roadId;
    slot.bankIndex  = ref.bankIndex;
    slot.segmentId  = ref.segmentId;
    slot.centerDist = ref.centerDist;
    if (ref.roadId < 0 || ref.roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    slot.startT = ref.startT;
    slot.endT   = ref.endT;
}

bool segmentMidpointFromRef(const Town& town, const FrontierRef& ref, Vec2& out) {
    FrontageSlot slot;
    fillFrontageSlotFromRefFields(town, ref, slot);
    return segmentMidpoint(town, slot, out);
}
