// Host-side tests for the rewind v2 input tape and replay-speed meter
// (degrau 2 of docs/rewind-v2-spec.md: recording only, nothing replays yet).

#include "doctest.h"

#include "3dsrewindtape.h"
#include "3dsrewindmeter.h"

namespace {

struct TapeFixture
{
    static constexpr int FRAMES = 8;
    static constexpr int EVENTS = 4;
    uint32_t pads[FRAMES];
    RewindTape::MsuRead msu[EVENTS];
    RewindTape tape;

    TapeFixture()
    {
        tape.init(pads, FRAMES, msu, EVENTS);
        tape.clear(1);   // production convention: first executed frame is 1
    }
};

} // namespace

TEST_CASE("tape: push and read back absolute frames")
{
    TapeFixture f;
    CHECK(f.tape.valid());
    CHECK(f.tape.nextFrame() == 1);

    for (uint32_t i = 1; i <= 5; i++)
        f.tape.push(0x100 + i);

    CHECK(f.tape.count == 5);
    uint32_t pad = 0;
    CHECK(f.tape.pad_at(1, &pad)); CHECK(pad == 0x101);
    CHECK(f.tape.pad_at(5, &pad)); CHECK(pad == 0x105);
    CHECK_FALSE(f.tape.pad_at(0, &pad));
    CHECK_FALSE(f.tape.pad_at(6, &pad));
}

TEST_CASE("tape: window slides when full and validFrom follows")
{
    TapeFixture f;
    for (uint32_t i = 1; i <= 11; i++)     // capacity 8: frames 1..3 fall off
        f.tape.push(0x200 + i);

    CHECK(f.tape.count == 8);
    CHECK(f.tape.baseFrame == 4);
    CHECK(f.tape.validFrom == 4);

    uint32_t pad = 0;
    CHECK_FALSE(f.tape.pad_at(3, &pad));
    CHECK(f.tape.pad_at(4, &pad));  CHECK(pad == 0x204);
    CHECK(f.tape.pad_at(11, &pad)); CHECK(pad == 0x20b);
}

TEST_CASE("tape: msu events tag the frame being executed")
{
    TapeFixture f;
    f.tape.push(0xA);              // frame 1
    f.tape.note_msu(0, 0x10);      // during frame 2
    f.tape.note_msu(0, 0x30);      // still frame 2
    f.tape.push(0xB);              // commits frame 2

    REQUIRE(f.tape.msu_event_count() == 2);
    CHECK(f.tape.msu_event(0).frame == 2);
    CHECK(f.tape.msu_event(0).value == 0x10);
    CHECK(f.tape.msu_event(1).frame == 2);
    CHECK(f.tape.msu_event(1).value == 0x30);
}

TEST_CASE("tape: sliding window drops msu events with their frames")
{
    TapeFixture f;
    f.tape.note_msu(0, 0x11);              // frame 1
    for (uint32_t i = 1; i <= 11; i++)     // slides base to 4
        f.tape.push(i);

    CHECK(f.tape.msu_event_count() == 0);  // frame-1 event left with frame 1
}

TEST_CASE("tape: msu ring overflow poisons older frames via validFrom")
{
    TapeFixture f;
    for (int i = 0; i < TapeFixture::EVENTS; i++) {
        f.tape.note_msu(0, (uint8_t)i);    // all during frame 1
        f.tape.push(0);                    // frames 1..4
    }
    CHECK(f.tape.validFrom == 1);

    f.tape.note_msu(0, 0xFF);              // overflow: drops the frame-1 event
    CHECK(f.tape.msu_event_count() == TapeFixture::EVENTS);
    CHECK(f.tape.validFrom == 2);          // frame 1 no longer replayable
}

TEST_CASE("tape: truncate_to makes the target the newest frame")
{
    TapeFixture f;
    for (uint32_t i = 1; i <= 6; i++) {
        if (i >= 3)
            f.tape.note_msu(0, (uint8_t)i);   // events on frames 3..6
        f.tape.push(i);
    }

    f.tape.truncate_to(4);
    CHECK(f.tape.nextFrame() == 5);
    uint32_t pad = 0;
    CHECK(f.tape.pad_at(4, &pad)); CHECK(pad == 4);
    CHECK_FALSE(f.tape.pad_at(5, &pad));
    REQUIRE(f.tape.msu_event_count() == 2);   // frames 3 and 4 survive
    CHECK(f.tape.msu_event(0).frame == 3);
    CHECK(f.tape.msu_event(1).frame == 4);

    // truncating to a frame older than the window restarts right after it
    f.tape.truncate_to(0);
    CHECK(f.tape.count == 0);
    CHECK(f.tape.nextFrame() == 1);
    CHECK(f.tape.msu_event_count() == 0);
}

TEST_CASE("tape: clear restarts numbering")
{
    TapeFixture f;
    f.tape.push(1); f.tape.note_msu(0, 1);
    f.tape.clear(1);
    CHECK(f.tape.count == 0);
    CHECK(f.tape.msu_event_count() == 0);
    CHECK(f.tape.nextFrame() == 1);
    CHECK(f.tape.validFrom == 1);
}

TEST_CASE("meter: EMA over samples, guards on zero")
{
    RewindMeter m;
    m.reset();
    CHECK_FALSE(m.measured());

    m.note(0, 100, 10);        // zero frames: ignored
    m.note(100, 0, 10);        // zero wall: ignored
    m.note(100, 100, 0);       // zero tick base: ignored
    CHECK_FALSE(m.measured());

    // 600 frames in the wall time of 240 frames -> 2.5x realtime
    m.note(600, 240 * 100, 100);
    CHECK(m.measured());
    CHECK(m.average() == doctest::Approx(2.5f));

    // next sample at 1.5x moves the average a quarter of the way
    m.note(600, 400 * 100, 100);
    CHECK(m.average() == doctest::Approx(2.5f + 0.25f * (1.5f - 2.5f)));
}
