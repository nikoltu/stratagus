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
/**@name test_nineslice.cpp - The test file for nineslice.cpp. */
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

#include "nineslice.h"

#include <vector>

// The nine destination patches must tile the destination rectangle exactly:
// every pixel covered once, nothing drawn outside it.
static bool tilesDestExactly(int srcW, int srcH, int l, int t, int r, int b, const SDL_Rect &dest)
{
	NineSlicePatch p[9];
	ComputeNineSlice(srcW, srcH, l, t, r, b, dest, p);

	const int dw = dest.w < 0 ? 0 : dest.w;
	const int dh = dest.h < 0 ? 0 : dest.h;
	if (dw == 0 || dh == 0) {
		for (const NineSlicePatch &q : p) {
			if (q.dst.w > 0 && q.dst.h > 0) {
				return false;
			}
		}
		return true;
	}
	std::vector<int> cover(dw * dh, 0);
	for (const NineSlicePatch &q : p) {
		if (q.src.w < 0 || q.src.h < 0 || q.dst.w < 0 || q.dst.h < 0) {
			return false;
		}
		for (int y = q.dst.y; y < q.dst.y + q.dst.h; ++y) {
			for (int x = q.dst.x; x < q.dst.x + q.dst.w; ++x) {
				const int lx = x - dest.x;
				const int ly = y - dest.y;
				if (lx < 0 || ly < 0 || lx >= dw || ly >= dh) {
					return false; // drew outside the destination
				}
				cover[ly * dw + lx] += 1;
			}
		}
	}
	for (int c : cover) {
		if (c != 1) {
			return false;
		}
	}
	return true;
}

TEST_CASE("nineslice tiles destination exactly")
{
	const int dims[] = { 0, 1, 3, 8, 16, 33, 60 };
	const int insets[] = { 0, 1, 4, 8, 40 };
	for (int l : insets)
		for (int r : insets)
			for (int t : insets)
				for (int b : insets)
					for (int dw : dims)
						for (int dh : dims) {
							SDL_Rect dest{ 5, -7, dw, dh };
							CHECK(tilesDestExactly(32, 24, l, t, r, b, dest));
						}
}

TEST_CASE("nineslice keeps native corners when there is room")
{
	SDL_Rect dest{ 0, 0, 200, 100 };
	NineSlicePatch p[9];
	ComputeNineSlice(32, 24, 6, 5, 7, 4, dest, p);
	CHECK(p[0].dst.w == 6); CHECK(p[0].dst.h == 5); // top-left
	CHECK(p[2].dst.w == 7); CHECK(p[2].dst.h == 5); // top-right
	CHECK(p[6].dst.w == 6); CHECK(p[6].dst.h == 4); // bottom-left
	CHECK(p[8].dst.w == 7); CHECK(p[8].dst.h == 4); // bottom-right
}

TEST_CASE("nineslice degenerate inputs do not misbehave")
{
	CHECK(tilesDestExactly(0, 0, 0, 0, 0, 0, SDL_Rect{ 0, 0, 0, 0 }));
	CHECK(tilesDestExactly(-4, -4, -1, -2, -3, -4, SDL_Rect{ 0, 0, -5, -5 }));
	CHECK(tilesDestExactly(3, 3, 100, 100, 100, 100, SDL_Rect{ 2, 2, 20, 20 }));
	CHECK(tilesDestExactly(10, 10, 5, 5, 5, 5, SDL_Rect{ 0, 0, 3, 3 }));
}
