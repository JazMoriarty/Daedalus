// render_graph.cpp

#include "daedalus/render/render_graph/render_graph.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_command_buffer.h"
#include "daedalus/core/assert.h"

namespace daedalus::render
{

// ─── Helpers ─────────────────────────────────────────────────────────────────

RenderGraph::TextureEntry& RenderGraph::entry(RGTextureId id)
{
    DAEDALUS_ASSERT(id != RGTextureNull && id < static_cast<u32>(m_textures.size()),
                    "RenderGraph::entry: invalid RGTextureId");
    return m_textures[id];
}

const RenderGraph::TextureEntry& RenderGraph::entry(RGTextureId id) const
{
    DAEDALUS_ASSERT(id != RGTextureNull && id < static_cast<u32>(m_textures.size()),
                    "RenderGraph::entry: invalid RGTextureId");
    return m_textures[id];
}

// ─── Declaration ─────────────────────────────────────────────────────────────

RGTextureId RenderGraph::createTexture(const RGTextureDesc& desc)
{
    if (m_textures.empty())
    {
        // Slot 0 is reserved for the null handle.
        m_textures.push_back({});
    }
    const auto id = static_cast<RGTextureId>(m_textures.size());
    TextureEntry e;
    e.desc = desc;
    m_textures.push_back(std::move(e));
    return id;
}

RGTextureId RenderGraph::importTexture(std::string_view name, ITexture* texture)
{
    DAEDALUS_ASSERT(texture != nullptr, "RenderGraph::importTexture: null texture");
    if (m_textures.empty())
        m_textures.push_back({});

    const auto id = static_cast<RGTextureId>(m_textures.size());
    TextureEntry e;
    e.desc.debugName = std::string(name);
    e.imported       = texture;
    m_textures.push_back(std::move(e));
    return id;
}

void RenderGraph::addRenderPass(RGRenderPassDesc desc)
{
    PassEntry pe;
    pe.type       = PassType::Render;
    pe.renderDesc = std::move(desc);
    m_passes.push_back(std::move(pe));
}

void RenderGraph::addComputePass(RGComputePassDesc desc)
{
    PassEntry pe;
    pe.type        = PassType::Compute;
    pe.computeDesc = std::move(desc);
    m_passes.push_back(std::move(pe));
}

// ─── Compilation ─────────────────────────────────────────────────────────────

void RenderGraph::compile(IRenderDevice& device, u32 swapWidth, u32 swapHeight)
{
    m_swapWidth  = swapWidth;
    m_swapHeight = swapHeight;

    for (auto& e : m_textures)
    {
        // Skip null slot and imported textures.
        if (e.imported != nullptr)
            continue;
        if (e.desc.format == TextureFormat::Invalid)
            continue;

        const u32 w = (e.desc.width  > 0) ? e.desc.width  : swapWidth;
        const u32 h = (e.desc.height > 0) ? e.desc.height : swapHeight;

        // Reuse existing allocation if compatible.
        if (e.owned)
        {
            if (e.owned->width()  == w &&
                e.owned->height() == h &&
                e.owned->format() == e.desc.format)
            {
                continue; // reuse
            }
            e.owned.reset();
        }

        TextureDescriptor td;
        td.width     = w;
        td.height    = h;
        td.format    = e.desc.format;
        td.usage     = e.desc.usage;
        td.debugName = e.desc.debugName;

        e.owned = device.createTexture(td);
    }
}

// ─── Execution ───────────────────────────────────────────────────────────────

void RenderGraph::execute(ICommandBuffer& cmdbuf)
{
    for (const auto& pe : m_passes)
    {
        if (pe.type == PassType::Render)
        {
            const auto& rd = pe.renderDesc;
            DAEDALUS_ASSERT(rd.execute, "RenderGraph::execute: render pass has no execute callback");

            RenderPassDescriptor rpd;
            rpd.debugLabel = rd.name;

            // Color attachments
            rpd.colorAttachmentCount = rd.colorOutputCount;
            for (u32 i = 0; i < rd.colorOutputCount; ++i)
            {
                ITexture* tex = get(rd.colorOutputs[i]);
                DAEDALUS_ASSERT(tex, "RenderGraph: null color attachment");
                rpd.colorAttachments[i].texture     = tex;
                rpd.colorAttachments[i].loadAction  = rd.loadColors ? LoadAction::Load : LoadAction::Clear;
                rpd.colorAttachments[i].storeAction = StoreAction::Store;
                rpd.colorAttachments[i].clearColor  = rd.clearColor;
            }

            // Depth attachment
            if (rd.depthOutput != RGTextureNull)
            {
                ITexture* dtex = get(rd.depthOutput);
                DAEDALUS_ASSERT(dtex, "RenderGraph: null depth attachment");
                rpd.hasDepthAttachment              = true;
                rpd.depthAttachment.texture         = dtex;
                rpd.depthAttachment.loadAction      = rd.loadDepth ? LoadAction::Load : LoadAction::Clear;
                rpd.depthAttachment.storeAction     = StoreAction::Store;
                rpd.depthAttachment.clearDepth      = rd.clearDepth;
            }

            IRenderPassEncoder* enc = cmdbuf.beginRenderPass(rpd);
            rd.execute(enc);
            enc->end();
        }
        else // Compute
        {
            const auto& cd = pe.computeDesc;
            DAEDALUS_ASSERT(cd.execute, "RenderGraph::execute: compute pass has no execute callback");

            IComputePassEncoder* enc = cmdbuf.beginComputePass(cd.name);
            cd.execute(enc);
            enc->end();
        }
    }
}

// ─── Accessors ───────────────────────────────────────────────────────────────

ITexture* RenderGraph::get(RGTextureId id) const
{
    if (id == RGTextureNull) return nullptr;
    const auto& e = entry(id);
    if (e.imported) return e.imported;
    return e.owned.get();
}

// ─── Reset ───────────────────────────────────────────────────────────────────

void RenderGraph::reset()
{
    // Clear passes and imported textures; keep owned (transient) allocations
    // so they can be reused next frame.
    m_passes.clear();

    for (auto& e : m_textures)
    {
        e.imported = nullptr;
        // e.owned intentionally retained for reuse
    }

    // Clear all entries but keep the reserved null slot capacity.
    // Null slot is re-added lazily in createTexture/importTexture.
    m_textures.clear();
}

} // namespace daedalus::render
