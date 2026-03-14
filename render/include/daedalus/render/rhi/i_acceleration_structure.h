// i_acceleration_structure.h
// Pure interface for a GPU-built bounding volume hierarchy (BVH).
//
// Two flavours exist at the API level:
//   • Primitive (BLAS) — built from vertex/index geometry for a single mesh.
//   • Instance  (TLAS) — built from BLAS instances with per-instance transforms.
//
// The RHI consumer does not distinguish between the two once built; both are
// opaque handles that can be bound to compute shaders for intersection queries.
//
// Backend mapping:
//   Metal  → id<MTLAccelerationStructure>
//   Vulkan → VkAccelerationStructureKHR

#pragma once

namespace daedalus::rhi
{

class IAccelerationStructure
{
public:
    virtual ~IAccelerationStructure() = default;

    IAccelerationStructure(const IAccelerationStructure&)            = delete;
    IAccelerationStructure& operator=(const IAccelerationStructure&) = delete;

    /// Backend-specific acceleration structure handle.
    /// Returns id<MTLAccelerationStructure> on Metal, VkAccelerationStructureKHR on Vulkan.
    [[nodiscard]] virtual void* nativeHandle() const noexcept = 0;

protected:
    IAccelerationStructure() = default;
};

} // namespace daedalus::rhi
