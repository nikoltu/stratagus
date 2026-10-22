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
/**@name palette_cycle.h - The original's palette-cycle steps that time the HD glow pictures. */
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

#ifndef PALETTE_CYCLE_H
#define PALETTE_CYCLE_H

#include <cstdint>

// The original keeps 8 palette-cycle records (0x93DE58, 16 bytes each), set up per tileset from the table
// at 0x8A0460 (0x56D790) and stepped on a real-time tick of 20 ms, not on game cycles, never while the
// game is paused (0x4AE5E1). A tick (0x56D850 -> 0x56DAD0): when any active record's countdown is at 1 or
// below, every active record whose countdown after the -1 is at 2 or below steps once (wrapping after its
// last palette entry) and reloads period + 1. The Remastered sprites with a "cycle" (Data/Art/hd/sprites.json)
// show the picture of that record's step: the Dark Portal and Runestone glow, the sunk-ship rings, the
// winter Church and Alchemist. Drawing only: no game state, not saved, not part of the sync.
namespace palette_cycle
{

constexpr int RecordCount = 8;
/// Real milliseconds per tick.
constexpr int TickMs = 20;
/// Ticks made up at most after the loop stalled.
constexpr int MaxCatchUpTicks = 10;

enum class Tileset : uint8_t { Forest = 0, Winter = 1, Wasteland = 2, Swamp = 3 };
constexpr int TilesetCount = 4;

/// One record of a tileset's table: its period in ticks and the number of palette entries it rotates
/// (last - first + 1). 0 entries: not used by any picture the port draws (the steps still count).
struct RecordSpec {
	uint8_t period = 0;
	uint8_t entries = 0;
};

/// Records 1..5 by tileset (record 0, the Circle of Power, is switched on by the unit, not by the
/// tileset; records 6 and 7 are unused). Record 3 is palette 0xF0..0xF4 everywhere; swamp record 4 is
/// 0x05..0x09, winter record 4 is 0xCD..0xCF (Church / Alchemist).
constexpr RecordSpec Table[TilesetCount][RecordCount] = {
	// forest: active mask 0x1E
	{{}, {8, 0}, {12, 0}, {20, 5}, {8, 0}, {}, {}, {}},
	// winter: 0x3E
	{{}, {10, 0}, {12, 0}, {20, 5}, {20, 3}, {8, 0}, {}, {}},
	// wasteland: 0x1E
	{{}, {12, 0}, {8, 0}, {20, 5}, {8, 0}, {}, {}, {}},
	// swamp: 0x3E
	{{}, {12, 0}, {8, 0}, {20, 5}, {8, 5}, {8, 0}, {}, {}},
};
constexpr uint8_t ActiveMask[TilesetCount] = {0x1E, 0x3E, 0x1E, 0x3E};

class Scheduler
{
public:
	/// Set up for a tileset (mission start): every step at 0, every countdown at its period.
	void Setup(Tileset tileset)
	{
		const int t = static_cast<int>(tileset);
		mask = ActiveMask[t];
		for (int r = 0; r < RecordCount; ++r) {
			spec[r] = Table[t][r];
			countdown[r] = spec[r].period;
			step[r] = 0;
		}
		lastMs = 0;
		clockStarted = false;
	}

	/// One 20 ms tick.
	void Tick()
	{
		bool due = false;
		for (int r = 0; r < RecordCount; ++r) {
			if (Active(r) && countdown[r] <= 1) {
				due = true;
			}
		}
		for (int r = 0; r < RecordCount; ++r) {
			if (!Active(r)) {
				continue;
			}
			--countdown[r];
			if (due && countdown[r] <= 2) {
				++step[r];
				if (spec[r].entries != 0 && step[r] >= spec[r].entries) {
					step[r] = 0;
				}
				countdown[r] = spec[r].period + 1;
			}
		}
	}

	/// The real-time clock: one tick per 20 ms since the last one, none while paused (the time paused
	/// is not made up).
	void Advance(uint32_t nowMs, bool paused)
	{
		if (!clockStarted || paused) {
			lastMs = nowMs;
			clockStarted = true;
			return;
		}
		int made = 0;
		while (nowMs - lastMs >= static_cast<uint32_t>(TickMs) && made < MaxCatchUpTicks) {
			Tick();
			lastMs += TickMs;
			++made;
		}
		if (nowMs - lastMs >= static_cast<uint32_t>(TickMs)) {
			lastMs = nowMs; // a long stall is not made up
		}
	}

	/// The step of a record (0 for an inactive or unknown one).
	int Step(int record) const
	{
		return record >= 0 && record < RecordCount ? step[record] : 0;
	}

private:
	bool Active(int r) const { return (mask >> r) & 1; }

	uint8_t mask = 0;
	RecordSpec spec[RecordCount] = {};
	int countdown[RecordCount] = {};
	int step[RecordCount] = {};
	uint32_t lastMs = 0;
	bool clockStarted = false;
};

/// The one schedule of the running game (drawing only).
inline Scheduler &Game()
{
	static Scheduler scheduler;
	return scheduler;
}

} // namespace palette_cycle

/// The step of palette-cycle record `record` in the running game.
inline int PaletteCycleStep(int record)
{
	return palette_cycle::Game().Step(record);
}

#endif // PALETTE_CYCLE_H
