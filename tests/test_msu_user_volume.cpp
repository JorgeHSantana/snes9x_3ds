#include "doctest.h"
#include "3dsmsu.h"
#include "fake_backend.h"

static int16_t staging[fake::CAP_SAMPLES * 2];

static void fresh()
{
    fake::reset();
    msu3dsFinalize();
    REQUIRE(msu3dsInitialize(fake::make(), staging, fake::CAP_SAMPLES));
    MSU1 = Msu1State{};
    MSU1.enabled = true;
    MSU1.volume = 255;
    msu3dsSetGlobalVolume(1.0f);
}

TEST_CASE("user volume defaults to neutral 1.0")
{
    fresh();
    msu3dsOnEvent(Msu1Event::VolumeChanged);
    CHECK(fake::last_mix == doctest::Approx(1.0f));
}

TEST_CASE("user volume multiplies into the mix and clamps")
{
    fresh();
    msu3dsSetUserVolume(1.5f);
    CHECK(fake::last_mix == doctest::Approx(1.5f));
    msu3dsSetUserVolume(99.0f);
    CHECK(fake::last_mix == doctest::Approx(2.0f));
    msu3dsSetGlobalVolume(1.5f);
    CHECK(fake::last_mix == doctest::Approx(3.0f));
}

TEST_CASE("mute still wins over user volume")
{
    fresh();
    msu3dsSetUserVolume(2.0f);
    msu3dsOnEvent(Msu1Event::MenuEnter);
    CHECK(fake::last_mix == doctest::Approx(0.0f));
    msu3dsOnEvent(Msu1Event::MenuExit);
    CHECK(fake::last_mix == doctest::Approx(2.0f));
}

TEST_CASE("uninitialized bridge: safe no-op")
{
    msu3dsFinalize();
    msu3dsSetUserVolume(1.5f);
}
