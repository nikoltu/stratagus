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
/**@name test_group_mark.cpp - The test file for the last group of a unit (groups.cpp). */
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

TEST_CASE("group mark: a unit in no group has no last group")
{
	const unsigned long stamps[10] = {0, 5, 3, 0, 0, 0, 0, 0, 0, 0};
	CHECK(LastGroupOfBits(0u, stamps, 10) == -1);
}

TEST_CASE("group mark: the group given units last wins")
{
	// In groups 1 and 2; group 1 was given units after group 2.
	const unsigned long stamps[10] = {0, 5, 3, 0, 0, 0, 0, 0, 0, 0};
	CHECK(LastGroupOfBits((1u << 1) | (1u << 2), stamps, 10) == 1);
	// Group 2 stored again later.
	const unsigned long later[10] = {0, 5, 7, 0, 0, 0, 0, 0, 0, 0};
	CHECK(LastGroupOfBits((1u << 1) | (1u << 2), later, 10) == 2);
}

TEST_CASE("group mark: groups the unit is not in do not count")
{
	const unsigned long stamps[10] = {0, 5, 3, 0, 0, 0, 0, 0, 0, 9};
	CHECK(LastGroupOfBits(1u << 2, stamps, 10) == 2);
}

TEST_CASE("group mark: without stamps (a loaded game) the lowest group stands")
{
	const unsigned long stamps[10] = {};
	CHECK(LastGroupOfBits((1u << 4) | (1u << 7), stamps, 10) == 4);
}
