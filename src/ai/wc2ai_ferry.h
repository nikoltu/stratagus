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
/**@name wc2ai_ferry.h - The rules of a scripted computer's transports (pure parts). */
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

#ifndef WC2AI_FERRY_H
#define WC2AI_FERRY_H

#include "wc2ai.h"

#include <cstddef>
#include <cstdint>
#include <vector>

/// How a land unit of a wave crosses water (local note work/AI_SEA_AIR.md, from DECODE_B3 1 point 4
/// and the transport routine of role 3 read for that note; seen in part in run 32, Human XII: the
/// computer's Transports lay at the person's coast 100 s after a land wave was launched).
///   - a land unit whose wave place is on another island is held to the place (role 2) and waits;
///   - every 50 cycles a free transport of its player: calls aboard the units it was given (they
///     walk to it; it stays where it lies), else sails its load to a coast tile of the place's
///     island, else - when it is the nearest free one - takes up to 6 waiting units of one island,
///     else goes home to the water beside the nearest hall. Calibrated to live run 57 (Human XII,
///     2 land waves; work/CALIB_RUN57.md): it sails the moment the last unit is aboard.
namespace wc2ai
{

/// Units a transport takes waiting units for at once (the original's `cmp cl,6` at 0x4B5D3D).
constexpr int FerryLoad = 6;

/// The eight directions the landing walk tries, from the original's tables 0x89B100 (x) and
/// 0x89B108 (y): north, north-east, east, south-east, south, south-west, west, north-west.
constexpr int FerryStepX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int FerryStepY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

/// Islands: every tile that is not water gets the number of the 8-connected group of such tiles
/// it belongs to (1, 2, ... in the order of the first tile of each group, row by row); water 0.
/// The original keeps a connectivity number per tile (word map [0x8F6674]); whether trees, rocks
/// or walls split it there is not established - here only water does.
inline std::vector<int> LandRegions(int width, int height, const std::vector<uint8_t> &water)
{
	std::vector<int> regions;
	if (width <= 0 || height <= 0 || water.size() != static_cast<size_t>(width) * height) {
		return regions;
	}
	regions.assign(water.size(), 0);
	std::vector<size_t> open;
	int next = 0;
	for (size_t start = 0; start < water.size(); ++start) {
		if (water[start] || regions[start]) {
			continue;
		}
		regions[start] = ++next;
		open.assign(1, start);
		while (!open.empty()) {
			const size_t at = open.back();
			open.pop_back();
			const int x = static_cast<int>(at % width);
			const int y = static_cast<int>(at / width);
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const int nx = x + dx;
					const int ny = y + dy;
					if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
						continue;
					}
					const size_t near = static_cast<size_t>(ny) * width + nx;
					if (!water[near] && !regions[near]) {
						regions[near] = next;
						open.push_back(near);
					}
				}
			}
		}
	}
	return regions;
}

/// The island number of a tile; 0 for water or a tile off the map.
inline int RegionOf(const std::vector<int> &regions, int width, int height, int x, int y)
{
	if (x < 0 || y < 0 || x >= width || y >= height || regions.size() != static_cast<size_t>(width) * height) {
		return 0;
	}
	return regions[static_cast<size_t>(y) * width + x];
}

/// With script parameter 0x0C set, the original lands a load not at the place itself: from the
/// place it walks, in one of the eight directions picked at random (`firstDirection`, 0..7), tile
/// by tile to the first coast tile of the place's own island; when the walk leaves the map it
/// tries the next direction, and with none left the place itself stands (transport routine,
/// 0x4B5A95-0x4B5B63). The original tests both axes against the map's width (its maps are square);
/// here each against its own side.
inline Place LandingCoast(int width, int height, const std::vector<int> &regions, const std::vector<uint8_t> &coast,
                          int x, int y, int firstDirection)
{
	Place place;
	const int region = RegionOf(regions, width, height, x, y);
	if (region == 0 || coast.size() != static_cast<size_t>(width) * height) {
		return place;
	}
	const int first = firstDirection & 7;
	int direction = first;
	do {
		int tx = x;
		int ty = y;
		for (;;) {
			tx += FerryStepX[direction];
			ty += FerryStepY[direction];
			if (tx < 0 || ty < 0 || tx >= width || ty >= height) {
				break;
			}
			const size_t at = static_cast<size_t>(ty) * width + tx;
			if (regions[at] == region && coast[at]) {
				place.found = true;
				place.x = tx;
				place.y = ty;
				return place;
			}
		}
		direction = (direction + 1) & 7;
	} while (direction != first);
	return place;
}

/// A land unit that waits for a transport, as the empty transport sees it.
struct FerryCandidate {
	int region = 0;             ///< the island of the unit's wave place
	bool heldElsewhere = false; ///< a living transport of its player has it already
};

/// The waiting units an empty transport takes, in list order: not held by another transport, at
/// most `load`, all with the place on the island of the first one taken (transport routine,
/// 0x4B5C59-0x4B5D4B). The original writes the transport into a unit of another island too and
/// only leaves out its order; that unit is left alone here, so that one load goes to one island.
inline std::vector<size_t> FerryClaims(const std::vector<FerryCandidate> &units, int load)
{
	std::vector<size_t> taken;
	int region = 0;
	for (size_t i = 0; i < units.size() && static_cast<int>(taken.size()) < load; ++i) {
		const FerryCandidate &unit = units[i];
		if (unit.heldElsewhere || unit.region == 0) {
			continue;
		}
		if (region == 0) {
			region = unit.region;
		} else if (unit.region != region) {
			continue;
		}
		taken.push_back(i);
	}
	return taken;
}

/// The transport waiting units are given to: the one nearest them (smallest distance, the first
/// of equals); -1 for none. Live run 57 (Human XII, 2 of 2 land waves): the nearer of two
/// transports took the wave, a map-placed one as well as a trained one.
inline int FerryPickNearest(const std::vector<int> &distances)
{
	int best = -1;
	for (size_t i = 0; i < distances.size(); ++i) {
		if (best < 0 || distances[i] < distances[static_cast<size_t>(best)]) {
			best = static_cast<int>(i);
		}
	}
	return best;
}

/// Does a fighter count for conditions 4 / 5 / 6 (free land / sea / air)? Alive and on the map,
/// or aboard a transport: live run 57 (2 of 2 land waves, 4 units) saw free_land keep the units
/// through boarding, the trip and the landing, and drop only at their deaths.
constexpr bool FerryCountsFighter(bool alive, bool removed, bool aboardTransport)
{
	return alive && (!removed || aboardTransport);
}

/// Does an idle trained warship of a scripted computer patrol? Live run 57 (Human XII): every
/// trained warship patrolled until a wave took it (10 of 10); the map's own warships stood the
/// whole play (9 of 9, here map-fixed), and a unit held to a wave place (role 2) is the wave's.
constexpr bool WarshipPatrols(bool warship, bool mapFixed, int homeRole, bool idle)
{
	return warship && !mapFixed && homeRole == 0 && idle;
}

/// What a free transport of a scripted computer does at its 50-cycle turn.
enum class FerryStep : uint8_t {
	CallAboard, ///< a unit it was given stands outside: the units walk to it, it waits
	Sail,       ///< all aboard: to a coast tile of the place's island, and unload there
	Take,       ///< empty and the nearest free one to the waiting units: take them
	Home        ///< empty and nobody to take: to the water beside the nearest hall
};

constexpr FerryStep FerryNext(bool givenOutside, bool loaded, bool anyToTake)
{
	return givenOutside ? FerryStep::CallAboard
	     : loaded ? FerryStep::Sail
	     : anyToTake ? FerryStep::Take : FerryStep::Home;
}

} // namespace wc2ai

#endif // WC2AI_FERRY_H
