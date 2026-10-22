//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name test_wc2ai_ferry.cpp - A scripted computer's transports: islands, landing walk, claims. */
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//

#include <doctest.h>

#include "../../src/ai/wc2ai_ferry.h"

#include <string>
#include <vector>

namespace
{

/// A map from rows of text: '~' water, 'c' coast (land for a land unit), anything else land.
struct TextMap {
	int width = 0, height = 0;
	std::vector<uint8_t> water, coast;

	explicit TextMap(const std::vector<std::string> &rows)
	{
		height = static_cast<int>(rows.size());
		width = static_cast<int>(rows.front().size());
		for (const std::string &row : rows) {
			for (char c : row) {
				water.push_back(c == '~' ? 1 : 0);
				coast.push_back(c == 'c' ? 1 : 0);
			}
		}
	}
};

} // namespace

TEST_CASE("ferry islands: water splits the land, a diagonal touch joins it")
{
	const TextMap map({
		"..~~..",
		"..~~..",
		"~~~~~~",
		"~~~.~~",
		"~~~~.~",
	});
	const std::vector<int> regions = wc2ai::LandRegions(map.width, map.height, map.water);
	REQUIRE(regions.size() == 30);
	const int west = wc2ai::RegionOf(regions, map.width, map.height, 0, 0);
	const int east = wc2ai::RegionOf(regions, map.width, map.height, 5, 0);
	const int south = wc2ai::RegionOf(regions, map.width, map.height, 3, 3);
	CHECK(west == 1);
	CHECK(east == 2);
	CHECK(south == 3);
	// (4,4) touches (3,3) only at a corner: the same island
	CHECK(wc2ai::RegionOf(regions, map.width, map.height, 4, 4) == south);
	CHECK(wc2ai::RegionOf(regions, map.width, map.height, 2, 0) == 0); // water
	CHECK(wc2ai::RegionOf(regions, map.width, map.height, -1, 0) == 0); // off the map
}

TEST_CASE("ferry islands: a map that does not fit gives nothing")
{
	CHECK(wc2ai::LandRegions(3, 3, std::vector<uint8_t>(8, 0)).empty());
	CHECK(wc2ai::LandRegions(0, 3, {}).empty());
}

TEST_CASE("ferry landing walk: first coast tile of the place's island in the drawn direction")
{
	const TextMap map({
		"~~~~~~~",
		"~ccccc~",
		"~c...c~",
		"~c...c~",
		"~c...c~",
		"~ccccc~",
		"~~~~~~~",
	});
	const std::vector<int> regions = wc2ai::LandRegions(map.width, map.height, map.water);
	// from the middle (3,3): north reaches (3,1), east (5,3), south-west (1,5)
	wc2ai::Place north = wc2ai::LandingCoast(map.width, map.height, regions, map.coast, 3, 3, 0);
	CHECK(north.found);
	CHECK(north.x == 3);
	CHECK(north.y == 1);
	wc2ai::Place east = wc2ai::LandingCoast(map.width, map.height, regions, map.coast, 3, 3, 2);
	CHECK(east.x == 5);
	CHECK(east.y == 3);
	wc2ai::Place southWest = wc2ai::LandingCoast(map.width, map.height, regions, map.coast, 3, 3, 5);
	CHECK(southWest.x == 1);
	CHECK(southWest.y == 5);
	// only the low three bits of the drawn number count
	wc2ai::Place again = wc2ai::LandingCoast(map.width, map.height, regions, map.coast, 3, 3, 8 + 2);
	CHECK(again.x == 5);
}

TEST_CASE("ferry landing walk: off the map tries the next direction; another island's coast is passed")
{
	const TextMap map({
		"c.~~cc",
		"..~~..",
		"..~~..",
	});
	const std::vector<int> regions = wc2ai::LandRegions(map.width, map.height, map.water);
	// from (1,1) north: (1,0) is land, not coast, then off the map; north-east: (2,0) water, off;
	// east: (2,1), (3,1) water, (4,1), (5,1) land of the other island, off; south-east: (2,2) off;
	// south: off; south-west: off; west: (0,1) land, off; north-west: (0,0) coast of its own island
	wc2ai::Place place = wc2ai::LandingCoast(map.width, map.height, regions, map.coast, 1, 1, 0);
	CHECK(place.found);
	CHECK(place.x == 0);
	CHECK(place.y == 0);
	// from (5,1) north: (5,0) is its own island's coast
	place = wc2ai::LandingCoast(map.width, map.height, regions, map.coast, 5, 1, 0);
	CHECK(place.x == 5);
	CHECK(place.y == 0);
}

TEST_CASE("ferry landing walk: no coast of the island in any direction keeps the place")
{
	const TextMap map({
		"...",
		"...",
		"...",
	});
	const std::vector<int> regions = wc2ai::LandRegions(map.width, map.height, map.water);
	CHECK_FALSE(wc2ai::LandingCoast(map.width, map.height, regions, map.coast, 1, 1, 3).found);
	// a place on water has no island
	const TextMap sea({"~~", "~~"});
	const std::vector<int> none = wc2ai::LandRegions(sea.width, sea.height, sea.water);
	CHECK_FALSE(wc2ai::LandingCoast(sea.width, sea.height, none, sea.coast, 0, 0, 0).found);
}

TEST_CASE("ferry claims: one island per load, at most the load, held units passed over")
{
	std::vector<wc2ai::FerryCandidate> units(9);
	units[0] = {3, true};  // another transport has it
	units[1] = {2, false}; // first taken: the load goes to island 2
	units[2] = {4, false}; // other island: left alone
	units[3] = {2, false};
	units[4] = {0, false}; // aboard or gone
	units[5] = {2, false};
	units[6] = {2, false};
	units[7] = {2, false};
	units[8] = {2, false};
	const std::vector<size_t> taken = wc2ai::FerryClaims(units, wc2ai::FerryLoad);
	const std::vector<size_t> six{1, 3, 5, 6, 7, 8};
	CHECK(taken == six);
	const std::vector<size_t> two{1, 3};
	CHECK(wc2ai::FerryClaims(units, 2) == two);
	CHECK(wc2ai::FerryClaims({}, wc2ai::FerryLoad).empty());
	CHECK(wc2ai::FerryClaims(units, 0).empty());
}

TEST_CASE("ferry step: call aboard before sailing, sail before taking, home last")
{
	CHECK(wc2ai::FerryNext(true, true, true) == wc2ai::FerryStep::CallAboard);
	CHECK(wc2ai::FerryNext(false, true, true) == wc2ai::FerryStep::Sail);
	CHECK(wc2ai::FerryNext(false, false, true) == wc2ai::FerryStep::Take);
	CHECK(wc2ai::FerryNext(false, false, false) == wc2ai::FerryStep::Home);
}

TEST_CASE("ferry pick (run 57): the transport nearest the waiting units, the first of equals")
{
	CHECK(wc2ai::FerryPickNearest({}) == -1);
	CHECK(wc2ai::FerryPickNearest({33, 13}) == 1); // Human XII play 1: the map-placed one at 13
	CHECK(wc2ai::FerryPickNearest({11, 13}) == 0); // play 2: the trained one at 11
	CHECK(wc2ai::FerryPickNearest({7, 7, 9}) == 0);
}

TEST_CASE("free fighters (run 57): units aboard a transport still count, the dead do not")
{
	CHECK(wc2ai::FerryCountsFighter(true, false, false));
	CHECK(wc2ai::FerryCountsFighter(true, true, true));   // aboard
	CHECK_FALSE(wc2ai::FerryCountsFighter(true, true, false)); // removed, not in a transport
	CHECK_FALSE(wc2ai::FerryCountsFighter(false, false, false));
	CHECK_FALSE(wc2ai::FerryCountsFighter(false, true, true));
}

TEST_CASE("warship patrol (run 57): trained, idle, not held by a wave, not fixed by the map")
{
	CHECK(wc2ai::WarshipPatrols(true, false, 0, true));
	CHECK_FALSE(wc2ai::WarshipPatrols(true, true, 1, true));  // map-placed: stands
	CHECK_FALSE(wc2ai::WarshipPatrols(true, false, 2, true)); // a wave's
	CHECK_FALSE(wc2ai::WarshipPatrols(true, false, 0, false)); // busy
	CHECK_FALSE(wc2ai::WarshipPatrols(false, false, 0, true)); // transport / tanker
}
