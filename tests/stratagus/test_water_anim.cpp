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
/**@name test_water_anim.cpp - The HD animated water: clock, twinkles, weights and the switch. */
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

#include "game.h"
#include "map.h"
#include "parameters.h"
#include "script.h"
#include "unit.h"
#include "water_anim.h"

#include <lauxlib.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// The real times (ms from the start) at which the clock steps, drawing every `frameMs`.
std::vector<uint32_t> StepTimes(uint32_t frameMs, uint32_t untilMs)
{
	water_anim::Clock c;
	c.Advance(0);
	std::vector<uint32_t> at;
	uint32_t last = c.Step();
	for (uint32_t t = frameMs; t <= untilMs; t += frameMs) {
		c.Advance(t);
		while (last < c.Step()) {
			at.push_back(t);
			++last;
		}
	}
	return at;
}

/// A phase map holding every value 0 .. 255 equally often (n pixels).
std::vector<uint8_t> UniformPhase(size_t n)
{
	std::vector<uint8_t> p(n);
	for (size_t i = 0; i < n; ++i) {
		p[i] = static_cast<uint8_t>(i % 256);
	}
	return p;
}

/// Fraction of the n values that are not 0.
double LitFraction(const std::vector<uint8_t> &v)
{
	size_t lit = 0;
	for (uint8_t x : v) {
		lit += x != 0;
	}
	return double(lit) / v.size();
}

} // namespace

TEST_CASE("water anim: open water steps every 120 ms, 8.3 a second (measured ~8 / s, median 123 ms)")
{
	const auto at = StepTimes(1, 10000);
	REQUIRE(at.size() == 83); // 10 s / 120 ms
	CHECK(at[0] == 120);
	bool all120 = true;
	for (size_t i = 1; i < at.size(); ++i) {
		all120 = all120 && at[i] - at[i - 1] == 120;
	}
	CHECK(all120);
	// Drawn at 60 frames a second (16 ms) the steps keep the rate: 8 or 9 in every second.
	const auto at60 = StepTimes(16, 10000);
	CHECK(at60.size() >= 82);
	CHECK(at60.size() <= 83);
	for (uint32_t s = 0; s < 9; ++s) {
		int n = 0;
		for (uint32_t t : at60) {
			n += t > s * 1000 && t <= (s + 1) * 1000;
		}
		CHECK(n >= 8);
		CHECK(n <= 9);
	}
}

TEST_CASE("water anim: the shore loop is 40 steps = 4.8 s (measured 4.8 - 5.1 s)")
{
	CHECK(water_anim::ShoreLoopSteps * water_anim::StepMs == 4800);
	water_anim::Clock c;
	c.Advance(0);
	c.Advance(1000);
	const uint32_t s0 = c.Step();
	for (uint32_t t = 1010; t <= 1000 + 4800; t += 10) {
		c.Advance(t); // drawn every 10 ms
	}
	CHECK(c.Step() == s0 + water_anim::ShoreLoopSteps);
	CHECK(c.ShorePhase() == static_cast<int>(s0 % water_anim::ShoreLoopSteps));
	// The shore layer is the same again after 40 steps and differs at every shorter lag.
	const auto phase = UniformPhase(256);
	std::vector<uint8_t> a(256), b(256);
	water_anim::BuildShoreLayer(phase.data(), 256, 7, 256, a.data());
	water_anim::BuildShoreLayer(phase.data(), 256, 7 + water_anim::ShoreLoopSteps, 256, b.data());
	CHECK(a == b);
	bool allDiffer = true;
	for (uint32_t k = 1; k < (uint32_t) water_anim::ShoreLoopSteps; ++k) {
		water_anim::BuildShoreLayer(phase.data(), 256, 7 + k, 256, b.data());
		allDiffer = allDiffer && a != b;
	}
	CHECK(allDiffer);
}

TEST_CASE("water anim: the clock runs on real time only, a long stall is not made up")
{
	// No pause input: the original's water kept changing while the game was paused (run 52 r2W5p,
	// ~122 ms). Drawn every 16 ms for 5 s the clock steps as in any other 5 s.
	CHECK(StepTimes(16, 5000).size() == 41);
	water_anim::Clock c;
	c.Advance(100);
	CHECK(c.Step() == 0);
	c.Advance(100 + 119);
	CHECK(c.Step() == 0);
	c.Advance(100 + 120);
	CHECK(c.Step() == 1);
	c.Advance(100 + 120 + 60000); // a minute without a frame
	CHECK(c.Step() == 1 + water_anim::MaxCatchUpSteps);
	c.Advance(100 + 120 + 60000 + 120);
	CHECK(c.Step() == 2 + water_anim::MaxCatchUpSteps);
	c.Reset();
	c.Advance(5);
	CHECK(c.Step() == 0);
}

TEST_CASE("water anim: nothing shifts - every pixel lights and fades in place from its own phase")
{
	// Owner feedback 2026-10-19: the sliding noise read as drifting clouds. Measured (run 55 shift
	// test, 40 / 40 pairs at 0,0): the picture never moves. A layer pixel depends only on the phase
	// map value at that pixel: shuffling the map shuffles the layer the same way, at every step.
	std::vector<uint8_t> phase(1000);
	for (size_t i = 0; i < phase.size(); ++i) {
		phase[i] = static_cast<uint8_t>((i * 37 + 11) % 256);
	}
	std::vector<size_t> perm(phase.size());
	for (size_t i = 0; i < perm.size(); ++i) {
		perm[i] = (i * 389) % perm.size(); // 389 and 1000 share no factor: a permutation
	}
	std::vector<uint8_t> shuffled(phase.size());
	for (size_t i = 0; i < perm.size(); ++i) {
		shuffled[i] = phase[perm[i]];
	}
	std::vector<uint8_t> a(phase.size()), b(phase.size());
	bool local = true;
	for (uint32_t s = 0; s < 200; s += 7) {
		water_anim::BuildOpenLayer(phase.data(), phase.size(), s, a.data());
		water_anim::BuildOpenLayer(shuffled.data(), shuffled.size(), s, b.data());
		for (size_t i = 0; i < perm.size(); ++i) {
			local = local && b[i] == a[perm[i]];
		}
		water_anim::BuildShoreLayer(phase.data(), phase.size(), s, 256, a.data());
		water_anim::BuildShoreLayer(shuffled.data(), shuffled.size(), s, 256, b.data());
		for (size_t i = 0; i < perm.size(); ++i) {
			local = local && b[i] == a[perm[i]];
		}
	}
	CHECK(local);
}

TEST_CASE("water anim: open water changes on every step and repeats first after 24 s")
{
	// Measured: 177 different states in 22 s (run 52 r2W4), no repeat in 6 - 13 s (run 55); a pixel
	// rises ~0.5 s and falls ~0.5 s (run 55 wF1).
	const auto phase = UniformPhase(256);
	std::vector<uint8_t> prev(256), cur(256);
	water_anim::BuildOpenLayer(phase.data(), 256, 0, prev.data());
	bool everyStep = true;
	for (uint32_t s = 1; s < 400; ++s) {
		water_anim::BuildOpenLayer(phase.data(), 256, s, cur.data());
		everyStep = everyStep && cur != prev;
		prev = cur;
	}
	CHECK(everyStep);
	// The twinkles repeat after 40 steps, the gate after 200: the pair first repeats after 200 steps.
	uint8_t g0[256], gk[256];
	water_anim::GateTable(3, g0);
	bool gateDiffers = true;
	for (uint32_t k = 40; k < water_anim::OpenGateSteps; k += 40) {
		water_anim::GateTable(3 + k, gk);
		gateDiffers = gateDiffers && !std::equal(g0, g0 + 256, gk);
	}
	CHECK(gateDiffers);
	water_anim::GateTable(3 + water_anim::OpenGateSteps, gk);
	CHECK(std::equal(g0, g0 + 256, gk));
	CHECK(water_anim::OpenGateSteps % water_anim::OpenPulseSteps == 0);
	CHECK(water_anim::OpenGateSteps * water_anim::StepMs > 22000);
	// One twinkle: over a loop of 40 steps it is lit on one run of 7 - 8 steps (~0.9 s), rising to
	// its peak and falling again, and 0 on all the others.
	for (uint32_t v : {0u, 1u, 31u, water_anim::OpenTwinkleValues - 1}) {
		std::vector<int> series;
		uint8_t t[256];
		for (uint32_t s = 0; s < water_anim::OpenPulseSteps; ++s) {
			water_anim::TwinkleTable(s, t);
			series.push_back(t[v]);
		}
		// Rotate so the series starts on an unlit step.
		const auto firstDark = std::find(series.begin(), series.end(), 0);
		REQUIRE(firstDark != series.end());
		std::rotate(series.begin(), firstDark, series.end());
		const auto on = std::find_if(series.begin(), series.end(), [](int x) { return x != 0; });
		const auto off = std::find(on, series.end(), 0);
		CHECK(off - on >= 7);
		CHECK(off - on <= 8);
		CHECK(std::all_of(off, series.end(), [](int x) { return x == 0; }));
		const auto top = std::max_element(on, off);
		CHECK(*top >= 216);
		CHECK(std::is_sorted(on, top + 1));
		CHECK(std::is_sorted(top, off, std::greater<int>()));
	}
	// Values from OpenTwinkleValues on never light: three quarters of the texels stay dark.
	bool restDark = true;
	for (uint32_t s = 0; s < water_anim::OpenPulseSteps; ++s) {
		uint8_t t[256];
		water_anim::TwinkleTable(s, t);
		restDark = restDark && std::all_of(t + water_anim::OpenTwinkleValues, t + 256, [](uint8_t x) { return x == 0; });
	}
	CHECK(restDark);
	// The gate keeps at least half and at most all.
	water_anim::GateTable(17, g0);
	CHECK(*std::min_element(g0, g0 + 256) >= water_anim::OpenGateMin);
	CHECK(*std::max_element(g0, g0 + 256) <= 255);
	// Shore: 12 % of the phases lit at a time, in two thin lines (2 turns over the map).
	water_anim::BuildShoreLayer(phase.data(), 256, 9, 256, cur.data());
	CHECK(LitFraction(cur) == doctest::Approx(0.12).epsilon(0.1));
}

TEST_CASE("water anim: outside the twinkles open water stays exactly unchanged, the same share every step")
{
	// Device 3.4.69 (c1127/DEVICE_3469.md W2): 23.6 % of open water changed by >= 1 level a step
	// (original 4.65 %), 3 - 64 % from step to step, in areas up to 250 px. Cause (desk simulation,
	// WATER_ANIM.md section 11): a smooth, bell-shaped phase map lit whole bands, and every lit pixel
	// carried a base that changed it on every step. Now: an unlit twinkle texel is exactly 0, so the
	// products with the mask, the crest and the gate keep it 0, and only the twinkling texels change.
	const int n = water_anim::TwinkleSize * water_anim::TwinkleSize;
	std::vector<uint8_t> map(n);
	for (int y = 0; y < water_anim::TwinkleSize; ++y) {
		for (int x = 0; x < water_anim::TwinkleSize; ++x) {
			map[y * water_anim::TwinkleSize + x] = water_anim::TwinkleHash(x, y);
		}
	}
	// The hash is even over 0 .. 255 and does not clump: two neighbours twinkle together about as
	// often as chance says (1 / 16).
	int hist[256] = {};
	for (uint8_t v : map) {
		++hist[v];
	}
	CHECK(*std::min_element(hist, hist + 256) >= 32);
	CHECK(*std::max_element(hist, hist + 256) <= 100);
	int bothTwinkle = 0;
	for (int y = 0; y < water_anim::TwinkleSize; ++y) {
		for (int x = 0; x + 1 < water_anim::TwinkleSize; ++x) {
			bothTwinkle += map[y * water_anim::TwinkleSize + x] < water_anim::OpenTwinkleValues
				&& map[y * water_anim::TwinkleSize + x + 1] < water_anim::OpenTwinkleValues;
		}
	}
	CHECK(double(bothTwinkle) / (water_anim::TwinkleSize * (water_anim::TwinkleSize - 1))
	      == doctest::Approx(0.0625).epsilon(0.15));
	std::vector<uint8_t> a(n), b(n);
	water_anim::BuildOpenLayer(map.data(), n, 0, a.data());
	double lo = 1, hi = 0;
	bool darkKept = true;
	for (uint32_t s = 1; s <= 400; ++s) {
		water_anim::BuildOpenLayer(map.data(), n, s, b.data());
		int changed = 0;
		for (int i = 0; i < n; ++i) {
			changed += a[i] != b[i];
			darkKept = darkKept && (map[i] >= water_anim::OpenTwinkleValues ? b[i] == 0 : true);
		}
		lo = std::min(lo, double(changed) / n);
		hi = std::max(hi, double(changed) / n);
		a = b;
	}
	CHECK(darkKept);
	// About 5 - 6 % of the texels change a step, on every step alike (was 1.6 - 57 % in the desk
	// simulation of 3.4.69's combine).
	CHECK(lo >= 0.045);
	CHECK(hi <= 0.065);
}

TEST_CASE("water anim: ranked phase maps light the same share on every step")
{
	// waterMasks.png's blue and red are bell-shaped; as phases they lit 8 - 16 % of a loop's shore
	// on some steps and half that on others. Equalize ranks them to an even spread.
	std::vector<uint8_t> bell(65536);
	for (size_t i = 0; i < bell.size(); ++i) {
		uint32_t h = 0;
		for (uint32_t k = 0; k < 4; ++k) {
			h += water_anim::TwinkleHash(static_cast<uint32_t>(i), k);
		}
		bell[i] = static_cast<uint8_t>(h / 4);
	}
	const auto spread = [](const std::vector<uint8_t> &m) {
		double lo = 1, hi = 0;
		std::vector<uint8_t> out(m.size());
		for (uint32_t s = 0; s < water_anim::ShoreLoopSteps; ++s) {
			water_anim::BuildShoreLayer(m.data(), m.size(), s, 256, out.data());
			const double f = LitFraction(out);
			lo = std::min(lo, f);
			hi = std::max(hi, f);
		}
		return std::make_pair(lo, hi);
	};
	const auto before = spread(bell);
	CHECK(before.second > 1.5 * before.first);
	water_anim::Equalize(bell.data(), bell.size());
	int quarter[4] = {};
	for (uint8_t v : bell) {
		++quarter[v / 64];
	}
	for (int q : quarter) {
		CHECK(q == doctest::Approx(16384).epsilon(0.05));
	}
	const auto after = spread(bell);
	CHECK(after.second < 1.3 * after.first);
	CHECK(after.first == doctest::Approx(0.12).epsilon(0.15));
	// The order of the values is kept (the shapes of the map stay).
	std::vector<uint8_t> ramp = {200, 10, 10, 90, 255, 0};
	water_anim::Equalize(ramp.data(), ramp.size());
	CHECK(ramp[5] < ramp[1]);
	CHECK(ramp[1] == ramp[2]);
	CHECK(ramp[2] < ramp[3]);
	CHECK(ramp[3] < ramp[0]);
	CHECK(ramp[0] < ramp[4]);
}

TEST_CASE("water anim: the twinkle copies are mirrored by place, so they do not repeat every square")
{
	// Device 3.4.69 W2b: the change map correlated 0.30 at the 228-px offset of one square.
	int count[4] = {};
	int sameBelow = 0;
	int sameRight = 0;
	for (int iy = -8; iy < 8; ++iy) {
		for (int ix = -8; ix < 8; ++ix) {
			const int f = water_anim::TwinkleFlip(ix, iy);
			REQUIRE(f >= 0);
			REQUIRE(f <= 3);
			++count[f];
			sameBelow += f == water_anim::TwinkleFlip(ix, iy + 1);
			sameRight += f == water_anim::TwinkleFlip(ix + 1, iy);
		}
	}
	for (int c : count) {
		CHECK(c >= 40); // 64 each by chance
	}
	CHECK(sameBelow <= 96); // 64 of 256 by chance
	CHECK(sameRight <= 96);
	// Fixed to the map: the same copy always gets the same mirror.
	CHECK(water_anim::TwinkleFlip(5, -3) == water_anim::TwinkleFlip(5, -3));
}

TEST_CASE("water anim: mask weights, the crest map and the water tint")
{
	// Channel map (WATER_ANIM.md "Look recalibration"): alpha 0 with blue 255 is open water (deep
	// tiles), alpha ~255 is land, blue under a falling alpha is the shore halo.
	CHECK(water_anim::OpenWeight(255, 0) == 255);
	CHECK(water_anim::ShoreWeight(255, 0) == 0);
	CHECK(water_anim::OpenWeight(0, 255) == 0);
	CHECK(water_anim::ShoreWeight(0, 255) == 0);
	CHECK(water_anim::OpenWeight(255, 255) == 0);
	CHECK(water_anim::ShoreWeight(255, 255) == 255);
	CHECK(water_anim::OpenWeight(255, 128) == 127);
	CHECK(water_anim::ShoreWeight(255, 128) == 128);
	// Crest map: the flat 61 floor gives the base only; a full crest gives the full amplitude.
	const uint8_t floorTexel = water_anim::CrestTexel(0);
	CHECK(water_anim::CrestTexel(water_anim::CrestFloor) == floorTexel);
	CHECK(floorTexel == water_anim::OpenBase * 255 / (water_anim::OpenBase + water_anim::OpenCrest));
	// Section 12: between the crests a twinkle stays dark (exactly 0 after the multiply).
	CHECK(floorTexel == 0);
	CHECK(water_anim::CrestTexel(255) == 255);
	CHECK(water_anim::CrestTexel(158) > floorTexel);
	CHECK(water_anim::CrestTexel(158) < 255);
	// The pulse: peak at phase 0, none beyond half the width.
	CHECK(water_anim::Pulse(0, 19661) == 255);
	CHECK(water_anim::Pulse(0x10000u - 1, 19661) >= 254);
	CHECK(water_anim::Pulse(19661 / 2, 19661) == 0);
	CHECK(water_anim::Pulse(0x8000u, 19661) == 0);
	// The tint follows each tileset's water hue (measured brightening), largest channel 255; no grey.
	const char *names[] = {"Forest", "Ice", "Swamp", "XSwamp"};
	for (const char *n : names) {
		const water_anim::Tint t = water_anim::TintFor(n);
		CHECK(std::max({t.r, t.g, t.b}) == 255);
		CHECK(std::min({t.r, t.g, t.b}) < 200);
		// The shore is raised so the luminance added is as tuned and never clips; the open layer is
		// tuned on the largest channel, 255 in every tint, and takes no gain.
		const uint32_t gain = water_anim::TintGain(t);
		CHECK(gain >= 256);
		CHECK(water_anim::ShoreStrength * gain / 256 <= 255);
	}
	CHECK(water_anim::TintFor("Forest").b == 255);
	CHECK(water_anim::TintFor("ice").b == 255);
	CHECK(water_anim::TintFor("Swamp").b == 255);
	CHECK(water_anim::TintFor("XSwamp").g == 255); // the swamp's water is green-brown
	CHECK(water_anim::TintFor("XSwamp").b < water_anim::TintFor("XSwamp").r);
	CHECK(water_anim::TintFor(nullptr).b == water_anim::TintFor("Forest").b);
	// The three maps are tiled at periods that are no ratio of small integers.
	CHECK(water_anim::CrestPeriod != 1.0f);
	CHECK(water_anim::GatePeriod != water_anim::CrestPeriod);
}

TEST_CASE("water anim: a held paused frame is drawn again at the water step rate, never every frame")
{
	// PERF-A3a under a paused window: the loop runs at 60 frames a second (16 ms); a frame is drawn
	// (and the clock advanced, as DrawHDWaterLayer does) only when RedrawHeldFrame says so.
	water_anim::Clock c;
	CHECK_FALSE(water_anim::RedrawHeldFrame(true, c, 1000)); // never drawn: nothing to move
	c.Advance(0);
	int drawn = 0;
	uint32_t lastDraw = 0;
	bool gapsOk = true;
	for (uint32_t t = 16; t <= 10000; t += 16) {
		if (water_anim::RedrawHeldFrame(true, c, t)) {
			const uint32_t before = c.Step();
			c.Advance(t);
			gapsOk = gapsOk && c.Step() == before + 1 && t - lastDraw >= 112 && t - lastDraw <= 128;
			lastDraw = t;
			++drawn;
		}
	}
	CHECK(gapsOk);
	CHECK(drawn >= 82); // 10 s at ~8.3 a second, out of 625 frames
	CHECK(drawn <= 83);
	// Off, or no water in view: the frame stays held (PERF-A3a's saving).
	CHECK_FALSE(water_anim::RedrawHeldFrame(false, c, 20000));
	// Right after a step nothing is due until the next one.
	c.Advance(20000);
	CHECK_FALSE(water_anim::RedrawHeldFrame(true, c, 20016));
	CHECK(water_anim::RedrawHeldFrame(true, c, 20120));
}

TEST_CASE("water anim: Animated Water off under a paused window stops the held water at once")
{
	using water_anim::HeldWater;
	water_anim::Clock c;
	c.Advance(0);
	const uint32_t due = 1000; // a step is due: on, the water would move
	// On: the GPU path adds the water; the old path draws again at the step.
	CHECK(water_anim::PlanHeldWater(true, false, true, true, c, due) == HeldWater::AddOnGpu);
	CHECK(water_anim::PlanHeldWater(true, false, false, true, c, due) == HeldWater::Redraw);
	// Off on the GPU path: the GPU state is dropped at once, whatever the step; nothing is drawn.
	CHECK(water_anim::PlanHeldWater(false, true, true, true, c, due) == HeldWater::DropGpu);
	CHECK(water_anim::PlanHeldWater(false, false, true, true, c, due) == HeldWater::DropGpu);
	// After the drop it only holds, step or no step.
	CHECK(water_anim::PlanHeldWater(false, false, false, true, c, due) == HeldWater::Hold);
	CHECK(water_anim::PlanHeldWater(false, false, false, false, c, due) == HeldWater::Hold);
	// Off on the old retake path: one draw without the layer (the kept greys held its last step) ...
	CHECK(water_anim::PlanHeldWater(false, true, false, true, c, due) == HeldWater::Redraw);
	// ... none when no water was shown, and none after that one.
	CHECK(water_anim::PlanHeldWater(false, true, false, false, c, due) == HeldWater::Hold);
	CHECK(water_anim::PlanHeldWater(false, false, false, false, c, due + 5000) == HeldWater::Hold);
	// On again while paused: it may move again when water was shown in the last draw.
	CHECK(water_anim::PlanHeldWater(true, false, false, true, c, due) == HeldWater::Redraw);
	CHECK(water_anim::PlanHeldWater(true, false, false, false, c, due) == HeldWater::Hold);
}

TEST_CASE("water anim: the Animated Water switch is on by default and is kept in the preferences")
{
	CHECK(CPreference().AnimatedWater == true);
	CHECK(Preference.AnimatedWater == true);

	InitLua();
	REQUIRE(Lua);
	// The Lua binding the Options entry and stratagus.lua use reaches the drawing flag.
	REQUIRE(luaL_dostring(Lua, "Preference.AnimatedWater = false") == 0);
	CHECK(Preference.AnimatedWater == false);
	REQUIRE(luaL_dostring(Lua, "return Preference.AnimatedWater") == 0);
	CHECK(lua_toboolean(Lua, -1) == 0);
	lua_pop(Lua, 1);
	Preference.AnimatedWater = true;

	// The choice is saved as wc2.preferences.AnimatedWater by SavePreferences (preferences.lua in the
	// user folder) and comes back on the next start.
	const std::string savedGameName = GameName;
	const fs::path savedUserDir = Parameters::Instance.GetUserDirectory();
	const fs::path dir = fs::temp_directory_path() / "wargushd_test_water_anim";
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::create_directories(dir / "wc2", ec);
	GameName = "wc2";
	Parameters::Instance.SetUserDirectory(dir);
	REQUIRE(luaL_dostring(Lua, "wc2 = {preferences = {AnimatedWater = false}}") == 0);
	SavePreferences();
	std::ifstream in(dir / "wc2" / "preferences.lua");
	std::stringstream text;
	text << in.rdbuf();
	in.close();
	CHECK(text.str().find("AnimatedWater") != std::string::npos);
	lua_close(Lua);
	Lua = nullptr;

	InitLua();
	REQUIRE(Lua);
	REQUIRE(luaL_dostring(Lua, text.str().c_str()) == 0);
	REQUIRE(luaL_dostring(Lua, "return wc2.preferences.AnimatedWater") == 0);
	CHECK(lua_isboolean(Lua, -1));
	CHECK(lua_toboolean(Lua, -1) == 0);
	lua_pop(Lua, 1);
	lua_close(Lua);
	Lua = nullptr;

	GameName = savedGameName;
	Parameters::Instance.SetUserDirectory(savedUserDir);
	fs::remove_all(dir, ec);
}

// CLASSIC-SHEET-LOOKUPS (terrain, work/TERRAIN_BGS_IMPL.md): the tileset image -> HD terrain atlas names
// used both by EnsureHDTileAtlas and by the classic-sheet skip in CclDefineTileset.
TEST_CASE("terrain sheet: HD terrain atlas names per tileset and no skip without an HD atlas")
{
	const HDTerrainNames summer = HDTerrainNamesFor("tilesets/summer/terrain/summer.png");
	CHECK(std::string(summer.bgsAtlas) == "Forest");
	CHECK(std::string(summer.bgsPrefix) == "forest");
	CHECK(std::string(summer.classicAtlas) == "classic_forest");
	CHECK(std::string(summer.classicPrefix) == "Forest/forest");
	const HDTerrainNames winter = HDTerrainNamesFor("tilesets/winter/terrain/winter.png");
	CHECK(std::string(winter.bgsAtlas) == "Ice");
	CHECK(std::string(winter.classicAtlas) == "classic_ice");
	// NG-TERR1: wasteland -> Swamp, swamp -> XSwamp ("wasteland" must win before "swamp" is tried)
	const HDTerrainNames waste = HDTerrainNamesFor("tilesets/wasteland/terrain/wasteland.png");
	CHECK(std::string(waste.bgsAtlas) == "Swamp");
	CHECK(std::string(waste.bgsPrefix) == "swamp");
	const HDTerrainNames swamp = HDTerrainNamesFor("tilesets/swamp/terrain/swamp.png");
	CHECK(std::string(swamp.bgsAtlas) == "XSwamp");
	CHECK(std::string(swamp.classicAtlas) == "classic_xswamp");
	const HDTerrainNames other = HDTerrainNamesFor("tilesets/mars/terrain/mars.png");
	CHECK(other.bgsAtlas == nullptr);
	CHECK(other.classicAtlas == nullptr);
	// an unknown tileset is never skipped: its classic sheet is searched for as before
	CHECK_FALSE(HDTerrainCoversTileset("tilesets/mars/terrain/mars.png"));
	CHECK_FALSE(HDTerrainCoversTileset(""));
}
