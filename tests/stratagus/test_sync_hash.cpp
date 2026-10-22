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
/**@name test_sync_hash.cpp - The unit part of the network sync hash. */
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

#include <doctest.h>

#include "stratagus.h"

#include "action/action_attack.h"
#include "action/action_move.h"
#include "action/action_patrol.h"
#include "actions.h"
#include "map.h"
#include "player.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"

#include <functional>
#include <set>
#include <vector>

namespace
{
SyncUnitState SampleState()
{
	SyncUnitState s;
	s.hasOrder = true;
	s.action = static_cast<int>(UnitAction::Attack);
	s.refs = 3;
	s.tileX = 12;
	s.tileY = 40;
	s.hp = 55;
	s.goalX = 20;
	s.goalY = 41;
	s.goalSlot = 7;
	s.range = 4;
	s.pointX = 30;
	s.pointY = 44;
	s.goalPulled = 1;
	return s;
}

unsigned HashOf(const std::vector<SyncUnitState> &world, unsigned start = 0x1234u)
{
	unsigned hash = start;
	for (const SyncUnitState &s : world) {
		hash = SyncHashMixUnit(hash, s);
	}
	return hash;
}

using FieldChange = std::function<void(SyncUnitState &)>;

const std::vector<FieldChange> &EveryFieldChange()
{
	static const std::vector<FieldChange> changes = {
		[](SyncUnitState &s) { s.hasOrder = false; },
		[](SyncUnitState &s) { s.action = static_cast<int>(UnitAction::Move); },
		[](SyncUnitState &s) { s.refs += 1; },
		[](SyncUnitState &s) { s.tileX += 1; },
		[](SyncUnitState &s) { s.tileY -= 1; },
		[](SyncUnitState &s) { s.hp -= 1; },
		[](SyncUnitState &s) { s.goalX += 1; },
		[](SyncUnitState &s) { s.goalY += 1; },
		[](SyncUnitState &s) { s.goalSlot = -1; },
		[](SyncUnitState &s) { s.range += 1; },
		[](SyncUnitState &s) { s.pointX += 1; },
		[](SyncUnitState &s) { s.pointY += 1; },
		[](SyncUnitState &s) { s.goalPulled = 0; },
	};
	return changes;
}
} // namespace

TEST_CASE("Sync hash: two identical worlds give the same hash")
{
	const std::vector<SyncUnitState> a = {SampleState(), SyncUnitState{}, SampleState()};
	const std::vector<SyncUnitState> b = a;
	CHECK(HashOf(a) == HashOf(b));
	CHECK(SyncHashMixUnit(0, SampleState()) == SyncHashMixUnit(0, SampleState()));
}

TEST_CASE("Sync hash: a change of any one covered field changes the hash")
{
	const std::vector<SyncUnitState> base = {SampleState(), SampleState(), SampleState()};
	const unsigned baseHash = HashOf(base);
	for (size_t unit = 0; unit != base.size(); ++unit) {
		for (size_t i = 0; i != EveryFieldChange().size(); ++i) {
			std::vector<SyncUnitState> changed = base;
			EveryFieldChange()[i](changed[unit]);
			CAPTURE(unit);
			CAPTURE(i);
			CHECK(HashOf(changed) != baseHash);
		}
	}
	// Every field change gives its own value (no two fields alias each other here).
	std::set<unsigned> seen;
	for (const FieldChange &change : EveryFieldChange()) {
		SyncUnitState s = SampleState();
		change(s);
		seen.insert(SyncHashMixUnit(0, s));
	}
	CHECK(seen.size() == EveryFieldChange().size());
}

TEST_CASE("Sync hash: the unit order and the start value count")
{
	SyncUnitState first = SampleState();
	SyncUnitState second = SampleState();
	second.tileX = 13;
	CHECK(HashOf({first, second}) != HashOf({second, first}));
	CHECK(HashOf({first}, 1u) != HashOf({first}, 2u));
}

TEST_CASE("Sync hash: the engine's terms come first, unchanged")
{
	// The old per-unit hash was rotl5, ^= action << 18, ^= refs << 3. The new words follow it.
	SyncUnitState s = SampleState();
	unsigned legacy = 0xDEADBEEFu;
	legacy = (legacy << 5) | (legacy >> 27);
	legacy ^= static_cast<unsigned>(s.action) << 18;
	legacy ^= static_cast<unsigned>(s.refs) << 3;
	unsigned expected = legacy;
	for (int word : {s.tileX, s.tileY, s.hp, s.goalX, s.goalY, s.goalSlot, s.range, s.pointX, s.pointY,
	                 s.goalPulled}) {
		expected = (expected ^ static_cast<unsigned>(word)) * 16777619u;
	}
	CHECK(SyncHashMixUnit(0xDEADBEEFu, s) == expected);
}

namespace
{
class SyncWorld
{
public:
	SyncWorld()
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		if (UnitManager == nullptr) {
			UnitManager = new CUnitManager;
		}
		UnitManager->Init();
		UnitTypeVar.Init();
		Map.Info.MapWidth = 32;
		Map.Info.MapHeight = 32;
		Map.Create();
		for (int y = 0; y != 32; ++y) {
			for (int x = 0; x != 32; ++x) {
				Map.Field(x, y)->Flags = MapFieldLandAllowed;
			}
		}
		for (int i = 0; i < 2; ++i) {
			Players[i].Clear();
			Players[i].Index = i;
			Players[i].Type = PlayerTypes::PlayerPerson;
			Players[i].Team = i;
			Players[i].UnitLimit = 100;
			Players[i].BuildingLimit = 100;
			Players[i].TotalUnitLimit = 100;
		}
		NumPlayers = 2;
		ThisPlayer = &Players[1];
		Players[0].SetDiplomacyEnemyWith(Players[1]);
		Players[1].SetDiplomacyEnemyWith(Players[0]);

		type.Ident = "unit-test-sync";
		type.Name = "unit-test-sync";
		type.Slot = 0;
		type.TileWidth = 1;
		type.TileHeight = 1;
		type.NumDirections = 8;
		type.MoveType = EMovement::Land;
		type.MovementMask = MapFieldLandUnit | MapFieldBuilding | MapFieldWaterAllowed | MapFieldUnpassable;
		type.FieldFlags = MapFieldLandUnit;
		type.CanAttack = true;
		type.CanTarget = ECanTargetFlag::Land;
		type.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		type.DefaultStat.Variables.resize(UnitTypeVar.GetNumberVariable());
		type.DefaultStat.Variables[HP_INDEX].Max = 60;
		type.DefaultStat.Variables[HP_INDEX].Value = 60;
		type.DefaultStat.Variables[HP_INDEX].Enable = 1;
		type.DefaultStat.Variables[ATTACKRANGE_INDEX].Max = 1;
		type.DefaultStat.Variables[ATTACKRANGE_INDEX].Value = 1;
		type.MapDefaultStat = type.DefaultStat;
		for (int i = 0; i < PlayerMax; ++i) {
			type.Stats[i] = type.DefaultStat;
		}
	}

	~SyncWorld()
	{
		for (int i = 0; i < 2; ++i) {
			Players[i].Clear();
		}
		UnitManager->Init();
		Map.Fields.clear();
		Map.Info.MapWidth = 0;
		Map.Info.MapHeight = 0;
		NumPlayers = savedNumPlayers;
		ThisPlayer = savedThisPlayer;
	}

	CUnit &Place(const Vec2i &pos, int player)
	{
		CUnit *unit = MakeUnitAndPlace(pos, type, &Players[player]);
		REQUIRE(unit != nullptr);
		return *unit;
	}

	static void Order(CUnit &unit, std::unique_ptr<COrder> order)
	{
		unit.Orders.clear();
		unit.Orders.push_back(std::move(order));
	}

	CUnitType type;

private:
	CPlayer *savedThisPlayer = nullptr;
	int savedNumPlayers = 0;
};
} // namespace

TEST_CASE("Sync hash: a unit's state is read from the unit and its current order")
{
	SyncWorld world;
	CUnit &unit = world.Place(Vec2i(5, 6), 0);
	CUnit &enemy = world.Place(Vec2i(9, 6), 1);

	SUBCASE("tile, HP, refs")
	{
		unit.Variable[HP_INDEX].Value = 41;
		const SyncUnitState s = SyncUnitStateOf(unit);
		CHECK(s.tileX == 5);
		CHECK(s.tileY == 6);
		CHECK(s.hp == 41);
		CHECK(s.refs == static_cast<int>(unit.Refs));
	}
	SUBCASE("move: goal tile, range, goal-pulled")
	{
		SyncWorld::Order(unit, COrder::NewActionMove(Vec2i(20, 21)));
		const SyncUnitState s = SyncUnitStateOf(unit);
		CHECK(s.hasOrder);
		CHECK(s.action == static_cast<int>(UnitAction::Move));
		CHECK(s.goalX == 20);
		CHECK(s.goalY == 21);
		CHECK(s.goalSlot == -1);
		const auto &move = static_cast<const COrder_Move &>(*unit.Orders[0]);
		CHECK(s.range == move.GetRange());
		CHECK(s.goalPulled == (move.GetGoalPulled() ? 1 : 0));
		CHECK(s.pointX == -1);
	}
	SUBCASE("attack on a unit: goal slot and range")
	{
		SyncWorld::Order(unit, COrder::NewActionAttack(unit, enemy));
		const SyncUnitState s = SyncUnitStateOf(unit);
		const auto &attack = static_cast<const COrder_Attack &>(*unit.Orders[0]);
		CHECK(s.action == static_cast<int>(UnitAction::Attack));
		CHECK(s.goalSlot == UnitNumber(enemy));
		CHECK(s.goalX == attack.GetGoalPos().x);
		CHECK(s.range == attack.GetRange());
		CHECK(s.pointX == attack.GetAttackMovePos().x);
	}
	SUBCASE("attack-move to a point: the ordered point")
	{
		SyncWorld::Order(unit, COrder::NewActionAttack(unit, Vec2i(25, 26)));
		const SyncUnitState s = SyncUnitStateOf(unit);
		CHECK(s.pointX == 25);
		CHECK(s.pointY == 26);
		CHECK(s.goalSlot == -1);
	}
	SUBCASE("patrol: way point and goal")
	{
		SyncWorld::Order(unit, COrder::NewActionPatrol(unit.tilePos, Vec2i(15, 6)));
		const SyncUnitState s = SyncUnitStateOf(unit);
		const auto &patrol = static_cast<const COrder_Patrol &>(*unit.Orders[0]);
		CHECK(s.action == static_cast<int>(UnitAction::Patrol));
		CHECK(s.pointX == patrol.GetWayPoint().x);
		CHECK(s.pointY == patrol.GetWayPoint().y);
		CHECK(s.goalX == patrol.GetGoalPos().x);
	}
	SUBCASE("no order")
	{
		unit.Orders.clear();
		const SyncUnitState s = SyncUnitStateOf(unit);
		CHECK_FALSE(s.hasOrder);
		CHECK(s.goalX == -1);
		CHECK(s.goalSlot == -1);
	}
	SUBCASE("two units in the same state hash alike; a moved unit does not")
	{
		SyncWorld::Order(unit, COrder::NewActionMove(Vec2i(20, 21)));
		const unsigned before = SyncHashMixUnit(0, SyncUnitStateOf(unit));
		CHECK(SyncHashMixUnit(0, SyncUnitStateOf(unit)) == before);
		SyncWorld::Order(unit, COrder::NewActionMove(Vec2i(20, 22)));
		CHECK(SyncHashMixUnit(0, SyncUnitStateOf(unit)) != before);
	}
}
