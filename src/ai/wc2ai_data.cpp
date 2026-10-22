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
/**@name wc2ai_data.cpp - Reader of the original game's computer-player definitions. */
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

#include "wc2ai.h"

#include "home_leash.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace wc2ai
{

namespace
{

uint16_t U16(const std::vector<uint8_t> &d, size_t o)
{
	return static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
}

bool Fail(std::string *error, const std::string &why)
{
	if (error) {
		*error = why;
	}
	return false;
}

} // namespace

bool Program::Decode(uint16_t address, Instruction &out) const
{
	const size_t size = bytes.size();
	if (address >= size) {
		return false;
	}
	out = Instruction();
	out.address = address;
	switch (bytes[address]) {
		case 0:
			if (address + 3u > size) {
				return false;
			}
			out.op = Op::Set;
			out.a = bytes[address + 1];
			out.b = bytes[address + 2];
			out.next = static_cast<uint16_t>(address + 3);
			return true;
		case 1:
			if (address + 3u > size) {
				return false;
			}
			out.op = Op::Goto;
			out.a = U16(bytes, address + 1);
			out.next = static_cast<uint16_t>(out.a);
			return out.a < size;
		case 2:
			if (address + 5u > size) {
				return false;
			}
			out.op = Op::Wait;
			out.a = static_cast<uint32_t>(bytes[address + 1]) | (static_cast<uint32_t>(bytes[address + 2]) << 8)
			        | (static_cast<uint32_t>(bytes[address + 3]) << 16)
			        | (static_cast<uint32_t>(bytes[address + 4]) << 24);
			out.next = static_cast<uint16_t>(address + 5);
			return true;
		case 3:
			if (address + 2u > size) {
				return false;
			}
			out.op = Op::Until;
			out.a = bytes[address + 1];
			out.next = static_cast<uint16_t>(address + 2);
			return true;
		default:
			return false;
	}
}

int Program::InstructionCount(int id) const
{
	const Script *script = Get(id);
	if (!script) {
		return -1;
	}
	std::set<uint16_t> seen;
	uint16_t ip = script->first;
	int count = 0;
	while (seen.insert(ip).second) {
		Instruction in;
		if (!Decode(ip, in)) {
			return -1;
		}
		++count;
		ip = in.next;
	}
	return count;
}

const Script *Program::Get(int id) const
{
	if (id < 0 || id >= static_cast<int>(scripts.size())) {
		return nullptr;
	}
	return &scripts[id];
}

bool Program::Parse(const uint8_t *data, size_t size, std::string *error)
{
	bytes.clear();
	scripts.clear();
	const size_t tableBytes = (ScriptCount + 2) * 2;
	if (!data || size < tableBytes || size > 0xFFFF) {
		return Fail(error, "the file is too small or too large to be the definition file");
	}
	bytes.assign(data, data + size);

	std::vector<Script> parsed(ScriptCount);
	for (int id = 0; id < ScriptCount; ++id) {
		Script &s = parsed[id];
		s.entry = U16(bytes, id * 2);
		if (s.entry < tableBytes || s.entry + 4u > size) {
			bytes.clear();
			return Fail(error, "script " + std::to_string(id) + ": entry outside the file");
		}
		s.first = static_cast<uint16_t>(s.entry + 4);
		const uint16_t list = U16(bytes, s.entry);
		const uint16_t cadence = U16(bytes, s.entry + 2);
		if (list >= size || cadence + CadenceCount * 2u > size) {
			bytes.clear();
			return Fail(error, "script " + std::to_string(id) + ": build list or cadence table outside the file");
		}
		for (size_t o = list; o < size && bytes[o] != 0xFF && s.buildList.size() < BuildListMax; ++o) {
			s.buildList.push_back(bytes[o]);
		}
		s.cadence.resize(CadenceCount);
		for (int i = 0; i < CadenceCount; ++i) {
			s.cadence[i] = U16(bytes, cadence + i * 2);
		}
	}
	scripts = std::move(parsed);

	// Every script must decode to the point where it loops, with parameters in range.
	for (int id = 0; id < ScriptCount; ++id) {
		std::set<uint16_t> seen;
		uint16_t ip = scripts[id].first;
		while (seen.insert(ip).second) {
			Instruction in;
			if (!Decode(ip, in)) {
				scripts.clear();
				bytes.clear();
				return Fail(error, "script " + std::to_string(id) + ": not an instruction at offset "
				                   + std::to_string(ip));
			}
			if (in.op == Op::Set && (in.a < ParamFirst || in.a > ParamLast)) {
				scripts.clear();
				bytes.clear();
				return Fail(error, "script " + std::to_string(id) + ": parameter out of range at offset "
				                   + std::to_string(ip));
			}
			if (in.op == Op::Until && in.a > 7) {
				scripts.clear();
				bytes.clear();
				return Fail(error, "script " + std::to_string(id) + ": condition out of range at offset "
				                   + std::to_string(ip));
			}
			ip = in.next;
		}
	}
	return true;
}

bool BuildingDecides(const Script &script, unsigned buildingType)
{
	if (buildingType < 0x3A || buildingType - 0x3A >= script.cadence.size()) {
		return true;
	}
	return script.cadence[buildingType - 0x3A] != 0xFFFF;
}

Train BarracksDecision(const BarracksView &view, Dice dice, void *context)
{
	// A class that is wanted and of which there is none comes first, in this order.
	if (view.wantFootmen > 0 && view.haveFootmen == 0) {
		return Train::Footman;
	}
	if (view.wantKnights > 0 && view.haveKnights == 0 && view.knightsCan) {
		return Train::Knight;
	}
	if (view.wantArchers > 0 && view.haveArchers == 0 && view.archersCan) {
		return Train::Archer;
	}
	// Then towards the wanted numbers: archers and ballistas by the dice, knights and footmen without.
	if (view.haveArchers < view.wantArchers && view.archersCan && dice && dice(4, context)) {
		return Train::Archer;
	}
	if (view.haveBallistas < view.wantBallistas && view.ballistasCan && dice && dice(8, context)) {
		return Train::Ballista;
	}
	if (view.haveKnights < view.wantKnights && view.knightsCan) {
		return Train::Knight;
	}
	if (view.haveFootmen < view.wantFootmen) {
		return Train::Footman;
	}
	return Train::None;
}

Train ShipyardDecision(const ShipyardView &view, Dice dice, void *context)
{
	if (view.wantTankers > 0 && view.haveTankers == 0) {
		return Train::Tanker;
	}
	if (view.wantDestroyers > 0 && view.haveDestroyers == 0) {
		return Train::Destroyer;
	}
	if (view.wantTransports > 0 && view.haveTransports == 0) {
		return Train::Transport;
	}
	if (view.haveDestroyers < view.wantDestroyers && dice && dice(4, context)) {
		return Train::Destroyer;
	}
	if (view.haveBattleships < view.wantBattleships && view.battleshipsCan && dice && dice(8, context)) {
		return Train::Battleship;
	}
	if (view.haveSubmarines < view.wantSubmarines && view.submarinesCan && dice && dice(8, context)) {
		return Train::Submarine;
	}
	if (view.haveTransports < view.wantTransports && dice && dice(8, context)) {
		return Train::Transport;
	}
	if (view.haveTankers < view.wantTankers) {
		return Train::Tanker;
	}
	if (view.haveDestroyers < view.wantDestroyers) {
		return Train::Destroyer;
	}
	return Train::None;
}

bool Start(const Program &program, int id, State &state)
{
	const Script *script = program.Get(id);
	state = State();
	if (!script) {
		return false;
	}
	state.id = id;
	state.ip = script->first;
	state.params[ParamLast] = 0xFF; // the whole build list is active until a script says otherwise
	return true;
}

void Step(const Program &program, State &state, bool (*holds)(int condition, void *context), void *context)
{
	if (state.id < 0) {
		return;
	}
	if (state.wait > 0) {
		--state.wait;
		return;
	}
	// A script never carries out more than a handful of instructions between two waits; the bound
	// only keeps a damaged state from holding the game.
	for (int guard = 0; guard < 512; ++guard) {
		Instruction in;
		if (!program.Decode(state.ip, in)) {
			state.id = -1;
			return;
		}
		switch (in.op) {
			case Op::Set:
				if (in.a <= static_cast<uint32_t>(ParamLast)) {
					state.params[in.a] = static_cast<uint8_t>(in.b);
				}
				break;
			case Op::Goto:
				break;
			case Op::Wait:
				state.wait = in.a;
				state.ip = in.next;
				++state.executed;
				return;
			case Op::Until:
				if (!holds || !holds(static_cast<int>(in.a), context)) {
					return; // asked again next cycle
				}
				break;
		}
		state.ip = in.next;
		++state.executed;
	}
}

int AnimationLength(const uint8_t *data, size_t size, uint16_t offset)
{
	size_t at = offset;
	int length = 0;
	bool ended = false;
	for (size_t steps = 0; steps < size; ++steps) {
		if (at >= size) {
			return -1;
		}
		const uint8_t op = data[at];
		size_t operands = 0;
		switch (op) {
			case 0x01: case 0x04: case 0x05: case 0x07: case 0x08: case 0x09: case 0x0C: case 0x0D:
				operands = 1;
				break;
			case 0x03: case 0x06: case 0x0E:
				operands = 2;
				break;
			default:
				break;
		}
		if (at + operands >= size) {
			return -1;
		}
		switch (op) {
			case 0x00:
				length = (length + 1) & 0xFF;
				if (ended) {
					return length;
				}
				ended = true;
				break;
			case 0x01: case 0x07: case 0x08: case 0x09: case 0x0C:
				length = (length + data[at + 1]) & 0xFF;
				break;
			case 0x03:
				at = static_cast<size_t>(data[at + 1] | (data[at + 2] << 8));
				continue;
			default:
				break;
		}
		at += 1 + operands;
	}
	return -1;
}

bool AttackRates(const uint8_t *data, size_t size, uint8_t rates[RateTypes])
{
	for (int type = 0; type < RateTypes; ++type) {
		rates[type] = 0;
	}
	if (!data || size < static_cast<size_t>(RateTypes) * 2) {
		return false;
	}
	int lengths[RateTypes] = {};
	for (int type = 0; type < RateTypes; ++type) {
		const size_t table = static_cast<size_t>(data[type * 2] | (data[type * 2 + 1] << 8)) / 2;
		const size_t entry = (table + RateAttackEntry) * 2;
		if (entry + 1 >= size) {
			return false;
		}
		lengths[type] = AnimationLength(data, size, static_cast<uint16_t>(data[entry] | (data[entry + 1] << 8)));
		if (lengths[type] < 0) {
			return false;
		}
	}
	for (int type = 0; type < RateTypes; ++type) {
		rates[type] = lengths[type] == 0 ? 0 : static_cast<uint8_t>(10 * lengths[0] / lengths[type]);
	}
	return true;
}

namespace
{

int TileDistance(int x, int y, int toX, int toY)
{
	const int dx = x > toX ? x - toX : toX - x;
	const int dy = y > toY ? y - toY : toY - y;
	return dx > dy ? dx : dy;
}

/// Tile distance of a point to the nearest edge of a unit.
int EdgeDistance(int x, int y, const PlaceUnit &unit)
{
	const int dx = x < unit.x ? unit.x - x : (x > unit.x + unit.w - 1 ? x - (unit.x + unit.w - 1) : 0);
	const int dy = y < unit.y ? unit.y - y : (y > unit.y + unit.h - 1 ? y - (unit.y + unit.h - 1) : 0);
	return dx > dy ? dx : dy;
}

/// The target's hall nearest to its first unit (the first one of equals); null without a hall.
const PlaceUnit *NearestHall(const std::vector<PlaceUnit> &target)
{
	const PlaceUnit *hall = nullptr;
	int best = 0;
	for (const PlaceUnit &unit : target) {
		if (!unit.hall) {
			continue;
		}
		const int distance = TileDistance(unit.x, unit.y, target.front().x, target.front().y);
		if (!hall || distance < best) {
			hall = &unit;
			best = distance;
		}
	}
	return hall;
}

} // namespace

Place SeaPlace(const std::vector<PlaceUnit> &target)
{
	if (target.empty()) {
		return {};
	}
	const PlaceUnit *shipyard = nullptr;
	for (const PlaceUnit &unit : target) {
		if (unit.platform) {
			return {true, unit.x, unit.y};
		}
		if (unit.shipyard) {
			shipyard = &unit;
		}
	}
	if (shipyard) {
		return {true, shipyard->x, shipyard->y};
	}
	const PlaceUnit *hall = NearestHall(target);
	const PlaceUnit &reference = hall ? *hall : target.front();
	const PlaceUnit *ship = nullptr;
	int best = -1;
	for (const PlaceUnit &unit : target) {
		const int distance = TileDistance(unit.x, unit.y, reference.x, reference.y);
		if (unit.ship && distance > best) {
			ship = &unit;
			best = distance;
		}
	}
	return ship ? Place{true, ship->x, ship->y} : Place{};
}

Place AirPlace(const std::vector<PlaceUnit> &target)
{
	if (target.empty()) {
		return {};
	}
	const PlaceUnit *hall = nullptr;
	for (const PlaceUnit &unit : target) {
		if (unit.hall) {
			hall = &unit;
		}
	}
	if (hall) {
		return {true, hall->x, hall->y};
	}
	const PlaceUnit &reference = target.front();
	const PlaceUnit *farthest = &reference;
	int best = 0;
	for (const PlaceUnit &unit : target) {
		const int distance = TileDistance(unit.x, unit.y, reference.x, reference.y);
		if (distance > best) {
			farthest = &unit;
			best = distance;
		}
	}
	return {true, farthest->x, farthest->y};
}

int CellValue(const CellUnit &unit, bool canHit)
{
	if (unit.slot < 0 || unit.slot >= CellSlots || !unit.alive) {
		return 0;
	}
	if (unit.worker) {
		return -2;
	}
	if (unit.hall) {
		return -1;
	}
	if (unit.transport) {
		return -5;
	}
	if (unit.tanker) {
		return -2;
	}
	if (!unit.mobile) {
		return -1;
	}
	if (!canHit || unit.weight == 0 || unit.maxHitPoints <= 0) {
		return 0;
	}
	return unit.hitPoints * unit.weight / unit.maxHitPoints;
}

CellMap BuildCellMap(int mapWidth, int mapHeight, const std::vector<uint8_t> &kinds,
                     const std::vector<CellUnit> &units)
{
	CellMap map;
	map.cellsX = mapWidth / CellTiles;
	map.cellsY = mapHeight / CellTiles;
	if (map.cellsX <= 0 || map.cellsY <= 0) {
		map.cellsX = map.cellsY = 0;
		return map;
	}
	map.cells.resize(static_cast<size_t>(map.cellsX) * map.cellsY);
	for (size_t i = 0; i < map.cells.size(); ++i) {
		map.cells[i].kind = i < kinds.size() ? kinds[i] : 2;
	}
	for (const CellUnit &unit : units) {
		const int cellX = unit.x / CellTiles;
		const int cellY = unit.y / CellTiles;
		if (unit.x < 0 || unit.y < 0 || cellX >= map.cellsX || cellY >= map.cellsY || unit.slot < 0
			|| unit.slot >= CellSlots) {
			continue;
		}
		CellRecord &cell = map.cells[static_cast<size_t>(cellY) * map.cellsX + cellX];
		cell.land[unit.slot] = static_cast<int16_t>(cell.land[unit.slot] + CellValue(unit, unit.hitsLand));
		cell.sea[unit.slot] = static_cast<int16_t>(cell.sea[unit.slot] + CellValue(unit, unit.hitsSea));
	}
	for (CellRecord &cell : map.cells) {
		if (cell.kind > 1) {
			continue;
		}
		const int16_t *column = cell.kind == 0 ? cell.land : cell.sea;
		int best = -100;
		for (int slot = 0; slot < CellSlots; ++slot) {
			if (column[slot] != 0 && column[slot] > best) {
				best = column[slot];
				cell.owner = static_cast<int8_t>(slot);
			}
		}
	}
	return map;
}

const CellRecord *CellAt(const CellMap &cells, int x, int y)
{
	const int cellX = x / CellTiles;
	const int cellY = y / CellTiles;
	if (x < 0 || y < 0 || cellX >= cells.cellsX || cellY >= cells.cellsY) {
		return nullptr;
	}
	return &cells.cells[static_cast<size_t>(cellY) * cells.cellsX + cellX];
}

bool GuardRetreats(const CellMap &cells, int x, int y, int slot, bool ship)
{
	const CellRecord *cell = CellAt(cells, x, y);
	if (!cell || slot < 0 || slot >= CellSlots || cell->owner < 0 || cell->owner >= CellSlots) {
		return false;
	}
	const int16_t *column = ship ? cell->sea : cell->land;
	return StrengthRetreats(cell->owner, slot, column[slot], column[cell->owner]);
}

int TypeNumberOfIdent(const char *const *names, int count, const std::string &ident)
{
	for (int i = 0; names && i < count; ++i) {
		if (names[i] && ident == names[i]) {
			return i;
		}
	}
	return -1;
}

Place LandPlace(int targetSlot, const std::vector<PlaceUnit> &target, const CellMap &cells)
{
	if (target.empty()) {
		return {};
	}
	const PlaceUnit *reference = NearestHall(target);
	for (size_t i = 0; !reference && i < target.size(); ++i) {
		if (target[i].building) {
			reference = &target[i];
		}
	}
	if (!reference) {
		reference = &target.front();
	}
	int cellX = reference->x / CellTiles;
	int cellY = reference->y / CellTiles;
	int best = 0;
	for (int y = 0; y < cells.cellsY; ++y) {
		for (int x = 0; x < cells.cellsX; ++x) {
			const CellRecord &cell = cells.cells[static_cast<size_t>(y) * cells.cellsX + x];
			if (cell.kind != 0 || cell.owner != targetSlot) {
				continue;
			}
			const int pointX = (LandPlaceSwapsAxes ? y : x) * CellTiles + CellTiles / 2;
			const int pointY = (LandPlaceSwapsAxes ? x : y) * CellTiles + CellTiles / 2;
			const int distance = EdgeDistance(pointX, pointY, *reference);
			if (distance > best) {
				best = distance;
				cellX = x;
				cellY = y;
			}
		}
	}
	const int centreX = cellX * CellTiles + CellTiles / 2;
	const int centreY = cellY * CellTiles + CellTiles / 2;
	const PlaceUnit *nearest = nullptr;
	int nearestDistance = 0;
	for (const PlaceUnit &unit : target) {
		if (unit.ship || (unit.flyer && unit.fighter) || unit.platform) {
			continue;
		}
		const int distance = TileDistance(unit.x, unit.y, centreX, centreY);
		if (!nearest || distance < nearestDistance) {
			nearest = &unit;
			nearestDistance = distance;
		}
	}
	return nearest ? Place{true, nearest->x, nearest->y} : Place{true, centreX, centreY};
}

int RepairPick(int gold, int lumber, int workers, int repairers, const std::vector<RepairView> &units)
{
	if (repairers >= RepairersAllowed(workers)) {
		return -1;
	}
	const bool poor = gold < 200 && lumber < 100;
	int best = -1;
	int bestPriority = 0;
	for (size_t i = 0; i < units.size(); ++i) {
		const RepairView &unit = units[i];
		if (!unit.finishedBuilding || unit.oilPlatform || (poor && !unit.hallClass) || !unit.sameGround
			|| unit.hp >= (3 * unit.maxHp) >> 2 || unit.enemyFighterNear) {
			continue;
		}
		if (unit.priority > bestPriority) {
			bestPriority = unit.priority;
			best = static_cast<int>(i);
		}
	}
	return best;
}

Place PlaceWalk(int refX, int refY, int step, int mapSide, bool (*sameGround)(int x, int y, void *context),
                bool (*fits)(int x, int y, void *context), void *context)
{
	if (step <= 0 || mapSide <= 0 || !sameGround || !fits) {
		return Place();
	}
	static const int stepX[4] = {1, 0, -1, 0};
	static const int stepY[4] = {0, 1, 0, -1};
	const int limit = static_cast<uint8_t>(std::max(std::max(refX, mapSide - refX), std::max(refY, mapSide - refY)) - 1);
	for (int k = 1;; ++k) {
		const int side = static_cast<uint8_t>((2 * k + 1) * step);
		if (side >= limit) {
			return Place();
		}
		int x = refX - k * step;
		int y = refY - k * step;
		bool onGround = false;
		for (int direction = 0; direction < 4; ++direction) {
			for (int i = 0; i < 2 * k + 1; ++i) {
				if (x >= 0 && y >= 0 && x < mapSide && y < mapSide && sameGround(x, y, context)) {
					onGround = true;
					if (fits(x, y, context)) {
						return Place{true, x, y};
					}
				}
				x += stepX[direction] * step;
				y += stepY[direction] * step;
			}
			// one step back: the next side starts on this side's last candidate
			x -= stepX[direction] * step;
			y -= stepY[direction] * step;
		}
		if (!onGround) {
			return Place();
		}
	}
}

int NearestOf(const std::vector<int> &distances, bool laterOnTie)
{
	int best = -1;
	for (size_t i = 0; i < distances.size(); ++i) {
		if (distances[i] < 0) {
			continue;
		}
		if (best < 0 || distances[i] < distances[best] || (laterOnTie && distances[i] == distances[best])) {
			best = static_cast<int>(i);
		}
	}
	return best;
}

int PlatformPick(const std::vector<PlatformView> &platforms)
{
	int best = -1;
	int bestScore = 0;
	for (size_t i = 0; i < platforms.size(); ++i) {
		const PlatformView &platform = platforms[i];
		if (!platform.usable) {
			continue;
		}
		const int score = platform.toDepot + platform.toTanker / 2;
		if (best < 0 || score < bestScore) {
			best = static_cast<int>(i);
			bestScore = score;
		}
	}
	return best;
}

int PlatformWaitPick(const std::vector<PlatformView> &platforms)
{
	if (PlatformPick(platforms) >= 0) {
		return -1;
	}
	int best = -1;
	int bestScore = 0;
	for (size_t i = 0; i < platforms.size(); ++i) {
		const PlatformView &platform = platforms[i];
		if (!platform.building) {
			continue;
		}
		const int score = platform.toDepot + platform.toTanker / 2;
		if (best < 0 || score < bestScore) {
			best = static_cast<int>(i);
			bestScore = score;
		}
	}
	return best;
}

Place FleePoint(int x, int y, int direction8, int randX, int randY, int mapWidth, int mapHeight)
{
	static const int dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
	static const int dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
	const int d = direction8 & 7;
	const int fx = std::clamp(x - 2 + 4 * dx[d] + (randX & 7), 0, mapWidth - 1);
	const int fy = std::clamp(y - 2 + 4 * dy[d] + (randY & 7), 0, mapHeight - 1);
	return Place{true, fx, fy};
}

bool SiteTaken(int x, int y, int w, int h, const std::vector<Site> &sites)
{
	for (const Site &site : sites) {
		if (x < site.x + site.w && site.x < x + w && y < site.y + site.h && site.y < y + h) {
			return true;
		}
	}
	return false;
}

/// t / n of d, rounded to the nearest whole number (halves away from zero); 0 when n is 0.
static int RoundedShare(int d, int t, int n)
{
	if (n == 0) {
		return 0;
	}
	return (2 * d * t + (d < 0 ? -n : n)) / (2 * n);
}

static bool InSite(const Site &site, int x, int y)
{
	return x >= site.x && x < site.x + site.w && y >= site.y && y < site.y + site.h;
}

std::vector<Place> HaulLineTiles(const Site &depot, const Site &resource)
{
	std::vector<Place> line;
	const int x0 = depot.x + depot.w / 2;
	const int y0 = depot.y + depot.h / 2;
	const int dx = resource.x + resource.w / 2 - x0;
	const int dy = resource.y + resource.h / 2 - y0;
	const int steps = std::max(std::abs(dx), std::abs(dy));
	for (int t = 0; t <= steps; ++t) {
		const int x = x0 + RoundedShare(dx, t, steps);
		const int y = y0 + RoundedShare(dy, t, steps);
		if (!InSite(depot, x, y) && !InSite(resource, x, y)) {
			line.push_back({true, x, y});
		}
	}
	return line;
}

bool HaulLineBlocked(int x, int y, int w, int h, const std::vector<Place> &line,
                     bool (*open)(int x, int y, void *context), void *context)
{
	const Site building{x, y, w, h};
	static const int sides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
	for (const Place &tile : line) {
		if (InSite(building, tile.x, tile.y)) {
			return true;
		}
	}
	for (const Place &tile : line) {
		for (const auto &side : sides) {
			if (InSite(building, tile.x + side[0], tile.y + side[1])
			    && !open(tile.x - side[0], tile.y - side[1], context)) {
				return true;
			}
		}
	}
	return false;
}

int PointToBoxDistance(int px, int py, int bx, int by, int bw, int bh)
{
	const int dx = px < bx ? bx - px : (px > bx + bw - 1 ? px - (bx + bw - 1) : 0);
	const int dy = py < by ? by - py : (py > by + bh - 1 ? py - (by + bh - 1) : 0);
	return std::max(dx, dy);
}

void ReserveSquare(std::vector<uint8_t> &tiles, int mapWidth, int mapHeight, int x, int y, int size, int grow,
                   bool footprint)
{
	if (tiles.size() != static_cast<size_t>(mapWidth) * mapHeight) {
		return;
	}
	for (int ty = std::max(0, y - grow); ty < std::min(mapHeight, y + size + grow); ++ty) {
		for (int tx = std::max(0, x - grow); tx < std::min(mapWidth, x + size + grow); ++tx) {
			const bool inner = tx >= x && tx < x + size && ty >= y && ty < y + size;
			if (footprint || !inner) {
				tiles[static_cast<size_t>(ty) * mapWidth + tx] = 1;
			}
		}
	}
}

bool ReservedAny(const std::vector<uint8_t> &tiles, int mapWidth, int mapHeight, int x, int y, int w, int h)
{
	if (tiles.size() != static_cast<size_t>(mapWidth) * mapHeight) {
		return false;
	}
	for (int ty = std::max(0, y); ty < std::min(mapHeight, y + h); ++ty) {
		for (int tx = std::max(0, x); tx < std::min(mapWidth, x + w); ++tx) {
			if (tiles[static_cast<size_t>(ty) * mapWidth + tx]) {
				return true;
			}
		}
	}
	return false;
}

std::vector<Place> RoadTiles(int x0, int y0, int x1, int y1)
{
	std::vector<Place> road;
	if (x0 == x1 || y0 == y1) {
		return road;
	}
	// 0x4B7890 / 0x4B7630: the error starts at half the major length (at least 1); each step takes
	// the minor length off it, and the minor axis steps when it is then 0 or below.
	const int dx = std::abs(x1 - x0);
	const int dy = std::abs(y1 - y0);
	const int sx = x1 > x0 ? 1 : -1;
	const int sy = y1 > y0 ? 1 : -1;
	const bool xMajor = dx >= dy;
	const int major = xMajor ? dx : dy;
	const int minor = xMajor ? dy : dx;
	int err = major / 2;
	if (err == 0) {
		err = 1;
	}
	int x = x0;
	int y = y0;
	while (x != x1 && y != y1) {
		err -= minor;
		const bool minorStep = err <= 0;
		if (minorStep) {
			err += major;
		}
		if (xMajor) {
			x += sx;
			y += minorStep ? sy : 0;
		} else {
			y += sy;
			x += minorStep ? sx : 0;
		}
		road.push_back(Place{true, x, y});
	}
	return road;
}

std::string ReservedText(const std::vector<uint8_t> &tiles)
{
	std::string text;
	size_t i = 0;
	while (i < tiles.size()) {
		if (!tiles[i]) {
			++i;
			continue;
		}
		size_t end = i;
		while (end < tiles.size() && tiles[end]) {
			++end;
		}
		text += (text.empty() ? "" : ",") + std::to_string(i) + "," + std::to_string(end - i);
		i = end;
	}
	return text;
}

std::vector<uint8_t> ReservedFromText(const std::string &text, size_t size)
{
	std::vector<uint8_t> tiles(size, 0);
	std::vector<long> numbers;
	long value = 0;
	bool inNumber = false;
	for (const char c : text) {
		if (c >= '0' && c <= '9') {
			value = value * 10 + (c - '0');
			inNumber = true;
		} else {
			if (inNumber) {
				numbers.push_back(value);
			}
			value = 0;
			inNumber = false;
		}
	}
	if (inNumber) {
		numbers.push_back(value);
	}
	for (size_t n = 0; n + 1 < numbers.size(); n += 2) {
		for (long t = numbers[n]; t < numbers[n] + numbers[n + 1] && t < static_cast<long>(size); ++t) {
			tiles[static_cast<size_t>(t)] = 1;
		}
	}
	return tiles;
}

} // namespace wc2ai
