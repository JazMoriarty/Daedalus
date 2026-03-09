#pragma once

#include "daedalus/editor/i_command.h"

#include <cstddef>
#include <glm/glm.hpp>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Changes the non-uniform scale of an existing entity.
/// Undo restores the previous scale.
class CmdScaleEntity final : public ICommand
{
public:
    CmdScaleEntity(EditMapDocument& doc,
                   std::size_t      entityIndex,
                   glm::vec3        oldScale,
                   glm::vec3        newScale);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Scale Entity"; }

private:
    EditMapDocument& m_doc;
    std::size_t      m_idx;
    glm::vec3        m_oldScale;
    glm::vec3        m_newScale;
};

} // namespace daedalus::editor
