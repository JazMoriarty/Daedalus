#include "draw_sector_tool.h"
#include "geometry_utils.h"
#include "document/commands/cmd_draw_sector.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

#include "imgui.h"

#include <glm/glm.hpp>
#include <cmath>

namespace daedalus::editor
{

void DrawSectorTool::activate(EditMapDocument& /*doc*/)
{
    cancel();
}

void DrawSectorTool::deactivate(EditMapDocument& /*doc*/)
{
    cancel();
}

void DrawSectorTool::onMouseDown(EditMapDocument& doc,
                                  float mapX, float mapZ,
                                  int   button)
{
    if (button == 1)  // right-click → cancel
    {
        cancel();
        return;
    }
    if (button != 0) return;

    const glm::vec2 clicked{mapX, mapZ};

    // Snap-close: if we have ≥ 3 vertices and click near the first one, finish.
    if (m_vertices.size() >= 3)
    {
        const float dist = glm::length(clicked - m_vertices.front());
        if (dist <= k_snapRadius)
        {
            tryFinish(doc);
            return;
        }
    }

    m_vertices.push_back(clicked);
    m_cursor = clicked;
}

void DrawSectorTool::onMouseMove(EditMapDocument& /*doc*/,
                                  float mapX, float mapZ)
{
    m_cursor = {mapX, mapZ};
}

void DrawSectorTool::cancel()
{
    m_vertices.clear();
}

bool DrawSectorTool::tryFinish(EditMapDocument& doc)
{
    if (m_vertices.size() < 3)
    {
        doc.log("Draw Sector: need at least 3 vertices.");
        return false;
    }

    // Ensure CCW winding (positive signed area).
    if (geometry::signedArea(m_vertices) < 0.0f)
    {
        std::reverse(m_vertices.begin(), m_vertices.end());
    }

    if (geometry::isSelfIntersecting(m_vertices))
    {
        doc.log("Draw Sector: polygon is self-intersecting — cancelled.");
        cancel();
        return false;
    }

    // Build the Sector using the document's current defaults.
    world::Sector sector;
    sector.floorHeight = doc.defaultFloorHeight();
    sector.ceilHeight  = doc.defaultCeilHeight();
    for (const auto& v : m_vertices)
    {
        world::Wall wall;
        wall.p0 = v;
        sector.walls.push_back(wall);
    }

    doc.pushCommand(std::make_unique<CmdDrawSector>(doc, std::move(sector)));
    doc.appendSectorLayer();  // registers new sector on the active layer
    doc.log("Drew sector with " + std::to_string(m_vertices.size()) + " walls.");

    cancel();
    return true;
}

// ─── Overlay drawing ──────────────────────────────────────────────────────────

glm::vec2 DrawSectorTool::toScreen(glm::vec2 mapPos,
                                    float     zoom,
                                    glm::vec2 panOffset,
                                    glm::vec2 canvasMin) const noexcept
{
    return {
        canvasMin.x + mapPos.x * zoom + panOffset.x,
        canvasMin.y + mapPos.y * zoom + panOffset.y
    };
}

void DrawSectorTool::drawOverlay(ImDrawList* drawList,
                                  float       zoom,
                                  glm::vec2   panOffset,
                                  glm::vec2   canvasMin) const
{
    if (m_vertices.empty()) return;

    constexpr ImU32 lineColor   = IM_COL32(255, 200,  50, 220);
    constexpr ImU32 dotColor    = IM_COL32(255, 220, 100, 255);
    constexpr ImU32 snapColor   = IM_COL32( 80, 255, 120, 255);
    constexpr ImU32 ghostColor  = IM_COL32(255, 200,  50, 100);

    // Draw placed edges.
    for (std::size_t i = 0; i + 1 < m_vertices.size(); ++i)
    {
        const auto s0 = toScreen(m_vertices[i],     zoom, panOffset, canvasMin);
        const auto s1 = toScreen(m_vertices[i + 1], zoom, panOffset, canvasMin);
        drawList->AddLine({s0.x, s0.y}, {s1.x, s1.y}, lineColor, 2.0f);
    }

    // Ghost line from last placed vertex to cursor.
    {
        const auto last   = toScreen(m_vertices.back(), zoom, panOffset, canvasMin);
        const auto cursor = toScreen(m_cursor,          zoom, panOffset, canvasMin);
        drawList->AddLine({last.x, last.y}, {cursor.x, cursor.y}, ghostColor, 1.5f);

        // Closing line preview (cursor back to first vertex).
        if (m_vertices.size() >= 3)
        {
            const auto first = toScreen(m_vertices.front(), zoom, panOffset, canvasMin);
            drawList->AddLine({cursor.x, cursor.y}, {first.x, first.y}, ghostColor, 1.0f);
        }
    }

    // Draw vertex dots.
    for (std::size_t i = 0; i < m_vertices.size(); ++i)
    {
        const auto s = toScreen(m_vertices[i], zoom, panOffset, canvasMin);
        const bool isFirst = (i == 0);
        const float dist   = isFirst ? glm::length(m_cursor - m_vertices[i]) : -1.0f;
        const ImU32 col    = (isFirst && dist <= k_snapRadius) ? snapColor : dotColor;
        const float radius = isFirst ? 6.0f : 4.0f;
        drawList->AddCircleFilled({s.x, s.y}, radius, col);
    }
}

} // namespace daedalus::editor
