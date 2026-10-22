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
/**@name test_slot_bounds.cpp - A unit number from a script that names no unit never crashes. */
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

#include "interface.h"
#include "map.h"
#include "pathfinder.h"
#include "player.h"
#include "script.h"
#include "ui.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"

#include <string>

// GetUnitVariable(300, "HitPoints") in a game with fewer unit slots read past the slot table
// (CUnitManager::GetSlotUnit had no bounds check) and ended the game with SIGSEGV. A script
// number that names no slot is now a Lua error; a released slot reads and writes as nil.

namespace
{
class SlotWorld
{
public:
	SlotWorld()
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		savedViewport = UI.SelectedViewport;
		if (UnitManager == nullptr) {
			UnitManager = new CUnitManager;
		}
		UnitManager->Init();
		UnitTypeVar.Init();
		Map.Info.MapWidth = 16;
		Map.Info.MapHeight = 16;
		Map.Create();
		for (int y = 0; y < 16; ++y) {
			for (int x = 0; x < 16; ++x) {
				Map.Field(x, y)->Flags = MapFieldLandAllowed;
			}
		}
		InitPathfinder();
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
		ThisPlayer = &Players[0];
		UI.SelectedViewport = UI.Viewports;
		Selected.clear();
		UnitUnderCursor = nullptr;

		footman.Ident = "unit-test-footman";
		footman.Name = "unit-test-footman";
		footman.Slot = 0;
		footman.TileWidth = 1;
		footman.TileHeight = 1;
		footman.NumDirections = 8;
		footman.MoveType = EMovement::Land;
		footman.MovementMask = MapFieldLandUnit | MapFieldBuilding | MapFieldUnpassable;
		footman.FieldFlags = MapFieldLandUnit;
		footman.Animations = nullptr;
		footman.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		footman.DefaultStat.Variables.resize(UnitTypeVar.GetNumberVariable());
		footman.DefaultStat.Variables[HP_INDEX].Max = 60;
		footman.DefaultStat.Variables[HP_INDEX].Value = 60;
		footman.DefaultStat.Variables[HP_INDEX].Enable = 1;
		footman.MapDefaultStat = footman.DefaultStat;
		for (int i = 0; i < PlayerMax; ++i) {
			footman.Stats[i] = footman.DefaultStat;
		}

		InitLua();
		UnitCclRegister();
	}

	~SlotWorld()
	{
		lua_close(Lua);
		Lua = nullptr;
		Selected.clear();
		UnitUnderCursor = nullptr;
		for (int i = 0; i < 2; ++i) {
			Players[i].Clear();
		}
		UnitManager->Init();
		FreePathfinder();
		Map.Fields.clear();
		Map.Info.MapWidth = 0;
		Map.Info.MapHeight = 0;
		NumPlayers = savedNumPlayers;
		ThisPlayer = savedThisPlayer;
		UI.SelectedViewport = savedViewport;
	}

	CUnit &Place(const Vec2i &pos)
	{
		CUnit *unit = MakeUnitAndPlace(pos, footman, &Players[0]);
		REQUIRE(unit != nullptr);
		return *unit;
	}

	/// Run a chunk; true when it ran without an error that escaped it.
	static bool Run(const std::string &chunk)
	{
		if (luaL_loadstring(Lua, chunk.c_str()) != 0) {
			lua_pop(Lua, 1);
			return false;
		}
		const bool ok = lua_pcall(Lua, 0, 0, 0) == 0;
		if (!ok) {
			lua_pop(Lua, 1);
		}
		return ok;
	}

	static bool GlobalIsNil(const char *name)
	{
		lua_getglobal(Lua, name);
		const bool isNil = lua_isnil(Lua, -1);
		lua_pop(Lua, 1);
		return isNil;
	}

	static bool GlobalBool(const char *name)
	{
		lua_getglobal(Lua, name);
		const bool value = lua_toboolean(Lua, -1) != 0;
		lua_pop(Lua, 1);
		return value;
	}

	static double GlobalNumber(const char *name)
	{
		lua_getglobal(Lua, name);
		const double value = lua_tonumber(Lua, -1);
		lua_pop(Lua, 1);
		return value;
	}

	static std::string GlobalString(const char *name)
	{
		lua_getglobal(Lua, name);
		const char *s = lua_tostring(Lua, -1);
		std::string value = s ? s : "";
		lua_pop(Lua, 1);
		return value;
	}

	CUnitType footman;

private:
	CPlayer *savedThisPlayer = nullptr;
	int savedNumPlayers = 0;
	CViewport *savedViewport = nullptr;
};
} // namespace

TEST_CASE("slot bounds: FindSlotUnit gives nullptr outside the slot table, GetSlotUnit the unit inside it")
{
	SlotWorld world;
	CUnit &unit = world.Place(Vec2i(4, 4));
	const int id = UnitNumber(unit);
	REQUIRE(id >= 0);
	REQUIRE(static_cast<unsigned int>(id) < UnitManager->GetUsedSlotCount());

	CHECK(UnitManager->IsSlotIndex(id));
	CHECK(UnitManager->FindSlotUnit(id) == &unit);
	CHECK(&UnitManager->GetSlotUnit(id) == &unit);

	const int count = static_cast<int>(UnitManager->GetUsedSlotCount());
	CHECK_FALSE(UnitManager->IsSlotIndex(count));
	CHECK_FALSE(UnitManager->IsSlotIndex(-1));
	CHECK(UnitManager->FindSlotUnit(count) == nullptr);
	CHECK(UnitManager->FindSlotUnit(300) == nullptr);
	CHECK(UnitManager->FindSlotUnit(-1) == nullptr);
	CHECK(UnitManager->FindSlotUnit(-12345) == nullptr);
}

TEST_CASE("slot bounds: GetUnitVariable / SetUnitVariable with a number that names no slot are a Lua error, no crash")
{
	SlotWorld world;
	world.Place(Vec2i(4, 4));

	// the bridge's probe of the 3.4.64 round: pcall(GetUnitVariable, 300, "HitPoints")
	REQUIRE(SlotWorld::Run(R"(
		ok_get, err_get = pcall(GetUnitVariable, 300, "HitPoints")
		ok_set, err_set = pcall(SetUnitVariable, 300, "HitPoints", 5)
		ok_neg, err_neg = pcall(GetUnitVariable, -7, "HitPoints")
		ok_flag, err_flag = pcall(GetUnitBoolFlag, 300, "Building")
		ok_dmg, err_dmg = pcall(DamageUnit, 300, 0, 5)
		ok_around, err_around = pcall(GetUnitsAroundUnit, 300, 2)
	)"));
	CHECK_FALSE(SlotWorld::GlobalBool("ok_get"));
	CHECK(SlotWorld::GlobalString("err_get").find("no unit with the number 300") != std::string::npos);
	CHECK_FALSE(SlotWorld::GlobalBool("ok_set"));
	CHECK(SlotWorld::GlobalString("err_set").find("no unit with the number 300") != std::string::npos);
	CHECK_FALSE(SlotWorld::GlobalBool("ok_neg"));
	CHECK_FALSE(SlotWorld::GlobalBool("ok_flag"));
	CHECK_FALSE(SlotWorld::GlobalBool("ok_dmg"));
	CHECK_FALSE(SlotWorld::GlobalBool("ok_around"));

	// the binding of UnitManager:GetSlotUnit gives nil for a slot that does not exist
	REQUIRE(SlotWorld::Run("bound = UnitManager:GetSlotUnit(300)"));
	CHECK(SlotWorld::GlobalIsNil("bound"));
}

TEST_CASE("slot bounds: a valid number still reads and writes its unit")
{
	SlotWorld world;
	CUnit &unit = world.Place(Vec2i(4, 4));
	const std::string id = std::to_string(UnitNumber(unit));

	REQUIRE(SlotWorld::Run("hp = GetUnitVariable(" + id + ", \"HitPoints\")"));
	CHECK(SlotWorld::GlobalNumber("hp") == doctest::Approx(60));

	REQUIRE(SlotWorld::Run("set = SetUnitVariable(" + id + ", \"HitPoints\", 40)"));
	CHECK(SlotWorld::GlobalNumber("set") == doctest::Approx(40));
	CHECK(unit.Variable[HP_INDEX].Value == 40);

	REQUIRE(SlotWorld::Run("hp2 = GetUnitVariable(" + id + ", \"HitPoints\")"));
	CHECK(SlotWorld::GlobalNumber("hp2") == doctest::Approx(40));

	REQUIRE(SlotWorld::Run("ident = GetUnitVariable(" + id + ", \"Ident\")"));
	CHECK(SlotWorld::GlobalString("ident") == "unit-test-footman");

	REQUIRE(SlotWorld::Run("bound = UnitManager:GetSlotUnit(" + id + ")"));
	CHECK_FALSE(SlotWorld::GlobalIsNil("bound"));
}

TEST_CASE("slot bounds: a released slot reads and writes as nil, a second RemoveUnit does nothing")
{
	SlotWorld world;
	CUnit &unit = world.Place(Vec2i(4, 4));
	const std::string id = std::to_string(UnitNumber(unit));

	REQUIRE(SlotWorld::Run("RemoveUnit(" + id + ")"));
	REQUIRE(bool(unit.Destroyed));
	REQUIRE(bool(unit.Released));

	REQUIRE(SlotWorld::Run(
		"hp = GetUnitVariable(" + id + ", \"HitPoints\")\n"
		"set = SetUnitVariable(" + id + ", \"HitPoints\", 40)\n"
		"flag = GetUnitBoolFlag(" + id + ", \"Building\")\n"
		"RemoveUnit(" + id + ")\n"
		"SelectSingleUnit(" + id + ")\n"
		"DamageUnit(-1, " + id + ", 5)\n"));
	CHECK(SlotWorld::GlobalIsNil("hp"));
	CHECK(SlotWorld::GlobalIsNil("set"));
	CHECK(SlotWorld::GlobalIsNil("flag"));
	CHECK(Selected.empty());
	CHECK(bool(unit.Released));
}

TEST_CASE("slot bounds: SetUnitVariable(-1) with nothing selected is nil, no crash")
{
	SlotWorld world;
	world.Place(Vec2i(4, 4));
	REQUIRE(Selected.empty());
	REQUIRE(UnitUnderCursor == nullptr);

	REQUIRE(SlotWorld::Run("set = SetUnitVariable(-1, \"HitPoints\", 40)"));
	CHECK(SlotWorld::GlobalIsNil("set"));
}
