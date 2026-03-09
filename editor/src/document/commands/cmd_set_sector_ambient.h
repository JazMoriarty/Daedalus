#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets the per-sector ambient colour and intensity.
class CmdSetSectorAmbient final : public ICommand
{
public:
    CmdSetSectorAmbient(EditMapDocument& doc,
                        world::SectorId  sectorId,
                        glm::vec3        newColor,
                        float            newIntensity);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Sector Ambient"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    glm::vec3        m_newColor;
    float            m_newIntensity;
    glm::vec3        m_oldColor{0.05f, 0.05f, 0.08f};
    float            m_oldIntensity = 1.0f;
};

} // namespace daedalus::editor
