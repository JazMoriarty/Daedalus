// scene_settings.h
// Real-time lighting parameters stored on EditMapDocument.
// Gate 6a will expose these through a RenderSettingsPanel; for now the values
// are set to sensible defaults and read directly by Viewport3D.

#pragma once

#include <glm/glm.hpp>

namespace daedalus::editor
{

struct SceneSettings
{
    /// Directional (sun) light — normalised world-space vector *towards* the light.
    glm::vec3 sunDirection  = glm::vec3(0.371f, 0.928f, 0.278f); // pre-normalised ~(0.4,1,0.3)
    glm::vec3 sunColor      = glm::vec3(1.0f, 0.95f, 0.8f);
    float     sunIntensity  = 2.0f;

};

} // namespace daedalus::editor
