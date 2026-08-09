#include "doctest.h"
#include "msu1.h"
#include "fixtures.h"
#include <string>
using fixtures::put_file;
using fixtures::make_tmpdir;
using fixtures::write_pcm_at;

namespace {
int  lock_calls;
int  unlock_calls;
bool lock_held;               // true between fake_lock and fake_unlock
bool unlock_saw_held;         // fake_unlock observed the lock as held
bool cb_saw_lock_held;        // volume callback observed the lock as held

void fake_lock(void)   { lock_calls++; lock_held = true; }
void fake_unlock(void) { unlock_saw_held = lock_held; unlock_calls++; lock_held = false; }

void reset_hook_counters(void)
{
    lock_calls = unlock_calls = 0;
    lock_held = unlock_saw_held = cb_saw_lock_held = false;
}

// Builds the GLOBAL MSU1 state (S9xMSU1* wrappers operate on it) from a tmpdir.
std::string setup_global_msu1(void)
{
    std::string dir = make_tmpdir();
    REQUIRE_FALSE(dir.empty());
    std::string rom = put_file(dir, "Game.sfc", "", 0);
    REQUIRE_FALSE(rom.empty());
    put_file(dir, "Game.msu", "ABCD", 4);
    std::string pcm1 = dir + "/Game-1.pcm";
    REQUIRE(write_pcm_at(pcm1.c_str(), 0, 16));

    S9xMSU1Shutdown();
    MSU1 = Msu1State{};
    REQUIRE(msu1_init(MSU1, rom.c_str()) == Msu1Result::Ok);
    return rom;
}

void teardown_global_msu1(void)
{
    msu1_set_lock_hooks(nullptr, nullptr);
    S9xMSU1Shutdown();
}
} // namespace

TEST_CASE("S9xMSU1WritePort takes lock hooks around a track load; latch bytes stay lock-free")
{
    setup_global_msu1();
    msu1_set_lock_hooks(fake_lock, fake_unlock);
    reset_hook_counters();

    // port 4 = latch low byte (emu-thread-only state): no lock
    // port 5 = latch high byte + track load (fclose/fopen races the mixer): lock
    S9xMSU1WritePort(4, 1);
    S9xMSU1WritePort(5, 0);

    CHECK(lock_calls == 1);
    CHECK(unlock_calls == 1);
    CHECK(unlock_saw_held);            // unlock came while the lock was held: balanced pair
    CHECK_FALSE(lock_held);            // released after the call
    CHECK(MSU1.current_track == 1);    // the load actually happened
    CHECK(MSU1.audio_file != nullptr);

    teardown_global_msu1();
}

TEST_CASE("lock is held for the duration of a hooked write (observed from inside)")
{
    setup_global_msu1();
    msu1_set_lock_hooks(fake_lock, fake_unlock);
    reset_hook_counters();

    // the volume callback fires inside msu1_write_port, i.e. inside the hooked
    // region of S9xMSU1WritePort — it must observe the lock as held
    MSU1.volume_changed_cb = [] { cb_saw_lock_held = lock_held; };
    S9xMSU1WritePort(6, 200);

    CHECK(lock_calls == 1);
    CHECK(unlock_calls == 1);
    CHECK(cb_saw_lock_held);
    CHECK(MSU1.volume == 200);

    MSU1.volume_changed_cb = nullptr;
    teardown_global_msu1();
}

TEST_CASE("msu1_write_port (unwrapped) never touches the lock hooks")
{
    setup_global_msu1();
    msu1_set_lock_hooks(fake_lock, fake_unlock);
    reset_hook_counters();

    msu1_write_port(MSU1, 4, 1);
    msu1_write_port(MSU1, 5, 0);
    msu1_write_port(MSU1, 3, 0);
    msu1_write_port(MSU1, 7, 0x02);

    CHECK(lock_calls == 0);
    CHECK(unlock_calls == 0);

    teardown_global_msu1();
}

TEST_CASE("null hooks: wrapped writes and restore are safe no-ops on the lock")
{
    setup_global_msu1();
    msu1_set_lock_hooks(nullptr, nullptr);

    S9xMSU1WritePort(4, 1);
    S9xMSU1WritePort(5, 0);            // no crash with no hooks installed
    S9xMSU1WritePort(7, 0x02);
    CHECK(MSU1.current_track == 1);

    Msu1Snapshot snap = {};
    msu1_capture(MSU1, snap);
    CHECK(msu1_restore(MSU1, snap) == Msu1Result::Ok);   // no crash either

    teardown_global_msu1();
}

TEST_CASE("msu1_restore takes the lock hooks around the whole restore")
{
    setup_global_msu1();

    S9xMSU1WritePort(4, 1);
    S9xMSU1WritePort(5, 0);
    S9xMSU1WritePort(7, 0x02);
    Msu1Snapshot snap = {};
    msu1_capture(MSU1, snap);

    msu1_set_lock_hooks(fake_lock, fake_unlock);
    reset_hook_counters();

    CHECK(msu1_restore(MSU1, snap) == Msu1Result::Ok);
    CHECK(lock_calls == 1);
    CHECK(unlock_calls == 1);
    CHECK(unlock_saw_held);
    CHECK_FALSE(lock_held);

    teardown_global_msu1();
}
