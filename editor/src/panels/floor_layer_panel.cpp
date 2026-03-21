#include "floor_layer_panel.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace daedalus::editor
{

// ─── collectLayer ─────────────────────────────────────────────────────────────
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

// ─── isSectorVisible ─────────────────────────────────────────────────────────

bool FloorLayerPanel::isSectorVisible(world::SectorId id) const noexcept
{
    if (!m_filterEnabled) return true;
    return std::binary_search(m_visibleSectors.begin(), m_visibleSectors.end(), id);
}

// ─── draw ─────────────────────────────────────────────────────────────────────

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

    // Partition all sectors into vertical stacks (portal-linked groups).
    // Each sector appears in exactly one group.
    std::vector<bool>                           assigned(n, false);
    std::vector<std::vector<world::SectorId>>   groups;

    for (world::SectorId i = 0; i < n; ++i)
    {
        if (assigned[i]) continue;
        std::vector<world::SectorId> layer = collectLayer(doc, i);
        for (const auto id : layer) assigned[id] = true;
        groups.push_back(std::move(layer));
    }

    // "Show all" row.
    {
        const bool selected = !m_filterEnabled;
        if (ImGui::Selectable("All floors##flall", selected))
        {
            m_filterEnabled = false;
            m_visibleSectors.clear();
        }
    }

    ImGui::Separator();

    // Per-group rows.
    for (std::size_t gi = 0; gi < groups.size(); ++gi)
    {
        const auto& grp = groups[gi];

        // Determine the Y range of the group for display.
        float minY =  1e9f;
        float maxY = -1e9f;
        for (const auto sid : grp)
        {
            minY = std::min(minY, sectors[sid].floorHeight);
            maxY = std::max(maxY, sectors[sid].ceilHeight);
        }

        char label[64];
        std::snprintf(label, sizeof(label), "Floor %zu  (Y %.1f \xe2\x80\x93 %.1f)  %zu sector(s)##fl%zu",
                      gi, minY, maxY, grp.size(), gi);

        const bool isThisGroup = m_filterEnabled &&
                                 m_visibleSectors == grp;

        if (ImGui::Selectable(label, isThisGroup))
        {
            m_filterEnabled  = true;
            m_visibleSectors = grp;
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Dims sectors not in the selected floor in the 2D viewport.");

    ImGui::End();
}

} // namespace daedalus::editor
