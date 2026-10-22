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
/**@name missile_cadence.h - The hit cadence of the spell missiles. */
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

#ifndef MISSILE_CADENCE_H
#define MISSILE_CADENCE_H

// The original runs a missile's handler when its picture script (Data/Rez/script.bin) yields: for a
// flame of the Flame Shield on every game cycle, for the Whirlwind on every third. A flame makes its
// area hit when (count & 15) == 1, the Whirlwind on every second handler call (800 calls in all).

/// A flame of the Flame Shield makes its area hit on this count
inline bool FlameShieldHitsOn(int count)
{
	return (count & 15) == 1;
}

// A flame of the Flame Shield goes round its unit on a ring of 51 steps, one step on every call,
// with a step counter of its own (the five flames start on steps 0, 10, 20, 30, 40). The ring, in
// pixels of the classic 32 pixel tile from the centre of the unit to the flame's point, is an
// ellipse of half axes 30.25 and 23 about the point (2, -11), walked clockwise from one pixel
// left of its top so that every step is 3 pixels on the axis the flame moves faster along. It
// is worked out once, in whole numbers only, so that it is the same on every machine.

/// Steps of the ring of a Flame Shield flame
constexpr int FlameShieldSteps = 51;
/// Calls each picture of a flame is held
constexpr int FlameShieldPictureCalls = 2;

/// The step after @p step on the ring (a step outside the ring starts it again)
constexpr int FlameShieldNextStep(int step)
{
	return step < 0 || step + 1 >= FlameShieldSteps ? 0 : step + 1;
}

/// The whole root of @p value
constexpr long long FlameShieldRoot(long long value)
{
	long long root = 0;
	for (long long bit = 1LL << 31; bit != 0; bit >>= 1) {
		if ((root + bit) * (root + bit) <= value) {
			root += bit;
		}
	}
	return root;
}

/// The ring: x and y per step, in pixels of the classic tile
struct FlameShieldRingPoints {
	int x[FlameShieldSteps];
	int y[FlameShieldSteps];
};

inline FlameShieldRingPoints MakeFlameShieldRing()
{
	// all lengths in 64ths of a pixel, places counted from the middle of the ellipse
	constexpr long long unit = 64;
	constexpr long long halfX = 30 * unit + unit / 4;
	constexpr long long halfY = 23 * unit;
	constexpr long long middleX = 2 * unit;
	constexpr long long middleY = -11 * unit;
	constexpr long long step = 3 * unit;
	// how far from the middle the ellipse is on the other axis, `v` from the middle on the axis of `half`
	const auto across = [](long long v, long long half, long long other) {
		return FlameShieldRoot(other * other * (half * half - v * v)) / half;
	};
	const auto pixel = [](long long v) {
		const long long shifted = v + unit / 2;
		return static_cast<int>(shifted >= 0 ? shifted / unit : -((-shifted + unit - 1) / unit));
	};
	FlameShieldRingPoints ring{};
	long long x = -unit;
	long long y = -across(x, halfX, halfY);
	for (int i = 0; i < FlameShieldSteps; ++i) {
		ring.x[i] = pixel(middleX + x);
		ring.y[i] = pixel(middleY + y);
		// clockwise on the screen: the upper half goes right, the right side goes down
		const long long nextX = x + (y < 0 ? step : -step);
		const long long nextY = y + (x > 0 ? step : -step);
		const bool alongX = nextX >= -halfX && nextX <= halfX;
		const bool alongY = nextY >= -halfY && nextY <= halfY;
		const long long yOfX = alongX ? (y < 0 ? -across(nextX, halfX, halfY) : across(nextX, halfX, halfY)) : 0;
		const long long xOfY = alongY ? (x > 0 ? across(nextY, halfY, halfX) : -across(nextY, halfY, halfX)) : 0;
		const long long movedY = yOfX > y ? yOfX - y : y - yOfX;
		const long long movedX = xOfY > x ? xOfY - x : x - xOfY;
		// the step that moves the other axis less is the one along the faster axis
		if (alongX && (!alongY || movedY <= movedX)) {
			x = nextX;
			y = yOfX;
		} else {
			x = xOfY;
			y = nextY;
		}
	}
	return ring;
}

/// Offset of a flame's point from the centre of its unit on @p step of the ring, in pixels of a
/// tile of @p tileSize pixels; @p vertical picks the y offset
inline int FlameShieldOffset(int step, int tileSize, bool vertical)
{
	static const FlameShieldRingPoints ring = MakeFlameShieldRing();
	const int index = step < 0 ? 0 : step % FlameShieldSteps;
	return (vertical ? ring.y[index] : ring.x[index]) * tileSize / 32;
}

// A rune is seen only as a flash on every 256th cycle of its life: pictures 3 2 1 0 1 2 3 held
// for 2 3 2 3 2 3 2 cycles, 17 in all, then nothing.

/// Cycles a rune's flash lasts
constexpr int RuneFlashCycles = 17;

/// Where a rune with @p ttl cycles left is in its period of 256 (a life of 2048: 1 on the first call)
constexpr int RuneFlashPhase(int ttl)
{
	return (256 - (ttl & 255)) & 255;
}

/// The picture a rune shows on @p phase of its period; -1 = not drawn
constexpr int RuneFlashPicture(int phase)
{
	return phase < 0 || phase >= RuneFlashCycles ? -1
	       : phase < 2 ? 3 : phase < 5 ? 2 : phase < 7 ? 1 : phase < 10 ? 0 : phase < 12 ? 1 : phase < 15 ? 2 : 3;
}

/// The Whirlwind makes its area hit on this count: every second handler call, a call every 3 cycles
inline bool WhirlwindHitsOn(int count)
{
	return count % 6 == 0;
}

/// Game cycles the Whirlwind lives: 800 handler calls, a call every 3 cycles
constexpr int WhirlwindLifeCycles = 800 * 3;

// A flying missile of the original advances its type's speed, in pixels of the classic 32 pixel
// tile, along the longer axis of its line on every game cycle (the shorter axis follows the
// line). A missile of a flyer and the Fireball go at half of it inside the last 50 pixels before
// the aimed point and on the legs behind it.

/// Pixels of the classic tile inside which a bouncing missile slows down before its aimed point
constexpr int MissileSlowDistance = 50;

/// Step of a flying missile for one cycle, in pixels along its line of @p length pixels whose
/// longer axis spans @p major pixels; @p speed is in pixels of the classic tile
constexpr int MissileFlightStep(int speed, int tileSize, int major, int length, bool slow)
{
	const int perCycle = (slow ? speed >> 1 : speed) * tileSize / 32;
	const int step = major > 0 ? perCycle * length / major : perCycle;
	return speed > 0 && step < 1 ? 1 : step;
}

/// Cycles a missile flies over @p major pixels of the longer axis of its line
constexpr int MissileFlightCycles(int speed, int tileSize, int major)
{
	const int perCycle = speed * tileSize / 32;
	return perCycle > 0 ? (major + perCycle - 1) / perCycle : 0;
}

// The original makes the explosion (missile type 23) of a unit that dies in it (0x4D7BE8, 0x4D7C08)
// and of a critter clicked to death (0x4B1333) 8 pixels of the classic 32 pixel tile above the
// unit's centre (0x49B790: y = unit y + the type's centre offset - 8).

/// Pixels above the unit's centre at which a unit's own explosion is made
constexpr int UnitExplosionLift(int tileSize)
{
	return 8 * tileSize / 32;
}

// The fire over a burning building stands above the centre of its footprint by a height that
// depends on the building's size, not one tile, and is the same in both views (5 cases, live runs
// 58 and S2B): Farm 2 x 2 24 classic px (Remastered -72.3 px at 96 a tile; classic frame match
// -24); Barracks 3 x 3 20 (Remastered -59.8 px); Town Hall 4 x 4 26 (classic o11_68 / o11_69 /
// o11_71 -26; Remastered -75.8 px = 25.3, one weak match). Not measured: 1 x 1 (taken as 2 x 2)
// and 5 x 5 or more (taken as 4 x 4); the 3 x 3 in the classic view.

/// Classic pixels (of the 32 pixel tile) above the footprint centre of the fire of a burning
/// building `tilesHigh` tiles high
constexpr int BurningFlameLiftClassicPx(int tilesHigh)
{
	return tilesHigh <= 2 ? 24 : (tilesHigh == 3 ? 20 : 26);
}

/// The same in map pixels, for a tile of `tileSize` pixels
constexpr int BurningFlameLift(int tilesHigh, int tileSize)
{
	return BurningFlameLiftClassicPx(tilesHigh) * tileSize / 32;
}

// The picture script of the Death and Decay missile: pictures 1 .. 7 with waits 3 2 3 2 3 2 3,
// picture 0, the handler, wait 8, and again from picture 2. The handler is called on tick 18 and
// then on every 24th tick; the missile ends with its 10th call. Each call makes one area hit.

/// Tick of the first area hit of a Death and Decay missile, counted from 0
constexpr int DeathAndDecayFirstHit = 18;
/// Ticks from one area hit of a Death and Decay missile to the next
constexpr int DeathAndDecayPeriod = 24;
/// Area hits of one Death and Decay missile
constexpr int DeathAndDecayRounds = 10;

/// A Death and Decay missile makes its area hit on this tick of its life
constexpr bool DeathAndDecayHitsOn(int tick)
{
	return tick >= DeathAndDecayFirstHit
	    && tick < DeathAndDecayFirstHit + DeathAndDecayRounds * DeathAndDecayPeriod
	    && (tick - DeathAndDecayFirstHit) % DeathAndDecayPeriod == 0;
}

/// Picture of a Death and Decay missile on a tick of a round of 24; a round that follows
/// another one begins with the picture the script left
constexpr int DeathAndDecayPictureOn(int tick, bool laterRound)
{
	return tick < 3 ? (laterRound ? 0 : 1)
	     : tick < 5 ? 2
	     : tick < 8 ? 3
	     : tick < 10 ? 4
	     : tick < 13 ? 5
	     : tick < 15 ? 6
	     : tick < 18 ? 7
	     : 0;
}

// The cross shown where an order is given, as the original draws it: the whole X (picture 3) held
// 6 cycles first, then cracked (2) 3, broken (1) 2, shattered (0) 1, 12 in all, then the missile is
// gone. The script reads 0 1 2 3 2 1 0 held 1 1 1 6 1 1 1, but the original's screen shows no
// build-up before the whole X; the screen is followed here.

/// Cycles the order cross is shown
constexpr int ClickCrossCycles = 12;

/// Picture of the order cross on a tick of its life, counted from 0; -1 = gone
constexpr int ClickCrossPictureOn(int tick)
{
	return tick < 0 || tick >= ClickCrossCycles ? -1
	     : tick < 6 ? 3
	     : tick < 9 ? 2
	     : tick < 11 ? 1
	     : 0;
}

#endif
