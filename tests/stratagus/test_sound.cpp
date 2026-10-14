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
/**@name test_sound.cpp - The test file for sound.cpp. */
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

#include "sound.h"

// A viewport 14 tiles wide on the 64 px tile pans over 13 tiles = 832 px.

TEST_CASE("sound: stereo runs from the viewport's left edge to its right edge")
{
	CHECK(StereoForMapPixel(0, 0, 832) == -128);
	CHECK(StereoForMapPixel(416, 0, 832) == 0);
	CHECK(StereoForMapPixel(832, 0, 832) == 127);
}

TEST_CASE("sound: stereo is relative to the scrolled viewport")
{
	// the same place on the screen pans the same wherever the map is scrolled to
	const int viewX = 40 * 64;
	CHECK(StereoForMapPixel(viewX, viewX, 832) == -128);
	CHECK(StereoForMapPixel(viewX + 416, viewX, 832) == 0);
	CHECK(StereoForMapPixel(viewX + 208, viewX, 832) == StereoForMapPixel(208, 0, 832));
	// left of the viewport stays left, right of it stays right
	CHECK(StereoForMapPixel(viewX - 640, viewX, 832) == -128);
	CHECK(StereoForMapPixel(viewX + 2000, viewX, 832) == 127);
}

TEST_CASE("sound: stereo of a viewport without width is the centre")
{
	CHECK(StereoForMapPixel(100, 0, 0) == 0);
	CHECK(StereoForMapPixel(100, 0, -64) == 0);
}

TEST_CASE("sound: audio focus scales a volume - full, ducked, silent")
{
	CHECK(FocusScaledVolume(128, AudioFocus::Full) == 128);
	CHECK(FocusScaledVolume(128, AudioFocus::Ducked) == 38);
	CHECK(FocusScaledVolume(128, AudioFocus::Silent) == 0);
	CHECK(FocusScaledVolume(255, AudioFocus::Ducked) == 76);
	CHECK(FocusScaledVolume(0, AudioFocus::Ducked) == 0);
	CHECK(FocusScaledVolume(1, AudioFocus::Ducked) == 0);
}
