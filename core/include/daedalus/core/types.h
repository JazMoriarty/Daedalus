#pragma once

#include <cstddef>
#include <cstdint>

namespace daedalus
{

// ─── Numeric type aliases ─────────────────────────────────────────────────────

using u8    = uint8_t;
using u16   = uint16_t;
using u32   = uint32_t;
using u64   = uint64_t;

using i8    = int8_t;
using i16   = int16_t;
using i32   = int32_t;
using i64   = int64_t;

using f32   = float;
using f64   = double;

using usize = size_t;
using isize = ptrdiff_t;

using byte  = std::byte;

// ─── EntityId ─────────────────────────────────────────────────────────────────
// Stable opaque 64-bit entity handle encoded as:
//   bits [63:32] — generation counter (stale-handle detection)
//   bits [31:0]  — index into the entity table

using EntityId = u64;

constexpr EntityId INVALID_ENTITY = 0u;

[[nodiscard]] inline constexpr u32 entityIndex(EntityId id) noexcept
{
    return static_cast<u32>(id & 0xFFFF'FFFFu);
}

[[nodiscard]] inline constexpr u32 entityGeneration(EntityId id) noexcept
{
    return static_cast<u32>(id >> 32u);
}

[[nodiscard]] inline constexpr EntityId makeEntityId(u32 index, u32 generation) noexcept
{
    return (static_cast<u64>(generation) << 32u) | static_cast<u64>(index);
}

// ─── UUID ──────────────────────────────────────────────────────────────────────
// 128-bit content-addressed asset identifier. Stable across processes and
// sessions. Zero value is the null/invalid UUID.

struct UUID
{
    u64 hi = 0;
    u64 lo = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept { return hi != 0 || lo != 0; }

    constexpr bool operator==(const UUID&) const noexcept = default;
};

struct UUIDHash
{
    [[nodiscard]] usize operator()(const UUID& uuid) const noexcept
    {
        // Mixing two 64-bit words via a FNV-like multiplicative cascade.
        usize h = uuid.hi ^ (uuid.hi >> 33u);
        h *= 0xff51afd7ed558ccdULL;
        h ^= uuid.lo ^ (uuid.lo >> 33u);
        h *= 0xc4ceb9fe1a85ec53ULL;
        return h;
    }
};

} // namespace daedalus
