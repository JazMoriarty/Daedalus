// cmd_slide_wall.h
// Slides a wall segment by moving both its endpoint vertices simultaneously.
// On a convex polygon wall[i] runs from wall[i].p0 to wall[(i+1)%n].p0, so
// sliding wall i means updating BOTH of those p0 values.
//
// Lazy-execute: the first execute() is a no-op because the live drag already
// placed the vertices at their final positions.

#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

class CmdSlideWall final : public ICommand
{
public:
    /// @param doc      The document to act on.
    /// @param sectorId Owning sector.
    /// @param wallIdx  Index of the wall being slid.
    /// @param origPos1 Original p0 of wall[wallIdx]         (before live drag).
    /// @param newPos1  Final   p0 of wall[wallIdx]         (after  live drag).
    /// @param origPos2 Original p0 of wall[(wallIdx+1)%n]  (before live drag).
    /// @param newPos2  Final   p0 of wall[(wallIdx+1)%n]   (after  live drag).
    CmdSlideWall(EditMapDocument& doc,
                 world::SectorId  sectorId,
                 std::size_t      wallIdx,
                 glm::vec2        origPos1,
                 glm::vec2        newPos1,
                 glm::vec2        origPos2,
                 glm::vec2        newPos2);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Slide Wall"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    std::size_t      m_wallIdx;
    glm::vec2        m_origPos1;
    glm::vec2        m_newPos1;
    glm::vec2        m_origPos2;
    glm::vec2        m_newPos2;
    bool             m_executed = false;
};

} // namespace daedalus::editor
