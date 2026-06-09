#pragma once

#include "Config.h"
#include "Town.h"

int generateRoadPlots(Cell& cell, int roadId, const Town& town, const PlotConfig& plots,
                      int nextPlotId);
