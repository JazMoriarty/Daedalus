#include "staircase_generator_panel.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/compound_command.h"
#include "daedalus/world/map_data.h"
#include "document/commands/cmd_set_sector_floor_shape.h"
#include "document/commands/cmd_draw_sector.h"
#include "document/commands/cmd_link_portal.h"
#include "document/commands/cmd_set_sector_heights.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <memory>
#include <vector>

namespace daedalus::editor
{

// ─── draw ─────────────────────────────────────────────────────────────────────

void StaircaseGeneratorPanel::draw(EditMapDocument& doc)
{
    ImGui::Begin("Staircase Generator");

    // Mode tabs.
    const char* tabLabels[] = {"Visual Stair", "Sector Chain"};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##sgmode", &m_modeTab, tabLabels, 2);
    ImGui::Spacing();

    // Shared parameters.
    ImGui::DragInt("Steps##sg",       &m_stepCount,   0.5f, 1, 64);
    ImGui::DragFloat("Riser (m)##sg", &m_riserHeight, 0.01f, 0.01f, 4.0f);
    ImGui::DragFloat("Tread (m)##sg", &m_treadDepth,  0.01f, 0.1f, 8.0f);
    ImGui::DragFloat("Direction##sg", &m_dirDeg,      1.0f,  0.0f, 0.0f, "%.1f\xc2\xb0");
    if (m_modeTab == 1)
        ImGui::DragFloat("Width (m)##sg", &m_corridorWidth, 0.1f, 0.5f, 20.0f);

    ImGui::Spacing();

    // Validate selection.
    const auto& sel      = doc.selection();
    const bool  hasSec   = (sel.type == SelectionType::Sector && !sel.sectors.empty());
    const world::SectorId sid = hasSec ? sel.sectors.front() : world::INVALID_SECTOR_ID;

    if (!hasSec)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Generate##sg"))
    {
        if (hasSec)
        {
            m_statusMsg.clear();
            m_statusIsError = false;
            if (m_modeTab == 0)
                applyVisualStair(doc, sid);
            else
                applySectorChain(doc, sid);
        }
    }

    if (!hasSec)
    {
        ImGui::EndDisabled();
        ImGui::TextDisabled("(select a sector first)");
    }

    if (!m_statusMsg.empty())
    {
        ImGui::Spacing();
        if (m_statusIsError)
            ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", m_statusMsg.c_str());
        else
            ImGui::TextDisabled("%s", m_statusMsg.c_str());
    }

    ImGui::End();
}

// ─── applyVisualStair ─────────────────────────────────────────────────────────
// Sets the selected sector to FloorShape::VisualStairs with the current profile.

void StaircaseGeneratorPanel::applyVisualStair(EditMapDocument& doc,
                                               world::SectorId  sid)
{
    world::StairProfile profile;
    profile.stepCount      = static_cast<unsigned>(std::max(m_stepCount, 1));
    profile.riserHeight    = m_riserHeight;
    profile.treadDepth     = m_treadDepth;
    profile.directionAngle = m_dirDeg * glm::pi<float>() / 180.0f;

    doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
        doc, sid, world::FloorShape::VisualStairs, profile));

    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "Applied %d-step visual stair to sector %u.",
                  m_stepCount, static_cast<unsigned>(sid));
    m_statusMsg     = buf;
    m_statusIsError = false;
}

// ─── applySectorChain ─────────────────────────────────────────────────────────
// Generates a chain of new step sectors starting adjacent to the selected sector.
//
// Each step is a rectangular sector whose width is m_corridorWidth and whose
// tread depth is m_treadDepth.  The first step's entry wall aligns with the
// selected sector's exit centroid and runs perpendicular to m_dirDeg.
//
// The chain is built as a CompoundCommand so the entire sequence is undoable in
// one Ctrl+Z.

void StaircaseGeneratorPanel::applySectorChain(EditMapDocument& doc,
                                               world::SectorId  startSid)
{
    const auto& sectors = doc.mapData().sectors;
    if (startSid >= sectors.size())
    {
        m_statusMsg     = "Invalid sector selection.";
        m_statusIsError = true;
        return;
    }

    const int steps = std::max(m_stepCount, 1);

    // Step direction unit vector in XZ map space.
    const float  angleRad = m_dirDeg * glm::pi<float>() / 180.0f;
    const glm::vec2 fwd{std::cos(angleRad), std::sin(angleRad)};
    const glm::vec2 right{-fwd.y, fwd.x};  // perpendicular

    // Find the centroid of the selected sector to anchor the chain origin.
    glm::vec2 centroid{0.0f, 0.0f};
    {
        const auto& sec = sectors[startSid];
        if (sec.walls.empty())
        {
            m_statusMsg     = "Selected sector has no walls.";
            m_statusIsError = true;
            return;
        }
        for (const auto& w : sec.walls) centroid += w.p0;
        centroid /= static_cast<float>(sec.walls.size());
    }

    const float baseFloor = sectors[startSid].ceilHeight;  // chain starts at ceiling of start.
    const float half      = m_corridorWidth * 0.5f;

    std::vector<std::unique_ptr<ICommand>> steps_cmds;

    // Record indices of the new sectors so we can link portals between them.
    std::vector<world::SectorId> newIds;
    newIds.reserve(static_cast<std::size_t>(steps));

    for (int i = 0; i < steps; ++i)
    {
        // Origin of this step's near (entry) face.
        const glm::vec2 nearCentre = centroid + fwd * (static_cast<float>(i) * m_treadDepth);

        // Four corners of the step quad (CCW from above).
        const glm::vec2 p0 = nearCentre - right * half;              // entry left
        const glm::vec2 p1 = nearCentre + fwd   * m_treadDepth - right * half;  // exit left
        const glm::vec2 p2 = nearCentre + fwd   * m_treadDepth + right * half;  // exit right
        const glm::vec2 p3 = nearCentre + right * half;              // entry right

        world::Sector sec;
        sec.floorHeight = baseFloor + static_cast<float>(i) * m_riserHeight;
        sec.ceilHeight  = sec.floorHeight + 4.0f;  // default ceiling clearance

        // Build walls in CCW winding order.
        sec.walls.resize(4);
        sec.walls[0].p0 = p0;
        sec.walls[1].p0 = p1;
        sec.walls[2].p0 = p2;
        sec.walls[3].p0 = p3;

        steps_cmds.push_back(std::make_unique<CmdDrawSector>(doc, sec));

        // Record where the new sector will be (at time of compound execution).
        // CmdDrawSector appends to sectors, so the index is sectors.size() + i
        // at the point when this compound executes.
        // We store the pre-execution size + offset; actual linking uses CmdLinkPortal
        // which resolves at execute() time using the live sector list.
        newIds.push_back(static_cast<world::SectorId>(sectors.size() + i));
    }

    // Link adjacent steps via wall portals (wall 0 = entry face, wall 1 = exit face).
    // Step i's exit wall (index 1, going toward fwd) aligns with step i+1's entry (index 3).
    // Note: portal linking requires matching geometry; we rely on CmdLinkPortal's geometry
    // match logic.  For the sector-chain case the walls are perfectly aligned so auto-match
    // will succeed.  We push the links as part of the same CompoundCommand.
    for (int i = 0; i + 1 < steps; ++i)
    {
        // Exit wall of step i is wall index 1 (p1→p2 direction).
        // Entry wall of step i+1 is wall index 3 (p3→p0 direction, reversed).
        // CmdLinkPortal links two specified walls bidirectionally.
        steps_cmds.push_back(std::make_unique<CmdLinkPortal>(
            doc, newIds[i], 1u, newIds[i + 1], 3u));
    }

    doc.pushCommand(std::make_unique<CompoundCommand>(
        "Generate Sector Chain Staircase", std::move(steps_cmds)));

    char buf[80];
    std::snprintf(buf, sizeof(buf), "Generated %d-step sector chain.", steps);
    m_statusMsg     = buf;
    m_statusIsError = false;
}

} // namespace daedalus::editor
