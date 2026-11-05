#include "doctest.h"
#include "gpu_pressure.h"

TEST_CASE("gpu_pressure: a wait over 20% of the frame budget is pressure") {
    const unsigned long long frame = 4468531;   // 60 Hz in 268 MHz ticks
    CHECK_FALSE(gpu_pressure(0, frame));
    CHECK_FALSE(gpu_pressure(frame / 10, frame));        // 1.7 ms: the normal end-of-frame handshake
    CHECK_FALSE(gpu_pressure(frame / 5, frame));         // exactly 20%: not yet
    CHECK(gpu_pressure(frame / 4, frame));               // 4.2 ms
    CHECK(gpu_pressure(frame / 2, frame));               // 8.3 ms (Jorge's log: up to 9.1)
    CHECK_FALSE(gpu_pressure(frame, 0));                 // no budget known
}
