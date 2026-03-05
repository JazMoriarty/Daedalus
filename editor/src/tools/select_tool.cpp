#include "select_tool.h"
#include "geometry_utils.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

namespace daedalus::editor
{

void SelectTool::onMouseDown(EditMapDocument& doc,
                              float mapX, float mapZ,
                              int   button)
{
    if (button != 0) return;  // left button only

    const world::SectorId hit = hitTestSector(doc.mapData(), mapX, mapZ);
    auto& sel = doc.selection();

    if (hit == world::INVALID_SECTOR_ID)
    {
        sel.clear();
    }
    else
    {
        sel.clear();
        sel.type = SelectionType::Sector;
        sel.sectors.push_back(hit);
    }
}

world::SectorId SelectTool::hitTestSector(const world::WorldMapData& map,
                                           float mapX, float mapZ) const noexcept
{
    const glm::vec2 p{mapX, mapZ};

    // Test sectors in reverse order (top-most drawn sector wins).
    for (std::size_t i = map.sectors.size(); i > 0; --i)
    {
        const std::size_t        si     = i - 1;
        const world::Sector&     sector = map.sectors[si];
        const std::size_t        n      = sector.walls.size();

        if (n < 3) continue;

        std::vector<glm::vec2> verts;
        verts.reserve(n);
        for (const auto& wall : sector.walls)
            verts.push_back(wall.p0);

        if (geometry::pointInPolygon(p, verts))
            return static_cast<world::SectorId>(si);
    }
    return world::INVALID_SECTOR_ID;
}

} // namespace daedalus::editor
