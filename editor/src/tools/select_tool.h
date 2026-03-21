#pragma once

#include "daedalus/editor/i_editor_tool.h"
#include "daedalus/world/map_data.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <optional>
#include <vector>

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

    /// Selects all sectors that have at least one vertex inside the rectangle.
    void onRectSelect(EditMapDocument& doc,
                      glm::vec2 minCorner,
                      glm::vec2 maxCorner) override;

    /// True while an element (vertex, wall, or sector) is being live-dragged.
    [[nodiscard]] bool isDragging() const noexcept { return m_dragTarget != DragTarget::None; }

    /// True while a vertex specifically is being dragged (for vertex snap).
    [[nodiscard]] bool isDraggingVertex() const noexcept { return m_dragTarget == DragTarget::Vertex; }

    /// Sector/wall of the vertex currently being dragged (valid only when isDraggingVertex()).
    [[nodiscard]] world::SectorId dragSectorId()  const noexcept { return m_dragSectorId; }
    [[nodiscard]] std::size_t     dragWallIndex()  const noexcept { return m_dragWallIndex; }

private:
    // ─── Hit thresholds (map units) ───────────────────────────────────────────
    static constexpr float k_vertexRadius    = 0.4f;          ///< Vertex pick radius.
    static constexpr float k_wallThresholdSq = 0.25f * 0.25f; ///< Wall pick dist².

    // ─── Drag state ───────────────────────────────────────────────────────────
    enum class DragTarget { None, Vertex, Wall, Sector };

    DragTarget              m_dragTarget    = DragTarget::None;
    world::SectorId         m_dragSectorId  = world::INVALID_SECTOR_ID;
    std::size_t             m_dragWallIndex = 0;  ///< Wall index for Vertex and Wall drag.
    glm::vec2               m_dragOrigPos{};      ///< Vertex: orig p0; Wall: orig wall[i].p0.
    glm::vec2               m_dragOrig2{};        ///< Wall drag: orig p0 of wall[(i+1)%n].
    glm::vec2               m_dragClickPos{};     ///< Wall/Sector: map pos at drag start.
    std::vector<glm::vec2>  m_dragOrigAll{};      ///< Sector drag: orig p0 for every wall.
    /// Sector drag: saved curveControlA/B for every wall (nullopt if the wall has no curve).
    std::vector<std::optional<glm::vec2>> m_dragOrigCurveA;
    std::vector<std::optional<glm::vec2>> m_dragOrigCurveB;
    /// Wall slide drag: saved curveControlA/B for the dragged wall.
    std::optional<glm::vec2> m_dragOrigWallCurveA;
    std::optional<glm::vec2> m_dragOrigWallCurveB;
    bool                    m_dragMoved     = false; ///< True once target actually moved.
    bool                    m_dragFirstMove = true;  ///< True until onMouseMove fires; aligns
                                                     ///<  click anchor to grid-snapped position.

    // ─── Hit testing ─────────────────────────────────────────────────────────
    [[nodiscard]] world::SectorId hitTestSector(
        const world::WorldMapData& map,
        float mapX, float mapZ) const noexcept;
};

} // namespace daedalus::editor
