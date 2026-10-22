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
/**@name goal_pull.h - A goal on a tile the unit cannot enter is pulled toward the unit. */
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

#ifndef GOAL_PULL_H
#define GOAL_PULL_H

#include "vec2i.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

class CUnit;

// When a move, an attack-move to a point or a patrol leg becomes the unit's order, the original
// (routine 0x4C2630, called from the order setter 0x4D8970 via 0x4C2880) walks a line from the
// ordered tile back toward the unit, one step at a time, and stops at the first tile the unit
// could stand on: free for its movement kind (terrain and buildings; standing units are ignored)
// and, for every unit but a flyer, in the unit's own region of the map's region map. The goal
// becomes the tile walked just before that one, i.e. the last blocked tile. A free ordered tile
// stays the goal; a line blocked all the way to the unit leaves the ordered tile as the goal.

/// The original's line (0x4B7890 set-up, 0x4B7630 step): set up from `from` toward `to`; each
/// Next() is one step of that line, starting at `from`.
class GoalPullLine
{
public:
	GoalPullLine(const Vec2i &from, const Vec2i &to);
	/// The next step (each axis -1, 0 or +1).
	Vec2i Next();

private:
	int major = 0;
	int minor = 0;
	int err = 0;
	bool xMajor = true;
	int sx = 1;
	int sy = 1;
};

/// The pull itself, with the map reached through two tests. `isFree(P)`: the unit's movement
/// kind may stand on P. `inRegion(P)`: P lies in the unit's own region (not asked for a flyer).
Vec2i PullGoalOnLine(const Vec2i &unitPos, const Vec2i &goal, bool needRegion,
                     const std::function<bool(const Vec2i &)> &isFree,
                     const std::function<bool(const Vec2i &)> &inRegion);

/// The pull for a unit on the current map: the unit's movement mask without the bits of standing
/// units, the region map of the running game (non-flyers only).
Vec2i PullGoalTowardUnit(const CUnit &unit, const Vec2i &goal);

// The original's path step (0x4D5E20) first walks the same line from the unit toward the goal
// (0x4D56C0; for a unit on the two-tile grid the goal is floored to the grid and the step is two
// tiles). The first tile the unit cannot enter stops the line; the line tiles from there to the goal
// (the goal excluded) are marked for the wall tracer, which follows the wall until it steps on one of
// them. When the very first step is blocked and no marked tile is free, there is no way at all: the
// stored way is empty and the order drops to idle where the unit stands (0x4C2A9B -> 0x4D8720).

/// The path step's "no way" with the map reached through one test. `isFree(P)`: the unit's movement
/// kind may stand on P. True when the goal (floored to the grid) is not the unit's own grid tile,
/// cannot be entered, and every tile between the unit and the goal on the pull's line (set up from
/// the unit, walked from the goal) is blocked too.
bool PathLineFindsNoWay(const Vec2i &unitPos, const Vec2i &goal, int gridStep,
                        const std::function<bool(const Vec2i &)> &isFree);

/// Live run 69 F (3 group + 3 single trials, 30 unit orders onto forest): a unit whose (pulled)
/// goal cannot be entered does not move when none of the goal's 8 neighbours is free (no terrain,
/// building or unit on it) and reachable for the unit (`inRegion`: by terrain and buildings); the
/// unit's own tile does not count. True for such a goal.
bool GoalHasNoFreeNeighbour(const Vec2i &unitPos, const Vec2i &goal,
                            const std::function<bool(const Vec2i &)> &isFree,
                            const std::function<bool(const Vec2i &)> &inRegion);

/// Host tests: land units of one tile take the "no way" from GoalHasNoFreeNeighbour (live run 69,
/// on in the game); off: from PathLineFindsNoWay as before.
extern bool PathStepNoWayByGoalNeighbours;

/// The same for a unit on the current map: terrain, buildings and units standing on the line block
/// it (the unit's whole movement mask, unlike the pull). Never for a flyer. A land unit on the
/// one-tile grid: GoalHasNoFreeNeighbour (live run 69); ships: PathLineFindsNoWay.
bool PathStepFindsNoWay(const CUnit &unit, const Vec2i &goal);

/// Live run 70 G (calibration, not decoded): a land unit of one tile far from its blocked (pulled)
/// goal whose 8 neighbours hold no free tile it can reach by terrain and buildings (the pocket
/// beside 40,36 / 40,37 / 40,38) walks straight toward it and stops at the near edge. Returns the
/// goal pulled once more along the same line, this time to the last blocked tile before a free
/// tile the unit can reach; any other goal unchanged.
Vec2i PullGoalIntoReach(const CUnit &unit, const Vec2i &goal);

/// The stop beside a blocked (pulled) goal (PULL-10; calibration, not decoded). A unit that has
/// just reached the first tile within 1 of its blocked goal, `heading` its last step (each
/// component -1 / 0 / +1), takes one more step onto a free tile that is also within 1 of the goal
/// (not the goal): straight on first, else 45 degrees to its left, else 45 degrees to its right.
/// Returns that tile, or `pos` (stop here) when none fits. Fitted on every live walk with a known
/// path: run 71 G (35,16 -> pulled 35,23, stop 34,23 6 of 6), ORIG48 1 / 1r / 1a, 2, 3, 6, 9-13,
/// 15-17 and ORIG55 Q1-Q6 (HUMAN03 terrain). Left before right is NOT measured: no live case had
/// both free.
Vec2i PulledGoalStopStep(const Vec2i &pos, const Vec2i &heading, const Vec2i &goal,
                         const std::function<bool(const Vec2i &)> &isFree);

/// The flag for PulledGoalStopStep in the Move order (on by default; host tests may switch it).
extern bool MoveGoalPullStopStep;

// ---- the region map --------------------------------------------------------------------------
// The original keeps one 16-bit word per tile (0x8F6674), the map's REGM section: water bodies
// and land masses each have their own number, forest, rock and wall tiles a number of their own.
// The port reads it from the map's PUD file at game start (new game and loaded game). Whether
// the original rebuilds it when trees fall is not known: the map as loaded is kept.
// A map without a REGM section (or one whose size disagrees with the map) gets a region map
// built from the terrain at the first use: 8-connected bodies of water, 8-connected land, and
// one number for forest, rock and wall tiles. That build is the port's own; the original's
// region builder is not decoded.

/// Read the REGM section of the game's map, if it has one; otherwise clear (built at first use).
void LoadGoalRegionMap(const std::filesystem::path &mapPath);
/// Install a region map (tests, and the PUD reader). An empty map means "build at first use".
void SetGoalRegionMap(int width, int height, std::vector<uint16_t> words);
/// Drop the region map (game teardown).
void ClearGoalRegionMap();
/// The region word of a tile; -1 off the map.
int GoalRegionAt(const Vec2i &pos);
/// The fallback build from the current map's terrain (exposed for the tests).
std::vector<uint16_t> BuildGoalRegionsFromTerrain();

#endif // GOAL_PULL_H
