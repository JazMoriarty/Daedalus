// normal_map_generator_panel.cpp
// Implementation of the normal map generator panel.

#include "normal_map_generator_panel.h"
#include "catalog/material_catalog.h"
#include "daedalus/editor/edit_map_document.h"

#include "daedalus/render/rhi/rhi_types.h"

#include "imgui.h"

#include "stb_image.h"

// Define STB image write implementation once in this TU
#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include "stb_image_write.h"
#pragma clang diagnostic pop

#include <algorithm>
#include <cstring>

namespace daedalus::editor
{

void NormalMapGeneratorPanel::draw(bool*                  pOpen,
                                    MaterialCatalog&       catalog,
                                    rhi::IRenderDevice&    device,
                                    render::IAssetLoader&  loader,
                                    EditMapDocument&       doc)
{
    if (!pOpen || !*pOpen)
        return;

    ImGui::SetNextWindowSize(ImVec2(600, 700), ImGuiCond_FirstUseEver);
    
    if (!ImGui::Begin("Normal Map Generator", pOpen))
    {
        ImGui::End();
        return;
    }

    // If no source texture is selected, show a message
    if (m_sourceTextureUUID == UUID{})
    {
        ImGui::TextWrapped("No texture selected. Use Window → Normal Map Generator to open this panel, then right-click a texture in the Asset Browser to select it.");
        ImGui::End();
        return;
    }

    // Load source texture on first draw
    if (m_sourceAlbedoData.empty())
    {
        loadSourceTexture(catalog);
        if (m_sourceAlbedoData.empty())
        {
            ImGui::TextWrapped("Failed to load source texture.");
            ImGui::End();
            return;
        }
    }

    // Display source texture info
    ImGui::SeparatorText("Source Texture");
    ImGui::Text("File: %s", m_sourceTexturePath.filename().string().c_str());
    ImGui::Text("Size: %u × %u px", m_sourceWidth, m_sourceHeight);
    ImGui::Spacing();

    // Parameter controls
    ImGui::SeparatorText("Generation Parameters");

    bool paramsChanged = false;

    // Height Scale slider
    if (ImGui::SliderFloat("Height Scale", &m_params.heightScale, 0.0f, 2.0f, "%.2f"))
    {
        paramsChanged = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Overall bump intensity. Higher = more pronounced relief.");

    // Detail Level slider
    int detailLevel = static_cast<int>(m_params.detailLevel);
    if (ImGui::SliderInt("Detail Level", &detailLevel, 0, 10))
    {
        m_params.detailLevel = static_cast<u32>(detailLevel);
        paramsChanged = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Frequency filtering: 0 = full detail, 10 = broad features only");

    // Blur Radius slider
    int blurRadius = static_cast<int>(m_params.blurRadius);
    if (ImGui::SliderInt("Blur Radius", &blurRadius, 0, 8))
    {
        m_params.blurRadius = static_cast<u32>(blurRadius);
        paramsChanged = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Smoothing radius: 0 = no blur, 8 = heavy smoothing");

    // Edge Bias slider
    if (ImGui::SliderFloat("Edge Bias", &m_params.edgeBias, -1.0f, 1.0f, "%.2f"))
    {
        paramsChanged = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Negative = emphasize concave (grout, cracks), Positive = emphasize convex (raised bricks)");

    // Invert checkbox
    if (ImGui::Checkbox("Invert Height", &m_params.invert))
    {
        paramsChanged = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Flip luminance interpretation: bright = low, dark = high");

    if (paramsChanged)
        m_needsRegenerate = true;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Preview section
    ImGui::SeparatorText("Preview");

    // Regenerate if dirty
    if (m_needsRegenerate)
    {
        regeneratePreview(device);
        m_needsRegenerate = false;
    }

    // Display previews side by side if we have both
    rhi::ITexture* sourcePreview = catalog.getOrLoadThumbnail(m_sourceTextureUUID, device);
    
    if (sourcePreview && m_normalPreview)
    {
        const float previewSize = 256.0f;
        
        // Get Metal texture handles for ImGui
        void* sourceHandle = sourcePreview->nativeHandle();
        void* normalHandle = m_normalPreview->nativeHandle();

        ImGui::BeginGroup();
        ImGui::Text("Albedo");
        ImGui::Image(sourceHandle, ImVec2(previewSize, previewSize));
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::Text("Generated Normal");
        ImGui::Image(normalHandle, ImVec2(previewSize, previewSize));
        ImGui::EndGroup();
    }
    else if (!m_normalPreview)
    {
        ImGui::TextDisabled("Generating preview...");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Action buttons
    if (ImGui::Button("Generate & Save", ImVec2(150, 0)))
    {
        saveAndClose(catalog, doc);
        *pOpen = false;
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(150, 0)))
    {
        *pOpen = false;
    }

    ImGui::End();
}

void NormalMapGeneratorPanel::openWithTexture(const UUID& textureUUID)
{
    m_sourceTextureUUID = textureUUID;
    m_needsRegenerate   = true;
    
    // Reset params to defaults
    m_params = render::NormalMapParams{};
    
    // Clear old texture data to force reload
    m_sourceAlbedoData.clear();
    m_sourceWidth = 0;
    m_sourceHeight = 0;
    m_normalPreview.reset();
}

void NormalMapGeneratorPanel::loadSourceTexture(MaterialCatalog& catalog)
{
    const MaterialEntry* entry = catalog.find(m_sourceTextureUUID);
    if (!entry)
        return;

    m_sourceTexturePath = entry->absPath;

    // Load image data via stb_image
    int w, h, channels;
    u8* pixels = stbi_load(m_sourceTexturePath.string().c_str(), &w, &h, &channels, 4);
    
    if (!pixels)
        return;

    m_sourceWidth  = static_cast<u32>(w);
    m_sourceHeight = static_cast<u32>(h);

    // Copy into our persistent buffer
    const std::size_t dataSize = static_cast<std::size_t>(w * h * 4);
    m_sourceAlbedoData.resize(dataSize);
    std::memcpy(m_sourceAlbedoData.data(), pixels, dataSize);

    stbi_image_free(pixels);
}

void NormalMapGeneratorPanel::regeneratePreview(rhi::IRenderDevice& device)
{
    // Note: source texture must be loaded before calling this.
    // This is called from draw() where catalog is available.

    // Generate normal map
    render::NormalMapResult result = render::generateNormalMap(
        m_sourceAlbedoData.data(),
        m_sourceWidth,
        m_sourceHeight,
        m_params);

    if (result.data.empty())
        return;

    // Upload to GPU texture for preview
    rhi::TextureDescriptor desc;
    desc.width      = result.width;
    desc.height     = result.height;
    desc.format     = rhi::TextureFormat::RGBA8Unorm;
    desc.usage      = rhi::TextureUsage::ShaderRead;
    desc.mipLevels  = 1;

    desc.initData = result.data.data();
    m_normalPreview = device.createTexture(desc);
}

void NormalMapGeneratorPanel::saveAndClose(MaterialCatalog& catalog, EditMapDocument& doc)
{
    // Generate final normal map
    render::NormalMapResult result = render::generateNormalMap(
        m_sourceAlbedoData.data(),
        m_sourceWidth,
        m_sourceHeight,
        m_params);

    if (result.data.empty())
        return;

    // Determine output path: same directory as source, with _n suffix
    std::filesystem::path outputPath = m_sourceTexturePath;
    outputPath.replace_filename(m_sourceTexturePath.stem().string() + "_n.png");

    // Save PNG using stb_image_write
    const int writeResult = stbi_write_png(
        outputPath.string().c_str(),
        static_cast<int>(result.width),
        static_cast<int>(result.height),
        4,  // RGBA8
        result.data.data(),
        static_cast<int>(result.width * 4));  // stride

    if (writeResult == 0)
    {
        doc.log("Failed to write normal map PNG: " + outputPath.string());
        return;
    }

    doc.log("Saved normal map to " + outputPath.string());
    
    // Request a catalog rescan to pick up the newly generated normal map.
    // This happens on the next frame so it doesn't interfere with panel close.
    catalog.requestRescan();
}

} // namespace daedalus::editor
