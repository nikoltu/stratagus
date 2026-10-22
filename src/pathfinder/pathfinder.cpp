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
/**@name pathfinder.cpp - The path finder routines. */
//
//      I use breadth-first.
//
//      (c) Copyright 1998-2007 by Lutz Sammer and Russell Smith
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
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//      02111-1307, USA.
//

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "stratagus.h"

#include "pathfinder.h"

#include "actions.h"
#include "map.h"
#include "unittype.h"
#include "unit.h"

#include <algorithm>
#include <cstring>

//astar.cpp

/// Init the a* data structures
extern void InitAStar(int mapWidth, int mapHeight);

/// free the a* data structures
extern void FreeAStar();

/// Find and a* path for a unit
extern int AStarFindPath(const Vec2i &startPos, const Vec2i &goalPos, int gw, int gh,
						 int tilesizex, int tilesizey, int minrange,
						 int maxrange, char *path, int pathlen, const CUnit &unit,
						 bool squareRange = false);

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

void TerrainTraversal::SetSize(unsigned int width, unsigned int height)
{
	m_values.resize((width + 2) * (height + 2));
	m_extented_width = width + 2;
	m_height = height;
}

void TerrainTraversal::Init()
{
	const unsigned int height = m_height;
	const unsigned int width = m_extented_width - 2;
	const unsigned int width_ext = m_extented_width;

	memset(&m_values[0], '\xFF', width_ext * sizeof(dataType));
	for (unsigned i = 1; i < 1 + height; ++i) {
		m_values[i * width_ext] = -1;
		memset(&m_values[i * width_ext + 1], '\0', width * sizeof(dataType));
		m_values[i * width_ext + width + 1] = -1;
	}
	memset(&m_values[(height + 1) * width_ext], '\xFF', width_ext * sizeof(dataType));
}

void TerrainTraversal::PushPos(const Vec2i &pos)
{
	if (IsVisited(pos) == false) {
		m_queue.push(PosNode(pos, pos));
		Set(pos, 1);
	}
}

void TerrainTraversal::PushNeighboor(const Vec2i &pos)
{
	const Vec2i offsets[] = {Vec2i(0, -1), Vec2i(-1, 0), Vec2i(1, 0), Vec2i(0, 1),
							 Vec2i(-1, -1), Vec2i(1, -1), Vec2i(-1, 1), Vec2i(1, 1)
							};

	for (int i = 0; i != 8; ++i) {
		const Vec2i newPos = pos + offsets[i];

		if (IsVisited(newPos) == false) {
			m_queue.push(PosNode(newPos, pos));
			Set(newPos, Get(pos) + 1);
		}
	}
}

void TerrainTraversal::PushUnitPosAndNeighboor(const CUnit &unit)
{
	const CUnit *startUnit = GetFirstContainer(unit);
	const Vec2i offset(1, 1);
	const Vec2i extraTileSize(startUnit->Type->TileWidth - 1, startUnit->Type->TileHeight - 1);
	const Vec2i start = startUnit->tilePos - offset;
	const Vec2i end = startUnit->tilePos + extraTileSize + offset;

	for (Vec2i it = start; it.y != end.y; ++it.y) {
		for (it.x = start.x; it.x != end.x; ++it.x) {
			PushPos(it);
		}
	}
}

bool TerrainTraversal::IsVisited(const Vec2i &pos) const
{
	return Get(pos) != 0;
}

bool TerrainTraversal::IsReached(const Vec2i &pos) const
{
	return Get(pos) != 0 && Get(pos) != -1;
}

bool TerrainTraversal::IsInvalid(const Vec2i &pos) const
{
	return Get(pos) != -1;
}

TerrainTraversal::dataType TerrainTraversal::Get(const Vec2i &pos) const
{
	return m_values[m_extented_width + 1 + pos.y * m_extented_width + pos.x];
}

void TerrainTraversal::Set(const Vec2i &pos, TerrainTraversal::dataType value)
{
	m_values[m_extented_width + 1 + pos.y * m_extented_width + pos.x] = value;
}

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

/**
**  Init the pathfinder
*/
void InitPathfinder()
{
	InitAStar(Map.Info.MapWidth, Map.Info.MapHeight);
}

/**
**  Free the pathfinder
*/
void FreePathfinder()
{
	FreeAStar();
}

/*----------------------------------------------------------------------------
--  PATH-FINDER USE
----------------------------------------------------------------------------*/

/**
**  Can the unit 'src' reach the place goalPos.
**
**  @param src       Unit for the path.
**  @param goalPos   Map tile position.
**  @param w         Width of Goal
**  @param h         Height of Goal
**  @param minrange  min range to the tile
**  @param range     Range to the tile.
**
**  @return          Distance to place.
*/
int PlaceReachable(const CUnit &src, const Vec2i &goalPos, int w, int h, int minrange, int range, bool from_outside_container, bool squareRange)
{
	SetAStarFixedEnemyUnitsUnpassable(true); /// change Path Finder setting to don't count tiles with enemy units as passable
	int i;
	Vec2i srcTilePos = src.tilePos;
	int srcTW = src.Type->TileWidth;
	int srcTH = src.Type->TileHeight;
	if (!from_outside_container || !src.Container) {
		i = AStarFindPath(srcTilePos, goalPos, w, h,
						  srcTW, srcTH,
						  minrange, range, nullptr, 0, src, squareRange);
	} else {
		const CUnit *first_container = GetFirstContainer(src);

        const Vec2i offset(1, 1);
		int containerW = first_container->Type->TileWidth;
		int containerH = first_container->Type->TileHeight;
		Vec2i containerTilePos = first_container->tilePos;
		i = PF_UNREACHABLE;
		// check top and bottom rows and left and right columns around the container
		for (int x = -1; x <= containerW; x++) {
			for (int y = -1; y <= containerH; y++) {
				if (x >= 0 && x < containerW && y >= 0 && y < containerH) {
					// inside the container, no need to check
					continue;
				}
				Vec2i tile_pos = containerTilePos + Vec2i(x, y);
				if (!Map.Info.IsPointOnMap(tile_pos)) {
					continue;
				}
				if (!CanMoveToMask(tile_pos, src.Type->MovementMask)) {
					//ignore tiles to which the unit cannot be dropped from its container
					continue;
				}

				i = AStarFindPath(tile_pos, goalPos, w, h,
					srcTW, srcTH,
					minrange, range, nullptr, 0, src, squareRange);

				switch (i) {
					case PF_FAILED:
					case PF_UNREACHABLE:
					case PF_WAIT:
						continue;
				}
				goto finished;
			}
		}
	}
finished:
	SetAStarFixedEnemyUnitsUnpassable(false); /// restore Path Finder setting
	switch (i) {
		case PF_FAILED:
		case PF_UNREACHABLE:
		case PF_WAIT:
			return 0;
		case PF_REACHED:
			/* since most of this function usage check return value as bool
			* then reached state should be track as true value */
			return std::max(i, 1);
		default:
			return i;
	}
}

/**
**  Can the unit 'src' reach the unit 'dst'.
**
**  @param src    Unit for the path.
**  @param dst    Unit to be reached.
**  @param range  Range to unit.
**
**  @return       Distance to place.
*/
int UnitReachable(const CUnit &src, const CUnit &dst, int range, bool from_outside_container, bool squareRange)
{
	//  Find a path to the goal.
	if (src.Type->Building) {
		return 0;
	}
	const int depth = PlaceReachable(src, dst.tilePos,
									 dst.Type->TileWidth, dst.Type->TileHeight, 0, range, from_outside_container, squareRange);
	if (depth <= 0) {
		return 0;
	}
	return depth;
}

/**
**  Calc path length for the unit 'src' to reach the unit 'dst'.
**
**  @param src     	 Unit for the path.
**  @param dst    	 Unit to be reached.
**  @param minrange  min range to the tile
**  @param range     Range to the tile.
**
**  @return       path length to dst or error code
*/

int CalcPathLengthToUnit(const CUnit &src, const CUnit &dst, const int minrange, const int range, bool squareRange)
{
	SetAStarFixedEnemyUnitsUnpassable(true); /// change Path Finder setting to don't count tiles with enemy units as passable
	int length = AStarFindPath(src.tilePos, dst.tilePos,
							   dst.Type->TileWidth, dst.Type->TileHeight,
							   src.Type->TileWidth, src.Type->TileHeight,
							   minrange, range,
							   nullptr, 0, src, squareRange);
	SetAStarFixedEnemyUnitsUnpassable(false); /// restore Path Finder setting
	switch (length) {
		case PF_FAILED:
		case PF_UNREACHABLE:
		case PF_WAIT:
			return -1;
		case PF_REACHED:
			return 0;
	}
	return length;
}

/*----------------------------------------------------------------------------
--  REAL PATH-FINDER
----------------------------------------------------------------------------*/

const Vec2i &PathFinderInput::GetUnitPos() const { return unit->tilePos; }
Vec2i PathFinderInput::GetUnitSize() const
{
	const Vec2i tileSize(unit->Type->TileWidth, unit->Type->TileHeight);
	return tileSize;
}

void PathFinderInput::SetUnit(CUnit &_unit)
{
	unit = &_unit;

	isRecalculatePathNeeded = true;
}


void PathFinderInput::SetGoal(const Vec2i &pos, const Vec2i &size)
{
	Assert(Map.Info.IsPointOnMap(pos));
	Assert(unit);
	Assert(unit->IsAliveOnMap());
	Vec2i newPos = pos;
	// Large units may have a goal that goes outside the map, fix it here
	if (newPos.x + unit->Type->TileWidth - 1 >= Map.Info.MapWidth) {
		newPos.x = Map.Info.MapWidth - unit->Type->TileWidth;
	}
	if (newPos.y + unit->Type->TileHeight - 1 >= Map.Info.MapHeight) {
		newPos.y = Map.Info.MapHeight - unit->Type->TileHeight;
	}
	if (goalPos != newPos || goalSize != size) {
		isRecalculatePathNeeded = true;
	}
	goalPos = newPos;
	goalSize = size;
}

void PathFinderInput::SetMinRange(int range)
{
	if (minRange != range) {
		minRange = range;
		isRecalculatePathNeeded = true;
	}
}

void PathFinderInput::SetMaxRange(int range, bool squareRange)
{
	if (maxRange != range || this->squareRange != squareRange) {
		maxRange = range;
		this->squareRange = squareRange;
		isRecalculatePathNeeded = true;
	}
}

void PathFinderInput::PathRecalculated()
{
	unitSize.x = unit->Type->TileWidth;
	unitSize.y = unit->Type->TileHeight;

	isRecalculatePathNeeded = false;
}


PathFinderOutput::PathFinderOutput()
{
	memset(this, 0, sizeof(*this));
}

/**
**  Find new path.
**
**  The destination could be a unit or a field.
**  Range gives how far we must reach the goal.
**
**  @note  The destination could become negative coordinates!
**
**  @param unit  Path for this unit.
**
**  @return      >0 remaining path length, 0 wait for path, -1
**               reached goal, -2 can't reach the goal.
*/
static int NewPath(PathFinderInput &input, PathFinderOutput &output)
{
	char *path = output.Path;
	int i = AStarFindPath(input.GetUnitPos(),
						  input.GetGoalPos(),
						  input.GetGoalSize().x, input.GetGoalSize().y,
						  input.GetUnitSize().x, input.GetUnitSize().y,
						  input.GetMinRange(), input.GetMaxRange(),
						  path, PathFinderOutput::MAX_PATH_LENGTH,
						  *input.GetUnit(), input.IsSquareRange());
	input.PathRecalculated();
	if (i == PF_FAILED) {
		i = PF_UNREACHABLE;
	}

	// Update path if it was requested. Otherwise we may only want
	// to know if there exists a path.
	if (path != nullptr) {
		if (i >= 0) {
			output.Length = std::min<int>(i, PathFinderOutput::MAX_PATH_LENGTH);
			output.OverflowLength = std::min<int>(i - output.Length, PathFinderOutput::MAX_OVERFLOW);
			if (output.Length == 0) {
				++output.Length;
			}
		} else {
			output.Length = 0;
			output.OverflowLength = 0;
		}
	}
	return i;
}

/**
**  Returns the next element of a path.
**
**  @param unit  Unit that wants the path element.
**  @param dir   Pointer for the direction.
**
**  @return >0 remaining path length, 0 wait for path, -1
**  reached goal, -2 can't reach the goal.
*/
/// Can the unit take the path element that ends on pos now. On a one-tile step a transporter may
/// cross a tile it could not rest on (water off its grid), so only the tile itself is tested.
static bool CanStepTo(const CUnit &unit, const Vec2i &pos, int element)
{
	if (!(element & PathShortStep) || unit.Type->GridStep == 1) {
		return UnitCanBeAt(unit, pos);
	}
	return Map.Info.IsPointOnMap(pos) && !Map.Field(pos)->CheckMask(unit.Type->MovementMask);
}

bool PathKeepsHeldStep = true;

std::pair<int, Vec2i> NextPathElement(CUnit &unit)
{
	PathFinderInput &input = unit.pathFinderData->input;
	PathFinderOutput &output = unit.pathFinderData->output;

	unit.CurrentOrder()->UpdatePathFinderData(input);
	// Attempt to use path cache
	// FIXME: If there is a goal, it may have moved, ruining the cache

	// The cached element at Path[Length - 1] is the step just taken: drop it. When it was the last
	// one stored, a new search follows (it answers PF_REACHED at the goal) - reading the element
	// below it would read Path[-1], and its 0 was taken for PF_WAIT: a 10-cycle tail on every
	// arrival (host test "path end").
	if (output.Length > 0 && !input.IsRecalculateNeeded()) {
		output.Length--;
	}
	// Goal has moved, need to recalculate path or no cached path
	if (output.Length <= 0 || input.IsRecalculateNeeded()) {
		const int result = NewPath(input, output);

		if (result == PF_UNREACHABLE) {
			output.OverflowLength = output.Length = 0;
			return {result, {}};
		}
		if (result == PF_REACHED) {
			return {result, {}};
		}
	}

	// A path element is one heading; the unit covers GridStep tiles in it. Only the end tile of the
	// step is tested: the tile passed between need not be enterable.
	int element = output.Path[output.Length - 1];
	int step = PathElementLength(element, unit.Type->GridStep);
	Vec2i dir(Heading2X[PathElementHeading(element)] * step, Heading2Y[PathElementHeading(element)] * step);
	int result = output.Length;
	if (!CanStepTo(unit, unit.tilePos + dir, element)) {
		// If obstructing unit is moving, wait for a bit: the first blocked call starts the wait,
		// each further one adds the wait the call before set, and when PathWaitExpiryCycles have
		// been waited the way is searched again (work/PATHFINDING_2026-10-21.md C5: this search was
		// dead code; live run 67: about 1 s, see PathWaitExpiryCycles). Fast only marks the wait
		// (blocked calls, at most MAX_FAST).
		bool expired = false;
		if (output.Fast) {
			output.Waited = static_cast<uint8_t>(std::min(255, output.Waited + output.LastWait));
			expired = output.Waited >= PathWaitExpiryCycles;
			if (output.Fast < PathFinderOutput::MAX_FAST) {
				output.Fast++;
			}
			AstarDebugPrint("WAIT %d cycles\n", output.Waited);
		} else {
			output.Fast = 1;
			output.Waited = 0;
			AstarDebugPrint("SET WAIT\n");
		}
		result = PF_WAIT;
		if (expired) {
			AstarDebugPrint("WAIT expired\n");
			result = NewPath(input, output);
			if (result == PF_UNREACHABLE) {
				output.OverflowLength = output.Length = 0;
			}
			if (result < 0) {
				return {result, {}};
			}
			element = output.Path[output.Length - 1];
			step = PathElementLength(element, unit.Type->GridStep);
			dir.x = Heading2X[PathElementHeading(element)] * step;
			dir.y = Heading2Y[PathElementHeading(element)] * step;
			// The new way may still begin on the held tile (a moving unit is crossed at a cost):
			// wait again, the counter starts anew.
			result = CanStepTo(unit, unit.tilePos + dir, element) ? int(output.Length) : PF_WAIT;
			output.Fast = 1;
			output.Waited = 0;
		}
	}
	if (result != PF_WAIT) {
		output.Fast = 0;
		output.Waited = 0;
		output.LastWait = 0;
	} else {
		// The wait this call stands for unless the caller sets another (SetPathWait).
		output.LastWait = static_cast<uint8_t>(std::min(PathWaitDefaultCycles, PathWaitExpiryCycles - output.Waited));
		// The blocked step was not taken: keep it (C5). The next call drops Path[Length - 1] as the
		// step just taken, so it is stored twice; with the cache full the farthest step gives way.
		if (PathKeepsHeldStep) {
			if (output.Length >= PathFinderOutput::MAX_PATH_LENGTH) {
				std::memmove(output.Path, output.Path + 1, PathFinderOutput::MAX_PATH_LENGTH - 1);
				output.Length--;
				if (output.OverflowLength < PathFinderOutput::MAX_OVERFLOW) {
					output.OverflowLength++;
				}
			}
			output.Path[output.Length] = output.Path[output.Length - 1];
			output.Length++;
		}
		dir = {0, 0};
	}
	return {result, dir};
}

/// The wait a unit whose step is held gets: at most what is left of PathWaitExpiryCycles (live run
/// 67), so the call that ends the wait comes when the expiry has been waited, not up to 14 later.
void SetPathWait(CUnit &unit, int cycles)
{
	PathFinderOutput &output = unit.pathFinderData->output;
	const int left = std::max(1, PathWaitExpiryCycles - int(output.Waited));
	const int wait = std::clamp(cycles, 1, left);
	output.LastWait = static_cast<uint8_t>(wait);
	unit.Wait = wait;
}

extern const CUnit *AStarWallUnit;

/**
**  Live run 67 M1: a unit whose next step is held by a moving group mate goes round it at once
**  instead of queuing behind it. One search with that mate as a wall; the way is taken when it
**  is at most maxLength steps, else the cached way and the wait stay as they were.
*/
bool PathRoundUnit(CUnit &unit, const CUnit &wall, int maxLength)
{
	PathFinderInput &input = unit.pathFinderData->input;
	PathFinderOutput &output = unit.pathFinderData->output;
	const PathFinderOutput saved = output;
	unit.CurrentOrder()->UpdatePathFinderData(input);
	AStarWallUnit = &wall;
	const int result = NewPath(input, output);
	AStarWallUnit = nullptr;
	if (result <= 0 || result > maxLength || output.Length == 0) {
		output = saved;
		return false;
	}
	// The next NextPathElement drops Path[Length - 1] as the step just taken: store the first
	// step of the new way twice (the farthest one gives way when the cache is full), as on a wait.
	if (output.Length >= PathFinderOutput::MAX_PATH_LENGTH) {
		std::memmove(output.Path, output.Path + 1, PathFinderOutput::MAX_PATH_LENGTH - 1);
		output.Length--;
		if (output.OverflowLength < PathFinderOutput::MAX_OVERFLOW) {
			output.OverflowLength++;
		}
	}
	output.Path[output.Length] = output.Path[output.Length - 1];
	output.Length++;
	output.Fast = 0;
	output.Waited = 0;
	output.LastWait = 0;
	return true;
}

extern bool AStarCrossMatesOnTheWay;

/**
**  Round 3.4.81 (two groups head-on in a 1-tile gap): a unit whose search found no way only
**  because own units waiting on their way stand in it searches once more with those units crossed
**  at a cost. The way found begins at (or runs through) such a unit, so the unit waits for that
**  step and a head-on pair can be seen by both (HeadOnStepAside). UNMEASURED (the original was
**  not measured head-on). False leaves no way cached.
*/
bool PathThroughWaitingMates(CUnit &unit)
{
	PathFinderInput &input = unit.pathFinderData->input;
	PathFinderOutput &output = unit.pathFinderData->output;
	unit.CurrentOrder()->UpdatePathFinderData(input);
	AStarCrossMatesOnTheWay = true;
	const int result = NewPath(input, output);
	AStarCrossMatesOnTheWay = false;
	if (result <= 0 || output.Length == 0) {
		output.Length = 0;
		output.OverflowLength = 0;
		return false;
	}
	// stored twice: the next NextPathElement drops Path[Length - 1] as the step just taken
	if (output.Length >= PathFinderOutput::MAX_PATH_LENGTH) {
		std::memmove(output.Path, output.Path + 1, PathFinderOutput::MAX_PATH_LENGTH - 1);
		output.Length--;
		if (output.OverflowLength < PathFinderOutput::MAX_OVERFLOW) {
			output.OverflowLength++;
		}
	}
	output.Path[output.Length] = output.Path[output.Length - 1];
	output.Length++;
	output.Fast = 0;
	output.Waited = 0;
	output.LastWait = 0;
	return true;
}

bool PathHasWayRound(CUnit &unit, const CUnit &wall)
{
	PathFinderInput &input = unit.pathFinderData->input;
	PathFinderOutput &output = unit.pathFinderData->output;
	const PathFinderOutput saved = output;
	unit.CurrentOrder()->UpdatePathFinderData(input);
	AStarWallUnit = &wall;
	AStarCrossMatesOnTheWay = true;
	const int result = NewPath(input, output);
	AStarCrossMatesOnTheWay = false;
	AStarWallUnit = nullptr;
	output = saved;
	return result > 0 || result == PF_REACHED;
}

/// A one-tile step of a unit on the one-tile grid as its whole cached way (stored twice, as on a
/// wait): the next NextPathElement takes it, the one after searches the goal again.
void PathStepAside(CUnit &unit, const Vec2i &dir)
{
	PathFinderOutput &output = unit.pathFinderData->output;
	output.Path[0] = static_cast<char>(XY2Heading[dir.x + 1][dir.y + 1]);
	output.Path[1] = output.Path[0];
	output.Length = 2;
	output.OverflowLength = 0;
	output.Fast = 0;
	output.Waited = 0;
	output.LastWait = 0;
}

//@}
