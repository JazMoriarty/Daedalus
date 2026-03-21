// vertex_tool.h
// Dedicated vertex-editing tool (V key).
//
// Unlike SelectTool, VertexTool only selects and drags wall vertices —
// clicking on empty space or a sector/wall does not change the selection.
// This gives a focused workflow for fine-tuning polygon shapes.

#pragma once

#include "daedalus/editor/i_editor_tool.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <vector>

namespace daedalus::editor
{

class VertexTool final : public IEditorTool
{
public:
    VertexTool()  = default;
    ~VertexTool() = default;

    void onMouseDown(EditMapDocument& doc,
                     float mapX, float mapZ,
                     int   button) override;

    void onMouseMove(EditMapDocument& doc,
                     float mapX, float mapZ) override;

    void onMouseUp  (EditMapDocument& doc,
                     float mapX, float mapZ,
                     int   button) override;

    /// Selects all wall vertices whose p0 lies inside the rectangle.
    void onRectSelect(EditMapDocument& doc,
                      glm::vec2 minCorner,
                      glm::vec2 maxCorner) override;

    /// True while any vertex drag (single or multi) is in progress.
    [[nodiscard]] bool            isDragging()    const noexcept { return m_dragging || m_multiDrag; }

    /// Sector/wall of the reference vertex when single-dragging.
    [[nodiscard]] world::SectorId dragSectorId()  const noexcept { return m_dragSectorId; }
    [[nodiscard]] std::size_t     dragWallIndex()  const noexcept { return m_dragWallIndex; }

private:
    /// Pick radius in map units — slightly larger than SelectTool's to make
    /// individual vertices easier to click in vertex-only mode.
    static constexpr float k_vertexRadius = 0.5f;

    // ─── Single-vertex drag state ───────────────────────────────────────────────
    bool            m_dragging      = false;
    world::SectorId m_dragSectorId  = world::INVALID_SECTOR_ID;
    std::size_t     m_dragWallIndex = 0;
    glm::vec2       m_dragOrigPos{};
    bool            m_dragMoved     = false;

    // ─── Multi-vertex drag state (rect-selected group) ─────────────────────────
    struct VertexSnap { world::SectorId sectorId; std::size_t wallIndex; glm::vec2 origPos; };
    bool                    m_multiDrag = false;
    glm::vec2               m_multiDragClickPos{};
    glm::vec2               m_multiDragFirstPos{};  ///< Anchor aligned to first mouse move.
    bool                    m_multiDragFirstMove = true;
    bool                    m_multiDragMoved     = false;
    std::vector<VertexSnap> m_multiOrig;            ///< Original positions of all selected verts.
};

} // namespace daedalus::editor
