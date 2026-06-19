#pragma once

#include <SFML/Graphics.hpp>

#include "common/Vec2.h"

// Shared mesh-emission helper: appends two triangles forming a quad of the
// given thickness centered on the segment a->b. Used by every subsystem that
// bakes road/plot/debug line meshes.
void appendThickSegment(sf::VertexArray& tris, const Vec2& a, const Vec2& b, float thickness,
                        const sf::Color& color);
