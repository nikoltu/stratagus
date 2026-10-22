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
/**@name idle_bob.h - The idle bob of a standing flyer. */
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


#ifndef IDLE_BOB_H
#define IDLE_BOB_H

#include <cstdint>

// The original's idle handler runs once per 5 frames of a standing unit (the Still script loops
// "wait 4" + 1). For a ship or a flyer it counts a per-unit timer down; at 0 the timer is set
// again to 8 + (random & 7) calls and a 1-pixel vertical offset toggles 0 <-> 1: the unit rests
// 40, 45 .. 75 frames between two toggles, a new rest each time, every unit on its own count.
// Leaving the idle order does not reset the offset.
//
// The port keeps the bob as drawing only: the state below is not simulation state (nothing of
// the game reads it, it is not in the sync hash and not saved), and its random is a hash of the
// unit's slot and the game cycle, not SyncRand, so the lockstep random stream is not touched.
// A loaded game starts every flyer with the offset 0 and a fresh timer.

/// Frames between two calls of the idle handler
constexpr int IdleBobCallFrames = 5;

/// The offset in map pixels: one pixel of the original at 32-pixel tiles
constexpr int IdleBobPixels(int pixelTileHeight)
{
	return pixelTileHeight >= 32 ? pixelTileHeight / 32 : 1;
}

/// Per-unit bob state (drawing only)
struct IdleBobState
{
	uint8_t Frame = 0;  /// frames since the last handler call
	uint8_t Timer = 0;  /// handler calls until the next toggle; 0 = not started
	uint8_t Offset = 0; /// 0 or 1: the sprite is drawn one original pixel lower
};

/// The drawing-only random: a mix of the unit's slot and the game cycle
inline uint32_t IdleBobHash(uint32_t slot, uint32_t cycle)
{
	uint32_t h = slot * 0x9E3779B1u ^ (cycle + 0x7F4A7C15u);
	h ^= h >> 16;
	h *= 0x85EBCA6Bu;
	h ^= h >> 13;
	h *= 0xC2B2AE35u;
	h ^= h >> 16;
	return h;
}

/// The rest in handler calls: 8 + (random & 7), as the original
inline uint8_t IdleBobRestCalls(uint32_t slot, uint32_t cycle)
{
	return static_cast<uint8_t>(8 + (IdleBobHash(slot, cycle) & 7));
}

/// One game cycle of a flyer in the idle order. Returns true when the offset toggled.
/// The first handler call of a fresh state draws a rest without a toggle, so units created or
/// loaded together do not bob in step.
inline bool IdleBobCycle(IdleBobState &bob, uint32_t slot, uint32_t cycle)
{
	if (++bob.Frame < IdleBobCallFrames) {
		return false;
	}
	bob.Frame = 0;
	if (bob.Timer == 0) {
		bob.Timer = IdleBobRestCalls(slot, cycle);
		return false;
	}
	if (--bob.Timer != 0) {
		return false;
	}
	bob.Timer = IdleBobRestCalls(slot, cycle);
	bob.Offset = bob.Offset ? 0 : 1;
	return true;
}

#endif // !IDLE_BOB_H
