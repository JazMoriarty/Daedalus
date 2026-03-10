// audio_system.h
// ECS-to-audio bridge: drives IAudioEngine from TransformComponent +
// SoundEmitterComponent data each frame.
//
// AudioSystem does not own the engine; it is passed by reference to update().
// The engine's update() must be called BEFORE AudioSystem::update() each frame
// so finished sounds are purged before new ones are started.
//
// Responsibilities:
//   update — set listener transform, start new autoPlay emitters, stop removed ones
//   stopEmitter — explicitly stop a specific emitter's active playback

#pragma once

#include "daedalus/audio/i_audio_engine.h"
#include "daedalus/core/ecs/world.h"
#include "daedalus/core/types.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

namespace daedalus::audio
{

// ─── AudioSystem ──────────────────────────────────────────────────────────────

class AudioSystem
{
public:
    AudioSystem()  = default;
    ~AudioSystem() = default;

    AudioSystem(const AudioSystem&)            = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // ─── Per-frame ────────────────────────────────────────────────────────────

    /// Update audio state for one frame.
    ///
    /// Sets the 3D listener transform then iterates all entities that have both
    /// a TransformComponent and a SoundEmitterComponent:
    ///
    ///  - New entities with autoPlay=true: the sound is loaded (or retrieved from
    ///    the cache) and playback is started.
    ///  - Entities that have disappeared since the last call: their active
    ///    playback is stopped.
    ///
    /// Emitters with autoPlay=false are tracked for removal but never auto-started;
    /// manage their playback manually via startEmitter() / stopEmitter().
    ///
    /// @param engine       The audio engine to drive (must outlive this call).
    /// @param world        ECS world queried for emitter + transform components.
    /// @param listenerPos  World-space listener position (camera eye position).
    /// @param forward      Normalised listener forward direction.
    /// @param up           World up vector (typically world +Y).
    void update(IAudioEngine&    engine,
                daedalus::World& world,
                glm::vec3        listenerPos,
                glm::vec3        forward,
                glm::vec3        up);

    // ─── Manual emitter control ───────────────────────────────────────────────

    /// Start playback for a specific emitter entity immediately, ignoring autoPlay.
    ///
    /// If the entity already has an active playback it is stopped first.
    /// Requires TransformComponent and SoundEmitterComponent to be present.
    /// No-op if either component is missing or soundPath is empty.
    ///
    /// @param id      Entity to start.
    /// @param engine  The audio engine to drive.
    /// @param world   ECS world containing the entity's components.
    void startEmitter(EntityId         id,
                      IAudioEngine&    engine,
                      daedalus::World& world);

    /// Stop the active playback for a specific emitter entity.
    ///
    /// No-op if the entity has no active playback.
    ///
    /// @param id      Entity whose playback should be stopped.
    /// @param engine  The audio engine to drive.
    void stopEmitter(EntityId id, IAudioEngine& engine);

private:
    // ── Runtime state ─────────────────────────────────────────────────────────

    // entity → current PlaybackHandle (at most one active playback per entity)
    std::unordered_map<EntityId, PlaybackHandle> m_playbacks;

    // sound-file path string → SoundHandle (avoids redundant loadSound calls)
    std::unordered_map<std::string, SoundHandle> m_soundCache;

    // ── Private helpers ───────────────────────────────────────────────────────

    /// Load (or retrieve from cache) the SoundHandle for the given path.
    /// Returns kInvalidSoundHandle on failure.
    [[nodiscard]] SoundHandle acquireSound(IAudioEngine&              engine,
                                           const std::filesystem::path& path);
};

} // namespace daedalus::audio
