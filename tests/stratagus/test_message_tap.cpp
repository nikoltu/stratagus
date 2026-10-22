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
/**@name test_message_tap.cpp - The test file for message_tap.h. */
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

#include "message_tap.h"

// A text drawn at (100, 500), 300 x 40 screen pixels: the tap area is widened by 20 on every side.
TEST_CASE("message tap: a tap on the text of a positioned message is taken")
{
	CHECK(messagetap::hits(100, 500, 100, 500, 300, 40, true));
	CHECK(messagetap::hits(250, 520, 100, 500, 300, 40, true));
	CHECK(messagetap::hits(80, 480, 100, 500, 300, 40, true));
	CHECK(messagetap::hits(419, 559, 100, 500, 300, 40, true));
}

TEST_CASE("message tap: outside the widened text the tap is the map's")
{
	CHECK_FALSE(messagetap::hits(79, 520, 100, 500, 300, 40, true));
	CHECK_FALSE(messagetap::hits(420, 520, 100, 500, 300, 40, true));
	CHECK_FALSE(messagetap::hits(250, 479, 100, 500, 300, 40, true));
	CHECK_FALSE(messagetap::hits(250, 560, 100, 500, 300, 40, true));
}

TEST_CASE("message tap: a message without a position, or no text, takes nothing")
{
	CHECK_FALSE(messagetap::hits(250, 520, 100, 500, 300, 40, false));
	CHECK_FALSE(messagetap::hits(100, 500, 100, 500, 0, 40, true));
	CHECK_FALSE(messagetap::hits(100, 500, 100, 500, 300, 0, true));
}

// The pointer over the map (target_cursor.h): the original's cursors read in run 70.
#include "target_cursor.h"

TEST_CASE("target cursor: no order waiting - no hover cursor over units, mines or trees")
{
	using targetcursor::Pick;
	CHECK(targetcursor::pick(false, false, false, false) == Pick::Unchanged);
	CHECK(targetcursor::pick(false, true, true, false) == Pick::Unchanged);  // own unit
	CHECK(targetcursor::pick(false, true, false, false) == Pick::Unchanged); // enemy unit
	CHECK(targetcursor::pick(false, true, false, true) == Pick::Unchanged);  // gold mine
}

TEST_CASE("target cursor: an order waiting - yellow over ground, green over own, red over enemy")
{
	using targetcursor::Pick;
	CHECK(targetcursor::pick(true, false, false, false) == Pick::Yellow);
	CHECK(targetcursor::pick(true, true, true, false) == Pick::Green);
	CHECK(targetcursor::pick(true, true, false, false) == Pick::Red);
	// A neutral unit (gold mine) keeps the ground colour (not read in run 70).
	CHECK(targetcursor::pick(true, true, false, true) == Pick::Yellow);
}

TEST_CASE("target cursor run 79: neutral critters, gold mine and Circle of Power show the ground colour")
{
	// Live run 79: the attack cursor over two sheep, the gold mine and the Circle of Power was
	// the very cursor of bare ground (yellow); over an own Archer it was green.
	using targetcursor::Pick;
	const Pick ground = targetcursor::pick(true, false, false, false);
	CHECK(ground == Pick::Yellow);
	CHECK(targetcursor::pick(true, true, false, true) == ground); // critter / mine / circle
	CHECK(targetcursor::pick(true, true, true, false) == Pick::Green);
	CHECK(targetcursor::pick(true, true, true, false) != ground);
}
