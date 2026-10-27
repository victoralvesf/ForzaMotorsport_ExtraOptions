#include "doctest.h"

#include "registry.hpp"

#include <cstring>
#include <cwchar>

TEST_CASE("games: FM5 and FM6 are found by TimeDateStamp + SizeOfImage")
{
    const game::Module *fm5 = games::find(0x53d8d1f2, 0x279d000);
    REQUIRE(fm5);
    CHECK(strcmp(fm5->tag, "FM5") == 0);
    CHECK(wcscmp(fm5->ini_file, L"azimuth_fm5.ini") == 0);

    const game::Module *fm6 = games::find(0x58afb1cd, 0x1fc8000);
    REQUIRE(fm6);
    CHECK(strcmp(fm6->tag, "FM6") == 0);
    CHECK(wcscmp(fm6->ini_file, L"azimuth_fm6.ini") == 0);
}

TEST_CASE("games: only both fields together identify a game")
{
    CHECK(games::find(0x53d8d1f2, 0x1fc8000) == nullptr);
    CHECK(games::find(0x58afb1cd, 0x279d000) == nullptr);
    CHECK(games::find(0, 0) == nullptr);
}

TEST_CASE("games: every module is complete and unique")
{
    auto all = games::all();
    CHECK(all.size() == 2);
    for (size_t i = 0; i < all.size(); i++) {
        const game::Module *g = all[i];
        CHECK(g->tag);
        CHECK(g->ini_file);
        CHECK(g->apply);
        for (size_t j = i + 1; j < all.size(); j++) {
            CHECK(strcmp(g->tag, all[j]->tag) != 0);
            CHECK(!(g->time_date_stamp == all[j]->time_date_stamp && g->size_of_image == all[j]->size_of_image));
        }
    }
}
