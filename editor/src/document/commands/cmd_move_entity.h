#pragma once

#include "daedalus/editor/i_command.h"

#include <cstddef>
#include <glm/glm.hpp>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Moves an existing entity to a new world-space position.
/// Undo restores the previous position.
class CmdMoveEntity final : public ICommand
{
public:
    CmdMoveEntity(EditMapDocument& doc,
                  std::size_t      entityIndex,
                  glm::vec3        oldPosition,
                  glm::vec3        newPosition);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Move Entity"; }

private:
    EditMapDocument& m_doc;
    std::size_t      m_idx;
    glm::vec3        m_oldPos;
    glm::vec3        m_newPos;
};

} // namespace daedalus::editor
