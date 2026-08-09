#include "doctest.h"
#include "3dsmsu.h"

// Truth table (task-3-brief.md):
//   setting_enabled | chip_active | action
//   true            | true        | None      (already on, nothing to do)
//   true            | false       | Detect    (user turned it on, chip not yet loaded)
//   false           | true        | TearDown  (user turned it off, chip is loaded)
//   false           | false       | None      (already off, nothing to do)

TEST_CASE("msu3dsDecideEnableAction: enabled + active -> None")
{
    CHECK(msu3dsDecideEnableAction(true, true) == Msu1EnableAction::None);
}

TEST_CASE("msu3dsDecideEnableAction: enabled + inactive -> Detect")
{
    CHECK(msu3dsDecideEnableAction(true, false) == Msu1EnableAction::Detect);
}

TEST_CASE("msu3dsDecideEnableAction: disabled + active -> TearDown")
{
    CHECK(msu3dsDecideEnableAction(false, true) == Msu1EnableAction::TearDown);
}

TEST_CASE("msu3dsDecideEnableAction: disabled + inactive -> None")
{
    CHECK(msu3dsDecideEnableAction(false, false) == Msu1EnableAction::None);
}
