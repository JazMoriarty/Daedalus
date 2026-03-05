// i_portal_traversal.h
// Pure interface for the portal visibility traversal (spec Pass 1).
//
// The traversal runs on the CPU before any GPU work each frame. Starting from
// the camera's sector it recursively crosses portals, clipping the visible
// screen-space window at each portal. Only sectors whose contributing portal
// window is non-empty are returned; only those sectors' geometry is submitted
// to the GPU.
//
// Reference: design_spec.md §Pass 1 — Portal Traversal (CPU)

#pragma once

#include "daedalus/world/world_types.h"
#include "daedalus/world/i_world_map.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace daedalus::world
{

// ─── VisibleSector ────────────────────────────────────────────────────────────
// Result entry produced by IPortalTraversal::traverse().

struct VisibleSector
{
    SectorId  sectorId;

    /// The NDC screen-space window through which this sector is visible.
    /// For the camera's own sector this is the full screen: (-1,-1) to (1,1).
    /// For subsequent sectors it is the intersection of the portal rectangle
    /// with the parent's window (further clipped at each recursion level).
    glm::vec2 windowMin;  ///< NDC top-left  (x=-1 is left, y=-1 is bottom)
    glm::vec2 windowMax;  ///< NDC bottom-right
};

// ─── IPortalTraversal ─────────────────────────────────────────────────────────

class IPortalTraversal
{
public:
    virtual ~IPortalTraversal() = default;

    IPortalTraversal(const IPortalTraversal&)            = delete;
    IPortalTraversal& operator=(const IPortalTraversal&) = delete;

    /// Run portal traversal from the camera sector and return all visible sectors.
    ///
    /// @param map           The loaded world map.
    /// @param cameraSector  The sector the camera is currently inside.
    ///                      Pass INVALID_SECTOR_ID to receive an empty result.
    /// @param viewProj      The combined view-projection matrix for this frame.
    ///                      Used to project portal corner vertices into NDC.
    /// @param maxDepth      Maximum recursive portal depth (default MAX_PORTAL_DEPTH).
    ///
    /// @return  A list of visible sectors with their contributing NDC windows.
    ///          The camera's own sector is always the first element (if valid).
    ///          No sector appears more than once.
    [[nodiscard]] virtual std::vector<VisibleSector>
    traverse(const IWorldMap& map,
             SectorId         cameraSector,
             const glm::mat4& viewProj,
             u32              maxDepth = MAX_PORTAL_DEPTH) const = 0;

protected:
    IPortalTraversal() = default;
};

// ─── Factory ──────────────────────────────────────────────────────────────────

/// Construct the platform-independent portal traversal implementation.
[[nodiscard]] std::unique_ptr<IPortalTraversal> makePortalTraversal();

} // namespace daedalus::world
