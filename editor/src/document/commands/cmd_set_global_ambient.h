#pragma once

#include "daedalus/editor/i_command.h"

#include <glm/glm.hpp>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets the map-wide global ambient light colour and intensity.  Undoable.
class CmdSetGlobalAmbient final : public ICommand
{
public:
    CmdSetGlobalAmbient(EditMapDocument& doc,
                        glm::vec3        oldColor,
                        glm::vec3        newColor,
                        float            oldIntensity,
                        float            newIntensity);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Global Ambient"; }

private:
    EditMapDocument& m_doc;
    glm::vec3        m_oldColor;
    glm::vec3        m_newColor;
    float            m_oldIntensity;
    float            m_newIntensity;
};

} // namespace daedalus::editor
