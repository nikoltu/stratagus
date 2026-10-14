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
/**@name group_move.h - The goals of a group order to one point. */
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

#ifndef GROUP_MOVE_H
#define GROUP_MOVE_H

//@{

#include "vec2i.h"

#include <algorithm>
#include <cstddef>
#include <vector>

class CUnit;

/// A group wider (taller) than this many tiles is folded onto the column (row) of the target.
constexpr int GroupMoveMaxExtent = 3;

/// Tiles per step of the ships that are sent as a group.
constexpr int ShipGridStep = 2;

/**
**  The goal tiles of a group that is sent to one tile.
**
**  The group is translated: the anchor is the floor of the mean of the start tiles and it goes
**  to the target. Each axis is handled alone: where the group spans more than
**  GroupMoveMaxExtent tiles no unit keeps its offset on that axis. An order to the anchor
**  itself would move nobody; then every unit gets the target.
**
**  @param starts     tiles the units stand on.
**  @param target     the ordered tile.
**  @param mapWidth   map width in tiles.
**  @param mapHeight  map height in tiles.
**
**  @return           one goal per start tile, in the same order, all on the map.
*/
inline std::vector<Vec2i> GroupMoveGoals(const std::vector<Vec2i> &starts, const Vec2i &target, int mapWidth, int mapHeight)
{
	const auto clamped = [&](int x, int y) {
		return Vec2i(std::clamp(x, 0, std::max(0, mapWidth - 1)), std::clamp(y, 0, std::max(0, mapHeight - 1)));
	};
	std::vector<Vec2i> goals(starts.size(), clamped(target.x, target.y));

	if (starts.size() < 2) {
		return goals;
	}
	const auto floorDiv = [](long long sum, long long n) {
		return static_cast<int>(sum >= 0 ? sum / n : -((-sum + n - 1) / n));
	};
	long long sumX = 0;
	long long sumY = 0;
	Vec2i min = starts[0];
	Vec2i max = starts[0];

	for (const Vec2i &start : starts) {
		sumX += start.x;
		sumY += start.y;
		min.x = std::min<int>(min.x, start.x);
		min.y = std::min<int>(min.y, start.y);
		max.x = std::max<int>(max.x, start.x);
		max.y = std::max<int>(max.y, start.y);
	}
	const long long count = static_cast<long long>(starts.size());
	const Vec2i center(floorDiv(sumX, count), floorDiv(sumY, count));

	if (center == target) {
		return goals;
	}
	const bool keepX = max.x - min.x <= GroupMoveMaxExtent;
	const bool keepY = max.y - min.y <= GroupMoveMaxExtent;

	for (size_t i = 0; i != starts.size(); ++i) {
		goals[i] = clamped(target.x + (keepX ? starts[i].x - center.x : 0),
		                   target.y + (keepY ? starts[i].y - center.y : 0));
	}
	return goals;
}

/**
**  The goal tiles of a group of units that rest on a step grid (ships: every second tile).
**
**  The rule of GroupMoveGoals, counted in tiles: the anchor is the floor of the mean of the
**  start tiles and an axis the group spans by more than GroupMoveMaxExtent tiles is folded, so
**  only units that touch keep their places. Each goal is then moved down onto the grid, as the
**  goal of a single unit is.
**
**  @param starts     tiles the units stand on.
**  @param target     the ordered tile.
**  @param mapWidth   map width in tiles.
**  @param mapHeight  map height in tiles.
**  @param gridStep   tiles per step of the units.
**
**  @return           one goal per start tile, in the same order, all on the map and on the grid.
*/
inline std::vector<Vec2i> GroupMoveGridGoals(const std::vector<Vec2i> &starts, const Vec2i &target, int mapWidth, int mapHeight, int gridStep)
{
	std::vector<Vec2i> goals = GroupMoveGoals(starts, target, mapWidth, mapHeight);

	if (gridStep > 1) {
		for (Vec2i &goal : goals) {
			goal.x -= goal.x % gridStep;
			goal.y -= goal.y % gridStep;
		}
	}
	return goals;
}

/// The goals of a group order of the player to @p target: one per unit of @p units, in order
extern std::vector<Vec2i> GroupMoveGoalsForUnits(const std::vector<CUnit *> &units, const Vec2i &target);

//@}

#endif // !GROUP_MOVE_H
