#pragma once

#include "cmd_set_sector_material.h"  // SectorSurface enum
#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets the UV mapping parameters (offset, scale, rotation) of a sector's
/// floor or ceiling surface.  Undoable; live preview is driven by direct
/// mutation of the sector before the command is pushed.
class CmdSetSectorSurfaceUV final : public ICommand
{
public:
    CmdSetSectorSurfaceUV(EditMapDocument& doc,
                          world::SectorId  sectorId,
                          SectorSurface    surface,
                          glm::vec2        newOffset,
                          glm::vec2        newScale,
                          float            newRotation);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    {
        return m_surface == SectorSurface::Floor ? "Set Floor UV" : "Set Ceiling UV";
    }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    SectorSurface    m_surface;
    glm::vec2        m_newOffset;
    glm::vec2        m_newScale;
    float            m_newRotation;
    glm::vec2        m_oldOffset{0.0f, 0.0f};
    glm::vec2        m_oldScale{1.0f, 1.0f};
    float            m_oldRotation = 0.0f;
};

} // namespace daedalus::editor
