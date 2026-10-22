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
/**@name test_release_selection.cpp - A destroyed unit never stays selected. */
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

#include "actions.h"
#include "interface.h"
#include "map.h"
#include "pathfinder.h"
#include "player.h"
#include "ui.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"

#include <algorithm>

// A worker that goes into a mine or a depot keeps its selection (DeselectInMine off,
// action_resource.cpp). When the building it is in dies, DestroyAllInside released the worker
// without taking it out of `Selected`; its orders were cleared and the next info panel draw read
// Orders[0]->Action through a null order (SIGSEGV at 0x10, round 3.4.64).

namespace
{
class ReleaseWorld
{
public:
	ReleaseWorld()
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		savedViewport = UI.SelectedViewport;
		savedMaxSelectable = MaxSelectable;
		MaxSelectable = 9;
		if (UnitManager == nullptr) {
			UnitManager = new CUnitManager;
		}
		UnitManager->Init();
		UnitTypeVar.Init();
		Map.Info.MapWidth = 32;
		Map.Info.MapHeight = 32;
		Map.Create();
		for (int y = 0; y < 32; ++y) {
			for (int x = 0; x < 32; ++x) {
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

		MakeType(peasant, "unit-test-peasant", 0, MapFieldLandUnit);
		MakeType(hall, "unit-test-hall", 1, MapFieldBuilding);
		hall.Building = true;
		hall.TileWidth = 3;
		hall.TileHeight = 3;
		MakeType(mine, "unit-test-mine", 2, MapFieldBuilding);
		mine.Building = true;
		mine.TileWidth = 3;
		mine.TileHeight = 3;
	}

	~ReleaseWorld()
	{
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
		MaxSelectable = savedMaxSelectable;
	}

	CUnit &Place(const CUnitType &type, const Vec2i &pos)
	{
		CUnit *unit = MakeUnitAndPlace(pos, type, &Players[0]);
		REQUIRE(unit != nullptr);
		return *unit;
	}

	/// What COrder_Resource does when a selected worker goes into a building
	/// (action_resource.cpp, the two `unit.Remove(goal)` places with DeselectInMine off).
	static void EnterKeepingSelection(CUnit &worker, CUnit &building)
	{
		const int selected = worker.Selected;
		worker.Remove(&building);
		if (selected) {
			worker.Removed = 0;
			SelectUnit(worker);
			SelectionChanged();
			worker.Removed = 1;
		}
	}

	CUnitType peasant;
	CUnitType hall;
	CUnitType mine;

private:
	void MakeType(CUnitType &type, const char *ident, int slot, tile_flags fieldFlags)
	{
		type.Ident = ident;
		type.Name = ident;
		type.Slot = slot;
		type.TileWidth = 1;
		type.TileHeight = 1;
		type.NumDirections = 8;
		type.MoveType = EMovement::Land;
		type.MovementMask = MapFieldLandUnit | MapFieldBuilding | MapFieldUnpassable;
		type.FieldFlags = fieldFlags;
		type.Animations = nullptr;
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
	CViewport *savedViewport = nullptr;
	unsigned int savedMaxSelectable = 0;
};

bool InSelection(const CUnit &unit)
{
	return std::find(Selected.begin(), Selected.end(), &unit) != Selected.end();
}
} // namespace

TEST_CASE("release: a selected worker inside a depot that dies leaves the selection (INFOPANEL-NULL-ORDER-CRASH)")
{
	ReleaseWorld world;
	CUnit &hall = world.Place(world.hall, Vec2i(10, 10));
	CUnit &worker = world.Place(world.peasant, Vec2i(14, 10));

	SelectSingleUnit(worker);
	ReleaseWorld::EnterKeepingSelection(worker, hall);
	// the data condition of the crash: removed, inside, and still selected
	REQUIRE(bool(worker.Removed));
	REQUIRE(worker.Container == &hall);
	REQUIRE(bool(worker.Selected));
	REQUIRE(Selected.size() == 1);

	LetUnitDie(hall);

	CHECK(bool(worker.Destroyed));
	CHECK_FALSE(bool(worker.Selected));
	CHECK_FALSE(InSelection(worker));
	CHECK(Selected.empty());
	// every unit the info panel can still be asked to draw has an order to read
	for (const CUnit *unit : Selected) {
		REQUIRE_FALSE(unit->Orders.empty());
		CHECK(unit->CurrentOrder() != nullptr);
	}
}

TEST_CASE("release: one worker of a group dies inside a depot - the others stay selected with their orders")
{
	ReleaseWorld world;
	CUnit &hall = world.Place(world.hall, Vec2i(10, 10));
	CUnit &inside = world.Place(world.peasant, Vec2i(14, 10));
	CUnit &outside = world.Place(world.peasant, Vec2i(16, 10));

	SelectSingleUnit(inside);
	REQUIRE(SelectUnit(outside));
	ReleaseWorld::EnterKeepingSelection(inside, hall);
	REQUIRE(Selected.size() == 2);

	LetUnitDie(hall);

	CHECK_FALSE(InSelection(inside));
	REQUIRE(Selected.size() == 1);
	CHECK(Selected[0] == &outside);
	CHECK(outside.CurrentOrder() != nullptr);
}

TEST_CASE("release: a selected worker inside a mine killed directly (a trigger's KillUnitAt) leaves the selection")
{
	ReleaseWorld world;
	CUnit &mine = world.Place(world.mine, Vec2i(10, 10));
	CUnit &worker = world.Place(world.peasant, Vec2i(14, 10));

	SelectSingleUnit(worker);
	ReleaseWorld::EnterKeepingSelection(worker, mine);
	REQUIRE(bool(worker.Selected));

	// LetUnitDie's "removed units, just remove" path: clear orders + release, no Remove
	LetUnitDie(worker);

	CHECK(bool(worker.Destroyed));
	CHECK_FALSE(bool(worker.Selected));
	CHECK(Selected.empty());
	CHECK(mine.InsideUnits.empty());
}

TEST_CASE("release: a worker in a mine whose depot dies is not touched")
{
	ReleaseWorld world;
	CUnit &hall = world.Place(world.hall, Vec2i(4, 4));
	CUnit &mine = world.Place(world.mine, Vec2i(20, 20));
	CUnit &worker = world.Place(world.peasant, Vec2i(24, 20));

	SelectSingleUnit(worker);
	ReleaseWorld::EnterKeepingSelection(worker, mine);

	LetUnitDie(hall);

	CHECK_FALSE(bool(worker.Destroyed));
	CHECK(bool(worker.Selected));
	REQUIRE(Selected.size() == 1);
	REQUIRE_FALSE(worker.Orders.empty());
	CHECK(worker.CurrentOrder() != nullptr);
}

TEST_CASE("release: the unit under the cursor is forgotten when it is destroyed inside a building")
{
	ReleaseWorld world;
	CUnit &hall = world.Place(world.hall, Vec2i(10, 10));
	CUnit &worker = world.Place(world.peasant, Vec2i(14, 10));
	worker.Remove(&hall);
	UnitUnderCursor = &worker; // as a stale hover could leave it

	LetUnitDie(hall);

	CHECK(UnitUnderCursor == nullptr);
}

// ---------------------------------------------------------------------------
// SELECTION-CRASH (S20+ 3.4.83): the Lua Selection() call - the save game's selection and the
// bridge harness - pushed units into `Selected` without their Selected flag. LetUnitDie /
// Remove / Release unselect only a flagged unit, so a dead unit stayed selected and the button
// panel read its released orders (SIGSEGV 0x10 in CUnit::CurrentAction).

#include "iolib.h"
#include "script.h"

namespace
{
struct SelectionLua
{
	SelectionLua()
	{
		InitLua();
		REQUIRE(Lua != nullptr);
		SelectionCclRegister();
	}
	~SelectionLua()
	{
		if (Lua) {
			lua_close(Lua);
			Lua = nullptr;
		}
	}
	SelectionLua(const SelectionLua &) = delete;

	static void Run(const std::string &text)
	{
		REQUIRE(luaL_loadbuffer(Lua, text.data(), text.size(), "selection") == 0);
		REQUIRE(lua_pcall(Lua, 0, 0, 0) == 0);
	}
};

std::string SelectionLine(std::initializer_list<const CUnit *> units)
{
	std::string text = "Selection(0, {";
	for (const CUnit *unit : units) {
		text += "\"" + UnitReference(*unit) + "\", ";
	}
	return text + "})\n";
}
} // namespace

TEST_CASE("selection: units selected by the Lua Selection() call are marked selected")
{
	ReleaseWorld world;
	SelectionLua lua;
	CUnit &a = world.Place(world.peasant, Vec2i(4, 4));
	CUnit &b = world.Place(world.peasant, Vec2i(6, 4));

	SelectionLua::Run(SelectionLine({&a, &b, &a}));

	REQUIRE(Selected.size() == 2); // listed twice -> once
	CHECK(Selected[0] == &a);
	CHECK(Selected[1] == &b);
	CHECK(bool(a.Selected));
	CHECK(bool(b.Selected));

	// a second call replaces the selection, it does not append
	SelectionLua::Run(SelectionLine({&b}));
	REQUIRE(Selected.size() == 1);
	CHECK(Selected[0] == &b);
	CHECK_FALSE(bool(a.Selected));
}

TEST_CASE("selection: a unit selected by Lua that dies leaves the selection; the panel reads only live units")
{
	ReleaseWorld world;
	SelectionLua lua;
	CUnit &dies = world.Place(world.peasant, Vec2i(4, 4));
	CUnit &lives = world.Place(world.peasant, Vec2i(6, 4));

	SelectionLua::Run(SelectionLine({&dies, &lives}));
	REQUIRE(Selected.size() == 2);

	LetUnitDie(dies);
	CHECK_FALSE(InSelection(dies)); // the dying unit leaves at once
	UnitClearOrders(dies); // later the corpse is released (the moment the panel read a null order)
	dies.Release();

	CHECK_FALSE(InSelection(dies));
	CHECK_FALSE(bool(dies.Selected));
	REQUIRE(Selected.size() == 1);
	CHECK(Selected[0] == &lives);
	for (const CUnit *unit : Selected) {
		REQUIRE_FALSE(unit->Orders.empty());
		CHECK(unit->CurrentOrder() != nullptr);
	}
	CHECK(DropUnreadableSelectedUnits() == 0);
}

TEST_CASE("selection: a dead or unknown unit in a Selection() list is not selected")
{
	ReleaseWorld world;
	SelectionLua lua;
	CUnit &dead = world.Place(world.peasant, Vec2i(4, 4));
	CUnit &alive = world.Place(world.peasant, Vec2i(6, 4));
	LetUnitDie(dead);

	SelectionLua::Run("Selection(0, {\"" + UnitReference(dead) + "\", \"UFFFF\", \""
	                  + UnitReference(alive) + "\"})\n");

	REQUIRE(Selected.size() == 1);
	CHECK(Selected[0] == &alive);
	CHECK_FALSE(bool(dead.Selected));
}

TEST_CASE("selection: save / load round trip - the loaded selection leaves on death")
{
	ReleaseWorld world;
	SelectionLua lua;
	CUnit &a = world.Place(world.peasant, Vec2i(4, 4));
	CUnit &b = world.Place(world.peasant, Vec2i(6, 4));
	SelectSingleUnit(a);
	REQUIRE(SelectUnit(b));

	CFile mem;
	REQUIRE(mem.open("", CL_WRITE_MEMORY | CL_OPEN_WRITE) == 0);
	SaveSelections(mem);
	const std::string text = mem.TakeText();
	CHECK(mem.close() == 0);
	CHECK(text.find("Selection(2, {\"" + UnitReference(a) + "\", \"" + UnitReference(b) + "\", })")
	      != std::string::npos);

	SUBCASE("the units' own lines carried the flag (Unit(..., {\"selected\"}))")
	{
		CleanSelections();
		a.Selected = 1;
		b.Selected = 1;
	}
	SUBCASE("the units' own lines did not carry the flag")
	{
		CleanSelections();
		a.Selected = 0;
		b.Selected = 0;
	}
	SelectionLua::Run(text);

	REQUIRE(Selected.size() == 2);
	CHECK(Selected[0] == &a);
	CHECK(Selected[1] == &b);
	CHECK(bool(a.Selected));
	CHECK(bool(b.Selected));

	LetUnitDie(b);

	CHECK_FALSE(InSelection(b));
	REQUIRE(Selected.size() == 1);
	CHECK(Selected[0] == &a);
	CHECK(a.CurrentOrder() != nullptr);
}

TEST_CASE("selection: the panel guard drops a released unit that a missed path left selected")
{
	ReleaseWorld world;
	CUnit &a = world.Place(world.peasant, Vec2i(4, 4));
	CUnit &b = world.Place(world.peasant, Vec2i(6, 4));
	CUnit &c = world.Place(world.peasant, Vec2i(8, 4));
	SelectSingleUnit(a);
	REQUIRE(SelectUnit(c));
	LetUnitDie(b);
	UnitClearOrders(b); // what the die action / DestroyAllInside do before the release
	b.Release();
	REQUIRE(bool(b.Destroyed));
	REQUIRE(b.Orders.empty());
	// the data of the crash: a destroyed unit in the list (as the old Selection() left it)
	Selected.insert(Selected.begin() + 1, &b);

	CHECK(DropUnreadableSelectedUnits() == 1);

	CHECK_FALSE(InSelection(b));
	REQUIRE(Selected.size() == 2);
	CHECK(Selected[0] == &a);
	CHECK(Selected[1] == &c);
	CHECK(DropUnreadableSelectedUnits() == 0);

	// a list of only released units: the panel update runs the guard first and reads nothing
	Selected.assign({&b});
	UI.ButtonPanel.Update();
	CHECK(Selected.empty());
	CHECK(CurrentButtons.empty());
}
