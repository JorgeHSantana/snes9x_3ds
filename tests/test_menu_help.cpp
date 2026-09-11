#include "doctest.h"
#include <fstream>
#include <iterator>
#include <string>

// Menu construction depends on libctru. Guard its declarative help bindings
// on the host; interactive SELECT/layout checks are performed in Azahar.
TEST_CASE("option explanations live in SELECT help, not extra menu rows")
{
    std::ifstream file("../source/3dsmain.cpp");
    REQUIRE(file.is_open());
    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    const char* labels[] = {
        "  Auto Save / Auto Load",
        "  Create screenshot when saving",
        "  Enable Logging (use when issues occur)",
        "  Automatically save state on exit, load state on start",
        "  Force SRAM Write on Pause",
        "  Bind Circle Pad to D-Pad"
    };
    for (const char* label : labels) {
        INFO(label);
        const size_t start = source.find(label);
        REQUIRE(start != std::string::npos);
        const size_t help = source.find("items.back().PickerDescription =", start);
        REQUIRE(help != std::string::npos);
        CHECK(help < source.find("AddMenu", start));
        CHECK(help < source.find("MenuItemType::Textarea", start));
    }
    const size_t layer_start = source.find("for (int bg = LAYER_BG0; bg <= LAYER_BG2; bg++)");
    REQUIRE(layer_start != std::string::npos);
    const size_t layer_help = source.find("items.back().PickerDescription =", layer_start);
    REQUIRE(layer_help != std::string::npos);
    CHECK(layer_help < source.find("AddMenuDisabledOption", layer_start));
    CHECK(source.find("AddMenuDisabledOption(items, logfileInfo)") == std::string::npos);

    // Only build status and the empty-cheats message remain as text areas.
    size_t rows = 0;
    size_t pos = 0;
    while ((pos = source.find("MenuItemType::Textarea,", pos)) != std::string::npos) {
        ++rows;
        ++pos;
    }
    CHECK(rows == 2);
}
