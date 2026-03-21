#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <optional>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets the floor shape mode of an existing sector and, optionally, its stair profile.
/// Used when switching between FloorShape::Flat, FloorShape::VisualStairs, and
/// FloorShape::Heightfield (the heightfield data itself is managed by CmdSetHeightfield).
class CmdSetSectorFloorShape final : public ICommand
{
public:
    CmdSetSectorFloorShape(EditMapDocument&                    doc,
                           world::SectorId                     sectorId,
                           world::FloorShape                   newShape,
                           std::optional<world::StairProfile>  newProfile);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Sector Floor Shape"; }

private:
    EditMapDocument&                    m_doc;
    world::SectorId                     m_sectorId;
    world::FloorShape                   m_newShape;
    std::optional<world::StairProfile>  m_newProfile;
    world::FloorShape                   m_oldShape   = world::FloorShape::Flat;
    std::optional<world::StairProfile>  m_oldProfile;
};

} // namespace daedalus::editor
