// tests/test_main.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

TEST_CASE("smoke: doctest runs")
{
    CHECK(1 + 1 == 2);
}
