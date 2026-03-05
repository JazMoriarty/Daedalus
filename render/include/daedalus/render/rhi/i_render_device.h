#pragma once

#include "daedalus/render/rhi/i_command_queue.h"
#include "daedalus/render/rhi/i_swapchain.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/rhi/i_sampler.h"
#include "daedalus/render/rhi/i_shader.h"
#include "daedalus/render/rhi/i_pipeline.h"
#include "daedalus/render/rhi/i_bind_group.h"
#include "daedalus/render/rhi/i_fence.h"

#include "daedalus/core/types.h"

#include <memory>
#include <span>
#include <string_view>

namespace daedalus::rhi
{

// ─── IRenderDevice ────────────────────────────────────────────────────────────
// The root GPU device abstraction.  One instance per physical GPU in use.
// All other RHI objects are created through this interface.
//
// Backend implementations:
//   • macOS  → MetalRenderDevice (Metal framework)
//   • Windows → VulkanRenderDevice (Vulkan + VMA)

class IRenderDevice
{
public:
    virtual ~IRenderDevice() = default;

    IRenderDevice(const IRenderDevice&)            = delete;
    IRenderDevice& operator=(const IRenderDevice&) = delete;

    // ─── Queues ───────────────────────────────────────────────────────────────

    [[nodiscard]] virtual std::unique_ptr<ICommandQueue>
    createCommandQueue(std::string_view debugName = {}) = 0;

    // ─── Swapchain ────────────────────────────────────────────────────────────

    /// Create a swapchain connected to the native window handle.
    /// `nativeWindowHandle` is an SDL_MetalView* (macOS) or HWND (Windows).
    [[nodiscard]] virtual std::unique_ptr<ISwapchain>
    createSwapchain(void* nativeWindowHandle,
                    u32   width,
                    u32   height) = 0;

    // ─── Resources ────────────────────────────────────────────────────────────

    [[nodiscard]] virtual std::unique_ptr<IBuffer>
    createBuffer(const BufferDescriptor& desc) = 0;

    [[nodiscard]] virtual std::unique_ptr<ITexture>
    createTexture(const TextureDescriptor& desc) = 0;

    [[nodiscard]] virtual std::unique_ptr<ISampler>
    createSampler(const SamplerDescriptor& desc) = 0;

    // ─── Shaders ──────────────────────────────────────────────────────────────

    /// Compile or load a shader from source bytes.
    /// `source` is MSL source text for Metal or SPIRV bytes for Vulkan.
    [[nodiscard]] virtual std::unique_ptr<IShader>
    createShader(std::span<const byte> source,
                 ShaderStage           stage,
                 std::string_view      entryPoint) = 0;

    /// Load a pre-compiled shader function from a binary library on disk.
    /// `libraryPath` is an absolute path to a .metallib (Metal) or SPIR-V module (Vulkan).
    /// Libraries are cached by path — repeated calls with the same path reuse the loaded library.
    [[nodiscard]] virtual std::unique_ptr<IShader>
    createShaderFromLibrary(std::string_view libraryPath,
                            ShaderStage      stage,
                            std::string_view entryPoint) = 0;

    // ─── Pipelines ────────────────────────────────────────────────────────────

    [[nodiscard]] virtual std::unique_ptr<IPipeline>
    createRenderPipeline(const RenderPipelineDescriptor& desc) = 0;

    [[nodiscard]] virtual std::unique_ptr<IPipeline>
    createComputePipeline(const ComputePipelineDescriptor& desc) = 0;

    // ─── Synchronisation ──────────────────────────────────────────────────────

    [[nodiscard]] virtual std::unique_ptr<IFence>
    createFence() = 0;

    // ─── Diagnostics ──────────────────────────────────────────────────────────

    [[nodiscard]] virtual std::string_view deviceName() const noexcept = 0;

protected:
    IRenderDevice() = default;
};

} // namespace daedalus::rhi
