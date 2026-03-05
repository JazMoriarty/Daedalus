#pragma once

#include "daedalus/editor/i_editor_tool.h"
#include "daedalus/world/map_data.h"

#include <glm/glm.hpp>
#include <cstddef>

namespace daedalus::editor
{

class SelectTool final : public IEditorTool
{
public:
    SelectTool()  = default;
    ~SelectTool() = default;

    void onMouseDown(EditMapDocument& doc,
                     float mapX, float mapZ,
                     int   button) override;

    void onMouseMove(EditMapDocument& doc,
                     float mapX, float mapZ) override;

    void onMouseUp  (EditMapDocument& doc,
                     float mapX, float mapZ,
                     int   button) override;

private:
    // ─── Hit thresholds (map units) ───────────────────────────────────────────
    static constexpr float k_vertexRadius    = 0.4f;   ///< Vertex pick radius.
    static constexpr float k_wallThresholdSq = 0.25f * 0.25f;  ///< Wall pick dist².

    // ─── Drag state ───────────────────────────────────────────────────────────
    bool            m_dragging       = false;
    world::SectorId m_dragSectorId   = world::INVALID_SECTOR_ID;
    std::size_t     m_dragWallIndex  = 0;
    glm::vec2       m_dragOrigPos{};
    bool            m_dragMoved      = false;  ///< True once the vertex actually moved.

    // ─── Hit testing ─────────────────────────────────────────────────────────
    [[nodiscard]] world::SectorId hitTestSector(
        const world::WorldMapData& map,
        float mapX, float mapZ) const noexcept;
};

} // namespace daedalus::editor
