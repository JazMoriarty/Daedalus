#include "floor_layer_panel.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace daedalus::editor
{

// ─── collectLayer ─────────────────────────────────────────────────────────────────────────
// BFS over floorPortalSectorId / ceilPortalSectorId links from a seed sector
// to collect all sectors that belong to the same vertical stack.

/*static*/
std::vector<world::SectorId> FloorLayerPanel::collectLayer(const EditMapDocument& doc,
                                                            world::SectorId        seed)
{
    const auto& sectors = doc.mapData().sectors;
    const auto  n       = static_cast<world::SectorId>(sectors.size());
    if (seed >= n) return {};

    std::vector<bool>            visited(n, false);
    std::vector<world::SectorId> queue;
    std::vector<world::SectorId> result;

    visited[seed] = true;
    queue.push_back(seed);

    while (!queue.empty())
    {
        const world::SectorId cur = queue.back();
        queue.pop_back();
        result.push_back(cur);

        const auto& sec = sectors[cur];
        for (const world::SectorId neighbour :
             {sec.floorPortalSectorId, sec.ceilPortalSectorId})
        {
            if (neighbour != world::INVALID_SECTOR_ID &&
                neighbour < n && !visited[neighbour])
            {
                visited[neighbour] = true;
                queue.push_back(neighbour);
            }
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

// ─── isSectorFullOpacity ─────────────────────────────────────────────────────────────────

bool FloorLayerPanel::isSectorFullOpacity(float floorH, float ceilH) const noexcept
{
    if (!m_filterEnabled) return true;
    // Full opacity when the edit height falls within [floorH, ceilH].
    return m_editHeight >= floorH && m_editHeight <= ceilH;
}

// ─── draw ──────────────────────────────────────────────────────────────────────────────

void FloorLayerPanel::draw(EditMapDocument& doc)
{
    ImGui::Begin("Floor Layers");

    const auto& sectors = doc.mapData().sectors;
    const auto  n       = static_cast<world::SectorId>(sectors.size());

    if (n == 0)
    {
        ImGui::TextDisabled("(no sectors)");
        ImGui::End();
        return;
    }

    // ── Show All toggle ─────────────────────────────────────────────────────────────
    bool showAll = !m_filterEnabled;
    if (ImGui::Checkbox("Show All##flall", &showAll))
        m_filterEnabled = !showAll;

    // ── Edit height slider ──────────────────────────────────────────────────────────
    // Compute map Y bounds for slider range.
    float mapMinY =  1e9f;
    float mapMaxY = -1e9f;
    for (const auto& sec : sectors)
    {
        mapMinY = std::min(mapMinY, sec.floorHeight);
        mapMaxY = std::max(mapMaxY, sec.ceilHeight);
    }
    if (mapMinY > mapMaxY) { mapMinY = -16.0f; mapMaxY = 16.0f; }

    ImGui::Spacing();
    ImGui::TextDisabled("Edit height (world Y)");
    if (m_filterEnabled)
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##editH", &m_editHeight, mapMinY, mapMaxY, "%.2f m");
        // Right-click to save a named preset.
        if (ImGui::BeginPopupContextItem("##editHCtx"))
        {
            static char s_presetName[64] = "Floor";
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputText("Name##pname", s_presetName, sizeof(s_presetName));
            ImGui::SameLine();
            if (ImGui::SmallButton("Save##psave"))
            {
                m_presets.push_back({std::string(s_presetName), m_editHeight});
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::TextDisabled("(right-click to save preset)");
    }
    else
    {
        ImGui::TextDisabled("Disabled — enable by unchecking Show All");
    }

    // ── Presets ──────────────────────────────────────────────────────────────────
    if (!m_presets.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Presets");
        for (std::size_t pi = 0; pi < m_presets.size(); ++pi)
        {
            ImGui::PushID(static_cast<int>(pi));
            if (ImGui::SmallButton(m_presets[pi].name.c_str()))
            {
                m_editHeight    = m_presets[pi].y;
                m_filterEnabled = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x##delpset"))
            {
                m_presets.erase(m_presets.begin() + static_cast<std::ptrdiff_t>(pi));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    // ── Floor group list ───────────────────────────────────────────────────────────
    // Partition sectors into portal-linked vertical stacks.
    std::vector<bool>                         assigned(n, false);
    std::vector<std::vector<world::SectorId>> groups;
    for (world::SectorId i = 0; i < n; ++i)
    {
        if (assigned[i]) continue;
        std::vector<world::SectorId> layer = collectLayer(doc, i);
        for (const auto id : layer) assigned[id] = true;
        groups.push_back(std::move(layer));
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Floor Groups");

    for (std::size_t gi = 0; gi < groups.size(); ++gi)
    {
        const auto& grp = groups[gi];

        // Y range of this group.
        float minY =  1e9f;
        float maxY = -1e9f;
        for (const auto sid : grp)
        {
            minY = std::min(minY, sectors[sid].floorHeight);
            maxY = std::max(maxY, sectors[sid].ceilHeight);
        }
        const float midY = (minY + maxY) * 0.5f;

        // Highlight the group that contains the current edit height.
        const bool isActiveGroup = m_filterEnabled &&
                                   m_editHeight >= minY && m_editHeight <= maxY;

        char label[72];
        std::snprintf(label, sizeof(label),
                      "Floor %zu  (%.1f \xe2\x80\x93 %.1f m)  %zu sec##fg%zu",
                      gi, minY, maxY, grp.size(), gi);

        // Clicking a group jumps the edit height to its midpoint.
        if (ImGui::Selectable(label, isActiveGroup))
        {
            m_editHeight    = midY;
            m_filterEnabled = true;
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Sectors outside edit height are shown at 20%% opacity in 2D.");

    ImGui::End();
}

} // namespace daedalus::editor
