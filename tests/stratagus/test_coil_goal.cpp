//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \   __\_  __ \__  \   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name test_coil_goal.cpp - The test file for the goal of a position autocast (spells.cpp). */
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

#include "spells.h"
#include "unit.h"

#include <vector>

// DECODE_COIL: the computer's Death Coil is ordered on the unit the pass chose, not on its tile.
TEST_CASE("position goal: the candidate on the chosen tile becomes the goal")
{
	CUnit a;
	CUnit b;
	CUnit c;
	a.tilePos = Vec2i(3, 4);
	b.tilePos = Vec2i(10, 12);
	c.tilePos = Vec2i(7, 7);
	const std::vector<CUnit *> candidates{&a, &b, &c};

	CHECK(PositionGoalOf(true, candidates, Vec2i(10, 12)) == &b);
	CHECK(PositionGoalOf(true, candidates, Vec2i(3, 4)) == &a);
}

TEST_CASE("position goal: without the flag the order stays a tile order")
{
	CUnit a;
	a.tilePos = Vec2i(3, 4);
	const std::vector<CUnit *> candidates{&a};

	CHECK(PositionGoalOf(false, candidates, Vec2i(3, 4)) == nullptr);
}

TEST_CASE("position goal: a tile without a candidate gives no goal")
{
	CUnit a;
	a.tilePos = Vec2i(3, 4);
	const std::vector<CUnit *> candidates{&a};

	CHECK(PositionGoalOf(true, candidates, Vec2i(4, 4)) == nullptr);
	CHECK(PositionGoalOf(true, {}, Vec2i(3, 4)) == nullptr);
}

TEST_CASE("position goal: two candidates on one tile -- the first in candidate order")
{
	CUnit a;
	CUnit b;
	a.tilePos = Vec2i(5, 5);
	b.tilePos = Vec2i(5, 5);
	const std::vector<CUnit *> candidates{&b, &a};

	CHECK(PositionGoalOf(true, candidates, Vec2i(5, 5)) == &b);
}

#include "actions.h"
#include "player.h"
#include "unittype.h"

// AUTOCAST-CORPSE-CRASH: the autocast pass reads the order, type and owner of every candidate; a
// unit without them (a released corpse, a unit whose orders were cleared) is left out, not read.
TEST_CASE("autocast candidate: a unit with an order, a type and an owner is fit")
{
	CUnitType type;
	CPlayer player;
	CUnit unit;
	unit.Type = &type;
	unit.Player = &player;
	unit.Orders.push_back(COrder::NewActionStill());

	CHECK_FALSE(IsUnfitAutoCastUnit(unit));

	unit.Destroyed = 1; // released: its orders are about to be cleared
	CHECK(IsUnfitAutoCastUnit(unit));
	unit.Orders.clear();
}

TEST_CASE("autocast candidate: no order, a null order, no type or no owner is unfit")
{
	CUnitType type;
	CPlayer player;
	CUnit unit;
	unit.Type = &type;
	unit.Player = &player;

	CHECK(IsUnfitAutoCastUnit(unit)); // no order at all

	unit.Orders.push_back(nullptr);
	CHECK(IsUnfitAutoCastUnit(unit)); // a null first order
	unit.Orders.clear();

	unit.Orders.push_back(COrder::NewActionStill());
	unit.Type = nullptr;
	CHECK(IsUnfitAutoCastUnit(unit));
	unit.Type = &type;
	unit.Player = nullptr;
	CHECK(IsUnfitAutoCastUnit(unit));
	unit.Orders.clear();
}
