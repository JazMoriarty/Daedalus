#pragma once

#include "daedalus/editor/i_command.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Moves a light (by index) to a new world-space position.
/// Undo restores the original position.
class CmdMoveLight final : public ICommand
{
public:
    CmdMoveLight(EditMapDocument& doc,
                 std::size_t      lightIndex,
                 glm::vec3        oldPosition,
                 glm::vec3        newPosition);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Move Light"; }

private:
    EditMapDocument& m_doc;
    std::size_t      m_idx;
    glm::vec3        m_oldPos;
    glm::vec3        m_newPos;
};

} // namespace daedalus::editor
