#include "cmd_save_prefab.h"

#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/world/world_types.h"

#include <algorithm>
#include <limits>

namespace daedalus::editor
{

CmdSavePrefab::CmdSavePrefab(EditMapDocument& doc, std::string name)
    : m_doc(doc)
{
    m_prefab.name = std::move(name);

    const SelectionState& sel = doc.selection();
    if (sel.uniformType() != SelectionType::Sector || sel.items.empty())
        return;  // Nothing to capture; execute/undo will be no-ops.

    const auto& mapSectors = doc.mapData().sectors;

    // Compute the XZ AABB over all wall vertices in the selected sectors.
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    for (const auto& item : sel.items)
    {
        const world::SectorId sid = item.sectorId;
        if (sid >= mapSectors.size()) continue;
        for (const auto& wall : mapSectors[sid].walls)
        {
            minX = std::min(minX, wall.p0.x);
            maxX = std::max(maxX, wall.p0.x);
            minZ = std::min(minZ, wall.p0.y);  // wall.p0.y == world Z
            maxZ = std::max(maxZ, wall.p0.y);
        }
    }

    const float pivotX = (minX + maxX) * 0.5f;
    const float pivotZ = (minZ + maxZ) * 0.5f;

    // Capture sectors: shift wall positions to be pivot-relative, strip portals.
    m_prefab.sectors.reserve(sel.items.size());
    for (const auto& item2 : sel.items)
    {
        const world::SectorId sid2 = item2.sectorId;
        if (sid2 >= mapSectors.size()) continue;
        world::Sector copy = mapSectors[sid2];
        for (auto& wall : copy.walls)
        {
            wall.p0.x          -= pivotX;
            wall.p0.y          -= pivotZ;
            wall.portalSectorId = world::INVALID_SECTOR_ID;  // strip portals
        }
        m_prefab.sectors.push_back(std::move(copy));
    }

    // Capture entities whose XZ position falls within the selection AABB.
    for (const EntityDef& ed : doc.entities())
    {
        if (ed.position.x >= minX && ed.position.x <= maxX &&
            ed.position.z >= minZ && ed.position.z <= maxZ)
        {
            EntityDef copy = ed;
            copy.position.x -= pivotX;
            copy.position.z -= pivotZ;
            m_prefab.entities.push_back(std::move(copy));
        }
    }
}

void CmdSavePrefab::execute()
{
    m_insertedIdx = m_doc.prefabs().size();
    m_doc.prefabs().push_back(m_prefab);
}

void CmdSavePrefab::undo()
{
    auto& prefabs = m_doc.prefabs();
    if (m_insertedIdx < prefabs.size())
        prefabs.erase(prefabs.begin() + static_cast<std::ptrdiff_t>(m_insertedIdx));
}

} // namespace daedalus::editor
