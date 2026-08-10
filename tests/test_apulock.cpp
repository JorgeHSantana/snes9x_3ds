#include "doctest.h"
#include "apulock.h"

namespace {
int lock_calls = 0;
int unlock_calls = 0;
void fake_lock(void) { lock_calls++; }
void fake_unlock(void) { unlock_calls++; }
void reset_counters(void)
{
    lock_calls = 0;
    unlock_calls = 0;
    apulock_set_hooks(nullptr, nullptr);
}
} // namespace

TEST_CASE("null hooks: lock/unlock are safe no-ops")
{
    reset_counters();
    apulock_lock();
    apulock_unlock();
    CHECK(lock_calls == 0);
    CHECK(unlock_calls == 0);
}

TEST_CASE("installed hooks are invoked and stay balanced")
{
    reset_counters();
    apulock_set_hooks(fake_lock, fake_unlock);
    apulock_lock();
    apulock_unlock();
    apulock_lock();
    apulock_unlock();
    CHECK(lock_calls == 2);
    CHECK(unlock_calls == 2);
    apulock_set_hooks(nullptr, nullptr);   // uninstall works
    apulock_lock();
    CHECK(lock_calls == 2);
}

TEST_CASE("unbalanced installs are refused")
{
    reset_counters();
    apulock_set_hooks(fake_lock, nullptr);   // refused
    apulock_lock();
    apulock_unlock();
    CHECK(lock_calls == 0);
    CHECK(unlock_calls == 0);
    apulock_set_hooks(nullptr, fake_unlock); // refused
    apulock_unlock();
    CHECK(unlock_calls == 0);
}
