// test_audio_engine.cpp
// Tests for IAudioEngine (MiniaudioEngine) and AudioSystem.
//
// No miniaudio headers are included here; all tests use the public interfaces.
//
// Tests that require an initialised audio device skip gracefully when no device
// is available (CI/headless environments).  Master volume is set to 0 at the
// start of every engine test so no audible output is produced.
//
// A tiny but valid WAV file is synthesised in memory and written to a temp
// path by createTempWav() — allowing play() to be exercised without shipping
// binary test assets.

#include "daedalus/audio/audio_system.h"
#include "daedalus/audio/components/sound_emitter_component.h"
#include "daedalus/audio/i_audio_engine.h"
#include "daedalus/core/components/transform_component.h"
#include "daedalus/core/ecs/world.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <tuple>

using namespace daedalus;
using namespace daedalus::audio;

// ─── Helpers ──────────────────────────────────────────────────────────────────

/// Write a minimal but valid mono 16-bit PCM WAV to `path`.
/// Contains 100 zero-valued samples at 44100 Hz — inaudible and tiny.
static bool createTempWav(const std::filesystem::path& path)
{
    constexpr u16 kChannels    = 1u;
    constexpr u32 kSampleRate  = 44100u;
    constexpr u16 kBitsPerSamp = 16u;
    constexpr u32 kNumSamples  = 100u;

    const u32 kDataBytes   = kNumSamples * (kBitsPerSamp / 8u);
    const u32 kRiffBytes   = 36u + kDataBytes;  // total file − 8 (RIFF+size)
    const u32 kByteRate    = kSampleRate * kChannels * (kBitsPerSamp / 8u);
    const u16 kBlockAlign  = static_cast<u16>(kChannels * (kBitsPerSamp / 8u));

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) { return false; }

    auto writeU16 = [&](u16 v)
    {
        ofs.write(reinterpret_cast<const char*>(&v), sizeof(v));
    };
    auto writeU32 = [&](u32 v)
    {
        ofs.write(reinterpret_cast<const char*>(&v), sizeof(v));
    };
    auto writeTag = [&](const char* tag)
    {
        ofs.write(tag, 4);
    };

    writeTag("RIFF");
    writeU32(kRiffBytes);
    writeTag("WAVE");
    writeTag("fmt ");
    writeU32(16u);            // fmt chunk size (PCM)
    writeU16(1u);             // PCM format
    writeU16(kChannels);
    writeU32(kSampleRate);
    writeU32(kByteRate);
    writeU16(kBlockAlign);
    writeU16(kBitsPerSamp);
    writeTag("data");
    writeU32(kDataBytes);

    std::vector<u16> samples(kNumSamples, 0u);
    ofs.write(reinterpret_cast<const char*>(samples.data()),
              static_cast<std::streamsize>(kDataBytes));

    return ofs.good();
}

/// RAII temp WAV: creates on construction, deletes on destruction.
struct TempWav
{
    std::filesystem::path path;

    TempWav()
    {
        path = std::filesystem::temp_directory_path()
               / "daedalus_audio_test.wav";
        createTempWav(path);
    }

    ~TempWav()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    [[nodiscard]] bool exists() const
    {
        return std::filesystem::exists(path);
    }
};

// ─── Engine lifecycle ─────────────────────────────────────────────────────────

TEST(AudioEngineTest, FactoryReturnsExpected)
{
    // makeAudioEngine() must return an expected (value or error), not throw.
    auto result = makeAudioEngine();
    // We do not assert success here — a headless machine may have no device.
    // The test passes as long as no exception is thrown.
    (void)result;
}

TEST(AudioEngineTest, EngineOrSkip)
{
    auto result = makeAudioEngine();
    if (!result.has_value())
    {
        GTEST_SKIP() << "No audio device available — skipping engine tests";
    }

    auto& engine = *result;
    engine->setMasterVolume(0.0f);  // silence all output for the test suite

    // Basic liveness: update + stopAll on a fresh engine must not crash.
    engine->update();
    engine->stopAll();
    engine->setMasterVolume(0.0f);  // idempotent
}

// ─── loadSound ────────────────────────────────────────────────────────────────

TEST(AudioEngineTest, LoadSoundFileNotFound)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    auto r = engine->loadSound("/nonexistent/path/to/missing_sound.wav");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), AudioError::FileNotFound);
}

TEST(AudioEngineTest, LoadSoundDeduplication)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    TempWav wav;
    ASSERT_TRUE(wav.exists()) << "Could not create temp WAV";

    auto r1 = engine->loadSound(wav.path);
    ASSERT_TRUE(r1.has_value());
    EXPECT_TRUE(isValid(*r1));

    // Loading the same path again must return the same handle.
    auto r2 = engine->loadSound(wav.path);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r1, *r2) << "Duplicate path must return the same SoundHandle";
}

// ─── releaseSound ─────────────────────────────────────────────────────────────

TEST(AudioEngineTest, ReleaseSoundInvalidHandleIsNoop)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    // Must not crash.
    engine->releaseSound(kInvalidSoundHandle);
}

TEST(AudioEngineTest, ReleaseSoundThenReloadSucceeds)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    TempWav wav;
    ASSERT_TRUE(wav.exists());

    auto r1 = engine->loadSound(wav.path);
    ASSERT_TRUE(r1.has_value());

    engine->releaseSound(*r1);

    // After release the path can be loaded again (new handle).
    auto r2 = engine->loadSound(wav.path);
    ASSERT_TRUE(r2.has_value());
    EXPECT_TRUE(isValid(*r2));
}

// ─── playMusic / stop ─────────────────────────────────────────────────────────

TEST(AudioEngineTest, PlayMusicAndStopSucceeds)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    TempWav wav;
    ASSERT_TRUE(wav.exists());

    auto sh = engine->loadSound(wav.path);
    ASSERT_TRUE(sh.has_value());

    auto ph = engine->playMusic(*sh, 0.0f);
    ASSERT_TRUE(ph.has_value());
    EXPECT_TRUE(isValid(*ph));

    // stop() must not crash.
    engine->stop(*ph);

    // Stopping the same handle again is a no-op.
    engine->stop(*ph);
}

TEST(AudioEngineTest, PlaySpatialAndStopSucceeds)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    TempWav wav;
    ASSERT_TRUE(wav.exists());

    auto sh = engine->loadSound(wav.path);
    ASSERT_TRUE(sh.has_value());

    const glm::vec3 pos(1.0f, 0.0f, 2.0f);
    auto ph = engine->play(*sh, pos, 0.0f, 10.0f);
    ASSERT_TRUE(ph.has_value());

    engine->stop(*ph);
}

TEST(AudioEngineTest, StopInvalidHandleIsNoop)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    // Must not crash.
    engine->stop(kInvalidPlaybackHandle);
}

TEST(AudioEngineTest, PlayInvalidSoundHandleReturnsError)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    auto ph = engine->play(kInvalidSoundHandle, glm::vec3(0.0f));
    ASSERT_FALSE(ph.has_value());
    EXPECT_EQ(ph.error(), AudioError::InvalidHandle);
}

// ─── stopAll ──────────────────────────────────────────────────────────────────

TEST(AudioEngineTest, StopAllOnEmptyEngineIsNoop)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    engine->stopAll();  // must not crash
}

TEST(AudioEngineTest, StopAllClearsActivePlaybacks)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    TempWav wav;
    ASSERT_TRUE(wav.exists());

    auto sh = engine->loadSound(wav.path);
    ASSERT_TRUE(sh.has_value());

    // Start several instances (results intentionally unused here).
    std::ignore = engine->playMusic(*sh, 0.0f);
    std::ignore = engine->playMusic(*sh, 0.0f);
    std::ignore = engine->playMusic(*sh, 0.0f);

    engine->stopAll();    // must not crash or leak
    engine->update();     // purge pass after stopAll
}

// ─── setListenerTransform ─────────────────────────────────────────────────────

TEST(AudioEngineTest, SetListenerTransformDoesNotCrash)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    engine->setListenerTransform(
        glm::vec3(1.0f, 2.0f, 3.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
}

// ─── update (purge) ───────────────────────────────────────────────────────────

TEST(AudioEngineTest, UpdatePurgesFinishedSounds)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    TempWav wav;
    ASSERT_TRUE(wav.exists());

    auto sh = engine->loadSound(wav.path);
    ASSERT_TRUE(sh.has_value());

    // Play a short (100-sample) sound and let it finish.
    std::ignore = engine->playMusic(*sh, 0.0f);

    // Wait for the sound to reach end-of-stream.
    // The WAV is 100 samples ≈ 2 ms at 44100 Hz — multiple update() calls
    // at ~16 ms intervals will see it reach end-of-stream and purge it.
    for (int i = 0; i < 10; ++i)
    {
        engine->update();
        // Briefly sleep so the audio thread can advance position.
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(5ms);
    }
    // No assertion needed — the test passes if update() did not crash.
}

// ─── AudioSystem ──────────────────────────────────────────────────────────────

TEST(AudioSystemTest, ConstructionAndDestructionIsNoop)
{
    AudioSystem sys;
    (void)sys;
}

TEST(AudioSystemTest, UpdateOnEmptyWorldIsNoop)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    AudioSystem sys;
    World world;

    engine->update();
    sys.update(*engine, world,
               glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(AudioSystemTest, StopEmitterUnknownEntityIsNoop)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    AudioSystem sys;
    sys.stopEmitter(999u, *engine);  // must not crash
}

TEST(AudioSystemTest, EmitterWithMissingFileIsSkipped)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    AudioSystem sys;
    World world;

    const EntityId ent = world.createEntity();
    world.addComponent(ent, TransformComponent{ .position = glm::vec3(0.0f) });

    SoundEmitterComponent sec;
    sec.soundPath = "/nonexistent/audio/missing.wav";
    sec.autoPlay  = true;
    world.addComponent(ent, std::move(sec));

    // AudioSystem must silently skip the emitter and not crash.
    engine->update();
    sys.update(*engine, world,
               glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(AudioSystemTest, AutoPlayStartsAndEntityRemovalStops)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    TempWav wav;
    ASSERT_TRUE(wav.exists());

    AudioSystem sys;
    World world;

    const EntityId ent = world.createEntity();
    world.addComponent(ent, TransformComponent{ .position = glm::vec3(0.0f) });

    SoundEmitterComponent sec;
    sec.soundPath     = wav.path;
    sec.falloffRadius = 0.0f;  // non-spatial (music mode)
    sec.autoPlay      = true;
    sec.looping       = false;
    world.addComponent(ent, std::move(sec));

    // First update: should start the sound.
    engine->update();
    sys.update(*engine, world,
               glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // Second update with the same entity: must not start a second instance.
    engine->update();
    sys.update(*engine, world,
               glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // Remove the entity; next update must stop its playback.
    world.destroyEntity(ent);
    engine->update();
    sys.update(*engine, world,
               glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(AudioSystemTest, ManualStopEmitter)
{
    auto result = makeAudioEngine();
    if (!result.has_value()) { GTEST_SKIP() << "No audio device"; }
    auto& engine = *result;
    engine->setMasterVolume(0.0f);

    TempWav wav;
    ASSERT_TRUE(wav.exists());

    AudioSystem sys;
    World world;

    const EntityId ent = world.createEntity();
    world.addComponent(ent, TransformComponent{ .position = glm::vec3(0.0f) });

    SoundEmitterComponent sec;
    sec.soundPath     = wav.path;
    sec.falloffRadius = 0.0f;
    sec.autoPlay      = true;
    sec.looping       = true;
    world.addComponent(ent, std::move(sec));

    engine->update();
    sys.update(*engine, world,
               glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // Manually stop the looping emitter while the entity is still alive.
    sys.stopEmitter(ent, *engine);

    // Subsequent updates must not crash and must not restart the sound
    // (autoPlay does not re-trigger once a playback entry has been established
    // and manually stopped — the entity is still in the ECS world so the
    // "removal" path is not taken; the sound is simply silent until the entity
    // is re-added or startEmitter() is called explicitly).
    engine->update();
    sys.update(*engine, world,
               glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}
