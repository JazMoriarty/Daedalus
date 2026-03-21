#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"
#include "daedalus/world/map_data.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Replaces the DetailBrush at Sector::details[brushIndex] with a new value.
/// Used by the property inspector when editing individual brush parameters.
class CmdSetDetailBrush final : public ICommand
{
public:
    CmdSetDetailBrush(EditMapDocument&       doc,
                      world::SectorId        sectorId,
                      std::size_t            brushIndex,
                      world::DetailBrush     newBrush);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Edit Detail Brush"; }

private:
    EditMapDocument&    m_doc;
    world::SectorId     m_sectorId;
    std::size_t         m_brushIndex;
    world::DetailBrush  m_newBrush;
    world::DetailBrush  m_oldBrush;
};

} // namespace daedalus::editor
