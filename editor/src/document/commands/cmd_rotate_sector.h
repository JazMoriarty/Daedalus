// cmd_rotate_sector.h
// Rotates all wall vertices of a sector around its geometric centroid.
//
// The centroid is computed from the wall vertices at construction time and
// stored.  execute() applies the rotation; undo() applies the exact inverse.

#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace daedalus::editor
{

class EditMapDocument;

class CmdRotateSector final : public ICommand
{
public:
    /// @param doc      The document to act on.
    /// @param sectorId Sector to rotate.
    /// @param angleDeg Rotation angle in degrees (positive = counter-clockwise
    ///                 in map XZ space, i.e. standard 2D convention).
    CmdRotateSector(EditMapDocument& doc,
                    world::SectorId  sectorId,
                    float            angleDeg);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Rotate Sector"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    float            m_angleDeg;

    /// Centroid computed at construction; pivot for the rotation.
    glm::vec2 m_centroid{};

    /// Snapshot of wall positions before execute() so undo() restores exactly.
    std::vector<glm::vec2> m_origPositions;
};

} // namespace daedalus::editor
