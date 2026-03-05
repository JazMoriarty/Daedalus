#include "viewport_2d.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/i_editor_tool.h"
#include "tools/draw_sector_tool.h"

#include "imgui.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace daedalus::editor
{

// ─── Coordinate helpers ───────────────────────────────────────────────────────

glm::vec2 Viewport2D::mapToScreen(glm::vec2 mapPos,
                                   glm::vec2 canvasMin) const noexcept
{
    return {
        canvasMin.x + mapPos.x * m_zoom + m_panOffset.x,
        canvasMin.y + mapPos.y * m_zoom + m_panOffset.y
    };
}

glm::vec2 Viewport2D::screenToMap(glm::vec2 screenPos,
                                   glm::vec2 canvasMin) const noexcept
{
    return {
        (screenPos.x - canvasMin.x - m_panOffset.x) / m_zoom,
        (screenPos.y - canvasMin.y - m_panOffset.y) / m_zoom
    };
}

// ─── Grid ─────────────────────────────────────────────────────────────────────

void Viewport2D::drawGrid(ImDrawList* dl,
                           glm::vec2   canvasMin,
                           glm::vec2   canvasMax) const
{
    constexpr float gridStepMap   = 1.0f;
    constexpr ImU32 minorColor    = IM_COL32(60, 60, 60, 180);
    constexpr ImU32 majorColor    = IM_COL32(90, 90, 90, 200);
    constexpr ImU32 axisColor     = IM_COL32(120, 120, 140, 220);

    const float step = gridStepMap * m_zoom;
    if (step < 4.0f) return;  // Grid too dense — skip.

    // The map origin (0,0) in screen space.
    const float originScreenX = canvasMin.x + m_panOffset.x;
    const float originScreenY = canvasMin.y + m_panOffset.y;

    // Compute first visible grid line.
    const auto firstX = static_cast<int>(std::floor((canvasMin.x - originScreenX) / step));
    const auto firstY = static_cast<int>(std::floor((canvasMin.y - originScreenY) / step));
    const auto lastX  = static_cast<int>(std::ceil ((canvasMax.x - originScreenX) / step));
    const auto lastY  = static_cast<int>(std::ceil ((canvasMax.y - originScreenY) / step));

    for (int xi = firstX; xi <= lastX; ++xi)
    {
        const float sx = originScreenX + xi * step;
        const ImU32 col = (xi == 0) ? axisColor : (xi % 5 == 0) ? majorColor : minorColor;
        dl->AddLine({sx, canvasMin.y}, {sx, canvasMax.y}, col, 1.0f);
    }
    for (int yi = firstY; yi <= lastY; ++yi)
    {
        const float sy = originScreenY + yi * step;
        const ImU32 col = (yi == 0) ? axisColor : (yi % 5 == 0) ? majorColor : minorColor;
        dl->AddLine({canvasMin.x, sy}, {canvasMax.x, sy}, col, 1.0f);
    }
}

// ─── Sector drawing ───────────────────────────────────────────────────────────

void Viewport2D::drawSectors(ImDrawList*            dl,
                               const EditMapDocument& doc,
                               glm::vec2              canvasMin) const
{
    const auto& map = doc.mapData();

    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const world::Sector& sector = map.sectors[si];
        if (sector.walls.size() < 3) continue;

        const bool selected = doc.selection().isSectorSelected(
            static_cast<world::SectorId>(si));

        // Build screen-space vertex list.
        std::vector<ImVec2> pts;
        pts.reserve(sector.walls.size());
        for (const auto& wall : sector.walls)
        {
            const auto s = mapToScreen(wall.p0, canvasMin);
            pts.push_back({s.x, s.y});
        }

        // Filled polygon.
        const ImU32 fillColor = selected
            ? IM_COL32(80, 130, 200, 60)
            : IM_COL32(50,  80, 120, 40);
        dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), fillColor);

        // Outline.
        const ImU32 lineColor = selected
            ? IM_COL32(120, 180, 255, 220)
            : IM_COL32( 80, 130, 200, 180);
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()),
                        lineColor, ImDrawFlags_Closed, 1.5f);

        // Vertex dots.
        for (const auto& pt : pts)
            dl->AddCircleFilled(pt, 3.0f, lineColor);
    }
}

// ─── Main draw ────────────────────────────────────────────────────────────────

void Viewport2D::draw(EditMapDocument& doc,
                       IEditorTool*     activeTool,
                       DrawSectorTool*  drawTool)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("2D Viewport");
    ImGui::PopStyleVar();

    // Reserve the canvas.
    ImVec2 canvasSz = ImGui::GetContentRegionAvail();
    if (canvasSz.x < 10.0f) canvasSz.x = 10.0f;
    if (canvasSz.y < 10.0f) canvasSz.y = 10.0f;

    const ImVec2 canvasMinV = ImGui::GetCursorScreenPos();
    const glm::vec2 canvasMin{canvasMinV.x, canvasMinV.y};
    const glm::vec2 canvasMax{canvasMin.x + canvasSz.x, canvasMin.y + canvasSz.y};

    // Center the view on first frame.
    if (m_firstFrame)
    {
        m_panOffset = {canvasSz.x * 0.5f, canvasSz.y * 0.5f};
        m_firstFrame = false;
    }

    // Invisible interaction target.
    ImGui::InvisibleButton("canvas2d", canvasSz,
                           ImGuiButtonFlags_MouseButtonLeft  |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);

    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    // ── Draw background + grid ────────────────────────────────────────────────
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({canvasMin.x, canvasMin.y},
                      {canvasMax.x, canvasMax.y},
                      IM_COL32(30, 30, 35, 255));
    dl->PushClipRect({canvasMin.x, canvasMin.y},
                     {canvasMax.x, canvasMax.y}, true);

    drawGrid(dl, canvasMin, canvasMax);
    drawSectors(dl, doc, canvasMin);

    // Ask the draw tool to render its overlay.
    if (drawTool)
        drawTool->drawOverlay(dl, m_zoom, m_panOffset, canvasMin);

    dl->PopClipRect();

    // ── Mouse input ───────────────────────────────────────────────────────────
    const ImVec2 mousePosV = ImGui::GetMousePos();
    const glm::vec2 mouseMap = screenToMap({mousePosV.x, mousePosV.y}, canvasMin);

    if (hovered)
    {
        // Zoom with scroll wheel.
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const float factor = std::pow(1.12f, wheel);
            // Zoom toward the mouse position.
            const glm::vec2 mouseCanvas{mousePosV.x - canvasMin.x,
                                        mousePosV.y - canvasMin.y};
            m_panOffset = mouseCanvas - (mouseCanvas - m_panOffset) * factor;
            m_zoom = std::clamp(m_zoom * factor, 2.0f, 400.0f);
        }

        // Tool mouse events.
        if (activeTool)
        {
            activeTool->onMouseMove(doc, mouseMap.x, mouseMap.y);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                activeTool->onMouseDown(doc, mouseMap.x, mouseMap.y, 0);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                activeTool->onMouseDown(doc, mouseMap.x, mouseMap.y, 1);
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                activeTool->onMouseUp(doc, mouseMap.x, mouseMap.y, 0);
        }
    }

    // Pan with middle mouse button drag (active anywhere on the canvas).
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        if (!m_panActive)
        {
            m_panActive      = true;
            m_panStartMouse  = {mousePosV.x, mousePosV.y};
            m_panStartOffset = m_panOffset;
        }
        const glm::vec2 delta{mousePosV.x - m_panStartMouse.x,
                               mousePosV.y - m_panStartMouse.y};
        m_panOffset = m_panStartOffset + delta;
    }
    else
    {
        m_panActive = false;
    }

    // Coordinates readout.
    ImGui::SetCursorScreenPos({canvasMin.x + 8.0f, canvasMax.y - 22.0f});
    ImGui::TextDisabled("(%.2f, %.2f)", mouseMap.x, mouseMap.y);

    ImGui::End();
}

} // namespace daedalus::editor
