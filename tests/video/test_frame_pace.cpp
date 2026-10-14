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
/**@name test_frame_pace.cpp - The test file for frame_pace.h. */
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

#include "frame_pace.h"

TEST_CASE("frame pace: presented frames per second")
{
	// The display's rate up to the cap of 60 ...
	CHECK(framepace::targetFps(60, 30) == 60);
	CHECK(framepace::targetFps(50, 30) == 50);
	CHECK(framepace::targetFps(30, 30) == 30);
	// ... and the cap on a faster panel.
	CHECK(framepace::targetFps(90, 30) == 60);
	CHECK(framepace::targetFps(120, 30) == 60);
	CHECK(framepace::targetFps(144, 30) == 60);
	// A display slower than the game: the game's rate.
	CHECK(framepace::targetFps(24, 30) == 30);
	// The frame period of every capped rate is the same, whatever the panel.
	CHECK(1000.0 / framepace::targetFps(120, 30) == 1000.0 / framepace::targetFps(60, 30));
	// Another cap, should one be asked for.
	CHECK(framepace::targetFps(120, 30, 120) == 120);
	CHECK(framepace::targetFps(120, 30, 30) == 30);
}
