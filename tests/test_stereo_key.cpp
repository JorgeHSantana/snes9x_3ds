#include "doctest.h"
#include "../source/3dsstereokey.h"

TEST_CASE("stereo key: plain title passes through") {
    char k[32];
    CHECK(stereo3dKeyFromRomName("MEGAMAN X3", k, sizeof(k)) == 10);
    CHECK(std::string(k) == "MEGAMAN X3");
}

TEST_CASE("stereo key: surrounding spaces are trimmed") {
    char k[32];
    CHECK(stereo3dKeyFromRomName("   ZELDA  ", k, sizeof(k)) == 5);
    CHECK(std::string(k) == "ZELDA");
}

TEST_CASE("stereo key: filesystem-hostile characters become underscores") {
    char k[32];
    stereo3dKeyFromRomName("A/B:C*D?E\"F<G>H|I\\J", k, sizeof(k));
    CHECK(std::string(k) == "A_B_C_D_E_F_G_H_I_J");
}

TEST_CASE("stereo key: control and non-ASCII bytes become underscores") {
    char k[32];
    stereo3dKeyFromRomName("AB\x01" "CD\xE9" "EF", k, sizeof(k));
    CHECK(std::string(k) == "AB_CD_EF");
}

TEST_CASE("stereo key: a title with nothing to key on yields 0") {
    char k[32];
    CHECK(stereo3dKeyFromRomName("", k, sizeof(k)) == 0);
    CHECK(k[0] == '\0');
    CHECK(stereo3dKeyFromRomName("   ", k, sizeof(k)) == 0);
    CHECK(stereo3dKeyFromRomName("\x01\x02***", k, sizeof(k)) == 0);
    CHECK(stereo3dKeyFromRomName(nullptr, k, sizeof(k)) == 0);
}

TEST_CASE("stereo key: output is truncated to the buffer") {
    char k[5];
    CHECK(stereo3dKeyFromRomName("SUPER MARIO WORLD", k, sizeof(k)) == 4);
    CHECK(std::string(k) == "SUPE");
    CHECK(stereo3dKeyFromRomName("X", nullptr, 0) == 0);
}
