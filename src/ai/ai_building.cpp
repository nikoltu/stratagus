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
/**@name ai_building.cpp - AI building functions. */
//
//      (c) Copyright 2001-2005 by Lutz Sammer
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

#include "ai_local.h"

#include "map.h"
#include "pathfinder.h"
#include "player.h"
#include "tileset.h"
#include "unit.h"
#include "unit_find.h"
#include "unit_manager.h"
#include "unittype.h"
#include "wc2ai.h"

#include <deque>

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

static bool IsPosFree(const Vec2i &pos, const CUnit &exceptionUnit)
{
	if (Map.Info.IsPointOnMap(pos) == false) {
		return false;
	}
	const CMapField &mf = *Map.Field(pos);
	if (ranges::contains(mf.UnitCache, &exceptionUnit)) {
		return true;
	}
	const tile_flags blockedFlag = (MapFieldUnpassable
									| MapFieldWall
									| MapFieldRocks
									| MapFieldForest
									| MapFieldBuilding);
	if (mf.isFlag(blockedFlag)) {
		return false;
	}
	const tile_flags passableFlag = (MapFieldWaterAllowed
									 | MapFieldCoastAllowed
									 | MapFieldLandAllowed);
	return (mf.isFlag(passableFlag) != 0);
}

/**
**  Check if the surrounding are free. Depending on the value of flag, it will check :
**  0: the building will not block any way
**  1: all surrounding is free
**
**  @param worker    Worker to build.
**  @param type      Type of building.
**  @param pos       map tile position for the building.
**  @param backupok  Location can be used as a backup
**
**  @return          True if the surrounding is free, false otherwise.
**
**  @note            Can be faster written.
*/
static bool AiCheckSurrounding(const CUnit &worker, const CUnitType &type, const Vec2i &pos, bool &backupok)
{
	const int surroundRange = type.AiAdjacentRange != -1 ? type.AiAdjacentRange : 1;
	const Vec2i pos_topLeft(pos.x - surroundRange, pos.y - surroundRange);
	const Vec2i pos_bottomRight(pos.x + type.TileWidth + surroundRange - 1,
								pos.y + type.TileHeight + surroundRange - 1);
	Vec2i it = pos_topLeft;
	const bool firstVal = IsPosFree(it, worker);
	bool lastval = firstVal;
	int obstacleCount = 0;

	for (++it.x; it.x < pos_bottomRight.x; ++it.x) {
		const bool isfree = IsPosFree(it, worker);

		if (isfree && !lastval) {
			++obstacleCount;
		}
		lastval = isfree;
	}
	for (; it.y < pos_bottomRight.y; ++it.y) {
		const bool isfree = IsPosFree(it, worker);

		if (isfree && !lastval) {
			++obstacleCount;
		}
		lastval = isfree;
	}
	for (; pos_topLeft.x < it.x; --it.x) {
		const bool isfree = IsPosFree(it, worker);

		if (isfree && !lastval) {
			++obstacleCount;
		}
		lastval = isfree;
	}
	for (; pos_topLeft.y < it.y; --it.y) {
		const bool isfree = IsPosFree(it, worker);

		if (isfree && !lastval) {
			++obstacleCount;
		}
		lastval = isfree;
	}
	if (firstVal && !lastval) {
		++obstacleCount;
	}

	if (!type.BoolFlag[SHOREBUILDING_INDEX].value) {
		backupok = obstacleCount < 5;
	} else {
		// Shore building have at least 2 obstacles : sea->ground & ground->sea
		backupok = obstacleCount < 3;
	}
	return obstacleCount == 0;
}

/**
**  Check that a building on a fallback place leaves the ground around it in one piece.
**
**  A fallback place is one whose surrounding ring is partly taken (AiCheckSurrounding: fewer than
**  five changes between taken and free). Such a building can close the only way between two parts
**  of a base: seen on a small map, where a farm between the hall and the only gold mine in reach
**  ended all gold income for the rest of the game. Asked here, on the tiles within `reach` of the
**  ring and with the building's own tiles taken:
**   - the free tiles of the ring are still joined to each other;
**   - a gold mine (a resource building that is harvested from) or a depot of the player that
**     touches the ring keeps a free tile at its side that is joined to them.
**  A way round that leaves the looked-at tiles counts as none.
**
**  This is the engine's own placement, made safe; where the original places a building is not
**  established.
**
**  @param worker  Worker to build (its own tile counts as free).
**  @param type    Type of building.
**  @param pos     map tile position for the building.
**
**  @return        True if the ground stays in one piece, false otherwise.
*/
static bool AiPlaceKeepsGroundJoined(const CUnit &worker, const CUnitType &type, const Vec2i &pos)
{
	const int reach = 8; // tiles looked at on each side of the ring: a detour longer than that is no way
	const Vec2i ringMin(pos.x - 1, pos.y - 1);
	const Vec2i ringMax(pos.x + type.TileWidth, pos.y + type.TileHeight);
	const Vec2i boxMin(ringMin.x - reach, ringMin.y - reach);
	const int boxWidth = ringMax.x - ringMin.x + 1 + 2 * reach;
	const int boxHeight = ringMax.y - ringMin.y + 1 + 2 * reach;
	std::vector<char> joined(static_cast<size_t>(boxWidth) * boxHeight, 0);

	const auto index = [&](const Vec2i &tile) {
		return static_cast<size_t>(tile.y - boxMin.y) * boxWidth + (tile.x - boxMin.x);
	};
	const auto inBox = [&](const Vec2i &tile) {
		return tile.x >= boxMin.x && tile.x < boxMin.x + boxWidth && tile.y >= boxMin.y
		       && tile.y < boxMin.y + boxHeight;
	};
	const auto inBuilding = [&](const Vec2i &tile) {
		return tile.x > ringMin.x && tile.x < ringMax.x && tile.y > ringMin.y && tile.y < ringMax.y;
	};

	std::vector<Vec2i> ring;
	for (int y = ringMin.y; y <= ringMax.y; ++y) {
		for (int x = ringMin.x; x <= ringMax.x; ++x) {
			if (x == ringMin.x || x == ringMax.x || y == ringMin.y || y == ringMax.y) {
				ring.push_back(Vec2i(x, y));
			}
		}
	}

	// The free ground that can be walked to from the first free tile of the ring.
	std::vector<Vec2i> todo;
	for (const Vec2i &tile : ring) {
		if (IsPosFree(tile, worker)) {
			joined[index(tile)] = 1;
			todo.push_back(tile);
			break;
		}
	}
	for (size_t head = 0; head < todo.size(); ++head) {
		const Vec2i from = todo[head];
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				const Vec2i next(from.x + dx, from.y + dy);
				if (!inBox(next) || joined[index(next)] || inBuilding(next) || !IsPosFree(next, worker)) {
					continue;
				}
				joined[index(next)] = 1;
				todo.push_back(next);
			}
		}
	}

	for (const Vec2i &tile : ring) {
		if (IsPosFree(tile, worker) && !joined[index(tile)]) {
			return false; // the building would stand between two parts of the free ground
		}
	}

	// A mine or a depot at the ring must keep a free tile at its side.
	for (const Vec2i &tile : ring) {
		if (!Map.Info.IsPointOnMap(tile)) {
			continue;
		}
		for (const CUnit *unit : Map.Field(tile)->UnitCache) {
			if (!unit || !unit->Type || unit == &worker) {
				continue;
			}
			const CUnitType &other = *unit->Type;
			bool needsSide = other.GivesResource && other.BoolFlag[CANHARVEST_INDEX].value;
			if (!needsSide && unit->Player == worker.Player) {
				for (int i = 1; i < MaxCosts; ++i) {
					if (other.CanStore[i]) {
						needsSide = true;
						break;
					}
				}
			}
			if (!needsSide) {
				continue;
			}
			bool sideFree = false;
			for (int y = unit->tilePos.y - 1; y <= unit->tilePos.y + other.TileHeight && !sideFree; ++y) {
				for (int x = unit->tilePos.x - 1; x <= unit->tilePos.x + other.TileWidth; ++x) {
					const Vec2i side(x, y);
					if (inBox(side) && joined[index(side)]) {
						sideFree = true;
						break;
					}
				}
			}
			if (!sideFree) {
				return false;
			}
		}
	}
	return true;
}

/**
**  Is a place on the way between a depot of the player and a resource it is harvested from?
**
**  The way is taken as the box that holds both the depot and the resource building when they
**  lie at most `lane` tiles apart. A building put there on a FALLBACK place (one that touches
**  other buildings) narrows the workers' road: seen on a small map, where a farm and a blacksmith
**  left one tile between the hall and its gold mine, two workers met in it and no gold came home
**  for the rest of the game (the ground was still in one piece, so the joined-ground rule let
**  both places pass). This is the engine's own placement made safe, not the original's rule.
**
**  @param worker  Worker to build.
**  @param type    Type of building.
**  @param pos     map tile position for the building.
**
**  @return        True if the building would stand on such a way.
*/
static bool AiPlaceOnHaulWay(const CUnit &worker, const CUnitType &type, const Vec2i &pos)
{
	const int lane = 10; // tiles between a depot and a resource that still count as one yard
	const int bx0 = pos.x, by0 = pos.y;
	const int bx1 = pos.x + type.TileWidth - 1, by1 = pos.y + type.TileHeight - 1;
	for (const CUnit *depot : worker.Player->GetUnits()) {
		if (!depot || !depot->Type || !depot->IsAliveOnMap()) {
			continue;
		}
		for (int res = 1; res < MaxCosts; ++res) {
			if (!depot->Type->CanStore[res]) {
				continue;
			}
			for (const CUnit *mine : UnitManager->GetUnits()) {
				if (!mine || !mine->Type || !mine->IsAliveOnMap()
					|| mine->Type->GivesResource != res
					|| !mine->Type->BoolFlag[CANHARVEST_INDEX].value
					|| depot->MapDistanceTo(*mine) > lane) {
					continue;
				}
				const int x0 = std::min(depot->tilePos.x, mine->tilePos.x);
				const int y0 = std::min(depot->tilePos.y, mine->tilePos.y);
				const int x1 = std::max(depot->tilePos.x + depot->Type->TileWidth, mine->tilePos.x + mine->Type->TileWidth) - 1;
				const int y1 = std::max(depot->tilePos.y + depot->Type->TileHeight, mine->tilePos.y + mine->Type->TileHeight) - 1;
				if (bx0 <= x1 && bx1 >= x0 && by0 <= y1 && by1 >= y0) {
					return true;
				}
			}
		}
	}
	return false;
}

class BuildingPlaceFinder
{
public:
	friend TerrainTraversal;
	/**
	**  Find free building place. (flood fill version)
	**
	**  @param worker   Worker to build building.
	**  @param type     Type of building.
	**  @param startPos Original position to try building
	**  @param checkSurround Check if the perimeter of the building is free
	**
	**  @return place found, std::nullopt if no found.
	*/
	static std::optional<Vec2i>
	find(const CUnit &worker, const CUnitType &type, const Vec2i &startPos, bool checkSurround)
	{
		TerrainTraversal terrainTraversal;

		terrainTraversal.SetSize(Map.Info.MapWidth, Map.Info.MapHeight);
		terrainTraversal.Init();

		Assert(Map.Info.IsPointOnMap(startPos));
		terrainTraversal.PushPos(startPos);

		BuildingPlaceFinder buildingPlaceFinder(worker, type, checkSurround);

		terrainTraversal.Run(buildingPlaceFinder);
		return buildingPlaceFinder.result();
	}

	/**
	**  Find free building place. (flood fill version)
	**
	**  @param worker   Worker to build building.
	**  @param type     Type of building.
	**  @param startUnit Original position to try building
	**  @param checkSurround Check if the perimeter of the building is free
	**
	**  @return place found, std::nullopt if no found.
	*/
	static std::optional<Vec2i> find(const CUnit &worker,
	                                 const CUnitType &type,
	                                 const CUnit &startUnit,
	                                 bool checkSurround)
	{
		TerrainTraversal terrainTraversal;

		terrainTraversal.SetSize(Map.Info.MapWidth, Map.Info.MapHeight);
		terrainTraversal.Init();
		terrainTraversal.PushUnitPosAndNeighboor(startUnit);

		BuildingPlaceFinder buildingPlaceFinder(worker, type, checkSurround);

		terrainTraversal.Run(buildingPlaceFinder);
		return buildingPlaceFinder.result();
	}


private:
	BuildingPlaceFinder(const CUnit &worker, const CUnitType &type, bool checkSurround) :
		worker(worker), type(type),
		movemask(
			worker.Type->MovementMask
			& ~((type.BoolFlag[SHOREBUILDING_INDEX].value
	                 ? (MapFieldCoastAllowed | MapFieldLandUnit | MapFieldAirUnit | MapFieldSeaUnit)
	                 : (MapFieldLandUnit | MapFieldAirUnit | MapFieldSeaUnit)))),
		checkSurround(checkSurround)
	{
	}
	VisitResult Visit(TerrainTraversal &terrainTraversal, const Vec2i &pos, const Vec2i &from);

	/// The place found: the one with free surroundings that ended the search, else the first
	/// place kept as a fallback (see Visit), else none.
	std::optional<Vec2i> result() const
	{
		return Map.Info.IsPointOnMap(resultPos) ? std::make_optional(resultPos) : std::nullopt;
	}
private:
	const CUnit &worker;
	const CUnitType &type;
	unsigned int movemask;
	bool checkSurround;
	Vec2i resultPos{-1, -1};
};

VisitResult BuildingPlaceFinder::Visit(TerrainTraversal &terrainTraversal, const Vec2i &pos, const Vec2i &from)
{
#if 0
	if (!player.AiEnabled && !Map.IsFieldExplored(player, pos)) {
		return VisitResult::DeadEnd;
	}
#endif
	if (CanBuildUnitType(&worker, type, pos, 1)
		&& !AiEnemyUnitsInDistance(*worker.Player, nullptr, pos, 8)) {
		bool backupok;
		if (AiCheckSurrounding(worker, type, pos, backupok) && checkSurround) {
			resultPos = pos;
			return VisitResult::Finished;
		} else if (backupok && resultPos.x == -1
				   && (!checkSurround || type.BoolFlag[SHOREBUILDING_INDEX].value
					   || (AiPlaceKeepsGroundJoined(worker, type, pos)
					       && !AiPlaceOnHaulWay(worker, type, pos)))) {
			// A fallback place on land is taken only when the building does not cut the ground
			// around it in two. Not asked for a building on top of a resource (no surroundings
			// checked) nor for a shore building (its ring is part sea, part land by nature).
			resultPos = pos;
			if (!checkSurround) {
				// Nothing later can replace this place (only a place with checked surroundings
				// would): the rest of the search cannot change the answer.
				return VisitResult::Finished;
			}
		}
	}
	if (CanMoveToMask(pos, movemask)
		|| (worker.Type->RepairRange == InfiniteRepairRange && type.BoolFlag[BUILDEROUTSIDE_INDEX].value)) { // reachable, or unit can build from outside and anywhere
		return VisitResult::Ok;
	} else { // unreachable
		return VisitResult::DeadEnd;
	}
}

class HallPlaceFinder
{
public:
	friend TerrainTraversal;

	/**
	**  Find building place for hall. (flood fill version)
	**
	**  The best place:
	**  1) near to resource.
	**  !2) near to wood.
	**  !3) near to worker and must be reachable.
	**  4) no enemy near it.
	**  5) no hall already near
	**  !6) enough gold in mine
	**
	**  @param worker    Worker to build building.
	**  @param type      Type of building.
	**  @param startPos  Start search position (if == -1 then unit X pos used).
	**  @param resource  resource to be near.
	**
	**  @return        place found, std::nullopt if not found.
	**
	**  @todo          FIXME: This is slow really slow, using
	**                 two flood fills, is not a perfect solution.
	*/
	static std::optional<Vec2i>
	find(const CUnit &worker, const CUnitType &type, const Vec2i &startPos, int resource)
	{
		TerrainTraversal terrainTraversal;

		terrainTraversal.SetSize(Map.Info.MapWidth, Map.Info.MapHeight);
		terrainTraversal.Init();

		Assert(Map.Info.IsPointOnMap(startPos));
		terrainTraversal.PushPos(startPos);

		HallPlaceFinder hallPlaceFinder(worker, type, resource);

		if (terrainTraversal.Run(hallPlaceFinder)) {
			return hallPlaceFinder.resultPos;
		}
		return BuildingPlaceFinder::find(worker, type, startPos, true);
	}

private:
	HallPlaceFinder(const CUnit &worker, const CUnitType &type, int resource) :
		worker(worker),
		type(type),
		movemask(
			worker.Type->MovementMask
			& ~((type.BoolFlag[SHOREBUILDING_INDEX].value
	                 ? (MapFieldCoastAllowed | MapFieldLandUnit | MapFieldAirUnit | MapFieldSeaUnit)
	                 : (MapFieldLandUnit | MapFieldAirUnit | MapFieldSeaUnit)))),
		resource(resource)
	{}
	VisitResult Visit(TerrainTraversal &terrainTraversal, const Vec2i &pos, const Vec2i &from);
private:
	bool IsAUsableMine(const CUnit &mine) const;
private:
	const CUnit &worker;
	const CUnitType &type;
	const unsigned int movemask;
	const int resource;
	Vec2i resultPos{-1, -1};
};

bool HallPlaceFinder::IsAUsableMine(const CUnit &mine) const
{
	// Check units around mine
	const Vec2i offset(5, 5);
	const Vec2i minpos = mine.tilePos - offset;
	const Vec2i typeSize(mine.Type->TileWidth - 1, mine.Type->TileHeight - 1);
	const Vec2i maxpos = mine.tilePos + typeSize + offset;
	std::vector<CUnit *> units = Select(minpos, maxpos);
	const size_t nunits = units.size();
	int buildings = 0;

	for (size_t j = 0; j < nunits; ++j) {
		const CUnit &unit = *units[j];
		// Enemy near mine
		if (AiPlayer->Player->IsEnemy(*unit.Player)) {
			return false;
		}
		// Town hall near mine
		if (unit.Type->CanStore[resource]) {
			return false;
		}
		// Town hall may not be near but we may be using it, check
		// for 2 buildings near it and assume it's been used
		if (unit.Type->Building && !(unit.Type->GivesResource == resource)) {
			++buildings;
			if (buildings == 2) {
				return false;
			}
		}
	}
	return true;
}

VisitResult HallPlaceFinder::Visit(TerrainTraversal &terrainTraversal, const Vec2i &pos, const Vec2i &from)
{
#if 0
	if (!player.AiEnabled && !Map.IsFieldExplored(player, pos)) {
		return VisitResult::DeadEnd;
	}
#endif
	CUnit *mine = ResourceOnMap(pos, resource);
	if (mine && IsAUsableMine(*mine)) {
		if (auto place = BuildingPlaceFinder::find(worker, type, *mine, true)) {
			resultPos = *place;
			return VisitResult::Finished;
		}
	}
	if (CanMoveToMask(pos, movemask)) { // reachable
		return VisitResult::Ok;
	} else { // unreachable
		return VisitResult::DeadEnd;
	}
}

class LumberMillPlaceFinder
{
public:
	friend TerrainTraversal;
	/**
	**  Find free building place for lumber mill. (flood fill version)
	**
	**  @param worker    Worker to build building.
	**  @param type      Type of building.
	**  @param resource  resource terrain to be near.
	**  @param startPos  Start search X position (if == -1 then unit X pos used).
	**
	**  @return        place found, std::nullopt if not found.
	**
	**  @todo          FIXME: This is slow really slow, using two flood fills, is not a perfect solution.
	*/
	static std::optional<Vec2i>
	find(const CUnit &worker, const CUnitType &type, const Vec2i &startPos, int resource)
	{
		TerrainTraversal terrainTraversal;

		terrainTraversal.SetSize(Map.Info.MapWidth, Map.Info.MapHeight);
		terrainTraversal.Init();

		Assert(Map.Info.IsPointOnMap(startPos));
		terrainTraversal.PushPos(startPos);

		LumberMillPlaceFinder lumberMillPlaceFinder(worker, type, resource);

		return terrainTraversal.Run(lumberMillPlaceFinder)
		         ? std::make_optional(lumberMillPlaceFinder.resultPos)
		         : std::nullopt;
	}

private:
	LumberMillPlaceFinder(const CUnit &worker,
	                      const CUnitType &type,
	                      int resource) :
		worker(worker),
		type(type),
		movemask(worker.Type->MovementMask
	             & ~(MapFieldLandUnit | MapFieldAirUnit | MapFieldSeaUnit)),
		resource(resource)
	{}

	VisitResult Visit(TerrainTraversal &terrainTraversal, const Vec2i &pos, const Vec2i &from);
private:
	const CUnit &worker;
	const CUnitType &type;
	unsigned int movemask;
	int resource;
	Vec2i resultPos{-1, -1};
};

VisitResult LumberMillPlaceFinder::Visit(TerrainTraversal &terrainTraversal, const Vec2i &pos, const Vec2i &from)
{
#if 0
	if (!player.AiEnabled && !Map.IsFieldExplored(player, pos)) {
		return VisitResult::DeadEnd;
	}
#endif
	if (Map.Field(pos)->IsTerrainResourceOnMap(resource)) {
		if (auto place = BuildingPlaceFinder::find(worker, type, from, true)) {
			resultPos = *place;
			return VisitResult::Finished;
		}
	}
	if (CanMoveToMask(pos, movemask)) { // reachable
		return VisitResult::Ok;
	} else { // unreachable
		return VisitResult::DeadEnd;
	}
}

static std::optional<Vec2i>
AiFindMiningPlace(const CUnit &worker, const CUnitType &type, const Vec2i &startPos, int resource)
{
	// look near (mine = ResourceOnMap(pos, resource, false) ?
	return BuildingPlaceFinder::find(worker, type, startPos, false);
}

/// The two placement rules of this port that the original does not have (the ground stays in one
/// piece, no building on the way between a depot and its resource) are still asked for a player
/// run by its original script (local evidence record AI-PLACE-001 shows no such test); each place
/// they refuse is logged. To be set false once the original's grid is seen in a game.
constexpr bool PlaceSafetyRulesForScripted = true;

namespace
{
/// What the original's placement walk asks about a tile, for one worker and one building type.
struct ScriptedPlace {
	const CUnit &worker;
	const CUnitType &type;
	std::vector<char> ground; ///< tiles the worker's ground holds (terrain only), filled when first asked
};
} // namespace

/// Is the tile on the worker's ground: joined to the worker's own tile over terrain a unit of
/// its kind can stand on, units and buildings not looked at (the original compares region
/// numbers of the terrain). One fill of the map per walk.
static bool ScriptedPlaceSameGround(int x, int y, void *context)
{
	ScriptedPlace &place = *static_cast<ScriptedPlace *>(context);
	const int width = Map.Info.MapWidth;
	const int height = Map.Info.MapHeight;
	if (place.ground.empty()) {
		place.ground.assign(static_cast<size_t>(width) * height, 0);
		const tile_flags mask = place.worker.Type->MovementMask
		                        & ~(MapFieldLandUnit | MapFieldSeaUnit | MapFieldAirUnit | MapFieldBuilding);
		std::deque<Vec2i> open;
		open.push_back(place.worker.tilePos);
		place.ground[static_cast<size_t>(place.worker.tilePos.y) * width + place.worker.tilePos.x] = 1;
		while (!open.empty()) {
			const Vec2i tile = open.front();
			open.pop_front();
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const Vec2i next(tile.x + dx, tile.y + dy);
					if (!Map.Info.IsPointOnMap(next)) {
						continue;
					}
					char &seen = place.ground[static_cast<size_t>(next.y) * width + next.x];
					if (!seen && !Map.Field(next)->CheckMask(mask)) {
						seen = 1;
						open.push_back(next);
					}
				}
			}
		}
	}
	return x >= 0 && y >= 0 && x < width && y < height && place.ground[static_cast<size_t>(y) * width + x] != 0;
}

static bool ScriptedPlaceFits(int x, int y, void *context)
{
	const ScriptedPlace &place = *static_cast<const ScriptedPlace *>(context);
	const Vec2i pos(x, y);
	if (!CanBuildUnitType(&place.worker, place.type, pos, 1)) {
		return false;
	}
	if (PlaceSafetyRulesForScripted
		&& (!AiPlaceKeepsGroundJoined(place.worker, place.type, pos) || AiPlaceOnHaulWay(place.worker, place.type, pos))) {
		LogPrint("wc2ai: place vetoed (port rule) %s %d %d\n", place.type.Ident.c_str(), x, y);
		return false;
	}
	return true;
}

/**
**  Find free building place.
**
**  @param worker     Worker to build building.
**  @param type       Type of building.
**  @param nearPos    Start search near nearPos position (or worker->X if nearPos is invalid).
**
**  @return        place found, std::nullopt if no found.
**
**  @todo          Better and faster way to find building place of oil
**                 platforms Special routines for special buildings.
*/
std::optional<Vec2i> AiFindBuildingPlace(const CUnit &worker, const CUnitType &type, const Vec2i &nearPos)
{
	// Find a good place for a new hall
	DebugPrint("%d: Want to build a %s(%s)\n",
	           AiPlayer->Player->Index,
	           type.Ident.c_str(),
	           type.Name.c_str());

	// A player run by its original script: farms and the land buildings go on the original's grid
	// round the hall (wc2ai::PlaceWalk). Without a place the worker gives up for this time. The
	// shore buildings, the halls and the oil platform stay with the finders below.
	if (Wc2AiRuns(worker.Player->Index)) {
		const int step = Wc2AiPlaceStepOf(type);
		if (step > 1) {
			int refX;
			int refY;
			Wc2AiPlaceReference(worker, refX, refY);
			ScriptedPlace context{worker, type, {}};
			const wc2ai::Place place = wc2ai::PlaceWalk(refX, refY, step, Map.Info.MapWidth,
			                                            ScriptedPlaceSameGround, ScriptedPlaceFits, &context);
			return place.found ? std::make_optional(Vec2i(place.x, place.y)) : std::nullopt;
		}
	}

	const Vec2i &startPos = Map.Info.IsPointOnMap(nearPos) ? nearPos : worker.tilePos;

	//Mines and Depots
	for (int i = 1; i < MaxCosts; ++i) {
		const auto &resinfo = worker.Type->ResInfo[i];
		//Depots
		if (type.CanStore[i]) {
			if (resinfo && resinfo->TerrainHarvester) {
				return LumberMillPlaceFinder::find(worker, type, startPos, i);
			} else {
				return HallPlaceFinder::find(worker, type, startPos, i);
			}
		} else {
			//mines
			if (type.GivesResource == i) {
				if (resinfo && resinfo->RefineryHarvester) {
					//Mine have to be build ONTOP resources
					return AiFindMiningPlace(worker, type, startPos, i);
				} else {
					//Mine can be build without resource restrictions: solar panels, etc
					return BuildingPlaceFinder::find(worker, type, startPos, true);
				}
			}
		}
	}
	return BuildingPlaceFinder::find(worker, type, startPos, true);
}

//@}
