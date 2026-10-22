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
/**@name water_anim.h - The clock, the noise walks and the mask weights of the HD animated water. */
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

#ifndef WATER_ANIM_H
#define WATER_ANIM_H

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Owner decision 87 (design: work/CALIB_RUN55.md "Item 5 design", spec: work/WATER_ANIM.md).
// Measured on the original (work/agent-orig-52/ORIG52.md item 2, work/agent-orig-55/ORIG55.md item 13):
//   - open water changes about 8 times a second (median 123 ms, Forest 8.0 / s) and does not repeat
//     in 6 - 22 s;
//   - the shore band repeats every 4.8 - 5.1 s;
//   - the water keeps changing while the game is paused (run 52, r2W5p: ~122 ms with the frame
//     counter stopped), so this clock has no pause input;
//   - fogged water animates too (the layer is drawn before the fog).
// Only the rates and the look statistics are measured; how the original combines bgs_water_<X> and
// waterMasks.png is UNKNOWN. The combine (work/WATER_ANIM.md "Look recalibration") is ours.
// Drawing only: no game state, not saved, not part of the sync.
namespace water_anim
{

/// Real milliseconds per tick (the same 20 ms tick as palette_cycle).
constexpr int TickMs = 20;
/// Ticks per water step: 6 x 20 ms = 120 ms = 8.33 steps a second (measured ~8 / s, median 123 ms).
constexpr int StepTicks = 6;
constexpr int StepMs = TickMs * StepTicks;
/// Steps in one shore loop: 40 x 120 ms = 4.8 s (measured 4.8 - 5.1 s).
constexpr int ShoreLoopSteps = 40;
/// Steps made up at most after the loop stalled (a long stall is not made up).
constexpr int MaxCatchUpSteps = 4;

/// The noise texture is a square of this many native HD pixels (waterMasks.png is 512 x 512) and wraps.
/// It is fixed to the map: it is never moved (owner feedback 2026-10-19, see "Look" below).
constexpr int NoiseSize = 512;

/// The drawing-only water clock. Advance() with the real time of every drawn frame.
class Clock
{
public:
	void Reset()
	{
		started = false;
		lastMs = 0;
		step = 0;
	}

	/// One step per 120 ms of real time since the last step. There is no pause input: the
	/// original's water runs on while the game is paused (run 52).
	void Advance(uint32_t nowMs)
	{
		if (!started) {
			started = true;
			lastMs = nowMs;
			return;
		}
		const uint32_t elapsed = nowMs - lastMs;
		uint32_t steps = elapsed / static_cast<uint32_t>(StepMs);
		if (steps == 0) {
			return;
		}
		if (steps > static_cast<uint32_t>(MaxCatchUpSteps)) {
			step += MaxCatchUpSteps;
			lastMs = nowMs; // a long stall is not made up
			return;
		}
		step += steps;
		lastMs += steps * static_cast<uint32_t>(StepMs);
	}

	/// Would Advance(nowMs) make a new step? (No change to the clock.)
	bool StepDue(uint32_t nowMs) const
	{
		return started && nowMs - lastMs >= static_cast<uint32_t>(StepMs);
	}

	/// Steps since the clock started (the open-water phase).
	uint32_t Step() const { return step; }
	/// The shore phase, 0 .. ShoreLoopSteps - 1.
	int ShorePhase() const { return static_cast<int>(step % ShoreLoopSteps); }

private:
	bool started = false;
	uint32_t lastMs = 0;
	uint32_t step = 0;
};

/// The one water clock of the running program (drawing only).
inline Clock &Game()
{
	static Clock clock;
	return clock;
}

/// PERF-A3a: a paused game under an in-game window is held (its grey copy is laid over and the game
/// is not drawn). The original's water moves on there (run 52), so the held frame is drawn and taken
/// again when, and only when, the water has a new step to show: about 8.3 times a second, never every
/// frame. `waterShown`: the switch is on and the layer was added in the last drawn frame.
inline bool RedrawHeldFrame(bool waterShown, const Clock &clock, uint32_t nowMs)
{
	return waterShown && clock.StepDue(nowMs);
}

/// What a held paused frame does with the water this frame (WATER_ANIM.md section 14).
enum class HeldWater {
	Hold,     // lay the kept greys only
	AddOnGpu, // lay the kept greys and add the water on the GPU (section 13.3)
	Redraw,   // draw the game once and take its greys again
	DropGpu   // drop the GPU water state, then hold: its kept greys were taken without the layer
};

/// `animated`: Preference.AnimatedWater now. `switchedOff`: it was on in the previous frame.
/// `onGpu`: the GPU water state is Ready. `waterShown`: the layer was added in the last drawn frame.
/// Switched off under the window: the water stops at once. The GPU path's kept greys have no layer,
/// so they are laid alone (no draw); the old retake's kept greys hold the last step, so they are
/// taken once more with the layer off. Switched on again: the old retake path, when water was shown.
inline HeldWater PlanHeldWater(bool animated, bool switchedOff, bool onGpu, bool waterShown, const Clock &clock,
                               uint32_t nowMs)
{
	if (!animated) {
		if (onGpu) {
			return HeldWater::DropGpu;
		}
		return switchedOff && waterShown ? HeldWater::Redraw : HeldWater::Hold;
	}
	if (onGpu) {
		return HeldWater::AddOnGpu;
	}
	return RedrawHeldFrame(waterShown, clock, nowMs) ? HeldWater::Redraw : HeldWater::Hold;
}

/// Mask weights of one bgs_water_<X> pixel (RAW reading, work/WATER_ANIM.md section 2): alpha 0 with
/// blue 255 is water (the deep-water tiles are that over the whole frame), alpha 255 with red is
/// land, blue under alpha is the shore band at the coast. Open = blue x (1 - alpha), shore =
/// blue x alpha, both 0 .. 255.
inline uint8_t OpenWeight(uint8_t b, uint8_t a)
{
	return static_cast<uint8_t>((b * (255 - a) + 127) / 255);
}
inline uint8_t ShoreWeight(uint8_t b, uint8_t a)
{
	return static_cast<uint8_t>((b * a + 127) / 255);
}

// ---- Look (recalibrated 2026-10-19 on owner feedback, work/WATER_ANIM.md "Look recalibration") ----
// Measured on the original (run 55 wF1, 32 px a megatile): the picture never moves; every pixel of
// open water brightens now and then in place (rise and fall ~0.5 s each, above half its peak ~14 %
// of the time), only brightening, towards the water's own hue; a step changes 1 - 2 px spots and
// fine streaks over all the water. The shore band (within ~20 native px of the coast) lights much
// more strongly on a ~4.9 s loop.
// Ours (section 11, 2026-10-19): nothing is ever shifted; each map is tiled at its own period.
//   open water = open weight x twinkle layer (an integer hash per texel, a quarter of the texels
//                twinkle once a 4.8 s loop, the rest exactly 0; copies mirrored by place) x crest
//                map (waterMasks.png green, period CrestPeriod) x gate (red, ranked even, period
//                GatePeriod, scales lit twinkles only);
//   shore      = shore weight x shore layer (blue, ranked even, thin lines on a 4.8 s loop).
// Each step only the tables move; a pixel lights when its phase meets the step's offset.

/// The CPU layers (shore, crest) are this many pixels square for one NoiseSize square.
constexpr int LayerSize = 256;
/// The gate layer is coarser (the red channel is large-scale noise).
constexpr int GateSize = 128;
/// The open-water twinkle layer: one texel = 4 native HD px (about one target pixel on the S8U),
/// drawn with the nearest filter so a twinkle never spreads into its neighbours.
constexpr int TwinkleSize = 128;
/// Tiling periods of the crest and the gate maps, in noise squares (not a ratio of small integers,
/// so the three maps line up again only far away).
constexpr float CrestPeriod = 1.37f;
constexpr float GatePeriod = 1.71f;

/// Phase offset of a step on a loop of `period` steps, in 1 / 65536 of a turn.
inline uint32_t PhaseOffset(uint32_t step, uint32_t period)
{
	return static_cast<uint32_t>((uint64_t(step % period) << 16) / period);
}

/// A triangle pulse over a 16-bit phase: 255 at phase 0, 0 at `width` / 2 turns (width 65536 = a
/// whole turn) and beyond.
inline uint8_t Pulse(uint32_t phase, uint32_t width)
{
	phase &= 0xFFFFu;
	const uint32_t d = phase < 0x8000u ? phase : 0x10000u - phase;
	const uint32_t half = width / 2;
	if (half == 0 || d >= half) {
		return 0;
	}
	return static_cast<uint8_t>(255 - d * 255 / half);
}

/// The pulse value of every 0 .. 255 phase-map value at a step (one table per step and layer).
/// `turns`: phase turns over the map's 0 .. 255 range (more turns = thinner, denser lines).
inline void PulseTable(uint32_t step, uint32_t period, uint32_t turns, uint32_t width, uint8_t table[256])
{
	const uint32_t off = PhaseOffset(step, period);
	for (uint32_t v = 0; v < 256; ++v) {
		table[v] = Pulse(v * turns * 256 + off, width);
	}
}

/// waterMasks.png's channels are bell-shaped (blue mean 128 sd 37, red 70 / 35). Used as phases they
/// lit very few pixels on some steps and half the water on others (desk simulation of 3.4.69: 1.6 -
/// 57 % of open water changing >= 1 level, device 3 - 64 %). Ranked to an even spread (each value
/// replaced by its mid-rank, 0 .. 255) the share lit is the same on every step; the shapes stay.
inline void Equalize(uint8_t *map, size_t n)
{
	if (n == 0) {
		return;
	}
	uint64_t count[256] = {};
	for (size_t i = 0; i < n; ++i) {
		++count[map[i]];
	}
	uint8_t lut[256];
	uint64_t below = 0;
	for (int v = 0; v < 256; ++v) {
		lut[v] = static_cast<uint8_t>(std::min<uint64_t>(255, (below + count[v] / 2) * 256 / n));
		below += count[v];
	}
	for (size_t i = 0; i < n; ++i) {
		map[i] = lut[map[i]];
	}
}

/// The twinkle phase of texel (x, y) of the open-water layer: an integer hash, even over 0 .. 255
/// and independent from texel to texel, so each twinkle is its own 1 - 2 px spot (a smooth phase map
/// lit whole bands at once: spots up to 250 px on the device, original max 10).
inline uint8_t TwinkleHash(uint32_t x, uint32_t y)
{
	uint32_t h = x * 374761393u + y * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return static_cast<uint8_t>((h ^ (h >> 16)) & 0xFFu);
}

/// How each copy of the twinkle square is mirrored (bit 0 = across x, bit 1 = across y), from the
/// copy's place on the map grid: the copies no longer repeat every 228 screen px (3.4.69 change map
/// correlated 0.30 at that offset).
inline int TwinkleFlip(int64_t ix, int64_t iy)
{
	uint32_t h = static_cast<uint32_t>(ix) * 73856093u ^ static_cast<uint32_t>(iy) * 19349663u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return static_cast<int>((h ^ (h >> 16)) & 3u);
}

/// Open water: each pixel whose twinkle value is below OpenTwinkleValues (64 of 256 = a quarter of
/// the texels) twinkles once a loop of 40 steps (4.8 s; measured near-repeat 4.9 s): it rises over
/// 4 steps and falls over 4 (pulse 0.2 turn wide; measured rise and fall ~0.5 s each) and is exactly
/// 0 the rest of the time, so every other pixel stays unchanged step to step. The gate (OpenGateMin
/// .. 255 over 200 steps = 24 s) only scales a lit twinkle: the open water repeats first after 24 s
/// (measured series of 22 s did not repeat), and it never touches an unlit pixel.
constexpr uint32_t OpenPulseSteps = 40;
constexpr uint32_t OpenPulseWidth = 13107; // 0.2 turn = 8 steps
constexpr uint32_t OpenTwinkleValues = 64;
constexpr uint32_t OpenGateSteps = 200;
constexpr int OpenGateMin = 128;           // the gate keeps at least half
constexpr int OpenBase = 0;                // relative strength of a twinkle between the crests: none.
                                           // At 1 those twinkles peak at ~2 levels and their sub-level
                                           // steps, over the textured water in 3 tinted channels, were
                                           // the device's >= 1 excess (WATER_ANIM.md section 12)
constexpr int OpenCrest = 100;             // relative strength on a full crest
constexpr int CrestFloor = 61;             // waterMasks.png green: the flat floor between crests

/// The crest map texel: the twinkle strength OpenBase .. OpenBase + OpenCrest as 0 .. 255
/// (multiplied in; the twinkle layer carries the full 255).
inline uint8_t CrestTexel(uint8_t green)
{
	const int c = green > CrestFloor ? (green - CrestFloor) * OpenCrest / (255 - CrestFloor) : 0;
	return static_cast<uint8_t>((OpenBase + c) * 255 / (OpenBase + OpenCrest));
}

/// The twinkle table of a step: values below OpenTwinkleValues are spread evenly over the loop,
/// the others never light.
inline void TwinkleTable(uint32_t step, uint8_t table[256])
{
	const uint32_t off = PhaseOffset(step, OpenPulseSteps);
	for (uint32_t v = 0; v < 256; ++v) {
		table[v] = v < OpenTwinkleValues ? Pulse((v << 16) / OpenTwinkleValues + off, OpenPulseWidth) : 0;
	}
}

/// The gate table of a step: OpenGateMin .. 255 over the (equalized) red phase map, one slow turn.
inline void GateTable(uint32_t step, uint8_t table[256])
{
	PulseTable(step, OpenGateSteps, 1, 0x10000u, table);
	for (int v = 0; v < 256; ++v) {
		table[v] = static_cast<uint8_t>(OpenGateMin + (255 - OpenGateMin) * table[v] / 255);
	}
}

/// Shore band: a loop of ShoreLoopSteps (4.8 s), 2 phase turns over the map (thin moving lines), the
/// pulse 12 % of the loop wide, up to ShoreStrength levels where the shore weight is full.
constexpr uint32_t ShorePhaseTurns = 2;
constexpr uint32_t ShorePulseWidth = 7864; // 0.12 turn
constexpr int ShoreStrength = 100;

/// The light added follows the water's own hue: measured brightening (brightest frame - darkest
/// frame, mean over the changing water pixels) per tileset, scaled to a largest channel of 255.
/// Forest wF1 (5.8, 15.4, 21.1); winter / Ice wW1 (9.8, 17.6, 23.2); wasteland / Swamp r2W4
/// (1.2, 9.8, 12.7); swamp / XSwamp wS1 (10.0, 13.1, 5.8).
struct Tint {
	uint8_t r = 255;
	uint8_t g = 255;
	uint8_t b = 255;
};
inline bool SameName(const char *a, const char *b)
{
	if (!a || !b) {
		return false;
	}
	for (; *a && *b; ++a, ++b) {
		const char x = (*a >= 'A' && *a <= 'Z') ? char(*a - 'A' + 'a') : *a;
		const char y = (*b >= 'A' && *b <= 'Z') ? char(*b - 'A' + 'a') : *b;
		if (x != y) {
			return false;
		}
	}
	return *a == 0 && *b == 0;
}
inline Tint TintFor(const char *bgsName)
{
	if (SameName(bgsName, "Ice")) {
		return {108, 193, 255};
	}
	if (SameName(bgsName, "Swamp")) {
		return {24, 197, 255};
	}
	if (SameName(bgsName, "XSwamp")) {
		return {195, 255, 113};
	}
	return {70, 186, 255}; // Forest, and any other
}
/// The shore layer is tuned in luminance levels; a tint darker than white adds less luminance, so
/// it is raised by 765 / (r + g + b) (8.8 fixed point). The open layer is tuned on the largest
/// channel (255 in every tint) and takes no gain.
inline uint32_t TintGain(const Tint &t)
{
	const uint32_t sum = uint32_t(t.r) + t.g + t.b;
	return sum ? 765u * 256u / sum : 256u;
}

/// The open-water twinkle layer of a step: n texels of the twinkle map (TwinkleHash), 0 .. 255.
/// Calibrated on the largest colour channel, which is 255 in every tint, so no tint gain here.
/// The crest and the gate maps are multiplied in later.
inline void BuildOpenLayer(const uint8_t *twinkle, size_t n, uint32_t step, uint8_t *out)
{
	uint8_t pulse[256];
	TwinkleTable(step, pulse);
	for (size_t i = 0; i < n; ++i) {
		out[i] = pulse[twinkle[i]];
	}
}

/// The gate layer of a step: n pixels of the red phase map, OpenGateMin .. 255.
inline void BuildGateLayer(const uint8_t *phase, size_t n, uint32_t step, uint8_t *out)
{
	uint8_t gate[256];
	GateTable(step, gate);
	for (size_t i = 0; i < n; ++i) {
		out[i] = gate[phase[i]];
	}
}

/// The shore layer of a step (levels to add, before the mask): it repeats every ShoreLoopSteps.
inline void BuildShoreLayer(const uint8_t *phase, size_t n, uint32_t step, uint32_t gain, uint8_t *out)
{
	uint8_t pulse[256];
	PulseTable(step, ShoreLoopSteps, ShorePhaseTurns, ShorePulseWidth, pulse);
	const uint32_t peak = std::min<uint32_t>(255u, ShoreStrength * gain / 256u);
	for (size_t i = 0; i < n; ++i) {
		out[i] = static_cast<uint8_t>(peak * pulse[phase[i]] / 255u);
	}
}

/// The layer's render targets are this fraction of the viewport (1/4 of the memory and of the fill
/// of a full-size target).
constexpr float TargetScale = 0.5f;

} // namespace water_anim

#endif // WATER_ANIM_H
