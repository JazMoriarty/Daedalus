// model_catalog.h
// Manages a directory tree of glTF / glb model assets for the editor's
// asset browser.
//
// Unlike MaterialCatalog (which uses UUIDs), models are referenced by a
// relative path string stored directly in EntityDef::assetPath.
// ModelEntry::relPath is the path relative to a caller-supplied base
// directory (typically the executable directory), so it matches the format
// expected by the renderer — e.g. "assets/models/Chair/Chair.gltf".

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace daedalus::editor
{

// ─── ModelEntry ──────────────────────────────────────────────────────────────

struct ModelEntry
{
    std::filesystem::path absPath;     ///< Absolute path on disk.
    std::filesystem::path relPath;     ///< Path relative to base directory (execDir).
    std::string           displayName; ///< Filename without extension (for display).
    std::string           folderPath;  ///< Relative folder within catalog root (for filter).
};

// ─── ModelCatalog ─────────────────────────────────────────────────────────────

class ModelCatalog
{
public:
    ModelCatalog()  = default;
    ~ModelCatalog() = default;

    ModelCatalog(const ModelCatalog&)            = delete;
    ModelCatalog& operator=(const ModelCatalog&) = delete;

    /// Set the root directory to scan for models, and a base directory used to
    /// compute the relative paths stored in EntityDef::assetPath fields.
    /// @param root  Directory containing model files (e.g. "…/assets/models").
    /// @param base  Directory from which relPath is computed (e.g. execDir).
    void setRoot(std::filesystem::path root, std::filesystem::path base);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return m_root; }

    /// Set the file extensions to match during scan (e.g. {"\"vox\""}). 
    /// If empty, defaults to {.gltf, .glb}.
    void setExtensions(std::initializer_list<std::string_view> exts);

    /// Scan (or re-scan) the root directory for model files.
    /// Clears any previously discovered entries.
    void scan();

    [[nodiscard]] const std::vector<ModelEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }

private:
    std::filesystem::path   m_root;
    std::filesystem::path   m_base;
    std::vector<ModelEntry> m_entries;
    std::vector<std::string> m_extensions;  ///< If empty, defaults to {.gltf, .glb}.

    bool isModelFile(const std::filesystem::path& p) const noexcept;
};

} // namespace daedalus::editor
