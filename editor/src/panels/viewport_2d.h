#pragma once

#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"
#include "document/map_doctor.h"

#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

struct ImDrawList; // global-namespace forward declaration
struct ImVec2;

namespace daedalus::editor
{

class EditMapDocument;
class IEditorTool;
class DrawSectorTool;
class SelectTool;

/// 2D top-down viewport.
/// Draws the map using ImGui DrawList and dispatches mouse input to the
/// active tool after converting screen coordinates to map coordinates.
class Viewport2D
{
public:
    Viewport2D()  = default;
    ~Viewport2D() = default;

    /// Draw the panel for this frame.
    /// activeTool may be nullptr.
    /// drawTool is used for the in-progress polygon overlay and may be nullptr.
    /// selectTool is non-null only when the select tool is the active tool;
    /// used for rect-select overlay and drag detection.
    void draw(EditMapDocument& doc,
              IEditorTool*     activeTool,
              DrawSectorTool*  drawTool,
              SelectTool*      selectTool);

    // ─── Camera state (accessed by DrawSectorTool overlay) ────────────────────

    [[nodiscard]] float     zoom()      const noexcept { return m_zoom; }
    [[nodiscard]] glm::vec2 panOffset() const noexcept { return m_panOffset; }
    [[nodiscard]] float     gridStep()  const noexcept { return m_gridStep; }

    void setGridStep   (float step) noexcept { m_gridStep    = step; }
    void setSnapEnabled(bool  on)   noexcept { m_snapEnabled = on;   }
    void setGridVisible(bool  on)   noexcept { m_gridVisible = on;   }
    [[nodiscard]] bool gridVisible() const noexcept { return m_gridVisible; }

    /// Last raw (unsnapped) map-space mouse position, updated every draw().
    /// Used by main.mm when placing objects via keyboard shortcut.
    [[nodiscard]] glm::vec2 lastMouseMapPos() const noexcept { return m_lastMouseMapPos; }

    /// Fit pan and zoom so all sectors in `map` are visible.
    /// No-op if the map has no sectors.
    void fitToView(const world::WorldMapData& map) noexcept;

    /// Request that the rotate-sector popup be opened for the given sector on
    /// the next draw() call.  Dispatched from main.mm on R key.
    void openRotatePopup(world::SectorId sectorId) noexcept;

    /// Expose rect-select state so the select tool can draw the overlay.
    [[nodiscard]] bool      isRectSelecting()   const noexcept { return m_rectSelActive; }
    [[nodiscard]] glm::vec2 rectSelectAnchor()  const noexcept { return m_rectSelAnchor; }
    [[nodiscard]] glm::vec2 rectSelectCurrent() const noexcept { return m_rectSelCurrent; }

private:
    glm::vec2 m_panOffset{400.0f, 300.0f};  ///< Canvas pixels from origin to map (0,0).
    float     m_zoom    = 20.0f;            ///< Pixels per map unit.

    float     m_gridStep    = 1.0f;  ///< Current grid snap increment (map units).
    bool      m_snapEnabled = true;  ///< Whether grid snapping is active.
    bool      m_gridVisible = true;  ///< Whether the grid is drawn.

    bool      m_panActive = false;
    glm::vec2 m_panStartMouse{};
    glm::vec2 m_panStartOffset{};

    // Entity drag state
    bool        m_entityDragActive = false;   ///< True while dragging an entity.
    std::size_t m_entityDragIdx    = 0;       ///< Index of the entity being dragged.
    glm::vec3   m_entityDragOrigin = {};      ///< World-space position at drag start.

    // Light drag state
    bool        m_lightDragActive = false;  ///< True while dragging a light.
    std::size_t m_lightDragIdx    = 0;      ///< Index of the light being dragged.
    glm::vec3   m_lightDragOrigin = {};     ///< World-space position at drag start.

    // Player start drag / rotate state
    bool      m_psDragActive = false;   ///< True while translating the player start (LMB drag).
    glm::vec3 m_psDragOrigin = {};      ///< Player position at drag start.
    bool      m_psRotActive  = false;   ///< True while rotating the player start (RMB drag).
    float     m_psRotOrigin  = 0.0f;    ///< Yaw at rotate start (radians).

    // Drag-rectangle selection state
    bool      m_rectSelActive  = false;  ///< True while a rect-select drag is in progress.
    glm::vec2 m_rectSelAnchor{};         ///< Anchor corner in map space.
    glm::vec2 m_rectSelCurrent{};        ///< Moving corner in map space (updated per frame).

    // Rotate popup
    bool            m_rotatePopupOpen     = false;
    world::SectorId m_rotatePopupSectorId = 0;
    float           m_rotatePopupAngleDeg = 45.0f;

    // Continuous validation overlay cache.
    std::vector<WallHighlight> m_wallHighlights;    ///< Last computed highlights.
    bool                       m_highlightsDirty = true; ///< Recompute on next draw.

    bool      m_firstFrame      = true;   ///< Center the view on first draw.
    glm::vec2 m_lastMouseMapPos = {};     ///< Raw map position of the cursor (updated per frame).

    [[nodiscard]] glm::vec2 mapToScreen(glm::vec2 mapPos,
                                        glm::vec2 canvasMin) const noexcept;

    [[nodiscard]] glm::vec2 screenToMap(glm::vec2 screenPos,
                                        glm::vec2 canvasMin) const noexcept;

    void drawGrid(ImDrawList* dl,
                  glm::vec2 canvasMin, glm::vec2 canvasMax) const;

    void drawSectors(ImDrawList* dl,
                     const EditMapDocument& doc,
                     glm::vec2 canvasMin) const;

    void drawEntities(ImDrawList* dl,
                      const EditMapDocument& doc,
                      glm::vec2 canvasMin) const;
};

} // namespace daedalus::editor
