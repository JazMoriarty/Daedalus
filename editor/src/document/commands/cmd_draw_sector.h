#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/map_data.h"

#include <cstddef>

namespace daedalus::editor
{

class EditMapDocument;

/// Appends a new sector to the map.  Undo removes it again.
class CmdDrawSector final : public ICommand
{
public:
    CmdDrawSector(EditMapDocument& doc, world::Sector sector);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Draw Sector"; }

private:
    EditMapDocument& m_doc;
    world::Sector    m_sector;
    std::size_t      m_insertedIndex = 0;
};

} // namespace daedalus::editor
