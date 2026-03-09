// cmd_delete_sector.h
// Deletes a sector and fixes up portal IDs in all remaining sectors.
// Undo re-inserts the sector at its original index and restores portals.

#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace daedalus::editor
{

class EditMapDocument;

class CmdDeleteSector final : public ICommand
{
public:
    /// @param doc      The document to act on.
    /// @param sectorId Index of the sector to delete.
    CmdDeleteSector(EditMapDocument& doc, world::SectorId sectorId);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    {
        return "Delete Sector";
    }

private:
    struct PortalRecord
    {
        std::size_t     sectorIdx; ///< Sector index in pre-delete ordering.
        std::size_t     wallIdx;
        world::SectorId oldPortal; ///< Portal value before execute().
    };

    EditMapDocument&          m_doc;
    world::SectorId           m_sectorId;
    world::Sector             m_savedSector;  ///< Full copy of the deleted sector.
    std::vector<PortalRecord> m_savedPortals; ///< Walls whose portals are touched by execute().
};

} // namespace daedalus::editor
