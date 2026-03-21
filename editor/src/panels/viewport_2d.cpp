#include "viewport_2d.h"
#include "floor_layer_panel.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/editor/light_def.h"
#include "daedalus/editor/entity_def.h"
#include "daedalus/editor/editor_layer.h"
#include "daedalus/editor/i_editor_tool.h"
#include "tools/draw_sector_tool.h"
#include "tools/select_tool.h"
#include "tools/vertex_tool.h"
#include "document/commands/cmd_move_entity.h"
#include "document/commands/cmd_move_light.h"
#include "document/commands/cmd_move_sector.h"
#include "daedalus/editor/compound_command.h"
#include "document/commands/cmd_set_player_start.h"
#include "document/commands/cmd_split_wall.h"
#include "document/commands/cmd_rotate_entity.h"
#include "document/commands/cmd_rotate_sector.h"
#include "document/commands/cmd_set_wall_curve.h"

#include "imgui.h"
#include "imgui_internal.h"  // GImGui->ActiveId, GImGui->ActiveIdWindow, DockTabIsVisible

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

inline float snapToGrid(float v, float step) noexcept
{
    return std::round(v / step) * step;
}

/// Evaluate a quadratic Bezier at parameter t ∈ [0,1].
inline glm::vec2 bezierQ(glm::vec2 p0, glm::vec2 cp,
                         glm::vec2 p1, float t) noexcept
{
    const float u = 1.0f - t;
    return u * u * p0 + 2.0f * u * t * cp + t * t * p1;
}

/// Evaluate a cubic Bezier at parameter t ∈ [0,1].
inline glm::vec2 bezierC(glm::vec2 p0, glm::vec2 ca, glm::vec2 cb,
                         glm::vec2 p1, float t) noexcept
{
    const float u = 1.0f - t;
    return u*u*u*p0 + 3.0f*u*u*t*ca + 3.0f*u*t*t*cb + t*t*t*p1;
}

/// Draw a dashed line between two screen-space points.
void addDashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col,
                   float thickness = 1.2f,
                   float dashLen = 5.0f, float gapLen = 4.0f)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const float ux = dx / len;
    const float uy = dy / len;
    float t = 0.0f;
    bool  drawing = true;
    while (t < len)
    {
        const float segEnd = std::min(t + (drawing ? dashLen : gapLen), len);
        if (drawing)
        {
            dl->AddLine(ImVec2(a.x + ux * t,      a.y + uy * t),
                        ImVec2(a.x + ux * segEnd,  a.y + uy * segEnd),
                        col, thickness);
        }
        t = segEnd;
        drawing = !drawing;
    }
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
    constexpr ImU32 minorColor = IM_COL32(60, 60, 60, 180);
    constexpr ImU32 majorColor = IM_COL32(90, 90, 90, 200);
    constexpr ImU32 axisColor  = IM_COL32(120, 120, 140, 220);

    // Scale the grid step so lines are always at least 8 px apart even when
    // zoomed far out — instead of hiding the grid, show a coarser one.
    float displayStep = 1.0f;         // map units between lines
    float step        = m_zoom;       // screen pixels between lines (zoom * 1.0)
    while (step < 8.0f)
    {
        displayStep *= 2.0f;
        step        *= 2.0f;
        if (displayStep > 1e6f) return;  // safety: map is enormous
    }
    if (step > 8000.0f) return;  // zoomed so far in grid lines are > 8000 px apart

    // The map origin (0,0) in screen space.
    const float originScreenX = canvasMin.x + m_panOffset.x;
    const float originScreenY = canvasMin.y + m_panOffset.y;

    const auto firstX = static_cast<int>(std::floor((canvasMin.x - originScreenX) / step));
    const auto firstY = static_cast<int>(std::floor((canvasMin.y - originScreenY) / step));
    const auto lastX  = static_cast<int>(std::ceil ((canvasMax.x - originScreenX) / step));
    const auto lastY  = static_cast<int>(std::ceil ((canvasMax.y - originScreenY) / step));

    for (int xi = firstX; xi <= lastX; ++xi)
    {
        const float   sx   = originScreenX + xi * step;
        // Map coordinate in original 1-unit grid space.
        const auto    mapX = static_cast<long long>(std::round(xi * displayStep));
        const ImU32   col  = (mapX == 0) ? axisColor
                           : (mapX % 5 == 0) ? majorColor : minorColor;
        dl->AddLine({sx, canvasMin.y}, {sx, canvasMax.y}, col, 1.0f);
    }
    for (int yi = firstY; yi <= lastY; ++yi)
    {
        const float   sy   = originScreenY + yi * step;
        const auto    mapY = static_cast<long long>(std::round(yi * displayStep));
        const ImU32   col  = (mapY == 0) ? axisColor
                           : (mapY % 5 == 0) ? majorColor : minorColor;
        dl->AddLine({canvasMin.x, sy}, {canvasMax.x, sy}, col, 1.0f);
    }
}

// ─── Sector drawing ─────────────────────────────────────────────────────────────────────────

void Viewport2D::drawSectors(ImDrawList*            dl,
                               const EditMapDocument& doc,
                               glm::vec2              canvasMin,
                               const FloorLayerPanel* floorLayer) const
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

        const auto sid     = static_cast<world::SectorId>(si);
        const bool sectSel = sel.isSectorSelected(sid);

        // Floor-layer dimming: sectors outside the edit height render at 20 % opacity.
        const bool dimmed = floorLayer &&
                            !floorLayer->isSectorFullOpacity(sector.floorHeight,
                                                             sector.ceilHeight);
        const float opFactor = dimmed ? 0.2f : 1.0f;

        // Helper: apply opacity factor to an IM_COL32 alpha channel.
        auto applyOp = [opFactor](ImU32 col) -> ImU32 {
            const float a = static_cast<float>((col >> 24) & 0xFF);
            return (col & 0x00FFFFFF) | (static_cast<ImU32>(a * opFactor) << 24);
        };

        // Build screen-space vertex list.
        std::vector<ImVec2> pts;
        pts.reserve(n);
        for (const auto& wall : sector.walls)
        {
            const auto s = mapToScreen(wall.p0, canvasMin);
            pts.push_back({s.x, s.y});
        }

        // Filled polygon.
        dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()),
            applyOp(sectSel ? IM_COL32(80, 130, 200, 60)
                            : IM_COL32(50,  80, 120, 40)));

        // Per-wall outline and Bezier arc.
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const world::Wall& wall = sector.walls[wi];
            const ImVec2&      p0   = pts[wi];
            const ImVec2&      p1   = pts[(wi + 1) % n];

            const bool isPortal = (wall.portalSectorId != world::INVALID_SECTOR_ID);
            const bool wallSel  = (sel.hasSingleOf(SelectionType::Wall) &&
                                   sel.items[0].sectorId == sid && sel.items[0].index == wi);

            ImU32 edgeColor;
            float edgeThick;
            if (wallSel)       { edgeColor = IM_COL32(255, 255, 255, 240); edgeThick = 2.5f; }
            else if (isPortal) { edgeColor = IM_COL32(0, 210, 210, 220);  edgeThick = 2.5f; }
            else if (sectSel)  { edgeColor = IM_COL32(120, 180, 255, 220); edgeThick = 1.5f; }
            else               { edgeColor = IM_COL32(80,  130, 200, 180); edgeThick = 1.5f; }
            edgeColor = applyOp(edgeColor);

            // Always draw a small midpoint handle circle so the user can
            // click-drag it to create or edit a Bezier curve.  The handle is
            // cyan (bright) when the cursor is near it, or a faint hint colour
            // otherwise.  This avoids requiring a modifier key (Ctrl+click is
            // intercepted as right-click by macOS on many configurations).
            {
                const glm::vec2 mapMid = (wall.p0 + sector.walls[(wi + 1) % n].p0) * 0.5f;
                const glm::vec2 sMidV  = mapToScreen(mapMid, canvasMin);
                const ImVec2    sMid   = {sMidV.x, sMidV.y};

                // Is the cursor near this midpoint handle?
                const ImVec2 mp = ImGui::GetIO().MousePos;
                const float  dx = mp.x - sMid.x;
                const float  dy = mp.y - sMid.y;
                const bool   nearHandle = (dx*dx + dy*dy) <= 14.0f * 14.0f;

                const ImU32 handleCol =
                    nearHandle ? IM_COL32(100, 220, 255, 220)
                              : IM_COL32(100, 220, 255,  60);
                dl->AddCircle(sMid, 5.5f, applyOp(handleCol), 12, 1.2f);
            }

            if (wall.curveControlA.has_value())
            {
                // ── Bezier arc ───────────────────────────────────────────────────
                const glm::vec2 mapP0 = wall.p0;
                const glm::vec2 mapP1 = sector.walls[(wi + 1) % n].p0;
                const glm::vec2 mapCa = *wall.curveControlA;
                const glm::vec2 mapCb = wall.curveControlB.value_or(mapCa);
                const bool isCubic    = wall.curveControlB.has_value();

                const uint32_t segs = std::clamp(wall.curveSubdivisions, 4u, 64u);
                glm::vec2 prev = mapP0;
                for (uint32_t k = 1; k <= segs; ++k)
                {
                    const float t    = static_cast<float>(k) / static_cast<float>(segs);
                    const glm::vec2 cur = isCubic
                        ? bezierC(mapP0, mapCa, mapCb, mapP1, t)
                        : bezierQ(mapP0, mapCa, mapP1, t);
                    const auto sPrev = mapToScreen(prev, canvasMin);
                    const auto sCur  = mapToScreen(cur,  canvasMin);
                    dl->AddLine({sPrev.x, sPrev.y}, {sCur.x, sCur.y}, edgeColor, edgeThick);
                    prev = cur;
                }

                // ── Control point diamonds (hover-aware) ─────────────────────────
                // Helper: draw a diamond at a screen position; brightens on cursor hover.
                const ImVec2 mouseNow = ImGui::GetIO().MousePos;
                auto drawDiamond = [&](glm::vec2 mapPos, ImU32 col, float r)
                {
                    const auto  sc   = mapToScreen(mapPos, canvasMin);
                    const float dx   = mouseNow.x - sc.x;
                    const float dy   = mouseNow.y - sc.y;
                    const bool  near = (dx*dx + dy*dy) <= (r + 6.0f) * (r + 6.0f);
                    const ImU32 c    = applyOp(near ? IM_COL32(255, 255, 255, 240) : col);
                    dl->AddTriangleFilled({sc.x,     sc.y - r}, {sc.x + r, sc.y},
                                          {sc.x,     sc.y + r}, c);
                    dl->AddTriangleFilled({sc.x,     sc.y - r}, {sc.x,     sc.y + r},
                                          {sc.x - r, sc.y},     c);
                };

                // Stem lines from wall midpoint to each control point.
                const auto scMid = mapToScreen((mapP0 + mapP1) * 0.5f, canvasMin);
                const auto scCa  = mapToScreen(mapCa, canvasMin);
                dl->AddLine({scMid.x, scMid.y}, {scCa.x, scCa.y},
                            applyOp(IM_COL32(100, 220, 255, 80)), 1.0f);
                // A = cyan diamond
                drawDiamond(mapCa, IM_COL32(100, 220, 255, 220), 5.0f);

                if (isCubic)
                {
                    const auto scCb = mapToScreen(*wall.curveControlB, canvasMin);
                    dl->AddLine({scMid.x, scMid.y}, {scCb.x, scCb.y},
                                applyOp(IM_COL32(255, 180, 80, 80)), 1.0f);
                    // B = amber diamond so it is visually distinct from A
                    drawDiamond(*wall.curveControlB, IM_COL32(255, 180, 80, 220), 5.0f);
                }
            }
            else
            {
                // Straight wall segment.
                dl->AddLine(p0, p1, edgeColor, edgeThick);
            }

            // Portal midpoint arrow (small cyan triangle).
            if (isPortal)
            {
                const float mx = (p0.x + p1.x) * 0.5f;
                const float my = (p0.y + p1.y) * 0.5f;
                const float dx = p1.x - p0.x;
                const float dy = p1.y - p0.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.5f)
                {
                    const float nx = -dy / len;
                    const float ny =  dx / len;
                    constexpr float sz = 5.0f;
                    const ImVec2 tip = {mx + nx * sz, my + ny * sz};
                    const ImVec2 bl  = {mx - nx * 2.0f - dx * 0.12f,
                                        my - ny * 2.0f - dy * 0.12f};
                    const ImVec2 br  = {mx - nx * 2.0f + dx * 0.12f,
                                        my - ny * 2.0f + dy * 0.12f};
                    dl->AddTriangleFilled(tip, bl, br,
                                         applyOp(IM_COL32(0, 210, 210, 200)));
                }
            }
        }

        // Vertex dots.
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const bool vertSel = sel.isVertexSelected(sid, wi);
            const float radius = vertSel ? 6.0f : 3.0f;
            const ImU32 col    = applyOp(vertSel
                ? IM_COL32(255, 255, 180, 255)
                : (sectSel ? IM_COL32(120, 180, 255, 220)
                           : IM_COL32( 80, 130, 200, 180)));
            dl->AddCircleFilled(pts[wi], radius, col);
        }
    }
}

// ─── Detail brush XZ footprints ───────────────────────────────────────────────────────────────
// Draws dashed XZ outlines for each detail brush in the map.  Uses the brush's
// world-space transform matrix to correctly handle position and rotation.

void Viewport2D::drawDetailBrushFootprints(ImDrawList*            dl,
                                            const EditMapDocument& doc,
                                            glm::vec2              canvasMin) const
{
    constexpr ImU32 kBoxCol  = IM_COL32(180, 120, 200, 160);   // box / wedge: purple
    constexpr ImU32 kCylCol  = IM_COL32(120, 200, 120, 160);   // cylinder: green
    constexpr ImU32 kArchCol = IM_COL32(200, 160,  80, 160);   // arch: amber

    const auto& sectors = doc.mapData().sectors;
    for (const auto& sector : sectors)
    {
        for (const auto& db : sector.details)
        {
            // World-space position of the brush (XZ).
            const glm::vec2 pos{db.transform[3].x, db.transform[3].z};

            // Extract local X and Z axes in world XZ (columns 0 and 2 of mat4).
            // These carry both rotation and scale.
            const glm::vec2 axisX{db.transform[0].x, db.transform[0].z};
            const glm::vec2 axisZ{db.transform[2].x, db.transform[2].z};

            switch (db.type)
            {
            case world::DetailBrushType::Box:
            case world::DetailBrushType::Wedge:
            {
                // Four corners: pos ± (axisX * halfX) ± (axisZ * halfZ)
                const float hx = db.geom.halfExtents.x;
                const float hz = db.geom.halfExtents.z;
                const glm::vec2 corners[4] = {
                    pos - axisX * hx - axisZ * hz,
                    pos + axisX * hx - axisZ * hz,
                    pos + axisX * hx + axisZ * hz,
                    pos - axisX * hx + axisZ * hz,
                };
                for (int ci = 0; ci < 4; ++ci)
                {
                    const auto a = mapToScreen(corners[ci],         canvasMin);
                    const auto b = mapToScreen(corners[(ci + 1) % 4], canvasMin);
                    addDashedLine(dl, {a.x, a.y}, {b.x, b.y}, kBoxCol);
                }
                break;
            }
            case world::DetailBrushType::Cylinder:
            {
                // Approximate as a 16-segment circle scaled by radius.
                // Use the XZ scale of the transform to size it correctly.
                const float scaleXZ = std::max(glm::length(axisX), glm::length(axisZ));
                const float r       = db.geom.radius * scaleXZ;
                constexpr int kSegs = 16;
                for (int ci = 0; ci < kSegs; ++ci)
                {
                    const float a0 = glm::two_pi<float>() * static_cast<float>(ci)     / kSegs;
                    const float a1 = glm::two_pi<float>() * static_cast<float>(ci + 1) / kSegs;
                    const glm::vec2 p0 = pos + glm::vec2{std::cos(a0), std::sin(a0)} * r;
                    const glm::vec2 p1 = pos + glm::vec2{std::cos(a1), std::sin(a1)} * r;
                    const auto sp0 = mapToScreen(p0, canvasMin);
                    const auto sp1 = mapToScreen(p1, canvasMin);
                    addDashedLine(dl, {sp0.x, sp0.y}, {sp1.x, sp1.y}, kCylCol);
                }
                break;
            }
            case world::DetailBrushType::ArchSpan:
            {
                // Show as a rectangle: spanWidth × archHeight.
                const float hw = db.geom.spanWidth  * 0.5f;
                const float hd = db.geom.thickness  * 0.5f;
                const glm::vec2 corners[4] = {
                    pos - axisX * hw - axisZ * hd,
                    pos + axisX * hw - axisZ * hd,
                    pos + axisX * hw + axisZ * hd,
                    pos - axisX * hw + axisZ * hd,
                };
                for (int ci = 0; ci < 4; ++ci)
                {
                    const auto a = mapToScreen(corners[ci],           canvasMin);
                    const auto b = mapToScreen(corners[(ci + 1) % 4], canvasMin);
                    addDashedLine(dl, {a.x, a.y}, {b.x, b.y}, kArchCol);
                }
                break;
            }
            default:
                // ImportedMesh: no footprint available at edit time.
                break;
            }
        }
    }
}

// ─── Entity icons ─────────────────────────────────────────────────────────────────────────

void Viewport2D::drawEntities(ImDrawList*            dl,
                               const EditMapDocument& doc,
                               glm::vec2              canvasMin,
                               std::size_t            hoveredEntityIdx) const
{
    constexpr float kIconR = 7.0f;

    const auto&           entities = doc.entities();
    const SelectionState& sel      = doc.selection();

    for (std::size_t ei = 0; ei < entities.size(); ++ei)
    {
        const EntityDef& ed  = entities[ei];
        const auto       sp  = mapToScreen({ed.position.x, ed.position.z}, canvasMin);
        const ImVec2     spi {sp.x, sp.y};

        const bool selected = sel.isEntitySelected(ei);

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

        // Hover ring (white) shown when the cursor is over the icon but not selected.
        if (ei == hoveredEntityIdx && !selected)
            dl->AddCircle(spi, kIconR + 4.0f, IM_COL32(255, 255, 255, 160), 24, 1.5f);

        // Selection ring (yellow).
        if (selected)
            dl->AddCircle(spi, kIconR + 4.0f, IM_COL32(255, 200, 0, 220), 24, 1.5f);

        // Direction arrow — yaw=0 points toward +Z (down in top-down view).
        // Brightens when selected so it's easy to read.
        const ImU32  arrowAlpha = selected ? IM_COL32(255,255,255,220) : IM_COL32(255,255,255,120);
        const float  sinY = std::sin(ed.yaw);
        const float  cosY = std::cos(ed.yaw);
        const float  arrLen = kIconR + 9.0f;
        const ImVec2 tip {sp.x + sinY * arrLen, sp.y + cosY * arrLen};
        dl->AddLine(spi, tip, arrowAlpha, 1.5f);
        const float nx =  cosY;
        const float ny = -sinY;
        dl->AddTriangleFilled(
            tip,
            ImVec2{tip.x - sinY * 4.0f + nx * 3.0f, tip.y - cosY * 4.0f + ny * 3.0f},
            ImVec2{tip.x - sinY * 4.0f - nx * 3.0f, tip.y - cosY * 4.0f - ny * 3.0f},
            arrowAlpha);
    }
}

// ─── openRotatePopup ────────────────────────────────────────────────────────────

void Viewport2D::openRotatePopup(world::SectorId sectorId) noexcept
{
    m_rotatePopupOpen     = true;
    m_rotatePopupSectorId = sectorId;
    m_rotatePopupAngleDeg = 45.0f;
}

// ─── Ghost placement helpers ──────────────────────────────────────────────────

void Viewport2D::beginPendingPlacement(PendingPlacement type) noexcept
{
    m_pendingPlacement      = type;
    m_pendingPlacementFired = false;
}

void Viewport2D::cancelPendingPlacement() noexcept
{
    m_pendingPlacement      = PendingPlacement::None;
    m_pendingPlacementFired = false;
}

bool Viewport2D::consumePendingPlacement(glm::vec2& outMapPos) noexcept
{
    if (!m_pendingPlacementFired)
        return false;
    outMapPos               = m_pendingPlacementPos;
    m_pendingPlacementFired = false;
    m_pendingPlacement      = PendingPlacement::None;
    return true;
}

void Viewport2D::updateFlyCamera(bool captured, glm::vec2 eyeXZ, float yawRad) noexcept
{
    m_flyCameraActive = captured;
    m_flyCameraPos    = eyeXZ;
    m_flyCameraYaw    = yawRad;
}

// ─── Main draw ────────────────────────────────────────────────────────────────

void Viewport2D::draw(EditMapDocument& doc,
                       IEditorTool*     activeTool,
                       DrawSectorTool*  drawTool,
                       SelectTool*      selectTool,
                       VertexTool*      vertexTool,
                       FloorLayerPanel* floorLayer)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool windowOpen = ImGui::Begin("2D Viewport");
    ImGui::PopStyleVar();

    // Guard 1: Begin() returns false when the window is a non-active docked tab
    // (SkipItems=true).  Skip ALL drawing and input in that case.
    //
    // Guard 2: During the tab-switch transition frame, ImGui sets
    // HiddenFramesCannotSkipItems=1 (to allow auto-sizing), which keeps
    // SkipItems=false and causes Begin() to return true even though the tab is
    // not yet visually shown.  DockTabIsVisible is the authoritative flag: it
    // is false whenever the window is not the selected tab in its dock node,
    // regardless of SkipItems.  We must also skip input in that case.
    ImGuiWindow* const selfWin = ImGui::GetCurrentWindow();
    const bool tabVisible = !selfWin->DockIsActive || selfWin->DockTabIsVisible;

    if (!windowOpen || !tabVisible)
    {
        ImGui::End();
        return;
    }

    // Reserve the canvas.
    ImVec2 canvasSz = ImGui::GetContentRegionAvail();
    if (canvasSz.x < 10.0f) canvasSz.x = 10.0f;
    if (canvasSz.y < 10.0f) canvasSz.y = 10.0f;

    const ImVec2 canvasMinV = ImGui::GetCursorScreenPos();
    const glm::vec2 canvasMin{canvasMinV.x, canvasMinV.y};
    const glm::vec2 canvasMax{canvasMin.x + canvasSz.x, canvasMin.y + canvasSz.y};

    // Cache canvas size for viewCenterMapPos() and fitToView().
    m_lastCanvasSize = {canvasSz.x, canvasSz.y};

    // Center the view on first frame.
    if (m_firstFrame)
    {
        m_panOffset = {canvasSz.x * 0.5f, canvasSz.y * 0.5f};
        m_firstFrame = false;
    }

    // ── Mouse position + snap (computed early so the snap indicator can be rendered) ──
    const ImVec2 mousePosV      = ImGui::GetMousePos();
    const glm::vec2 mouseMapRaw = screenToMap({mousePosV.x, mousePosV.y}, canvasMin);
    m_lastMouseMapPos           = mouseMapRaw;

    const bool shiftHeld = ImGui::GetIO().KeyShift;
    const bool doSnap    = m_snapEnabled && !shiftHeld;
    glm::vec2 mouseMap = doSnap
        ? glm::vec2{snapToGrid(mouseMapRaw.x, m_gridStep),
                    snapToGrid(mouseMapRaw.y, m_gridStep)}
        : mouseMapRaw;

    // Vertex magnetic snap: when dragging a vertex, override mouseMap to the exact
    // position of the nearest other vertex within kSnapPx screen pixels.
    // This lets you align sector corners precisely for portal linking.
    {
        // Snap radius scales with zoom so it always covers the vertex pick radius
        // in screen space (SelectTool = 0.4 units, VertexTool = 0.5 units).
        // At low zoom this bottoms out at 12 px for comfortable dragging.
        const float kSnapPx   = std::max(12.0f, 0.6f * m_zoom);
        const float kSnapPxSq = kSnapPx * kSnapPx;

        m_vertexSnapSectorId = world::INVALID_SECTOR_ID;

        const bool snapFromSelect = selectTool && selectTool->isDraggingVertex();
        const bool snapFromVertex = vertexTool && vertexTool->isDragging();

        if (snapFromSelect || snapFromVertex)
        {
            const world::SectorId excludeSid = snapFromSelect
                ? selectTool->dragSectorId() : vertexTool->dragSectorId();
            const std::size_t excludeWi = snapFromSelect
                ? selectTool->dragWallIndex() : vertexTool->dragWallIndex();

            float           bestSq  = kSnapPxSq;
            world::SectorId bestSid = world::INVALID_SECTOR_ID;
            std::size_t     bestWi  = 0;
            glm::vec2       bestPos{};

            const glm::vec2 mouseScreen{mousePosV.x, mousePosV.y};
            const auto& sectors = doc.mapData().sectors;
            for (std::size_t si = 0; si < sectors.size(); ++si)
            {
                const auto& sector = sectors[si];
                for (std::size_t wi = 0; wi < sector.walls.size(); ++wi)
                {
                    if (static_cast<world::SectorId>(si) == excludeSid && wi == excludeWi)
                        continue;
                    const glm::vec2 sp = mapToScreen(sector.walls[wi].p0, canvasMin);
                    const float dx = mouseScreen.x - sp.x;
                    const float dy = mouseScreen.y - sp.y;
                    const float sq = dx * dx + dy * dy;
                    if (sq < bestSq)
                    {
                        bestSq  = sq;
                        bestSid = static_cast<world::SectorId>(si);
                        bestWi  = wi;
                        bestPos = sector.walls[wi].p0;
                    }
                }
            }

            if (bestSid != world::INVALID_SECTOR_ID)
            {
                mouseMap              = bestPos;  // Exact position — overrides grid snap.
                m_vertexSnapSectorId  = bestSid;
                m_vertexSnapWallIndex = bestWi;
            }
        }
    }

    // Canvas hover detection: check if the mouse is within our content region AND
    // our window is actually the one ImGui considers hovered (respects docking z-order).
    // We do NOT use InvisibleButton — that's a widget meant for UI elements, not
    // entire viewport canvases. InvisibleButton fights the docking system because it
    // claims input based on geometric rect overlap, ignoring window z-order.
    //
    // CRITICAL: Use ChildWindows flag to ensure we're truly the hovered window,
    // not just geometrically under the mouse. This prevents stealing clicks from
    // Properties and other docked panels.
    const bool mouseInCanvas = ImGui::IsMouseHoveringRect(canvasMinV, 
                                   ImVec2(canvasMax.x, canvasMax.y));
    const bool hovered = mouseInCanvas && 
                         ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | 
                                                 ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    
    // Active state: we're processing a drag that started in this canvas.
    // Track this manually since we no longer have InvisibleButton's IsItemActive().
    const bool active = hovered || m_panActive || m_entityDragActive ||
                        m_lightDragActive || m_entityRotActive || m_psDragActive ||
                        m_psRotActive || m_rectSelActive || m_groupDragActive;

    // ── Draw background + grid ────────────────────────────────────────────────
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({canvasMin.x, canvasMin.y},
                      {canvasMax.x, canvasMax.y},
                      IM_COL32(30, 30, 35, 255));
    dl->PushClipRect({canvasMin.x, canvasMin.y},
                     {canvasMax.x, canvasMax.y}, true);

    if (m_gridVisible)
        drawGrid(dl, canvasMin, canvasMax);
    drawSectors(dl, doc, canvasMin, floorLayer);
    drawDetailBrushFootprints(dl, doc, canvasMin);

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
            case WallHighlightKind::PortalGeomMismatch:
                col = IM_COL32(255, 140, 0, 210); break;   // orange
            case WallHighlightKind::MissingBackLink:
                col = IM_COL32(255, 220, 0, 210); break;   // yellow
            case WallHighlightKind::WindingOrder:
                col = IM_COL32(220, 80, 255, 210); break;  // magenta
            }
            dl->AddLine({p0s.x, p0s.y}, {p1s.x, p1s.y}, col, 2.5f);
        }
    }

    // Compute which entity (if any) the cursor is hovering over.
    std::size_t hoveredEntityIdx = std::numeric_limits<std::size_t>::max();
    if (hovered)
    {
        constexpr float kHitRSq = 10.0f * 10.0f;
        const auto& entities = doc.entities();
        for (std::size_t ei = 0; ei < entities.size(); ++ei)
        {
            const EntityDef& ed = entities[ei];
            const auto  sp = mapToScreen({ed.position.x, ed.position.z}, canvasMin);
            const float dx = mousePosV.x - sp.x;
            const float dy = mousePosV.y - sp.y;
            if (dx * dx + dy * dy <= kHitRSq)
            {
                hoveredEntityIdx = ei;
                break;
            }
        }
    }

    drawEntities(dl, doc, canvasMin, hoveredEntityIdx);

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

            const bool lightSel = sel.isLightSelected(li);

            // Influence-radius ring — point lights only.
            // Spot lights use ld.range, shown via the cone overlay below.
            if (ld.type == LightType::Point)
            {
                const float ringR = ld.radius * m_zoom;
                if (ringR > kIconR + 2.0f)
                    dl->AddCircle(spi, ringR,
                                  lightSel ? IM_COL32(255, 220, 80, 120)
                                           : IM_COL32(255, 180, 50, 60),
                                  48, 1.5f);
            }

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

            // Spotlight direction cone (top-down XZ projection).
            // When the light points nearly straight up/down, the XZ component is tiny;
            // a dot marker indicates vertical aim rather than a degenerate cone.
            if (ld.type == LightType::Spot)
            {
                const float xzLen = std::sqrt(ld.direction.x * ld.direction.x +
                                              ld.direction.z * ld.direction.z);
                const ImU32 coneCol  = lightSel
                    ? IM_COL32(255, 255, 140, 200) : IM_COL32(255, 220, 80, 110);
                const ImU32 coneFill = lightSel
                    ? IM_COL32(255, 255, 140,  45) : IM_COL32(255, 220, 80,  22);

                if (xzLen < 0.1f)
                {
                    // Pointing nearly straight up/down — small ring to indicate vertical aim.
                    dl->AddCircle(spi, kIconR * 0.55f, coneCol, 6, 1.2f);
                }
                else
                {
                    // Project direction onto XZ plane and normalise.
                    // mapToScreen maps worldX → screenX, worldZ → screenY, so the
                    // XZ projection maps directly to screen X/Y coordinates.
                    const float invLen = 1.0f / xzLen;
                    const float dirX = ld.direction.x * invLen;
                    const float dirZ = ld.direction.z * invLen;

                    // Cone length = range scaled to screen pixels, attenuated by the
                    // horizontal component so a nearly-vertical light shows a short cone.
                    // Matches the point-light radius ring convention: radius * zoom.
                    const float coneLen = ld.range * m_zoom * xzLen;

                    // Rotate ±outerConeAngle around the XZ direction to get the
                    // two cone-edge endpoints.
                    const float cosH = std::cos(ld.outerConeAngle);
                    const float sinH = std::sin(ld.outerConeAngle);
                    const ImVec2 coneL{
                        sp.x + (dirX * cosH - dirZ * sinH) * coneLen,
                        sp.y + (dirX * sinH + dirZ * cosH) * coneLen};
                    const ImVec2 coneR{
                        sp.x + (dirX * cosH + dirZ * sinH) * coneLen,
                        sp.y + (-dirX * sinH + dirZ * cosH) * coneLen};

                    dl->AddTriangleFilled(spi, coneL, coneR, coneFill);
                    dl->AddLine(spi, coneL, coneCol, 1.2f);
                    dl->AddLine(spi, coneR, coneCol, 1.2f);

                    // Inner cone — brighter fill + contrasting lines drawn on top.
                    const float cosHi = std::cos(ld.innerConeAngle);
                    const float sinHi = std::sin(ld.innerConeAngle);
                    const ImVec2 innerL{
                        sp.x + (dirX * cosHi - dirZ * sinHi) * coneLen,
                        sp.y + (dirX * sinHi + dirZ * cosHi) * coneLen};
                    const ImVec2 innerR{
                        sp.x + (dirX * cosHi + dirZ * sinHi) * coneLen,
                        sp.y + (-dirX * sinHi + dirZ * cosHi) * coneLen};
                    const ImU32 innerFill = lightSel
                        ? IM_COL32(255, 255, 180,  80) : IM_COL32(255, 240, 130,  45);
                    const ImU32 innerLine = lightSel
                        ? IM_COL32(255, 255, 220, 220) : IM_COL32(255, 245, 150, 150);
                    dl->AddTriangleFilled(spi, innerL, innerR, innerFill);
                    dl->AddLine(spi, innerL, innerLine, 1.0f);
                    dl->AddLine(spi, innerR, innerLine, 1.0f);
                }
            }
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

    // ── Vertex snap indicator ───────────────────────────────────────────────────────────────────
    if (m_vertexSnapSectorId != world::INVALID_SECTOR_ID)
    {
        const auto& sectors = doc.mapData().sectors;
        if (m_vertexSnapSectorId < sectors.size())
        {
            const auto& snapSector = sectors[m_vertexSnapSectorId];
            if (m_vertexSnapWallIndex < snapSector.walls.size())
            {
                const glm::vec2 sp = mapToScreen(
                    snapSector.walls[m_vertexSnapWallIndex].p0, canvasMin);
                const ImVec2 spi{sp.x, sp.y};

                // Bright green ring — outer glow + inner ring.
                dl->AddCircle(spi, 13.0f, IM_COL32(80, 255, 120, 80),  16, 1.5f);
                dl->AddCircle(spi,  9.0f, IM_COL32(80, 255, 120, 240), 16, 2.0f);

                // Line from cursor to snap target.
                dl->AddLine({mousePosV.x, mousePosV.y}, spi,
                            IM_COL32(80, 255, 120, 140), 1.0f);
            }
        }
    }

    // ── Ghost placement overlay ─────────────────────────────────────────────
    if (m_pendingPlacement != PendingPlacement::None)
    {
        const glm::vec2 sp = mapToScreen(mouseMap, canvasMin);
        const ImVec2    spi{sp.x, sp.y};
        constexpr float kR = 10.0f;
        const ImU32 ghostCol = (m_pendingPlacement == PendingPlacement::Light)
            ? IM_COL32(255, 200, 50, 200)
            : IM_COL32(80, 200, 255, 200);
        dl->AddCircle(spi, kR, ghostCol, 24, 1.5f);
        dl->AddLine({sp.x - kR, sp.y}, {sp.x + kR, sp.y}, ghostCol, 1.0f);
        dl->AddLine({sp.x, sp.y - kR}, {sp.x, sp.y + kR}, ghostCol, 1.0f);
        const char* label = (m_pendingPlacement == PendingPlacement::Light)
            ? "Place Light" : "Place Entity";
        dl->AddText({sp.x + kR + 4.0f, sp.y - 6.0f}, ghostCol, label);
    }

    // ── Fly camera overlay ───────────────────────────────────────────────────
    if (m_flyCameraActive)
    {
        const glm::vec2 sp = mapToScreen(m_flyCameraPos, canvasMin);
        const ImVec2    spi{sp.x, sp.y};
        constexpr float kIconR   = 8.0f;
        constexpr float kConeLen = 40.0f;
        constexpr float kHalfFov = 0.6108652f;  // ~35 degrees
        constexpr ImU32 cyanCol  = IM_COL32(0, 220, 220, 220);
        constexpr ImU32 cyanFill = IM_COL32(0, 220, 220, 30);

        const float fwdX = std::sin(m_flyCameraYaw);
        const float fwdY = std::cos(m_flyCameraYaw);
        const float cosH = std::cos(kHalfFov);
        const float sinH = std::sin(kHalfFov);

        // Left cone ray (rotate fwd by +halfFov).
        const ImVec2 coneL{sp.x + (fwdX * cosH - fwdY * sinH) * kConeLen,
                           sp.y + (fwdX * sinH + fwdY * cosH) * kConeLen};
        // Right cone ray (rotate fwd by -halfFov).
        const ImVec2 coneR{sp.x + (fwdX * cosH + fwdY * sinH) * kConeLen,
                           sp.y + (-fwdX * sinH + fwdY * cosH) * kConeLen};

        dl->AddTriangleFilled(spi, coneL, coneR, cyanFill);
        dl->AddLine(spi, coneL, cyanCol, 1.0f);
        dl->AddLine(spi, coneR, cyanCol, 1.0f);

        // Camera icon circle + direction triangle.
        dl->AddCircle(spi, kIconR, cyanCol, 16, 1.5f);
        const float arrowFwdX = fwdX * (kIconR + 5.0f);
        const float arrowFwdY = fwdY * (kIconR + 5.0f);
        const ImVec2 arrowTip{sp.x + arrowFwdX, sp.y + arrowFwdY};
        const float  perpX   =  fwdY * 3.5f;
        const float  perpY   = -fwdX * 3.5f;
        dl->AddTriangleFilled(
            arrowTip,
            ImVec2{sp.x + fwdX * kIconR + perpX, sp.y + fwdY * kIconR + perpY},
            ImVec2{sp.x + fwdX * kIconR - perpX, sp.y + fwdY * kIconR - perpY},
            cyanCol);
    }

    dl->PopClipRect();

    // ── Mouse input ──────────────────────────────────────────────────────────────────
    // (mousePosV, mouseMapRaw, shiftHeld, doSnap, mouseMap computed at top of draw())
    //
    // Guard: suppress canvas input if a widget in another window currently owns
    // the drag (e.g. dragging a DragFloat in Properties while the cursor briefly
    // crosses back onto the canvas).
    const bool otherWindowOwnsActiveDrag =
        (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
         GImGui->ActiveId != 0 &&
         GImGui->ActiveIdWindow != nullptr &&
         GImGui->ActiveIdWindow != ImGui::GetCurrentWindow());

    // CRITICAL: Check if ImGui has an active item (widget being interacted with)
    // OR if the mouse is hovering a widget that will consume the click.
    // We must NOT check WantCaptureMouse alone - it's true whenever the mouse is
    // over ANY ImGui window, including our own viewport canvas.
    // Instead, check if there's an actual widget (button, slider, etc.) claiming input.
    //
    // Use HoveredIdPreviousFrame because on the click frame, HoveredId is cleared
    // before we process input, but PreviousFrame still tells us what was hovered.
    const bool imguiWidgetActive = (GImGui->ActiveId != 0 && 
                                     GImGui->ActiveIdWindow != ImGui::GetCurrentWindow());
    const bool imguiWidgetHovered = (GImGui->HoveredIdPreviousFrame != 0);
    const bool imguiBlockingInput = imguiWidgetActive || imguiWidgetHovered;

    if (hovered && !otherWindowOwnsActiveDrag && !imguiBlockingInput)
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

        // Arrow-key pan: only when the 2D view is focused and 3D fly mode is off.
        if (!m_flyCameraActive && !ImGui::GetIO().WantCaptureKeyboard)
        {
            const bool  fast     = ImGui::GetIO().KeyShift;
            const float panDelta = (fast ? 5.0f : 1.0f) * 300.0f * ImGui::GetIO().DeltaTime;
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow))    m_panOffset.y += panDelta;
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow))  m_panOffset.y -= panDelta;
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))  m_panOffset.x += panDelta;
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) m_panOffset.x -= panDelta;
        }

        // Pending placement: intercept LMB while armed; suppress normal tool events.
        if (m_pendingPlacement != PendingPlacement::None &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_pendingPlacementFired = true;
            m_pendingPlacementPos   = mouseMap;
        }

        // ── Curve handle drag (live update on move) ─────────────────────────────
        if (m_curveDragActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            auto& sectors = doc.mapData().sectors;
            if (m_curveDragSectorId < sectors.size() &&
                m_curveDragWallIndex < sectors[m_curveDragSectorId].walls.size())
            {
                auto& dragWall = sectors[m_curveDragSectorId].walls[m_curveDragWallIndex];
                if (m_curveDragIsB)
                    dragWall.curveControlB = mouseMap;
                else
                    dragWall.curveControlA = mouseMap;
                m_curveDragMoved = true;
                doc.markDirty();
            }
        }

        // Tool mouse events (snapped coordinates) — suppressed while placement pending.
        if (activeTool && m_pendingPlacement == PendingPlacement::None)
        {
            activeTool->onMouseMove(doc, mouseMap.x, mouseMap.y);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const ImGuiIO& clickIO = ImGui::GetIO();
                constexpr float kHitR = 10.0f;  ///< Icon hit radius in screen pixels.

                // Curve handle click: check existing control-point diamonds first,
                // then fall back to the midpoint circle.  Priority order:
                //   1. B diamond (amber)  — only when cubic curve is active
                //   2. A diamond (cyan)   — only when any curve is active
                //   3. Midpoint circle    — always (creates a new curve if absent)
                // Alt is reserved for wall split so skip all handle detection when Alt is held.
                bool handledByMidpoint = false;
                if (!clickIO.KeyAlt)
                {
                    constexpr float kHandleHitSq = 11.0f * 11.0f;  // diamond pick radius
                    constexpr float kMidHitSq    = 14.0f * 14.0f;  // midpoint circle radius

                    float           bestBSq  = kHandleHitSq;
                    world::SectorId bestBSid = world::INVALID_SECTOR_ID;
                    std::size_t     bestBWi  = 0;

                    float           bestASq  = kHandleHitSq;
                    world::SectorId bestASid = world::INVALID_SECTOR_ID;
                    std::size_t     bestAWi  = 0;

                    float           bestMSq  = kMidHitSq;
                    world::SectorId bestMSid = world::INVALID_SECTOR_ID;
                    std::size_t     bestMWi  = 0;

                    const auto& sectors = doc.mapData().sectors;
                    for (std::size_t si = 0; si < sectors.size(); ++si)
                    {
                        const auto&       sec = sectors[si];
                        const std::size_t ns  = sec.walls.size();
                        for (std::size_t wi = 0; wi < ns; ++wi)
                        {
                            const world::Wall& wl = sec.walls[wi];

                            // B diamond
                            if (wl.curveControlB.has_value())
                            {
                                const glm::vec2 sB = mapToScreen(*wl.curveControlB, canvasMin);
                                const float dx = mousePosV.x - sB.x;
                                const float dy = mousePosV.y - sB.y;
                                const float sq = dx*dx + dy*dy;
                                if (sq < bestBSq)
                                { bestBSq = sq; bestBSid = static_cast<world::SectorId>(si); bestBWi = wi; }
                            }

                            // A diamond
                            if (wl.curveControlA.has_value())
                            {
                                const glm::vec2 sA = mapToScreen(*wl.curveControlA, canvasMin);
                                const float dx = mousePosV.x - sA.x;
                                const float dy = mousePosV.y - sA.y;
                                const float sq = dx*dx + dy*dy;
                                if (sq < bestASq)
                                { bestASq = sq; bestASid = static_cast<world::SectorId>(si); bestAWi = wi; }
                            }

                            // Midpoint circle
                            {
                                const glm::vec2 mid =
                                    (wl.p0 + sec.walls[(wi + 1) % ns].p0) * 0.5f;
                                const glm::vec2 sM = mapToScreen(mid, canvasMin);
                                const float dx = mousePosV.x - sM.x;
                                const float dy = mousePosV.y - sM.y;
                                const float sq = dx*dx + dy*dy;
                                if (sq < bestMSq)
                                { bestMSq = sq; bestMSid = static_cast<world::SectorId>(si); bestMWi = wi; }
                            }
                        }
                    }

                    // Start drag in priority order: B > A > midpoint.
                    if (bestBSid != world::INVALID_SECTOR_ID)
                    {
                        m_curveDragActive    = true;
                        m_curveDragIsB       = true;
                        m_curveDragSectorId  = bestBSid;
                        m_curveDragWallIndex = bestBWi;
                        m_curveDragOldControlA = sectors[bestBSid].walls[bestBWi].curveControlA;
                        m_curveDragOldControlB = sectors[bestBSid].walls[bestBWi].curveControlB;
                        m_curveDragMoved = false;
                        handledByMidpoint = true;
                    }
                    else if (bestASid != world::INVALID_SECTOR_ID)
                    {
                        m_curveDragActive    = true;
                        m_curveDragIsB       = false;
                        m_curveDragSectorId  = bestASid;
                        m_curveDragWallIndex = bestAWi;
                        m_curveDragOldControlA = sectors[bestASid].walls[bestAWi].curveControlA;
                        m_curveDragOldControlB = sectors[bestASid].walls[bestAWi].curveControlB;
                        m_curveDragMoved = false;
                        handledByMidpoint = true;
                    }
                    else if (bestMSid != world::INVALID_SECTOR_ID)
                    {
                        m_curveDragActive    = true;
                        m_curveDragIsB       = false;
                        m_curveDragSectorId  = bestMSid;
                        m_curveDragWallIndex = bestMWi;
                        m_curveDragOldControlA = sectors[bestMSid].walls[bestMWi].curveControlA;
                        m_curveDragOldControlB = sectors[bestMSid].walls[bestMWi].curveControlB;
                        m_curveDragMoved = false;
                        handledByMidpoint = true;
                    }
                }

                if (handledByMidpoint)
                {
                    // Curve drag started; suppress normal click handling this frame.
                }
                else
                {

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
                    // ── Group drag ────────────────────────────────────────────────────────
                    // If the cursor is over an already-selected object in a multi-item
                    // selection (Select tool only), capture all selected objects and start
                    // a grouped drag rather than re-selecting a single object.
                    bool handledByGroupDrag = false;
                    if (selectTool &&
                        activeTool == static_cast<IEditorTool*>(selectTool) &&
                        doc.selection().items.size() > 1)
                    {
                        const SelectionState& gsel = doc.selection();
                        constexpr float kGR = 10.0f;  // screen-pixel icon hit radius

                        // Test selected entities
                        for (const auto& gItem : gsel.items)
                        {
                            if (gItem.type != SelectionType::Entity) continue;
                            if (gItem.index >= doc.entities().size()) continue;
                            const auto& gEd = doc.entities()[gItem.index];
                            const auto  gSp = mapToScreen({gEd.position.x, gEd.position.z}, canvasMin);
                            const float gdx = mousePosV.x - gSp.x;
                            const float gdy = mousePosV.y - gSp.y;
                            if (gdx * gdx + gdy * gdy <= kGR * kGR)
                            { handledByGroupDrag = true; break; }
                        }

                        // Test selected lights
                        if (!handledByGroupDrag)
                        {
                            for (const auto& gItem : gsel.items)
                            {
                                if (gItem.type != SelectionType::Light) continue;
                                if (gItem.index >= doc.lights().size()) continue;
                                const auto& gLd = doc.lights()[gItem.index];
                                const auto  gSp = mapToScreen({gLd.position.x, gLd.position.z}, canvasMin);
                                const float gdx = mousePosV.x - gSp.x;
                                const float gdy = mousePosV.y - gSp.y;
                                if (gdx * gdx + gdy * gdy <= kGR * kGR)
                                { handledByGroupDrag = true; break; }
                            }
                        }

                        // Test selected sectors (ray-cast point-in-polygon in XZ map space)
                        if (!handledByGroupDrag)
                        {
                            const auto& gMap = doc.mapData();
                            for (const auto& gItem : gsel.items)
                            {
                                if (gItem.type != SelectionType::Sector) continue;
                                if (gItem.sectorId >= static_cast<world::SectorId>(gMap.sectors.size())) continue;
                                const auto& gSec = gMap.sectors[gItem.sectorId];
                                if (gSec.walls.size() < 3) continue;
                                int gCross = 0;
                                const glm::vec2 gP = mouseMapRaw;
                                const std::size_t gN = gSec.walls.size();
                                for (std::size_t gi = 0; gi < gN; ++gi)
                                {
                                    const glm::vec2 ga = gSec.walls[gi].p0;
                                    const glm::vec2 gb = gSec.walls[(gi + 1) % gN].p0;
                                    if ((ga.y <= gP.y) != (gb.y <= gP.y))
                                        if (gP.x < ga.x + (gP.y - ga.y) / (gb.y - ga.y) * (gb.x - ga.x))
                                            ++gCross;
                                }
                                if (gCross & 1) { handledByGroupDrag = true; break; }
                            }
                        }

                        if (handledByGroupDrag)
                        {
                            // Snapshot original positions for every selected item.
                            m_groupSectors.clear();
                            m_groupEntities.clear();
                            m_groupLights.clear();
                            const auto& capMap = doc.mapData();
                            for (const auto& capItem : gsel.items)
                            {
                                switch (capItem.type)
                                {
                                case SelectionType::Sector:
                                    if (capItem.sectorId < static_cast<world::SectorId>(capMap.sectors.size()))
                                    {
                                        GroupSectorOrig gso;
                                        gso.sectorId = capItem.sectorId;
                                        const auto& gWalls = capMap.sectors[capItem.sectorId].walls;
                                        gso.wallPositions.reserve(gWalls.size());
                                        gso.curveA.reserve(gWalls.size());
                                        gso.curveB.reserve(gWalls.size());
                                        for (const auto& gw : gWalls)
                                        {
                                            gso.wallPositions.push_back(gw.p0);
                                            gso.curveA.push_back(gw.curveControlA);
                                            gso.curveB.push_back(gw.curveControlB);
                                        }
                                        m_groupSectors.push_back(std::move(gso));
                                    }
                                    break;
                                case SelectionType::Entity:
                                    if (capItem.index < doc.entities().size())
                                        m_groupEntities.push_back(
                                            {capItem.index, doc.entities()[capItem.index].position});
                                    break;
                                case SelectionType::Light:
                                    if (capItem.index < doc.lights().size())
                                        m_groupLights.push_back(
                                            {capItem.index, doc.lights()[capItem.index].position});
                                    break;
                                default: break;
                                }
                            }
                            m_groupDragActive    = true;
                            m_groupDragMoved     = false;
                            m_groupDragFirstMove = true;
                            m_groupDragAnchor    = mouseMap;
                        }
                    }

                    if (!handledByGroupDrag)
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
                    sel.items.push_back({SelectionType::PlayerStart,
                                         world::INVALID_SECTOR_ID, 0});
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
                            sel.items.push_back({SelectionType::Entity,
                                                 world::INVALID_SECTOR_ID, ei});
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
                                sel.items.push_back({SelectionType::Light,
                                                     world::INVALID_SECTOR_ID, li});
                                hitLight          = true;
                                m_lightDragActive = true;
                                m_lightDragIdx    = li;
                                m_lightDragOrigin = ld.position;
                            }
                        }
                        if (!hitLight)
                        {
                            // Use unsnapped coordinates for hit detection so that
                            // vertices at off-grid positions (e.g. after a W-split)
                            // remain selectable regardless of the current grid step.
                            // DrawSectorTool is the only tool that places new vertices
                            // on click and therefore needs the pre-snapped position.
                            const glm::vec2 downPos = drawTool ? mouseMap : mouseMapRaw;
                            activeTool->onMouseDown(doc, downPos.x, downPos.y, 0);
                            // Start drag-rect when the active tool supports it
                            // (SelectTool or VertexTool) and no geometry drag was
                            // initiated on this click.
                            const bool selectToolReady =
                                selectTool &&
                                activeTool == static_cast<IEditorTool*>(selectTool) &&
                                !selectTool->isDragging();
                            const bool vertexToolReady =
                                vertexTool &&
                                activeTool == static_cast<IEditorTool*>(vertexTool) &&
                                !vertexTool->isDragging();
                            if (selectToolReady || vertexToolReady)
                            {
                                m_rectSelActive  = true;
                                m_rectSelAnchor  = mouseMap;
                                m_rectSelCurrent = mouseMap;
                            }
                        }
                    }
                }
                    }  // if (!handledByGroupDrag)
            }  // if (!handledByAlt)
                }  // else (not handledByCtrl)
            }  // if (IsMouseClicked LMB)
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                constexpr float kHitR = 10.0f;  // screen-pixel hit radius
                bool handled = false;

                // RMB on player start icon → begin yaw rotate drag.
                if (doc.selection().hasSingleOf(SelectionType::PlayerStart))
                {
                    if (const auto& ps = doc.playerStart(); ps.has_value())
                    {
                        const auto  sp = mapToScreen({ps->position.x, ps->position.z},
                                                     canvasMin);
                        const float dx = mousePosV.x - sp.x;
                        const float dy = mousePosV.y - sp.y;
                        if (dx * dx + dy * dy <= kHitR * kHitR)
                        {
                            m_psRotActive = true;
                            m_psRotOrigin = ps->yaw;
                            handled       = true;
                        }
                    }
                }

                // RMB on any entity icon → select it and begin yaw rotate drag.
                if (!handled)
                {
                    const auto& entities = doc.entities();
                    for (std::size_t ei = 0; ei < entities.size() && !handled; ++ei)
                    {
                        const EntityDef& ed = entities[ei];
                        const auto  sp = mapToScreen({ed.position.x, ed.position.z},
                                                     canvasMin);
                        const float dx = mousePosV.x - sp.x;
                        const float dy = mousePosV.y - sp.y;
                        if (dx * dx + dy * dy <= kHitR * kHitR)
                        {
                            SelectionState& sel = doc.selection();
                            sel.clear();
                            sel.items.push_back({SelectionType::Entity,
                                                 world::INVALID_SECTOR_ID, ei});
                            m_entityRotActive = true;
                            m_entityRotIdx    = ei;
                            m_entityRotOrigin = ed.yaw;
                            handled           = true;
                        }
                    }
                }

                if (!handled)
                    activeTool->onMouseDown(doc, mouseMapRaw.x, mouseMapRaw.y, 1);
            }
        }
    }

    // LMB release: commit any in-progress canvas drag even if the mouse left the canvas
    // (e.g. released over the Properties panel).  Previously gated on hovered, which left
    // drag flags dangling and caused spurious deselection on the next Properties click.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        // Commit group drag as one CompoundCommand.
        if (m_groupDragActive)
        {
            m_groupDragActive = false;
            if (m_groupDragMoved)
            {
                std::vector<std::unique_ptr<ICommand>> steps;

                // Entities
                for (const auto& eorig : m_groupEntities)
                {
                    if (eorig.idx >= doc.entities().size()) continue;
                    const glm::vec3 finalPos = doc.entities()[eorig.idx].position;
                    if (finalPos != eorig.position)
                        steps.push_back(std::make_unique<CmdMoveEntity>(
                            doc, eorig.idx, eorig.position, finalPos));
                }
                // Lights
                for (const auto& lorig : m_groupLights)
                {
                    if (lorig.idx >= doc.lights().size()) continue;
                    const glm::vec3 finalPos = doc.lights()[lorig.idx].position;
                    if (finalPos != lorig.position)
                        steps.push_back(std::make_unique<CmdMoveLight>(
                            doc, lorig.idx, lorig.position, finalPos));
                }
                // Sectors (CmdMoveSector first execute is a no-op; undo/redo apply delta)
                auto& sectors = doc.mapData().sectors;
                for (const auto& sorig : m_groupSectors)
                {
                    if (sorig.sectorId >= static_cast<world::SectorId>(sectors.size())) continue;
                    if (sorig.wallPositions.empty()) continue;
                    const glm::vec2 delta =
                        sectors[sorig.sectorId].walls[0].p0 - sorig.wallPositions[0];
                    if (glm::dot(delta, delta) > 1e-10f)
                        steps.push_back(std::make_unique<CmdMoveSector>(
                            doc, sorig.sectorId, delta));
                }

                if (!steps.empty())
                    doc.pushCommand(std::make_unique<CompoundCommand>(
                        "Move Selection", std::move(steps)));
            }
        }
        // Commit curve handle drag as an undoable command.
        else if (m_curveDragActive)
        {
            m_curveDragActive = false;
            if (m_curveDragMoved)
            {
                auto& sectors = doc.mapData().sectors;
                if (m_curveDragSectorId < sectors.size() &&
                    m_curveDragWallIndex < sectors[m_curveDragSectorId].walls.size())
                {
                    auto& wall = sectors[m_curveDragSectorId].walls[m_curveDragWallIndex];
                    // Snapshot the final dragged values, then restore to pre-drag state
                    // so that CmdSetWallCurve's constructor captures the correct old values.
                    const std::optional<glm::vec2> finalA = wall.curveControlA;
                    const std::optional<glm::vec2> finalB = wall.curveControlB;
                    wall.curveControlA = m_curveDragOldControlA;
                    wall.curveControlB = m_curveDragOldControlB;
                    doc.pushCommand(std::make_unique<CmdSetWallCurve>(
                        doc, m_curveDragSectorId, m_curveDragWallIndex,
                        finalA, finalB, wall.curveSubdivisions));
                }
                m_curveDragMoved = false;
            }
        }
        else if (m_psDragActive)
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
            if (glm::dot(d, d) > kMinDragMapSq && activeTool)
            {
                const glm::vec2 minC {std::min(m_rectSelAnchor.x, m_rectSelCurrent.x),
                                      std::min(m_rectSelAnchor.y, m_rectSelCurrent.y)};
                const glm::vec2 maxC {std::max(m_rectSelAnchor.x, m_rectSelCurrent.x),
                                      std::max(m_rectSelAnchor.y, m_rectSelCurrent.y)};
                activeTool->onRectSelect(doc, minC, maxC);
            }
            // else: trivial click — onMouseDown already ran and cleared selection.
        }
        else if (activeTool && m_pendingPlacement == PendingPlacement::None)
        {
            activeTool->onMouseUp(doc, mouseMap.x, mouseMap.y, 0);
        }
    }

    // Group drag live update: apply uniform XZ delta to all captured objects.
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_groupDragActive)
    {
        // Align anchor to the first snapped position so the delta is always
        // computed in snapped space (same pattern as SelectTool sector drag).
        if (m_groupDragFirstMove)
        {
            m_groupDragAnchor    = mouseMap;
            m_groupDragFirstMove = false;
        }
        const glm::vec2 delta = mouseMap - m_groupDragAnchor;
        auto& sectors = doc.mapData().sectors;

        for (const auto& sorig : m_groupSectors)
        {
            if (sorig.sectorId >= static_cast<world::SectorId>(sectors.size())) continue;
            auto& sec = sectors[sorig.sectorId];
            for (std::size_t wi = 0; wi < sec.walls.size() && wi < sorig.wallPositions.size(); ++wi)
            {
                sec.walls[wi].p0 = sorig.wallPositions[wi] + delta;
                if (wi < sorig.curveA.size() && sorig.curveA[wi].has_value())
                    sec.walls[wi].curveControlA = *sorig.curveA[wi] + delta;
                if (wi < sorig.curveB.size() && sorig.curveB[wi].has_value())
                    sec.walls[wi].curveControlB = *sorig.curveB[wi] + delta;
            }
        }
        for (const auto& eorig : m_groupEntities)
        {
            if (eorig.idx < doc.entities().size())
            {
                doc.entities()[eorig.idx].position.x = eorig.position.x + delta.x;
                doc.entities()[eorig.idx].position.z = eorig.position.z + delta.y;
            }
        }
        for (const auto& lorig : m_groupLights)
        {
            if (lorig.idx < doc.lights().size())
            {
                doc.lights()[lorig.idx].position.x = lorig.position.x + delta.x;
                doc.lights()[lorig.idx].position.z = lorig.position.z + delta.y;
            }
        }
        doc.markDirty();
        doc.markEntityDirty();
        m_groupDragMoved = true;
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

    // Entity live rotate (RMB drag — aim yaw toward mouse cursor).
    // Default: snap to 45° increments.  Hold Shift for free rotation.
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Right) && m_entityRotActive)
    {
        if (m_entityRotIdx < doc.entities().size())
        {
            EntityDef& ed = doc.entities()[m_entityRotIdx];
            const auto  sp = mapToScreen({ed.position.x, ed.position.z}, canvasMin);
            const float dx = mousePosV.x - sp.x;
            const float dy = mousePosV.y - sp.y;
            if (std::abs(dx) + std::abs(dy) > 2.0f)
            {
                const float rawYaw = std::atan2(dx, dy);
                constexpr float k45 = glm::pi<float>() * 0.25f;
                ed.yaw = shiftHeld ? rawYaw
                                   : std::round(rawYaw / k45) * k45;
                doc.markEntityDirty();
            }
        }
    }

    // Commit entity rotate when RMB is released.
    if (m_entityRotActive && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        if (m_entityRotIdx < doc.entities().size())
        {
            const float finalYaw = doc.entities()[m_entityRotIdx].yaw;
            if (finalYaw != m_entityRotOrigin)
                doc.pushCommand(std::make_unique<CmdRotateEntity>(
                    doc, m_entityRotIdx, m_entityRotOrigin, finalYaw));
        }
        m_entityRotActive = false;
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

    // Use the cached canvas size so the fit is exact for the current panel size.
    const float canvasW = m_lastCanvasSize.x;
    const float canvasH = m_lastCanvasSize.y;

    const float zoomX = canvasW / (mapW + 2.0f * padW);
    const float zoomY = canvasH / (mapH + 2.0f * padH);
    m_zoom = std::clamp(std::min(zoomX, zoomY), 2.0f, 400.0f);

    // Pan so the map centre lands at the canvas centre.
    const float centreMapX = (minX + maxX) * 0.5f;
    const float centreMapY = (minY + maxY) * 0.5f;
    m_panOffset = {
        canvasW * 0.5f - centreMapX * m_zoom,
        canvasH * 0.5f - centreMapY * m_zoom
    };
    m_firstFrame = false;  // suppress the default first-frame centering
}

} // namespace daedalus::editor
