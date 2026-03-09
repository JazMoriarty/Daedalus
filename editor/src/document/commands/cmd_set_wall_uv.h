#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets the UV mapping parameters (offset, scale, rotation) of a single wall.
class CmdSetWallUV final : public ICommand
{
public:
    CmdSetWallUV(EditMapDocument& doc,
                 world::SectorId  sectorId,
                 std::size_t      wallIndex,
                 glm::vec2        newOffset,
                 glm::vec2        newScale,
                 float            newRotation);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Wall UV"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    std::size_t      m_wallIndex;
    glm::vec2        m_newOffset;
    glm::vec2        m_newScale;
    float            m_newRotation;
    glm::vec2        m_oldOffset{0.0f, 0.0f};
    glm::vec2        m_oldScale{1.0f, 1.0f};
    float            m_oldRotation = 0.0f;
};

} // namespace daedalus::editor
