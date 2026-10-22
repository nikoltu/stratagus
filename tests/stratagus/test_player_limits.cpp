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

#include "player.h"

TEST_CASE("food: the supply the test compares is capped at 200")
{
	CHECK(FoodSupplyCap == 200);
	// under the cap: the plain test
	CHECK_FALSE(FoodRefuses(8, 7, 1));
	CHECK(FoodRefuses(8, 8, 1));
	CHECK(FoodRefuses(0, 0, 1));
	// a unit that needs no food is never refused
	CHECK_FALSE(FoodRefuses(0, 5, 0));
	// at and above the cap: 51 farms (204) make no more room than 50 (200)
	CHECK_FALSE(FoodRefuses(200, 199, 1));
	CHECK(FoodRefuses(200, 200, 1));
	CHECK(FoodRefuses(204, 200, 1));
	CHECK(FoodRefuses(260, 200, 1));
	CHECK_FALSE(FoodRefuses(260, 199, 1));
}
