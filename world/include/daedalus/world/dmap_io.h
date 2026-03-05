// dmap_io.h
// Serialisation and deserialisation for the .dmap binary format and the
// .dmap.json ASCII format.
//
// Binary format (.dmap):
//   A compact, versioned binary format designed for fast runtime loading.
//   See dmap_binary.cpp for the full byte-level layout specification.
//
// JSON format (.dmap.json):
//   A human-readable ASCII representation intended for debugging and
//   source-control diffs. Not used at runtime.
//
// Error handling: all operations return std::expected — errors are never
// silently swallowed. See DmapError for typed failure reasons.

#pragma once

#include "daedalus/world/map_data.h"
#include "daedalus/core/types.h"

#include <expected>
#include <filesystem>
#include <span>
#include <vector>

namespace daedalus::world
{

// ─── DmapError ────────────────────────────────────────────────────────────────

enum class DmapError : u32
{
    FileNotFound,     ///< The specified path does not exist or cannot be opened.
    WriteError,       ///< Failed to write to the destination path.
    ParseError,       ///< Data is corrupt or does not match the expected format.
    VersionMismatch,  ///< Binary format version is newer than this build supports.
};

// ─── Binary .dmap ─────────────────────────────────────────────────────────────

/// Deserialise a WorldMapData from a binary .dmap file.
///
/// @param path  Absolute or relative path to the .dmap file.
/// @return      The loaded map data, or a DmapError.
[[nodiscard]] std::expected<WorldMapData, DmapError>
loadDmap(const std::filesystem::path& path);

/// Serialise a WorldMapData to a binary .dmap file.
///
/// @param map   The map data to write.
/// @param path  Destination path. The file is created or overwritten.
/// @return      void on success, or a DmapError.
[[nodiscard]] std::expected<void, DmapError>
saveDmap(const WorldMapData& map, const std::filesystem::path& path);

// ─── JSON .dmap.json ──────────────────────────────────────────────────────────

/// Deserialise a WorldMapData from a JSON .dmap.json file.
///
/// @param path  Absolute or relative path to the .dmap.json file.
/// @return      The loaded map data, or a DmapError.
[[nodiscard]] std::expected<WorldMapData, DmapError>
loadDmapJson(const std::filesystem::path& path);

/// Serialise a WorldMapData to a JSON .dmap.json file.
///
/// @param map   The map data to write.
/// @param path  Destination path. The file is created or overwritten.
/// @return      void on success, or a DmapError.
[[nodiscard]] std::expected<void, DmapError>
saveDmapJson(const WorldMapData& map, const std::filesystem::path& path);

} // namespace daedalus::world
