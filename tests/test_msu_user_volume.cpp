#include "doctest.h"
#include "3dsmsu.h"
#include "fake_backend.h"
#include "fixtures.h"

static int16_t staging[fake::CAP_SAMPLES * 2];

static void fresh_bridge()
{
    fake::reset();
    msu3dsFinalize();
    REQUIRE(msu3dsInitialize(fake::make(), staging, fake::CAP_SAMPLES));
}

TEST_CASE("user volume scales the mix: global * user * game")
{
    fresh_bridge();
    MSU1.volume = 255;
    msu3dsSetGlobalVolume(1.5f);
    msu3dsSetUserVolume(1.0f);
    CHECK(fake::last_mix == doctest::Approx(1.5f));

    msu3dsSetUserVolume(0.5f);
    CHECK(fake::last_mix == doctest::Approx(0.75f));

    msu3dsSetUserVolume(0.0f);
    CHECK(fake::last_mix == doctest::Approx(0.0f));

    MSU1.volume = 128;
    msu3dsSetUserVolume(2.0f);
    CHECK(fake::last_mix == doctest::Approx(1.5f * 2.0f * 128.0f / 255.0f));
}

TEST_CASE("user volume clamps to [0, 2]")
{
    fresh_bridge();
    MSU1.volume = 255;
    msu3dsSetGlobalVolume(1.0f);
    msu3dsSetUserVolume(-1.0f);
    CHECK(fake::last_mix == doctest::Approx(0.0f));
    msu3dsSetUserVolume(5.0f);
    CHECK(fake::last_mix == doctest::Approx(2.0f));
}

TEST_CASE("user volume defaults to 1.0 and ignores calls before init")
{
    fake::reset();
    msu3dsFinalize();
    msu3dsSetUserVolume(0.5f);      // no-op: not initialized
    REQUIRE(msu3dsInitialize(fake::make(), staging, fake::CAP_SAMPLES));
    MSU1.volume = 255;
    msu3dsSetGlobalVolume(1.0f);    // apply_mix runs with default user volume
    CHECK(fake::last_mix == doctest::Approx(1.0f));
}
