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
/**@name test_goal_pull.cpp - A goal on a blocked tile is pulled toward the unit. */
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
#include "animation.h"
#include "animation/animation_move.h"
#include "animation/animation_wait.h"
#include "commands.h"
#include "game.h"
#include "goal_pull.h"
#include "iolib.h"
#include "map.h"
#include "pathfinder.h"
#include "player.h"
#include "script.h"
#include "tileset.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace doctest
{
template <>
struct StringMaker<Vec2i>
{
	static String convert(const Vec2i &v)
	{
		return ("(" + std::to_string(v.x) + "," + std::to_string(v.y) + ")").c_str();
	}
};
} // namespace doctest

// All maps here are synthetic: straight shores and blocks laid so that the line of each live trial of
// run 47 (ToD Human III, ORIG47 section 6) meets free ground where the live map does.

namespace
{
using FreeTest = std::function<bool(const Vec2i &)>;

const FreeTest everywhere = [](const Vec2i &) { return true; };

Vec2i Pull(const Vec2i &unit, const Vec2i &click, const FreeTest &free)
{
	return PullGoalOnLine(unit, click, false, free, everywhere);
}

/// The steps of the line, as many as asked.
std::string Steps(const Vec2i &from, const Vec2i &to, int count)
{
	GoalPullLine line(from, to);
	std::string res;
	for (int i = 0; i != count; ++i) {
		const Vec2i s = line.Next();
		res += "(" + std::to_string(s.x) + "," + std::to_string(s.y) + ")";
	}
	return res;
}
} // namespace

TEST_CASE("goal pull: the line is set up from the unit and stepped from it (0x4B7890 / 0x4B7630)")
{
	// err starts at major >> 1 and minor steps come when it falls to 0 or below.
	CHECK(Steps({19, 19}, {20, 26}, 7) == "(0,1)(0,1)(1,1)(0,1)(0,1)(0,1)(0,1)");
	CHECK(Steps({24, 19}, {17, 18}, 7) == "(-1,0)(-1,0)(-1,-1)(-1,0)(-1,0)(-1,0)(-1,0)");
	// major 1: err starts at 1, not 0
	CHECK(Steps({5, 5}, {6, 6}, 1) == "(1,1)");
	CHECK(Steps({5, 5}, {6, 5}, 1) == "(1,0)");
	// a straight diagonal
	CHECK(Steps({0, 0}, {-3, 3}, 3) == "(-1,1)(-1,1)(-1,1)");
}

TEST_CASE("goal pull: the twelve live goals of run 47 on synthetic shores")
{
	struct Row
	{
		const char *trial;
		Vec2i unit;
		Vec2i click;
		FreeTest free;
		Vec2i goal; /// live +0x84 (ORIG47 6a-6d)
	};
	const std::vector<Row> rows = {
		// land, Attack into a forest edge running north-south (free from x 20 east)
		{"A2", {24, 19}, {17, 18}, [](const Vec2i &p) { return p.x >= 20; }, {19, 18}},
		// land, Attack into a lake whose shore runs east-west (free up to y 22)
		{"A3", {19, 19}, {20, 26}, [](const Vec2i &p) { return p.y <= 22; }, {19, 23}},
		// the same forest point from the south-east: a forest corner (x <= 18 and y <= 19)
		{"D1", {37, 28}, {17, 18}, [](const Vec2i &p) { return !(p.x <= 18 && p.y <= 19); }, {18, 19}},
		// Move deep into a north-east forest (x >= 34 and y <= 7)
		{"B2", {20, 22}, {40, 1}, [](const Vec2i &p) { return !(p.x >= 34 && p.y <= 7); }, {34, 7}},
		// Move into water south of the unit (free up to y 3)
		{"B3", {55, 2}, {50, 20}, [](const Vec2i &p) { return p.y <= 3; }, {54, 4}},
		// passable black ground: unchanged
		{"A1", {30, 9}, {24, 19}, everywhere, {24, 19}},
		{"B1", {23, 8}, {8, 16}, everywhere, {8, 16}},
		// Destroyer, coast and land from y 14 south (water up to y 13)
		{"C1", {42, 12}, {43, 18}, [](const Vec2i &p) { return p.y <= 13; }, {42, 14}},
		{"C2", {42, 12}, {40, 16}, [](const Vec2i &p) { return p.y <= 13; }, {41, 14}},
		// Destroyer, open water behind a headland and back: unchanged
		{"C5", {42, 12}, {21, 25}, everywhere, {21, 25}},
		{"C6", {22, 24}, {46, 16}, everywhere, {46, 16}},
		// Destroyer, the far coast of the enemy island from y 37 south
		{"C7", {46, 16}, {47, 38}, [](const Vec2i &p) { return p.y <= 36; }, {47, 37}},
	};
	for (const Row &row : rows) {
		CAPTURE(row.trial);
		CHECK(Pull(row.unit, row.click, row.free) == row.goal);
	}
	// C3 / C4 are C1 / C2 as Move orders: the same goals.
	CHECK(Pull({42, 12}, {43, 18}, rows[7].free) == Vec2i(42, 14));
	CHECK(Pull({42, 12}, {40, 16}, rows[8].free) == Vec2i(41, 14));
}

TEST_CASE("goal pull: a free click, a click on the unit, a line blocked to the unit")
{
	const FreeTest nothing = [](const Vec2i &) { return false; };
	CHECK(Pull({5, 5}, {9, 7}, everywhere) == Vec2i(9, 7));
	CHECK(Pull({5, 5}, {5, 5}, nothing) == Vec2i(5, 5));
	// nothing free on the line (the unit's own tile is never tested): the click stays the goal
	CHECK(Pull({5, 5}, {12, 9}, nothing) == Vec2i(12, 9));
	// the only free tile is the one next to the unit: the goal is the tile after it on the way back
	const FreeTest nextToUnit = [](const Vec2i &p) { return p == Vec2i(6, 5); };
	CHECK(Pull({5, 5}, {9, 5}, nextToUnit) == Vec2i(7, 5));
}

TEST_CASE("goal pull: the region test - free tiles of another region are walked over (non-flyers)")
{
	// Land 0..9 is the unit's (region 1), water 10..14 (region 0), an island 15..30 (region 2).
	const FreeTest land = [](const Vec2i &p) { return p.x < 10 || p.x > 14; };
	const FreeTest region = [](const Vec2i &p) { return p.x < 10; };
	CHECK(PullGoalOnLine({3, 5}, {20, 5}, true, land, region) == Vec2i(10, 5));
	// a flyer does not ask the region
	CHECK(PullGoalOnLine({3, 5}, {20, 5}, false, land, region) == Vec2i(20, 5));
}

TEST_CASE("goal pull: determinism - the same inputs give the same goal")
{
	const FreeTest forest = [](const Vec2i &p) { return !(p.x >= 34 && p.y <= 7); };
	const Vec2i first = Pull({20, 22}, {40, 1}, forest);
	for (int i = 0; i != 50; ++i) {
		CHECK(Pull({20, 22}, {40, 1}, forest) == first);
	}
	// stepping a line twice from scratch gives the same steps
	CHECK(Steps({20, 22}, {40, 1}, 21) == Steps({20, 22}, {40, 1}, 21));
}

// ---- on the map ---------------------------------------------------------------------------------

namespace
{
constexpr tile_flags LandMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding | MapFieldCoastAllowed
                                | MapFieldWaterAllowed | MapFieldUnpassable;
constexpr tile_flags NavalMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding | MapFieldCoastAllowed
                                 | MapFieldLandAllowed | MapFieldUnpassable;

/// '.' water, '#' ground, 'c' coast, 'T' forest. Rows from the top-left corner given.
void Paint(const Vec2i &topLeft, const std::vector<std::string> &rows)
{
	for (size_t y = 0; y != rows.size(); ++y) {
		for (size_t x = 0; x != rows[y].size(); ++x) {
			CMapField &mf = *Map.Field(topLeft.x + int(x), topLeft.y + int(y));
			switch (rows[y][x]) {
				case '.': mf.Flags = MapFieldWaterAllowed; break;
				case '#': mf.Flags = MapFieldLandAllowed; break;
				case 'c': mf.Flags = MapFieldCoastAllowed; break;
				case 'T': mf.Flags = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable; break;
				default: break;
			}
		}
	}
}

void Fill(const Vec2i &topLeft, const Vec2i &bottomRight, tile_flags flags)
{
	for (int y = topLeft.y; y <= bottomRight.y; ++y) {
		for (int x = topLeft.x; x <= bottomRight.x; ++x) {
			Map.Field(x, y)->Flags = flags;
		}
	}
}

/// A synthetic map with real units on it (no Lua, no graphics): the move animation goes one pixel
/// step of a whole tile per cycle, so an order's Execute walks a tile (a ship two) per cycle.
class PullWorld
{
public:
	explicit PullWorld(int size = 64)
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		savedGameCycle = GameCycle;
		if (UnitManager == nullptr) {
			UnitManager = new CUnitManager;
		}
		UnitManager->Init();
		UnitTypeVar.Init();
		Map.Info.MapWidth = size;
		Map.Info.MapHeight = size;
		Map.Create();
		InitPathfinder();
		AStarKnowUnseenTerrain = true;
		ClearGoalRegionMap();
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
		GameCycle = 1000;

		auto move = std::make_unique<CAnimation_Move>();
		move->Init("32", nullptr);
		auto wait = std::make_unique<CAnimation_Wait>();
		wait->Init("1", nullptr);
		animations.Move.push_back(std::move(move));
		animations.Move.push_back(std::move(wait));
		auto still = std::make_unique<CAnimation_Wait>();
		still->Init("1", nullptr);
		animations.Still.push_back(std::move(still));

		MakeType(footman, "unit-test-footman", 0, EMovement::Land, LandMask, MapFieldLandUnit, 1);
		MakeType(destroyer, "unit-test-destroyer", 1, EMovement::Naval, NavalMask, MapFieldSeaUnit, 2);
		MakeType(farm, "unit-test-farm", 2, EMovement::Land, LandMask, MapFieldBuilding, 1);
		farm.Building = true;
		farm.Animations = nullptr;
		MakeType(hall, "unit-test-hall", 3, EMovement::Land, LandMask, MapFieldBuilding, 1);
		hall.TileWidth = hall.TileHeight = 4;
		hall.Building = true;
		hall.Animations = nullptr;
		MakeType(flyer, "unit-test-flyer", 4, EMovement::Fly, MapFieldAirUnit, MapFieldAirUnit, 2);
	}

	~PullWorld()
	{
		for (int i = 0; i < 2; ++i) {
			Players[i].Clear();
		}
		UnitManager->Init();
		AStarKnowUnseenTerrain = false;
		ClearGoalRegionMap();
		FreePathfinder();
		Map.Fields.clear();
		Map.Info.MapWidth = 0;
		Map.Info.MapHeight = 0;
		NumPlayers = savedNumPlayers;
		ThisPlayer = savedThisPlayer;
		GameCycle = savedGameCycle;
	}

	CUnit &Place(const CUnitType &type, const Vec2i &pos)
	{
		CUnit *unit = MakeUnitAndPlace(pos, type, &Players[0]);
		REQUIRE(unit != nullptr);
		return *unit;
	}

	/// Run the unit's current order until it is finished (or `cycles` pass); the order is then
	/// taken off as the unit's order loop does. Returns the cycles used.
	int Run(CUnit &unit, int cycles = 400)
	{
		for (int i = 0; i != cycles; ++i) {
			++GameCycle;
			COrder *order = unit.CurrentOrder();
			if (order->Action == UnitAction::Still) {
				return i;
			}
			if (unit.Wait) {
				--unit.Wait;
				continue;
			}
			order->Execute(unit);
			if (unit.CurrentOrder()->Finished) {
				unit.Orders.erase(unit.Orders.begin());
				if (unit.Orders.empty()) {
					unit.Orders.push_back(COrder::NewActionStill());
				}
			}
		}
		return cycles;
	}

	void Order(CUnit &unit, std::unique_ptr<COrder> order)
	{
		unit.Orders.clear();
		unit.Orders.push_back(std::move(order));
		unit.pathFinderData->output.Length = 0;
	}

	CUnitType footman;
	CUnitType destroyer;
	CUnitType farm;
	CUnitType hall;
	CUnitType flyer;
	CAnimations animations;

private:
	void MakeType(CUnitType &type, const char *ident, int slot, EMovement moveType, tile_flags mask,
	              tile_flags fieldFlags, int gridStep)
	{
		type.Ident = ident;
		type.Name = ident;
		type.Slot = slot;
		type.TileWidth = 1;
		type.TileHeight = 1;
		type.GridStep = gridStep;
		type.NumDirections = 8;
		type.MoveType = moveType;
		type.MovementMask = mask;
		type.FieldFlags = fieldFlags;
		type.Animations = &animations;
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
	unsigned long savedGameCycle = 0;
};

const COrder_Attack &AsAttack(const COrder &order)
{
	return static_cast<const COrder_Attack &>(order);
}
} // namespace

TEST_CASE("goal pull on the map: terrain and buildings block, standing units do not")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	Fill({0, 0}, {19, 40}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
	CUnit &footman = w.Place(w.footman, {24, 19});
	SetGoalRegionMap(64, 64, std::vector<uint16_t>(64 * 64, 0x4000));

	CHECK(PullGoalTowardUnit(footman, {17, 18}) == Vec2i(19, 18));
	CHECK(PullGoalTowardUnit(footman, {22, 19}) == Vec2i(22, 19));
	// a standing unit on the first free tile of the line does not block it
	CUnit &other = w.Place(w.footman, {20, 19});
	CHECK(Map.Field(20, 19)->CheckMask(MapFieldLandUnit));
	CHECK(PullGoalTowardUnit(footman, {17, 18}) == Vec2i(19, 18));
	other.Remove(nullptr);
	// a building does
	w.Place(w.farm, {20, 19});
	CHECK(PullGoalTowardUnit(footman, {17, 18}) == Vec2i(20, 19));
}

TEST_CASE("goal pull on the map: the region map - a free tile of another land mass is walked over")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	Fill({10, 0}, {14, 63}, MapFieldWaterAllowed);
	std::vector<uint16_t> words(64 * 64);
	for (int y = 0; y < 64; ++y) {
		for (int x = 0; x < 64; ++x) {
			words[y * 64 + x] = x < 10 ? 0x4000 : x <= 14 ? 0 : 0x4001;
		}
	}
	SetGoalRegionMap(64, 64, words);
	CUnit &footman = w.Place(w.footman, {3, 5});
	CHECK(PullGoalTowardUnit(footman, {20, 5}) == Vec2i(10, 5));
	CHECK(PullGoalTowardUnit(footman, {7, 9}) == Vec2i(7, 9));
}

TEST_CASE("goal pull: the region map built from the terrain when the map has no REGM")
{
	PullWorld w(16);
	Paint({0, 0}, {
		"####....####....",
		"####....####....",
		"##TT....####....",
		"##TT....####....",
		"........####....",
		"................",
	});
	Fill({0, 6}, {15, 15}, MapFieldLandAllowed);
	const std::vector<uint16_t> words = BuildGoalRegionsFromTerrain();
	const auto at = [&](int x, int y) { return words[y * 16 + x]; };
	CHECK(at(0, 0) == 0x4000);           // first land mass
	CHECK(at(2, 2) == 0xFFFE);           // forest
	CHECK(at(8, 0) == at(9, 0));         // one land mass
	CHECK(at(8, 0) != at(0, 0));         // ... not the first one
	CHECK(at(4, 0) < 0x4000);            // water
	CHECK(at(12, 0) == at(4, 0));        // the water round the middle land is one body
	CHECK(at(0, 6) == at(15, 15));       // the south land
	CHECK(at(0, 6) != at(0, 0));
	// GoalRegionAt builds it at the first use when nothing was loaded
	ClearGoalRegionMap();
	CHECK(GoalRegionAt({2, 2}) == 0xFFFE);
	CHECK(GoalRegionAt({0, 0}) == 0x4000);
	CHECK(GoalRegionAt({-1, 0}) == -1);
}

TEST_CASE("Move onto a forest: the goal is pulled once, from the tile the order starts on; the unit stops beside it")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	// north-east forest (B2)
	Fill({34, 0}, {63, 7}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
	CUnit &footman = w.Place(w.footman, {20, 22});
	w.Order(footman, COrder::NewActionMove({40, 1}));
	REQUIRE(footman.CurrentOrder()->GetGoalPos() == Vec2i(40, 1)); // not pulled before it runs
	footman.CurrentOrder()->Execute(footman);
	CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(34, 7));
	w.Run(footman);
	CHECK(footman.CurrentAction() == UnitAction::Still);
	// ORIG47 B2: stops on 33,8; the port's A* picks a free tile of the ring round 34,7 (PULL-10 open)
	CHECK(std::max(std::abs(footman.tilePos.x - 34), std::abs(footman.tilePos.y - 7)) == 1);
	CHECK(footman.SavedOrder == nullptr);
}

TEST_CASE("Move onto a free tile: unchanged, ends on it")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	CUnit &footman = w.Place(w.footman, {23, 8});
	w.Order(footman, COrder::NewActionMove({8, 16}));
	w.Run(footman);
	CHECK(footman.tilePos == Vec2i(8, 16));
}

TEST_CASE("Attack-move into a forest: goal pulled, ordered point kept; idle beside it with the memory, no retry")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	Fill({0, 0}, {19, 40}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
	CUnit &footman = w.Place(w.footman, {24, 19});
	w.Order(footman, COrder::NewActionAttack(footman, Vec2i(17, 18)));
	footman.CurrentOrder()->Execute(footman);
	const COrder_Attack &order = AsAttack(*footman.CurrentOrder());
	CHECK(order.GetGoalPos() == Vec2i(19, 18));
	CHECK(order.IsRememberedAttackMove());
	w.Run(footman);
	CHECK(footman.CurrentAction() == UnitAction::Still);
	CHECK(std::max(std::abs(footman.tilePos.x - 19), std::abs(footman.tilePos.y - 18)) == 1);
	CHECK(footman.tilePos.x == 20); // came from the east
	// idle with the attack-move as its memory (ORIG47 A2: memory 0x0A, counter 0), the ordered point
	REQUIRE(footman.SavedOrder != nullptr);
	CHECK(footman.SavedOrderIsMemory());
	CHECK(footman.SavedOrder->GetGoalPos() == Vec2i(17, 18));
	CHECK(footman.Wait == 0);
	// no timed retry: nothing moves it afterwards
	const Vec2i stop = footman.tilePos;
	w.Run(footman, 200);
	CHECK(footman.tilePos == stop);

	// the memory sent again (a fight's end) is pulled again from where the unit stands then: from beside
	// the forest the line back meets no free tile before the unit, so the click stays the goal
	// (0x4C2630 with flag 0); from farther south-east it is pulled to 19,19.
	GameCycle += 500;
	REQUIRE(footman.RestoreOrder());
	REQUIRE(footman.Orders.size() == 2); // put after the order that ends (here the idle one)
	footman.Orders.erase(footman.Orders.begin());
	REQUIRE(footman.CurrentAction() == UnitAction::Attack);
	CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(17, 18));
	CHECK(AsAttack(*footman.CurrentOrder()).IsRememberedAttackMove());
	auto copy = footman.CurrentOrder()->Clone();
	footman.CurrentOrder()->Execute(footman);
	CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(17, 18));
	CHECK(PullGoalTowardUnit(footman, {17, 18}) == Vec2i(17, 18));
	footman.MoveToXY({30, 25});
	w.Order(footman, std::move(copy));
	footman.CurrentOrder()->Execute(footman);
	CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(19, 19));
}

TEST_CASE("Attack-move onto passable ground: unchanged, ends on it, memory forgotten")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	CUnit &footman = w.Place(w.footman, {30, 9});
	w.Order(footman, COrder::NewActionAttack(footman, Vec2i(24, 19)));
	w.Run(footman);
	CHECK(footman.tilePos == Vec2i(24, 19));
	CHECK(footman.SavedOrder == nullptr);
}

TEST_CASE("Attack-move by an enclosed unit: the range widens until a tile of its pocket is in it, then idle with the memory")
{
	PullWorld w(24);
	Fill({0, 0}, {23, 23}, MapFieldLandAllowed);
	// a closed pocket round 3,3
	for (int i = 1; i <= 5; ++i) {
		for (const Vec2i p : {Vec2i(i, 1), Vec2i(i, 5), Vec2i(1, i), Vec2i(5, i)}) {
			Map.Field(p)->Flags = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
		}
	}
	CUnit &footman = w.Place(w.footman, {3, 3});
	w.Order(footman, COrder::NewActionAttack(footman, Vec2i(18, 18)));
	w.Run(footman, 200);
	CHECK(footman.CurrentAction() == UnitAction::Still);
	// the pocket's tile nearest the point (the ring of range 14 round 18,18 holds 4,4)
	CHECK(footman.tilePos.x == 4);
	CHECK(footman.tilePos.y == 4);
	CHECK(footman.Wait == 0);
	REQUIRE(footman.SavedOrder != nullptr);
	CHECK(footman.SavedOrder->GetGoalPos() == Vec2i(18, 18));
}

TEST_CASE("Destroyer onto a near coast tile or land inland: goal pulled one grid step away, no move")
{
	PullWorld w;
	Fill({0, 0}, {63, 13}, MapFieldWaterAllowed);
	Fill({0, 14}, {63, 63}, MapFieldLandAllowed);
	CUnit &ship = w.Place(w.destroyer, {42, 12});

	SUBCASE("C1 Attack onto the coast: no move, memory kept")
	{
		w.Order(ship, COrder::NewActionAttack(ship, Vec2i(43, 18)));
		ship.CurrentOrder()->Execute(ship);
		CHECK(ship.CurrentOrder()->GetGoalPos() == Vec2i(42, 14));
		w.Run(ship);
		CHECK(ship.tilePos == Vec2i(42, 12));
		CHECK(ship.CurrentAction() == UnitAction::Still);
		REQUIRE(ship.SavedOrder != nullptr);
		CHECK(ship.SavedOrder->GetGoalPos() == Vec2i(43, 18));
	}
	SUBCASE("C2 Attack inland: no move, memory kept")
	{
		w.Order(ship, COrder::NewActionAttack(ship, Vec2i(40, 16)));
		ship.CurrentOrder()->Execute(ship);
		CHECK(ship.CurrentOrder()->GetGoalPos() == Vec2i(41, 14));
		w.Run(ship);
		CHECK(ship.tilePos == Vec2i(42, 12));
		REQUIRE(ship.SavedOrder != nullptr);
		CHECK(ship.SavedOrder->GetGoalPos() == Vec2i(40, 16));
	}
	SUBCASE("C3 / C4 Move: no move, no memory")
	{
		for (const Vec2i click : {Vec2i(43, 18), Vec2i(40, 16)}) {
			w.Order(ship, COrder::NewActionMove(click));
			w.Run(ship);
			CHECK(ship.tilePos == Vec2i(42, 12));
			CHECK(ship.CurrentAction() == UnitAction::Still);
			CHECK(ship.SavedOrder == nullptr);
		}
	}
}

TEST_CASE("Destroyer round a headland and to a far coast: it sails")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldWaterAllowed);
	// a headland: land from x 30 to 40, y 14 to 63, the ship east of it, the point west of it
	Fill({30, 14}, {40, 63}, MapFieldLandAllowed);
	CUnit &ship = w.Place(w.destroyer, {44, 30});

	SUBCASE("C5: open water behind the headland, unchanged; sails round, ends within 1")
	{
		w.Order(ship, COrder::NewActionAttack(ship, Vec2i(21, 31)));
		ship.CurrentOrder()->Execute(ship);
		CHECK(ship.CurrentOrder()->GetGoalPos() == Vec2i(21, 31));
		w.Run(ship, 800);
		CHECK(std::abs(ship.tilePos.x - 21) <= 1);
		CHECK(std::abs(ship.tilePos.y - 31) <= 1);
		CHECK(ship.SavedOrder == nullptr);
	}
	SUBCASE("C7: a far coast tile across the headland's tip pulls to the shore; sails, stops on water beside it")
	{
		// land from y 37 south east of the headland too
		Fill({41, 50}, {63, 63}, MapFieldLandAllowed);
		w.Order(ship, COrder::NewActionAttack(ship, Vec2i(45, 52)));
		ship.CurrentOrder()->Execute(ship);
		CHECK(ship.CurrentOrder()->GetGoalPos() == Vec2i(45, 50));
		w.Run(ship, 800);
		CHECK(ship.tilePos.y < 50);
		CHECK(std::max(std::abs(ship.tilePos.x - 45), std::abs(ship.tilePos.y - 50)) <= 2);
		CHECK(Map.Field(ship.tilePos)->WaterOnMap());
	}
}

TEST_CASE("Patrol to a forest-locked point: each leg pulled, the unit walks to the edge and stands with the memory")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	Fill({0, 0}, {19, 40}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
	CUnit &footman = w.Place(w.footman, {30, 19});
	w.Order(footman, COrder::NewActionPatrol(footman.tilePos, Vec2i(17, 18)));
	footman.CurrentOrder()->Execute(footman);
	CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(19, 18));
	w.Run(footman);
	CHECK(footman.CurrentAction() == UnitAction::Still);
	CHECK(footman.tilePos.x == 20);
	REQUIRE(footman.SavedOrder != nullptr); // a land unit's patrol is remembered
	CHECK(footman.SavedOrder->Action == UnitAction::Patrol);
}

TEST_CASE("Patrol between two free points turns round as before")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	CUnit &footman = w.Place(w.footman, {10, 10});
	w.Order(footman, COrder::NewActionPatrol(footman.tilePos, Vec2i(14, 10)));
	for (int i = 0; i != 100 && footman.CurrentOrder()->GetGoalPos() == Vec2i(14, 10); ++i) {
		w.Run(footman, 1);
	}
	CHECK(footman.tilePos.x == 14);
	CHECK(footman.CurrentAction() == UnitAction::Patrol);
	CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(10, 10));
	CHECK(static_cast<const COrder_Patrol &>(*footman.CurrentOrder()).GetWayPoint() == Vec2i(14, 10));
}

// ---- the path step's "no way" (0x4D5E20 / 0x4D56C0; ORIG48 section 1) ----------------------------

TEST_CASE("no way: the first line step blocked and nothing free up to the blocked goal")
{
	// ORIG48 trial 5 (S): 33,28 -> pulled goal 34,26. The pull's line has 33,27 between them (free:
	// run 49 walked there 4 of 4); with a unit standing on it there is no way (run 49 7c: 2 of 2).
	const FreeTest trial5 = [](const Vec2i &p) { return p != Vec2i(34, 27) && p != Vec2i(34, 26); };
	CHECK_FALSE(PathLineFindsNoWay({33, 28}, {34, 26}, 1, trial5));
	const FreeTest trial5Taken = [&](const Vec2i &p) { return trial5(p) && p != Vec2i(33, 27); };
	CHECK(PathLineFindsNoWay({33, 28}, {34, 26}, 1, trial5Taken));
	// trials 7 / 8 / 14: the goal is the click, the whole line is forest
	const FreeTest nothing = [](const Vec2i &) { return false; };
	CHECK(PathLineFindsNoWay({35, 28}, {35, 25}, 1, nothing));
	CHECK(PathLineFindsNoWay({26, 17}, {35, 25}, 1, nothing));
	CHECK(PathLineFindsNoWay({12, 26}, {17, 18}, 1, nothing));
	// next to a blocked goal: no way either (the move ends where it stands, as before)
	CHECK(PathLineFindsNoWay({33, 27}, {34, 26}, 1, trial5));
	// the normal stops: the first line step is free (trial 6: 29,24; trial 10: 6,18; trial 13: 25,15)
	const FreeTest westOf30 = [](const Vec2i &p) { return p.x < 30; };
	CHECK_FALSE(PathLineFindsNoWay({28, 24}, {30, 24}, 1, westOf30));
	CHECK_FALSE(PathLineFindsNoWay({5, 18}, {14, 18}, 1, [](const Vec2i &p) { return p.x < 14; }));
	// run 47 D1: the first steps are forest but a later line tile is free - the tracer has a way; it walked
	const FreeTest d1 = [](const Vec2i &p) {
		return !(p.x <= 18 && p.y <= 19) && p != Vec2i(36, 27) && p != Vec2i(35, 27) && p != Vec2i(34, 27);
	};
	CHECK_FALSE(PathLineFindsNoWay({37, 28}, {18, 19}, 1, d1));
	// a free goal is never "no way" here, even behind a wall (not measured: left to the path finder)
	CHECK_FALSE(PathLineFindsNoWay({5, 5}, {7, 5}, 1, [](const Vec2i &p) { return p != Vec2i(6, 5); }));
	// the unit's own tile
	CHECK_FALSE(PathLineFindsNoWay({5, 5}, {5, 5}, 1, nothing));
}

TEST_CASE("no way on the grid: a ship's goal is floored to the grid and the line steps two tiles")
{
	const FreeTest waterToY13 = [](const Vec2i &p) { return p.y <= 13; };
	// C1 / C3: pulled goal 42,14 is the first grid step and blocked -> no way (live: no move)
	CHECK(PathLineFindsNoWay({42, 12}, {42, 14}, 2, waterToY13));
	// C2 / C4: 41,14 floored to 40,14 (land), the first diagonal grid step -> no way
	CHECK(PathLineFindsNoWay({42, 12}, {41, 14}, 2, waterToY13));
	// C7: 47,37 floored to 46,36 = open water -> a way (live: sails, ends on 46,36)
	CHECK_FALSE(PathLineFindsNoWay({46, 16}, {47, 37}, 2, [](const Vec2i &p) { return p.y <= 36; }));
}

TEST_CASE("Move / attack-move, ORIG48 trial 5 with a unit standing on 33,27: no way - the unit stays at distance 2")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	// the forest block F2 with its south edge as on the live map round the trial: 33,27 free, 34,27 forest
	const tile_flags forest = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
	Fill({27, 18}, {33, 26}, forest);
	Fill({34, 18}, {39, 27}, forest);
	CUnit &footman = w.Place(w.footman, {33, 28});
	// run 49 7c / P5f: a friendly unit stands on 33,27 (it is never given an order here)
	w.Place(w.footman, {33, 27});

	SUBCASE("Move: idle where it stands, no memory")
	{
		w.Order(footman, COrder::NewActionMove({35, 25}));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(34, 26)); // pulled as live (ORIG48 table)
		CHECK(footman.CurrentOrder()->Finished);
		w.Run(footman);
		CHECK(footman.tilePos == Vec2i(33, 28)); // live: no move (C7c)
		CHECK(footman.CurrentAction() == UnitAction::Still);
		CHECK(footman.SavedOrder == nullptr);
	}
	SUBCASE("Attack-move: idle where it stands with the attack-move as its memory")
	{
		w.Order(footman, COrder::NewActionAttack(footman, Vec2i(35, 25)));
		w.Run(footman);
		CHECK(footman.tilePos == Vec2i(33, 28));
		CHECK(footman.CurrentAction() == UnitAction::Still);
		REQUIRE(footman.SavedOrder != nullptr);
		CHECK(footman.SavedOrderIsMemory());
		CHECK(footman.SavedOrder->GetGoalPos() == Vec2i(35, 25));
	}
	SUBCASE("Patrol: stands with the memory")
	{
		w.Order(footman, COrder::NewActionPatrol(footman.tilePos, Vec2i(35, 25)));
		w.Run(footman);
		CHECK(footman.tilePos == Vec2i(33, 28));
		CHECK(footman.CurrentAction() == UnitAction::Still);
		REQUIRE(footman.SavedOrder != nullptr);
		CHECK(footman.SavedOrder->Action == UnitAction::Patrol);
	}
}

TEST_CASE("Move / patrol, ORIG48 trial 5 with 33,27 free: one step to 33,27, then idle (run 49 M5a-c, P5)")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	const tile_flags forest = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
	Fill({27, 18}, {33, 26}, forest);
	Fill({34, 18}, {39, 27}, forest);
	CUnit &footman = w.Place(w.footman, {33, 28});

	SUBCASE("Move")
	{
		w.Order(footman, COrder::NewActionMove({35, 25}));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(34, 26));
		w.Run(footman);
		CHECK(footman.tilePos == Vec2i(33, 27));
		CHECK(footman.CurrentAction() == UnitAction::Still);
	}
	SUBCASE("Patrol: stands there with the memory, no turn")
	{
		w.Order(footman, COrder::NewActionPatrol(footman.tilePos, Vec2i(35, 25)));
		w.Run(footman);
		CHECK(footman.tilePos == Vec2i(33, 27));
		CHECK(footman.CurrentAction() == UnitAction::Still);
		REQUIRE(footman.SavedOrder != nullptr);
		CHECK(footman.SavedOrder->Action == UnitAction::Patrol);
	}
}

TEST_CASE("Move / attack-move, ORIG48 trials 7 / 8: the line runs forest all the way - the goal stays the click, no move")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	Fill({27, 18}, {39, 27}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);

	SUBCASE("trial 8: from 9 tiles away (NW corner), Move")
	{
		CUnit &footman = w.Place(w.footman, {26, 17});
		w.Order(footman, COrder::NewActionMove({35, 25}));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(35, 25));
		w.Run(footman);
		CHECK(footman.tilePos == Vec2i(26, 17));
		CHECK(footman.CurrentAction() == UnitAction::Still);
	}
	SUBCASE("trial 8 as an attack-move: no move, memory kept")
	{
		CUnit &footman = w.Place(w.footman, {26, 17});
		w.Order(footman, COrder::NewActionAttack(footman, Vec2i(35, 25)));
		w.Run(footman);
		CHECK(footman.tilePos == Vec2i(26, 17));
		REQUIRE(footman.SavedOrder != nullptr);
		CHECK(footman.SavedOrder->GetGoalPos() == Vec2i(35, 25));
	}
	SUBCASE("trial 7: from the south, Move")
	{
		CUnit &footman = w.Place(w.footman, {35, 28});
		w.Order(footman, COrder::NewActionMove({35, 25}));
		w.Run(footman);
		CHECK(footman.tilePos == Vec2i(35, 28));
		CHECK(footman.CurrentAction() == UnitAction::Still);
	}
}

TEST_CASE("Move, ORIG48 normal stops: a free first line step walks to beside the pulled goal")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);

	SUBCASE("trial 6 (W): 28,24 -> pulled 30,24, first step 29,24 free")
	{
		Fill({30, 18}, {39, 27}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
		CUnit &footman = w.Place(w.footman, {28, 24});
		w.Order(footman, COrder::NewActionMove({35, 25}));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(30, 24));
		w.Run(footman);
		CHECK(footman.CurrentAction() == UnitAction::Still);
		CHECK(footman.tilePos != Vec2i(28, 24));
		CHECK(std::max(std::abs(footman.tilePos.x - 30), std::abs(footman.tilePos.y - 24)) == 1);
	}
	SUBCASE("trial 1 (N): 35,16 -> pulled 35,23, the column above free")
	{
		Fill({27, 23}, {39, 27}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
		CUnit &footman = w.Place(w.footman, {35, 16});
		w.Order(footman, COrder::NewActionMove({35, 25}));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(35, 23));
		w.Run(footman);
		CHECK(std::max(std::abs(footman.tilePos.x - 35), std::abs(footman.tilePos.y - 23)) == 1);
	}
	SUBCASE("run 47 D1: the first line steps are forest, but the line comes out of it - walks")
	{
		Fill({0, 0}, {18, 19}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
		Fill({34, 27}, {36, 27}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
		CUnit &footman = w.Place(w.footman, {37, 28});
		w.Order(footman, COrder::NewActionMove({17, 18}));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(18, 19));
		w.Run(footman, 800);
		CHECK(std::max(std::abs(footman.tilePos.x - 18), std::abs(footman.tilePos.y - 19)) == 1);
	}
}

TEST_CASE("Destroyer onto the coast: no way at the first Execute, the order ends there")
{
	PullWorld w;
	Fill({0, 0}, {63, 13}, MapFieldWaterAllowed);
	Fill({0, 14}, {63, 63}, MapFieldLandAllowed);
	CUnit &ship = w.Place(w.destroyer, {42, 12});
	w.Order(ship, COrder::NewActionMove({43, 18}));
	ship.CurrentOrder()->Execute(ship);
	CHECK(ship.CurrentOrder()->GetGoalPos() == Vec2i(42, 14));
	CHECK(ship.CurrentOrder()->Finished);
	CHECK(ship.tilePos == Vec2i(42, 12));
	w.Order(ship, COrder::NewActionAttack(ship, Vec2i(40, 16)));
	ship.CurrentOrder()->Execute(ship);
	CHECK(ship.CurrentOrder()->Finished);
	REQUIRE(ship.SavedOrder != nullptr);
	CHECK(ship.SavedOrder->GetGoalPos() == Vec2i(40, 16));
}

// ---- save and load --------------------------------------------------------------------------

namespace
{
std::unique_ptr<COrder> ParseOrderText(const std::string &body, CUnit &unit)
{
	const std::string chunk = "return " + body;
	REQUIRE(luaL_loadstring(Lua, chunk.c_str()) == 0);
	REQUIRE(lua_pcall(Lua, 0, 1, 0) == 0);
	std::unique_ptr<COrder> order = CclParseOrder(Lua, unit);
	lua_pop(Lua, 1);
	return order;
}

/// Save the order through its writer and read it back through the order reader of a save.
std::unique_ptr<COrder> SaveAndLoad(const COrder &order, CUnit &unit, std::string *text = nullptr)
{
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "goal_pull_order_test.lua";
	{
		CFile file;
		REQUIRE(file.open(path.string().c_str(), CL_OPEN_WRITE) != -1);
		order.Save(file, unit);
		file.close();
	}
	std::ifstream in(path);
	std::stringstream ss;
	ss << in.rdbuf();
	in.close();
	std::filesystem::remove(path);
	if (text) {
		*text = ss.str();
	}
	return ParseOrderText(ss.str(), unit);
}

struct LuaFixture
{
	LuaFixture() { InitLua(); }
	~LuaFixture()
	{
		lua_close(Lua);
		Lua = nullptr;
	}
};
} // namespace

TEST_CASE("save: goal-pulled round trip; a save without the key loads as already pulled")
{
	LuaFixture lua;
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	Fill({0, 0}, {19, 40}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
	CUnit &footman = w.Place(w.footman, {24, 19});

	SUBCASE("Move: not yet pulled stays to be pulled; pulled stays pulled")
	{
		auto fresh = COrder::NewActionMove({17, 18});
		std::string text;
		auto loaded = SaveAndLoad(*fresh, footman, &text);
		CHECK(text.find("\"goal-pulled\", 0") != std::string::npos);
		w.Order(footman, std::move(loaded));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(19, 18));

		auto again = SaveAndLoad(*footman.CurrentOrder(), footman, &text);
		CHECK(text.find("\"goal-pulled\", 1") != std::string::npos);
		footman.tilePos = Vec2i(30, 30); // as if loaded elsewhere: no new pull
		w.Order(footman, std::move(again));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(19, 18));
	}
	SUBCASE("Move written before the key: kept as it is")
	{
		auto old = ParseOrderText("{\"action-move\", \"range\", 0, \"tile\", {17, 18}}", footman);
		w.Order(footman, std::move(old));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(17, 18));
	}
	SUBCASE("Attack-move: the pulled goal and the ordered point survive a save")
	{
		w.Order(footman, COrder::NewActionAttack(footman, Vec2i(17, 18)));
		footman.CurrentOrder()->Execute(footman);
		REQUIRE(footman.CurrentOrder()->GetGoalPos() == Vec2i(19, 18));
		std::string text;
		auto loaded = SaveAndLoad(*footman.CurrentOrder(), footman, &text);
		CHECK(text.find("\"goal-pulled\", 1") != std::string::npos);
		CHECK(text.find("\"amove-tile\", {17, 18}") != std::string::npos);
		footman.tilePos = Vec2i(30, 30);
		w.Order(footman, std::move(loaded));
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(19, 18));
		CHECK(AsAttack(*footman.CurrentOrder()).IsRememberedAttackMove());
	}
	SUBCASE("Attack-move written before the key, heading for its point: kept, not pulled")
	{
		auto old = ParseOrderText("{\"action-attack\", \"range\", 0, \"min-range\", 0, \"tile\", {17, 18},"
		                          " \"amove-tile\", {17, 18}, \"state\", 1}",
		                          footman);
		w.Order(footman, std::move(old));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(17, 18));
	}
	SUBCASE("Patrol: round trip and the old form")
	{
		auto patrol = COrder::NewActionPatrol(footman.tilePos, Vec2i(17, 18));
		std::string text;
		auto loaded = SaveAndLoad(*patrol, footman, &text);
		CHECK(text.find("\"goal-pulled\", 0") != std::string::npos);
		w.Order(footman, std::move(loaded));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(19, 18));

		auto old = ParseOrderText("{\"action-patrol\", \"tile\", {17, 18}, \"range\", 0, \"patrol\", {24, 19}}",
		                          footman);
		w.Order(footman, std::move(old));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(17, 18));
	}
}

TEST_CASE("Patrol memory re-issue: the leg is set again from its clicked point and pulled from the unit's tile then")
{
	LuaFixture lua;
	// Run 49 P1 / P2: the patrol memory keeps the CLICKED point (+0x94); DECODE_B11_ROUND2 rule 3: the
	// re-issue sends the same leg toward it; DECODE_GOAL_PULL PULL-6: through 0x4D8970, which pulls.
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	Fill({0, 0}, {19, 40}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
	CUnit &footman = w.Place(w.footman, {30, 19});
	w.Order(footman, COrder::NewActionPatrol(footman.tilePos, Vec2i(17, 18)));
	w.Run(footman);
	CHECK(footman.CurrentAction() == UnitAction::Still);
	REQUIRE(footman.SavedOrder != nullptr);
	CHECK(footman.SavedOrder->GetGoalPos() == Vec2i(19, 18)); // the pulled goal while it stands

	// a fight took it elsewhere; the memory is sent again from there
	footman.MoveToXY({30, 32});
	const Vec2i repulled = PullGoalTowardUnit(footman, {17, 18});
	CHECK(repulled.x == 19);
	CHECK(repulled.y > 18); // another tile of the edge than the first pull
	GameCycle += 100;
	REQUIRE(footman.RestoreOrder());
	footman.Orders.erase(footman.Orders.begin());
	REQUIRE(footman.CurrentAction() == UnitAction::Patrol);
	CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(17, 18)); // set again from the click
	footman.CurrentOrder()->Execute(footman);
	CHECK(footman.CurrentOrder()->GetGoalPos() == repulled);

	SUBCASE("the ordered point survives a save; a save without it keeps the pulled goal")
	{
		std::string text;
		auto loaded = SaveAndLoad(*footman.CurrentOrder(), footman, &text);
		CHECK(text.find("\"ordered\", {17, 18}") != std::string::npos);
		CHECK(loaded->ReissueReady(footman, GameCycle + 1000) == wc2ai::Reissue::Ready);
		CHECK(loaded->GetGoalPos() == Vec2i(17, 18));

		auto old = ParseOrderText("{\"action-patrol\", \"tile\", {19, 18}, \"goal-pulled\", 1, \"range\", 0,"
		                          " \"patrol\", {30, 19}}",
		                          footman);
		CHECK(old->ReissueReady(footman, GameCycle + 1000) == wc2ai::Reissue::Ready);
		CHECK(old->GetGoalPos() == Vec2i(19, 18));
	}
}

TEST_CASE("flyer goal: never pulled off building, forest or water; pulled off tiles holding an air unit")
{
	// Run 49 item 6 (4 cases): air goals on a building, forest and water are not pulled. Off a tile
	// holding an air unit (0x200 kept in the air mask) the pull is the static reading PULL-11, not
	// measured live.
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	CUnit &flyer = w.Place(w.flyer, {10, 10});
	Map.Field(20, 10)->Flags = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
	CHECK(PullGoalTowardUnit(flyer, {20, 10}) == Vec2i(20, 10));
	Map.Field(20, 12)->Flags = MapFieldWaterAllowed;
	CHECK(PullGoalTowardUnit(flyer, {20, 12}) == Vec2i(20, 12));
	Map.Field(20, 14)->Flags = MapFieldLandAllowed | MapFieldBuilding;
	CHECK(PullGoalTowardUnit(flyer, {20, 14}) == Vec2i(20, 14));
	CUnit &flyer2 = w.Place(w.flyer, {10, 20});
	Map.Field(20, 20)->Flags = MapFieldLandAllowed | MapFieldAirUnit;
	Map.Field(19, 20)->Flags = MapFieldLandAllowed | MapFieldAirUnit;
	CHECK(PullGoalTowardUnit(flyer2, {20, 20}) == Vec2i(19, 20)); // the last air-held tile before 18,20
	CHECK_FALSE(PathStepFindsNoWay(flyer, {20, 10}));
}

// ---- the end of a Move: path end, an occupied goal, blocked steps, a folded group (B14-B) ------

namespace
{
/// Every unit's order loop for one cycle, in slot order (as the game's unit loop).
void StepAll(const std::vector<CUnit *> &units)
{
	++GameCycle;
	for (CUnit *unit : units) {
		COrder *order = unit->CurrentOrder();
		if (order->Action == UnitAction::Still) {
			continue;
		}
		if (unit->Wait) {
			--unit->Wait;
			continue;
		}
		order->Execute(*unit);
		if (unit->CurrentOrder()->Finished) {
			unit->Orders.erase(unit->Orders.begin());
			if (unit->Orders.empty()) {
				unit->Orders.push_back(COrder::NewActionStill());
			}
		}
	}
}

/// The longest run of cycles the unit spent on one tile while its order went on.
int LongestStall(PullWorld &w, CUnit &unit, int cycles, const std::function<void(int)> &each = nullptr)
{
	int longest = 0;
	int run = 0;
	Vec2i last = unit.tilePos;
	for (int i = 0; i != cycles && unit.CurrentAction() != UnitAction::Still; ++i) {
		if (each) {
			each(i);
		}
		w.Run(unit, 1);
		if (unit.tilePos == last) {
			longest = std::max(longest, ++run);
		} else {
			run = 0;
			last = unit.tilePos;
		}
	}
	return longest;
}
} // namespace

TEST_CASE("path end: the call after the last step answers PF_REACHED, not a wait (no Path[-1] read)")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	CUnit &footman = w.Place(w.footman, {10, 10});
	w.Order(footman, COrder::NewActionMove({13, 10}));
	std::vector<int> results;
	for (int i = 0; i != 6; ++i) {
		const auto [result, dir] = NextPathElement(footman);
		results.push_back(result);
		if (result <= 0) {
			break;
		}
		footman.MoveToXY(footman.tilePos + dir);
	}
	CHECK(results == std::vector<int>{3, 2, 1, PF_REACHED});
	CHECK(footman.tilePos == Vec2i(13, 10));
}

TEST_CASE("path end: a Move ends the cycle after its last step (no 10-cycle tail)")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	CUnit &footman = w.Place(w.footman, {10, 10});
	w.Order(footman, COrder::NewActionMove({15, 10}));
	const int cycles = w.Run(footman);
	CHECK(footman.tilePos == Vec2i(15, 10));
	// 5 steps of 2 cycles each (move + the animation's wait 1), then the reached answer
	CHECK(cycles <= 12);
}

TEST_CASE("occupied goal (ORIG50 s.4): a tile order onto a standing unit")
{
	PullWorld w(80);
	Fill({0, 0}, {79, 79}, MapFieldLandAllowed);
	CUnit &blocker = w.Place(w.footman, {62, 61}); // Footman 1497, standing

	CHECK(OccupiedGoalSide({65, 61}, {62, 61}) == Vec2i(0, 1));  // heading west: left = south
	CHECK(OccupiedGoalSide({62, 58}, {62, 61}) == Vec2i(1, 0));  // heading south: left = east
	CHECK(OccupiedGoalSide({59, 61}, {62, 61}) == Vec2i(0, -1)); // heading east: left = north
	CHECK(OccupiedGoalSide({62, 64}, {62, 61}) == Vec2i(-1, 0)); // heading north: left = west
	CHECK(OccupiedGoalSide({65, 64}, {62, 61}) == Vec2i(0, 0));  // a pure diagonal: no live case

	struct Row {
		const char *name;
		Vec2i start;
		Vec2i end;
	};
	const Row rows[] = {
		{"O1mm / O1b: from 1 tile, no move", {63, 61}, {63, 61}},
		{"O3a / O3b: from 3 east, ends south of it", {65, 61}, {62, 62}},
		{"O3c: from 3 north, ends east of it", {62, 58}, {63, 61}},
		{"O8a / O8b: from 8 east, ends south of it", {70, 61}, {62, 62}},
	};
	for (const Row &row : rows) {
		CAPTURE(row.name);
		CUnit &footman = w.Place(w.footman, row.start);
		w.Order(footman, COrder::NewActionMove({62, 61}));
		w.Run(footman);
		CHECK(footman.CurrentAction() == UnitAction::Still);
		CHECK(footman.tilePos == row.end);
		// idle there: no retry, no jitter (watched ~600 frames live)
		w.Run(footman, 600);
		CHECK(footman.tilePos == row.end);
		CHECK(blocker.tilePos == Vec2i(62, 61));
		footman.Remove(nullptr);
	}
}

TEST_CASE("occupied goal: the left tile taken -> the old ring end; a free goal is unchanged")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	w.Place(w.footman, {30, 30});
	w.Place(w.footman, {30, 31}); // the south neighbour is held too
	CUnit &footman = w.Place(w.footman, {34, 30});
	w.Order(footman, COrder::NewActionMove({30, 30}));
	w.Run(footman);
	CHECK(footman.CurrentAction() == UnitAction::Still);
	CHECK(std::max(std::abs(footman.tilePos.x - 30), std::abs(footman.tilePos.y - 30)) == 1);

	CUnit &other = w.Place(w.footman, {40, 40});
	w.Order(other, COrder::NewActionMove({44, 40}));
	w.Run(other);
	CHECK(other.tilePos == Vec2i(44, 40));
}

TEST_CASE("blocked step, standing unit (C1): no 10-cycle wait - the next cycle searches round it")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	CUnit &footman = w.Place(w.footman, {10, 20});
	w.Order(footman, COrder::NewActionMove({20, 20}));
	w.Run(footman, 1); // the first step is taken on the cached way through 12,20
	REQUIRE(footman.tilePos == Vec2i(11, 20));
	w.Place(w.footman, {12, 20}); // stands in the way now
	const int stall = LongestStall(w, footman, 200);
	CHECK(footman.CurrentAction() == UnitAction::Still);
	CHECK(footman.tilePos == Vec2i(20, 20));
	CHECK(stall <= 3); // was 10+ (PF_WAIT, unit.Wait = 10)
}

TEST_CASE("blocked step, crossing unit (G3): waits 15 cycles, then walks on to its goal")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	CUnit &footman = w.Place(w.footman, {10, 30});
	w.Order(footman, COrder::NewActionMove({20, 30}));
	w.Run(footman, 1);
	REQUIRE(footman.tilePos == Vec2i(11, 30));
	// a unit on its way over 12,30, there for 12 cycles (a Knight's step)
	CUnit &crosser = w.Place(w.footman, {12, 30});
	crosser.Moving = 1;
	const int stall = LongestStall(w, footman, 200, [&](int i) {
		if (i == 12 && !crosser.Removed) {
			crosser.Moving = 0;
			crosser.Remove(nullptr);
		}
	});
	CHECK(footman.CurrentAction() == UnitAction::Still);
	CHECK(footman.tilePos == Vec2i(20, 30));
	CHECK(stall >= 14);
	CHECK(stall <= 17); // live 15 (2 of 2); was 20 (two waits of 10)
}

TEST_CASE("folded group (ORIG51 s.4): the ring round T fills behind, behind-left, left, ... of the heading")
{
	PullWorld w;
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	const Vec2i T(30, 30);
	const auto sorted = [](std::vector<Vec2i> out) {
		std::sort(out.begin(), out.end(), [](const Vec2i &a, const Vec2i &b) {
			return a.y != b.y ? a.y < b.y : a.x < b.x;
		});
		return out;
	};
	// n units in a line behind each other, front unit first, coming from `from` (a unit step away
	// from T); every goal = T (extent >= 4: folded). Returns the end tiles relative to T in the
	// order the units went idle (slot order within one cycle).
	const auto march = [&](int n, const Vec2i &from) {
		std::vector<CUnit *> units;
		for (int k = 0; k != n; ++k) {
			units.push_back(&w.Place(w.footman, T + Vec2i(from.x * (6 + k), from.y * (6 + k))));
		}
		for (CUnit *u : units) {
			w.Order(*u, COrder::NewActionMove(T));
		}
		std::vector<Vec2i> arrival;
		std::vector<bool> done(units.size(), false);
		for (int c = 0; c != 600; ++c) {
			StepAll(units);
			for (size_t i = 0; i != units.size(); ++i) {
				if (!done[i] && units[i]->CurrentAction() == UnitAction::Still) {
					done[i] = true;
					arrival.push_back(units[i]->tilePos - T);
				}
			}
		}
		for (const CUnit *u : units) {
			CHECK(u->CurrentAction() == UnitAction::Still);
		}
		for (CUnit *u : units) {
			u->Remove(nullptr);
		}
		return arrival;
	};
	Vec2i places[8];
	REQUIRE(RingPlacesAround(T + Vec2i(6, 0), T, places) == 8); // heading W
	CHECK(places[0] == T + Vec2i(1, 0));   // behind = E
	CHECK(places[1] == T + Vec2i(1, 1));   // behind-left = SE
	CHECK(places[2] == T + Vec2i(0, 1));   // left = S
	CHECK(places[3] == T + Vec2i(1, -1));  // behind-right = NE
	CHECK(places[4] == T + Vec2i(-1, 1));  // front-left = SW
	CHECK(places[5] == T + Vec2i(0, -1));  // right = N
	CHECK(places[6] == T + Vec2i(-1, 0));  // front = W
	CHECK(places[7] == T + Vec2i(-1, -1)); // front-right = NW
	CHECK(RingPlacesAround(T + Vec2i(3, 3), T, places) == 0); // a pure diagonal: no live case

	SUBCASE("E4a-c / ORIG50 G1e: 4 from the east -> T, E, SE, S (6 of 6)")
	{
		CHECK(march(4, {1, 0}) == std::vector<Vec2i>{{0, 0}, {1, 0}, {1, 1}, {0, 1}});
	}
	SUBCASE("RS4a-c: 4 from the south -> T, S, SW, W (3 of 3)")
	{
		CHECK(march(4, {0, 1}) == std::vector<Vec2i>{{0, 0}, {0, 1}, {-1, 1}, {-1, 0}});
	}
	SUBCASE("ORIG50 L4m1 / L8m2 / G1n: 4 from the west -> T, W, NW, N; from the north -> T, N, NE, E")
	{
		// G1w1-3 (3 of 3) ended T, W, NW, SW: the 4th behind-right, against the rule's left
		CHECK(march(4, {-1, 0}) == std::vector<Vec2i>{{0, 0}, {-1, 0}, {-1, -1}, {0, -1}});
		CHECK(march(4, {0, -1}) == std::vector<Vec2i>{{0, 0}, {0, -1}, {1, -1}, {1, 0}});
	}
	SUBCASE("E9e / RS9a / RS9c: 9 -> the full 3 x 3 in the order T, b, bl, l, br, fl, r, f, fr")
	{
		const std::vector<Vec2i> fromEast{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {1, -1}, {-1, 1}, {0, -1}, {-1, 0}, {-1, -1}};
		const std::vector<Vec2i> fromSouth{{0, 0}, {0, 1}, {-1, 1}, {-1, 0}, {1, 1}, {-1, -1}, {1, 0}, {0, -1}, {1, -1}};
		const std::vector<Vec2i> east = march(9, {1, 0});
		const std::vector<Vec2i> south = march(9, {0, 1});
		REQUIRE(east.size() == 9);
		REQUIRE(south.size() == 9);
		// the first three places are the same in every live case (6 of 6)
		CHECK(std::vector<Vec2i>(east.begin(), east.begin() + 3) == std::vector<Vec2i>(fromEast.begin(), fromEast.begin() + 3));
		CHECK(std::vector<Vec2i>(south.begin(), south.begin() + 3) == std::vector<Vec2i>(fromSouth.begin(), fromSouth.begin() + 3));
		CHECK(sorted(east) == sorted(fromEast));
		CHECK(sorted(south) == sorted(fromSouth));
		for (size_t i = 0; i != east.size(); ++i) {
			MESSAGE("9 from E, place " << i << ": " << east[i].x << "," << east[i].y);
		}
		for (size_t i = 0; i != south.size(); ++i) {
			MESSAGE("9 from S, place " << i << ": " << south[i].x << "," << south[i].y);
		}
		const std::vector<Vec2i> west = march(9, {-1, 0});
		CHECK(sorted(west).size() == 9);
		CHECK(sorted(west) == sorted(fromEast));
	}
}

// ---- a standing blocker in the way (ORIG51 s.1, BDP Human IV around the corridor x 96, y 51..55) --

namespace
{
/// 2XHUM04.PUD x 78..114, y 38..68 read from the map file: '#' ground, 'T' forest / rock.
void PaintHuman4Corridor()
{
	Fill({0, 0}, {127, 127}, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
	Paint({78, 38}, {
		"T############TTTT#######TTTTTTT######",
		"#############TTTTT########TTT########",
		"############TTTTTTTT#################",
		"####TTTT####TTTTTTTT#################",
		"TTTTTTTT####TTTTTTTT#################",
		"TTTTTTTT#############################",
		"TTTTTTT#########################TTT##",
		"TTTTTTT######################TTTTTTT#",
		"TTTTTTT###TTTT##############TTTTTTTTT",
		"TTTTTT####TTTTTT#TTTT#######TTTTTTTTT",
		"TTTTTT###TTTTTTT#TTTT########TTTTTTTT",
		"TTTT#####TTTTTTT#TT############TTTTT#",
		"TTTT#####TTTTTT#####TTT########TTTTT#",
		"TTTT#####TTT####TT#TTTTTT############",
		"TTT##########TTTTT#TTTTTT############",
		"TTT######TTTTTTTTT#TTTTTTTT##########",
		"TTTT#####TTTTTTTTT#TTTTTTTTT#########",
		"TTTTT####TTTTTTTTT#TTTTTTTTT#########",
		"TTTTTT####TTTTTTTT##TTTTTTTT####TTTT#",
		"TTTTTT####TTTTTTTTT##TTTTT######TTTTT",
		"#TTT########TTTTTTT############TTTTTT",
		"############TTTTTTT############TTTTTT",
		"##############TTT##############TTTTTT",
		"################################TTTTT",
		"##################################TTT",
		"T#TTT####TT########################TT",
		"TTTTT####TTTT#####TT#################",
		"TTTTT###TTTTTTT#TTTTT################",
		"TTTT####TTTTTTTTTTTTTT###############",
		"TTT#####TTTTTTTTTTTTTTTTTT###########",
		"TT####TTTTTTTTTTTTTTTTTTTTT##########",
	});
}

/// Walk the unit's Move to its end; the tiles it stood on, in order (the first is the start).
std::vector<Vec2i> WalkPath(PullWorld &w, CUnit &unit, int cycles = 800, const std::function<void(int)> &each = nullptr)
{
	std::vector<Vec2i> path{unit.tilePos};
	for (int i = 0; i != cycles && unit.CurrentAction() != UnitAction::Still; ++i) {
		if (each) {
			each(i);
		}
		w.Run(unit, 1);
		if (unit.tilePos != path.back()) {
			path.push_back(unit.tilePos);
		}
	}
	return path;
}

std::string PathText(const std::vector<Vec2i> &path)
{
	std::string text;
	for (const Vec2i &p : path) {
		text += std::to_string(p.x) + "," + std::to_string(p.y) + " ";
	}
	return text;
}

bool Visits(const std::vector<Vec2i> &path, const Vec2i &tile)
{
	return std::find(path.begin(), path.end(), tile) != path.end();
}
} // namespace

TEST_CASE("standing blocker (ORIG51 s.1): never a stop - round it, the way it was there before the order")
{
	PullWorld w(128);
	struct Row {
		const char *name;
		Vec2i blocker;
		Vec2i start;
		Vec2i goal;
		int liveSteps;
	};
	// live: arrives ON the goal every time, 0 of 13 stops; the corridor rows walk to the mouth and
	// turn there in the original (29 / 31 / 32 / 31 steps), ours plans round it at the order
	const Row rows[] = {
		{"S1a", {96, 53}, {96, 46}, {96, 62}, 29},
		{"S1b", {96, 53}, {96, 44}, {96, 62}, 31},
		{"S2a", {96, 53}, {96, 62}, {96, 44}, 32},
		{"S3a", {96, 55}, {96, 46}, {96, 63}, 31},
		{"S5a", {98, 62}, {90, 62}, {108, 62}, 18},
		{"S5b", {98, 62}, {108, 62}, {90, 62}, 18},
	};
	for (const Row &row : rows) {
		CAPTURE(std::string(row.name));
		PaintHuman4Corridor();
		CUnit &blocker = w.Place(w.footman, row.blocker);
		CUnit &mover = w.Place(w.footman, row.start);
		w.Order(mover, COrder::NewActionMove(row.goal));
		int longest = 0;
		int run = 0;
		std::vector<Vec2i> path{mover.tilePos};
		for (int i = 0; i != 800 && mover.CurrentAction() != UnitAction::Still; ++i) {
			w.Run(mover, 1);
			if (mover.tilePos == path.back()) {
				longest = std::max(longest, ++run);
			} else {
				run = 0;
				path.push_back(mover.tilePos);
			}
		}
		MESSAGE(std::string(row.name) << " ours " << path.size() - 1 << " steps (live " << row.liveSteps << "): " << PathText(path));
		CHECK(mover.CurrentAction() == UnitAction::Still);
		CHECK(mover.tilePos == row.goal); // never stops short
		CHECK(longest <= 2);              // never waits (a step is 2 cycles here)
		CHECK(int(path.size()) - 1 <= row.liveSteps);
		CHECK(!Visits(path, row.blocker));
		mover.Remove(nullptr);
		blocker.Remove(nullptr);
	}
	SUBCASE("S5a / S5b: on open ground round the side left of the heading")
	{
		PaintHuman4Corridor();
		CUnit &blocker = w.Place(w.footman, {98, 62});
		CUnit &east = w.Place(w.footman, {90, 62});
		w.Order(east, COrder::NewActionMove({108, 62}));
		const auto pathE = WalkPath(w, east);
		CHECK(Visits(pathE, {98, 61})); // heading E: left = north (live row 61)
		east.Remove(nullptr);
		CUnit &west = w.Place(w.footman, {108, 62});
		w.Order(west, COrder::NewActionMove({90, 62}));
		const auto pathW = WalkPath(w, west);
		CHECK(Visits(pathW, {98, 63})); // heading W: left = south (live row 63)
		west.Remove(nullptr);
		blocker.Remove(nullptr);
	}
}

TEST_CASE("standing blocker steps in (ORIG51 s.1 S4 / S6): one step, then round it - never a stop")
{
	PullWorld w(128);
	SUBCASE("C4a / S4a-c: in the corridor - backs out the way it came and goes round")
	{
		PaintHuman4Corridor();
		CUnit &mover = w.Place(w.footman, {96, 51});
		w.Order(mover, COrder::NewActionMove({96, 63}));
		CUnit *blocker = nullptr;
		int longest = 0;
		int run = 0;
		std::vector<Vec2i> path{mover.tilePos};
		for (int i = 0; i != 800 && mover.CurrentAction() != UnitAction::Still; ++i) {
			if (blocker == nullptr && mover.tilePos == Vec2i(96, 53)) {
				blocker = &w.Place(w.footman, {96, 55}); // stands there now
			}
			w.Run(mover, 1);
			if (mover.tilePos == path.back()) {
				longest = std::max(longest, ++run);
			} else {
				run = 0;
				path.push_back(mover.tilePos);
			}
		}
		MESSAGE("C4a ours " << path.size() - 1 << " steps: " << PathText(path));
		REQUIRE(blocker != nullptr);
		CHECK(mover.CurrentAction() == UnitAction::Still);
		CHECK(mover.tilePos == Vec2i(96, 63));
		CHECK(longest <= 3); // live: back 17 frames after reaching it = one step
		// up to the tile next to it, then 3 steps back the way it came (live C4a: 96,54 -> 53 -> 52 -> 51,
		// then out at the mouth; live went round the west, ours the east - live S4a-c 2 east / 2 west)
		REQUIRE(path.size() > 7);
		CHECK(path[3] == Vec2i(96, 54));
		CHECK(path[4] == Vec2i(96, 53));
		CHECK(path[5] == Vec2i(96, 52));
		CHECK(path[6] == Vec2i(96, 51));
		CHECK(path[7].y == 50);
		CHECK(!Visits(path, {96, 55}));
	}
	struct Row {
		const char *name;
		Vec2i start;
		Vec2i goal;
		Vec2i stepIn; // the mover's tile when the blocker comes
		Vec2i blocker;
		Vec2i side;   // the live side step
	};
	const Row rows[] = {
		{"S6a: heading E, round the north", {90, 62}, {108, 62}, {98, 62}, {100, 62}, {100, 61}},
		{"S6c: heading W, round the south", {108, 62}, {90, 62}, {98, 62}, {96, 62}, {96, 63}},
	};
	for (const Row &row : rows) {
		CAPTURE(std::string(row.name));
		PaintHuman4Corridor();
		CUnit &mover = w.Place(w.footman, row.start);
		w.Order(mover, COrder::NewActionMove(row.goal));
		CUnit *blocker = nullptr;
		const auto path = WalkPath(w, mover, 800, [&](int) {
			if (blocker == nullptr && mover.tilePos == row.stepIn) {
				blocker = &w.Place(w.footman, row.blocker);
			}
		});
		MESSAGE(std::string(row.name) << " ours: " << PathText(path));
		REQUIRE(blocker != nullptr);
		CHECK(mover.tilePos == row.goal);
		CHECK(Visits(path, row.side));
		CHECK(int(path.size()) - 1 == 18); // the straight count (live S6c 18; S6a started 3 tiles further west)
		mover.Remove(nullptr);
		blocker->Remove(nullptr);
	}
}

TEST_CASE("folded ships (ORIG51 s.3 SL4a-c): 4 onto one tile end T, b, bl, l in ship places (3 of 3)")
{
	PullWorld w(128);
	Fill({0, 0}, {127, 127}, MapFieldWaterAllowed);
	const Vec2i T(84, 112);
	std::vector<CUnit *> ships;
	for (int k = 0; k != 4; ++k) {
		ships.push_back(&w.Place(w.destroyer, Vec2i(70 - 2 * k, 110))); // front ship first, heading E
	}
	for (CUnit *ship : ships) {
		w.Order(*ship, COrder::NewActionMove(T));
	}
	std::vector<Vec2i> arrival;
	std::vector<bool> done(ships.size(), false);
	for (int c = 0; c != 800; ++c) {
		StepAll(ships);
		for (size_t i = 0; i != ships.size(); ++i) {
			if (!done[i] && ships[i]->CurrentAction() == UnitAction::Still) {
				done[i] = true;
				arrival.push_back(ships[i]->tilePos - T);
			}
		}
	}
	for (size_t i = 0; i != arrival.size(); ++i) {
		MESSAGE("ship place " << i << ": " << arrival[i].x << "," << arrival[i].y);
	}
	CHECK(arrival == std::vector<Vec2i>{{0, 0}, {-2, 0}, {-2, -2}, {0, -2}});
}

TEST_CASE("ship onto an odd tile (ORIG51 s.3 SG1a): ends one short on the 2-tile grid")
{
	PullWorld w(128);
	Fill({0, 0}, {127, 127}, MapFieldWaterAllowed);
	CUnit &ship = w.Place(w.destroyer, {66, 110});
	w.Order(ship, COrder::NewActionMove({81, 116}));
	w.Run(ship, 400);
	CHECK(ship.CurrentAction() == UnitAction::Still);
	CHECK(ship.tilePos == Vec2i(80, 116));
}

// ---- HALL-EXIT-STOP (work/HALL_EXIT_STOP.md): Peasants that just left a Town Hall and get a move ----
// Round 3.4.74 (emulator) saw Peasants beside a hall "stop" on a debug-bridge OrderUnit "move". The
// engine side: the units leave the hall as the harvest does (DropOutNearest toward the mine), walk
// on toward the mine, and get the player's order through CommandMove with the queue flushed, as
// OrderUnit and the network command do; the loop below is HandleUnitAction's (actions.cpp).

namespace
{
/// HandleUnitAction for each unit standing on the map, in slot order (no unbreakable animation).
void HallStep(const std::vector<CUnit *> &units)
{
	++GameCycle;
	for (CUnit *unit : units) {
		if (unit->Removed) {
			continue;
		}
		if (unit->Orders[0]->Finished && unit->Orders[0]->Action != UnitAction::Still && unit->Orders.size() == 1) {
			unit->Orders[0] = COrder::NewActionStill();
		}
		if (unit->CurrentOrder()->Finished && unit->Orders.size() > 1) {
			unit->Orders.erase(unit->Orders.begin());
			unit->Wait = 0;
		}
		if (unit->CurrentAction() == UnitAction::Still) {
			continue;
		}
		unit->Orders[0]->Execute(*unit);
	}
}

int Chebyshev(const Vec2i &a, const Vec2i &b)
{
	return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

/// Four Peasants in the hall at (20,20) leave one by one toward the mine point, walk on toward it,
/// and get CommandMove(target) `delay[i]` cycles after leaving. Every one must end at the target
/// (the first) or in the ring round it, far from the hall.
void HallExitThenMove(PullWorld &w, CUnit &hall, const std::vector<CUnit *> &peasants, const Vec2i &mine,
                      const Vec2i &target)
{
	const int delays[4] = {0, 1, 3, 6};
	std::vector<int> leaveAt;
	for (size_t i = 0; i != peasants.size(); ++i) {
		leaveAt.push_back(int(i) * 4);
	}
	std::vector<Vec2i> exitTiles(peasants.size());
	for (int cycle = 0; cycle != 900; ++cycle) {
		for (size_t i = 0; i != peasants.size(); ++i) {
			CUnit &p = *peasants[i];
			if (cycle == leaveAt[i]) {
				REQUIRE(static_cast<bool>(p.Removed));
				DropOutNearest(p, mine, &hall);
				REQUIRE_FALSE(static_cast<bool>(p.Removed));
				exitTiles[i] = p.tilePos;
				w.Order(p, COrder::NewActionMove(mine)); // on its way back to the mine
			}
			if (cycle == leaveAt[i] + delays[i % 4]) {
				CommandMove(p, target, EFlushMode::On);
			}
		}
		HallStep(peasants);
	}
	int onTarget = 0;
	for (size_t i = 0; i != peasants.size(); ++i) {
		const CUnit &p = *peasants[i];
		CAPTURE(i);
		CAPTURE(exitTiles[i]);
		CAPTURE(p.tilePos);
		CHECK(p.CurrentAction() == UnitAction::Still);
		CHECK(Chebyshev(p.tilePos, target) <= 2);
		CHECK(Chebyshev(p.tilePos, hall.tilePos + Vec2i(1, 1)) >= 10);
		onTarget += p.tilePos == target ? 1 : 0;
	}
	CHECK(onTarget == 1);
}

std::vector<CUnit *> PeasantsInside(PullWorld &w, CUnit &hall, int count)
{
	std::vector<CUnit *> peasants;
	for (int i = 0; i != count; ++i) {
		CUnit &p = w.Place(w.footman, Vec2i(50 + i, 50));
		p.Remove(&hall);
		REQUIRE(p.Container == &hall);
		peasants.push_back(&p);
	}
	return peasants;
}
} // namespace

TEST_CASE("hall exit: Peasants out of a hall after harvesting, sent 10+ tiles away, all arrive")
{
	const Vec2i mines[] = {{12, 21}, {30, 22}, {21, 30}};
	const Vec2i targets[] = {{36, 34}, {6, 8}, {21, 6}, {8, 36}};
	for (const Vec2i &mine : mines) {
		for (const Vec2i &target : targets) {
			CAPTURE(mine);
			CAPTURE(target);
			PullWorld w;
			Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
			CUnit &hall = w.Place(w.hall, {20, 20});
			const std::vector<CUnit *> peasants = PeasantsInside(w, hall, 4);
			HallExitThenMove(w, hall, peasants, mine, target);
		}
	}
}

TEST_CASE("hall exit: Peasants read inside a hall from a save, placed by the load, sent 10+ tiles away, all arrive")
{
	const Vec2i targets[] = {{36, 34}, {6, 8}};
	for (const Vec2i &target : targets) {
		CAPTURE(target);
		PullWorld w;
		Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
		CUnit &hall = w.Place(w.hall, {20, 20});
		const std::vector<CUnit *> peasants = PeasantsInside(w, hall, 4);
		// What the file gives back: the hall read as standing, the Peasants as removed inside it.
		hall.Remove(nullptr);
		hall.Removed = 0;
		PlaceLoadedUnits();
		REQUIRE_FALSE(static_cast<bool>(hall.Removed));
		REQUIRE(Map.Field(Vec2i(21, 21))->CheckMask(MapFieldBuilding));
		for (CUnit *p : peasants) {
			REQUIRE(static_cast<bool>(p->Removed));
			REQUIRE(p->Container == &hall);
		}
		HallExitThenMove(w, hall, peasants, {12, 21}, target);
	}
}

// ---- the stop beside a pulled goal (PULL-10 calibration, live run 71 G) ---------------------------

TEST_CASE("pulled goal stop step: straight on, else 45 left, else 45 right, onto a free tile round the goal")
{
	const auto freeExcept = [](std::vector<Vec2i> blocked) {
		return [blocked](const Vec2i &p) { return std::find(blocked.begin(), blocked.end(), p) == blocked.end(); };
	};
	// run 71 / ORIG48 1r: at 34,22 heading S, goal 35,23: straight 34,23 (free, beside the goal)
	CHECK(PulledGoalStopStep({34, 22}, {0, 1}, {35, 23}, freeExcept({{35, 23}})) == Vec2i(34, 23));
	// run 71 g2: at 35,22 heading S: straight is the goal, left 36,23 forest -> right 34,23
	CHECK(PulledGoalStopStep({35, 22}, {0, 1}, {35, 23}, freeExcept({{35, 23}, {36, 23}})) == Vec2i(34, 23));
	// ORIG48 2: at 40,20 heading SW, goal 39,21: straight = goal, left S 40,21 free, right W 39,20 forest
	CHECK(PulledGoalStopStep({40, 20}, {-1, 1}, {39, 21}, freeExcept({{39, 21}, {39, 20}})) == Vec2i(40, 21));
	// ORIG48 6: at 29,23 heading NE, goal 30,24: no free tile round the goal ahead -> stop
	CHECK(PulledGoalStopStep({29, 23}, {1, -1}, {30, 24}, freeExcept({{30, 24}, {30, 23}})) == Vec2i(29, 23));
	// ORIG48 9: at 18,14 heading S, goal 17,15: straight 18,15 forest, the sides off the ring -> stop
	CHECK(PulledGoalStopStep({18, 14}, {0, 1}, {17, 15}, freeExcept({{17, 15}, {18, 15}})) == Vec2i(18, 14));
	// no heading (never stepped): stop
	CHECK(PulledGoalStopStep({34, 22}, {0, 0}, {35, 23}, freeExcept({})) == Vec2i(34, 22));
}

namespace
{
/// The passability round forest block F2 of the live map (tile classes only: '#' ground, 'T' blocked,
/// '.' water), x 26..42, y 14..28.
void PaintF2(PullWorld &)
{
	Fill({0, 0}, {63, 63}, MapFieldLandAllowed);
	Paint({26, 14}, {"###############TT",
	                 "################T",
	                 "################T",
	                 "#################",
	                 "#TTT#############",
	                 "#TTT#############",
	                 "#TTTT#######TT###",
	                 "##TTTTTT###TTT###",
	                 "###TTTTT##TTTT###",
	                 "####TTTT#TTTTT##T",
	                 "T###TTTTTTTTTT##T",
	                 "TT###TTTTTTTTT##T",
	                 ".T####TTTTTTT###T",
	                 ".TTT####TTTT###TT",
	                 "...TT##########T."});
}
} // namespace

TEST_CASE("Move, run 71 G: 35,16 -> 35,25 pulled to 35,23, stops on 34,23 (6 of 6 live), every time")
{
	for (int facing = 0; facing != 8; ++facing) {
		CAPTURE(facing);
		PullWorld w;
		PaintF2(w);
		CUnit &footman = w.Place(w.footman, {35, 16});
		footman.Direction = facing * NextDirection;
		w.Order(footman, COrder::NewActionMove({35, 25}));
		footman.CurrentOrder()->Execute(footman);
		CHECK(footman.CurrentOrder()->GetGoalPos() == Vec2i(35, 23));
		w.Run(footman);
		CHECK(footman.CurrentAction() == UnitAction::Still);
		CHECK(footman.tilePos == Vec2i(34, 23));
	}
}

TEST_CASE("Move, F2 walks of ORIG48 / ORIG55: where the port stops beside the pulled goal")
{
	struct Case { const char *name; Vec2i start; Vec2i goal; Vec2i live; };
	const Case cases[] = {
		{"ORIG48 2", {42, 18}, {39, 21}, {40, 21}},
		{"ORIG48 3", {41, 23}, {39, 24}, {40, 25}},
		{"ORIG48 6", {28, 24}, {30, 24}, {29, 23}},
		{"ORIG55 Q1", {37, 17}, {36, 22}, {35, 22}},
		{"ORIG55 Q2", {32, 17}, {35, 24}, {34, 23}},
		{"ORIG55 Q3", {37, 29}, {36, 27}, {35, 28}},
		{"ORIG55 Q4", {41, 21}, {39, 22}, {40, 23}},
		{"ORIG55 Q5", {30, 27}, {32, 26}, {32, 27}},
		{"ORIG55 Q6", {41, 25}, {39, 25}, {39, 26}},
	};
	int sameWith[2] = {0, 0};
	for (int stopStep = 0; stopStep != 2; ++stopStep) {
		MoveGoalPullStopStep = stopStep != 0;
		for (const Case &c : cases) {
			PullWorld w;
			PaintF2(w);
			CUnit &footman = w.Place(w.footman, c.start);
			w.Order(footman, COrder::NewActionMove({35, 25}));
			footman.CurrentOrder()->Execute(footman);
			const Vec2i goal = footman.CurrentOrder()->GetGoalPos();
			w.Run(footman);
			MESSAGE(std::string(c.name) << (stopStep ? " [stop step]" : " [without]") << ": goal " << goal.x << ","
			        << goal.y << " (live " << c.goal.x << "," << c.goal.y << "), stop " << footman.tilePos.x << ","
			        << footman.tilePos.y << " (live " << c.live.x << "," << c.live.y << ")");
			if (stopStep) { // without it: shown only, for the comparison
				CHECK(goal == c.goal);
				CHECK(std::max(std::abs(footman.tilePos.x - goal.x), std::abs(footman.tilePos.y - goal.y)) == 1);
			}
			sameWith[stopStep] += footman.tilePos == c.live ? 1 : 0;
		}
	}
	MoveGoalPullStopStep = true;
	MESSAGE("stops equal to live: without the stop step " << sameWith[0] << " of 9, with it " << sameWith[1] << " of 9");
	CHECK(sameWith[1] >= 7); // the two left (ORIG48 6, ORIG55 Q5) differ by the port's walk, not by the stop
}
