#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <cstddef>

namespace daedalus::editor
{

class EditMapDocument;

/// Replaces the WallFlags on a single wall.
class CmdSetWallFlags final : public ICommand
{
public:
    CmdSetWallFlags(EditMapDocument& doc,
                    world::SectorId  sectorId,
                    std::size_t      wallIndex,
                    world::WallFlags newFlags);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Wall Flags"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    std::size_t      m_wallIndex;
    world::WallFlags m_newFlags;
    world::WallFlags m_oldFlags = world::WallFlags::None;
};

} // namespace daedalus::editor
