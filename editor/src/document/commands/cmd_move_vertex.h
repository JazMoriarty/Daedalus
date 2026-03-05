#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Moves the start vertex (p0) of a single wall.  Undo restores the original
/// position.  Used by SelectTool to commit a live vertex drag.
class CmdMoveVertex final : public ICommand
{
public:
    CmdMoveVertex(EditMapDocument& doc,
                  world::SectorId  sectorId,
                  std::size_t      wallIndex,
                  glm::vec2        oldPos,
                  glm::vec2        newPos);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Move Vertex"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    std::size_t      m_wallIndex;
    glm::vec2        m_oldPos;
    glm::vec2        m_newPos;
};

} // namespace daedalus::editor
