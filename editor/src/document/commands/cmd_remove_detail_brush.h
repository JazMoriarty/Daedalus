#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"
#include "daedalus/world/map_data.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Removes the DetailBrush at details[brushIndex] from a sector.
/// Undo re-inserts the saved brush at the same index.
class CmdRemoveDetailBrush final : public ICommand
{
public:
    CmdRemoveDetailBrush(EditMapDocument& doc,
                         world::SectorId  sectorId,
                         std::size_t      brushIndex);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Remove Detail Brush"; }

private:
    EditMapDocument&    m_doc;
    world::SectorId     m_sectorId;
    std::size_t         m_brushIndex;
    world::DetailBrush  m_savedBrush;  ///< Captured at execute() time for undo.
};

} // namespace daedalus::editor
