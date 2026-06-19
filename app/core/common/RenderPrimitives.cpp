#include "common/RenderPrimitives.h"

#include <cmath>

void appendThickSegment(sf::VertexArray& tris, const Vec2& a, const Vec2& b, float thickness,
                        const sf::Color& color) {
    const float dx    = b.x - a.x;
    const float dy    = b.y - a.y;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-4f) {
        return;
    }
    const float len = std::sqrt(lenSq);
    const float nx  = -dy / len * thickness * 0.5f;
    const float ny  = dx / len * thickness * 0.5f;

    tris.append({{a.x + nx, a.y + ny}, color});
    tris.append({{a.x - nx, a.y - ny}, color});
    tris.append({{b.x + nx, b.y + ny}, color});

    tris.append({{b.x + nx, b.y + ny}, color});
    tris.append({{a.x - nx, a.y - ny}, color});
    tris.append({{b.x - nx, b.y - ny}, color});
}
