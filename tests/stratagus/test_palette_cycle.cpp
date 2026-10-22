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
/**@name test_palette_cycle.cpp - The palette-cycle steps that time the glow pictures. */
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

#include "palette_cycle.h"

#include <vector>

namespace
{

/// The ticks (counted from 0) on which a record steps, over `ticks` ticks.
std::vector<int> StepTicks(palette_cycle::Tileset tileset, int record, int ticks)
{
	palette_cycle::Scheduler s;
	s.Setup(tileset);
	std::vector<int> at;
	int last = s.Step(record);
	for (int t = 0; t < ticks; ++t) {
		s.Tick();
		if (s.Step(record) != last) {
			at.push_back(t);
			last = s.Step(record);
		}
	}
	return at;
}

} // namespace

TEST_CASE("palette cycle: record 3 (Dark Portal, Runestone) steps every 21 ticks on every tileset")
{
	for (int t = 0; t < palette_cycle::TilesetCount; ++t) {
		const auto at = StepTicks(static_cast<palette_cycle::Tileset>(t), 3, 20000);
		REQUIRE(at.size() > 100);
		CHECK(at[0] == 19);
		bool all21 = true;
		for (size_t i = 1; i < at.size(); ++i) {
			all21 = all21 && at[i] - at[i - 1] == 21;
		}
		CHECK(all21);
	}
}

TEST_CASE("palette cycle: swamp record 4 (swamp portal) steps after 7, 8 or 9 ticks")
{
	const auto at = StepTicks(palette_cycle::Tileset::Swamp, 4, 20000);
	REQUIRE(at.size() > 1000);
	CHECK(at[0] == 7);
	int seen[10] = {};
	bool inRange = true;
	for (size_t i = 1; i < at.size(); ++i) {
		const int d = at[i] - at[i - 1];
		inRange = inRange && d >= 7 && d <= 9;
		if (d >= 0 && d < 10) {
			++seen[d];
		}
	}
	CHECK(inRange);
	CHECK(seen[7] > 0);
	CHECK(seen[8] > 0);
	CHECK(seen[9] > 0);
	// mean 8.59 ticks a step
	const double mean = double(at.back() - at.front()) / double(at.size() - 1);
	CHECK(mean > 8.55);
	CHECK(mean < 8.63);
}

TEST_CASE("palette cycle: the step wraps after the record's palette entries")
{
	palette_cycle::Scheduler s;
	s.Setup(palette_cycle::Tileset::Forest);
	std::vector<int> steps;
	int last = s.Step(3);
	for (int t = 0; t < 20 + 21 * 6; ++t) {
		s.Tick();
		if (s.Step(3) != last) {
			last = s.Step(3);
			steps.push_back(last);
		}
	}
	CHECK(steps == std::vector<int>{1, 2, 3, 4, 0, 1, 2});
	// winter record 4 (Church, Alchemist): three pictures
	s.Setup(palette_cycle::Tileset::Winter);
	steps.clear();
	last = s.Step(4);
	for (int t = 0; t < 20 + 21 * 4; ++t) {
		s.Tick();
		if (s.Step(4) != last) {
			last = s.Step(4);
			steps.push_back(last);
		}
	}
	CHECK(steps == std::vector<int>{1, 2, 0, 1, 2});
	// a record the tileset does not switch on never steps
	s.Setup(palette_cycle::Tileset::Forest);
	for (int t = 0; t < 500; ++t) {
		s.Tick();
	}
	CHECK(s.Step(5) == 0);
	CHECK(s.Step(0) == 0);
}

TEST_CASE("palette cycle: 20 ms of real time per tick, none while paused")
{
	palette_cycle::Scheduler s;
	s.Setup(palette_cycle::Tileset::Forest);
	uint32_t now = 1000;
	s.Advance(now, false); // the clock starts
	for (int i = 0; i < 19; ++i) {
		now += 20;
		s.Advance(now, false);
	}
	CHECK(s.Step(3) == 0);
	now += 20;
	s.Advance(now, false); // the 20th tick (tick 19)
	CHECK(s.Step(3) == 1);
	// paused for ten seconds: nothing, and the time is not made up
	for (int i = 0; i < 500; ++i) {
		now += 20;
		s.Advance(now, true);
	}
	CHECK(s.Step(3) == 1);
	for (int i = 0; i < 20; ++i) {
		now += 20;
		s.Advance(now, false);
	}
	CHECK(s.Step(3) == 1);
	now += 20;
	s.Advance(now, false);
	CHECK(s.Step(3) == 2);
	// a stall of a second makes up at most 10 ticks
	s.Setup(palette_cycle::Tileset::Forest);
	now = 3000;
	s.Advance(now, false);
	now += 1000;
	s.Advance(now, false);
	for (int i = 0; i < 10; ++i) {
		now += 20;
		s.Advance(now, false);
	}
	CHECK(s.Step(3) == 1); // 10 + 10 ticks, not 50 + 10
	CHECK(s.Step(1) != 0);
	// a 16 ms frame loop still ticks every 20 ms on average
	s.Setup(palette_cycle::Tileset::Forest);
	now = 5000;
	s.Advance(now, false);
	for (int f = 0; f < 1000; ++f) { // 16 s
		now += 16;
		s.Advance(now, false);
	}
	// 800 ticks: first step at tick 19, then every 21 -> 1 + (799 - 19) / 21 = 38 steps, 38 % 5 = 3
	CHECK(s.Step(3) == 3);
}
