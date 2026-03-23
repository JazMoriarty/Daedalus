#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"
#include "daedalus/world/map_data.h"

#include <optional>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets or clears the ceiling HeightfieldFloor on a sector.
///
/// When newData has a value the sector's ceiling heightfield is replaced.
/// When newData is std::nullopt the ceiling heightfield is cleared.
///
/// This command does NOT automatically change ceilingShape; use CmdSetSectorCeilingShape
/// in a CompoundCommand if both should change together.
class CmdSetCeilingHeightfield final : public ICommand
{
public:
    CmdSetCeilingHeightfield(EditMapDocument&                          doc,
                             world::SectorId                           sectorId,
                             std::optional<world::HeightfieldFloor>    newData);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Ceiling Heightfield"; }

private:
    EditMapDocument&                        m_doc;
    world::SectorId                         m_sectorId;
    std::optional<world::HeightfieldFloor>  m_newData;
    std::optional<world::HeightfieldFloor>  m_oldData;

    void applyData(const std::optional<world::HeightfieldFloor>& data);
};

} // namespace daedalus::editor
