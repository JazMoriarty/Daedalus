// sound_emitter_component.h
// ECS component that tags an entity as a 3D sound emitter.
//
// Combine with TransformComponent; AudioSystem iterates entities with
// both components and drives IAudioEngine accordingly.
//
// No SoundHandle or PlaybackHandle fields appear here — those are runtime
// state owned by AudioSystem, not persistent component data.  This keeps
// the component dependency-free from audio implementation details and safe
// to serialise from the .dlevel format.

#pragma once

#include <filesystem>

namespace daedalus::audio
{

// ─── SoundEmitterComponent ────────────────────────────────────────────────────

struct SoundEmitterComponent
{
    /// Path to the sound file asset.  Resolved at runtime by AudioSystem.
    std::filesystem::path soundPath;

    /// Per-emitter volume multiplier [0, 1].
    float volume        = 1.0f;

    /// Distance in world units at which the sound reaches silence.
    /// Set to 0 or negative to treat as a non-positional (music/ambient) sound.
    float falloffRadius = 10.0f;

    /// Whether the sound loops continuously.
    bool  looping  = false;

    /// Start playing immediately on the first AudioSystem::update() encounter.
    /// Set to false to manage playback manually via AudioSystem::startEmitter().
    bool  autoPlay = true;
};

} // namespace daedalus::audio
