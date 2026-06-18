#!/usr/bin/env python3
"""Revert deca-unit storage; restore Vec2/float fields and direct access."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "app" / "core"

# Simple identifier-safe replacements (order matters for some).
SIMPLE = [
    (r"world::roadEndA\(([^)]+)\)", r"\1.a"),
    (r"world::roadEndB\(([^)]+)\)", r"\1.b"),
    (r"world::roadLength\(([^)]+)\)", r"\1.length()"),
    (r"world::roadSideInward\(([^)]+)\)", r"\1.inward"),
    (r"world::junctionPos\(([^)]+)\)", r"\1.pos"),
    (r"world::plotCorner\(([^,]+),\s*([^)]+)\)", r"\1.corners[\2]"),
    (r"world::plotOutlineTangent\(([^)]+)\)", r"\1.outlineTangent"),
    (r"world::plotOutlineInward\(([^)]+)\)", r"\1.outlineInward"),
    (r"world::plotFrontage\(([^)]+)\)", r"\1.frontage"),
    (r"world::plotDepth\(([^)]+)\)", r"\1.depth"),
    (r"world::plotArea\(([^)]+)\)", r"\1.area"),
    (r"world::footprintCorner\(([^,]+),\s*([^)]+)\)", r"\1.corners[\2]"),
    (r"world::footprintPlacedShortLen\(([^)]+)\)", r"\1.placedShortLen"),
    (r"world::footprintPlacedLongLen\(([^)]+)\)", r"\1.placedLongLen"),
    (r"world::secondaryRecordA\(([^)]+)\)", r"\1.a"),
    (r"world::secondaryRecordB\(([^)]+)\)", r"\1.b"),
    (r"world::alleyProbeA\(([^)]+)\)", r"\1.a"),
    (r"world::alleyProbeB\(([^)]+)\)", r"\1.b"),
    (r"world::townCenter\(([^)]+)\)", r"\1.center"),
    (r"world::townRadius\(([^)]+)\)", r"\1.radius"),
    (r"world::townWidth\(([^)]+)\)", r"\1.width"),
    (r"world::townHeight\(([^)]+)\)", r"\1.height"),
    (r"world::toNormFloat\(([^)]+)\)", r"\1"),
    (r"world::toDecaFloat\(([^)]+)\)", r"\1"),
    (r"world::toAreaFloat\(([^)]+)\)", r"\1"),
    (r"world::toVec2\(([^)]+)\)", r"\1"),
    (r"world::fromDecaU16\(([^)]+)\)", r"\1"),
    (r"world::fromNorm\(([^)]+)\)", r"\1"),
    (r"world::fromArea\(([^)]+)\)", r"\1"),
    (r"world::fromVec2Deca\(([^)]+)\)", r"\1"),
    (r"world::fromUnitVec2\(([^)]+)\)", r"\1.normalized()"),
    (r"world::frontageSegmentStart\([^,]+,\s*([^)]+)\)", r"\1.startT"),
    (r"world::frontageSegmentEnd\([^,]+,\s*([^)]+)\)", r"\1.endT"),
    (r"world::frontageSegmentWidth\([^,]+,\s*([^)]+)\)", r"(\1.endT - \1.startT)"),
    (r"world::frontageSegmentStart\(([^,]+),\s*([^)]+)\)", r"\2.startT"),
    (r"world::frontageSegmentEnd\(([^,]+),\s*([^)]+)\)", r"\2.endT"),
    (r"world::frontageSegmentWidth\(([^,]+),\s*([^)]+)\)", r"(\2.endT - \2.startT)"),
    (r"world::toRoadT\([^,]+,\s*([^)]+)\)", r"\1"),
    (r"world::toRoadT\(([^,]+),\s*([^)]+)\)", r"\2"),
    (r"world::fromRoadT\([^,]+,\s*([^)]+)\)", r"\1"),
    (r"world::fromRoadT\(([^,]+),\s*([^)]+)\)", r"\2"),
]

SETTERS = [
    (r"world::setRoadEndA\(([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)", r"\1.a = \2"),
    (r"world::setRoadEndB\(([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)", r"\1.b = \2"),
    (r"world::setRoadSideInward\(([^,]+),\s*([^)]+)\)", r"\1.inward = \2.normalized()"),
    (r"world::setJunctionPos\(([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)", r"\1.pos = \2"),
    (r"world::setPlotCorner\(([^,]+),\s*([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)", r"\1.corners[\2] = \3"),
    (r"world::setPlotOutlineTangent\(([^,]+),\s*([^)]+)\)", r"\1.outlineTangent = \2.normalized()"),
    (r"world::setPlotOutlineInward\(([^,]+),\s*([^)]+)\)", r"\1.outlineInward = \2.normalized()"),
    (r"world::setPlotFrontage\(([^,]+),\s*([^)]+)\)", r"\1.frontage = \2"),
    (r"world::setPlotDepth\(([^,]+),\s*([^)]+)\)", r"\1.depth = \2"),
    (r"world::setPlotArea\(([^,]+),\s*([^)]+)\)", r"\1.area = \2"),
    (r"world::setFootprintCorner\(([^,]+),\s*([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)",
     r"\1.corners[\2] = \3"),
    (r"world::setFootprintPlacedShortLen\(([^,]+),\s*([^)]+)\)", r"\1.placedShortLen = \2"),
    (r"world::setFootprintPlacedLongLen\(([^,]+),\s*([^)]+)\)", r"\1.placedLongLen = \2"),
    (r"world::setSecondaryRecordA\(([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)", r"\1.a = \2"),
    (r"world::setSecondaryRecordB\(([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)", r"\1.b = \2"),
    (r"world::setAlleyProbeA\(([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)", r"\1.a = \2"),
    (r"world::setAlleyProbeB\(([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)", r"\1.b = \2"),
    (r"world::setTownCenter\(([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)", r"\1.center = \2"),
    (r"world::setTownRadius\(([^,]+),\s*([^)]+)\)", r"\1.radius = \2"),
    (r"world::setTownWidth\(([^,]+),\s*([^)]+)\)", r"\1.width = \2"),
    (r"world::setTownHeight\(([^,]+),\s*([^)]+)\)", r"\1.height = \2"),
    (r"world::setFrontageSegmentSpan\(([^,]+),\s*[^,]+,\s*([^,]+),\s*([^)]+)\)",
     r"\1.startT = \2; \1.endT = \3"),
    (r"world::fromVec2\(([^,)]+)(?:,\s*[^)]+)?\)", r"\1"),
]

READ_PLOT = re.compile(
    r"world::readPlotCorners\(([^,]+),\s*([^)]+)\)"
)
READ_FP = re.compile(
    r"world::readFootprintCorners\(([^,]+),\s*([^)]+)\)"
)
SET_PLOT_CORNERS = re.compile(
    r"world::setPlotCorners\(([^,]+),\s*([^)]+)\)"
)
SET_FP_CORNERS = re.compile(
    r"world::setFootprintCorners\(([^,]+),\s*([^,)]+)(?:,\s*[^)]+)?\)"
)
TO_VEC2_RING = re.compile(r"world::toVec2Ring\(([^)]+)\)")
FROM_VEC2_RING = re.compile(r"world::fromVec2Ring\(([^,)]+)(?:,\s*[^)]+)?\)")


def expand_read_plot_corners(m):
    plot, out = m.group(1), m.group(2)
    return (
        f"for (int _ri = 0; _ri < 4; ++_ri) {{ {out}[_ri] = {plot}.corners[_ri]; }}"
    )


def expand_read_fp_corners(m):
    fp, out = m.group(1), m.group(2)
    return (
        f"for (int _ri = 0; _ri < 4; ++_ri) {{ {out}[_ri] = {fp}.corners[_ri]; }}"
    )


def expand_set_plot_corners(m):
    plot, corners = m.group(1), m.group(2)
    return (
        f"for (int _si = 0; _si < 4; ++_si) {{ {plot}.corners[_si] = {corners}[_si]; }}"
    )


def expand_set_fp_corners(m):
    fp, corners = m.group(1), m.group(2)
    return (
        f"for (int _si = 0; _si < 4; ++_si) {{ {fp}.corners[_si] = {corners}[_si]; }}"
    )


def process_file(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    orig = text

    text = re.sub(r'#include\s+"WorldQuant\.h"\s*\n', "", text)
    text = re.sub(r'using world::[^;]+;\s*\n', "", text)

    for pat, repl in SETTERS:
        text = re.sub(pat, repl, text)
    for pat, repl in SIMPLE:
        text = re.sub(pat, repl, text)

    text = READ_PLOT.sub(expand_read_plot_corners, text)
    text = READ_FP.sub(expand_read_fp_corners, text)
    text = SET_PLOT_CORNERS.sub(expand_set_plot_corners, text)
    text = SET_FP_CORNERS.sub(expand_set_fp_corners, text)
    text = TO_VEC2_RING.sub(r"\1", text)
    text = FROM_VEC2_RING.sub(r"\1", text)

    # kQuantSlop -> small epsilon
    text = text.replace("world::kQuantSlopHalf", "1e-4f")
    text = text.replace("world::kQuantSlop", "1e-3f")

    if text != orig:
        path.write_text(text, encoding="utf-8")
        return True
    return False


def main():
    changed = []
    for path in sorted(CORE.rglob("*")):
        if path.suffix not in (".cpp", ".h") or path.name.startswith("WorldQuant"):
            continue
        if process_file(path):
            changed.append(path.relative_to(ROOT))
    print(f"Updated {len(changed)} files")
    for p in changed:
        print(f"  {p}")


if __name__ == "__main__":
    main()
