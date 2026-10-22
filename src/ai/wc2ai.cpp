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
/**@name wc2ai.cpp - The original game's computer-player definitions in the engine. */
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

#include "stratagus.h"

#include "wc2ai.h"
#include "wc2ai_ferry.h"

#include "../ai/ai_local.h"
#include "action/action_resource.h"
#include "actions.h"
#include "commands.h"
#include "depend.h"
#include "home_leash.h"
#include "iolib.h"
#include "map.h"
#include "pathfinder.h"
#include "pud.h"
#include "script.h"
#include "player.h"
#include "settings.h"
#include "unit.h"
#include "unit_find.h"
#include "unit_manager.h"
#include "unittype.h"
#include "upgrade.h"
#include "upgrade_structs.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <iterator>
#include <set>
#include <utility>

namespace wc2ai
{

/// Read Data/Rez/ai.bin of the game folder once. The file is the player's own; nothing of it is
/// shipped. Absent or not decodable: an empty program, and the callers keep their own behaviour.
const Program &GameProgram()
{
	static Program program;
	static bool tried = false;
	if (tried) {
		return program;
	}
	tried = true;
	const fs::path resolved = LibraryFileName("Data/Rez/ai.bin");
	if (resolved.empty()) {
		LogPrint("wc2ai: no Data/Rez/ai.bin in the game folder\n");
		return program;
	}
	std::error_code ec;
	const uintmax_t size = fs::file_size(resolved, ec);
	if (ec || size == 0 || size > 0xFFFF) {
		LogPrint("wc2ai: Data/Rez/ai.bin has an unexpected size\n");
		return program;
	}
	std::vector<uint8_t> data(static_cast<size_t>(size));
	CFile file;
	if (file.open(resolved.string().c_str(), CL_OPEN_READ) == -1) {
		LogPrint("wc2ai: Data/Rez/ai.bin cannot be opened\n");
		return program;
	}
	const bool whole = static_cast<size_t>(file.read(data.data(), data.size())) == data.size();
	file.close();
	std::string error;
	if (!whole || !program.Parse(data.data(), data.size(), &error)) {
		LogPrint("wc2ai: Data/Rez/ai.bin not used: %s\n", whole ? error.c_str() : "short read");
		return program;
	}
	LogPrint("wc2ai: %d scripts\n", ScriptCount);
	return program;
}

/// Read Data/Rez/script.bin of the game folder once, for the attack rates. The file is the
/// player's own; nothing of it is shipped. Absent or not decodable: every rate is 0.
const uint8_t *GameAttackRates()
{
	static uint8_t rates[RateTypes] = {};
	static bool tried = false;
	if (tried) {
		return rates;
	}
	tried = true;
	const fs::path resolved = LibraryFileName("Data/Rez/script.bin");
	if (resolved.empty()) {
		LogPrint("wc2ai: no Data/Rez/script.bin in the game folder: no attack rates\n");
		return rates;
	}
	std::error_code ec;
	const uintmax_t size = fs::file_size(resolved, ec);
	if (ec || size == 0 || size > 0xFFFF) {
		LogPrint("wc2ai: Data/Rez/script.bin has an unexpected size: no attack rates\n");
		return rates;
	}
	std::vector<uint8_t> data(static_cast<size_t>(size));
	CFile file;
	if (file.open(resolved.string().c_str(), CL_OPEN_READ) == -1) {
		LogPrint("wc2ai: Data/Rez/script.bin cannot be opened: no attack rates\n");
		return rates;
	}
	const bool whole = static_cast<size_t>(file.read(data.data(), data.size())) == data.size();
	file.close();
	if (!whole || !AttackRates(data.data(), data.size(), rates)) {
		LogPrint("wc2ai: Data/Rez/script.bin not used: %s\n", whole ? "it does not decode" : "short read");
		return rates;
	}
	LogPrint("wc2ai: attack rates of %d unit types\n", RateTypes);
	return rates;
}

} // namespace wc2ai

static std::array<int, PlayerMax> Wc2AiIds = [] {
	std::array<int, PlayerMax> ids;
	ids.fill(-1);
	return ids;
}();

int Wc2AiIdOf(int player)
{
	return (player >= 0 && player < PlayerMax) ? Wc2AiIds[player] : -1;
}

static std::array<wc2ai::State, PlayerMax> Wc2AiStates;

/// What the strength map is built from and the map itself, kept for one map: the original's
/// type number per engine unit type (by CUnitType::Slot; TypeNumberUnknown = not looked up yet),
/// the kind of each cell, and the map of the stretch of 50 game cycles it was last asked for in.
constexpr int TypeNumberUnknown = -2;
static std::vector<int> Wc2AiTypeNumbers;
static std::vector<uint8_t> Wc2AiCellKinds;
static wc2ai::CellMap Wc2AiCellMap;
static long Wc2AiCellTick = -1;

/// The reserved tiles of the scripted slots (tile flag 0x400 of the original, DECODE_B8 item 2), one
/// byte per map tile, kept for one map; and whether the map's own buildings have been marked (at
/// the first cycle of a new game, or read back from a saved game).
static std::vector<uint8_t> Wc2AiReserved;
static bool Wc2AiReservedReady = false;

/// The land units of each scripted slot that wait for a transport, and the islands of the map in
/// use (wc2ai::LandRegions, kept for one map). See FerryPass.
struct FerryWait {
	int unit = -1;      ///< unit number of the waiting land unit
	int transport = -1; ///< unit number of the transport that has it, -1 none
	int region = 0;     ///< island of its wave place
	int x = -1, y = -1; ///< its wave place (its home tile); another home = it waits no more
};
static std::array<std::vector<FerryWait>, PlayerMax> Wc2AiFerry;
static std::vector<int> Wc2AiRegions;

void Wc2AiClearIds()
{
	for (auto &waits : Wc2AiFerry) {
		waits.clear();
	}
	Wc2AiRegions.clear();
	Wc2AiReserved.clear();
	Wc2AiReservedReady = false;
	Wc2AiIds.fill(-1);
	Wc2AiStates.fill(wc2ai::State());
	Wc2AiTypeNumbers.clear();
	Wc2AiCellKinds.clear();
	Wc2AiCellMap = wc2ai::CellMap();
	Wc2AiCellTick = -1;
}

void Wc2AiDropId(int player)
{
	if (player >= 0 && player < PlayerMax) {
		Wc2AiIds[player] = -1;
		Wc2AiStates[player] = wc2ai::State();
		Wc2AiFerry[player].clear();
	}
}

/// The original's number of an engine unit type, by its ident in the map converter's table;
/// -1 for a type that has none among the types with an attack rate.
static int TypeNumberOf(const CUnitType &type)
{
	const size_t slot = static_cast<size_t>(type.Slot);
	if (slot >= Wc2AiTypeNumbers.size()) {
		Wc2AiTypeNumbers.resize(slot + 1, TypeNumberUnknown);
	}
	if (Wc2AiTypeNumbers[slot] == TypeNumberUnknown) {
		Wc2AiTypeNumbers[slot] = wc2ai::TypeNumberOfIdent(wc2pud::UnitScriptNames, wc2ai::RateTypes, type.Ident);
	}
	return Wc2AiTypeNumbers[slot];
}

/// What a unit type is worth in a fight (wc2ai::StrengthWeight), from the type's own damage
/// numbers and its attack rate; 0 without the rates or for a type the original has no rate for.
static int WeightOf(const CUnitType &type)
{
	const int number = TypeNumberOf(type);
	if (number < 0) {
		return 0;
	}
	const uint8_t *rates = wc2ai::GameAttackRates();
	return wc2ai::StrengthWeight(type.DefaultStat.Variables[BASICDAMAGE_INDEX].Value,
	                             type.DefaultStat.Variables[PIERCINGDAMAGE_INDEX].Value, rates[number], rates[1]);
}

/// The player's fighters of one kind (land, sea, air) that the map did not fix: alive, on the
/// map or aboard a transport (live run 57: boarding does not lower free_land, only deaths do),
/// able to attack, not buildings and not workers. All of them are what conditions 4 / 5 / 6
/// count (wc2ai::Counted); with `pickable` only the ones on the map a wave may take
/// (wc2ai::Pickable).
static std::vector<CUnit *> Fighters(int index, EMovement kind, bool pickable)
{
	const CPlayer &player = Players[index];
	const CUnitType *peasant = UnitTypeByIdentOrNull("unit-peasant");
	const CUnitType *peon = UnitTypeByIdentOrNull("unit-peon");
	std::vector<CUnit *> free;
	for (CUnit *unit : player.GetUnits()) {
		if (!unit || !unit->Type) {
			continue;
		}
		const bool aboard = unit->Removed && unit->Container && unit->Container->Type
		                    && unit->Container->Type->CanTransport();
		if (pickable ? !unit->IsAliveOnMap() : !wc2ai::FerryCountsFighter(unit->IsAlive(), unit->Removed, aboard)) {
			continue;
		}
		const CUnitType &type = *unit->Type;
		const bool fighter = !type.Building && type.CanAttack && &type != peasant && &type != peon;
		const bool counted = wc2ai::Counted(true, fighter, type.MoveType == kind, unit->MapFixed != 0);
		if (pickable ? !wc2ai::Pickable(counted, unit->HomeRole) : !counted) {
			continue;
		}
		free.push_back(unit);
	}
	return free;
}

static int CountedFighters(int index, EMovement kind)
{
	return static_cast<int>(Fighters(index, kind, false).size());
}

static std::vector<CUnit *> PickableFighters(int index, EMovement kind)
{
	return Fighters(index, kind, true);
}

static bool IsOneOf(const CUnitType &type, std::initializer_list<const char *> idents)
{
	for (const char *ident : idents) {
		if (type.Ident == ident) {
			return true;
		}
	}
	return false;
}

/// The living units of the player a wave is sent at, in the order of its unit list.
static std::vector<wc2ai::PlaceUnit> TargetUnits(const CPlayer &target)
{
	std::vector<wc2ai::PlaceUnit> units;
	for (const CUnit *unit : target.GetUnits()) {
		if (!unit || !unit->IsAliveOnMap() || !unit->Type) {
			continue;
		}
		const CUnitType &type = *unit->Type;
		wc2ai::PlaceUnit entry;
		entry.x = unit->tilePos.x;
		entry.y = unit->tilePos.y;
		entry.w = type.TileWidth;
		entry.h = type.TileHeight;
		entry.ship = type.MoveType == EMovement::Naval;
		entry.flyer = type.MoveType == EMovement::Fly;
		entry.building = type.Building;
		entry.fighter = type.CanAttack && !type.Building;
		entry.hall = IsOneOf(type, {"unit-town-hall", "unit-keep", "unit-castle", "unit-great-hall",
		                            "unit-stronghold", "unit-fortress"});
		entry.platform = IsOneOf(type, {"unit-human-oil-platform", "unit-orc-oil-platform"});
		entry.shipyard = IsOneOf(type, {"unit-human-shipyard", "unit-orc-shipyard"});
		units.push_back(entry);
	}
	return units;
}

/// The kind of every cell of the map in use, once per map: land tiles against water tiles, a
/// coast tile in neither number.
static const std::vector<uint8_t> &CellKinds()
{
	const int cellsX = Map.Info.MapWidth / wc2ai::CellTiles;
	const int cellsY = Map.Info.MapHeight / wc2ai::CellTiles;
	if (cellsX <= 0 || cellsY <= 0 || Wc2AiCellKinds.size() == static_cast<size_t>(cellsX) * cellsY) {
		return Wc2AiCellKinds;
	}
	Wc2AiCellKinds.assign(static_cast<size_t>(cellsX) * cellsY, 2);
	for (int cellY = 0; cellY < cellsY; ++cellY) {
		for (int cellX = 0; cellX < cellsX; ++cellX) {
			int land = 0;
			int water = 0;
			for (int y = cellY * wc2ai::CellTiles; y < (cellY + 1) * wc2ai::CellTiles; ++y) {
				for (int x = cellX * wc2ai::CellTiles; x < (cellX + 1) * wc2ai::CellTiles; ++x) {
					const CMapField &field = *Map.Field(x, y);
					if (field.CoastOnMap()) {
						continue;
					}
					++(field.WaterOnMap() ? water : land);
				}
			}
			Wc2AiCellKinds[static_cast<size_t>(cellY) * cellsX + cellX] = wc2ai::CellKind(land, water);
		}
	}
	return Wc2AiCellKinds;
}

/// The strength map of the units as they stand now.
static wc2ai::CellMap BuildCells()
{
	const CUnitType *peasant = UnitTypeByIdentOrNull("unit-peasant");
	const CUnitType *peon = UnitTypeByIdentOrNull("unit-peon");
	std::vector<wc2ai::CellUnit> units;
	for (const CUnit *unit : UnitManager->GetUnits()) {
		if (!unit || !unit->IsAliveOnMap() || !unit->Type || unit->Player->Index >= wc2ai::CellSlots) {
			continue;
		}
		const CUnitType &type = *unit->Type;
		wc2ai::CellUnit entry;
		entry.slot = unit->Player->Index;
		entry.mobile = !type.Building;
		entry.worker = &type == peasant || &type == peon;
		entry.tanker = type.MoveType == EMovement::Naval && type.BoolFlag[HARVESTER_INDEX].value;
		entry.transport = type.MoveType == EMovement::Naval && type.CanTransport();
		entry.hall = IsOneOf(type, {"unit-town-hall", "unit-keep", "unit-castle", "unit-great-hall",
		                            "unit-stronghold", "unit-fortress"});
		entry.hitsLand = type.CanAttack && (type.CanTarget & ECanTargetFlag::Land) != ECanTargetFlag::NulFlag;
		entry.hitsSea = type.CanAttack && (type.CanTarget & ECanTargetFlag::Sea) != ECanTargetFlag::NulFlag;
		entry.hitPoints = unit->Variable[HP_INDEX].Value;
		entry.maxHitPoints = unit->Variable[HP_INDEX].Max;
		entry.weight = entry.mobile ? WeightOf(type) : 0;
		// A unit of several tiles is met on each of them.
		for (int dy = 0; dy < type.TileHeight; ++dy) {
			for (int dx = 0; dx < type.TileWidth; ++dx) {
				entry.x = unit->tilePos.x + dx;
				entry.y = unit->tilePos.y + dy;
				units.push_back(entry);
			}
		}
	}
	return wc2ai::BuildCellMap(Map.Info.MapWidth, Map.Info.MapHeight, CellKinds(), units);
}

const wc2ai::CellMap &Wc2AiCells()
{
	const long tick = static_cast<long>(GameCycle / HomeLeashPassCycles);
	if (tick != Wc2AiCellTick) {
		Wc2AiCellTick = tick;
		Wc2AiCellMap = BuildCells();
	}
	return Wc2AiCellMap;
}

/// The player a wave of `self` is sent at: the person-controlled enemy with the most units.
static const CPlayer *WaveTarget(const CPlayer &self)
{
	const CPlayer *target = nullptr;
	for (int i = 0; i < PlayerMax; ++i) {
		const CPlayer &other = Players[i];
		if (other.Type == PlayerTypes::PlayerPerson && self.IsEnemy(other) && other.GetUnitCount() > 0
			&& (!target || other.GetUnitCount() > target->GetUnitCount())) {
			target = &other;
		}
	}
	return target;
}

/// Where a land wave against `target` goes (wc2ai::LandPlace over a strength map).
static wc2ai::Place LandPlaceOf(const CPlayer &target, const wc2ai::CellMap &cells)
{
	return wc2ai::LandPlace(target.Index, TargetUnits(target), cells);
}

/// Tiles around the place of a sea wave in which a ship looks for water to stand on. The
/// original's search takes 0x10; that it counts tiles is not established.
constexpr int ShipPlaceSearch = 16;

/// The tile a ship of a wave is sent to: the place itself where the ship can stand, else the
/// nearest tile around it where it can, looking outwards. False when there is none.
static bool ShipTileNear(const CUnit &ship, const Vec2i &place, Vec2i &tile)
{
	for (int ring = 0; ring <= ShipPlaceSearch; ++ring) {
		for (int y = place.y - ring; y <= place.y + ring; ++y) {
			for (int x = place.x - ring; x <= place.x + ring; ++x) {
				if (HomeLeashDistance(x, y, place.x, place.y) != ring) {
					continue;
				}
				const Vec2i pos(x, y);
				if (Map.Info.IsPointOnMap(pos) && UnitCanBeAt(ship, pos)) {
					tile = pos;
					return true;
				}
			}
		}
	}
	return false;
}

/// The tile a unit of a wave or of the last stand is ordered to, for the place of the wave. A
/// flyer: the place. A ship: water it can stand on at the place, and only when a path leads there.
/// A land unit: the place, and only when a path leads there (how the original brings a unit
/// across water is not established: it waits). False = no order for this unit.
static bool WaveGoal(const CUnit &unit, const wc2ai::Place &place, Vec2i &goal)
{
	goal = Vec2i(place.x, place.y);
	if (unit.Type->MoveType == EMovement::Fly) {
		return true;
	}
	if (unit.Type->MoveType == EMovement::Naval && !ShipTileNear(unit, Vec2i(place.x, place.y), goal)) {
		return false;
	}
	const int path = PlaceReachable(unit, goal, 1, 1, 0, 1, false);
	return path != PF_UNREACHABLE && path != PF_FAILED;
}

/// The islands of the map in use, once per map: water tiles against all others (a coast tile is
/// land for a land unit).
static const std::vector<int> &Regions()
{
	const size_t size = static_cast<size_t>(Map.Info.MapWidth) * Map.Info.MapHeight;
	if (size == 0 || Wc2AiRegions.size() == size) {
		return Wc2AiRegions;
	}
	std::vector<uint8_t> water(size, 0);
	for (int y = 0; y < Map.Info.MapHeight; ++y) {
		for (int x = 0; x < Map.Info.MapWidth; ++x) {
			water[static_cast<size_t>(y) * Map.Info.MapWidth + x] = Map.Field(x, y)->WaterOnMap() ? 1 : 0;
		}
	}
	Wc2AiRegions = wc2ai::LandRegions(Map.Info.MapWidth, Map.Info.MapHeight, water);
	return Wc2AiRegions;
}

static int RegionAt(const Vec2i &pos)
{
	return wc2ai::RegionOf(Regions(), Map.Info.MapWidth, Map.Info.MapHeight, pos.x, pos.y);
}

/// A land unit of a wave that no path leads to the place: when the place lies on another island
/// the original marks the unit "waits for a transport", holds it to the place (role 2) and gives
/// it no order (role setter 0x4B6000, DECODE_B3 1 point 4). It is not picked for a later wave.
/// On the same island nothing is written (ours: no path means no order, WaveGoal).
static void WaitForTransport(int player, CUnit &unit, const wc2ai::Place &place)
{
	const Vec2i goal(place.x, place.y);
	const int region = RegionAt(goal);
	if (region == 0 || region == RegionAt(unit.tilePos)) {
		return;
	}
	unit.HomeRole = HomeRolePoint;
	unit.HomePos = goal;
	std::vector<FerryWait> &waits = Wc2AiFerry[player];
	const int number = UnitNumber(unit);
	const FerryWait entry{number, -1, region, goal.x, goal.y};
	for (FerryWait &wait : waits) {
		if (wait.unit == number) {
			wait = entry;
			return;
		}
	}
	waits.push_back(entry);
}

/// The unit of a wait entry while the entry still stands for it: alive, the player's, held to
/// the same place. A unit number is given to a new unit only 500 cycles after a death
/// (unit_manager.cpp); the entries are looked at every 50.
static CUnit *WaitingUnit(int player, const FerryWait &wait)
{
	CUnit *unit = UnitManager->FindSlotUnit(wait.unit);
	if (!unit || unit->Destroyed || !unit->IsAlive() || unit->Player != &Players[player]
		|| unit->HomeRole != HomeRolePoint || unit->HomePos != Vec2i(wait.x, wait.y)) {
		return nullptr;
	}
	return unit;
}

/// A transport of the player that may carry for it: a living ship that carries, on the map and
/// not in the last stand (there every unit is held to a place, role 2: the original's transport
/// routine is role 3 only). A map-placed transport ferries like a trained one (live run 57, 1 of 1).
static bool FreeTransport(int player, const CUnit &unit)
{
	return unit.Player == &Players[player] && unit.IsAliveOnMap() && unit.Type && unit.Type->CanTransport()
	       && unit.Type->MoveType == EMovement::Naval && unit.HomeRole < HomeRolePoint;
}

/// The nearest living enemy of the whole map a unit can attack (tile distance, the first of
/// equals in unit order); nullptr when none.
static CUnit *NearestEnemyOf(const CUnit &unit)
{
	CUnit *nearest = nullptr;
	int best = 0;
	for (CUnit *other : UnitManager->GetUnits()) {
		if (!other || !other->IsAliveOnMap() || !other->Type || !unit.IsEnemy(*other)
			|| !CanTarget(*unit.Type, *other->Type)) {
			continue;
		}
		const int distance = HomeLeashDistance(unit.tilePos.x, unit.tilePos.y, other->tilePos.x, other->tilePos.y);
		if (!nearest || distance < best) {
			nearest = other;
			best = distance;
		}
	}
	return nearest;
}

/// The nearest building of the hall class of the player to a tile (the original's 0x4C4950).
static const CUnit *NearestOf(const CPlayer &player, const Vec2i &from, std::initializer_list<const char *> idents)
{
	const CUnit *hall = nullptr;
	int best = 0;
	for (const CUnit *unit : player.GetUnits()) {
		if (!unit || !unit->IsAliveOnMap() || !unit->Type || !unit->Type->Building || !IsOneOf(*unit->Type, idents)) {
			continue;
		}
		const int distance = HomeLeashDistance(unit->tilePos.x, unit->tilePos.y, from.x, from.y);
		if (!hall || distance < best) {
			hall = unit;
			best = distance;
		}
	}
	return hall;
}

static const CUnit *NearestHall(const CPlayer &player, const Vec2i &from)
{
	return NearestOf(player, from, {"unit-town-hall", "unit-keep", "unit-castle", "unit-great-hall",
	                                "unit-stronghold", "unit-fortress"});
}

/// The original's transport routine (role 3, 0x4B5950) for each free transport of the player
/// that stands idle, calibrated to live run 57 (Human XII, 2 land waves, work/CALIB_RUN57.md):
///   1. a unit given to it stands outside: the units walk to it and board (CommandBoard); the
///      transport stays where it lies;
///   2. all given units are aboard: it sails at once to a coast tile of the place's island (with
///      parameter 0x0C a coast tile in a random direction from the place, wc2ai::LandingCoast;
///      else the engine's drop zone nearest the place) and unloads there;
///   3. empty: waiting units of one island (wc2ai::FerryClaims, up to 6) are given to the free
///      empty transport nearest them (wc2ai::FerryPickNearest), map-placed ones included;
///   4. nobody to take: it goes to the water beside the player's nearest hall.
/// A landed unit attacks the nearest enemy at once (3 of 4 landed units in run 57; the fourth
/// was first ordered to the place: the majority rule is taken), then the role-2 pass leads it.
/// `quick` (every cycle between the 50-cycle passes): only landings and departures, so that a
/// transport sails in the cycle its last unit boards, as measured.
/// Not ported: the escort (live run 57: 0 escorts in 6 chances).
static void FerryPass(int player, const wc2ai::State &state, bool quick)
{
	std::vector<FerryWait> &waits = Wc2AiFerry[player];
	if (quick && waits.empty()) {
		return;
	}
	// Forget the entries whose unit is gone, held elsewhere, or standing on its place's island; a
	// unit just put ashore there attacks the nearest enemy.
	waits.erase(std::remove_if(waits.begin(), waits.end(), [&](const FerryWait &wait) {
		CUnit *unit = WaitingUnit(player, wait);
		if (!unit) {
			return true;
		}
		if (unit->Removed || RegionAt(unit->tilePos) != wait.region) {
			return false;
		}
		if (CUnit *enemy = NearestEnemyOf(*unit)) {
			CommandAttack(*unit, enemy->tilePos, enemy->IsVisibleAsGoal(*unit->Player) ? enemy : nullptr,
			              EFlushMode::On);
		}
		return true;
	}), waits.end());

	CPlayer &owner = Players[player];
	const auto transportOf = [&](const FerryWait &wait) -> CUnit * {
		CUnit *transport = wait.transport >= 0 ? UnitManager->FindSlotUnit(wait.transport) : nullptr;
		return (transport && !transport->Destroyed && FreeTransport(player, *transport)) ? transport : nullptr;
	};
	const std::vector<CUnit *> units = owner.GetUnits();

	// Who takes waiting units: the free empty idle transport nearest the first of them.
	std::vector<wc2ai::FerryCandidate> candidates;
	for (const FerryWait &wait : waits) {
		wc2ai::FerryCandidate candidate;
		const CUnit *unit = WaitingUnit(player, wait);
		candidate.region = (unit && !unit->Removed) ? wait.region : 0;
		candidate.heldElsewhere = transportOf(wait) != nullptr;
		candidates.push_back(candidate);
	}
	const CUnit *taker = nullptr;
	if (!quick) {
		const std::vector<size_t> open = wc2ai::FerryClaims(candidates, wc2ai::FerryLoad);
		const CUnit *first = open.empty() ? nullptr : WaitingUnit(player, waits[open.front()]);
		std::vector<const CUnit *> empty;
		std::vector<int> distances;
		for (const CUnit *unit : units) {
			if (!first || !unit || !FreeTransport(player, *unit) || !unit->IsIdle() || unit->BoardCount != 0) {
				continue;
			}
			const int number = UnitNumber(*unit);
			if (std::any_of(waits.begin(), waits.end(), [&](const FerryWait &wait) { return wait.transport == number; })) {
				continue;
			}
			empty.push_back(unit);
			distances.push_back(HomeLeashDistance(first->tilePos.x, first->tilePos.y, unit->tilePos.x, unit->tilePos.y));
		}
		const int pick = wc2ai::FerryPickNearest(distances);
		taker = pick >= 0 ? empty[static_cast<size_t>(pick)] : nullptr;
	}

	for (CUnit *transportPtr : units) {
		if (!transportPtr || !FreeTransport(player, *transportPtr) || !transportPtr->IsIdle()) {
			continue;
		}
		CUnit &transport = *transportPtr;
		const int number = UnitNumber(transport);

		// The units given to this transport that stand outside.
		std::vector<CUnit *> outside;
		bool given = false;
		for (const FerryWait &wait : waits) {
			if (wait.transport != number) {
				continue;
			}
			given = true;
			CUnit *unit = WaitingUnit(player, wait);
			if (unit && !unit->Removed) {
				outside.push_back(unit);
			}
		}
		if (quick && !given) {
			continue;
		}
		// The last of its load that is held to a place.
		const CUnit *last = nullptr;
		for (const CUnit *unit : units) {
			if (unit && unit->Container == &transport && unit->HomeRole == HomeRolePoint && unit->HomePos.x >= 0) {
				last = unit;
			}
		}
		const wc2ai::FerryStep step = wc2ai::FerryNext(!outside.empty(), last != nullptr, &transport == taker);
		if (quick && step != wc2ai::FerryStep::Sail) {
			continue;
		}
		switch (step) {
			case wc2ai::FerryStep::CallAboard:
				// The units walk to it; it stays where it lies.
				for (CUnit *unit : outside) {
					const bool boarding = unit->CurrentAction() == UnitAction::Board
					                      && unit->CurrentOrder()->GetGoal() == &transport;
					if (!boarding) {
						CommandBoard(*unit, transport, EFlushMode::On);
					}
				}
				break;
			case wc2ai::FerryStep::Sail: {
				Vec2i landing = last->HomePos;
				if (state.params[0x0C] != 0) {
					std::vector<uint8_t> coast(static_cast<size_t>(Map.Info.MapWidth) * Map.Info.MapHeight, 0);
					for (int y = 0; y < Map.Info.MapHeight; ++y) {
						for (int x = 0; x < Map.Info.MapWidth; ++x) {
							coast[static_cast<size_t>(y) * Map.Info.MapWidth + x] = Map.Field(x, y)->CoastOnMap() ? 1 : 0;
						}
					}
					const wc2ai::Place shore = wc2ai::LandingCoast(Map.Info.MapWidth, Map.Info.MapHeight, Regions(), coast,
					                                               landing.x, landing.y, SyncRand(8));
					if (shore.found) {
						landing = Vec2i(shore.x, shore.y);
					}
				}
				CommandUnload(transport, landing, nullptr, EFlushMode::On);
				// The load is let go: a unit put ashore on its place's island waits no more (the
				// next pass forgets it), one put ashore anywhere else is free to be taken again.
				for (FerryWait &wait : waits) {
					if (wait.transport == number) {
						wait.transport = -1;
					}
				}
				break;
			}
			case wc2ai::FerryStep::Take: {
				const int load = std::min(wc2ai::FerryLoad, transport.Type->MaxOnBoard - transport.BoardCount);
				for (size_t index : wc2ai::FerryClaims(candidates, load)) {
					FerryWait &wait = waits[index];
					wait.transport = number;
					if (CUnit *unit = WaitingUnit(player, wait)) {
						CommandBoard(*unit, transport, EFlushMode::On);
					}
				}
				break;
			}
			case wc2ai::FerryStep::Home: {
				Vec2i tile;
				// Beside its own Shipyard (where it was trained): it stays (live run 64, Human XIII:
				// 5 new Transports of 2 computers lay 1-3 tiles from their yards, the halls 19-23 tiles
				// away inland; 9 of 9 with run 62). After a wave it goes to the hall (run 57; run 64 1).
				const CUnit *yard = NearestOf(owner, transport.tilePos, {"unit-human-shipyard", "unit-orc-shipyard"});
				if (yard && ShipTileNear(transport, yard->tilePos, tile)
				    && !HomeLeashSendsHome(transport.tilePos.x, transport.tilePos.y, tile.x, tile.y,
				                           transport.Type->ReactRangeComputer)) {
					break;
				}
				const CUnit *hall = NearestHall(owner, transport.tilePos);
				// Beside the hall already (the leash's own limit): it stays.
				if (!hall || !ShipTileNear(transport, hall->tilePos, tile)
					|| !HomeLeashSendsHome(transport.tilePos.x, transport.tilePos.y, tile.x, tile.y,
					                       transport.Type->ReactRangeComputer)) {
					break;
				}
				const int path = PlaceReachable(transport, tile, 1, 1, 0, 1, false);
				if (path != PF_UNREACHABLE && path != PF_FAILED) {
					CommandMove(transport, tile, EFlushMode::On);
				}
				break;
			}
		}
	}
}

/// The idle ships and flyers of a scripted computer, every 50 cycles (live run 57, Human XII):
/// a trained warship that no wave holds patrols between where it stands and the water beside
/// the player's nearest hall (10 of 10 patrolled; the original's patrol points - 93,46 / 87,59 /
/// 121,27 and others - are not derived: UNKNOWN rule, this pair is ours); a flyer that cannot
/// attack (Zeppelin, balloon) wanders to a point up to a quarter of the map away (5 of 5
/// map-placed Zeppelins; trained ones not seen). A map-fixed warship has role 1 and stands.
static void IdleShipsAndFlyers(int player)
{
	CPlayer &owner = Players[player];
	for (CUnit *unit : owner.GetUnits()) {
		if (!unit || !unit->IsAliveOnMap() || !unit->Type || unit->Type->Building || !unit->CanMove()) {
			continue;
		}
		const CUnitType &type = *unit->Type;
		if (type.MoveType == EMovement::Fly && !type.CanAttack && !type.CanTransport()) {
			if (unit->IsIdle()) {
				const int x = WanderAxis(unit->tilePos.x, Map.Info.MapWidth, static_cast<unsigned>(SyncRand()));
				const int y = WanderAxis(unit->tilePos.y, Map.Info.MapHeight, static_cast<unsigned>(SyncRand()));
				CommandMove(*unit, Vec2i(x, y), EFlushMode::On);
			}
			continue;
		}
		const bool warship = type.MoveType == EMovement::Naval && type.CanAttack && !type.CanTransport()
		                     && !type.BoolFlag[HARVESTER_INDEX].value;
		if (!wc2ai::WarshipPatrols(warship, unit->MapFixed != 0, unit->HomeRole, unit->IsIdle())) {
			continue;
		}
		const CUnit *hall = NearestHall(owner, unit->tilePos);
		Vec2i tile;
		if (!hall || !ShipTileNear(*unit, hall->tilePos, tile) || tile == unit->tilePos) {
			continue;
		}
		const int path = PlaceReachable(*unit, tile, 1, 1, 0, 1, false);
		if (path != PF_UNREACHABLE && path != PF_FAILED) {
			CommandPatrolUnit(*unit, tile, EFlushMode::On);
		}
	}
}

/// How many units of the named types a player has.
static int CountOf(const CPlayer &player, std::initializer_list<const char *> idents)
{
	int count = 0;
	for (const char *ident : idents) {
		if (const CUnitType *type = UnitTypeByIdentOrNull(ident)) {
			count += player.UnitTypesCount[type->Slot];
		}
	}
	return count;
}

/// The conditions a script can wait for. 0..3 are answered from the player's own counts; 4..6
/// (enough fighters, ships, flyers for the next wave: wc2ai::Counted) and 7 are not answered yet: a script
/// stops at them, which is where the original's wave launcher would take over.
static bool ConditionHolds(int condition, void *context)
{
	const int index = *static_cast<int *>(context);
	const CPlayer &player = Players[index];
	switch (condition) {
		case 0: return CountOf(player, {"unit-human-shipyard", "unit-orc-shipyard"}) > 0;
		case 1: return CountOf(player, {"unit-keep", "unit-stronghold"}) > 0;
		case 2: return CountOf(player, {"unit-castle", "unit-fortress"}) > 0;
		case 3: return CountOf(player, {"unit-peasant", "unit-peon"}) >= Wc2AiStates[index].params[0x13];
		case 4: {
			const wc2ai::State &state = Wc2AiStates[index];
			return CountedFighters(index, EMovement::Land) >= state.params[0x0D] * state.params[0x0E];
		}
		case 5: {
			const wc2ai::State &state = Wc2AiStates[index];
			return CountedFighters(index, EMovement::Naval) >= state.params[0x0F] * state.params[0x10];
		}
		case 6: {
			const wc2ai::State &state = Wc2AiStates[index];
			return CountedFighters(index, EMovement::Fly) >= state.params[0x11] * state.params[0x12];
		}
		default: return false;
	}
}

/// The reserved tiles, sized to the map (a map of another size starts with none).
static std::vector<uint8_t> &ReservedTiles()
{
	const size_t size = static_cast<size_t>(Map.Info.MapWidth) * Map.Info.MapHeight;
	if (Wc2AiReserved.size() != size) {
		Wc2AiReserved.assign(size, 0);
	}
	return Wc2AiReserved;
}

void Wc2AiReservePlanned(const CUnitType &type, int x, int y)
{
	const int number = wc2ai::TypeNumberOfIdent(wc2pud::UnitScriptNames, 0x69, type.Ident);
	if (number == 0x3A || number == 0x3B) {
		return; // a farm: the bare site only, a planned site while the order lives
	}
	wc2ai::ReserveSquare(ReservedTiles(), Map.Info.MapWidth, Map.Info.MapHeight, x, y, type.TileWidth, 1, false);
}

void Wc2AiReserveRoad(int depotX, int depotY, int mineX, int mineY)
{
	std::vector<uint8_t> &tiles = ReservedTiles();
	for (const wc2ai::Place &tile : wc2ai::RoadTiles(depotX, depotY, mineX, mineY)) {
		if (Map.Info.IsPointOnMap(tile.x, tile.y)) {
			tiles[static_cast<size_t>(tile.y) * Map.Info.MapWidth + tile.x] = 1;
		}
	}
}

void Wc2AiReserveBuilding(const CUnit &building, bool mapPlaced)
{
	const CUnitType &type = *building.Type;
	const int number = wc2ai::TypeNumberOfIdent(wc2pud::UnitScriptNames, 0x69, type.Ident);
	const int x = building.tilePos.x;
	const int y = building.tilePos.y;
	const int size = type.TileWidth;
	std::vector<uint8_t> &tiles = ReservedTiles();
	switch (wc2ai::ReserveKindOf(number)) {
		case wc2ai::ReserveKind::Border:
			wc2ai::ReserveSquare(tiles, Map.Info.MapWidth, Map.Info.MapHeight, x, y, size, 1, mapPlaced);
			break;
		case wc2ai::ReserveKind::Coast:
			for (int ty = y - 6; ty <= y + 6; ++ty) {
				for (int tx = x - 6; tx <= x + 6; ++tx) {
					const bool inner = tx >= x && tx < x + size && ty >= y && ty < y + size;
					if (Map.Info.IsPointOnMap(tx, ty) && (mapPlaced || !inner) && Map.Field(tx, ty)->CoastOnMap()) {
						tiles[static_cast<size_t>(ty) * Map.Info.MapWidth + tx] = 1;
					}
				}
			}
			break;
		case wc2ai::ReserveKind::None:
			break;
	}
}

bool Wc2AiReservedAt(int x, int y, int w, int h)
{
	return wc2ai::ReservedAny(Wc2AiReserved, Map.Info.MapWidth, Map.Info.MapHeight, x, y, w, h);
}

/// At the first cycle of a new game: the buildings the map placed for the scripted slots.
static void ReserveMapBuildings()
{
	if (Wc2AiReservedReady) {
		return;
	}
	Wc2AiReservedReady = true;
	ReservedTiles();
	for (int p = 0; p < PlayerMax; ++p) {
		if (!Wc2AiRuns(p)) {
			continue;
		}
		for (const CUnit *unit : Players[p].GetUnits()) {
			if (unit && unit->IsAliveOnMap() && unit->Type->Building) {
				Wc2AiReserveBuilding(*unit, true);
			}
		}
	}
}

void Wc2AiCycle(int player)
{
	if (player < 0 || player >= PlayerMax || Wc2AiIds[player] < 0) {
		return;
	}
	const wc2ai::Program &program = wc2ai::GameProgram();
	if (!program.Loaded()) {
		return;
	}
	ReserveMapBuildings();
	wc2ai::State &state = Wc2AiStates[player];
	if (state.id != Wc2AiIds[player]) {
		if (state.id == -1 && state.executed != 0) {
			return; // the script stopped on a damaged state: stay stopped
		}
		if (!wc2ai::Start(program, Wc2AiIds[player], state)) {
			// A map may carry an id the definition file has no script for: reported, and the
			// player keeps the engine's own script.
			LogPrint("wc2ai: ERROR: player %d: no script with id %d in the definition file\n", player,
			         Wc2AiIds[player]);
			Wc2AiIds[player] = -1;
			return;
		}
	}
	int index = player;
	wc2ai::Step(program, state, ConditionHolds, &index);

	// The wave launcher, every 50 cycles: a set "attack by land / sea / air now" sends `count`
	// groups of up to `size` fighters of that kind a wave may take at a player that is not a computer, then the
	// trigger is cleared. A wave goes to the place the original picks for its kind
	// (wc2ai::LandPlace, wc2ai::SeaPlace, wc2ai::AirPlace). Every unit sent is held to the place
	// ordered (role 2 of the home leash). How a group travels is the engine's own; a land unit
	// whose place is on another island is carried by the player's transports (FerryPass).
	if (GameCycle % 50 == 0 && (state.params[0x09] | state.params[0x0A] | state.params[0x0B]) != 0) {
		const CPlayer *target = WaveTarget(Players[player]);
		static const struct { int trigger, size, count; EMovement kind; } waves[] = {
			{0x09, 0x0D, 0x0E, EMovement::Land},
			{0x0A, 0x0F, 0x10, EMovement::Naval},
			{0x0B, 0x11, 0x12, EMovement::Fly},
		};
		for (const auto &wave : waves) {
			if (state.params[wave.trigger] == 0) {
				continue;
			}
			wc2ai::Place place;
			if (target) {
				if (wave.kind == EMovement::Naval) {
					place = wc2ai::SeaPlace(TargetUnits(*target));
				} else if (wave.kind == EMovement::Fly) {
					place = wc2ai::AirPlace(TargetUnits(*target));
				} else {
					place = LandPlaceOf(*target, Wc2AiCells());
				}
			}
			// Without a place nobody is sent and nobody is marked; the trigger is cleared all the same.
			if (place.found) {
				std::vector<CUnit *> free = PickableFighters(player, wave.kind);
				size_t next = 0;
				for (int group = 0; group < state.params[wave.count]; ++group) {
					for (int n = 0; n < state.params[wave.size] && next < free.size(); ++n, ++next) {
						CUnit &unit = *free[next];
						Vec2i goal;
						// A ship or a land unit that no path leads there stays, and its place in the
						// group is used up; a land unit whose place is on another island waits
						// there for a transport (FerryPass).
						if (!WaveGoal(unit, place, goal)) {
							if (wave.kind == EMovement::Land) {
								WaitForTransport(player, unit, place);
							}
							continue;
						}
						unit.HomeRole = HomeRolePoint;
						unit.HomePos = goal;
						CommandAttack(unit, goal, nullptr, EFlushMode::On);
					}
				}
			}
			state.params[wave.trigger] = 0;
		}
	}
	// The transports: in full every 50 cycles, landings and departures every cycle; the idle
	// ships and flyers every 50 cycles.
	FerryPass(player, state, GameCycle % 50 != 0);
	if (GameCycle % 50 == 0) {
		IdleShipsAndFlyers(player);
	}
}

void Wc2AiBuildingLost(const CUnit &building)
{
	const int index = building.Player->Index;
	if (Wc2AiIdOf(index) < 0) {
		return;
	}
	const CPlayer &self = Players[index];
	int buildings = 0;
	int platforms = 0;
	for (const CUnit *unit : self.GetUnits()) {
		if (!unit || unit == &building || !unit->IsAlive() || !unit->Type || !unit->Type->Building
			|| unit->Type->BoolFlag[WALL_INDEX].value) {
			continue;
		}
		++buildings;
		platforms += IsOneOf(*unit->Type, {"unit-human-oil-platform", "unit-orc-oil-platform"}) ? 1 : 0;
	}
	if (!wc2ai::AllOutNow(buildings, platforms)) {
		return;
	}
	const CPlayer *target = WaveTarget(self);
	if (!target) {
		return;
	}
	const wc2ai::Place place = LandPlaceOf(*target, Wc2AiCells());
	if (!place.found) {
		return;
	}
	// Every unit that can move is held to the place (the 50-cycle pass leads it there); the
	// workers and the tankers are sent there at once besides, by a plain move.
	for (CUnit *unit : self.GetUnits()) {
		if (!unit || !unit->IsAliveOnMap() || !unit->Type || unit->Type->Building || !unit->CanMove()) {
			continue;
		}
		Vec2i goal;
		if (!WaveGoal(*unit, place, goal)) {
			continue;
		}
		unit->HomeRole = HomeRolePoint;
		unit->HomePos = goal;
		if (unit->Type->BoolFlag[HARVESTER_INDEX].value) {
			CommandMove(*unit, goal, EFlushMode::On);
		}
	}
}

/// The building types of a build list, as the file numbers them: 0x3A + 2 * index, the human type
/// even and the orc type odd.
static const char *const BuildingIdents[][2] = {
	{"unit-farm", "unit-pig-farm"},                        // 0x3A
	{"unit-human-barracks", "unit-orc-barracks"},          // 0x3C
	{"unit-church", "unit-altar-of-storms"},               // 0x3E
	{"unit-human-watch-tower", "unit-orc-watch-tower"},    // 0x40
	{"unit-stables", "unit-ogre-mound"},                   // 0x42
	{"unit-inventor", "unit-alchemist"},                   // 0x44
	{"unit-gryphon-aviary", "unit-dragon-roost"},          // 0x46
	{"unit-human-shipyard", "unit-orc-shipyard"},          // 0x48
	{"unit-town-hall", "unit-great-hall"},                 // 0x4A
	{"unit-elven-lumber-mill", "unit-troll-lumber-mill"},  // 0x4C
	{"unit-human-foundry", "unit-orc-foundry"},            // 0x4E
	{"unit-mage-tower", "unit-temple-of-the-damned"},      // 0x50
	{"unit-human-blacksmith", "unit-orc-blacksmith"},      // 0x52
	{"unit-human-refinery", "unit-orc-refinery"},          // 0x54
	{"unit-human-oil-platform", "unit-orc-oil-platform"},  // 0x56
};

/// The research entries of a build list, 0x80 + index, human / orc; 0x98 and 0x99 are the hall's
/// two upgrades and are handled as buildings to upgrade to.
static const char *const ResearchIdents[][2] = {
	{"upgrade-arrow1", "upgrade-throwing-axe1"},                         // 0x80
	{"upgrade-arrow2", "upgrade-throwing-axe2"},                         // 0x81
	{"upgrade-ranger", "upgrade-berserker"},                             // 0x82
	{"upgrade-ranger-marksmanship", "upgrade-berserker-regeneration"},   // 0x83
	{"upgrade-longbow", "upgrade-light-axes"},                           // 0x84
	{"upgrade-ranger-scouting", "upgrade-berserker-scouting"},           // 0x85
	{"upgrade-sword1", "upgrade-battle-axe1"},                           // 0x86
	{"upgrade-sword2", "upgrade-battle-axe2"},                           // 0x87
	{"upgrade-human-shield1", "upgrade-orc-shield1"},                    // 0x88
	{"upgrade-human-shield2", "upgrade-orc-shield2"},                    // 0x89
	{"upgrade-ballista1", "upgrade-catapult1"},                          // 0x8A
	{"upgrade-ballista2", "upgrade-catapult2"},                          // 0x8B
	{"upgrade-human-ship-cannon1", "upgrade-orc-ship-cannon1"},          // 0x8C
	{"upgrade-human-ship-cannon2", "upgrade-orc-ship-cannon2"},          // 0x8D
	{"upgrade-human-ship-armor1", "upgrade-orc-ship-armor1"},            // 0x8E
	{"upgrade-human-ship-armor2", "upgrade-orc-ship-armor2"},            // 0x8F
	{"upgrade-paladin", "upgrade-ogre-mage"},                            // 0x90
	{"upgrade-healing", "upgrade-bloodlust"},                            // 0x91
	{"upgrade-exorcism", "upgrade-runes"},                               // 0x92
	{"upgrade-flame-shield", "upgrade-raise-dead"},                      // 0x93
	{"upgrade-slow", "upgrade-haste"},                                   // 0x94
	{"upgrade-invisibility", "upgrade-whirlwind"},                       // 0x95
	{"upgrade-polymorph", "upgrade-unholy-armor"},                       // 0x96
	{"upgrade-blizzard", "upgrade-death-and-decay"},                     // 0x97
};

/// Queue a research, or take the request back while it may not start: the engine's research path
/// of a computer asks neither the upgrade's dependency rules nor a person's button, so they are
/// asked here (the engine drops a done research from its checks by itself).
static void WantResearch(PlayerAi &ai, const CPlayer &player, const char *ident)
{
	CUpgrade *upgrade = nullptr;
	for (const auto &known : AllUpgrades) {
		if (known && known->Ident == ident) {
			upgrade = known.get();
			break;
		}
	}
	if (!upgrade) {
		return;
	}
	const auto found = std::find(ai.ResearchRequests.begin(), ai.ResearchRequests.end(), upgrade);
	if (CheckDependByIdent(player, upgrade->Ident)) {
		if (found == ai.ResearchRequests.end()) {
			ai.ResearchRequests.push_back(upgrade);
		}
	} else if (found != ai.ResearchRequests.end()) {
		ai.ResearchRequests.erase(found);
	}
}

/// Ask for the upgrade of the hall to another type, or take the request back while it may not
/// start. The original's hall trains workers while fewer than wanted, and otherwise upgrades: to
/// the keep / stronghold with a barracks, to the castle / fortress with stables / ogre mound,
/// blacksmith and lumber mill (local evidence record AI-HALL-001). Those buildings are the
/// types' own dependency rules here, so the rules are asked. The engine's upgrade path of a
/// computer does not ask them (its setting for that is off by default), hence the check here.
static void WantUpgradeTo(PlayerAi &ai, const CPlayer &player, const char *ident, bool workersThere)
{
	CUnitType *type = UnitTypeByIdentOrNull(ident);
	if (!type) {
		return;
	}
	const auto found = std::find(ai.UpgradeToRequests.begin(), ai.UpgradeToRequests.end(), type);
	if (workersThere && CheckDependByType(player, *type)) {
		if (found == ai.UpgradeToRequests.end()) {
			ai.UpgradeToRequests.push_back(type);
		}
	} else if (found != ai.UpgradeToRequests.end()) {
		ai.UpgradeToRequests.erase(found);
	}
}

/// Ask the engine's production for `count` units of a type (the request table is a list of
/// "have this many"; the resource manager trains and builds towards it).
static void Want(PlayerAi &ai, const char *ident, unsigned count)
{
	CUnitType *type = UnitTypeByIdentOrNull(ident);
	if (!type) {
		return;
	}
	for (AiRequestType &request : ai.UnitTypeRequests) {
		if (request.Type == type) {
			request.Count = count;
			return;
		}
	}
	if (count == 0) {
		return;
	}
	AiRequestType request;
	request.Type = type;
	request.Count = count;
	ai.UnitTypeRequests.push_back(request);
}

/// What the engine's request table already stands for of a type: the units the player has, with
/// the equivalent types the engine counts in (rangers with archers, paladins with knights), and
/// the ones in its production queue.
static int Standing(const CPlayer &player, const std::array<int, UnitTypeMax> &queued, const char *ident)
{
	const CUnitType *type = UnitTypeByIdentOrNull(ident);
	if (!type) {
		return 0;
	}
	int count = player.UnitTypesAiActiveCount[type->Slot] + queued[type->Slot];
	if (static_cast<size_t>(type->Slot) < AiHelpers.Equiv().size()) {
		for (const CUnitType *equiv : AiHelpers.Equiv()[type->Slot]) {
			count += player.UnitTypesAiActiveCount[equiv->Slot];
		}
	}
	return count;
}

/// How many decisions the player's buildings of one kind take now: one per building that does
/// nothing, less the units of `classes` that are queued and not handed to a building yet. The
/// queued units of a class that cannot be trained any more (`can` false) are taken back first:
/// they would hold every later decision.
static int OpenDecisions(PlayerAi &ai, const CPlayer &player, const char *building,
                         std::initializer_list<std::pair<const char *, bool>> classes)
{
	int open = 0;
	if (const CUnitType *type = UnitTypeByIdentOrNull(building)) {
		for (const CUnit *unit : FindPlayerUnitsByType(player, *type, true)) {
			open += unit->IsIdle() ? 1 : 0;
		}
	}
	for (const auto &[ident, can] : classes) {
		const CUnitType *type = UnitTypeByIdentOrNull(ident);
		for (size_t i = 0; type && i < ai.UnitTypeBuilt.size();) {
			AiBuildQueue &queue = ai.UnitTypeBuilt[i];
			if (queue.Type == type && queue.Want > queue.Made) {
				if (can) {
					open -= static_cast<int>(queue.Want - queue.Made);
				} else if ((queue.Want = queue.Made) == 0) {
					ai.UnitTypeBuilt.erase(ai.UnitTypeBuilt.begin() + i);
					continue;
				}
			}
			++i;
		}
	}
	return open;
}

/// The dice of a decision: the game's own synchronised random numbers.
static bool SyncDice(int sides, void *)
{
	return SyncRand(sides) == 0;
}

bool Wc2AiRuns(int player)
{
	if (player < 0 || player >= PlayerMax || Wc2AiIds[player] < 0 || !Players[player].Ai) {
		return false;
	}
	const wc2ai::Program &program = wc2ai::GameProgram();
	return program.Loaded() && program.Get(Wc2AiStates[player].id) != nullptr;
}

/// Town hall, keep, castle and their orc forms.
static bool IsHallClass(const CUnitType &type)
{
	static const char *const idents[] = {"unit-town-hall", "unit-keep", "unit-castle",
	                                     "unit-great-hall", "unit-stronghold", "unit-fortress"};
	for (const char *ident : idents) {
		if (type.Ident == ident) {
			return true;
		}
	}
	return false;
}

int Wc2AiPlaceStepOf(const CUnitType &type)
{
	// the map converter's table holds every type the original numbers, the buildings from 0x3A on
	constexpr int types = 0x69;
	const int number = wc2ai::TypeNumberOfIdent(wc2pud::UnitScriptNames, types, type.Ident);
	return number < 0 ? 0 : wc2ai::PlaceStep(number);
}

void Wc2AiPlaceReference(const CUnit &worker, int &x, int &y)
{
	x = worker.tilePos.x;
	y = worker.tilePos.y;
	int best = -1;
	for (const CUnit *unit : worker.Player->GetUnits()) {
		if (!unit || !unit->IsAliveOnMap() || !unit->Type || !unit->Type->Building || !IsHallClass(*unit->Type)) {
			continue;
		}
		const int distance = worker.MapDistanceTo(*unit);
		if (best < 0 || distance < best) {
			best = distance;
			x = unit->tilePos.x;
			y = unit->tilePos.y;
		}
	}
}

int Wc2AiWorkerCount(const CPlayer &player)
{
	int workers = 0;
	for (const CUnit *unit : player.GetUnits()) {
		if (unit && unit->IsAlive() && unit->Type && unit->Type->BoolFlag[HARVESTER_INDEX].value
			&& unit->Type->ResInfo[GoldCost]) {
			++workers;
		}
	}
	return workers;
}

int Wc2AiLumberWanted(const CPlayer &player)
{
	// the stock at hand only: the original has one stock per resource
	return wc2ai::LumberWorkers(player.Resources[GoldCost], player.Resources[WoodCost], Wc2AiWorkerCount(player));
}

int Wc2AiOnLumber(const CPlayer &player, const CUnit *except)
{
	int onLumber = 0;
	for (const CUnit *unit : player.GetUnits()) {
		if (!unit || unit == except || !unit->IsAlive() || !unit->Type
			|| !unit->Type->BoolFlag[HARVESTER_INDEX].value) {
			continue;
		}
		if (unit->Orders.size() == 1 && unit->CurrentAction() == UnitAction::Resource) {
			const auto &order = *static_cast<const COrder_Resource *>(unit->Orders[0].get());
			onLumber += order.GetCurrentResource() == WoodCost;
		} else if (unit->IsIdle() && unit->ResourcesHeld && unit->CurrentResource == WoodCost) {
			++onLumber;
		}
	}
	return onLumber;
}

CUnit *Wc2AiRepairTarget(const CPlayer &player, const std::vector<const CUnit *> &unreachable)
{
	std::vector<wc2ai::RepairView> views;
	std::vector<CUnit *> buildings;
	int workers = 0;
	int repairers = 0;
	for (CUnit *unit : player.GetUnits()) {
		if (!unit || !unit->IsAlive() || !unit->Type) {
			continue;
		}
		const CUnitType &type = *unit->Type;
		if (type.BoolFlag[HARVESTER_INDEX].value && type.ResInfo[GoldCost]) {
			++workers;
			if (unit->CurrentAction() == UnitAction::Repair) {
				++repairers;
			}
			continue;
		}
		const int hp = unit->Variable[HP_INDEX].Value;
		const int maxHp = unit->Variable[HP_INDEX].Max;
		if (!type.Building || !type.RepairHP || !unit->IsAliveOnMap() || unit->CurrentAction() == UnitAction::Built
			|| hp >= (3 * maxHp) >> 2) {
			continue; // whole enough or not a ready building: the rule passes it over, nothing more is asked
		}
		wc2ai::RepairView view;
		view.finishedBuilding = true;
		view.oilPlatform = type.Ident == "unit-human-oil-platform" || type.Ident == "unit-orc-oil-platform";
		view.hallClass = IsHallClass(type);
		view.sameGround = ranges::find(unreachable, unit) == unreachable.end();
		view.hp = hp;
		view.maxHp = maxHp;
		const Vec2i corner(unit->tilePos.x - 3, unit->tilePos.y - 3);
		const std::vector<CUnit *> near = Select(corner, Vec2i(corner.x + 8, corner.y + 8), [&](const CUnit *other) {
			return other->IsAliveOnMap() && other->Type->CanAttack && player.IsEnemy(*other);
		});
		view.enemyFighterNear = !near.empty();
		view.priority = type.AnnoyComputerFactor; // the unit data's "annoy computer factor" (DECODE_B7 1), not Priority
		views.push_back(view);
		buildings.push_back(unit);
	}
	const int pick = wc2ai::RepairPick(player.Resources[GoldCost], player.Resources[WoodCost], workers, repairers, views);
	return pick < 0 ? nullptr : buildings[pick];
}

bool Wc2AiSecond(int player)
{
	if (player < 0 || player >= PlayerMax || Wc2AiIds[player] < 0 || !Players[player].Ai) {
		return false;
	}
	const wc2ai::Program &program = wc2ai::GameProgram();
	const wc2ai::State &state = Wc2AiStates[player];
	const wc2ai::Script *script = program.Get(state.id);
	if (!program.Loaded() || !script) {
		return false;
	}
	PlayerAi &ai = *Players[player].Ai;
	const int orc = (PlayerRaces.Name[Players[player].Race] == "orc") ? 1 : 0;

	// The active part of the build list: entries below 0x80 are buildings, 0x80..0x97 research,
	// 0x98 / 0x99 the hall's upgrades. The engine's own builder finds the worker and the
	// place; where the original places a building is not established.
	const size_t limit = std::min<size_t>(state.params[wc2ai::ParamLast], script->buildList.size());
	// The requests are served in the order they are made, so they are made in the list's order:
	// the first building (the hall), then the workers, then the rest, then the fighters.
	const char *workers = orc ? "unit-peon" : "unit-peasant";
	const CPlayer &owner = Players[player];
	const bool workersThere = CountOf(owner, {workers}) >= state.params[0x13];

	// Farms. No build list of the definition file holds a farm: the original's worker, once its
	// player has a hall, builds one by itself, before it looks at the build list, when
	//     supply + 4 x (farms being built) <= demand + 3   and   supply < 200
	// (local evidence record AI-FARM-001; 4 is what a farm gives there). Asked for here in the
	// engine's terms, first in the request table: as many farms as leave more than 3 food free,
	// the request table itself counting the ones that are being built.
	const char *farmIdent = orc ? "unit-pig-farm" : "unit-farm";
	if (const CUnitType *farm = UnitTypeByIdentOrNull(farmIdent)) {
		const int gives = farm->Stats[player].Variables[SUPPLY_INDEX].Value;
		const bool hasHall = orc ? CountOf(owner, {"unit-great-hall", "unit-stronghold", "unit-fortress"}) > 0
		                         : CountOf(owner, {"unit-town-hall", "unit-keep", "unit-castle"}) > 0;
		int more = 0;
		if (hasHall && gives > 0 && owner.Supply < 200 && owner.Supply <= owner.Demand + 3) {
			more = (owner.Demand + 3 - owner.Supply) / gives + 1;
		}
		Want(ai, farmIdent, static_cast<unsigned>(owner.UnitTypesAiActiveCount[farm->Slot] + more));
	}

	unsigned wanted[std::size(BuildingIdents)] = {};
	bool workersAsked = false;
	for (size_t i = 0; i < limit; ++i) {
		const unsigned entry = script->buildList[i];
		if (entry >= 0x3A && entry < 0x3A + 2 * std::size(BuildingIdents)) {
			const size_t index = (entry - 0x3A) / 2;
			Want(ai, BuildingIdents[index][orc], ++wanted[index]);
			if (!workersAsked) {
				Want(ai, workers, state.params[0x13]);
				workersAsked = true;
			}
		} else if (entry >= 0x80 && entry < 0x80 + std::size(ResearchIdents)) {
			WantResearch(ai, owner, ResearchIdents[entry - 0x80][orc]);
		} else if (entry == 0x98) {
			WantUpgradeTo(ai, owner, orc ? "unit-stronghold" : "unit-keep", workersThere);
		} else if (entry == 0x99) {
			WantUpgradeTo(ai, owner, orc ? "unit-fortress" : "unit-castle", workersThere);
		}
	}
	// Workers, by the script's wanted number (the hall trains without dice).
	Want(ai, workers, state.params[0x13]);
	// The land classes, by the decisions of the barracks: a barracks that does nothing decides for
	// one unit by the original's rules and dice (wc2ai::BarracksDecision), and the request table
	// is raised by that unit only. A class counts its upgraded form too (rangers with archers,
	// paladins with knights), as the original's class counters do. How often the original's
	// building decides is not established (the script's cadence table is not applied here): ours
	// decides once a second while it does nothing. A class the map forbids is not wanted.
	const std::array<int, UnitTypeMax> queued = AiGetBuildRequestsCount(ai);
	const auto there = [&](const char *human, const char *orcish) { return CountOf(owner, {orc ? orcish : human}) > 0; };
	const auto may = [&](const char *ident) {
		const CUnitType *type = UnitTypeByIdentOrNull(ident);
		return type && CheckDependByType(owner, *type);
	};
	const char *footman = orc ? "unit-grunt" : "unit-footman";
	const char *archer = orc ? "unit-axethrower" : "unit-archer";
	const char *ballista = orc ? "unit-catapult" : "unit-ballista";
	const char *knight = orc ? "unit-ogre" : "unit-knight";
	const bool mill = there("unit-elven-lumber-mill", "unit-troll-lumber-mill");
	const bool smith = there("unit-human-blacksmith", "unit-orc-blacksmith");
	const bool stables = there("unit-stables", "unit-ogre-mound");
	wc2ai::BarracksView land;
	land.haveFootmen = Standing(owner, queued, footman);
	land.haveArchers = Standing(owner, queued, archer);
	land.haveBallistas = Standing(owner, queued, ballista);
	land.haveKnights = Standing(owner, queued, knight);
	land.wantFootmen = may(footman) ? state.params[0x14] : 0;
	land.wantArchers = state.params[0x15];
	land.wantBallistas = state.params[0x16];
	land.wantKnights = state.params[0x17];
	land.archersCan = mill && (may(archer) || may(orc ? "unit-berserker" : "unit-ranger"));
	land.ballistasCan = smith && mill && may(ballista);
	land.knightsCan = smith && stables && (may(knight) || may(orc ? "unit-ogre-mage" : "unit-paladin"));
	const int barracksFree = OpenDecisions(ai, owner, orc ? "unit-orc-barracks" : "unit-human-barracks",
	                                       {{footman, true}, {archer, land.archersCan},
	                                        {ballista, land.ballistasCan}, {knight, land.knightsCan}});
	for (int n = 0; n < barracksFree; ++n) {
		switch (wc2ai::BarracksDecision(land, SyncDice, nullptr)) {
			case wc2ai::Train::Footman: ++land.haveFootmen; break;
			case wc2ai::Train::Archer: ++land.haveArchers; break;
			case wc2ai::Train::Ballista: ++land.haveBallistas; break;
			case wc2ai::Train::Knight: ++land.haveKnights; break;
			default: break;
		}
	}
	Want(ai, footman, static_cast<unsigned>(land.haveFootmen));
	Want(ai, knight, static_cast<unsigned>(land.haveKnights));
	Want(ai, archer, static_cast<unsigned>(land.haveArchers));
	Want(ai, ballista, static_cast<unsigned>(land.haveBallistas));

	// The ships, by the decisions of the shipyard in the same way (wc2ai::ShipyardDecision).
	const char *tanker = orc ? "unit-orc-oil-tanker" : "unit-human-oil-tanker";
	const char *destroyer = orc ? "unit-orc-destroyer" : "unit-human-destroyer";
	const char *transport = orc ? "unit-orc-transport" : "unit-human-transport";
	const char *battleship = orc ? "unit-ogre-juggernaught" : "unit-battleship";
	const char *submarine = orc ? "unit-orc-submarine" : "unit-human-submarine";
	wc2ai::ShipyardView sea;
	sea.haveTankers = Standing(owner, queued, tanker);
	sea.haveDestroyers = Standing(owner, queued, destroyer);
	sea.haveTransports = Standing(owner, queued, transport);
	sea.haveBattleships = Standing(owner, queued, battleship);
	sea.haveSubmarines = Standing(owner, queued, submarine);
	sea.wantTankers = may(tanker) ? state.params[0x18] : 0;
	sea.wantDestroyers = may(destroyer) ? state.params[0x19] : 0;
	sea.wantTransports = may(transport) ? state.params[0x1A] : 0;
	sea.wantBattleships = state.params[0x1B];
	sea.wantSubmarines = state.params[0x1C];
	sea.battleshipsCan = there("unit-human-foundry", "unit-orc-foundry") && may(battleship);
	sea.submarinesCan = there("unit-inventor", "unit-alchemist") && may(submarine);
	const int shipyardFree = OpenDecisions(ai, owner, orc ? "unit-orc-shipyard" : "unit-human-shipyard",
	                                       {{tanker, true}, {destroyer, true}, {transport, true},
	                                        {battleship, sea.battleshipsCan}, {submarine, sea.submarinesCan}});
	for (int n = 0; n < shipyardFree; ++n) {
		switch (wc2ai::ShipyardDecision(sea, SyncDice, nullptr)) {
			case wc2ai::Train::Tanker: ++sea.haveTankers; break;
			case wc2ai::Train::Destroyer: ++sea.haveDestroyers; break;
			case wc2ai::Train::Transport: ++sea.haveTransports; break;
			case wc2ai::Train::Battleship: ++sea.haveBattleships; break;
			case wc2ai::Train::Submarine: ++sea.haveSubmarines; break;
			default: break;
		}
	}
	Want(ai, tanker, static_cast<unsigned>(sea.haveTankers));
	// The oil platform is in no build list either: the original's tanker, when its player has no
	// platform to use, builds one on the oil patch nearest to its depot (local evidence record
	// AI-OIL-001). Here: one platform is wanted as soon as the player has a tanker.
	Want(ai, orc ? "unit-orc-oil-platform" : "unit-human-oil-platform", CountOf(owner, {tanker}) > 0 ? 1 : 0);
	Want(ai, destroyer, static_cast<unsigned>(sea.haveDestroyers));
	Want(ai, transport, static_cast<unsigned>(sea.haveTransports));
	Want(ai, battleship, static_cast<unsigned>(sea.haveBattleships));
	Want(ai, submarine, static_cast<unsigned>(sea.haveSubmarines));
	// Casters and air, towards the script's wanted numbers (rules with dice are established for the
	// barracks and the shipyard only).
	Want(ai, orc ? "unit-death-knight" : "unit-mage", state.params[0x1D]);
	Want(ai, orc ? "unit-zeppelin" : "unit-balloon", state.params[0x1E]);
	Want(ai, orc ? "unit-goblin-sappers" : "unit-dwarves", state.params[0x1F]);
	Want(ai, orc ? "unit-dragon" : "unit-gryphon-rider", state.params[0x20]);

	// Towers: the original's scout / watch tower, when it decides, upgrades to the guard tower and
	// to nothing else; a tower type the script's cadence table marks "never" stays as it is. One
	// tower a second here, through the engine's upgrade path (it asks the price); the guard
	// tower's own dependency rule is asked as for the hall.
	if (AiPlayer == &ai && wc2ai::BuildingDecides(*script, 0x40 + orc)) {
		const CUnitType *watch = UnitTypeByIdentOrNull(orc ? "unit-orc-watch-tower" : "unit-human-watch-tower");
		CUnitType *guard = UnitTypeByIdentOrNull(orc ? "unit-orc-guard-tower" : "unit-human-guard-tower");
		if (watch && guard && CheckDependByType(owner, *guard)) {
			for (const CUnit *tower : FindPlayerUnitsByType(owner, *watch, true)) {
				if (tower->IsIdle()) {
					AiAddUpgradeToRequest(*guard);
					break;
				}
			}
		}
	}
	return true;
}

std::string Wc2AiSaveText()
{
	std::string text;
	for (int i = 0; i < PlayerMax; ++i) {
		const wc2ai::State &state = Wc2AiStates[i];
		if (Wc2AiIds[i] < 0) {
			continue;
		}
		text += "if SetWc2AiState then SetWc2AiState(" + std::to_string(i) + ", " + std::to_string(Wc2AiIds[i])
		        + ", " + std::to_string(state.id) + ", " + std::to_string(state.ip) + ", "
		        + std::to_string(state.wait) + ", " + std::to_string(state.executed) + ", {";
		for (int p = 0; p <= wc2ai::ParamLast; ++p) {
			text += std::to_string(state.params[p]) + (p < wc2ai::ParamLast ? ", " : "");
		}
		text += "}) end\n";
		if (!Wc2AiFerry[i].empty()) {
			text += "if SetWc2AiFerry then SetWc2AiFerry(" + std::to_string(i) + ", {";
			for (const FerryWait &wait : Wc2AiFerry[i]) {
				text += std::to_string(wait.unit) + ", " + std::to_string(wait.transport) + ", "
				        + std::to_string(wait.region) + ", " + std::to_string(wait.x) + ", " + std::to_string(wait.y) + ", ";
			}
			text += "}) end\n";
		}
	}
	if (Wc2AiReservedReady) {
		text += "if SetWc2AiReserved then SetWc2AiReserved(" + std::to_string(Map.Info.MapWidth) + ", "
		        + std::to_string(Map.Info.MapHeight) + ", \"" + wc2ai::ReservedText(Wc2AiReserved) + "\") end\n";
	}
	return text;
}

/// Lua: SetWc2AiReserved(width, height, "start,count,...") -- the reserved tiles from a saved game.
static int CclSetWc2AiReserved(lua_State *l)
{
	LuaCheckArgs(l, 3);
	const int width = LuaToNumber(l, 1);
	const int height = LuaToNumber(l, 2);
	if (width <= 0 || height <= 0) {
		return 0;
	}
	Wc2AiReserved = wc2ai::ReservedFromText(std::string(LuaToString(l, 3)), static_cast<size_t>(width) * height);
	Wc2AiReservedReady = true;
	return 0;
}

/// Lua: SetWc2AiState(player, mapId, scriptId, ip, wait, executed, {params}) -- from a saved game.
static int CclSetWc2AiState(lua_State *l)
{
	LuaCheckArgs(l, 7);
	const int player = LuaToNumber(l, 1);
	if (player < 0 || player >= PlayerMax || !lua_istable(l, 7)) {
		return 0;
	}
	Wc2AiIds[player] = LuaToNumber(l, 2);
	wc2ai::State &state = Wc2AiStates[player];
	state = wc2ai::State();
	state.id = LuaToNumber(l, 3);
	state.ip = static_cast<uint16_t>(LuaToNumber(l, 4));
	state.wait = static_cast<uint32_t>(LuaToNumber(l, 5));
	state.executed = static_cast<uint32_t>(LuaToNumber(l, 6));
	for (int p = 0; p <= wc2ai::ParamLast; ++p) {
		lua_rawgeti(l, 7, p + 1);
		state.params[p] = static_cast<uint8_t>(lua_isnumber(l, -1) ? lua_tonumber(l, -1) : 0);
		lua_pop(l, 1);
	}
	return 0;
}

/// Lua: SetWc2AiFerry(player, {unit, transport, region, x, y, ...}) -- the land units that wait for
/// a transport, from a saved game (after SetWc2AiState of the same player).
static int CclSetWc2AiFerry(lua_State *l)
{
	LuaCheckArgs(l, 2);
	const int player = LuaToNumber(l, 1);
	if (player < 0 || player >= PlayerMax || !lua_istable(l, 2)) {
		return 0;
	}
	std::vector<FerryWait> &waits = Wc2AiFerry[player];
	waits.clear();
	const int count = static_cast<int>(lua_rawlen(l, 2));
	for (int i = 1; i + 4 <= count; i += 5) {
		int values[5];
		for (int k = 0; k < 5; ++k) {
			lua_rawgeti(l, 2, i + k);
			values[k] = lua_isnumber(l, -1) ? static_cast<int>(lua_tonumber(l, -1)) : -1;
			lua_pop(l, 1);
		}
		waits.push_back(FerryWait{values[0], values[1], values[2], values[3], values[4]});
	}
	return 0;
}

/// Lua: GetWc2AiFerry(player) -> text, one line per waiting land unit
/// ("unit <n> transport <n|-1> region <r> place <x>,<y> aboard <0|1>"), then one line per transport
/// of the player ("transport <n> <x>,<y> action <n> load <k> free <0|1>"). For checks through the
/// debug bridge. Read-only.
static int CclGetWc2AiFerry(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int player = LuaToNumber(l, 1);
	std::string text;
	if (player >= 0 && player < PlayerMax) {
		for (const FerryWait &wait : Wc2AiFerry[player]) {
			const CUnit *unit = WaitingUnit(player, wait);
			text += "unit " + std::to_string(wait.unit) + " transport " + std::to_string(wait.transport) + " region "
			        + std::to_string(wait.region) + " place " + std::to_string(wait.x) + "," + std::to_string(wait.y)
			        + " aboard " + std::to_string(unit && unit->Removed ? 1 : 0) + "\n";
		}
		for (const CUnit *unit : Players[player].GetUnits()) {
			if (!unit || !unit->IsAlive() || !unit->Type || !unit->Type->CanTransport()
				|| unit->Type->MoveType != EMovement::Naval) {
				continue;
			}
			text += "transport " + std::to_string(UnitNumber(*unit)) + " " + std::to_string(unit->tilePos.x) + ","
			        + std::to_string(unit->tilePos.y) + " action "
			        + std::to_string(static_cast<int>(unit->CurrentAction())) + " load " + std::to_string(unit->BoardCount)
			        + " free " + std::to_string(FreeTransport(player, *unit) ? 1 : 0) + "\n";
		}
	}
	lua_pushstring(l, text.c_str());
	return 1;
}

/// Lua: SetWc2AiSent(player, {unit numbers}) -- called by a game saved while the sent units were
/// kept as a list; the list is not used (the home role is saved with each unit).
static int CclSetWc2AiSent(lua_State *l)
{
	LuaCheckArgs(l, 2);
	const int player = LuaToNumber(l, 1);
	if (player < 0 || player >= PlayerMax || !lua_istable(l, 2)) {
		return 0;
	}
	return 0;
}

/// Lua: GetWc2AiSentCount(player) -> how many living units are held to a wave place (role 2).
static int CclGetWc2AiSentCount(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int player = LuaToNumber(l, 1);
	int sent = 0;
	if (player >= 0 && player < PlayerMax) {
		for (const CUnit *unit : Players[player].GetUnits()) {
			sent += (unit && unit->IsAlive() && unit->HomeRole == HomeRolePoint) ? 1 : 0;
		}
	}
	lua_pushnumber(l, sent);
	return 1;
}

/// Lua: GetWc2AiState(player) -> scriptId, ip, wait, executed, {params by number}
static int CclGetWc2AiState(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int player = LuaToNumber(l, 1);
	static const wc2ai::State none;
	const wc2ai::State &state = (player >= 0 && player < PlayerMax) ? Wc2AiStates[player] : none;
	lua_pushnumber(l, state.id);
	lua_pushnumber(l, state.ip);
	lua_pushnumber(l, state.wait);
	lua_pushnumber(l, state.executed);
	lua_newtable(l);
	for (int p = wc2ai::ParamFirst; p <= wc2ai::ParamLast; ++p) {
		lua_pushnumber(l, state.params[p]);
		lua_rawseti(l, -2, p);
	}
	return 5;
}

/// Lua: GetWc2AiQueue(player) -> text, one line per entry of the engine's production queue of the
/// player ("<type> <made>/<want> wait <cycle>"), then its upgrade and research requests and its
/// supply / demand. For checks through the debug bridge.
static int CclGetWc2AiQueue(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int player = LuaToNumber(l, 1);
	std::string text;
	if (player >= 0 && player < PlayerMax && Players[player].Ai) {
		const PlayerAi &ai = *Players[player].Ai;
		for (const AiBuildQueue &queue : ai.UnitTypeBuilt) {
			text += queue.Type->Ident + " " + std::to_string(queue.Made) + "/" + std::to_string(queue.Want)
			        + " wait " + std::to_string(queue.Wait) + "\n";
		}
		for (const CUnitType *type : ai.UpgradeToRequests) {
			text += "upgrade-to " + type->Ident + "\n";
		}
		for (const CUpgrade *upgrade : ai.ResearchRequests) {
			text += "research " + upgrade->Ident + "\n";
		}
		text += "supply " + std::to_string(Players[player].Supply) + " demand "
		        + std::to_string(Players[player].Demand) + " cycle " + std::to_string(GameCycle) + "\n";
	}
	lua_pushstring(l, text.c_str());
	return 1;
}

/// Lua: GetWc2AiWorkers(player) -> text, for checks through the debug bridge. Read-only.
///   "stock gold G wood W oil O"
///   "upgrade-to <type> cost g/w/o missing <names|none> depend <0|1>" per upgrade request
///   "hall <number> <type> <x>,<y> action <n> orders <k> idle <0|1>" per building that takes resources in
///   "gold-from <hall number>: mine <number> <x>,<y> stock <n>" or "gold-from <hall number>: none"
///       -- what the worker assignment finds when it looks for gold from that hall
///   "worker <number> <x>,<y> action <n> orders <k> res <id> held <n> state <-|new|started|waiting|returning> in <type|->"
///   "split gold G wood W workers N lumber-wanted T"   -- the original's split rule, stock at hand
///   "workers N idle I gold A wood B other C carrying K"
/// `action` is the engine's order number (1 still, 5 move, 10 train, 11 upgrade-to, 13 being built,
/// 17 build, 19 repair, 20 gather).
static int CclGetWc2AiWorkers(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int index = LuaToNumber(l, 1);
	std::string text;
	if (index < 0 || index >= PlayerMax) {
		lua_pushstring(l, text.c_str());
		return 1;
	}
	const CPlayer &player = Players[index];
	const auto stock = [&](int resource) { return player.Resources[resource] + player.StoredResources[resource]; };
	text += "stock gold " + std::to_string(stock(GoldCost)) + " wood " + std::to_string(stock(WoodCost))
	        + " oil " + std::to_string(stock(OilCost)) + "\n";
	if (player.Ai) {
		for (const CUnitType *type : player.Ai->UpgradeToRequests) {
			const int *costs = type->Stats[index].Costs;
			std::string missing;
			for (int c = GoldCost; c <= OilCost; ++c) {
				if (stock(c) < costs[c]) {
					missing += (missing.empty() ? "" : ",") + DefaultResourceNames[c];
				}
			}
			text += "upgrade-to " + type->Ident + " cost " + std::to_string(costs[GoldCost]) + "/"
			        + std::to_string(costs[WoodCost]) + "/" + std::to_string(costs[OilCost]) + " missing "
			        + (missing.empty() ? "none" : missing) + " depend "
			        + std::to_string(CheckDependByType(player, *type) ? 1 : 0) + "\n";
		}
	}
	const CUnit *anyWorker = nullptr;
	for (const CUnit *unit : player.GetUnits()) {
		if (unit && unit->IsAlive() && unit->Type && unit->Type->BoolFlag[HARVESTER_INDEX].value
			&& unit->Type->ResInfo[GoldCost]) {
			anyWorker = unit;
			break;
		}
	}
	int workers = 0, idle = 0, onGold = 0, onWood = 0, other = 0, carrying = 0;
	std::string workerLines;
	for (const CUnit *unit : player.GetUnits()) {
		if (!unit || !unit->IsAlive() || !unit->Type) {
			continue;
		}
		const CUnitType &type = *unit->Type;
		const std::string place = std::to_string(unit->tilePos.x) + "," + std::to_string(unit->tilePos.y);
		const std::string order = " action " + std::to_string(static_cast<int>(unit->CurrentAction()))
		                          + " orders " + std::to_string(unit->Orders.size());
		if (type.Building && type.CanStore[GoldCost]) {
			text += "hall " + std::to_string(UnitNumber(*unit)) + " " + type.Ident + " " + place + order
			        + " idle " + std::to_string(unit->IsIdle() ? 1 : 0) + "\n";
			if (anyWorker) {
				const CUnit *mine = UnitFindResource(*anyWorker, *unit, 1000, GoldCost, true);
				text += "gold-from " + std::to_string(UnitNumber(*unit)) + ": "
				        + (mine ? "mine " + std::to_string(UnitNumber(*mine)) + " "
				                      + std::to_string(mine->tilePos.x) + "," + std::to_string(mine->tilePos.y)
				                      + " stock " + std::to_string(mine->ResourcesHeld)
				                : std::string("none"))
				        + "\n";
			}
		}
		if (!type.BoolFlag[HARVESTER_INDEX].value) {
			continue;
		}
		++workers;
		std::string state = "-";
		int resource = unit->CurrentResource;
		if (unit->CurrentAction() == UnitAction::Resource) {
			const COrder_Resource &gather = *static_cast<const COrder_Resource *>(unit->CurrentOrder());
			resource = gather.GetCurrentResource();
			state = gather.IsGatheringFinished() ? "returning"
			        : gather.IsGatheringWaiting() ? "waiting" : gather.IsGatheringStarted() ? "started" : "new";
			++(resource == GoldCost ? onGold : resource == WoodCost ? onWood : other);
		} else if (unit->IsIdle()) {
			++idle;
		} else {
			++other;
		}
		if (unit->ResourcesHeld) {
			++carrying;
		}
		workerLines += "worker " + std::to_string(UnitNumber(*unit)) + " " + place + order + " res "
		               + std::to_string(resource) + " held " + std::to_string(unit->ResourcesHeld) + " state "
		               + state + " in " + (unit->Container ? unit->Container->Type->Ident : std::string("-")) + "\n";
	}
	text += workerLines;
	// what the split rule (wc2ai::LumberWorkers) asks for, from the stock at hand
	text += "split gold " + std::to_string(player.Resources[GoldCost]) + " wood "
	        + std::to_string(player.Resources[WoodCost]) + " workers " + std::to_string(Wc2AiWorkerCount(player))
	        + " lumber-wanted " + std::to_string(Wc2AiLumberWanted(player)) + "\n";
	text += "workers " + std::to_string(workers) + " idle " + std::to_string(idle) + " gold "
	        + std::to_string(onGold) + " wood " + std::to_string(onWood) + " other " + std::to_string(other)
	        + " carrying " + std::to_string(carrying) + "\n";
	lua_pushstring(l, text.c_str());
	return 1;
}

/// Lua: GetWc2AiPlace(player, kind) -> found, x, y: where a land (kind 0), a sea (kind 1) or an
/// air (kind 2) wave against `player` would go now. For checks through the debug bridge.
/// Read-only: the land place is taken from a strength map of its own, not the game's.
static int CclGetWc2AiPlace(lua_State *l)
{
	LuaCheckArgs(l, 2);
	const int player = LuaToNumber(l, 1);
	const int kind = LuaToNumber(l, 2);
	wc2ai::Place place;
	if (player >= 0 && player < PlayerMax && (kind == 1 || kind == 2)) {
		const std::vector<wc2ai::PlaceUnit> units = TargetUnits(Players[player]);
		place = kind == 1 ? wc2ai::SeaPlace(units) : wc2ai::AirPlace(units);
	} else if (player >= 0 && player < PlayerMax && kind == 0) {
		place = LandPlaceOf(Players[player], BuildCells());
	}
	lua_pushboolean(l, place.found);
	lua_pushnumber(l, place.x);
	lua_pushnumber(l, place.y);
	return 3;
}

/// Lua: GetWc2AiRate(type) -> the attack rate of an original unit type 0x00..0x39 (0 when the
/// game folder has no Data/Rez/script.bin). For checks through the debug bridge. Read-only.
static int CclGetWc2AiRate(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int type = LuaToNumber(l, 1);
	lua_pushnumber(l, (type >= 0 && type < wc2ai::RateTypes) ? wc2ai::GameAttackRates()[type] : 0);
	return 1;
}

/// Lua: GetWc2AiWeight(ident) -> the strength weight of a unit type, the original's number of the
/// type (-1: none). For checks through the debug bridge. Read-only.
static int CclGetWc2AiWeight(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const CUnitType *type = UnitTypeByIdentOrNull(LuaToString(l, 1));
	lua_pushnumber(l, type ? WeightOf(*type) : 0);
	lua_pushnumber(l, type ? TypeNumberOf(*type) : -1);
	return 2;
}

/// Lua: GetWc2AiCell(x, y, slot) -> holder, kind, land, sea: of the strength map's cell the tile
/// is in, the slot that holds it (-1: none), its kind (0 land, 1 water, 2 neither) and what `slot`
/// has standing there against land and against sea, as the units stand now. For checks through
/// the debug bridge. Read-only.
static int CclGetWc2AiCell(lua_State *l)
{
	LuaCheckArgs(l, 3);
	const int slot = LuaToNumber(l, 3);
	const wc2ai::CellMap cells = BuildCells();
	const wc2ai::CellRecord *cell = wc2ai::CellAt(cells, LuaToNumber(l, 1), LuaToNumber(l, 2));
	const bool counted = cell && slot >= 0 && slot < wc2ai::CellSlots;
	lua_pushnumber(l, cell ? cell->owner : -1);
	lua_pushnumber(l, cell ? cell->kind : 2);
	lua_pushnumber(l, counted ? cell->land[slot] : 0);
	lua_pushnumber(l, counted ? cell->sea[slot] : 0);
	return 4;
}

/// Lua: SetWc2AiId(player, id) -- the map's AI byte of a player slot.
static int CclSetWc2AiId(lua_State *l)
{
	LuaCheckArgs(l, 2);
	const int player = LuaToNumber(l, 1);
	const int id = LuaToNumber(l, 2);
	if (player >= 0 && player < PlayerMax) {
		Wc2AiIds[player] = (id >= 0 && id <= 255) ? id : -1;
	}
	return 0;
}

/// Lua: GetWc2AiId(player) -> the id, or -1.
static int CclGetWc2AiId(lua_State *l)
{
	LuaCheckArgs(l, 1);
	lua_pushnumber(l, Wc2AiIdOf(LuaToNumber(l, 1)));
	return 1;
}

/// Lua: GetWc2AiInfo([id]) -> scripts loaded (0 when the file is absent); with an id also the
/// script's build-list length and its instruction count (-1 when there is no such script).
static int CclGetWc2AiInfo(lua_State *l)
{
	const wc2ai::Program &program = wc2ai::GameProgram();
	lua_pushnumber(l, program.Loaded() ? wc2ai::ScriptCount : 0);
	if (lua_gettop(l) < 2) {
		return 1;
	}
	const int id = LuaToNumber(l, 1);
	const wc2ai::Script *script = program.Get(id);
	lua_pushnumber(l, script ? static_cast<int>(script->buildList.size()) : -1);
	lua_pushnumber(l, script ? program.InstructionCount(id) : -1);
	return 3;
}

void Wc2AiCclRegister()
{
	lua_register(Lua, "SetWc2AiId", CclSetWc2AiId);
	lua_register(Lua, "GetWc2AiId", CclGetWc2AiId);
	lua_register(Lua, "GetWc2AiInfo", CclGetWc2AiInfo);
	lua_register(Lua, "SetWc2AiState", CclSetWc2AiState);
	lua_register(Lua, "SetWc2AiReserved", CclSetWc2AiReserved);
	lua_register(Lua, "GetWc2AiState", CclGetWc2AiState);
	lua_register(Lua, "SetWc2AiSent", CclSetWc2AiSent);
	lua_register(Lua, "SetWc2AiFerry", CclSetWc2AiFerry);
	lua_register(Lua, "GetWc2AiFerry", CclGetWc2AiFerry);
	lua_register(Lua, "GetWc2AiSentCount", CclGetWc2AiSentCount);
	lua_register(Lua, "GetWc2AiQueue", CclGetWc2AiQueue);
	lua_register(Lua, "GetWc2AiWorkers", CclGetWc2AiWorkers);
	lua_register(Lua, "GetWc2AiPlace", CclGetWc2AiPlace);
	lua_register(Lua, "GetWc2AiRate", CclGetWc2AiRate);
	lua_register(Lua, "GetWc2AiWeight", CclGetWc2AiWeight);
	lua_register(Lua, "GetWc2AiCell", CclGetWc2AiCell);
}
