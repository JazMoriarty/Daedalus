#pragma once

#include "daedalus/editor/i_editor_tool.h"

#include <glm/glm.hpp>
#include <vector>

struct ImDrawList;  // forward-declare to avoid full imgui.h include in header

namespace daedalus::editor
{

/// Interactive sector-drawing tool.
///
/// Usage:
///   • Left-click to place polygon vertices one at a time.
///   • Left-click within snapRadius map-units of the first vertex (or press Enter)
///     to close and commit the sector.
///   • Right-click or press Escape to cancel without committing.
///   • At least 3 vertices are required to commit.
class DrawSectorTool final : public IEditorTool
{
public:
    DrawSectorTool()  = default;
    ~DrawSectorTool() = default;

    void activate  (EditMapDocument& doc) override;
    void deactivate(EditMapDocument& doc) override;

    void onMouseDown(EditMapDocument& doc,
                     float mapX, float mapZ, int button) override;

    void onMouseMove(EditMapDocument& doc,
                     float mapX, float mapZ) override;

    // ─── Overlay ──────────────────────────────────────────────────────────────
    // Called by Viewport2D each frame to render the in-progress polygon.
    // `toScreen` converts map coordinates to absolute screen coordinates.

    void drawOverlay(ImDrawList*    drawList,
                     float          zoom,
                     glm::vec2      panOffset,
                     glm::vec2      canvasMin) const;

    // ─── State queries ────────────────────────────────────────────────────────

    [[nodiscard]] bool isDrawing() const noexcept { return !m_vertices.empty(); }
    [[nodiscard]] const std::vector<glm::vec2>& vertices() const noexcept { return m_vertices; }

    /// Cancel any in-progress polygon without committing.
    void cancel();

    /// Try to close the polygon and commit a CmdDrawSector.
    /// Returns true if the polygon was valid and committed.
    bool tryFinish(EditMapDocument& doc);

private:
    std::vector<glm::vec2> m_vertices;
    glm::vec2              m_cursor{};

    static constexpr float k_snapRadius = 0.6f;  ///< Map units to snap-close.

    [[nodiscard]] glm::vec2 toScreen(glm::vec2 mapPos,
                                     float     zoom,
                                     glm::vec2 panOffset,
                                     glm::vec2 canvasMin) const noexcept;
};

} // namespace daedalus::editor
