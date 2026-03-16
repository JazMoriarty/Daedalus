// model_catalog.cpp

#include "model_catalog.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace daedalus::editor
{

namespace
{

constexpr std::array<std::string_view, 2> k_modelExts = { ".gltf", ".glb" };

} // anonymous namespace

// ─── setExtensions ───────────────────────────────────────────────────────────

void ModelCatalog::setExtensions(std::initializer_list<std::string_view> exts)
{
    m_extensions.clear();
    for (auto sv : exts)
        m_extensions.emplace_back(sv);
}

// ─── isModelFile ─────────────────────────────────────────────────────────────

bool ModelCatalog::isModelFile(const std::filesystem::path& p) const noexcept
{
    std::string ext = p.extension().string();
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (m_extensions.empty())
    {
        for (const auto& e : k_modelExts)
            if (ext == e) return true;
        return false;
    }
    for (const auto& e : m_extensions)
        if (ext == e) return true;
    return false;
}

// ─── setRoot ─────────────────────────────────────────────────────────────────

void ModelCatalog::setRoot(std::filesystem::path root, std::filesystem::path base)
{
    m_root = std::move(root);
    m_base = std::move(base);
}

// ─── scan ─────────────────────────────────────────────────────────────────────

void ModelCatalog::scan()
{
    m_entries.clear();

    if (m_root.empty() || !std::filesystem::exists(m_root)) return;

    std::error_code ec;
    for (const auto& dirEntry :
         std::filesystem::recursive_directory_iterator(m_root, ec))
    {
        if (!dirEntry.is_regular_file()) continue;
        const auto& p = dirEntry.path();
        if (!isModelFile(p)) continue;

        ModelEntry me;
        me.absPath    = p;
        me.displayName = p.stem().string();

        // Relative path from base (execDir) — stored in EntityDef::assetPath.
        // We strip the base prefix as a string rather than using
        // std::filesystem::relative, which can misbehave when the base
        // path has a trailing separator (as SDL_GetBasePath produces on macOS).
        if (!m_base.empty())
        {
            std::string baseStr = m_base.string();
            if (baseStr.back() != '/') baseStr += '/';
            const std::string absStr = p.string();
            if (absStr.size() > baseStr.size() &&
                absStr.compare(0, baseStr.size(), baseStr) == 0)
                me.relPath = absStr.substr(baseStr.size());
            else
                me.relPath = p;  // fallback: absolute path
        }
        else
        {
            me.relPath = p;
        }

        // Folder path relative to catalog root (for the folder filter sidebar).
        {
            std::error_code relEc;
            const auto rel = std::filesystem::relative(p.parent_path(), m_root, relEc);
            if (!relEc && rel != ".")
                me.folderPath = rel.string();
        }

        m_entries.push_back(std::move(me));
    }

    // Sort by folder then display name for stable UI ordering.
    std::sort(m_entries.begin(), m_entries.end(),
        [](const ModelEntry& a, const ModelEntry& b)
        {
            if (a.folderPath != b.folderPath) return a.folderPath < b.folderPath;
            return a.displayName < b.displayName;
        });
}

} // namespace daedalus::editor
