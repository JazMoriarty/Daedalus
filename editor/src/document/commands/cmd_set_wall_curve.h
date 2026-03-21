#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Undoable update of a wall's Bezier curve settings.
class CmdSetWallCurve final : public ICommand
{
public:
    CmdSetWallCurve(EditMapDocument&                  doc,
                    world::SectorId                   sectorId,
                    std::size_t                       wallIndex,
                    std::optional<glm::vec2>          newControlA,
                    std::optional<glm::vec2>          newControlB,
                    uint32_t                          newSubdivisions);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Wall Curve"; }

private:
    EditMapDocument&          m_doc;
    world::SectorId           m_sectorId;
    std::size_t               m_wallIndex;
    std::optional<glm::vec2>  m_newControlA;
    std::optional<glm::vec2>  m_newControlB;
    uint32_t                  m_newSubdivisions = 12u;
    std::optional<glm::vec2>  m_oldControlA;
    std::optional<glm::vec2>  m_oldControlB;
    uint32_t                  m_oldSubdivisions = 12u;
};

} // namespace daedalus::editor
