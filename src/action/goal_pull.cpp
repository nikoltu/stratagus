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
/**@name goal_pull.cpp - A goal on a tile the unit cannot enter is pulled toward the unit. */
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

#include "goal_pull.h"

#include "map.h"
#include "tileset.h"
#include "unit.h"
#include "unittype.h"
#include "wc2mission.h"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

GoalPullLine::GoalPullLine(const Vec2i &from, const Vec2i &to)
{
	const int dx = to.x - from.x;
	const int dy = to.y - from.y;
	const int ax = std::abs(dx);
	const int ay = std::abs(dy);
	xMajor = ax >= ay;
	major = xMajor ? ax : ay;
	minor = xMajor ? ay : ax;
	err = major >> 1;
	if (err == 0) {
		err = 1;
	}
	sx = dx >= 0 ? 1 : -1;
	sy = dy >= 0 ? 1 : -1;
}

Vec2i GoalPullLine::Next()
{
	err -= minor;
	const bool minorStep = err <= 0;
	if (minorStep) {
		err += major;
	}
	if (xMajor) {
		return Vec2i(sx, minorStep ? sy : 0);
	}
	return Vec2i(minorStep ? sx : 0, sy);
}

Vec2i PullGoalOnLine(const Vec2i &unitPos, const Vec2i &goal, bool needRegion,
                     const std::function<bool(const Vec2i &)> &isFree,
                     const std::function<bool(const Vec2i &)> &inRegion)
{
	if (goal == unitPos) {
		return goal;
	}
	// The line is set up from the unit's end and walked from the goal's end (0x4C2630).
	GoalPullLine line(unitPos, goal);
	Vec2i pos = goal;
	Vec2i step(0, 0);
	// The walk reaches the unit within max(|dx|, |dy|) steps; the bound only guards the loop.
	const int limit = std::max(std::abs(goal.x - unitPos.x), std::abs(goal.y - unitPos.y)) + 1;
	for (int i = 0; i <= limit; ++i) {
		if (pos == unitPos) {
			return goal; // blocked all the way: the ordered tile stays the goal
		}
		if ((!needRegion || inRegion(pos)) && isFree(pos)) {
			return pos + step; // the tile walked just before (the goal itself when it is free)
		}
		step = line.Next();
		pos = pos - step;
	}
	return goal;
}

Vec2i PullGoalTowardUnit(const CUnit &unit, const Vec2i &goal)
{
	if (!Map.Info.IsPointOnMap(goal) || !Map.Info.IsPointOnMap(unit.tilePos)) {
		return goal;
	}
	// Movement mask without "a unit stands here" (0x89DAB8[kind] & 0xFEFF): standing land and sea
	// units are ignored, buildings block. A flyer's mask is the air-unit bit only and is kept as the
	// original keeps 0x200.
	const bool flyer = unit.Type->MoveType == EMovement::Fly;
	const tile_flags mask = flyer ? tile_flags(unit.Type->MovementMask)
	                              : tile_flags(unit.Type->MovementMask) & ~(MapFieldLandUnit | MapFieldSeaUnit);
	const int myRegion = GoalRegionAt(unit.tilePos);
	return PullGoalOnLine(unit.tilePos, goal, !flyer,
	                      [&](const Vec2i &p) { return !Map.Field(p)->CheckMask(mask); },
	                      [&](const Vec2i &p) { return GoalRegionAt(p) == myRegion; });
}

bool PathLineFindsNoWay(const Vec2i &unitPos, const Vec2i &goal, int gridStep,
                        const std::function<bool(const Vec2i &)> &isFree)
{
	const int step = std::max(gridStep, 1);
	// The path step floors the goal of a unit on the grid to the grid (0x4D5E20); the line runs in
	// grid steps from the unit's grid tile.
	const Vec2i from(unitPos.x / step, unitPos.y / step);
	const Vec2i to(goal.x / step, goal.y / step);
	if (from == to || isFree(to * step)) {
		return false;
	}
	// The tiles between are those of the pull's own line (set up from the unit, walked from the
	// goal). Calibrated on the live cases: ORIG48 trial 5 (33,28 -> 34,26) walks to 33,27 when no
	// unit stands there (run 49: 4 of 4) and stays when one does (2 of 2); the line walked from the
	// unit would pass 34,27 (forest) and leave no way in both.
	GoalPullLine line(from, to);
	Vec2i pos = to;
	const int limit = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y));
	for (int i = 0; i != limit; ++i) {
		pos = pos - line.Next();
		if (pos == from) {
			return true; // nothing free between the unit and its blocked goal
		}
		if (isFree(pos * step)) {
			return false; // a tile between to walk to
		}
	}
	return true;
}

bool GoalHasNoFreeNeighbour(const Vec2i &unitPos, const Vec2i &goal,
                            const std::function<bool(const Vec2i &)> &isFree,
                            const std::function<bool(const Vec2i &)> &inRegion)
{
	if (goal == unitPos || isFree(goal)) {
		return false;
	}
	if (std::abs(goal.x - unitPos.x) <= 1 && std::abs(goal.y - unitPos.y) <= 1) {
		return true; // beside its goal already (ORIG50 s.4 O1: from 1 tile, no move; ORIG48 / test 33,27)
	}
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			const Vec2i p = goal + Vec2i(dx, dy);
			if ((dx != 0 || dy != 0) && p != unitPos && isFree(p) && inRegion(p)) {
				return false;
			}
		}
	}
	return true;
}

bool PathStepNoWayByGoalNeighbours = true;
/// The farthest (Chebyshev) a land unit stands from its stored goal for the no-free-neighbour drop:
/// live drops at 3-6 (run 69 F1 / F3 / F6) and at 9 (ORIG48 trial 8), live walks at 11-17 (run 70
/// G1 / G2).
static constexpr int NoWayDropReach = 10;

/// Mark the tiles reachable from `from` (8 ways) over tiles without any bit of `blocked`. True when
/// the flood ended before `limit` tiles (then `seen` is the whole reach), false when cut short.
static bool FloodReach(const Vec2i &from, tile_flags blocked, std::vector<uint8_t> &seen, int limit)
{
	const int w = Map.Info.MapWidth;
	const int h = Map.Info.MapHeight;
	seen.assign((size_t) w * h, 0);
	std::vector<Vec2i> queue;
	queue.push_back(from);
	seen[(size_t) from.y * w + from.x] = 1;
	for (size_t i = 0; i != queue.size(); ++i) {
		if (int(queue.size()) > limit) {
			return false;
		}
		const Vec2i p = queue[i];
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				const Vec2i n = p + Vec2i(dx, dy);
				if (n.x < 0 || n.y < 0 || n.x >= w || n.y >= h) {
					continue;
				}
				uint8_t &s = seen[(size_t) n.y * w + n.x];
				if (s == 0 && !Map.Field(n)->CheckMask(blocked)) {
					s = 1;
					queue.push_back(n);
				}
			}
		}
	}
	return true;
}

bool PathStepFindsNoWay(const CUnit &unit, const Vec2i &goal)
{
	if (unit.Type->MoveType == EMovement::Fly || !Map.Info.IsPointOnMap(goal)
	    || !Map.Info.IsPointOnMap(unit.tilePos)) {
		return false;
	}
	// Unlike the pull, units standing on the line block it: a friendly unit on the free tile beside
	// a blocked goal leaves no way (run 49 item 7c: Move and patrol, no step; 4 walks without it).
	const tile_flags mask = tile_flags(unit.Type->MovementMask);
	const auto isFree = [&](const Vec2i &p) { return Map.Info.IsPointOnMap(p) && !Map.Field(p)->CheckMask(mask); };
	if (PathStepNoWayByGoalNeighbours && unit.Type->MoveType == EMovement::Land && unit.Type->GridStep <= 1) {
		// Live run 69 F: every no-move of 30 orders onto forest (F1 4 of 9, F2 5 of 9, F3, F6) had no
		// free tile it can reach round its stored goal (forest, the Farm, an enclosed pocket, or
		// mates standing there at the order); every unit that moved (F1-F5, 20) had one, also where
		// the line toward the goal crossed a standing mate first (F5 1570, 1576: the line rule left
		// them no way). The ORIG48 trials (5, 7, 8, 14, 6, 1) and run 49 7c come out the same.
		// Reachable: by terrain and buildings, standing units not counted (F1: the free 39,37 beside
		// the goals 40,36 / 40,37 lies in a pocket closed by forest and the Farm 37,38 - 3 units did
		// not move; the region map, built without buildings, joins that pocket to the land round it).
		// Live run 70 G (2 trials, 18 unit runs): units 11-17 tiles from such a goal (the same stored
		// goals 40,36 / 40,37 / 40,38 beside the pocket) all MOVED, walked straight to the near edge
		// and stopped 4-8 short; the run 69 no-moves were 3-6 tiles from their goals. So the drop is
		// kept only near the goal; the limit 10 lies between the two measured sets (UNMEASURED there).
		if (std::max(std::abs(goal.x - unit.tilePos.x), std::abs(goal.y - unit.tilePos.y)) > NoWayDropReach) {
			return false;
		}
		const tile_flags terrainMask = mask & ~(MapFieldLandUnit | MapFieldSeaUnit);
		std::vector<uint8_t> seen;
		bool flooded = false;
		bool complete = false;
		const auto reachable = [&](const Vec2i &p) {
			if (!flooded) {
				flooded = true;
				complete = FloodReach(unit.tilePos, terrainMask, seen, 16384);
			}
			return !complete || seen[(size_t) p.y * Map.Info.MapWidth + p.x] != 0;
		};
		return GoalHasNoFreeNeighbour(unit.tilePos, goal, isFree, reachable);
	}
	return PathLineFindsNoWay(unit.tilePos, goal, unit.Type->GridStep, isFree);
}

Vec2i PullGoalIntoReach(const CUnit &unit, const Vec2i &goal)
{
	const CUnitType &type = *unit.Type;
	if (type.MoveType != EMovement::Land || type.TileWidth != 1 || type.TileHeight != 1 || type.GridStep > 1
	    || !Map.Info.IsPointOnMap(goal) || !Map.Info.IsPointOnMap(unit.tilePos) || goal == unit.tilePos) {
		return goal;
	}
	const tile_flags terrainMask = tile_flags(type.MovementMask) & ~(MapFieldLandUnit | MapFieldSeaUnit);
	const auto isFree = [&](const Vec2i &p) { return Map.Info.IsPointOnMap(p) && !Map.Field(p)->CheckMask(terrainMask); };
	if (isFree(goal)) {
		return goal;
	}
	std::vector<uint8_t> seen;
	if (!FloodReach(unit.tilePos, terrainMask, seen, 16384)) {
		return goal;
	}
	const auto reachable = [&](const Vec2i &p) { return seen[(size_t) p.y * Map.Info.MapWidth + p.x] != 0; };
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			const Vec2i p = goal + Vec2i(dx, dy);
			if ((dx != 0 || dy != 0) && isFree(p) && reachable(p)) {
				return goal; // a free tile round the goal the unit can reach: the ring as before
			}
		}
	}
	// Run 70 G1 / G2: the 8 units with such goals (11-17 tiles off) ended against the west edge
	// (34,37 35,35-38 36,37-39 / 35-37,36-38), none round the clump to the north or the east.
	return PullGoalOnLine(unit.tilePos, goal, true, isFree, reachable);
}

bool MoveGoalPullStopStep = true;

Vec2i PulledGoalStopStep(const Vec2i &pos, const Vec2i &heading, const Vec2i &goal,
                         const std::function<bool(const Vec2i &)> &isFree)
{
	if (heading == Vec2i(0, 0)) {
		return pos;
	}
	// The 8 headings clockwise from north (y grows downward): left = one back, right = one on.
	static const Vec2i Ring[8] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
	int at = -1;
	for (int i = 0; i != 8; ++i) {
		if (Ring[i] == heading) {
			at = i;
		}
	}
	if (at < 0) {
		return pos;
	}
	const Vec2i tries[3] = {heading, Ring[(at + 7) % 8], Ring[(at + 1) % 8]};
	for (const Vec2i &d : tries) {
		const Vec2i p = pos + d;
		if (p != goal && std::abs(p.x - goal.x) <= 1 && std::abs(p.y - goal.y) <= 1 && isFree(p)) {
			return p;
		}
	}
	return pos;
}

// ---- the region map --------------------------------------------------------------------------

namespace
{
struct GoalRegions {
	int width = 0;
	int height = 0;
	std::vector<uint16_t> words;
	bool fromFile = false; /// read from the map's REGM section
};
GoalRegions g_regions;

/// Region words of the fallback build (the numbering of the PUD's REGM: water bodies from 0,
/// land masses from 0x4000, forest 0xFFFE).
constexpr uint16_t RegionBlocked = 0xFFFE;
constexpr uint16_t RegionLandFirst = 0x4000;

bool RegionMapFits()
{
	return !g_regions.words.empty() && g_regions.width == Map.Info.MapWidth
	       && g_regions.height == Map.Info.MapHeight;
}
} // namespace

std::vector<uint16_t> BuildGoalRegionsFromTerrain()
{
	const int w = Map.Info.MapWidth;
	const int h = Map.Info.MapHeight;
	std::vector<uint16_t> words((size_t) w * h, RegionBlocked);
	if (w <= 0 || h <= 0 || Map.Fields.empty()) {
		return words;
	}
	// 0 blocked, 1 water, 2 land - from the terrain bits only (no unit, no building).
	std::vector<char> kind((size_t) w * h, 0);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const tile_flags f = Map.Field(x, y)->Flags;
			char k = 0;
			if (f & (MapFieldForest | MapFieldRocks | MapFieldWall)) {
				k = 0;
			} else if ((f & MapFieldWaterAllowed) && !(f & (MapFieldLandAllowed | MapFieldCoastAllowed))) {
				k = 1;
			} else if (f & (MapFieldLandAllowed | MapFieldCoastAllowed)) {
				k = 2; // land and shore, as the REGM counts the shore to the land mass
			}
			kind[(size_t) y * w + x] = k;
		}
	}
	uint16_t nextWater = 0;
	uint16_t nextLand = RegionLandFirst;
	std::vector<char> done((size_t) w * h, 0);
	std::vector<Vec2i> stack;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const size_t i = (size_t) y * w + x;
			if (done[i] || kind[i] == 0) {
				continue;
			}
			const char k = kind[i];
			const uint16_t id = k == 1 ? nextWater++ : nextLand++;
			stack.assign(1, Vec2i(x, y));
			done[i] = 1;
			while (!stack.empty()) {
				const Vec2i p = stack.back();
				stack.pop_back();
				words[(size_t) p.y * w + p.x] = id;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						const int nx = p.x + dx;
						const int ny = p.y + dy;
						if ((dx || dy) && nx >= 0 && ny >= 0 && nx < w && ny < h) {
							const size_t j = (size_t) ny * w + nx;
							if (!done[j] && kind[j] == k) {
								done[j] = 1;
								stack.push_back(Vec2i(nx, ny));
							}
						}
					}
				}
			}
		}
	}
	return words;
}

void LoadGoalRegionMap(const std::filesystem::path &mapPath)
{
	ClearGoalRegionMap();
	int w = 0;
	int h = 0;
	std::vector<uint16_t> words;
	if (ReadPudRegionMap(mapPath, w, h, words)) {
		SetGoalRegionMap(w, h, std::move(words));
		g_regions.fromFile = true;
	}
}

void SetGoalRegionMap(int width, int height, std::vector<uint16_t> words)
{
	g_regions = GoalRegions();
	if (width > 0 && height > 0 && words.size() == (size_t) width * height) {
		g_regions.width = width;
		g_regions.height = height;
		g_regions.words = std::move(words);
	}
}

void ClearGoalRegionMap()
{
	g_regions = GoalRegions();
}

int GoalRegionAt(const Vec2i &pos)
{
	if (!Map.Info.IsPointOnMap(pos)) {
		return -1;
	}
	if (!RegionMapFits()) {
		// No REGM (or not this map's): built once from the terrain as it stands at the first use.
		// Every peer reaches the first use at the same cycle of the same game, so the build is the
		// same on all of them.
		SetGoalRegionMap(Map.Info.MapWidth, Map.Info.MapHeight, BuildGoalRegionsFromTerrain());
	}
	return g_regions.words[(size_t) pos.y * g_regions.width + pos.x];
}
