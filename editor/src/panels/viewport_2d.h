#pragma once

#include <glm/glm.hpp>

struct ImDrawList; // global-namespace forward declaration

namespace daedalus::editor
{

class EditMapDocument;
class IEditorTool;
class DrawSectorTool;

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
    void draw(EditMapDocument& doc,
              IEditorTool*     activeTool,
              DrawSectorTool*  drawTool);

    // ─── Camera state (accessed by DrawSectorTool overlay) ────────────────────

    [[nodiscard]] float     zoom()      const noexcept { return m_zoom; }
    [[nodiscard]] glm::vec2 panOffset() const noexcept { return m_panOffset; }
    [[nodiscard]] float     gridStep()  const noexcept { return m_gridStep; }

    void setGridStep   (float step) noexcept { m_gridStep    = step; }
    void setSnapEnabled(bool  on)   noexcept { m_snapEnabled = on;   }

private:
    glm::vec2 m_panOffset{400.0f, 300.0f};  ///< Canvas pixels from origin to map (0,0).
    float     m_zoom    = 20.0f;            ///< Pixels per map unit.

    float     m_gridStep    = 1.0f;  ///< Current grid snap increment (map units).
    bool      m_snapEnabled = true;  ///< Whether grid snapping is active.

    bool      m_panActive = false;
    glm::vec2 m_panStartMouse{};
    glm::vec2 m_panStartOffset{};

    bool m_firstFrame = true;  ///< Center the view on first draw.

    [[nodiscard]] glm::vec2 mapToScreen(glm::vec2 mapPos,
                                        glm::vec2 canvasMin) const noexcept;

    [[nodiscard]] glm::vec2 screenToMap(glm::vec2 screenPos,
                                        glm::vec2 canvasMin) const noexcept;

    void drawGrid(ImDrawList* dl,
                  glm::vec2 canvasMin, glm::vec2 canvasMax) const;

    void drawSectors(ImDrawList* dl,
                     const EditMapDocument& doc,
                     glm::vec2 canvasMin) const;
};

} // namespace daedalus::editor
