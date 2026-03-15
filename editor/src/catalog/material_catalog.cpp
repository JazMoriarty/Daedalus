// material_catalog.cpp

#include "material_catalog.h"

// stb_image (declaration only — implementation compiled once in asset_loader.cpp)
#include "stb_image.h"

#include "daedalus/render/rhi/rhi_types.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#include <nlohmann/json.hpp>
#pragma clang diagnostic pop

#include <algorithm>
#include <cstdlib>   // arc4random_buf (macOS / BSD)
#include <cstring>
#include <fstream>
#include <string>

namespace daedalus::editor
{

namespace
{

// Supported image file extensions (lower-case).
constexpr std::array<std::string_view, 5> k_imgExts = {
    ".png", ".jpg", ".jpeg", ".tga", ".bmp"
};

[[nodiscard]] UUID generateUUID() noexcept
{
    UUID uuid;
    arc4random_buf(&uuid, sizeof(uuid));
    return uuid;
}

} // anonymous namespace

// ─── setRoot ──────────────────────────────────────────────────────────────────

void MaterialCatalog::setRoot(std::filesystem::path root)
{
    m_root = std::move(root);
}

// ─── isImageFile ──────────────────────────────────────────────────────────────

bool MaterialCatalog::isImageFile(const std::filesystem::path& p) noexcept
{
    std::string ext = p.extension().string();
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& e : k_imgExts)
        if (ext == e) return true;
    return false;
}

// ─── readOrCreateMeta ────────────────────────────────────────────────────────

UUID MaterialCatalog::readOrCreateMeta(const std::filesystem::path& imgPath)
{
    const std::filesystem::path metaPath = imgPath.string() + ".meta";

    // Try to read an existing .meta file.
    if (std::filesystem::exists(metaPath))
    {
        std::ifstream ifs(metaPath);
        if (ifs.is_open())
        {
            try
            {
                nlohmann::json j;
                ifs >> j;
                UUID uuid;
                uuid.hi = j["uuid_hi"].get<uint64_t>();
                uuid.lo = j["uuid_lo"].get<uint64_t>();
                if (uuid.isValid()) return uuid;
            }
            catch (...) {}
        }
    }

    // Generate a fresh UUID and persist it.
    const UUID uuid = generateUUID();
    try
    {
        nlohmann::json j;
        j["uuid_hi"] = uuid.hi;
        j["uuid_lo"] = uuid.lo;
        std::ofstream ofs(metaPath);
        if (ofs.is_open())
            ofs << j.dump(4) << '\n';
    }
    catch (...) {}

    return uuid;
}

// ─── scan ─────────────────────────────────────────────────────────────────────

void MaterialCatalog::scan()
{
    m_entries.clear();
    m_uuidIndex.clear();

    if (m_root.empty() || !std::filesystem::exists(m_root)) return;

    std::error_code ec;
    for (const auto& dirEntry :
         std::filesystem::recursive_directory_iterator(m_root, ec))
    {
        if (!dirEntry.is_regular_file()) continue;
        const auto& p = dirEntry.path();
        if (!isImageFile(p))           continue;
        // Skip .meta companion files themselves.
        if (p.extension() == ".meta")  continue;

        MaterialEntry me;
        me.absPath    = p;
        me.displayName = p.stem().string();

        // Relative folder path within the root ("" when directly in root).
        {
            std::error_code relEc;
            const auto rel = std::filesystem::relative(p.parent_path(), m_root, relEc);
            if (!relEc && rel != ".")
                me.folderPath = rel.string();
        }

        me.uuid = readOrCreateMeta(p);

        // Query native image dimensions from the file header (no pixel decode).
        {
            int tw = 0, th = 0;
            if (stbi_info(p.c_str(), &tw, &th, nullptr))
            {
                me.texWidth  = static_cast<uint32_t>(tw);
                me.texHeight = static_cast<uint32_t>(th);
            }
        }

        m_entries.push_back(std::move(me));
    }

    // Sort by folder then display name for stable UI ordering.
    std::sort(m_entries.begin(), m_entries.end(),
        [](const MaterialEntry& a, const MaterialEntry& b)
        {
            if (a.folderPath != b.folderPath) return a.folderPath < b.folderPath;
            return a.displayName < b.displayName;
        });

    // Build UUID index after sort.
    for (std::size_t i = 0; i < m_entries.size(); ++i)
        m_uuidIndex[m_entries[i].uuid] = i;
}

// ─── find / findMutable ───────────────────────────────────────────────────────

const MaterialEntry* MaterialCatalog::find(const UUID& uuid) const noexcept
{
    const auto it = m_uuidIndex.find(uuid);
    if (it == m_uuidIndex.end()) return nullptr;
    return &m_entries[it->second];
}

MaterialEntry* MaterialCatalog::findMutable(const UUID& uuid) noexcept
{
    const auto it = m_uuidIndex.find(uuid);
    if (it == m_uuidIndex.end()) return nullptr;
    return &m_entries[it->second];
}

// ─── buildThumbnail ───────────────────────────────────────────────────────────

std::unique_ptr<rhi::ITexture>
MaterialCatalog::buildThumbnail(const std::filesystem::path& path,
                                 rhi::IRenderDevice&           device)
{
    constexpr int kSize = 64;

    int imgW = 0, imgH = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &imgW, &imgH, &channels, 4);
    if (!pixels) return nullptr;

    // Build kSize×kSize downscale via box filter.
    std::vector<stbi_uc> thumb(static_cast<std::size_t>(kSize * kSize * 4), 0);

    for (int ty = 0; ty < kSize; ++ty)
    for (int tx = 0; tx < kSize; ++tx)
    {
        const int sx0 = tx       * imgW / kSize;
        const int sx1 = (tx + 1) * imgW / kSize;
        const int sy0 = ty       * imgH / kSize;
        const int sy1 = (ty + 1) * imgH / kSize;

        int r = 0, g = 0, b = 0, a = 0, cnt = 0;
        for (int sy = sy0; sy < sy1; ++sy)
        for (int sx = sx0; sx < sx1; ++sx)
        {
            const int src = (sy * imgW + sx) * 4;
            r += pixels[src + 0];
            g += pixels[src + 1];
            b += pixels[src + 2];
            a += pixels[src + 3];
            ++cnt;
        }
        if (cnt > 0)
        {
            const int dst = (ty * kSize + tx) * 4;
            thumb[static_cast<std::size_t>(dst) + 0] = static_cast<stbi_uc>(r / cnt);
            thumb[static_cast<std::size_t>(dst) + 1] = static_cast<stbi_uc>(g / cnt);
            thumb[static_cast<std::size_t>(dst) + 2] = static_cast<stbi_uc>(b / cnt);
            thumb[static_cast<std::size_t>(dst) + 3] = static_cast<stbi_uc>(a / cnt);
        }
    }
    stbi_image_free(pixels);

    rhi::TextureDescriptor td;
    td.width     = static_cast<uint32_t>(kSize);
    td.height    = static_cast<uint32_t>(kSize);
    td.format    = rhi::TextureFormat::RGBA8Unorm;
    td.usage     = rhi::TextureUsage::ShaderRead;
    td.initData  = thumb.data();
    td.debugName = path.filename().string() + "_thumb";
    return device.createTexture(td);
}

// ─── getOrLoadThumbnail ───────────────────────────────────────────────────────

rhi::ITexture* MaterialCatalog::getOrLoadThumbnail(const UUID&         uuid,
                                                    rhi::IRenderDevice& device)
{
    MaterialEntry* entry = findMutable(uuid);
    if (!entry) return nullptr;

    if (!entry->thumbnail)
        entry->thumbnail = buildThumbnail(entry->absPath, device);

    return entry->thumbnail.get();
}

// ─── getOrLoadTexture ─────────────────────────────────────────────────────────

rhi::ITexture* MaterialCatalog::getOrLoadTexture(const UUID&           uuid,
                                                   rhi::IRenderDevice&   device,
                                                   render::IAssetLoader& loader)
{
    MaterialEntry* entry = findMutable(uuid);
    if (!entry) return nullptr;

    if (!entry->fullTexture)
    {
        auto result = loader.loadTexture(device, entry->absPath, /*sRGB=*/true);
        if (result.has_value())
            entry->fullTexture = std::move(*result);
    }

    return entry->fullTexture.get();
}

} // namespace daedalus::editor
