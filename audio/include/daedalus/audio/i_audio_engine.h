// i_audio_engine.h
// Pure interface for the Daedalus audio subsystem.
//
// DaedalusAudio wraps miniaudio behind this interface so that no miniaudio
// types cross the module boundary. All higher-level code (app, game systems)
// depends only on this header.
//
// Responsibilities:
//   loadSound       — load and buffer a sound file from disk
//   releaseSound    — unload a buffered sound
//   setListenerTransform — update 3D listener position/orientation each frame
//   play            — start a world-positioned sound instance
//   playMusic       — start a non-positional (music/ambient) sound instance
//   stop            — stop a specific playback instance
//   stopAll         — stop all active instances
//   setMasterVolume — global output level [0, 1]
//   update          — process deferred callbacks; call once per frame
//
// Error handling: fallible operations return std::expected<T, AudioError>.
// Infallible operations (releaseSound, stop, stopAll, setMasterVolume, update)
// do not.

#pragma once

#include "daedalus/audio/audio_types.h"

#include <expected>
#include <filesystem>
#include <memory>

#include <glm/glm.hpp>

namespace daedalus::audio
{

// ─── IAudioEngine ─────────────────────────────────────────────────────────────

class IAudioEngine
{
public:
    virtual ~IAudioEngine() = default;

    IAudioEngine(const IAudioEngine&)            = delete;
    IAudioEngine& operator=(const IAudioEngine&) = delete;

    // ─── Sound asset management ───────────────────────────────────────────────

    /// Decode and buffer a sound file, returning a stable handle.
    ///
    /// Supports WAV, MP3, FLAC, and OGG Vorbis (via miniaudio built-in decoders).
    /// The file is streamed or fully decoded based on the engine's internal
    /// heuristic (short sounds are decoded to RAM; long sounds are streamed).
    ///
    /// @param path  Absolute or relative path to the sound file.
    /// @return      A valid SoundHandle, or AudioError::FileNotFound if the
    ///              file cannot be opened.
    [[nodiscard]] virtual std::expected<SoundHandle, AudioError>
    loadSound(const std::filesystem::path& path) = 0;

    /// Unload a sound asset and free its memory.
    ///
    /// Any active playback instances using this handle are stopped first.
    /// No-op if handle is kInvalidSoundHandle.
    ///
    /// @param handle  Handle returned by loadSound().
    virtual void releaseSound(SoundHandle handle) = 0;

    // ─── Listener ─────────────────────────────────────────────────────────────

    /// Update the 3D listener transform used for spatial audio.
    ///
    /// Should be called once per frame before any play() calls for that frame.
    /// Position and forward must be in the same world-space coordinate system
    /// as the worldPos arguments passed to play().
    ///
    /// @param position  Listener world-space position (camera/player position).
    /// @param forward   Normalised forward direction vector.
    /// @param up        Normalised up vector (typically world +Y).
    virtual void setListenerTransform(glm::vec3 position,
                                      glm::vec3 forward,
                                      glm::vec3 up) = 0;

    // ─── Playback ─────────────────────────────────────────────────────────────

    /// Start playing a sound at a world-space position with 3D spatialization.
    ///
    /// Attenuation is linear: full volume at distance 0, silence at falloffRadius.
    /// Multiple simultaneous instances of the same SoundHandle are supported.
    ///
    /// @param handle        A valid SoundHandle returned by loadSound().
    /// @param worldPos      World-space position of the sound emitter.
    /// @param volume        Per-instance volume multiplier [0, 1].
    /// @param falloffRadius Distance in world units at which volume reaches zero.
    /// @param looping       If true, the sound repeats until explicitly stopped.
    /// @return              A PlaybackHandle to track and stop this instance, or
    ///                      AudioError::InvalidHandle if handle is invalid.
    [[nodiscard]] virtual std::expected<PlaybackHandle, AudioError>
    play(SoundHandle   handle,
         glm::vec3     worldPos,
         float         volume        = 1.0f,
         float         falloffRadius = 10.0f,
         bool          looping       = false) = 0;

    /// Start playing a sound without 3D spatialization (music, UI sounds, ambience).
    ///
    /// The sound plays at the given volume regardless of listener position.
    ///
    /// @param handle   A valid SoundHandle returned by loadSound().
    /// @param volume   Output volume [0, 1].
    /// @param looping  If true, the sound repeats until explicitly stopped.
    /// @return         A PlaybackHandle, or AudioError::InvalidHandle.
    [[nodiscard]] virtual std::expected<PlaybackHandle, AudioError>
    playMusic(SoundHandle handle,
              float       volume  = 1.0f,
              bool        looping = false) = 0;

    /// Stop a specific active playback instance.
    ///
    /// No-op if handle is kInvalidPlaybackHandle or the instance has already
    /// finished naturally.
    ///
    /// @param handle  Handle returned by play() or playMusic().
    virtual void stop(PlaybackHandle handle) = 0;

    /// Stop all currently active playback instances immediately.
    virtual void stopAll() = 0;

    // ─── Volume ───────────────────────────────────────────────────────────────

    /// Set the master output volume applied to all sounds.
    ///
    /// @param volume  Clamped to [0, 1]. 0 = silence, 1 = full output.
    virtual void setMasterVolume(float volume) = 0;

    // ─── Per-frame ────────────────────────────────────────────────────────────

    /// Process deferred audio callbacks and advance internal state.
    ///
    /// Must be called once per frame, before AudioSystem::update().
    /// Safe to call on a fresh engine with no active sounds.
    virtual void update() = 0;

protected:
    IAudioEngine() = default;
};

// ─── Factory ──────────────────────────────────────────────────────────────────

/// Construct the miniaudio-backed audio engine implementation.
///
/// Initialises the audio device and mixer. Returns nullptr (via
/// AudioError::DeviceInitFailed) if the audio device cannot be opened.
///
/// @return  A fully initialised IAudioEngine, or AudioError::DeviceInitFailed.
[[nodiscard]] std::expected<std::unique_ptr<IAudioEngine>, AudioError>
makeAudioEngine();

} // namespace daedalus::audio
