// miniaudio_engine.h  (private — never included from audio/include/)
// miniaudio-backed implementation of IAudioEngine.
//
// All miniaudio types (ma_engine, ma_sound, …) are confined to this header
// and its corresponding .cpp.  No miniaudio symbols cross the module boundary.
//
// MINIAUDIO_IMPLEMENTATION must NOT be defined before including this header.
// It is defined exactly once in miniaudio_engine.cpp to compile the single-header
// library into that translation unit.

#pragma once

#include "daedalus/audio/i_audio_engine.h"

// Include miniaudio type declarations (no implementation here).
#include <miniaudio.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace daedalus::audio
{

// ─── MiniaudioEngine ──────────────────────────────────────────────────────────

class MiniaudioEngine final : public IAudioEngine
{
public:
    MiniaudioEngine();
    ~MiniaudioEngine() override;

    /// Returns true if the audio device was successfully initialised.
    /// All play* calls return AudioError::DeviceInitFailed when false.
    [[nodiscard]] bool isReady() const noexcept { return m_engineReady; }

    // ─── IAudioEngine ─────────────────────────────────────────────────────────

    [[nodiscard]] std::expected<SoundHandle, AudioError>
    loadSound(const std::filesystem::path& path) override;

    void releaseSound(SoundHandle handle) override;

    void setListenerTransform(glm::vec3 position,
                              glm::vec3 forward,
                              glm::vec3 up) override;

    [[nodiscard]] std::expected<PlaybackHandle, AudioError>
    play(SoundHandle handle,
         glm::vec3   worldPos,
         float       volume        = 1.0f,
         float       falloffRadius = 10.0f,
         bool        looping       = false) override;

    [[nodiscard]] std::expected<PlaybackHandle, AudioError>
    playMusic(SoundHandle handle,
              float       volume  = 1.0f,
              bool        looping = false) override;

    void stop(PlaybackHandle handle) override;
    void stopAll() override;

    void setMasterVolume(float volume) override;

    void update() override;

private:
    // ── miniaudio state ───────────────────────────────────────────────────────

    ma_engine m_engine{};
    bool      m_engineReady = false;

    // ── Handle counters ───────────────────────────────────────────────────────

    u32 m_nextSoundId    = 1u;
    u32 m_nextPlaybackId = 1u;

    // ── Sound catalog ─────────────────────────────────────────────────────────
    // loadSound() is lightweight: it records the path and returns a handle.
    // Actual decoding happens at play() time via ma_sound_init_from_file().
    // miniaudio's internal resource manager caches decoded audio data so
    // multiple concurrent play() calls for the same file are efficient.

    std::unordered_map<u32, std::string> m_soundPaths;    // handle id → path
    std::unordered_map<std::string, u32> m_pathToSound;   // path → handle id (dedup)

    // ── Active playbacks ──────────────────────────────────────────────────────
    // Each play() / playMusic() call allocates a ma_sound on the heap.
    // ma_sound must not be moved after init, so heap allocation is required.

    struct PlaybackEntry
    {
        std::unique_ptr<ma_sound> sound;
        u32 soundHandleId = 0u;
    };

    std::unordered_map<u32, PlaybackEntry> m_playbacks;

    // ── Private helpers ───────────────────────────────────────────────────────

    /// Allocate and initialise a ma_sound from the given path.
    ///
    /// @param path          Absolute path to the audio file.
    /// @param spatial       True → 3D positional; false → non-spatial.
    /// @param worldPos      Emitter world-space position (spatial only).
    /// @param volume        Per-instance volume [0, 1].
    /// @param falloffRadius Linear attenuation distance (spatial only).
    /// @param looping       True → sound loops until explicitly stopped.
    /// @return              Heap-allocated ma_sound, or nullptr on failure.
    [[nodiscard]] std::unique_ptr<ma_sound>
    initSound(const std::string& path,
              bool               spatial,
              glm::vec3          worldPos,
              float              volume,
              float              falloffRadius,
              bool               looping);

    /// Remove all completed (non-looping, at-end) sounds from m_playbacks.
    void purgeFinished();
};

} // namespace daedalus::audio
