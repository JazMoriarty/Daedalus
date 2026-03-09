// editor_layer.h
// Editor-only scene-organisation types.  These are NOT stored in WorldMapData
// and have no runtime game effect; they exist purely to help the map author
// organise their work.

#pragma once

#include <glm/glm.hpp>
#include <string>

namespace daedalus::editor
{

// ─── EditorLayer ─────────────────────────────────────────────────────────────
// A named group of sectors.  Stored on EditMapDocument; not serialised to .dmap
// in Phase 1C (Phase 1D will add a versioned sidecar or dmap extension).

struct EditorLayer
{
    std::string name    = "Default";  ///< Display name (editable in Layers panel).
    bool        visible = true;       ///< false → sectors on this layer hidden in 2D.
    bool        locked  = false;      ///< true → sectors cannot be selected/moved.
};

// ─── PlayerStart ─────────────────────────────────────────────────────────────
// Initial player spawn point.  One per map.  Stored on EditMapDocument.

struct PlayerStart
{
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
    float     yaw      = 0.0f;  ///< Radians; 0 = looking toward +Z.
};

} // namespace daedalus::editor
