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
/**@name test_touch_drag.cpp - The test file for touch_drag.h. */
//
//      (c) Copyright 2026 by The Stratagus Project
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

#include "touch_drag.h"

using touchdrag::Kind;
using touchdrag::Step;
using touchdrag::onMotion;

static const int DragPx = 24;

TEST_CASE("touch drag: the drag press is given where the finger landed, then the pointer follows")
{
	// Landed at (500, 300), detected at (525, 300): 25 px, past the 24 px threshold.
	const Step s = onMotion(500, 300, 525, 300, false, false, DragPx);
	CHECK(s.startsDrag);
	REQUIRE(s.count == 3);
	CHECK(s.events[0].kind == Kind::Motion);
	CHECK(s.events[0].x == 500);
	CHECK(s.events[0].y == 300);
	CHECK(s.events[1].kind == Kind::LeftDown);
	CHECK(s.events[1].x == 500);
	CHECK(s.events[1].y == 300);
	CHECK(s.events[2].kind == Kind::Motion);
	CHECK(s.events[2].x == 525);
	CHECK(s.events[2].y == 300);
}

TEST_CASE("touch drag: vertical and diagonal drift start the drag at the origin too")
{
	const Step v = onMotion(200, 200, 200, 170, false, false, DragPx);
	CHECK(v.startsDrag);
	REQUIRE(v.count == 3);
	CHECK(v.events[1].kind == Kind::LeftDown);
	CHECK(v.events[1].x == 200);
	CHECK(v.events[1].y == 200);
	CHECK(v.events[2].y == 170);

	const Step d = onMotion(200, 200, 160, 240, false, false, DragPx);
	CHECK(d.startsDrag);
	REQUIRE(d.count == 3);
	CHECK(d.events[1].x == 200);
	CHECK(d.events[1].y == 200);
	CHECK(d.events[2].x == 160);
	CHECK(d.events[2].y == 240);
}

TEST_CASE("touch drag: within the threshold the pointer follows and nothing is pressed")
{
	const Step s = onMotion(500, 300, 524, 276, false, false, DragPx);
	CHECK_FALSE(s.startsDrag);
	REQUIRE(s.count == 1);
	CHECK(s.events[0].kind == Kind::Motion);
	CHECK(s.events[0].x == 524);
	CHECK(s.events[0].y == 276);
}

TEST_CASE("touch drag: once dragging, every motion is a single motion and no second press")
{
	const Step s = onMotion(500, 300, 700, 450, true, false, DragPx);
	CHECK_FALSE(s.startsDrag);
	REQUIRE(s.count == 1);
	CHECK(s.events[0].kind == Kind::Motion);
	CHECK(s.events[0].x == 700);
	CHECK(s.events[0].y == 450);

	// Even back inside the threshold.
	const Step back = onMotion(500, 300, 505, 300, true, false, DragPx);
	CHECK_FALSE(back.startsDrag);
	REQUIRE(back.count == 1);
	CHECK(back.events[0].kind == Kind::Motion);
}

TEST_CASE("touch drag: the second touch of a pair sends nothing until it drifts")
{
	const Step still = onMotion(500, 300, 510, 310, false, true, DragPx);
	CHECK_FALSE(still.startsDrag);
	CHECK(still.count == 0);

	// When it drifts, its drag press is at its own landing point as well (TOUCH-R5 box).
	const Step drag = onMotion(500, 300, 540, 300, false, true, DragPx);
	CHECK(drag.startsDrag);
	REQUIRE(drag.count == 3);
	CHECK(drag.events[0].kind == Kind::Motion);
	CHECK(drag.events[0].x == 500);
	CHECK(drag.events[1].kind == Kind::LeftDown);
	CHECK(drag.events[1].x == 500);
	CHECK(drag.events[1].y == 300);
	CHECK(drag.events[2].x == 540);

	// A pair already dragging follows the finger.
	const Step follow = onMotion(500, 300, 505, 300, true, true, DragPx);
	REQUIRE(follow.count == 1);
	CHECK(follow.events[0].x == 505);
}
