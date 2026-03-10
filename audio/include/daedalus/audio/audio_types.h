// audio_types.h
// Plain data types shared across the DaedalusAudio public interface.
//
// No miniaudio types appear here. Consumers of DaedalusAudio see only
// engine-native types — miniaudio stays private to audio/src/.

#pragma once

#include "daedalus/core/types.h"

namespace daedalus::audio
{

// ─── AudioError ───────────────────────────────────────────────────────────────

enum class AudioError : u32
{
    FileNotFound,      ///< The sound file does not exist or cannot be opened.
    DeviceInitFailed,  ///< The audio device could not be initialised.
    InvalidHandle,     ///< The supplied SoundHandle or PlaybackHandle is invalid.
};

// ─── SoundHandle ──────────────────────────────────────────────────────────────
// Opaque handle representing a loaded (buffered) sound asset.
// Obtained from IAudioEngine::loadSound(); released via releaseSound().
// Value 0 is always invalid.

enum class SoundHandle : u32 {};

inline constexpr SoundHandle kInvalidSoundHandle { 0u };

[[nodiscard]] inline bool isValid(SoundHandle h) noexcept
{
    return static_cast<u32>(h) != 0u;
}

// ─── PlaybackHandle ───────────────────────────────────────────────────────────
// Opaque handle representing a single active playback instance.
// Obtained from IAudioEngine::play() or playMusic(); stopped via stop().
// Value 0 is always invalid.

enum class PlaybackHandle : u32 {};

inline constexpr PlaybackHandle kInvalidPlaybackHandle { 0u };

[[nodiscard]] inline bool isValid(PlaybackHandle h) noexcept
{
    return static_cast<u32>(h) != 0u;
}

} // namespace daedalus::audio
