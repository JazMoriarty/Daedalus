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

private:
    /// Pick radius in map units — slightly larger than SelectTool's to make
    /// individual vertices easier to click in vertex-only mode.
    static constexpr float k_vertexRadius = 0.5f;

    // ─── Drag state ───────────────────────────────────────────────────────────
    bool            m_dragging      = false;
    world::SectorId m_dragSectorId  = world::INVALID_SECTOR_ID;
    std::size_t     m_dragWallIndex = 0;
    glm::vec2       m_dragOrigPos{};
    bool            m_dragMoved     = false;  ///< True once the vertex has moved.
};

} // namespace daedalus::editor
