// cmd_paste_sector.h
// Pastes the editor clipboard sector into the map at a given XZ offset.
//
// The clipboard copy is taken at construction time (snapshot), so changing
// the clipboard later does not affect an already-created paste command.
// All portal links in the pasted copy are cleared.
//
// Undo/redo pattern identical to CmdDuplicateSector.

#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

class CmdPasteSector final : public ICommand
{
public:
    /// @param source  Clipboard sector (already portal-cleared).
    /// @param offset  XZ offset applied to every wall vertex of the paste.
    CmdPasteSector(EditMapDocument&    doc,
                   const world::Sector& source,
                   glm::vec2            offset);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Paste Sector"; }

private:
    EditMapDocument& m_doc;
    world::Sector    m_copy;            ///< Offset copy ready to insert.
    world::SectorId  m_insertedId = 0;  ///< Index assigned on first execute().
    bool             m_executed   = false;
};

} // namespace daedalus::editor
