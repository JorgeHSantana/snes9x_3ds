#include "doctest.h"
#include "3dsmsu.h"
#include "fake_backend.h"
#include "fixtures.h"
#include <cstring>

using fixtures::put_file;
using fixtures::make_tmpdir;
using fixtures::write_pcm_at;

static int16_t staging[fake::CAP_SAMPLES * 2];

static void fresh_bridge()
{
    fake::reset();
    msu3dsFinalize();
    REQUIRE(msu3dsInitialize(fake::make(), staging, fake::CAP_SAMPLES));
}

TEST_CASE("initialize validates backend and staging")
{
    Msu1AudioBackend b = fake::make();
    b.queue_buffer = nullptr;
    CHECK_FALSE(msu3dsInitialize(b, staging, fake::CAP_SAMPLES));
    CHECK_FALSE(msu3dsInitialize(fake::make(), nullptr, fake::CAP_SAMPLES));
    CHECK_FALSE(msu3dsInitialize(fake::make(), staging, 1));   // < capacity
}

TEST_CASE("fill: nothing when disabled, silence when idle, PCM when playing")
{
    fresh_bridge();
    MSU1 = Msu1State{};              // disabled: zero-cost path queues NOTHING
    msu3dsFillAudio();
    CHECK(fake::queued == 0);

    // enabled path: build a real temp-dir MSU1 state (as in Task 6 tests)
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    // Create Game-1.pcm: loop_point=0, 16 samples (64 data bytes)
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 16));

    MSU1 = Msu1State{};
    REQUIRE(msu1_init(MSU1, rom.c_str()) == Msu1Result::Ok);

    // enabled but not playing: silence keeps the channel fed
    fake::reset();
    msu3dsFillAudio();
    CHECK(fake::queued == fake::TOTAL_BUFS);
    CHECK(fake::last_samples[0] == 0);

    // Load track 1 (port 4 = low byte, port 5 = high byte + triggers load)
    msu1_write_port(MSU1, 4, 1);
    msu1_write_port(MSU1, 5, 0);

    // Play with repeat (0x03 = repeat + play)
    msu1_write_port(MSU1, 7, 0x03);

    fake::reset();
    msu3dsFillAudio();
    CHECK(fake::queued > 0);
    CHECK(fake::last_samples[0] == 0);   // frame 0: L == 0 per fixture
    CHECK(fake::last_samples[2] == 1);   // frame 1: L == 1 (discriminates PCM from silence)
    CHECK(fake::last_samples[3] == -1);  // frame 1: R == -1 per fixture

    msu1_shutdown(MSU1);
}

TEST_CASE("menu mute freezes the track position; exit resumes exactly there")
{
    fresh_bridge();

    // 512-sample track (frame i => L=i, R=-i) so positions are discriminable
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 512));

    MSU1 = Msu1State{};
    REQUIRE(msu1_init(MSU1, rom.c_str()) == Msu1Result::Ok);
    msu1_write_port(MSU1, 4, 1);
    msu1_write_port(MSU1, 5, 0);
    msu1_write_port(MSU1, 7, 0x02);      // play, no repeat

    // one buffer of PCM: frames 0..63 consumed
    fake::reset();
    fake::free_bufs = 1;
    msu3dsFillAudio();
    CHECK(fake::queued == 1);
    CHECK(fake::last_samples[2] == 1);
    CHECK(fake::last_samples[3] == -1);
    uint32_t pos_before = MSU1.audio_play_pos;
    CHECK(pos_before == 64 * MSU1_BYTES_PER_SAMPLE);

    // menu mute: channel stays fed with silence, PCM NOT consumed
    msu3dsOnEvent(Msu1Event::MenuEnter);
    fake::reset();
    msu3dsFillAudio();
    CHECK(fake::queued == fake::TOTAL_BUFS);
    CHECK(fake::last_samples[0] == 0);
    CHECK(fake::last_samples[1] == 0);
    CHECK(fake::last_samples[2] == 0);
    CHECK(fake::last_samples[3] == 0);
    CHECK(MSU1.audio_play_pos == pos_before);      // position frozen

    // menu exit: playback continues from the frozen position (frame 64)
    msu3dsOnEvent(Msu1Event::MenuExit);
    fake::reset();
    fake::free_bufs = 1;
    msu3dsFillAudio();
    CHECK(fake::queued == 1);
    CHECK(fake::last_samples[0] == 64);            // frame 64: L == 64
    CHECK(fake::last_samples[1] == -64);           // frame 64: R == -64

    msu1_shutdown(MSU1);
}

TEST_CASE("turbo mute keeps consuming PCM (mute-only, position drifts by design)")
{
    fresh_bridge();

    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 512));

    MSU1 = Msu1State{};
    REQUIRE(msu1_init(MSU1, rom.c_str()) == Msu1Result::Ok);
    msu1_write_port(MSU1, 4, 1);
    msu1_write_port(MSU1, 5, 0);
    msu1_write_port(MSU1, 7, 0x02);

    msu3dsOnEvent(Msu1Event::TurboOn);
    CHECK(msu3dsIsMuted());
    fake::reset();
    fake::free_bufs = 1;
    msu3dsFillAudio();
    CHECK(fake::queued == 1);
    CHECK(MSU1.audio_play_pos == 64 * MSU1_BYTES_PER_SAMPLE);   // still consumed

    msu3dsOnEvent(Msu1Event::TurboOff);
    msu1_shutdown(MSU1);
}

TEST_CASE("mix formula: global * game volume, zero when muted")
{
    fresh_bridge();
    MSU1.volume = 255;
    msu3dsSetGlobalVolume(1.0f);
    msu3dsOnEvent(Msu1Event::VolumeChanged);
    CHECK(fake::last_mix == doctest::Approx(1.0f));
    MSU1.volume = 128;
    msu3dsOnEvent(Msu1Event::VolumeChanged);
    CHECK(fake::last_mix == doctest::Approx(128.0f / 255.0f));
    msu3dsOnEvent(Msu1Event::MenuEnter);
    CHECK(fake::last_mix == doctest::Approx(0.0f));
    CHECK(msu3dsIsMuted());
    msu3dsOnEvent(Msu1Event::MenuExit);
    CHECK(fake::last_mix == doctest::Approx(128.0f / 255.0f));
}

TEST_CASE("underrun: not counted before the first PCM queue, counted once fed queue empties")
{
    // Build a real temp-dir MSU1 state for the playing fixture
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    // Create Game-1.pcm: loop_point=0, 16 samples
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 16));

    MSU1 = Msu1State{};
    REQUIRE(msu1_init(MSU1, rom.c_str()) == Msu1Result::Ok);

    // Load and play track 1 (repeat keeps PCM flowing for every fill)
    msu1_write_port(MSU1, 4, 1);
    msu1_write_port(MSU1, 5, 0);
    msu1_write_port(MSU1, 7, 0x03);

    fresh_bridge();
    // first fill after play start: queue is empty but nothing was ever
    // queued — an empty queue here is normal startup, NOT an underrun
    msu3dsFillAudio();
    CHECK(msu3dsGetUnderrunCount() == 0);

    // PCM has been queued since; a fully empty queue now IS an underrun
    fake::free_bufs = fake::TOTAL_BUFS;        // simulate the channel starving
    msu3dsFillAudio();
    CHECK(msu3dsGetUnderrunCount() == 1);

    msu1_shutdown(MSU1);
}

TEST_CASE("RomUnload resets the underrun count")
{
    // Same fixture/starvation setup as the "underrun: not counted..." test
    // above, but this time we unload the ROM and confirm the counter — a
    // debug/UI signal for THIS session — doesn't leak into the next game.
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "", 0);

    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 16));

    MSU1 = Msu1State{};
    REQUIRE(msu1_init(MSU1, rom.c_str()) == Msu1Result::Ok);

    msu1_write_port(MSU1, 4, 1);
    msu1_write_port(MSU1, 5, 0);
    msu1_write_port(MSU1, 7, 0x03);

    fresh_bridge();
    msu3dsFillAudio();                     // establishes queued_since_clear
    fake::free_bufs = fake::TOTAL_BUFS;    // simulate the channel starving
    msu3dsFillAudio();
    REQUIRE(msu3dsGetUnderrunCount() == 1);

    // RomUnload runs inside the caller's drain window (mixer-safe); it must
    // zero the counter so a stutter warning from a previous game never
    // shows up on the next one.
    msu3dsOnEvent(Msu1Event::RomUnload);
    CHECK(msu3dsGetUnderrunCount() == 0);
}

TEST_CASE("uninitialized bridge: all entry points are safe no-ops")
{
    msu3dsFinalize();
    msu3dsFillAudio();
    msu3dsOnEvent(Msu1Event::MenuEnter);
    msu3dsSetGlobalVolume(1.5f);
    CHECK(msu3dsGetUnderrunCount() == 0);
    CHECK_FALSE(msu3dsIsMuted());
}
