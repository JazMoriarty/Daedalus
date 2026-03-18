// normal_map_generator_panel.h
// Dockable panel for interactive normal map generation from albedo textures.

#pragma once

#include "daedalus/core/types.h"
#include "daedalus/render/normal_map_generator.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/i_asset_loader.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace daedalus::editor
{

class MaterialCatalog;
class EditMapDocument;

/// Dockable panel that generates tangent-space normal maps from albedo textures
/// using a Sobel-based height-from-luminance algorithm. Provides live preview
/// and simple sliders for artist control.
class NormalMapGeneratorPanel
{
public:
    NormalMapGeneratorPanel()  = default;
    ~NormalMapGeneratorPanel() = default;

    NormalMapGeneratorPanel(const NormalMapGeneratorPanel&)            = delete;
    NormalMapGeneratorPanel& operator=(const NormalMapGeneratorPanel&) = delete;

    /// Draw the panel. Pass visibility bool pointer to allow Window menu control.
    /// The panel must be drawn every frame for proper docking behavior.
    void draw(bool*                  pOpen,
              MaterialCatalog&       catalog,
              rhi::IRenderDevice&    device,
              render::IAssetLoader&  loader,
              EditMapDocument&       doc);

    /// Open the panel and select a source texture by UUID.
    /// The panel becomes visible and dockable immediately.
    void openWithTexture(const UUID& textureUUID);

private:
    UUID                           m_sourceTextureUUID;
    std::filesystem::path          m_sourceTexturePath;
    render::NormalMapParams        m_params;
    bool                           m_needsRegenerate = true;

    // Preview textures (GPU-resident for ImGui display)
    std::unique_ptr<rhi::ITexture> m_normalPreview;

    // Source albedo loaded into CPU memory for generation
    std::vector<u8>                m_sourceAlbedoData;
    u32                            m_sourceWidth  = 0;
    u32                            m_sourceHeight = 0;

    // Load source albedo into CPU memory
    void loadSourceTexture(MaterialCatalog& catalog);

    // Regenerate normal map preview from current params
    void regeneratePreview(rhi::IRenderDevice& device);

    // Save generated normal map to disk and register in catalog
    void saveAndClose(MaterialCatalog& catalog, EditMapDocument& doc);
};

} // namespace daedalus::editor
