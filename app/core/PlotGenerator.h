#pragma once

#include "Config.h"
#include "Town.h"

class PlotGenerator {
public:
    static void generate(Town& town, const Config& config);
};
