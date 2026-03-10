// miniaudio_engine.cpp
// miniaudio-backed IAudioEngine implementation.
//
// MINIAUDIO_IMPLEMENTATION is defined exactly once in this translation unit,
// compiling the entire miniaudio single-header library into this object file.
// No other TU must define MINIAUDIO_IMPLEMENTATION.
//
// The header include order is critical:
//   1. #define MINIAUDIO_IMPLEMENTATION
//   2. #include <miniaudio.h>   ← compiles full implementation (include guard set)
//   3. #include "miniaudio_engine.h"  ← re-includes miniaudio.h → no-op (guard)

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "miniaudio_engine.h"

#include <algorithm>
#include <vector>

namespace daedalus::audio
{

// ─── MiniaudioEngine — constructor / destructor ───────────────────────────────

MiniaudioEngine::MiniaudioEngine()
{
    ma_engine_config cfg = ma_engine_config_init();
    // Request default output device with default sample rate.
    if (ma_engine_init(&cfg, &m_engine) == MA_SUCCESS)
    {
        m_engineReady = true;
    }
    // If the audio device is unavailable (headless CI, sandboxed environment, etc.)
    // m_engineReady stays false; all play* calls gracefully return errors.
}

MiniaudioEngine::~MiniaudioEngine()
{
    // Uninit all active sounds before tearing down the engine.
    for (auto& [id, entry] : m_playbacks)
    {
        if (entry.sound)
        {
            ma_sound_stop(entry.sound.get());
            ma_sound_uninit(entry.sound.get());
        }
    }
    m_playbacks.clear();

    if (m_engineReady)
    {
        ma_engine_uninit(&m_engine);
    }
}

// ─── loadSound ────────────────────────────────────────────────────────────────

std::expected<SoundHandle, AudioError>
MiniaudioEngine::loadSound(const std::filesystem::path& path)
{
    const std::string pathStr = path.string();

    // Deduplication: return existing handle if this path is already registered.
    if (const auto it = m_pathToSound.find(pathStr); it != m_pathToSound.end())
    {
        return static_cast<SoundHandle>(it->second);
    }

    // Early file-existence check for a clear error code.
    if (!std::filesystem::exists(path))
    {
        return std::unexpected(AudioError::FileNotFound);
    }

    const u32 id        = m_nextSoundId++;
    m_soundPaths[id]    = pathStr;
    m_pathToSound[pathStr] = id;

    return static_cast<SoundHandle>(id);
}

// ─── releaseSound ─────────────────────────────────────────────────────────────

void MiniaudioEngine::releaseSound(SoundHandle handle)
{
    const u32 id = static_cast<u32>(handle);

    const auto it = m_soundPaths.find(id);
    if (it == m_soundPaths.end()) { return; }

    // Stop every active playback that uses this sound handle.
    std::vector<u32> toStop;
    for (const auto& [pbId, entry] : m_playbacks)
    {
        if (entry.soundHandleId == id) { toStop.push_back(pbId); }
    }
    for (u32 pbId : toStop)
    {
        stop(static_cast<PlaybackHandle>(pbId));
    }

    m_pathToSound.erase(it->second);
    m_soundPaths.erase(it);
}

// ─── setListenerTransform ─────────────────────────────────────────────────────

void MiniaudioEngine::setListenerTransform(glm::vec3 position,
                                            glm::vec3 forward,
                                            glm::vec3 up)
{
    if (!m_engineReady) { return; }

    ma_engine_listener_set_position(&m_engine, 0,
        position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_engine, 0,
        forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_engine, 0,
        up.x, up.y, up.z);
}

// ─── initSound (private helper) ───────────────────────────────────────────────

std::unique_ptr<ma_sound>
MiniaudioEngine::initSound(const std::string& path,
                            bool               spatial,
                            glm::vec3          worldPos,
                            float              volume,
                            float              falloffRadius,
                            bool               looping)
{
    if (!m_engineReady) { return nullptr; }

    auto sound = std::make_unique<ma_sound>();

    ma_uint32 flags = MA_SOUND_FLAG_DECODE;
    if (!spatial) { flags |= MA_SOUND_FLAG_NO_SPATIALIZATION; }

    if (ma_sound_init_from_file(&m_engine, path.c_str(), flags,
                                nullptr, nullptr, sound.get()) != MA_SUCCESS)
    {
        return nullptr;
    }

    ma_sound_set_volume(sound.get(), volume);
    ma_sound_set_looping(sound.get(), looping ? MA_TRUE : MA_FALSE);

    if (spatial)
    {
        ma_sound_set_position(sound.get(), worldPos.x, worldPos.y, worldPos.z);
        ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_linear);
        ma_sound_set_max_distance(sound.get(), falloffRadius);
        ma_sound_set_rolloff(sound.get(), 1.0f);
    }

    return sound;
}

// ─── play ─────────────────────────────────────────────────────────────────────

std::expected<PlaybackHandle, AudioError>
MiniaudioEngine::play(SoundHandle handle,
                       glm::vec3   worldPos,
                       float       volume,
                       float       falloffRadius,
                       bool        looping)
{
    if (!m_engineReady) { return std::unexpected(AudioError::DeviceInitFailed); }

    const u32 id = static_cast<u32>(handle);
    const auto it = m_soundPaths.find(id);
    if (it == m_soundPaths.end()) { return std::unexpected(AudioError::InvalidHandle); }

    auto sound = initSound(it->second, /*spatial=*/true, worldPos,
                           volume, falloffRadius, looping);
    if (!sound) { return std::unexpected(AudioError::FileNotFound); }

    if (ma_sound_start(sound.get()) != MA_SUCCESS)
    {
        ma_sound_uninit(sound.get());
        return std::unexpected(AudioError::DeviceInitFailed);
    }

    const u32 pbId = m_nextPlaybackId++;
    m_playbacks[pbId] = { std::move(sound), id };
    return static_cast<PlaybackHandle>(pbId);
}

// ─── playMusic ────────────────────────────────────────────────────────────────

std::expected<PlaybackHandle, AudioError>
MiniaudioEngine::playMusic(SoundHandle handle, float volume, bool looping)
{
    if (!m_engineReady) { return std::unexpected(AudioError::DeviceInitFailed); }

    const u32 id = static_cast<u32>(handle);
    const auto it = m_soundPaths.find(id);
    if (it == m_soundPaths.end()) { return std::unexpected(AudioError::InvalidHandle); }

    auto sound = initSound(it->second, /*spatial=*/false, glm::vec3(0.0f),
                           volume, 0.0f, looping);
    if (!sound) { return std::unexpected(AudioError::FileNotFound); }

    if (ma_sound_start(sound.get()) != MA_SUCCESS)
    {
        ma_sound_uninit(sound.get());
        return std::unexpected(AudioError::DeviceInitFailed);
    }

    const u32 pbId = m_nextPlaybackId++;
    m_playbacks[pbId] = { std::move(sound), id };
    return static_cast<PlaybackHandle>(pbId);
}

// ─── stop ─────────────────────────────────────────────────────────────────────

void MiniaudioEngine::stop(PlaybackHandle handle)
{
    const u32 id = static_cast<u32>(handle);
    const auto it = m_playbacks.find(id);
    if (it == m_playbacks.end()) { return; }

    if (it->second.sound)
    {
        ma_sound_stop(it->second.sound.get());
        ma_sound_uninit(it->second.sound.get());
    }
    m_playbacks.erase(it);
}

// ─── stopAll ──────────────────────────────────────────────────────────────────

void MiniaudioEngine::stopAll()
{
    for (auto& [id, entry] : m_playbacks)
    {
        if (entry.sound)
        {
            ma_sound_stop(entry.sound.get());
            ma_sound_uninit(entry.sound.get());
        }
    }
    m_playbacks.clear();
}

// ─── setMasterVolume ──────────────────────────────────────────────────────────

void MiniaudioEngine::setMasterVolume(float volume)
{
    if (!m_engineReady) { return; }
    const float clamped = std::clamp(volume, 0.0f, 1.0f);
    ma_engine_set_volume(&m_engine, clamped);
}

// ─── update ───────────────────────────────────────────────────────────────────

void MiniaudioEngine::update()
{
    purgeFinished();
}

// ─── purgeFinished (private) ──────────────────────────────────────────────────

void MiniaudioEngine::purgeFinished()
{
    std::vector<u32> finished;

    for (const auto& [id, entry] : m_playbacks)
    {
        if (entry.sound && ma_sound_at_end(entry.sound.get()))
        {
            finished.push_back(id);
        }
    }

    for (u32 id : finished)
    {
        const auto it = m_playbacks.find(id);
        if (it != m_playbacks.end())
        {
            ma_sound_uninit(it->second.sound.get());
            m_playbacks.erase(it);
        }
    }
}

// ─── Factory ──────────────────────────────────────────────────────────────────

std::expected<std::unique_ptr<IAudioEngine>, AudioError>
makeAudioEngine()
{
    auto engine = std::make_unique<MiniaudioEngine>();
    if (!engine->isReady())
    {
        return std::unexpected(AudioError::DeviceInitFailed);
    }
    return engine;
}

} // namespace daedalus::audio
