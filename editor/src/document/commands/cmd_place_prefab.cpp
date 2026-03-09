#include "cmd_place_prefab.h"

#include "cmd_draw_sector.h"
#include "cmd_place_entity.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdPlacePrefab::CmdPlacePrefab(EditMapDocument& doc,
                                const PrefabDef& prefab,
                                glm::vec2        placementXZ)
{
    // Sectors — offset each wall's XZ position by the placement point.
    for (const world::Sector& s : prefab.sectors)
    {
        world::Sector placed = s;
        for (auto& wall : placed.walls)
        {
            wall.p0.x += placementXZ.x;
            wall.p0.y += placementXZ.y;  // wall.p0.y == world Z
        }
        m_steps.push_back(std::make_unique<CmdDrawSector>(doc, std::move(placed)));
    }

    // Entities — offset the XZ components of the world-space position.
    for (const EntityDef& e : prefab.entities)
    {
        EntityDef placed = e;
        placed.position.x += placementXZ.x;
        placed.position.z += placementXZ.y;  // placementXZ.y maps to world Z
        m_steps.push_back(std::make_unique<CmdPlaceEntity>(doc, std::move(placed)));
    }
}

void CmdPlacePrefab::execute()
{
    for (auto& cmd : m_steps)
        cmd->execute();
}

void CmdPlacePrefab::undo()
{
    for (auto it = m_steps.rbegin(); it != m_steps.rend(); ++it)
        (*it)->undo();
}

} // namespace daedalus::editor
