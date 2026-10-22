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
/**@name test_spell_range.cpp - The test file for the cast range of spells (unit.h, unit_find.h). */
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

#include "unit.h"
#include "unit_find.h"
#include "unittype.h"

// The original's range test 0x4C2140: in range when both tile offsets are within the spell's
// range (the larger of the x and y gaps, Chebyshev), every spell order (0x26..0x38).
namespace
{
struct SpellRangeScene
{
	SpellRangeScene()
	{
		single.TileWidth = 1;
		single.TileHeight = 1;
		building.TileWidth = 3;
		building.TileHeight = 3;
		caster.Type = &single;
		target.Type = &single;
		hall.Type = &building;
		caster.tilePos = Vec2i(20, 20);
	}
	CUnitType single;
	CUnitType building;
	CUnit caster;
	CUnit target;
	CUnit hall;
};
}

TEST_CASE("spell range: a diagonal target at the range is in range")
{
	SpellRangeScene s;
	s.target.tilePos = Vec2i(30, 30); // 10 tiles on both axes, Death Coil's range 10

	CHECK(s.caster.SpellDistanceTo(s.target) == 10);
	CHECK(s.caster.SpellDistanceTo(Vec2i(30, 30)) == 10);
	CHECK(s.caster.SpellDistanceTo(Vec2i(10, 10)) == 10);
	CHECK(s.caster.SpellDistanceTo(Vec2i(30, 10)) == 10);
	// the earlier measure (rounded Euclidean) called the same tile 14 away
	CHECK(s.caster.MapDistanceTo(s.target) == 14);
}

TEST_CASE("spell range: one tile beyond on either axis is out of range")
{
	SpellRangeScene s;
	s.target.tilePos = Vec2i(31, 30);
	CHECK(s.caster.SpellDistanceTo(s.target) == 11);
	s.target.tilePos = Vec2i(25, 31);
	CHECK(s.caster.SpellDistanceTo(s.target) == 11);
	CHECK(s.caster.SpellDistanceTo(Vec2i(9, 20)) == 11);
}

TEST_CASE("spell range: straight lines and the caster's own tile")
{
	SpellRangeScene s;
	CHECK(s.caster.SpellDistanceTo(Vec2i(20, 20)) == 0);
	CHECK(s.caster.SpellDistanceTo(Vec2i(26, 20)) == 6);
	CHECK(s.caster.SpellDistanceTo(Vec2i(20, 14)) == 6);
	s.target.tilePos = Vec2i(21, 21);
	CHECK(s.caster.SpellDistanceTo(s.target) == 1);
}

TEST_CASE("spell range: a building goal is measured to its nearest tile")
{
	SpellRangeScene s;
	s.hall.tilePos = Vec2i(28, 28); // footprint 28..30
	CHECK(s.caster.SpellDistanceTo(s.hall) == 8);
	s.hall.tilePos = Vec2i(8, 20);  // footprint x 8..10, the caster's row
	CHECK(s.caster.SpellDistanceTo(s.hall) == 10);
	s.hall.tilePos = Vec2i(7, 7);   // footprint 7..9
	CHECK(s.caster.SpellDistanceTo(s.hall) == 11);
}

TEST_CASE("spell range: the measure does not depend on the direction")
{
	SpellRangeScene s;
	s.target.tilePos = Vec2i(14, 27);
	CHECK(s.caster.SpellDistanceTo(s.target) == s.target.SpellDistanceTo(s.caster));
	CHECK(s.caster.SpellDistanceTo(s.target) == 7);
}

TEST_CASE("spell range: the autocast's minimum range uses the same measure")
{
	SpellRangeScene s;
	const OutOfMinRange outside(3, s.caster.tilePos);
	s.target.tilePos = Vec2i(22, 22); // 2 on both axes: Euclidean 2, Chebyshev 2
	CHECK_FALSE(outside(&s.target));
	s.target.tilePos = Vec2i(23, 23); // Chebyshev 3 (Euclidean 4)
	CHECK(outside(&s.target));
	s.target.tilePos = Vec2i(23, 20);
	CHECK(outside(&s.target));
}
