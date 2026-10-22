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
/**@name test_rescue_55.cpp - Rescue only by touching (ORIG55 item 6). */
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

#include "map.h"
#include "pathfinder.h"
#include "player.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"

// Live run 55, item 6 (ORIG55.md): BDP Human I, player 0 rescue-passive, a Knight of player 1.
// Peasants 1554 at 81,25 and 1559 at 79,21; Farm 1556 (2 x 2) at 75..76, 23..24. Distance 2 held
// 96 - 2,500 frames: no rescue (4 cases); a touching tile: owner 0 -> 1 within 1 - 3 frames (3 of 3);
// only the touched unit changes owner (3 of 3).

namespace
{
constexpr tile_flags LandMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding | MapFieldCoastAllowed
                                | MapFieldWaterAllowed | MapFieldUnpassable;

class RescueWorld
{
public:
	RescueWorld()
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		if (UnitManager == nullptr) {
			UnitManager = new CUnitManager;
		}
		UnitManager->Init();
		UnitTypeVar.Init();
		Map.Info.MapWidth = 128;
		Map.Info.MapHeight = 128;
		Map.Create();
		InitPathfinder();
		for (int i = 0; i < PlayerMax; ++i) {
			Players[i].Clear();
			Players[i].Index = i;
			Players[i].Type = PlayerTypes::PlayerNobody;
		}
		for (int i = 0; i < 2; ++i) {
			Players[i].Team = i;
			Players[i].UnitLimit = 100;
			Players[i].BuildingLimit = 100;
			Players[i].TotalUnitLimit = 100;
		}
		Players[0].Type = PlayerTypes::PlayerRescuePassive;
		Players[1].Type = PlayerTypes::PlayerPerson;
		NumPlayers = 2;
		ThisPlayer = &Players[1];
		for (int y = 0; y < 128; ++y) {
			for (int x = 0; x < 128; ++x) {
				Map.Field(x, y)->Flags = MapFieldLandAllowed;
			}
		}
		MakeType(knight, "unit-test-knight", 0, 1, MapFieldLandUnit);
		knight.CanAttack = true;
		MakeType(peasant, "unit-test-peasant", 1, 1, MapFieldLandUnit);
		peasant.CanAttack = true;
		MakeType(farm, "unit-test-farm", 2, 2, MapFieldBuilding);
		farm.Building = true;
		NoRescueCheck = false;
	}

	~RescueWorld()
	{
		for (int i = 0; i < PlayerMax; ++i) {
			Players[i].Clear();
		}
		UnitManager->Init();
		FreePathfinder();
		Map.Fields.clear();
		Map.Info.MapWidth = 0;
		Map.Info.MapHeight = 0;
		NumPlayers = savedNumPlayers;
		ThisPlayer = savedThisPlayer;
		NoRescueCheck = false;
	}

	CUnit &Place(const CUnitType &type, const Vec2i &pos, int player)
	{
		CUnit *unit = MakeUnitAndPlace(pos, type, &Players[player]);
		REQUIRE(unit != nullptr);
		return *unit;
	}

	/// The rescue check of `cycles` game cycles (mainloop.cpp runs it on every cycle).
	void Cycles(int cycles)
	{
		for (int i = 0; i != cycles; ++i) {
			RescueUnits();
		}
	}

	CUnitType knight;
	CUnitType peasant;
	CUnitType farm;

private:
	void MakeType(CUnitType &type, const char *ident, int slot, int size, tile_flags fieldFlags)
	{
		type.Ident = ident;
		type.Name = ident;
		type.Slot = slot;
		type.TileWidth = size;
		type.TileHeight = size;
		type.GridStep = 1;
		type.NumDirections = 8;
		type.MoveType = EMovement::Land;
		type.MovementMask = LandMask;
		type.FieldFlags = fieldFlags;
		type.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		type.DefaultStat.Variables.resize(UnitTypeVar.GetNumberVariable());
		type.DefaultStat.Variables[HP_INDEX].Max = 60;
		type.DefaultStat.Variables[HP_INDEX].Value = 60;
		type.DefaultStat.Variables[HP_INDEX].Enable = 1;
		type.MapDefaultStat = type.DefaultStat;
		for (int i = 0; i < PlayerMax; ++i) {
			type.Stats[i] = type.DefaultStat;
		}
	}

	CPlayer *savedThisPlayer = nullptr;
	int savedNumPlayers = 0;
};
} // namespace

TEST_CASE("rescue, run 55: distance 2 never rescues, a touching tile does at once, only the touched unit")
{
	RescueWorld w;
	CUnit &p1554 = w.Place(w.peasant, {81, 25}, 0);
	CUnit &p1559 = w.Place(w.peasant, {79, 21}, 0);
	CUnit &farm = w.Place(w.farm, {75, 23}, 0);
	CUnit &knight = w.Place(w.knight, {81, 27}, 1);

	// a: 2 tiles straight below Peasant 1554 (live: 96 frames held, no rescue)
	w.Cycles(100);
	CHECK(p1554.Player == &Players[0]);

	// b: one diagonal step to 82,26 -> taken over on the next check (live: the next frame)
	knight.MoveToXY({82, 26});
	w.Cycles(1);
	CHECK(p1554.Player == &Players[1]);
	CHECK(p1554.RescuedFrom == &Players[0]);
	// only the touched unit: the other Peasant and the Farm stay with the rescue slot
	CHECK(p1559.Player == &Players[0]);
	CHECK(farm.Player == &Players[0]);

	// c, d: Peasant 1559 from 79,23 (0, 2) and from 81,23 (2, 2), each held 2,500 cycles
	knight.MoveToXY({79, 23});
	w.Cycles(2500);
	CHECK(p1559.Player == &Players[0]);
	knight.MoveToXY({81, 23});
	w.Cycles(2500);
	CHECK(p1559.Player == &Players[0]);
	// The Peasant taken over at b is a person's unit now and stands 2 from Farm / Peasant: no
	// rescue by it either (it is at 81,25: 1559 is 2 away, the Farm 5 away).
	CHECK(farm.Player == &Players[0]);

	// e: diagonal touch 80,22 -> at once
	knight.MoveToXY({80, 22});
	w.Cycles(1);
	CHECK(p1559.Player == &Players[1]);
	CHECK(farm.Player == &Players[0]);

	// f: 2 tiles east of the Farm's footprint (78,23), held 1,900 cycles: no rescue
	knight.MoveToXY({78, 23});
	w.Cycles(1900);
	CHECK(farm.Player == &Players[0]);

	// g: 77,23 touches the footprint (75..76) -> at once
	knight.MoveToXY({77, 23});
	w.Cycles(1);
	CHECK(farm.Player == &Players[1]);
}

TEST_CASE("rescue, run 55: every tile of the ring round a 2 x 2 footprint touches, the ring beyond does not")
{
	for (int dy = -2; dy <= 3; ++dy) {
		for (int dx = -2; dx <= 3; ++dx) {
			const bool onFootprint = dx >= 0 && dx <= 1 && dy >= 0 && dy <= 1;
			if (onFootprint) {
				continue;
			}
			RescueWorld w;
			CUnit &farm = w.Place(w.farm, {60, 60}, 0);
			w.Place(w.knight, {60 + dx, 60 + dy}, 1);
			w.Cycles(1);
			const bool touching = dx >= -1 && dx <= 2 && dy >= -1 && dy <= 2;
			CAPTURE(dx);
			CAPTURE(dy);
			CHECK((farm.Player == &Players[1]) == touching);
		}
	}
}
