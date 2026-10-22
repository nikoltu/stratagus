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
/**@name test_unit_cull_extent.cpp - CULL-HD-EXTENT: the box a unit draws into. */
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

#include "unit_cull_extent.h"

using unitcull::Box;
using unitcull::Extent;
using unitcull::Input;
using unitcull::Slack;
using unitcull::Touches;

static Input Footman()
{
	Input in;
	in.tileW = 1;
	in.tileH = 1;
	in.tilePxX = 64;
	in.tilePxY = 64;
	in.width = 72;
	in.height = 72;
	in.spriteW = 72;
	in.spriteH = 72;
	in.boxW = 64;
	in.boxH = 64;
	in.canvasW = 608; // 6.75 * 72 + 122
	in.canvasH = 608;
	return in;
}

TEST_CASE("cull extent: no HD art, no box = the classic cell plus slack")
{
	Input in = Footman();
	in.canvasW = in.canvasH = 0;
	in.boxW = in.boxH = 0;
	const Box b = Extent(in);
	CHECK(b.x0 == -4 - Slack);
	CHECK(b.y0 == -4 - Slack);
	CHECK(b.x1 == 68 + Slack);
	CHECK(b.y1 == 68 + Slack);
}

TEST_CASE("cull extent: the HD canvas overhangs the footprint by 58.07 map px (608 native, tile 64)")
{
	const Box b = Extent(Footman());
	// centre 32, half 608 * 64 / 216 / 2 = 90.07
	CHECK(b.x0 == -59 - Slack);
	CHECK(b.y0 == -59 - Slack);
	CHECK(b.x1 == 123 + Slack);
	CHECK(b.y1 == 123 + Slack);
}

TEST_CASE("cull extent: the old cull dropped a unit whose canvas reaches 27 screen px into the view")
{
	const Box b = Extent(Footman());
	// view's left edge at map x 1000; unit footprint at 900: classic cell ends at 968 (culled before)
	CHECK(900 + 68 < 1000);
	CHECK(Touches(b, 900, 500, 1000, 0, 2000, 1000));
	// far enough that even the canvas cannot reach: culled
	CHECK_FALSE(Touches(b, 1000 - 126, 500, 1000, 0, 2000, 1000));
	// right and bottom edges alike
	CHECK(Touches(b, 2000 + 55, 500, 1000, 0, 2000, 1000));
	CHECK(Touches(b, 1500, 1000 + 55, 1000, 0, 2000, 1000));
	CHECK_FALSE(Touches(b, 1500, 1000 + 62, 1000, 0, 2000, 1000));
}

TEST_CASE("cull extent: the health bar 15 classic px above the rectangle widens the top")
{
	Input in = Footman();
	in.canvasW = in.canvasH = 0;
	in.width = in.height = in.spriteW = in.spriteH = 64;
	const Box b = Extent(in);
	CHECK(b.y0 == -30 - Slack); // rectangle top 0, bar 15 x 2 map px above
	CHECK(b.y1 == 94 + Slack);  // the "below" place: 64 + (12 + 3) x 2
	CHECK(b.x0 == 0 - Slack);
	CHECK(b.x1 == 64 + Slack);
}

TEST_CASE("cull extent: Offset moves the canvas with the sprite")
{
	Input in;
	in.tilePxX = in.tilePxY = 64;
	in.width = in.height = 64;
	in.offY = -10;
	in.canvasW = in.canvasH = 338;
	const Box b = Extent(in);
	// centre y 22, half 338 * 64 / 216 / 2 = 50.07
	CHECK(b.y0 == -29 - Slack);
	CHECK(b.y1 == 73 + Slack);
	CHECK(b.x0 == -19 - Slack);
	CHECK(b.x1 == 83 + Slack);
}

TEST_CASE("cull extent: a 4x4 hall, canvas 986: overhang 18.07 map px = 27.1 screen px at k 3")
{
	Input in;
	in.tileW = in.tileH = 4;
	in.tilePxX = in.tilePxY = 64;
	in.width = in.height = 234;
	in.canvasW = in.canvasH = 986;
	const Box b = Extent(in);
	// centre 128, half 986 * 64 / 216 / 2 = 146.07
	CHECK(b.x0 == -19 - Slack);
	CHECK(b.x1 == 275 + Slack);
	CHECK(18.07 * 1.5 > 27.0); // the pop SCALE_ZOOM names, now inside the box
}
