#pragma once

#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/i_asset_loader.h"

namespace daedalus::editor
{

class EditMapDocument;
class MaterialCatalog;
class ModelCatalog;
class AssetBrowserPanel;

class PropertyInspector
{
public:
    PropertyInspector()  = default;
    ~PropertyInspector() = default;

    void draw(EditMapDocument&      doc,
              MaterialCatalog&      catalog,
              rhi::IRenderDevice&   device,
              render::IAssetLoader& loader,
              AssetBrowserPanel&    assetBrowser,
              ModelCatalog*         voxCatalog = nullptr,
              float                 gridStep   = 1.0f);
};

} // namespace daedalus::editor
