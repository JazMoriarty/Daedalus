// audio_system.cpp
// ECS-to-audio bridge implementation.
//
// Relies on TransformComponent for world-space emitter position and
// SoundEmitterComponent for playback parameters. No miniaudio types are used
// here; all audio calls go through the IAudioEngine interface.

#include "daedalus/audio/audio_system.h"
#include "daedalus/audio/components/sound_emitter_component.h"
#include "daedalus/core/components/transform_component.h"

#include <unordered_set>
#include <vector>

namespace daedalus::audio
{

// ─── acquireSound ─────────────────────────────────────────────────────────────

SoundHandle AudioSystem::acquireSound(IAudioEngine&               engine,
                                       const std::filesystem::path& path)
{
    const std::string key = path.string();

    if (const auto it = m_soundCache.find(key); it != m_soundCache.end())
    {
        return it->second;
    }

    auto result = engine.loadSound(path);
    if (!result.has_value()) { return kInvalidSoundHandle; }

    m_soundCache[key] = *result;
    return *result;
}

// ─── update ───────────────────────────────────────────────────────────────────

void AudioSystem::update(IAudioEngine&    engine,
                          daedalus::World& world,
                          glm::vec3        listenerPos,
                          glm::vec3        forward,
                          glm::vec3        up)
{
    engine.setListenerTransform(listenerPos, forward, up);

    // ── Collect active emitter entities and start new autoPlay sounds ─────────

    std::unordered_set<EntityId> activeEmitters;

    world.each<daedalus::TransformComponent, SoundEmitterComponent>(
        [&](EntityId id,
            daedalus::TransformComponent& tf,
            SoundEmitterComponent&        sec)
        {
            activeEmitters.insert(id);

            // Respect the autoPlay gate.
            if (!sec.autoPlay) { return; }

            // Already have an active playback for this entity — leave it running.
            if (m_playbacks.count(id)) { return; }

            // Nothing to play if no path is set.
            if (sec.soundPath.empty()) { return; }

            const SoundHandle sh = acquireSound(engine, sec.soundPath);
            if (!isValid(sh)) { return; }

            // Spatial (falloffRadius > 0) or non-spatial (music/ambient).
            std::expected<PlaybackHandle, AudioError> ph;
            if (sec.falloffRadius > 0.0f)
            {
                ph = engine.play(sh, tf.position,
                                 sec.volume, sec.falloffRadius, sec.looping);
            }
            else
            {
                ph = engine.playMusic(sh, sec.volume, sec.looping);
            }

            if (ph.has_value())
            {
                m_playbacks[id] = *ph;
            }
        }
    );

    // ── Stop playback for emitters that are no longer present ─────────────────

    std::vector<EntityId> toRemove;
    toRemove.reserve(m_playbacks.size());

    for (const auto& [entityId, ph] : m_playbacks)
    {
        if (!activeEmitters.count(entityId))
        {
            engine.stop(ph);  // no-op if the sound already finished naturally
            toRemove.push_back(entityId);
        }
    }

    for (EntityId eid : toRemove)
    {
        m_playbacks.erase(eid);
    }
}

// ─── startEmitter ─────────────────────────────────────────────────────────────

void AudioSystem::startEmitter(EntityId         id,
                                IAudioEngine&    engine,
                                daedalus::World& world)
{
    if (!world.hasComponent<daedalus::TransformComponent>(id)) { return; }
    if (!world.hasComponent<SoundEmitterComponent>(id))        { return; }

    // Stop any existing playback first.
    stopEmitter(id, engine);

    const auto& tf  = world.getComponent<daedalus::TransformComponent>(id);
    const auto& sec = world.getComponent<SoundEmitterComponent>(id);

    if (sec.soundPath.empty()) { return; }

    const SoundHandle sh = acquireSound(engine, sec.soundPath);
    if (!isValid(sh)) { return; }

    std::expected<PlaybackHandle, AudioError> ph;
    if (sec.falloffRadius > 0.0f)
    {
        ph = engine.play(sh, tf.position,
                         sec.volume, sec.falloffRadius, sec.looping);
    }
    else
    {
        ph = engine.playMusic(sh, sec.volume, sec.looping);
    }

    if (ph.has_value())
    {
        m_playbacks[id] = *ph;
    }
}

// ─── stopEmitter ──────────────────────────────────────────────────────────────

void AudioSystem::stopEmitter(EntityId id, IAudioEngine& engine)
{
    const auto it = m_playbacks.find(id);
    if (it == m_playbacks.end()) { return; }

    engine.stop(it->second);
    m_playbacks.erase(it);
}

} // namespace daedalus::audio
