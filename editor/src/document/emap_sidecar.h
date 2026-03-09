// emap_sidecar.h
// Editor-state sidecar serialisation for DaedalusEdit.
//
// Saves and loads editor-only state that lives on EditMapDocument but must NOT
// pollute WorldMapData or the dmap_io world module.  A sidecar file (.emap) is
// written alongside every .dmap file and is silently absent when loading legacy
// maps that pre-date the sidecar format.
//
// Error handling follows the same std::expected<T, E> convention as dmap_io.h:
// no error path is silently swallowed.
//
// File format: versioned JSON (.emap, UTF-8).

#pragma once

#include <expected>
#include <filesystem>

namespace daedalus::editor
{

class EditMapDocument;

// ─── EmapError ───────────────────────────────────────────────────────────────

enum class EmapError
{
    WriteError,   ///< Could not write the .emap file.
    ParseError,   ///< File exists but is corrupt or version-mismatched.
};

// ─── saveEmap ────────────────────────────────────────────────────────────────

/// Serialise editor-only state from @p doc to @p emapPath.
///
/// Writes: scene settings, map defaults, player start, lights, layer state.
/// Returns WriteError if the file cannot be created or written.
[[nodiscard]] std::expected<void, EmapError>
saveEmap(const EditMapDocument& doc, const std::filesystem::path& emapPath);

// ─── loadEmap ────────────────────────────────────────────────────────────────

/// Deserialise editor-only state from @p emapPath into @p doc.
///
/// A missing file is NOT an error — caller should check before calling, or
/// treat std::nullopt as "use defaults".  A present-but-corrupt file returns
/// ParseError.
[[nodiscard]] std::expected<void, EmapError>
loadEmap(EditMapDocument& doc, const std::filesystem::path& emapPath);

} // namespace daedalus::editor
