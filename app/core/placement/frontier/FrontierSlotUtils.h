#pragma once

#include "common/Vec2.h"

struct Town;
struct FrontierRef;
struct FrontageSlot;

// Shared helpers for terrain-scan and border frontier buckets. Both subsystems
// project a FrontierRef onto its road segment to read width, fill a
// FrontageSlot, and compute the segment midpoint.

float frontierRefWidth(const Town& town, const FrontierRef& ref);
void  fillFrontageSlotFromRefFields(const Town& town, const FrontierRef& ref, FrontageSlot& slot);
bool  segmentMidpointFromRef(const Town& town, const FrontierRef& ref, Vec2& out);
