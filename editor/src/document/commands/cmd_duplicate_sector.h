// cmd_duplicate_sector.h
// Duplicates a sector by copying it by value and offsetting all wall vertices
// by one grid step diagonally so the copy does not overlap the original.
//
// Self-contained: no clipboard dependency.

#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

namespace daedalus::editor
{

class EditMapDocument;

class CmdDuplicateSector final : public ICommand
{
public:
    /// @param doc       The document to act on.
    /// @param srcId     Index of the sector to copy.
    /// @param offset    XZ offset applied to every wall vertex of the copy.
    CmdDuplicateSector(EditMapDocument& doc,
                       world::SectorId  srcId,
                       glm::vec2        offset);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    {
        return "Duplicate Sector";
    }

private:
    EditMapDocument& m_doc;
    world::Sector    m_copy;           ///< Pre-built offset copy, ready to insert.
    world::SectorId  m_insertedId = 0; ///< Index assigned on first execute().
    bool             m_executed   = false;
};

} // namespace daedalus::editor
