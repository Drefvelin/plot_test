import re
import pathlib

root = pathlib.Path(__file__).resolve().parent.parent / "app" / "core"
src = (root / "BuildingPlacer.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")

m = re.search(r"namespace \{\n(.*)\n\}  // namespace\n\nvoid BuildingPlacer::sync", src, re.S)
if not m:
    raise SystemExit("namespace block not found")
body = m.group(1)


def extract(start, end):
    i = body.find(start)
    j = body.find(end, i)
    if i < 0 or j < 0:
        raise SystemExit(f"marker not found: {start!r} -> {end!r}")
    return body[i:j]


geom = extract("bool pointOnSegment", "struct PlotDimensions")
dims = extract("struct PlotDimensions", "struct CellSearchStats")
log_block = extract("struct CellSearchStats", "void appendThickSegment")
mesh = extract("void appendThickSegment", "float slotTNearestCenter")
frontage = extract("float slotTNearestCenter", "void rebuildOutlineMesh")
outline = body[body.find("void rebuildOutlineMesh") :].rstrip()

frontage = frontage.replace(
    'Logger::log("plot", "segment_slide:',
    'Logger::log("probe", "segment_slide:',
)

(root / "PlotGeometry.h").write_text(
    """#pragma once

#include "Town.h"

#include <vector>

bool pointInPolygon(const Vec2& p, const std::vector<Vec2>& polygon);
bool plotInsideCell(const Plot& plot, const Cell& cell);
void buildRoadPlot(const Vec2& roadStart, const Vec2& edgeDir, const Vec2& inward, float setback,
                   float frontage, float depth, Plot& plot);
Vec2 plotCenter(const Plot& plot);
float maxPlotDepthInCell(const Vec2& roadStart, const Vec2& edgeDir, float frontage,
                         const Vec2& inward, float setback, const Cell& cell);
bool plotsOverlap(const Plot& a, const Plot& b);
bool overlapsInstances(const Plot& plot, const std::vector<BuildingInstance>& instances);
bool roadFrameForCell(const Road& road, int cellId, Vec2& origin, Vec2& farEnd, Vec2& edgeDir);
bool orientRoadForCell(const Road& road, int cellId, Vec2& a, Vec2& b, Vec2& edgeDir, Vec2& inward,
                       const Cell& cell, float setback);
""",
    encoding="utf-8",
)

(root / "PlotDimensions.h").write_text(
    """#pragma once

#include "DefCache.h"
#include "Town.h"

#include <string>

enum class DimReject {
    None,
    MissingBand,
    InvalidInput,
    RoadTooShort,
    DepthRatioExceeded,
    AreaOutOfBand,
    DepthExceedsCell,
};

enum class PlotOrientation {
    Horizontal,
    Vertical,
};

struct PlotDimensions {
    float frontage = 0.f;
    float depth    = 0.f;
    float area     = 0.f;
    bool  valid    = false;
};

const char* orientationName(PlotOrientation orient);
std::string fmt1(float value);
void sampleOrientationOrder(int buildingId, int townSeed, PlotOrientation& first,
                            PlotOrientation& second);
PlotDimensions computePlotDimensionsForRoad(const DefCache& defs, const std::string& buildingType,
                                            float targetArea, PlotOrientation orient,
                                            const Vec2& roadStart, const Vec2& edgeDir,
                                            float maxFrontage, const Vec2& inward, const Cell& cell,
                                            float maxDepthToFrontRatio, float frontageSetback,
                                            DimReject* rejectOut = nullptr,
                                            const SizeBand* plotAreaBand = nullptr);
const char* rejectName(DimReject reason);
""",
    encoding="utf-8",
)

(root / "FrontageZones.h").write_text(
    """#pragma once

#include "DefCache.h"
#include "Town.h"

#include <string>
#include <vector>

struct FrontageSlot;

int minJunctionHopsForRural(float townGrowth);
float minBuildingSeparationForRural(float townGrowth);
float ruralTargetCenterDist(const Town& town, float townGrowth);
float ruralMaxCenterDist(const Town& town, float townGrowth);
float zoneBiasForType(const char* zone, float townGrowth);
float scoreSegmentForZone(const Town& town, const FrontageSlot& slot, const char* zone,
                          float townGrowth, const std::vector<int>& junctionHops);
const char* zoneTypeForBuilding(const DefCache& defs, const std::string& buildingType);
float bandMinFrontage(const DefCache& defs, const std::string& buildingType, float segWidth,
                      float maxDepthToFrontRatio);
""",
    encoding="utf-8",
)

(root / "PlacementLogging.h").write_text(
    """#pragma once

#include "Config.h"
#include "DefCache.h"
#include "PlotDimensions.h"
#include "Town.h"

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

struct CellSearchStats {
    int   cellId        = -1;
    float centDist      = 0.f;
    int   roadsChecked  = 0;
    int   dimInvalid    = 0;
    int   dimRoadShort  = 0;
    int   dimRatio      = 0;
    int   dimArea       = 0;
    int   dimDepth      = 0;
    int   dimNoDepth    = 0;
    int   outsideCell   = 0;
    int   overlap       = 0;
    int   valid         = 0;
    float bestValidDist = std::numeric_limits<float>::max();

    struct RoadProbe {
        int       roadId        = -1;
        float     roadLen       = 0.f;
        float     depthCap      = 0.f;
        DimReject reject        = DimReject::None;
        bool      setbackInside = false;
        bool      altSideInside = false;
    };
    std::vector<RoadProbe> roadProbes;
};

struct PlacementSearchLog {
    int         buildingId = -1;
    std::string buildingType;
    int         totalValid = 0;
    int         chosenCell = -1;
    int         chosenRoad = -1;
    int         chosenSegment = -1;
    float       chosenDist = 0.f;
    float       chosenFrontage = 0.f;
    float       chosenDepth = 0.f;
    float       chosenArea = 0.f;
    float       targetArea = 0.f;
    PlotOrientation orientFirst  = PlotOrientation::Horizontal;
    PlotOrientation chosenOrient = PlotOrientation::Horizontal;
    Vec2        chosenCenter{};
    float       townGrowth = 0.f;
    float       zoneBias = 0.f;
    float       chosenZoneScore = 0.f;
    std::string zoneType;
    int         slotsExamined = 0;
    int         zoneFiltered = 0;
    int         noInwardSkipped = 0;
    int         orientFailedSkipped = 0;
    int         dimFailedSegments = 0;
    int         layoutRequested = 0;
    int         layoutPlaced = 0;
    std::string resultSummary;
    std::unordered_map<int, CellSearchStats> cells;
};

struct FrontageSlot;

CellSearchStats& statsFor(PlacementSearchLog& log, int cellId, float centDist);
void recordDimReject(CellSearchStats& stats, DimReject reason);
void logPlacementDecision(const Town& town, const PlacementSearchLog& log, const PlotConfig& plots,
                          const DefCache& defs);
void logSegmentInventory(const Town& town);
void logSegmentProbe(int buildingId, const FrontageSlot& slot, const char* result,
                     DimReject reject = DimReject::None, float depthCap = -1.f,
                     float frontageNeed = -1.f, float areaFit = -1.f, float slotT = -1.f);
""",
    encoding="utf-8",
)

(root / "FrontagePlacement.h").write_text(
    """#pragma once

#include "Config.h"
#include "DefCache.h"
#include "PlacementLogging.h"
#include "Town.h"

#include <string>

struct FrontageSlot {
    int   segmentId  = -1;
    int   roadId     = -1;
    int   cellId     = -1;
    float startT     = 0.f;
    float endT       = 0.f;
    float centerDist = 0.f;
    float zoneScore  = 0.f;

    float width() const { return endT - startT; }
};

bool tryPlaceRoadPlot(Town& town, const std::string& buildingType, const DefCache& defs,
                      const PlotConfig& plots, BuildingInstance& out, float targetArea,
                      int townSeed, int maxBuildings, PlacementSearchLog& searchLog);
""",
    encoding="utf-8",
)

(root / "PlotGeometry.cpp").write_text(
    '#include "PlotGeometry.h"\n\n#include <algorithm>\n\n' + geom,
    encoding="utf-8",
)

dims_funcs = dims[dims.find("const char* orientationName") :]
(root / "PlotDimensions.cpp").write_text(
    '#include "PlotDimensions.h"\n\n#include "PlotGeometry.h"\n\n#include <algorithm>\n#include <cmath>\n#include <iomanip>\n#include <random>\n#include <sstream>\n\n'
    + dims_funcs,
    encoding="utf-8",
)

zones = frontage[frontage.find("constexpr float kResidential") : frontage.find("void buildSegmentTCandidates")]
(root / "FrontageZones.cpp").write_text(
    '#include "FrontageZones.h"\n\n#include "FrontagePlacement.h"\n\n#include <algorithm>\n#include <cmath>\n#include <cstring>\n#include <vector>\n\n'
    + zones,
    encoding="utf-8",
)

log_funcs = log_block[log_block.find("CellSearchStats& statsFor") :]
pl_content = (
    '#include "PlacementLogging.h"\n\n#include "FrontagePlacement.h"\n#include "FrontageZones.h"\n#include "Logger.h"\n#include "PlotGeometry.h"\n\n#include <algorithm>\n\n'
    + log_funcs
)
pl_content = re.sub(
    r"void logPlacementDecision[\s\S]*?(?=void logSegmentInventory|\Z)",
    lambda m: m.group(0).replace('Logger::log("plot"', 'Logger::log("placement"'),
    pl_content,
)
pl_content = re.sub(
    r"void logSegmentInventory[\s\S]*?(?=void logSegmentProbe|\Z)",
    lambda m: m.group(0).replace('Logger::log("plot"', 'Logger::log("segments"'),
    pl_content,
)
pl_content = re.sub(
    r"void logSegmentProbe[\s\S]*",
    lambda m: m.group(0).replace('Logger::log("plot"', 'Logger::log("probe"'),
    pl_content,
)
(root / "PlacementLogging.cpp").write_text(pl_content, encoding="utf-8")

slot_fn = frontage[: frontage.find("struct FrontageSlot")]
placement_start = frontage.find("void buildSegmentTCandidates")
placement_end = frontage.find("void rebuildOutlineMesh")
frontage_main = slot_fn + frontage[placement_start:placement_end]
(root / "FrontagePlacement.cpp").write_text(
    '#include "FrontagePlacement.h"\n\n#include "BuildingLayout.h"\n#include "FrontageZones.h"\n#include "Logger.h"\n#include "PlotDimensions.h"\n#include "PlotGeometry.h"\n\n#include <algorithm>\n#include <cmath>\n#include <cstring>\n#include <limits>\n#include <vector>\n\n'
    + frontage_main,
    encoding="utf-8",
)

building_placer = (
    """#include "BuildingPlacer.h"

#include "BuildingLayout.h"
#include "FrontagePlacement.h"
#include "Logger.h"
#include "PlacementLogging.h"
#include "PlotGeometry.h"
#include "Units.h"

#include <SFML/Graphics.hpp>

namespace {

"""
    + mesh
    + outline
    + """

}  // namespace

void BuildingPlacer::sync(Town& town, const BuildingGrowthQueue& queue, const DefCache& defs,
                          const PlotConfig& plots, float pixelsPerUnit, int townSeed) {
    const int targetCount = queue.activeCount();

    while (static_cast<int>(town.buildingInstances.size()) > targetCount) {
        town.buildingInstances.pop_back();
    }

    resetRoadFrontageSegments(town, plots.frontageSetback);
    for (const BuildingInstance& inst : town.buildingInstances) {
        carveRoadFrontageForPlot(town, inst.plot, plots.frontageSetback);
    }
    logSegmentInventory(town);

    const auto& queueTypes = queue.queue();
    int                    failed     = 0;

    while (static_cast<int>(town.buildingInstances.size()) < targetCount) {
        const int index = static_cast<int>(town.buildingInstances.size());
        if (index >= static_cast<int>(queueTypes.size())) {
            break;
        }

        BuildingInstance instance;
        instance.id           = index;
        instance.buildingType = queueTypes[static_cast<std::size_t>(index)];

        const float targetArea =
            samplePlotTargetArea(defs, instance.buildingType, instance.id, townSeed);

        PlacementSearchLog searchLog;
        if (!tryPlaceRoadPlot(town, instance.buildingType, defs, plots, instance, targetArea,
                              townSeed, queue.maxBuildings(), searchLog)) {
            logPlacementDecision(town, searchLog, plots, defs);
            ++failed;
            break;
        }

        logPlacementDecision(town, searchLog, plots, defs);
        town.buildingInstances.push_back(instance);
    }

    rebuildOutlineMesh(town, defs, pixelsPerUnit, townSeed);
    rebuildFrontageSegmentMesh(town, plots, pixelsPerUnit);

    Logger::log("render", "building instances=" + std::to_string(town.buildingInstances.size()) + "/"
                               + std::to_string(targetCount)
                               + (failed > 0 ? " failed=" + std::to_string(failed) : ""));
}
"""
)
(root / "BuildingPlacer.cpp").write_text(building_placer, encoding="utf-8")
print("Split complete")
