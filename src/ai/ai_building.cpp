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

#include "action/action_build.h"

#include "map.h"
#include "pathfinder.h"
#include "player.h"
#include "pud.h"
#include "tileset.h"
#include "unit.h"
#include "unit_find.h"
#include "unit_manager.h"
#include "unittype.h"
#include "wc2ai.h"
#include "wc2bot.h"

#include <algorithm>
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

/// Is the tile open ground for the haul way: on the map, passable terrain, no building on it.
static bool HaulWayOpenAt(int x, int y, void *context)
{
	return IsPosFree(Vec2i(x, y), *static_cast<const CUnit *>(context));
}

/**
**  Is a place on the way between a depot of the player and a resource it is harvested from?
**
**  The way is the straight line between the centre tiles of the depot and of the resource
**  building when they lie at most `lane` tiles apart (wc2ai::HaulLineTiles). A building put there
**  on a FALLBACK place (one that touches other buildings) narrows the workers' road: seen on a
**  small map, where a farm and a blacksmith left one tile between the hall and its gold mine, two
**  workers met in it and no gold came home for the rest of the game (the ground was still in one
**  piece, so the joined-ground rule let both places pass). So a building may not cover the line
**  nor leave it one tile wide; it may touch it (live run 43, item 2: the original's computer put
**  a Pig Farm right beside its mine-hall line). This is the engine's own placement made safe, not
**  the original's rule.
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
				const wc2ai::Site depotSite{depot->tilePos.x, depot->tilePos.y,
				                            depot->Type->TileWidth, depot->Type->TileHeight};
				const wc2ai::Site mineSite{mine->tilePos.x, mine->tilePos.y,
				                           mine->Type->TileWidth, mine->Type->TileHeight};
				if (wc2ai::HaulLineBlocked(pos.x, pos.y, type.TileWidth, type.TileHeight,
				                           wc2ai::HaulLineTiles(depotSite, mineSite), HaulWayOpenAt,
				                           const_cast<CUnit *>(&worker))) {
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

	/// As above from round startUnit, only on tiles at most `radius` from it (0: no limit).
	static std::optional<Vec2i> findWithin(const CUnit &worker,
	                                       const CUnitType &type,
	                                       const CUnit &startUnit,
	                                       int radius)
	{
		TerrainTraversal terrainTraversal;

		terrainTraversal.SetSize(Map.Info.MapWidth, Map.Info.MapHeight);
		terrainTraversal.Init();
		terrainTraversal.PushUnitPosAndNeighboor(startUnit);

		BuildingPlaceFinder buildingPlaceFinder(worker, type, true);
		if (radius > 0) {
			buildingPlaceFinder.centre = &startUnit;
			buildingPlaceFinder.radius = radius;
		}

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
	const CUnit *centre = nullptr; ///< findWithin: the unit the search keeps near
	int radius = 0;                ///< findWithin: most tiles from centre (0: no limit)
	Vec2i resultPos{-1, -1};
};

VisitResult BuildingPlaceFinder::Visit(TerrainTraversal &terrainTraversal, const Vec2i &pos, const Vec2i &from)
{
	if (centre != nullptr && centre->MapDistanceTo(pos) > radius) {
		return VisitResult::DeadEnd;
	}
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
/// they refuse is logged. To be set false once the original's grid is seen in a game. The
/// original's reserved tiles (DECODE_B8 item 2, now kept) do not stand in for them everywhere: no
/// road is marked when the depot and the mine share a row or a column, none for lumber or oil,
/// and nothing keeps the ground in one piece (host test "reserved tiles: what the haul-way rule
/// covers that the marks do not").
constexpr bool PlaceSafetyRulesForScripted = true;

namespace
{
/// What the original's placement walk asks about a tile, for one worker and one building type.
struct ScriptedPlace {
	const CUnit &worker;
	const CUnitType &type;
	std::vector<char> ground; ///< tiles the worker's ground holds (terrain only), filled when first asked
	std::vector<wc2ai::Site> sites; ///< where the slot's other workers are on their way to build
	std::vector<char> water;  ///< shipyard: the water the oil patch lies in (empty: not asked)
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

/// A shore building's candidate is on the worker's ground when a tile of its square or of the
/// ring round it is (its own tiles are coast and water, which the land fill does not hold).
static bool ScriptedShoreSameGround(int x, int y, void *context)
{
	ScriptedPlace &place = *static_cast<ScriptedPlace *>(context);
	for (int dy = -1; dy <= place.type.TileHeight; ++dy) {
		for (int dx = -1; dx <= place.type.TileWidth; ++dx) {
			if (ScriptedPlaceSameGround(x + dx, y + dy, context)) {
				return true;
			}
		}
	}
	return false;
}

/// The water joined to the tiles round a unit (an oil patch), units not looked at.
static std::vector<char> WaterRound(const CUnit &unit)
{
	const int width = Map.Info.MapWidth;
	const int height = Map.Info.MapHeight;
	std::vector<char> water(static_cast<size_t>(width) * height, 0);
	std::deque<Vec2i> open;
	for (int dy = -1; dy <= unit.Type->TileHeight; ++dy) {
		for (int dx = -1; dx <= unit.Type->TileWidth; ++dx) {
			const Vec2i tile(unit.tilePos.x + dx, unit.tilePos.y + dy);
			if (Map.Info.IsPointOnMap(tile) && Map.Field(tile)->WaterOnMap()) {
				water[static_cast<size_t>(tile.y) * width + tile.x] = 1;
				open.push_back(tile);
			}
		}
	}
	while (!open.empty()) {
		const Vec2i tile = open.front();
		open.pop_front();
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				const Vec2i next(tile.x + dx, tile.y + dy);
				if (!Map.Info.IsPointOnMap(next)) {
					continue;
				}
				char &seen = water[static_cast<size_t>(next.y) * width + next.x];
				if (!seen && Map.Field(next)->WaterOnMap()) {
					seen = 1;
					open.push_back(next);
				}
			}
		}
	}
	return water;
}

/// Does the square at (x, y), or the ring round it, touch the patch's water?
static bool TouchesWater(const ScriptedPlace &place, int x, int y)
{
	const int width = Map.Info.MapWidth;
	for (int dy = -1; dy <= place.type.TileHeight; ++dy) {
		for (int dx = -1; dx <= place.type.TileWidth; ++dx) {
			const Vec2i tile(x + dx, y + dy);
			if (Map.Info.IsPointOnMap(tile) && place.water[static_cast<size_t>(tile.y) * width + tile.x]) {
				return true;
			}
		}
	}
	return false;
}

/// The sites the slot's other workers are on their way to build on (a Build order whose building
/// is not laid yet). The original marks such a site on the map until the worker's task ends.
static std::vector<wc2ai::Site> PlannedSites(const CUnit &worker)
{
	std::vector<wc2ai::Site> sites;
	for (const CUnit *unit : worker.Player->GetUnits()) {
		if (!unit || unit == &worker || !unit->IsAlive()) {
			continue;
		}
		for (const auto &order : unit->Orders) {
			if (order && order->Action == UnitAction::Build) {
				const COrder_Build &build = static_cast<const COrder_Build &>(*order);
				if (!build.GetBuildingUnit()) {
					const Vec2i pos = build.GetGoalPos();
					sites.push_back({pos.x, pos.y, build.GetUnitType().TileWidth, build.GetUnitType().TileHeight});
				}
			}
		}
	}
	return sites;
}

static bool ScriptedPlaceFits(int x, int y, void *context)
{
	const ScriptedPlace &place = *static_cast<const ScriptedPlace *>(context);
	const Vec2i pos(x, y);
	if (wc2ai::SiteTaken(x, y, place.type.TileWidth, place.type.TileHeight, place.sites)) {
		return false;
	}
	// the original's ring walk masks (0x0DDE / 0x0D11) hold the reserved-tile flag 0x400
	if (Wc2AiReservedAt(x, y, place.type.TileWidth, place.type.TileHeight)) {
		return false;
	}
	if (!CanBuildUnitType(&place.worker, place.type, pos, 1)) {
		return false;
	}
	if (!place.water.empty() && !TouchesWater(place, x, y)) {
		return false;
	}
	if (PlaceSafetyRulesForScripted
		&& (!AiPlaceKeepsGroundJoined(place.worker, place.type, pos) || AiPlaceOnHaulWay(place.worker, place.type, pos))) {
		LogPrint("wc2ai: place vetoed (port rule) %s %d %d\n", place.type.Ident.c_str(), x, y);
		return false;
	}
	return true;
}

/// Does the player have a hall-class building (town hall, keep, castle, both races), finished or not?
static bool HasHall(const CPlayer &player)
{
	for (const CUnit *unit : player.GetUnits()) {
		if (unit && unit->IsAlive() && unit->Type->Building && unit->Type->CanStore[GoldCost]
			&& unit->Type->Ident != "unit-human-oil-platform" && unit->Type->Ident != "unit-orc-oil-platform") {
			return true;
		}
	}
	return false;
}

/// The player's finished shipyards, both races.
static int FinishedShipyards(const CPlayer &player)
{
	int count = 0;
	for (const CUnit *unit : player.GetUnits()) {
		if (unit && unit->IsAliveOnMap() && unit->CurrentAction() != UnitAction::Built
			&& (unit->Type->Ident == "unit-human-shipyard" || unit->Type->Ident == "unit-orc-shipyard")) {
			++count;
		}
	}
	return count;
}

/// A tanker of a player run by its original script builds its platform on the free oil patch
/// nearest to its depot (the first of equals); without a depot it builds nothing.
static std::optional<Vec2i> ScriptedPlatformPlace(const CUnit &tanker, const CUnitType &type)
{
	const CUnit *depot = FindDeposit(tanker, 1000, OilCost);
	if (!depot) {
		return std::nullopt;
	}
	std::vector<Vec2i> patches;
	std::vector<int> distances;
	for (const CUnit *unit : Players[PlayerNumNeutral].GetUnits()) {
		if (unit && unit->IsAliveOnMap() && unit->Type->Ident == "unit-oil-patch"
			&& CanBuildUnitType(&tanker, type, unit->tilePos, 1)) {
			patches.push_back(unit->tilePos);
			distances.push_back(depot->MapDistanceTo(*unit));
		}
	}
	const int patch = wc2ai::NearestOf(distances, false);
	return patch < 0 ? std::nullopt : std::make_optional(patches[patch]);
}

/// Is the unit a hall-class building (Town Hall, Keep, Castle and the orc ones), of any player?
static bool HallClass(const CUnit &unit)
{
	const int number = wc2ai::TypeNumberOfIdent(wc2pud::UnitScriptNames, 0x69, unit.Type->Ident);
	return number == 0x4A || number == 0x4B || (number >= 0x58 && number <= 0x5B);
}

/// A further hall of a player run by its original script (0x4C4B10, DECODE_B8 item 1): beside the
/// gold mine with a stock of at least 50 (wc2ai::FurtherHallMineStock) that has no hall of any
/// player within 5 tiles round it, on the ring walk round the mine's top-left tile, step 1; the
/// place is kept when its top-left tile is at most 10 tiles from the mine, the nearest of the kept
/// ones wins (the first mine of equals). None: the build-list entry is passed over.
static std::optional<Vec2i> ScriptedFurtherHallPlace(const CUnit &worker, const CUnitType &type)
{
	std::vector<const CUnit *> halls;
	for (int p = 0; p < PlayerMax; ++p) {
		for (const CUnit *unit : Players[p].GetUnits()) {
			if (unit && unit->IsAliveOnMap() && HallClass(*unit)) {
				halls.push_back(unit);
			}
		}
	}
	ScriptedPlace context{worker, type, {}, PlannedSites(worker), {}};
	std::vector<Vec2i> places;
	std::vector<int> distances;
	for (const CUnit *mine : Players[PlayerNumNeutral].GetUnits()) {
		if (!mine || !mine->IsAliveOnMap() || mine->Type->Ident != "unit-gold-mine"
			|| !wc2ai::FurtherHallMineStock(mine->ResourcesHeld)) {
			continue;
		}
		const Vec2i &m = mine->tilePos;
		const bool hallNear = std::any_of(halls.begin(), halls.end(), [&](const CUnit *hall) {
			return wc2ai::FurtherHallWindowHit(m.x, m.y, hall->tilePos.x, hall->tilePos.y,
			                                   hall->Type->TileWidth, hall->Type->TileHeight);
		});
		if (hallNear) {
			continue;
		}
		const wc2ai::Place place = wc2ai::PlaceWalk(m.x, m.y, 1, Map.Info.MapWidth, ScriptedPlaceSameGround,
		                                            ScriptedPlaceFits, &context);
		if (!place.found) {
			continue;
		}
		const int distance = wc2ai::PointToBoxDistance(place.x, place.y, m.x, m.y,
		                                               mine->Type->TileWidth, mine->Type->TileHeight);
		if (wc2ai::FurtherHallKeeps(distance)) {
			places.push_back(Vec2i(place.x, place.y));
			distances.push_back(distance);
		}
	}
	const int best = wc2ai::NearestOf(distances, false);
	return best < 0 ? std::nullopt : std::make_optional(places[best]);
}

namespace
{

/// The bot's hall its buildings go near: the standing own hall (a gold depot) nearest the player's
/// start position, else nearest the worker; nullptr with none.
const CUnit *BotHomeHall(const CPlayer &player, const CUnit &worker)
{
	const Vec2i from = Map.Info.IsPointOnMap(player.StartPos) ? player.StartPos : worker.tilePos;
	const CUnit *best = nullptr;
	int bestDistance = 0;
	for (const CUnit *unit : player.GetUnits()) {
		if (!unit || !unit->Type || !unit->IsAliveOnMap() || !unit->Type->Building
		    || !unit->Type->CanStore[GoldCost] || unit->CurrentAction() == UnitAction::Built) {
			continue;
		}
		const int d = unit->MapDistanceTo(from);
		if (best == nullptr || d < bestDistance) {
			best = unit;
			bestDistance = d;
		}
	}
	return best;
}

struct BotPlaceTry {
	const CUnit &worker;
	const CUnitType &type;
	const CUnit &hall;
	std::optional<Vec2i> place;
};

bool BotPlaceFits(int radius, void *context)
{
	BotPlaceTry &attempt = *static_cast<BotPlaceTry *>(context);
	attempt.place = BuildingPlaceFinder::findWithin(attempt.worker, attempt.type, attempt.hall, radius);
	return attempt.place.has_value();
}

/// A further hall of a bot (it has a hall already): beside the first gold mine of
/// wc2bot::ExpansionMines (gold left, on the worker's ground, no hall of any player near it,
/// finished or not; the nearest to the start first) with room within NearHallRadii[0] tiles of
/// it; none: no place (the hall never goes beside home). Check 3.4.66 (Unyielding): the engine's
/// finder took the main mine (its hall 8 tiles off, past the finder's 5-tile look) and walked
/// from it through the base to a free place 9 tiles from the main hall.
std::optional<Vec2i> BotExpansionPlace(const CUnit &worker, const CUnitType &type)
{
	std::vector<wc2bot::Point> halls;
	for (int p = 0; p < PlayerMax; ++p) {
		for (const CUnit *unit : Players[p].GetUnits()) {
			if (unit && unit->IsAlive() && HallClass(*unit)) {
				halls.push_back({unit->tilePos.x, unit->tilePos.y});
			}
		}
	}
	ScriptedPlace ground{worker, type, {}, {}, {}};
	std::vector<const CUnit *> units;
	std::vector<wc2bot::Mine> mines;
	for (const CUnit *mine : Players[PlayerNumNeutral].GetUnits()) {
		if (mine && mine->IsAliveOnMap() && mine->Type->GivesResource == GoldCost) {
			units.push_back(mine);
			mines.push_back({{mine->tilePos.x, mine->tilePos.y}, mine->ResourcesHeld,
			                 ScriptedPlaceSameGround(mine->tilePos.x, mine->tilePos.y, &ground)});
		}
	}
	const Vec2i &start = Map.Info.IsPointOnMap(worker.Player->StartPos) ? worker.Player->StartPos : worker.tilePos;
	for (const int i : wc2bot::ExpansionMines(mines, halls, {start.x, start.y})) {
		if (auto place = BuildingPlaceFinder::findWithin(worker, type, *units[i], wc2bot::NearHallRadii[0])) {
			return place;
		}
	}
	return std::nullopt;
}

} // namespace

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

	// A player run by its original script: farms, the land buildings, the shore buildings and the
	// first hall go on the original's grid (wc2ai::PlaceWalk) round the hall, the first hall round
	// the nearest gold mine; a site another worker of the slot is on its way to is taken. Without a
	// place the worker gives up for this time. The oil platform goes on the free patch nearest to the
	// tanker's depot; a further hall beside a gold mine with no hall near it (ScriptedFurtherHallPlace).
	// Every place found marks its border as reserved (the worker is sent at once by the caller).
	if (Wc2AiRuns(worker.Player->Index)) {
		if (type.Ident == "unit-human-oil-platform" || type.Ident == "unit-orc-oil-platform") {
			return ScriptedPlatformPlace(worker, type);
		}
		const int step = Wc2AiPlaceStepOf(type);
		const int number = wc2ai::TypeNumberOfIdent(wc2pud::UnitScriptNames, 0x69, type.Ident);
		const bool firstHall = (number == 0x4A || number == 0x4B) && !HasHall(*worker.Player);
		if ((number == 0x4A || number == 0x4B) && !firstHall) {
			const std::optional<Vec2i> place = ScriptedFurtherHallPlace(worker, type);
			if (place) {
				Wc2AiReservePlanned(type, place->x, place->y);
			}
			return place;
		}
		if (step > 1 || (step == 1 && number != 0x4A && number != 0x4B) || firstHall) {
			ScriptedPlace context{worker, type, {}, PlannedSites(worker), {}};
			int refX;
			int refY;
			Wc2AiPlaceReference(worker, refX, refY);
			if (number == 0x48 || number == 0x49) {
				// the oil patch nearest to the worker, anywhere on the map (later one of equals)
				std::vector<CUnit *> patches;
				std::vector<int> distances;
				for (CUnit *unit : Players[PlayerNumNeutral].GetUnits()) {
					if (unit && unit->IsAliveOnMap() && unit->Type->Ident == "unit-oil-patch") {
						patches.push_back(unit);
						distances.push_back(worker.MapDistanceTo(*unit));
					}
				}
				const int patch = wc2ai::NearestOf(distances, true);
				if (patch < 0) {
					return std::nullopt;
				}
				context.water = WaterRound(*patches[patch]);
			} else if (!wc2ai::ShipyardWorkAllowed(number, FinishedShipyards(*worker.Player))) {
				return std::nullopt;
			} else if (firstHall) {
				// round the gold mine with gold on the worker's ground nearest to it (first of
				// equals), else round the worker
				refX = worker.tilePos.x;
				refY = worker.tilePos.y;
				std::vector<CUnit *> mines;
				std::vector<int> distances;
				for (CUnit *unit : Players[PlayerNumNeutral].GetUnits()) {
					if (unit && unit->IsAliveOnMap() && unit->Type->Ident == "unit-gold-mine" && unit->ResourcesHeld > 0
						&& ScriptedPlaceSameGround(unit->tilePos.x, unit->tilePos.y, &context)) {
						mines.push_back(unit);
						distances.push_back(worker.MapDistanceTo(*unit));
					}
				}
				const int mine = wc2ai::NearestOf(distances, false);
				if (mine >= 0) {
					refX = mines[mine]->tilePos.x;
					refY = mines[mine]->tilePos.y;
				}
			}
			const bool shore = type.BoolFlag[SHOREBUILDING_INDEX].value;
			const wc2ai::Place place = wc2ai::PlaceWalk(refX, refY, step, Map.Info.MapWidth,
			                                            shore ? ScriptedShoreSameGround : ScriptedPlaceSameGround,
			                                            ScriptedPlaceFits, &context);
			if (!place.found) {
				return std::nullopt;
			}
			Wc2AiReservePlanned(type, place.x, place.y);
			return Vec2i(place.x, place.y);
		}
	}

	// A computer player of a mod (wc2bot) with a hall: a further hall only at a real expansion site.
	if (Wc2BotRuns(worker.Player->Index) && type.CanStore[GoldCost] && HasHall(*worker.Player)) {
		return BotExpansionPlace(worker, type);
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
	// A computer player of a mod (wc2bot) asking with no place of its own: near its hall, the
	// nearest place within a few tiles of it, further only when nothing fits there
	// (wc2bot::NearHallRadii). Check 0.2.1: the search from the worker's own tile put a farm ~32
	// tiles from the hall. With no hall, or nothing round it, from the worker as before.
	if (Wc2BotRuns(worker.Player->Index) && !Map.Info.IsPointOnMap(nearPos)) {
		if (const CUnit *hall = BotHomeHall(*worker.Player, worker)) {
			BotPlaceTry attempt{worker, type, *hall, std::nullopt};
			if (wc2bot::PlaceNearHall(BotPlaceFits, &attempt)) {
				return attempt.place;
			}
		}
	}
	return BuildingPlaceFinder::find(worker, type, startPos, true);
}

//@}
