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

#include "flyer_death.h"

TEST_CASE("A dying flyer keeps its shadow for the first 20 frames of its Death")
{
	// killed, the Death not yet run: the shadow is still there
	CHECK(FlyerDeathShadowDrawn(true, 0));
	for (int frames = 1; frames <= 20; ++frames) {
		CHECK(FlyerDeathShadowDrawn(true, frames));
	}
	// the measured Dragon: the shadow is gone after about 20 frames
	for (int frames = 21; frames <= 200; ++frames) {
		CHECK_FALSE(FlyerDeathShadowDrawn(true, frames));
	}
	CHECK(FlyerDeathShadowFrames == 20);
}

TEST_CASE("A unit that does not fly draws no shadow while it dies")
{
	for (int frames = 0; frames <= 30; ++frames) {
		CHECK_FALSE(FlyerDeathShadowDrawn(false, frames));
	}
}
