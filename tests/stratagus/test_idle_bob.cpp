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

#include "idle_bob.h"

#include <set>
#include <vector>

namespace
{

/// Cycles at which the offset toggled while a unit of the given slot stood from cycle 'start'
std::vector<uint32_t> ToggleCycles(uint32_t slot, uint32_t start, uint32_t cycles, IdleBobState &bob)
{
	std::vector<uint32_t> toggles;
	for (uint32_t c = start; c < start + cycles; ++c) {
		const uint8_t before = bob.Offset;
		if (IdleBobCycle(bob, slot, c)) {
			CHECK(bob.Offset != before);
			toggles.push_back(c);
		} else {
			CHECK(bob.Offset == before);
		}
	}
	return toggles;
}

} // namespace

TEST_CASE("IdleBob: rests of 40, 45 .. 75 frames, the offset toggling 0 <-> 1")
{
	IdleBobState bob;
	const auto toggles = ToggleCycles(7, 1000, 20000, bob);
	REQUIRE(toggles.size() > 200);

	std::set<uint32_t> rests;
	for (size_t i = 1; i < toggles.size(); ++i) {
		const uint32_t rest = toggles[i] - toggles[i - 1];
		CHECK(rest >= 40);
		CHECK(rest <= 75);
		CHECK(rest % IdleBobCallFrames == 0);
		rests.insert(rest);
	}
	// a new random rest each time: every one of the eight rests shows up
	CHECK(rests.size() == 8);
	CHECK(bob.Offset == (toggles.size() % 2));
}

TEST_CASE("IdleBob: the first handler call draws a rest without a toggle")
{
	IdleBobState bob;
	for (uint32_t c = 0; c < IdleBobCallFrames; ++c) {
		CHECK_FALSE(IdleBobCycle(bob, 3, c));
	}
	CHECK(bob.Offset == 0);
	CHECK(bob.Timer >= 8);
	CHECK(bob.Timer <= 15);
	// the first toggle comes 40 .. 75 frames after the first call
	const auto toggles = ToggleCycles(3, IdleBobCallFrames, 100, bob);
	REQUIRE_FALSE(toggles.empty());
	const uint32_t first = toggles.front() - (IdleBobCallFrames - 1);
	CHECK(first >= 40);
	CHECK(first <= 75);
}

TEST_CASE("IdleBob: deterministic per unit, units not in step")
{
	IdleBobState a1;
	IdleBobState a2;
	CHECK(ToggleCycles(11, 500, 3000, a1) == ToggleCycles(11, 500, 3000, a2));

	IdleBobState b;
	IdleBobState c;
	CHECK(ToggleCycles(11, 500, 3000, b) != ToggleCycles(12, 500, 3000, c));

	for (uint32_t slot = 0; slot < 64; ++slot) {
		for (uint32_t cycle = 0; cycle < 64; ++cycle) {
			const uint8_t r = IdleBobRestCalls(slot, cycle);
			CHECK(r >= 8);
			CHECK(r <= 15);
		}
	}
}

TEST_CASE("IdleBob: one pixel of the original at the port's tile size")
{
	CHECK(IdleBobPixels(32) == 1);
	CHECK(IdleBobPixels(64) == 2);
}
