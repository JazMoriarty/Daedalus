#include "floor_layer_panel.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/world/map_data.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
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

// ─── draw ──────────────────────────────────────────────────────────────────────

void FloorLayerPanel::draw(EditMapDocument& doc)
{
    ImGui::Begin("Floor Layers");

    const auto& sectors = doc.mapData().sectors;
    const auto  n       = static_cast<world::SectorId>(sectors.size());

    if (n == 0)
    {
        ImGui::TextDisabled("(no sectors)");
        ImGui::TextDisabled("Draw sectors to see floor groups.");
        ImGui::End();
        return;
    }

    // Helper: Y range of a group.
    auto groupRange = [&](const std::vector<world::SectorId>& g)
        -> std::pair<float, float>
    {
        float lo =  1e9f, hi = -1e9f;
        for (const auto sid : g)
        {
            lo = std::min(lo, sectors[sid].floorHeight);
            hi = std::max(hi, sectors[sid].ceilHeight);
        }
        return {lo, hi};
    };

    // ── Selected-sector lookup ────────────────────────────────────────────
    // Any selection item that references a sector ID is used to highlight
    // the relevant floor group and offer a jump shortcut.
    world::SectorId selSectorId = world::INVALID_SECTOR_ID;
    {
        const SelectionState& sel = doc.selection();
        for (const auto& item : sel.items)
        {
            if (item.sectorId != world::INVALID_SECTOR_ID)
            { selSectorId = item.sectorId; break; }
        }
    }

    // ── Show All toggle ──────────────────────────────────────────────────────
    bool showAll = !m_filterEnabled;
    if (ImGui::Checkbox("Show All##flall", &showAll))
        m_filterEnabled = !showAll;
    ImGui::SameLine();
    ImGui::TextDisabled("(dims sectors outside edit height in 2D)");

    // ── Edit height slider ────────────────────────────────────────────────
    float mapMinY =  1e9f;
    float mapMaxY = -1e9f;
    for (const auto& sec : sectors)
    {
        mapMinY = std::min(mapMinY, sec.floorHeight);
        mapMaxY = std::max(mapMaxY, sec.ceilHeight);
    }
    if (mapMinY > mapMaxY) { mapMinY = -16.0f; mapMaxY = 16.0f; }

    ImGui::Spacing();
    ImGui::SeparatorText("Edit Height");

    // Always show the slider; disable interaction when Show All is checked so
    // the current position remains visible.
    if (!m_filterEnabled) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##editH", &m_editHeight, mapMinY, mapMaxY, "%.2f m");
    if (!m_filterEnabled)
    {
        ImGui::EndDisabled();
        ImGui::TextDisabled("Enable by unchecking Show All.");
    }
    else
    {
        // Right-click on slider still works as a secondary shortcut.
        if (ImGui::BeginPopupContextItem("##editHCtx"))
        {
            ImGui::InputText("Name##prcname", m_addPresetBuf, sizeof(m_addPresetBuf));
            ImGui::SameLine();
            if (ImGui::SmallButton("Save##prcsave"))
            {
                m_presets.push_back({std::string(m_addPresetBuf), m_editHeight});
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // Jump-to-selection button: appears when the selected sector is not
    // currently visible at the edit height.
    if (selSectorId < n)
    {
        const auto& selSec = sectors[selSectorId];
        const bool covered = m_filterEnabled &&
                             m_editHeight >= selSec.floorHeight &&
                             m_editHeight <= selSec.ceilHeight;
        if (!covered)
        {
            if (ImGui::SmallButton("Jump to selected sector##jsel"))
            {
                m_editHeight    = (selSec.floorHeight + selSec.ceilHeight) * 0.5f;
                m_filterEnabled = true;
            }
        }
    }

    // ── Presets ───────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Presets");

    // Inline add-preset row: text field + Add button + current Y readout.
    {
        const float addBtnW = ImGui::CalcTextSize("Add").x + ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f;
        const float yReadW  = ImGui::CalcTextSize("-99.99").x + 6.0f;
        ImGui::SetNextItemWidth(
            std::max(20.0f, ImGui::GetContentRegionAvail().x - addBtnW - yReadW - ImGui::GetStyle().ItemSpacing.x * 2.0f));
        ImGui::InputText("##pnew", m_addPresetBuf, sizeof(m_addPresetBuf));
        ImGui::SameLine();
        if (ImGui::SmallButton("Add##padd") && std::strlen(m_addPresetBuf) > 0)
            m_presets.push_back({std::string(m_addPresetBuf), m_editHeight});
        ImGui::SameLine();
        ImGui::TextDisabled("%.2f", m_editHeight);
    }

    for (std::size_t pi = 0; pi < m_presets.size(); ++pi)
    {
        ImGui::PushID(static_cast<int>(pi));
        if (ImGui::SmallButton(m_presets[pi].name.c_str()))
        {
            m_editHeight    = m_presets[pi].y;
            m_filterEnabled = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%.2f m", m_presets[pi].y);
        ImGui::SameLine();
        if (ImGui::SmallButton("x##delpset"))
        {
            m_presets.erase(m_presets.begin() + static_cast<std::ptrdiff_t>(pi));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (m_presets.empty())
        ImGui::TextDisabled("No presets — type a name above and click Add.");

    // ── Floor groups ────────────────────────────────────────────────────────
    // Step 1: BFS partition by floor/ceiling portal links.
    std::vector<bool>                         assigned(n, false);
    std::vector<std::vector<world::SectorId>> groups;
    for (world::SectorId i = 0; i < n; ++i)
    {
        if (assigned[i]) continue;
        std::vector<world::SectorId> layer = collectLayer(doc, i);
        for (const auto id : layer) assigned[id] = true;
        groups.push_back(std::move(layer));
    }

    // Step 2: Merge groups with the same Y range.
    // For a flat single-level map every sector is its own BFS group (no
    // SoS portals), but they should all show as one floor level.  Two groups
    // merge when their floor-min and ceil-max are within a small epsilon.
    constexpr float kYMergeEps = 0.01f;
    for (std::size_t i = 0; i < groups.size(); ++i)
    {
        const auto [aLo, aHi] = groupRange(groups[i]);
        for (std::size_t j = i + 1; j < groups.size(); )
        {
            const auto [bLo, bHi] = groupRange(groups[j]);
            if (std::abs(aLo - bLo) < kYMergeEps && std::abs(aHi - bHi) < kYMergeEps)
            {
                groups[i].insert(groups[i].end(),
                                  groups[j].begin(), groups[j].end());
                groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(j));
            }
            else { ++j; }
        }
    }

    // Step 3: Sort groups bottom-to-top by their minimum floor height.
    std::sort(groups.begin(), groups.end(),
        [&](const std::vector<world::SectorId>& a,
            const std::vector<world::SectorId>& b) noexcept
        {
            return groupRange(a).first < groupRange(b).first;
        });

    ImGui::Spacing();
    ImGui::SeparatorText("Floor Groups");
    ImGui::TextDisabled("Groups are sectors at the same height.  Click to jump.");
    ImGui::Spacing();

    for (std::size_t gi = 0; gi < groups.size(); ++gi)
    {
        const auto& grp = groups[gi];
        const auto [minY, maxY] = groupRange(grp);
        const float midY = (minY + maxY) * 0.5f;

        const bool isActiveGroup = m_filterEnabled &&
                                   m_editHeight >= minY && m_editHeight <= maxY;
        const bool hasSelSector  = selSectorId < n &&
            std::find(grp.begin(), grp.end(), selSectorId) != grp.end();

        if (isActiveGroup)
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.45f, 0.75f, 0.5f));
        else if (hasSelSector)
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.65f, 0.50f, 0.10f, 0.4f));

        // Tree node label: Y range + sector count.
        char nodeLabel[80];
        std::snprintf(nodeLabel, sizeof(nodeLabel),
                      "%.1f \xe2\x80\x93 %.1f m  (%zu sector%s)##fg%zu",
                      minY, maxY, grp.size(), grp.size() == 1u ? "" : "s", gi);

        const ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanFullWidth;

        const bool open = ImGui::TreeNodeEx(nodeLabel, nodeFlags);

        // Single-click on the label (not the expand arrow) jumps edit height.
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            m_editHeight    = midY;
            m_filterEnabled = true;
        }

        if (isActiveGroup || hasSelSector)
            ImGui::PopStyleColor();

        if (open)
        {
            for (const auto sid : grp)
            {
                const auto& sec = sectors[sid];
                const bool  isSel = (sid == selSectorId);

                char secLabel[64];
                std::snprintf(secLabel, sizeof(secLabel),
                              "Sector %u  (%.2f \xe2\x80\x93 %.2f m)",
                              static_cast<unsigned>(sid),
                              sec.floorHeight, sec.ceilHeight);

                if (isSel)
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                         ImVec4(1.0f, 0.85f, 0.2f, 1.0f));

                ImGui::TreeNodeEx(secLabel,
                    ImGuiTreeNodeFlags_Leaf |
                    ImGuiTreeNodeFlags_NoTreePushOnOpen |
                    ImGuiTreeNodeFlags_SpanFullWidth);

                if (isSel)
                    ImGui::PopStyleColor();
            }
            ImGui::TreePop();
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Sectors outside edit height are shown at 20%% opacity in 2D.");

    ImGui::End();
}

} // namespace daedalus::editor
