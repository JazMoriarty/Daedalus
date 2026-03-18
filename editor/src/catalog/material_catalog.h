// material_catalog.h
// Manages a directory tree of image assets for the editor's material system.
//
// Each image file receives a companion `.meta` sidecar (e.g. stone.png.meta)
// containing a stable UUID that never changes even if the file is moved or
// renamed (to a different directory — same-directory move is still stable).
// UUIDs are generated with arc4random_buf on first encounter.
//
// Thumbnails (64×64 RGBA8Unorm) are built on first request via stb_image and
// cached in memory for the lifetime of the catalog.  Full-resolution textures
// are loaded on first request via IAssetLoader and similarly cached.
//
// The catalog root path is stored in the .emap sidecar as "asset_root" so it
// survives project reload.

#pragma once

#include "daedalus/core/types.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/i_asset_loader.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace daedalus::editor
{

// ─── MaterialEntry ────────────────────────────────────────────────────────────
// One item in the catalog — one image file on disk.

struct MaterialEntry
{
    UUID                  uuid;
    std::filesystem::path absPath;      ///< Absolute path to the primary albedo/diffuse image file.
    std::string           displayName;  ///< Filename without extension (for display).
    std::string           folderPath;   ///< Relative folder path within root ("" = root).

    /// Native image dimensions in pixels.  Populated during scan() via a
    /// lightweight stbi_info header read.  Zero if the file could not be queried.
    uint32_t texWidth  = 0;
    uint32_t texHeight = 0;

    // ─── Companion map paths ──────────────────────────────────────────────────
    // Auto-detected during scan() by suffix convention (stem_n.ext, stem_r.ext, etc.)
    // or explicitly specified in the .meta sidecar. Empty path = no companion map.
    std::filesystem::path normalPath;     ///< Tangent-space normal map (_n suffix).
    std::filesystem::path roughnessPath;  ///< Roughness map (_r suffix).
    std::filesystem::path metalnessPath;  ///< Metalness map (_m suffix).

    // ─── GPU textures (loaded on demand) ──────────────────────────────────────
    /// 64×64 thumbnail texture; null until first getOrLoadThumbnail() call.
    std::unique_ptr<rhi::ITexture> thumbnail;

    /// Full-resolution GPU texture; null until first getOrLoadTexture() call.
    std::unique_ptr<rhi::ITexture> fullTexture;      ///< Albedo/diffuse texture.
    std::unique_ptr<rhi::ITexture> normalTexture;    ///< Normal map texture.
    std::unique_ptr<rhi::ITexture> roughnessTexture; ///< Roughness map texture.
    std::unique_ptr<rhi::ITexture> metalnessTexture; ///< Metalness map texture.
};

// ─── MaterialCatalog ──────────────────────────────────────────────────────────

class MaterialCatalog
{
public:
    MaterialCatalog()  = default;
    ~MaterialCatalog() = default;

    MaterialCatalog(const MaterialCatalog&)            = delete;
    MaterialCatalog& operator=(const MaterialCatalog&) = delete;

    // ─── Root path ──────────────────────────────────────────────────────────

    void                               setRoot(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return m_root; }

    // ─── Scanning ──────────────────────────────────────────────────────────

    /// Scan (or re-scan) the root directory.  Discovers all image files,
    /// reads existing .meta sidecars, or writes new ones for UUID assignment.
    /// Clears any previously cached texture data.
    void scan();

    // ─── Lookup ────────────────────────────────────────────────────────────

    /// Find an entry by UUID.  Returns nullptr if not found.
    [[nodiscard]] const MaterialEntry* find(const UUID& uuid) const noexcept;

    /// Find an entry by absolute filesystem path.  Returns nullptr if not found.
    /// Used by entity loading to convert EntityDef::assetPath to a material UUID.
    [[nodiscard]] const MaterialEntry* findByPath(const std::filesystem::path& path) const noexcept;

    /// All entries discovered in the last scan(), sorted by folder then name.
    [[nodiscard]] const std::vector<MaterialEntry>& entries() const noexcept { return m_entries; }

    /// True if the catalog has no entries (not scanned or empty root).
    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }

    /// Request a rescan on the next frame. Set this after modifying assets on disk.
    void requestRescan() noexcept { m_needsRescan = true; }

    /// Check if a rescan was requested and consume the flag.
    [[nodiscard]] bool consumeRescanRequest() noexcept {
        if (m_needsRescan) {
            m_needsRescan = false;
            return true;
        }
        return false;
    }

    // ─── Lazy texture loading ──────────────────────────────────────────────

    /// Return a 64×64 thumbnail for uuid, loading it on first call.
    /// Returns nullptr if uuid is not in the catalog or loading fails.
    [[nodiscard]] rhi::ITexture* getOrLoadThumbnail(const UUID&           uuid,
                                                     rhi::IRenderDevice&   device);

    /// Return the full-resolution GPU texture for uuid, loading it on first call.
    /// Returns nullptr if uuid is not in the catalog or loading fails.
    [[nodiscard]] rhi::ITexture* getOrLoadTexture(const UUID&           uuid,
                                                   rhi::IRenderDevice&   device,
                                                   render::IAssetLoader& loader);

    /// Return the normal map texture for uuid, loading it on first call.
    /// Returns nullptr if uuid is not in the catalog, no normal map exists, or loading fails.
    /// Normal maps are loaded with sRGB=false to preserve tangent-space data.
    [[nodiscard]] rhi::ITexture* getOrLoadNormalMap(const UUID&           uuid,
                                                     rhi::IRenderDevice&   device,
                                                     render::IAssetLoader& loader);

    /// Return the roughness map texture for uuid, loading it on first call.
    /// Returns nullptr if uuid is not in the catalog, no roughness map exists, or loading fails.
    [[nodiscard]] rhi::ITexture* getOrLoadRoughnessMap(const UUID&           uuid,
                                                        rhi::IRenderDevice&   device,
                                                        render::IAssetLoader& loader);

    /// Return the metalness map texture for uuid, loading it on first call.
    /// Returns nullptr if uuid is not in the catalog, no metalness map exists, or loading fails.
    [[nodiscard]] rhi::ITexture* getOrLoadMetalnessMap(const UUID&           uuid,
                                                        rhi::IRenderDevice&   device,
                                                        render::IAssetLoader& loader);

private:
    std::filesystem::path m_root;
    std::vector<MaterialEntry> m_entries;
    std::unordered_map<UUID, std::size_t, UUIDHash> m_uuidIndex;
    bool m_needsRescan = false;

    [[nodiscard]] MaterialEntry* findMutable(const UUID& uuid) noexcept;

    static bool isImageFile(const std::filesystem::path& p) noexcept;
    static UUID readOrCreateMeta(const std::filesystem::path& imgPath,
                                  MaterialEntry&                entry);

    [[nodiscard]] std::unique_ptr<rhi::ITexture>
    buildThumbnail(const std::filesystem::path& path, rhi::IRenderDevice& device);
};

} // namespace daedalus::editor
