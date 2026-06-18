#!/usr/bin/env python3
"""Fix regex-induced corruptions from revert_deca.py."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "app" / "core"


def fix_text(text: str) -> str:
    # Pointer inward access
    text = re.sub(r"\*(\w+)\.inward", r"\1->inward", text)
    text = re.sub(r"sideBank\(([^)]+)\.inward\)", r"sideBank(\1)->inward", text)

    # Broken corner indices from footprintCorner/plotCorner with (i + 1) % 4
    text = text.replace("(i + 1] % 4)", "(i + 1) % 4)")
    text = text.replace("(edgeIndex + 1] % 4)", "(edgeIndex + 1) % 4)")

    # junction id vs .pos
    text = re.sub(
        r"junctions\[static_cast<std::size_t>\(([^.\]]+)\.pos\)\]",
        r"junctions[static_cast<std::size_t>(\1)].pos",
        text,
    )
    text = re.sub(
        r"chain\.junctions\.front\(\.pos\)",
        r"town.junctions[static_cast<std::size_t>(chain.junctions.front())].pos",
        text,
    )
    text = re.sub(
        r"chain\.junctions\.back\(\.pos\)",
        r"town.junctions[static_cast<std::size_t>(chain.junctions.back())].pos",
        text,
    )

    # container .front/.back member access
    text = re.sub(r"\.front\(\.(\w+)\)", r".front().\1", text)
    text = re.sub(r"\.back\(\.(\w+)\)", r".back().\1", text)

    # setter trailing paren
    text = re.sub(r"= (\w+Units)\);", r"= \1;", text)

    # recomputeSegmentDist
    text = text.replace(
        "(origin + edgeDir * ((startW + endW * 0.5f) - center).length());",
        "(origin + edgeDir * ((startW + endW) * 0.5f) - center).length();",
    )

    return text


def main():
    for path in sorted(CORE.rglob("*.cpp")):
        orig = path.read_text(encoding="utf-8")
        fixed = fix_text(orig)
        if fixed != orig:
            path.write_text(fixed, encoding="utf-8")
            print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
