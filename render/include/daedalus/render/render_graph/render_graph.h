// render_graph.h
// Frame-level render graph: declare passes, compile resource allocation, execute.
//
// Usage per frame:
//   1. graph.reset()
//   2. auto id = graph.createTexture(desc) / graph.importTexture(tex)
//   3. graph.addRenderPass(...) / graph.addComputePass(...)
//   4. graph.compile(device, swapWidth, swapHeight)
//   5. graph.execute(commandBuffer)

#pragma once

#include "daedalus/render/rhi/rhi_types.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/rhi/i_render_pass_encoder.h"
#include "daedalus/render/rhi/i_compute_pass_encoder.h"
#include "daedalus/core/types.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace daedalus::rhi { class IRenderDevice; class ICommandBuffer; }

namespace daedalus::render
{

using namespace daedalus::rhi;

// ─── RGTextureId ─────────────────────────────────────────────────────────────
// Opaque handle for a texture declared inside the render graph.
// 0 == invalid.

using RGTextureId = u32;
static constexpr RGTextureId RGTextureNull = 0;

// ─── RGTextureDesc ────────────────────────────────────────────────────────────
// Descriptor for a transient (graph-owned) texture.
// Width and height may be specified relative to the swap-chain dimensions.

struct RGTextureDesc
{
    /// If > 0, absolute pixel dimensions; if == 0, uses swapWidth/swapHeight.
    u32           width        = 0;
    u32           height       = 0;
    TextureFormat format       = TextureFormat::RGBA16Float;
    TextureUsage  usage        = TextureUsage::RenderTarget | TextureUsage::ShaderRead;
    std::string   debugName;
};

// ─── RGRenderPassDesc ─────────────────────────────────────────────────────────

static constexpr u32 RG_MAX_COLOR_OUTPUTS = 8;

struct RGRenderPassDesc
{
    std::string name;

    // Color outputs
    std::array<RGTextureId, RG_MAX_COLOR_OUTPUTS> colorOutputs = {};
    u32                                           colorOutputCount = 0;

    // Depth output (optional — RGTextureNull means no depth)
    RGTextureId depthOutput   = RGTextureNull;

    // Clear values applied when the pass begins
    ClearColor  clearColor;
    f32         clearDepth    = 1.0f;

    // Load control — independently for colour and depth attachments.
    // If false (default), the attachment is cleared at pass start.
    // If true, existing contents are loaded.
    bool        loadColors    = false;
    bool        loadDepth     = false;

    // Execute callback — receives the open render pass encoder.
    std::function<void(IRenderPassEncoder*)> execute;
};

// ─── RGComputePassDesc ────────────────────────────────────────────────────────

struct RGComputePassDesc
{
    std::string name;
    std::function<void(IComputePassEncoder*)> execute;
};

// ─── RenderGraph ──────────────────────────────────────────────────────────────

class RenderGraph
{
public:
    RenderGraph()  = default;
    ~RenderGraph() = default;

    RenderGraph(const RenderGraph&)            = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // ─── Declaration ─────────────────────────────────────────────────────────

    /// Declare a transient texture owned by the graph.
    [[nodiscard]] RGTextureId createTexture(const RGTextureDesc& desc);

    /// Import an externally-owned texture (e.g. the swapchain drawable).
    /// The returned handle is valid until the next reset().
    [[nodiscard]] RGTextureId importTexture(std::string_view name, ITexture* texture);

    /// Declare a render pass.  Outputs are realised during compile().
    void addRenderPass(RGRenderPassDesc desc);

    /// Declare a compute pass.
    void addComputePass(RGComputePassDesc desc);

    // ─── Compilation ─────────────────────────────────────────────────────────

    /// Allocate / reuse all declared transient textures.
    /// Must be called after all passes are declared and before execute().
    void compile(IRenderDevice& device, u32 swapWidth, u32 swapHeight);

    // ─── Execution ───────────────────────────────────────────────────────────

    /// Record all declared passes into `cmdbuf`, in declaration order.
    void execute(ICommandBuffer& cmdbuf);

    // ─── Accessors ───────────────────────────────────────────────────────────

    /// Resolve a texture handle to the underlying ITexture*.
    /// Only valid after compile() and before the next reset().
    [[nodiscard]] ITexture* get(RGTextureId id) const;

    // ─── Reset ───────────────────────────────────────────────────────────────

    /// Clear all declared passes and imported textures; retain cached allocations.
    void reset();

private:
    // ─── Internal texture entry ───────────────────────────────────────────────

    struct TextureEntry
    {
        RGTextureDesc             desc;
        ITexture*                 imported  = nullptr; // non-null ↔ imported
        std::unique_ptr<ITexture> owned;               // non-null ↔ transient
    };

    // ─── Internal pass variant ────────────────────────────────────────────────

    enum class PassType { Render, Compute };

    struct PassEntry
    {
        PassType type;
        RGRenderPassDesc  renderDesc;  // valid when type == Render
        RGComputePassDesc computeDesc; // valid when type == Compute
    };

    // ─── State ────────────────────────────────────────────────────────────────

    // Index 0 is reserved (RGTextureNull); valid handles start at 1.
    std::vector<TextureEntry> m_textures;
    std::vector<PassEntry>    m_passes;

    u32 m_swapWidth  = 0;
    u32 m_swapHeight = 0;

    // ─── Helpers ──────────────────────────────────────────────────────────────

    [[nodiscard]] TextureEntry&       entry(RGTextureId id);
    [[nodiscard]] const TextureEntry& entry(RGTextureId id) const;
};

} // namespace daedalus::render
