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

TEST_CASE("fill queues silence when not playing, PCM when playing")
{
    fresh_bridge();
    MSU1 = Msu1State{};              // idle
    msu3dsFillAudio();
    CHECK(fake::queued == fake::TOTAL_BUFS);   // silence keeps the queue fed
    CHECK(fake::last_samples[0] == 0);

    // playing path: build a real temp-dir MSU1 state (as in Task 6 tests)
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

TEST_CASE("underrun counted when playing with an empty queue")
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

    // Load and play track 1
    msu1_write_port(MSU1, 4, 1);
    msu1_write_port(MSU1, 5, 0);
    msu1_write_port(MSU1, 7, 0x03);

    fresh_bridge();
    fake::free_bufs = fake::TOTAL_BUFS;        // queue fully drained
    uint32_t before = msu3dsGetUnderrunCount();
    msu3dsFillAudio();
    CHECK(msu3dsGetUnderrunCount() == before + 1);

    msu1_shutdown(MSU1);
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
