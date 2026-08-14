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
/**@name test_uiscale.cpp - The test file for uiscale.cpp. */
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

#include "uiscale.h"

TEST_CASE("uniform scale identity when target equals reference")
{
	UniformScale s = ComputeUniformScale(640, 480, SDL_Rect{ 0, 0, 640, 480 });
	CHECK(s.num == 1);
	CHECK(s.den == 1);
	CHECK(s.fit.x == 0);
	CHECK(s.fit.y == 0);
	CHECK(s.fit.w == 640);
	CHECK(s.fit.h == 480);
}

TEST_CASE("uniform scale exact integer multiple")
{
	UniformScale s = ComputeUniformScale(640, 480, SDL_Rect{ 0, 0, 1280, 960 });
	CHECK(s.num == 2);
	CHECK(s.den == 1);
	CHECK(s.fit.w == 1280);
	CHECK(s.fit.h == 960);
}

TEST_CASE("uniform scale pillarboxes a wider target")
{
	UniformScale s = ComputeUniformScale(640, 480, SDL_Rect{ 0, 0, 1920, 1080 }); // 4:3 into 16:9
	CHECK(s.fit.h == 1080);      // height binds
	CHECK(s.fit.w < 1920);       // pillarbox
	CHECK(s.fit.x * 2 + s.fit.w == 1920); // centred
}

TEST_CASE("uniform scale letterboxes a taller target")
{
	UniformScale s = ComputeUniformScale(640, 480, SDL_Rect{ 0, 0, 480, 800 });
	CHECK(s.fit.w == 480);       // width binds
	CHECK(s.fit.h < 800);        // letterbox
}

TEST_CASE("uniform scale never exceeds the target and stays centred")
{
	const int tws[] = { 0, 1, 100, 640, 1366, 1920, 2560 };
	const int ths[] = { 0, 1, 100, 480, 768, 1080, 1440 };
	for (int tw : tws)
		for (int th : ths) {
			SDL_Rect t{ 3, 5, tw, th };
			UniformScale s = ComputeUniformScale(640, 480, t);
			CHECK(s.den > 0);
			CHECK(s.fit.w >= 0);
			CHECK(s.fit.h >= 0);
			CHECK(s.fit.w <= (tw < 0 ? 0 : tw));
			CHECK(s.fit.h <= (th < 0 ? 0 : th));
			CHECK(s.fit.x >= t.x);
			CHECK(s.fit.y >= t.y);
		}
}

TEST_CASE("uniform scale rounds a rational factor")
{
	// 7 * 3 / 2 rounds to 11; used indirectly by AnchorScaleRect.
	UniformScale s = ComputeUniformScale(2, 2, SDL_Rect{ 0, 0, 3, 3 });
	CHECK(s.num == 3);
	CHECK(s.den == 2);
}

TEST_CASE("uniform scale survives degenerate input")
{
	CHECK(ComputeUniformScale(0, 0, SDL_Rect{ 0, 0, 0, 0 }).den > 0);
	CHECK(ComputeUniformScale(-5, -5, SDL_Rect{ 3, 3, -10, -10 }).den > 0);
}
