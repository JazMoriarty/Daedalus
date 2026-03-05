#include "viewport_2d.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/editor/i_editor_tool.h"
#include "tools/draw_sector_tool.h"

#include "imgui.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

inline float snapToGrid(float v, float step) noexcept
{
    return std::round(v / step) * step;
}

} // anonymous namespace

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
    const auto&           map = doc.mapData();
    const SelectionState& sel = doc.selection();

    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const world::Sector& sector = map.sectors[si];
        const std::size_t    n      = sector.walls.size();
        if (n < 3) continue;

        const auto   sid      = static_cast<world::SectorId>(si);
        const bool   sectSel  = sel.isSectorSelected(sid);

        // Build screen-space vertex list.
        std::vector<ImVec2> pts;
        pts.reserve(n);
        for (const auto& wall : sector.walls)
        {
            const auto s = mapToScreen(wall.p0, canvasMin);
            pts.push_back({s.x, s.y});
        }

        // Filled polygon.
        const ImU32 fillColor = sectSel
            ? IM_COL32(80, 130, 200, 60)
            : IM_COL32(50,  80, 120, 40);
        dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), fillColor);

        // Per-wall outline — colour depends on: portal, selection, default.
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const world::Wall& wall    = sector.walls[wi];
            const ImVec2&      p0      = pts[wi];
            const ImVec2&      p1      = pts[(wi + 1) % n];

            const bool isPortal   = (wall.portalSectorId != world::INVALID_SECTOR_ID);
            const bool wallSel    = (sel.type == SelectionType::Wall &&
                                     sel.wallSectorId == sid && sel.wallIndex == wi);

            ImU32 edgeColor;
            float edgeThick;
            if (wallSel)
            {
                edgeColor = IM_COL32(255, 255, 255, 240);
                edgeThick = 2.5f;
            }
            else if (isPortal)
            {
                edgeColor = IM_COL32(0, 210, 210, 220);
                edgeThick = 2.5f;
            }
            else if (sectSel)
            {
                edgeColor = IM_COL32(120, 180, 255, 220);
                edgeThick = 1.5f;
            }
            else
            {
                edgeColor = IM_COL32(80, 130, 200, 180);
                edgeThick = 1.5f;
            }
            dl->AddLine(p0, p1, edgeColor, edgeThick);

            // Portal midpoint arrow (small cyan triangle).
            if (isPortal)
            {
                const float mx = (p0.x + p1.x) * 0.5f;
                const float my = (p0.y + p1.y) * 0.5f;
                // Perpendicular direction (pointing inward).
                const float dx = p1.x - p0.x;
                const float dy = p1.y - p0.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.5f)
                {
                    const float nx = -dy / len;
                    const float ny =  dx / len;
                    constexpr float sz = 5.0f;
                    const ImVec2 tip  = {mx + nx * sz,       my + ny * sz};
                    const ImVec2 bl   = {mx - nx * 2.0f - dx * 0.12f,
                                         my - ny * 2.0f - dy * 0.12f};
                    const ImVec2 br   = {mx - nx * 2.0f + dx * 0.12f,
                                         my - ny * 2.0f + dy * 0.12f};
                    dl->AddTriangleFilled(tip, bl, br, IM_COL32(0, 210, 210, 200));
                }
            }
        }

        // Vertex dots.
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const bool vertSel = (sel.type == SelectionType::Vertex &&
                                  sel.vertexSectorId == sid &&
                                  sel.vertexWallIndex == wi);
            const float  radius = vertSel ? 6.0f : 3.0f;
            const ImU32  col    = vertSel
                ? IM_COL32(255, 255, 180, 255)
                : (sectSel ? IM_COL32(120, 180, 255, 220)
                           : IM_COL32( 80, 130, 200, 180));
            dl->AddCircleFilled(pts[wi], radius, col);
        }
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
    const glm::vec2 mouseMapRaw = screenToMap({mousePosV.x, mousePosV.y}, canvasMin);

    // Apply grid snap (disabled when Shift is held).
    const bool shiftHeld = ImGui::GetIO().KeyShift;
    const bool doSnap    = m_snapEnabled && !shiftHeld;
    const glm::vec2 mouseMap = doSnap
        ? glm::vec2{snapToGrid(mouseMapRaw.x, m_gridStep),
                    snapToGrid(mouseMapRaw.y, m_gridStep)}
        : mouseMapRaw;

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

        // Tool mouse events (snapped coordinates).
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

    // Coordinates readout (show snapped position, mark with dot when snapping).
    ImGui::SetCursorScreenPos({canvasMin.x + 8.0f, canvasMax.y - 22.0f});
    ImGui::TextDisabled(doSnap ? "%.2f, %.2f [%.2g]" : "%.2f, %.2f",
                        mouseMap.x, mouseMap.y, m_gridStep);

    ImGui::End();
}

} // namespace daedalus::editor
