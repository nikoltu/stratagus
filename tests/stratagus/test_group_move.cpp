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
/**@name test_group_move.cpp - The test file for group_move.h. */
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

#include "stratagus.h"

#include "group_move.h"

#include <vector>

namespace
{

using Tiles = std::vector<Vec2i>;

// The map of the measured trials is 64 x 64.
Tiles Goals(const Tiles &starts, const Vec2i &target)
{
	return GroupMoveGoals(starts, target, 64, 64);
}

void CheckGoals(const Tiles &starts, const Vec2i &target, const Tiles &expected)
{
	const Tiles goals = Goals(starts, target);

	REQUIRE(goals.size() == expected.size());
	for (size_t i = 0; i != goals.size(); ++i) {
		CAPTURE(i);
		CHECK(goals[i].x == expected[i].x);
		CHECK(goals[i].y == expected[i].y);
	}
}

Tiles Block(int x0, int x1, int y0, int y1)
{
	Tiles tiles;
	for (int y = y0; y <= y1; ++y) {
		for (int x = x0; x <= x1; ++x) {
			tiles.emplace_back(x, y);
		}
	}
	return tiles;
}

} // namespace

TEST_CASE("group move: one unit goes to the target")
{
	CheckGoals({{54, 52}}, {45, 52}, {{45, 52}});
	CheckGoals({}, {45, 52}, {});
}

TEST_CASE("group move: two units")
{
	// a row
	CheckGoals({{50, 60}, {51, 60}}, {47, 51}, {{47, 51}, {48, 51}});
	// a column with a gap: the target itself stays empty
	CheckGoals({{48, 59}, {48, 61}}, {46, 51}, {{46, 50}, {46, 52}});
	// x extent 3 is kept, y extent 4 is folded
	CheckGoals({{45, 49}, {48, 53}}, {47, 59}, {{46, 59}, {49, 59}});
}

TEST_CASE("group move: three units")
{
	// a row with a gap
	CheckGoals({{45, 60}, {46, 60}, {48, 60}}, {48, 51}, {{47, 51}, {48, 51}, {50, 51}});
	// x extent 4 is folded, y extent 3 is kept; the mean is floored (51.67 -> 51)
	CheckGoals({{46, 50}, {46, 52}, {50, 53}}, {47, 58}, {{47, 57}, {47, 59}, {47, 60}});
	// x extent 7 is folded: two units share one goal
	CheckGoals({{46, 50}, {50, 50}, {53, 52}}, {48, 59}, {{48, 59}, {48, 59}, {48, 61}});
	// both axes kept
	CheckGoals({{45, 52}, {47, 51}, {48, 51}}, {49, 59}, {{48, 60}, {50, 59}, {51, 59}});
	// a row
	CheckGoals({{50, 59}, {51, 59}, {52, 59}}, {46, 60}, {{45, 60}, {46, 60}, {47, 60}});
}

TEST_CASE("group move: four and five units")
{
	// a row of four: the target is the second unit
	CheckGoals({{47, 51}, {48, 51}, {49, 51}, {50, 51}}, {46, 60}, {{45, 60}, {46, 60}, {47, 60}, {48, 60}});
	CheckGoals({{45, 60}, {46, 60}, {47, 60}, {45, 61}, {47, 61}},
	           {49, 51},
	           {{48, 51}, {49, 51}, {50, 51}, {48, 52}, {50, 52}});
	CheckGoals({{48, 51}, {49, 51}, {50, 51}, {48, 52}, {50, 52}},
	           {46, 51},
	           {{45, 51}, {46, 51}, {47, 51}, {45, 52}, {47, 52}});
	// the same translation for an attack order on an empty tile
	CheckGoals({{45, 51}, {45, 52}, {46, 51}, {47, 52}, {48, 51}},
	           {48, 59},
	           {{47, 59}, {47, 60}, {48, 59}, {49, 60}, {50, 59}});
}

TEST_CASE("group move: blocks")
{
	// 3 x 2: the target is the middle of the upper row
	CheckGoals(Block(50, 52, 58, 59), {51, 51}, Block(50, 52, 51, 52));
	CheckGoals(Block(50, 52, 51, 52), {51, 58}, Block(50, 52, 58, 59));
	// 3 x 3: the target is the middle
	CheckGoals(Block(48, 50, 50, 52), {51, 58}, Block(50, 52, 57, 59));
	// 3 x 3 sent one row up
	CheckGoals(Block(48, 50, 51, 53), {49, 51}, Block(48, 50, 50, 52));
}

TEST_CASE("group move: nine units, folded")
{
	// x extent 4 is folded, y extent 2 is kept
	CheckGoals({{48, 60}, {49, 58}, {50, 58}, {50, 59}, {51, 58}, {51, 59}, {52, 58}, {52, 59}, {52, 60}},
	           {49, 51},
	           {{49, 53}, {49, 51}, {49, 51}, {49, 52}, {49, 51}, {49, 52}, {49, 51}, {49, 52}, {49, 53}});
	// scattered over 12 x 8: both axes folded, every unit gets the target
	CheckGoals({{41, 52}, {42, 50}, {43, 50}, {44, 50}, {45, 50}, {49, 52}, {51, 51}, {51, 54}, {52, 57}},
	           {51, 59},
	           Tiles(9, Vec2i(51, 59)));
}

TEST_CASE("group move: the fold limit")
{
	// extent 3 keeps the offsets, extent 4 drops them
	CheckGoals({{10, 10}, {13, 10}}, {30, 30}, {{29, 30}, {32, 30}});
	CheckGoals({{10, 10}, {14, 10}}, {30, 30}, {{30, 30}, {30, 30}});
	CheckGoals({{10, 10}, {10, 13}}, {30, 30}, {{30, 29}, {30, 32}});
	CheckGoals({{10, 10}, {10, 14}}, {30, 30}, {{30, 30}, {30, 30}});
}

TEST_CASE("group move: an order to the group's own anchor sends every unit to the target")
{
	// the translation would move nobody
	CheckGoals({{46, 59}, {49, 59}}, {47, 59}, {{47, 59}, {47, 59}});
	CheckGoals({{45, 60}, {46, 60}, {47, 60}, {48, 60}}, {46, 60}, Tiles(4, Vec2i(46, 60)));
}

TEST_CASE("group move: goals are kept on the map")
{
	// top-left corner
	CheckGoals(Block(10, 12, 10, 12), {0, 0}, {{0, 0}, {0, 0}, {1, 0}, {0, 0}, {0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 1}});
	// bottom-right corner
	CheckGoals(Block(10, 12, 10, 12), {63, 63}, {{62, 62}, {63, 62}, {63, 62}, {62, 63}, {63, 63}, {63, 63}, {62, 63}, {63, 63}, {63, 63}});
	// a target off the map is brought back, for one unit too
	CheckGoals({{5, 5}}, {70, -3}, {{63, 0}});
	// a map that is not square
	const Tiles goals = GroupMoveGoals({{5, 5}, {6, 6}}, {31, 127}, 32, 128);
	REQUIRE(goals.size() == 2);
	CHECK(goals[0] == Vec2i(31, 127));
	CHECK(goals[1] == Vec2i(31, 127));
}

namespace
{

// The map of the measured ship trials is 128 x 128; ships rest on every second tile.
void CheckShipGoals(const Tiles &starts, const Vec2i &target, const Tiles &expected)
{
	const Tiles goals = GroupMoveGridGoals(starts, target, 128, 128, ShipGridStep);

	REQUIRE(goals.size() == expected.size());
	for (size_t i = 0; i != goals.size(); ++i) {
		CAPTURE(i);
		CHECK(goals[i].x == expected[i].x);
		CHECK(goals[i].y == expected[i].y);
	}
}

} // namespace

TEST_CASE("group move of ships: touching ships keep their places")
{
	// three ships, the target on an odd tile: the goals go down onto the grid
	CheckShipGoals({{22, 122}, {22, 124}, {24, 124}}, {11, 125}, {{10, 124}, {10, 126}, {12, 126}});
	// a row and a column of two
	CheckShipGoals({{10, 10}, {12, 10}}, {30, 30}, {{28, 30}, {30, 30}});
	CheckShipGoals({{10, 10}, {10, 12}}, {30, 30}, {{30, 28}, {30, 30}});
	// two ships corner to corner
	CheckShipGoals({{10, 10}, {12, 12}}, {30, 30}, {{28, 28}, {30, 30}});
	// a block of four
	CheckShipGoals({{10, 10}, {12, 10}, {10, 12}, {12, 12}}, {40, 60}, {{38, 58}, {40, 58}, {38, 60}, {40, 60}});
}

TEST_CASE("group move of ships: an axis of four tiles or more is folded")
{
	// x extent 6 is folded, y extent 2 is kept: one ship above the target, three on it
	CheckShipGoals({{20, 122}, {18, 124}, {22, 124}, {24, 124}}, {12, 124}, {{12, 122}, {12, 124}, {12, 124}, {12, 124}});
	// x extent 2 is kept, y extent 4 is folded: three on the target, one beyond it
	CheckShipGoals({{12, 122}, {12, 124}, {12, 126}, {14, 124}}, {24, 124}, {{24, 124}, {24, 124}, {24, 124}, {26, 124}});
	// ships with a free cell between them do not keep their places
	CheckShipGoals({{10, 10}, {14, 10}}, {30, 30}, {{30, 30}, {30, 30}});
	CheckShipGoals({{10, 10}, {10, 14}}, {30, 30}, {{30, 30}, {30, 30}});
	// both axes folded, the target on an odd tile
	CheckShipGoals({{10, 10}, {14, 10}, {10, 16}}, {31, 31}, Tiles(3, Vec2i(30, 30)));
}

TEST_CASE("group move of ships: one ship")
{
	CheckShipGoals({{28, 122}}, {12, 124}, {{12, 124}});
	// an odd tile goes down onto the grid
	CheckShipGoals({{28, 122}}, {13, 123}, {{12, 122}});
	CheckShipGoals({}, {13, 123}, {});
}

TEST_CASE("group move of ships: goals are kept on the map and on the grid")
{
	const Tiles block = {{10, 10}, {12, 10}, {10, 12}, {12, 12}};

	// top-left corner
	CheckShipGoals(block, {0, 0}, Tiles(4, Vec2i(0, 0)));
	CheckShipGoals(block, {2, 1}, {{0, 0}, {2, 0}, {0, 2}, {2, 2}});
	// bottom-right corner: 126 is the last tile of the grid
	CheckShipGoals(block, {127, 127}, Tiles(4, Vec2i(126, 126)));
	CheckShipGoals(block, {125, 126}, {{124, 124}, {126, 124}, {124, 126}, {126, 126}});
	// a target off the map is brought back
	CheckShipGoals(block, {200, -5}, {{126, 0}, {126, 0}, {126, 0}, {126, 0}});
	// a map with an odd size: its last tile is on the grid
	const Tiles goals = GroupMoveGridGoals({{10, 10}, {12, 10}}, {32, 40}, 33, 41, ShipGridStep);
	REQUIRE(goals.size() == 2);
	CHECK(goals[0] == Vec2i(30, 40));
	CHECK(goals[1] == Vec2i(32, 40));
}

TEST_CASE("group move of ships: a mixed selection, each kind over itself")
{
	// land units and ships of one selection do not shift each other's anchor
	const Tiles land = {{50, 60}, {51, 60}};
	const Tiles ships = {{22, 122}, {22, 124}, {24, 124}};
	const Vec2i target(11, 125);

	const Tiles landGoals = GroupMoveGoals(land, target, 128, 128);
	REQUIRE(landGoals.size() == 2);
	CHECK(landGoals[0] == Vec2i(11, 125));
	CHECK(landGoals[1] == Vec2i(12, 125));
	CheckShipGoals(ships, target, {{10, 124}, {10, 126}, {12, 126}});
	// a step of one tile is the land rule
	const Tiles plain = GroupMoveGridGoals(land, target, 128, 128, 1);
	REQUIRE(plain.size() == 2);
	CHECK(plain[0] == landGoals[0]);
	CHECK(plain[1] == landGoals[1]);
}

#include "selection_cells.h"

TEST_CASE("panel: selection cells keep their places")
{
	using Cells = std::vector<int>; // units by number, 0 = an empty cell
	const Cells nine = {1, 2, 3, 4, 5, 6, 7, 8, 9};

	// the first selection, and one that shares no unit with the one before: from the first cell
	CHECK(KeepSelectionCells<int>({}, nine) == nine);
	CHECK(KeepSelectionCells<int>(nine, {}) == Cells{});
	CHECK(KeepSelectionCells<int>({6, 7, 8, 9}, {1, 2, 3, 4, 5}) == Cells{1, 2, 3, 4, 5});
	CHECK(KeepSelectionCells<int>({0, 0, 3, 4, 0}, {7, 8}) == Cells{7, 8});

	// death: the cell stays empty, the others where they were (the selection's own order has
	// changed: the last unit was moved into the freed place)
	CHECK(KeepSelectionCells<int>(nine, {1, 2, 3, 4, 9, 6, 7, 8}) == Cells{1, 2, 3, 4, 0, 6, 7, 8, 9});
	CHECK(KeepSelectionCells<int>({1, 2, 3, 4, 0, 6, 7, 8, 9}, {1, 2, 3, 4, 8, 6, 7}) == Cells{1, 2, 3, 4, 0, 6, 7, 8, 0});
	CHECK(KeepSelectionCells<int>({1, 2}, {1}) == Cells{1, 0});

	// a part of the selection chosen again: the two stay in cells 2 and 3
	CHECK(KeepSelectionCells<int>(nine, {4, 3}) == Cells{0, 0, 3, 4, 0, 0, 0, 0, 0});
	// then five with those two among them: the three new ones take cells 0, 1, 4
	CHECK(KeepSelectionCells<int>({0, 0, 3, 4, 0, 0, 0, 0, 0}, {11, 3, 12, 4, 13}) == Cells{11, 12, 3, 4, 13, 0, 0, 0, 0});

	// a wider selection: the five keep cells 0..4, the new ones follow
	CHECK(KeepSelectionCells<int>({1, 2, 3, 4, 5}, {9, 5, 8, 4, 7, 3, 6, 2, 1}) == Cells{1, 2, 3, 4, 5, 9, 8, 7, 6});
	// one unit kept its cell while it was alone
	CHECK(KeepSelectionCells<int>(nine, {5}) == Cells{0, 0, 0, 0, 5, 0, 0, 0, 0});
	CHECK(KeepSelectionCells<int>({0, 0, 0, 0, 5, 0, 0, 0, 0}, {1, 2, 5}) == Cells{1, 2, 0, 0, 5, 0, 0, 0, 0});

	// more units than cells so far: the free cells first, then behind the last one
	CHECK(KeepSelectionCells<int>({0, 0, 3, 4, 0, 0, 0, 0, 9}, {21, 22, 3, 4, 23, 24, 25, 26, 9, 27, 28, 29})
	      == Cells{21, 22, 3, 4, 23, 24, 25, 26, 9, 27, 28, 29});
	CHECK(KeepSelectionCells<int>({1, 2}, {3, 2, 4}) == Cells{3, 2, 4});

	// the same selection: nothing moves, a gap stays a gap
	CHECK(KeepSelectionCells<int>({1, 0, 3}, {3, 1}) == Cells{1, 0, 3});
}
