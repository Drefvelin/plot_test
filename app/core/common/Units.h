#pragma once

namespace units {

constexpr float kDefaultPixelsPerUnit = 10.f;

inline float toPixels(float units, float pixelsPerUnit) {
    return units * pixelsPerUnit;
}

inline float toUnits(float pixels, float pixelsPerUnit) {
    return pixels / pixelsPerUnit;
}

}  // namespace units
