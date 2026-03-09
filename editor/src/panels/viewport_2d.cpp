#include "viewport_2d.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/editor/light_def.h"
#include "daedalus/editor/entity_def.h"
#include "daedalus/editor/editor_layer.h"
#include "daedalus/editor/i_editor_tool.h"
#include "tools/draw_sector_tool.h"
#include "tools/select_tool.h"
#include "document/commands/cmd_move_entity.h"
#include "document/commands/cmd_move_light.h"
#include "document/commands/cmd_set_player_start.h"
#include "document/commands/cmd_split_wall.h"
#include "document/commands/cmd_rotate_sector.h"

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
        // Skip sectors on hidden layers.
        const uint32_t  layerIdx = doc.sectorLayerIndex(si);
        const auto&     layers   = doc.layers();
        if (layerIdx < static_cast<uint32_t>(layers.size()) && !layers[layerIdx].visible)
            continue;

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

// ─── Entity icons ─────────────────────────────────────────────────────────────

void Viewport2D::drawEntities(ImDrawList*            dl,
                               const EditMapDocument& doc,
                               glm::vec2              canvasMin) const
{
    constexpr float kIconR = 7.0f;

    const auto&           entities = doc.entities();
    const SelectionState& sel      = doc.selection();

    for (std::size_t ei = 0; ei < entities.size(); ++ei)
    {
        const EntityDef& ed  = entities[ei];
        const auto       sp  = mapToScreen({ed.position.x, ed.position.z}, canvasMin);
        const ImVec2     spi {sp.x, sp.y};

        const bool selected = (sel.type == SelectionType::Entity &&
                               sel.entityIndex == ei);

        // Fill colour by visual type.
        ImU32 fillCol;
        switch (ed.visualType)
        {
        case EntityVisualType::BillboardBlended:  fillCol = IM_COL32(255, 200,  80, selected ? 255 : 160); break;
        case EntityVisualType::AnimatedBillboard: fillCol = IM_COL32( 80, 220,  80, selected ? 255 : 220); break;
        case EntityVisualType::VoxelObject:       fillCol = IM_COL32( 80, 130, 220, selected ? 255 : 220); break;
        case EntityVisualType::StaticMesh:        fillCol = IM_COL32(180,  80, 220, selected ? 255 : 220); break;
        case EntityVisualType::Decal:             fillCol = IM_COL32(180, 120,  60, selected ? 255 : 220); break;
        case EntityVisualType::ParticleEmitter:   fillCol = IM_COL32( 80, 220, 220, selected ? 255 : 220); break;
        default: /* BillboardCutout */            fillCol = IM_COL32(255, 160,  50, selected ? 255 : 220); break;
        }

        dl->AddCircleFilled(spi, kIconR, fillCol);
        // Thin outline for legibility against any background.
        dl->AddCircle(spi, kIconR, IM_COL32(200, 200, 200, 100), 16, 1.0f);
        // Centre dot.
        dl->AddCircleFilled(spi, 2.0f, IM_COL32(255, 255, 255, 200));

        // Selection ring.
        if (selected)
            dl->AddCircle(spi, kIconR + 4.0f, IM_COL32(255, 255, 255, 200), 24, 1.5f);
    }
}

// ─── openRotatePopup ────────────────────────────────────────────────────────────

void Viewport2D::openRotatePopup(world::SectorId sectorId) noexcept
{
    m_rotatePopupOpen     = true;
    m_rotatePopupSectorId = sectorId;
    m_rotatePopupAngleDeg = 45.0f;
}

// ─── Main draw ────────────────────────────────────────────────────────────────

void Viewport2D::draw(EditMapDocument& doc,
                       IEditorTool*     activeTool,
                       DrawSectorTool*  drawTool,
                       SelectTool*      selectTool)
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

    if (m_gridVisible)
        drawGrid(dl, canvasMin, canvasMax);
    drawSectors(dl, doc, canvasMin);

    // ── Continuous validation overlay ─────────────────────────────────────
    if (doc.isGeometryDirty())
        m_highlightsDirty = true;

    if (m_highlightsDirty)
    {
        m_wallHighlights  = getWallHighlights(doc.mapData());
        m_highlightsDirty = false;
    }

    if (!m_wallHighlights.empty())
    {
        const auto& sectors = doc.mapData().sectors;
        for (const auto& h : m_wallHighlights)
        {
            if (h.sectorId >= sectors.size()) continue;
            const auto& sector = sectors[h.sectorId];
            const std::size_t n = sector.walls.size();
            if (h.wallIndex >= n) continue;

            const glm::vec2 p0s = mapToScreen(sector.walls[h.wallIndex].p0, canvasMin);
            const glm::vec2 p1s = mapToScreen(sector.walls[(h.wallIndex + 1) % n].p0, canvasMin);

            ImU32 col;
            switch (h.kind)
            {
            case WallHighlightKind::ZeroLength:
            case WallHighlightKind::SelfIntersecting:
                col = IM_COL32(255, 60, 60, 210); break;   // red
            case WallHighlightKind::OrphanedPortal:
                col = IM_COL32(255, 140, 0, 210); break;   // orange
            case WallHighlightKind::MissingBackLink:
                col = IM_COL32(255, 220, 0, 210); break;   // yellow
            }
            dl->AddLine({p0s.x, p0s.y}, {p1s.x, p1s.y}, col, 2.5f);
        }
    }

    drawEntities(dl, doc, canvasMin);

    // Ask the draw tool to render its overlay.
    if (drawTool)
        drawTool->drawOverlay(dl, m_zoom, m_panOffset, canvasMin);

    // ── Light icons ───────────────────────────────────────────────────────────
    {
        const auto&           lights = doc.lights();
        const SelectionState& sel    = doc.selection();
        constexpr float kIconR = 8.0f;  ///< Fixed icon radius in screen pixels.

        for (std::size_t li = 0; li < lights.size(); ++li)
        {
            const LightDef& ld = lights[li];
            const auto sp = mapToScreen({ld.position.x, ld.position.z}, canvasMin);
            const ImVec2 spi{sp.x, sp.y};

            const bool lightSel = (sel.type == SelectionType::Light &&
                                   sel.lightIndex == li);

            // Influence-radius ring.
            const float ringR = ld.radius * m_zoom;
            if (ringR > kIconR + 2.0f)
                dl->AddCircle(spi, ringR,
                              lightSel ? IM_COL32(255, 220, 80, 120)
                                       : IM_COL32(255, 180, 50, 60),
                              48, 1.5f);

            // Icon fill.
            const ImU32 fillCol = lightSel
                ? IM_COL32(255, 220, 80, 255)
                : IM_COL32(255, 190, 50, 210);
            dl->AddCircleFilled(spi, kIconR, fillCol);

            // White centre dot.
            dl->AddCircleFilled(spi, 2.5f, IM_COL32(255, 255, 255, 240));

            // Selection ring.
            if (lightSel)
                dl->AddCircle(spi, kIconR + 3.0f, IM_COL32(255, 255, 255, 200), 24, 1.5f);
        }
    }

    // ── Player start marker ───────────────────────────────────────────────────
    if (const auto& ps = doc.playerStart(); ps.has_value())
    {
        const auto   sp  = mapToScreen({ps->position.x, ps->position.z}, canvasMin);
        const ImVec2 spi {sp.x, sp.y};

        dl->AddCircle(spi, 9.0f, IM_COL32(80, 220, 100, 220), 24, 2.0f);
        dl->AddCircleFilled(spi, 5.0f, IM_COL32(80, 220, 100, 180));

        // Direction arrow — yaw=0 points toward +Z (down in top-down 2D).
        const float  yaw    = ps->yaw;
        const float  arrLen = 16.0f;
        const ImVec2 tip  {sp.x + std::sin(yaw) * arrLen,
                           sp.y + std::cos(yaw) * arrLen};
        dl->AddLine(spi, tip, IM_COL32(80, 220, 100, 240), 2.0f);

        // Arrow head.
        const float nx =  std::cos(yaw);
        const float ny = -std::sin(yaw);
        dl->AddTriangleFilled(
            tip,
            ImVec2{tip.x - std::sin(yaw) * 5.0f + nx * 4.0f,
                   tip.y - std::cos(yaw) * 5.0f + ny * 4.0f},
            ImVec2{tip.x - std::sin(yaw) * 5.0f - nx * 4.0f,
                   tip.y - std::cos(yaw) * 5.0f - ny * 4.0f},
            IM_COL32(80, 220, 100, 240));
    }

    // ── Drag-rectangle selection overlay ─────────────────────────────────────
    if (m_rectSelActive)
    {
        const glm::vec2 scrMin = mapToScreen(
            {std::min(m_rectSelAnchor.x, m_rectSelCurrent.x),
             std::min(m_rectSelAnchor.y, m_rectSelCurrent.y)}, canvasMin);
        const glm::vec2 scrMax = mapToScreen(
            {std::max(m_rectSelAnchor.x, m_rectSelCurrent.x),
             std::max(m_rectSelAnchor.y, m_rectSelCurrent.y)}, canvasMin);
        dl->AddRectFilled({scrMin.x, scrMin.y}, {scrMax.x, scrMax.y},
                          IM_COL32(120, 180, 255, 30));
        dl->AddRect({scrMin.x, scrMin.y}, {scrMax.x, scrMax.y},
                    IM_COL32(120, 180, 255, 200), 0.0f, 0, 1.5f);
    }

    dl->PopClipRect();

    // ── Mouse input
    const ImVec2 mousePosV = ImGui::GetMousePos();
    const glm::vec2 mouseMapRaw = screenToMap({mousePosV.x, mousePosV.y}, canvasMin);
    m_lastMouseMapPos = mouseMapRaw;  // expose to main.mm for keyboard-triggered placement

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
            {
                const ImGuiIO& clickIO = ImGui::GetIO();
                constexpr float kHitR = 10.0f;  ///< Icon hit radius in screen pixels.

                // Alt+LMB: split the nearest wall at the clicked map position.
                bool handledByAlt = false;
                if (clickIO.KeyAlt)
                {
                    constexpr float kWallPickSq = 12.0f * 12.0f;  // screen pixels²
                    float           bestSq      = kWallPickSq;
                    world::SectorId bestSid     = world::INVALID_SECTOR_ID;
                    std::size_t     bestWi      = 0;

                    const auto& map = doc.mapData();
                    for (std::size_t si = 0; si < map.sectors.size(); ++si)
                    {
                        const auto&       sec = map.sectors[si];
                        const std::size_t n   = sec.walls.size();
                        for (std::size_t wi = 0; wi < n; ++wi)
                        {
                            const glm::vec2 a  = mapToScreen(sec.walls[wi].p0, canvasMin);
                            const glm::vec2 b  = mapToScreen(sec.walls[(wi+1)%n].p0, canvasMin);
                            const glm::vec2 mp {mousePosV.x, mousePosV.y};
                            const glm::vec2 ab = b - a;
                            const float     ls = glm::dot(ab, ab);
                            const float     t  = ls > 0.0f
                                ? std::clamp(glm::dot(mp - a, ab) / ls, 0.0f, 1.0f)
                                : 0.0f;
                            const glm::vec2 d  = mp - (a + ab * t);
                            const float     sq = glm::dot(d, d);
                            if (sq < bestSq)
                            {
                                bestSq  = sq;
                                bestSid = static_cast<world::SectorId>(si);
                                bestWi  = wi;
                            }
                        }
                    }
                    if (bestSid != world::INVALID_SECTOR_ID)
                    {
                        doc.pushCommand(std::make_unique<CmdSplitWall>(
                            doc, bestSid, bestWi, mouseMap));
                        handledByAlt = true;
                    }
                }

                if (!handledByAlt)
                {
                    // Check player start icon (unique object — check first).
                    bool hitPlayerStart = false;
                    if (const auto& ps = doc.playerStart(); ps.has_value())
                    {
                        const auto  sp = mapToScreen({ps->position.x, ps->position.z},
                                                    canvasMin);
                        const float dx = mousePosV.x - sp.x;
                        const float dy = mousePosV.y - sp.y;
                        if (dx * dx + dy * dy <= kHitR * kHitR)
                        {
                            SelectionState& sel = doc.selection();
                            sel.clear();
                            sel.type       = SelectionType::PlayerStart;
                            hitPlayerStart = true;
                            // Start translate drag.
                            m_psDragActive = true;
                            m_psDragOrigin = ps->position;
                        }
                    }

                    if (!hitPlayerStart)
                    {
                    // Check entity icons first (entities are more common than lights).
                    bool hitEntity = false;
                    const auto& entities = doc.entities();
                    for (std::size_t ei = 0; ei < entities.size() && !hitEntity; ++ei)
                    {
                        const EntityDef& ed = entities[ei];
                        const auto sp = mapToScreen({ed.position.x, ed.position.z}, canvasMin);
                        const float dx = mousePosV.x - sp.x;
                        const float dy = mousePosV.y - sp.y;
                        if (dx * dx + dy * dy <= kHitR * kHitR)
                        {
                            SelectionState& sel = doc.selection();
                            sel.clear();
                            sel.type        = SelectionType::Entity;
                            sel.entityIndex = ei;
                            hitEntity           = true;
                            m_entityDragActive  = true;
                            m_entityDragIdx     = ei;
                            m_entityDragOrigin  = doc.entities()[ei].position;
                        }
                    }

                    if (!hitEntity)
                    {
                        // Check for light icon click.
                        bool hitLight = false;
                        const auto& lights = doc.lights();
                        for (std::size_t li = 0; li < lights.size() && !hitLight; ++li)
                        {
                            const LightDef& ld = lights[li];
                            const auto sp = mapToScreen({ld.position.x, ld.position.z}, canvasMin);
                            const float dx = mousePosV.x - sp.x;
                            const float dy = mousePosV.y - sp.y;
                            if (dx * dx + dy * dy <= kHitR * kHitR)
                            {
                                SelectionState& sel = doc.selection();
                                sel.clear();
                                sel.type       = SelectionType::Light;
                                sel.lightIndex = li;
                                hitLight          = true;
                                m_lightDragActive = true;
                                m_lightDragIdx    = li;
                                m_lightDragOrigin = ld.position;
                            }
                        }
                        if (!hitLight)
                        {
                            activeTool->onMouseDown(doc, mouseMap.x, mouseMap.y, 0);
                            // Start drag-rect if select tool is active and no geometry drag started.
                            if (selectTool && !selectTool->isDragging())
                            {
                                m_rectSelActive  = true;
                                m_rectSelAnchor  = mouseMap;
                                m_rectSelCurrent = mouseMap;
                            }
                        }
                    }
                }
            }  // if (!handledByAlt)
            }  // if (IsMouseClicked LMB)
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                // RMB on a selected player start icon → begin yaw rotate drag.
                constexpr float kHitRPs = 10.0f;  // screen-pixel hit radius
                bool handledByPs = false;
                if (doc.selection().type == SelectionType::PlayerStart)
                {
                    if (const auto& ps = doc.playerStart(); ps.has_value())
                    {
                        const auto  sp = mapToScreen({ps->position.x, ps->position.z},
                                                     canvasMin);
                        const float dx = mousePosV.x - sp.x;
                        const float dy = mousePosV.y - sp.y;
                        if (dx * dx + dy * dy <= kHitRPs * kHitRPs)
                        {
                            m_psRotActive = true;
                            m_psRotOrigin = ps->yaw;
                            handledByPs   = true;
                        }
                    }
                }
                if (!handledByPs)
                    activeTool->onMouseDown(doc, mouseMap.x, mouseMap.y, 1);
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                if (m_psDragActive)
                {
                    // Commit the player start move if position actually changed.
                    if (doc.playerStart().has_value())
                    {
                        const PlayerStart& finalPs = *doc.playerStart();
                        if (finalPs.position != m_psDragOrigin)
                        {
                            PlayerStart oldPs = finalPs;
                            oldPs.position    = m_psDragOrigin;
                            doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
                                doc,
                                std::make_optional(oldPs),
                                std::make_optional(finalPs)));
                        }
                    }
                    m_psDragActive = false;
                }
                else if (m_entityDragActive)
                {
                    // Commit drag as a command if position actually changed.
                    const glm::vec3 finalPos = doc.entities()[m_entityDragIdx].position;
                    if (finalPos != m_entityDragOrigin)
                        doc.pushCommand(std::make_unique<CmdMoveEntity>(
                            doc, m_entityDragIdx, m_entityDragOrigin, finalPos));
                    m_entityDragActive = false;
                }
                else if (m_lightDragActive)
                {
                    const auto& lights = doc.lights();
                    if (m_lightDragIdx < lights.size())
                    {
                        const glm::vec3 finalPos = lights[m_lightDragIdx].position;
                        if (finalPos != m_lightDragOrigin)
                            doc.pushCommand(std::make_unique<CmdMoveLight>(
                                doc, m_lightDragIdx, m_lightDragOrigin, finalPos));
                    }
                    m_lightDragActive = false;
                }
                else if (m_rectSelActive)
                {
                    m_rectSelActive = false;
                    // Only dispatch if the drag was larger than a trivial nudge.
                    constexpr float kMinDragMapSq = 0.1f;  // map units²
                    const glm::vec2 d = m_rectSelCurrent - m_rectSelAnchor;
                    if (glm::dot(d, d) > kMinDragMapSq)
                    {
                        const glm::vec2 minC {std::min(m_rectSelAnchor.x, m_rectSelCurrent.x),
                                              std::min(m_rectSelAnchor.y, m_rectSelCurrent.y)};
                        const glm::vec2 maxC {std::max(m_rectSelAnchor.x, m_rectSelCurrent.x),
                                              std::max(m_rectSelAnchor.y, m_rectSelCurrent.y)};
                        activeTool->onRectSelect(doc, minC, maxC);
                    }
                    // else: trivial click — onMouseDown already ran and cleared selection.
                }
                else
                {
                    activeTool->onMouseUp(doc, mouseMap.x, mouseMap.y, 0);
                }
            }
        }
    }

    // Entity live drag — update position while left button held.
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_entityDragActive)
    {
        if (m_entityDragIdx < doc.entities().size())
        {
            EntityDef& ed  = doc.entities()[m_entityDragIdx];
            ed.position.x  = mouseMap.x;
            ed.position.z  = mouseMap.y;
            doc.markEntityDirty();
        }
    }

    // Light live drag.
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_lightDragActive)
    {
        if (m_lightDragIdx < doc.lights().size())
        {
            LightDef& ld  = doc.lights()[m_lightDragIdx];
            ld.position.x = mouseMap.x;
            ld.position.z = mouseMap.y;
            doc.markDirty();
        }
    }

    // Player start live move (LMB drag).
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_psDragActive)
    {
        if (doc.playerStart().has_value())
        {
            PlayerStart newPs  = *doc.playerStart();
            newPs.position.x   = mouseMap.x;
            newPs.position.z   = mouseMap.y;
            doc.setPlayerStart(newPs);
            doc.markDirty();
        }
    }

    // Player start live rotate (RMB drag — aim toward mouse cursor).
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Right) && m_psRotActive)
    {
        if (doc.playerStart().has_value())
        {
            const auto& ps = *doc.playerStart();
            const glm::vec2 sp = mapToScreen({ps.position.x, ps.position.z}, canvasMin);
            const float dx = mousePosV.x - sp.x;
            const float dy = mousePosV.y - sp.y;
            if (std::abs(dx) + std::abs(dy) > 2.0f)
            {
                PlayerStart newPs = ps;
                newPs.yaw = std::atan2(dx, dy);
                doc.setPlayerStart(newPs);
                doc.markDirty();
            }
        }
    }

    // Commit player start rotate when RMB is released.
    if (m_psRotActive && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        if (doc.playerStart().has_value())
        {
            const PlayerStart& finalPs = *doc.playerStart();
            if (finalPs.yaw != m_psRotOrigin)
            {
                PlayerStart oldPs = finalPs;
                oldPs.yaw = m_psRotOrigin;
                doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
                    doc,
                    std::make_optional(oldPs),
                    std::make_optional(finalPs)));
            }
        }
        m_psRotActive = false;
    }

    // Rect-select tracking — update current corner while dragging.
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_rectSelActive)
        m_rectSelCurrent = mouseMap;

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

    // ── Rotate sector popup ──────────────────────────────────────────────────
    if (m_rotatePopupOpen)
    {
        ImGui::OpenPopup("Rotate Sector##rot");
        m_rotatePopupOpen = false;
    }
    if (ImGui::BeginPopupModal("Rotate Sector##rot", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Rotate selected sector:");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::DragFloat("##rotang", &m_rotatePopupAngleDeg,
                         1.0f, -360.0f, 360.0f, "%.1f\xc2\xb0");
        ImGui::Spacing();
        if (ImGui::Button("Apply", ImVec2(100.0f, 0.0f)))
        {
            if (m_rotatePopupAngleDeg != 0.0f &&
                m_rotatePopupSectorId < doc.mapData().sectors.size())
            {
                doc.pushCommand(std::make_unique<CmdRotateSector>(
                    doc, m_rotatePopupSectorId, m_rotatePopupAngleDeg));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
}

// ─── fitToView ────────────────────────────────────────────────────────────────

void Viewport2D::fitToView(const world::WorldMapData& map) noexcept
{
    if (map.sectors.empty()) return;

    // Compute axis-aligned bounding box of all wall vertices.
    float minX =  1e18f, maxX = -1e18f;
    float minY =  1e18f, maxY = -1e18f;
    for (const auto& sector : map.sectors)
    {
        for (const auto& wall : sector.walls)
        {
            minX = std::min(minX, wall.p0.x); maxX = std::max(maxX, wall.p0.x);
            minY = std::min(minY, wall.p0.y); maxY = std::max(maxY, wall.p0.y);
        }
    }

    const float mapW = maxX - minX;
    const float mapH = maxY - minY;
    if (mapW < 1e-6f && mapH < 1e-6f) return;  // degenerate map

    // Add 10% padding around the bounds.
    constexpr float kPad = 0.1f;
    const float padW = mapW * kPad + 1.0f;
    const float padH = mapH * kPad + 1.0f;

    // We don’t know the canvas size here, so use a standard reference size
    // that will be close enough for the first draw; the user can fine-tune.
    constexpr float kRefW = 800.0f;
    constexpr float kRefH = 600.0f;

    const float zoomX = kRefW  / (mapW + 2.0f * padW);
    const float zoomY = kRefH  / (mapH + 2.0f * padH);
    m_zoom = std::clamp(std::min(zoomX, zoomY), 2.0f, 400.0f);

    // Pan so the map centre lands at the canvas centre.
    const float centreMapX = (minX + maxX) * 0.5f;
    const float centreMapY = (minY + maxY) * 0.5f;
    m_panOffset = {
        kRefW * 0.5f - centreMapX * m_zoom,
        kRefH * 0.5f - centreMapY * m_zoom
    };
    m_firstFrame = false;  // suppress the default first-frame centering
}

} // namespace daedalus::editor
