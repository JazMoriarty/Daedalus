#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"
#include "daedalus/world/map_data.h"

#include <optional>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets or clears the HeightfieldFloor on a sector.
///
/// When newData has a value the sector's heightfield is replaced and the
/// SectorFlags::HasHeightfield flag is set.  When newData is std::nullopt the
/// heightfield is cleared and HasHeightfield is cleared.
///
/// This command does NOT automatically change floorShape; use CmdSetSectorFloorShape
/// in a CompoundCommand if both should change together.
class CmdSetHeightfield final : public ICommand
{
public:
    CmdSetHeightfield(EditMapDocument&                          doc,
                      world::SectorId                           sectorId,
                      std::optional<world::HeightfieldFloor>    newData);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Heightfield"; }

private:
    EditMapDocument&                        m_doc;
    world::SectorId                         m_sectorId;
    std::optional<world::HeightfieldFloor>  m_newData;
    std::optional<world::HeightfieldFloor>  m_oldData;
    world::SectorFlags                      m_oldFlags = world::SectorFlags::None;

    void applyData(const std::optional<world::HeightfieldFloor>& data,
                   world::SectorFlags                            flags);
};

} // namespace daedalus::editor
