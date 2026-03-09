// cmd_move_sector.h
// Moves all wall vertices of a sector by a uniform XZ delta.
//
// Lazy-execute: the first execute() is a no-op because the live drag already
// applied the delta; subsequent calls (redo) re-apply it.

#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

class CmdMoveSector final : public ICommand
{
public:
    /// @param doc      The document to act on.
    /// @param sectorId Sector to move.
    /// @param delta    Total XZ movement already applied by the live drag.
    CmdMoveSector(EditMapDocument& doc,
                  world::SectorId  sectorId,
                  glm::vec2        delta);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Move Sector"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    glm::vec2        m_delta;
    bool             m_executed = false;
};

} // namespace daedalus::editor
