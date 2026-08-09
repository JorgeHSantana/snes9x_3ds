#include "doctest.h"
#include "3dsmsu.h"
#include "fake_backend.h"
#include "fixtures.h"

using fixtures::put_file;
using fixtures::make_tmpdir;
using fixtures::write_pcm_at;

static int16_t staging[fake::CAP_SAMPLES * 2];

// Expected observable state after firing each event on a fresh, playing bridge.
struct Expectation {
    Msu1Event event;
    bool      muted;          // msu3dsIsMuted()
    bool      queue_cleared;  // fake::cleared > 0
    bool      msu_shutdown;   // MSU1.enabled == false afterwards
};

TEST_CASE("EVERY Msu1Event has a defined, tested outcome")
{
    const Expectation table[] = {
        { Msu1Event::MenuEnter,       true,  false, false },
        { Msu1Event::MenuExit,        false, false, false },
        { Msu1Event::MixerDrain,      false, false, false },
        { Msu1Event::MixerResume,     false, false, false },
        { Msu1Event::TurboOn,         true,  false, false },
        { Msu1Event::TurboOff,        false, false, false },
        { Msu1Event::AptSuspend,      true,  false, false },
        { Msu1Event::AptResume,       false, false, false },
        { Msu1Event::RomUnload,       false, true,  true  },
        { Msu1Event::SavestateLoaded, false, true,  false },
        { Msu1Event::VolumeChanged,   false, false, false },
        { Msu1Event::AppExit,         false, true,  false },
    };
    // THE GUARD: a new enum value without a row here fails immediately.
    static_assert(sizeof(table) / sizeof(table[0]) == (size_t)Msu1Event::Count,
                  "Msu1Event added without a state-mirror matrix row (spec section 6)!");

    for (const Expectation& e : table) {
        CAPTURE((int)e.event);
        fake::reset();
        msu3dsFinalize();
        REQUIRE(msu3dsInitialize(fake::make(), staging, fake::CAP_SAMPLES));
        fake::reset();  // clear counters from initialization
        MSU1 = Msu1State{};
        MSU1.enabled = true;
        MSU1.volume  = 255;
        MSU1.status  = MSU1_REVISION | MSU1_FLAG_AUDIO_PLAYING;

        msu3dsOnEvent(e.event);

        CHECK(msu3dsIsMuted() == e.muted);
        CHECK((fake::cleared > 0) == e.queue_cleared);
        CHECK((MSU1.enabled == false) == e.msu_shutdown);
    }
}

TEST_CASE("drain makes fill queue silence even while playing")
{
    fake::reset();
    msu3dsFinalize();
    REQUIRE(msu3dsInitialize(fake::make(), staging, fake::CAP_SAMPLES));

    /* MSU1 playing via tmpdir fixture (as in Task 7) with non-zero samples */
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

    fake::reset();  // clear counters from initialization

    // CONTROL: Prove PCM is present when drain is OFF
    msu3dsFillAudio();
    CHECK(fake::queued > 0);
    CHECK(fake::last_samples[2] == 1);   // sample idx 1 L: PCM from fixture
    CHECK(fake::last_samples[3] == -1);  // sample idx 1 R: PCM from fixture
    fake::reset();

    // DRAIN: After MixerDrain, fill queue must contain silence, not PCM
    msu3dsOnEvent(Msu1Event::MixerDrain);
    msu3dsFillAudio();
    CHECK(fake::queued > 0);
    CHECK(fake::last_samples[2] == 0);   // sample idx 1 L: silence (not 1)
    CHECK(fake::last_samples[3] == 0);   // sample idx 1 R: silence (not -1)

    msu3dsOnEvent(Msu1Event::MixerResume);
    msu1_shutdown(MSU1);
}

TEST_CASE("AppExit leaves the bridge uninitialized (subsequent calls are no-ops)")
{
    fake::reset();
    msu3dsFinalize();
    REQUIRE(msu3dsInitialize(fake::make(), staging, fake::CAP_SAMPLES));
    fake::reset();  // clear counters from initialization
    msu3dsOnEvent(Msu1Event::AppExit);
    CHECK(fake::shutdowns == 1);
    uint32_t q = fake::queued;
    msu3dsFillAudio();                        // must not queue anything
    CHECK(fake::queued == q);
}
