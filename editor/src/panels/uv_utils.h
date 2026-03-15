// uv_utils.h
// Private UV-mapping utilities shared by the 3D viewport and property inspector.
//
// UV scale convention (see sector_tessellator.cpp):
//   u = wallDistance / uvScale.x + uvOffset.x
//   v = (worldY - floorY) / uvScale.y + uvOffset.y
//
// So uvScale is in world-units per texture repeat.  A uvScale of 2 means the
// texture tiles once every 2 world units.  The default of 1 tiles once per unit.

#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace daedalus::editor
{

/// Texels per world unit assumed for pixel-perfect UV scale computation.
/// With this value a 64-pixel-wide texture tiles once per world unit and a
/// 128-pixel-wide texture tiles once per 2 world units.
/// Calibrate against the visual scale of your art assets.
constexpr float UV_TEXELS_PER_WORLD_UNIT = 64.0f;

/// Returns the uvScale that renders a texW×texH texture at 1:1 pixel density.
/// Each axis tiles once every (texDim / UV_TEXELS_PER_WORLD_UNIT) world units.
[[nodiscard]] inline glm::vec2
computePixelPerfectUVScale(uint32_t texW, uint32_t texH) noexcept
{
    return {
        static_cast<float>(texW) / UV_TEXELS_PER_WORLD_UNIT,
        static_cast<float>(texH) / UV_TEXELS_PER_WORLD_UNIT
    };
}

} // namespace daedalus::editor
