#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Replaces the SectorFlags on a single sector.
class CmdSetSectorFlags final : public ICommand
{
public:
    CmdSetSectorFlags(EditMapDocument&   doc,
                      world::SectorId    sectorId,
                      world::SectorFlags newFlags);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Sector Flags"; }

private:
    EditMapDocument&   m_doc;
    world::SectorId    m_sectorId;
    world::SectorFlags m_newFlags;
    world::SectorFlags m_oldFlags = world::SectorFlags::None;
};

} // namespace daedalus::editor
