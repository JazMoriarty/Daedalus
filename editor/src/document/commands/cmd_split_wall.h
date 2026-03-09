// cmd_split_wall.h
// Splits a wall by inserting a new vertex, defaulting to the midpoint.
//
// The original wall retains its p0; a new wall is inserted immediately after
// it with p0 at the split position and the same material/UV/flags as the
// original.  Any portal link on the original wall is cleared (a split
// invalidates the geometry match with the partner wall).
//
// Typical use: press W with the 2D cursor positioned at the desired split
// location (projected onto the wall).  Hold Shift+W to force midpoint.

#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

class CmdSplitWall final : public ICommand
{
public:
    /// @param sectorId   Sector owning the wall.
    /// @param wallIndex  Index of the wall to split within the sector.
    /// @param splitPoint If provided, use this map-space position as the new
    ///                   vertex; otherwise the midpoint of the wall is used.
    CmdSplitWall(EditMapDocument& doc,
                 world::SectorId  sectorId,
                 std::size_t      wallIndex,
                 glm::vec2        splitPoint = {-1e30f, -1e30f});

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Split Wall"; }

    /// Sentinel value indicating "use midpoint". A non-sentinel value means
    /// the caller provided an explicit split position.
    static constexpr float k_useMidpoint = -1e30f;

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    std::size_t      m_wallIndex;   ///< Index of the wall that is being split.
    glm::vec2        m_splitPoint{}; ///< Resolved split position (midpoint or explicit).

    /// Copy of the original wall used to (a) build the new wall and
    /// (b) restore the original wall's state on undo.
    world::Wall      m_origWall{};

    bool m_executed = false;
};

} // namespace daedalus::editor
