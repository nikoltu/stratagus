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
/**@name test_ship_hit.cpp - Ships and land units on an attack-move: what they take, when they go on. */
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
#include "actions.h"
#include "animation.h"
#include "animation/animation_attack.h"
#include "animation/animation_move.h"
#include "animation/animation_unbreakable.h"
#include "animation/animation_wait.h"
#include "goal_pull.h"
#include "iolib.h"
#include "map.h"
#include "missile.h"
#include "pathfinder.h"
#include "player.h"
#include "script.h"
#include "settings.h"
#include "unit.h"
#include "unit_find.h"
#include "unit_manager.h"
#include "unittype.h"
#include "wc2ai.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

// A synthetic shore laid as the E4 scene of run 47 (ToD Orc III, ORIG47 s.5): land on rows 0..51,
// water from row 52; the person's ship (player 0) on 56,52 with range 4; the enemy (player 1)
// Footman on 55,48 (d 4), Archer on 54,50 (d 2). No game data.

namespace
{
constexpr tile_flags LandMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding | MapFieldCoastAllowed
                                | MapFieldWaterAllowed | MapFieldUnpassable;
constexpr tile_flags NavalMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding | MapFieldCoastAllowed
                                 | MapFieldLandAllowed | MapFieldUnpassable;

class ShoreWorld
{
public:
	ShoreWorld() : missile("missile-test-none")
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		savedGameCycle = GameCycle;
		savedSimplified = GameSettings.SimplifiedAutoTargeting;
		GameSettings.SimplifiedAutoTargeting = false;
		if (UnitManager == nullptr) {
			UnitManager = new CUnitManager;
		}
		UnitManager->Init();
		UnitTypeVar.Init();
		Map.Info.MapWidth = 64;
		Map.Info.MapHeight = 64;
		Map.Create();
		InitPathfinder();
		AStarKnowUnseenTerrain = true;
		ClearGoalRegionMap();
		for (int y = 0; y != 64; ++y) {
			for (int x = 0; x != 64; ++x) {
				Map.Field(x, y)->Flags = y <= 51 ? MapFieldLandAllowed : MapFieldWaterAllowed;
			}
		}
		for (int i = 0; i < 4; ++i) {
			Players[i].Clear();
			Players[i].Index = i;
			Players[i].Type = PlayerTypes::PlayerPerson;
			Players[i].Team = i;
			Players[i].UnitLimit = 100;
			Players[i].BuildingLimit = 100;
			Players[i].TotalUnitLimit = 100;
		}
		NumPlayers = 4;
		ThisPlayer = &Players[3]; // owns nothing: no voices
		Players[0].SetDiplomacyEnemyWith(Players[1]);
		Players[1].SetDiplomacyEnemyWith(Players[0]);
		GameCycle = 1000;

		missile.Class = MissileClass::Nothing;
		auto move = std::make_unique<CAnimation_Move>();
		move->Init("32", nullptr);
		auto wait = std::make_unique<CAnimation_Wait>();
		wait->Init("1", nullptr);
		animations.Move.push_back(std::move(move));
		animations.Move.push_back(std::move(wait));
		auto still = std::make_unique<CAnimation_Wait>();
		still->Init("1", nullptr);
		animations.Still.push_back(std::move(still));

		MakeType(ship, "unit-test-ship", 0, EMovement::Naval, NavalMask, MapFieldSeaUnit, 2, 4, 30);
		ship.ReactRangePerson = 6;
		MakeType(footman, "unit-test-footman", 1, EMovement::Land, LandMask, MapFieldLandUnit, 1, 1, 60);
		MakeType(archer, "unit-test-archer", 2, EMovement::Land, LandMask, MapFieldLandUnit, 1, 4, 55);
		MakeType(farm, "unit-test-farm", 3, EMovement::Land, LandMask, MapFieldBuilding, 1, 0, 20);
		farm.Building = true;
		farm.CanAttack = false;
		farm.BoolFlag[CANATTACK_INDEX].value = false;
		farm.Animations = nullptr;
	}

	~ShoreWorld()
	{
		for (int i = 0; i < 4; ++i) {
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
		GameSettings.SimplifiedAutoTargeting = savedSimplified;
	}

	CUnit &Ship(const Vec2i &pos) { return Place(ship, pos, 0); }
	/// An enemy unit, seen by the ship's player.
	CUnit &Enemy(const CUnitType &type, const Vec2i &pos)
	{
		CUnit &unit = Place(type, pos, 1);
		unit.VisCount[0] = 1;
		return unit;
	}

	/// One game cycle of the unit's order loop: a finished order is taken off.
	void Step(CUnit &unit)
	{
		++GameCycle;
		if (unit.Wait) {
			--unit.Wait;
			return;
		}
		unit.CurrentOrder()->Execute(unit);
		while (unit.Orders.size() > 1 && unit.Orders[0]->Finished) {
			unit.Orders.erase(unit.Orders.begin());
		}
		if (unit.CurrentOrder()->Finished && unit.CurrentAction() != UnitAction::Still) {
			unit.Orders.erase(unit.Orders.begin());
			if (unit.Orders.empty()) {
				unit.Orders.push_back(COrder::NewActionStill());
			}
		}
	}
	void Steps(CUnit &unit, int count)
	{
		for (int i = 0; i != count; ++i) {
			Step(unit);
		}
	}

	void Order(CUnit &unit, std::unique_ptr<COrder> order)
	{
		unit.Orders.clear();
		unit.Orders.push_back(std::move(order));
		unit.pathFinderData->output.Length = 0;
	}

	static void Kill(CUnit &unit) { HitUnit(nullptr, unit, 10000); }

	CUnitType ship;
	CUnitType footman;
	CUnitType archer;
	CUnitType farm;
	CAnimations animations;
	MissileType missile;

private:
	CUnit &Place(const CUnitType &type, const Vec2i &pos, int player)
	{
		CUnit *unit = MakeUnitAndPlace(pos, type, &Players[player]);
		REQUIRE(unit != nullptr);
		return *unit;
	}

	void MakeType(CUnitType &type, const char *ident, int slot, EMovement moveType, tile_flags mask,
	              tile_flags fieldFlags, int gridStep, int range, int priority)
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
		type.CanAttack = true;
		type.CanTarget = ECanTargetFlag::Land | ECanTargetFlag::Sea;
		type.Missile.Missile = &missile;
		type.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		type.BoolFlag[CANATTACK_INDEX].value = true;
		type.DefaultStat.Variables.resize(UnitTypeVar.GetNumberVariable());
		type.DefaultStat.Variables[HP_INDEX].Max = 60;
		type.DefaultStat.Variables[HP_INDEX].Value = 60;
		type.DefaultStat.Variables[HP_INDEX].Enable = 1;
		type.DefaultStat.Variables[ATTACKRANGE_INDEX].Max = range;
		type.DefaultStat.Variables[ATTACKRANGE_INDEX].Value = range;
		type.DefaultStat.Variables[PRIORITY_INDEX].Max = priority;
		type.DefaultStat.Variables[PRIORITY_INDEX].Value = priority;
		type.MapDefaultStat = type.DefaultStat;
		for (int i = 0; i < PlayerMax; ++i) {
			type.Stats[i] = type.DefaultStat;
		}
	}

	CPlayer *savedThisPlayer = nullptr;
	int savedNumPlayers = 0;
	unsigned long savedGameCycle = 0;
	bool savedSimplified = false;
};

const COrder_Attack &AsAttack(const COrder &order)
{
	return static_cast<const COrder_Attack &>(order);
}

const Vec2i ShipTile{56, 52};
const Vec2i SeaPoint{20, 60};
} // namespace

namespace
{
/// Steps the unit until its attack-move has a goal unit (or `cycles` pass).
CUnit *StepUntilGoal(ShoreWorld &w, CUnit &unit, int cycles = 60)
{
	for (int i = 0; i != cycles; ++i) {
		if (unit.CurrentAction() == UnitAction::Attack && unit.CurrentOrder()->HasGoal()
		    && unit.CurrentOrder()->GetGoal()->IsAlive()) {
			return unit.CurrentOrder()->GetGoal();
		}
		w.Step(unit);
	}
	return unit.CurrentAction() == UnitAction::Attack ? unit.CurrentOrder()->GetGoal() : nullptr;
}

/// Steps the unit until it stands (or `cycles` pass).
void StepUntilStill(ShoreWorld &w, CUnit &unit, int cycles = 600)
{
	for (int i = 0; i != cycles && unit.CurrentAction() != UnitAction::Still; ++i) {
		w.Step(unit);
	}
}

int Dist(const Vec2i &a, const Vec2i &b)
{
	return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}
} // namespace

// Ships search as land units do (owner's play; run 49 section 4: a moving attack-move Destroyer
// took a Shipyard, then a Catapult, a Tower, Axethrowers; an idle one took a Shipyard).

TEST_CASE("ship attack-move: a moving ship takes a land building in reach (run 49 S4)")
{
	ShoreWorld w;
	CUnit &ship = w.Ship(ShipTile);
	CUnit &farm = w.Enemy(w.farm, {56, 49}); // d 3, does not shoot
	w.Order(ship, COrder::NewActionAttack(ship, SeaPoint));

	CHECK(StepUntilGoal(w, ship) == &farm);
	CHECK(ship.CurrentAction() == UnitAction::Attack);
}

TEST_CASE("ship attack-move: land units in reach are fought one after another, then it sails on (ORIG47 E4, run 49 H1 / H2)")
{
	ShoreWorld w;
	CUnit &ship = w.Ship(ShipTile);
	CUnit &footman = w.Enemy(w.footman, {55, 48}); // d 4
	CUnit &archer = w.Enemy(w.archer, {54, 50});   // d 2
	w.Order(ship, COrder::NewActionAttack(ship, SeaPoint));

	HitUnit(&archer, ship, 6);
	CUnit *first = StepUntilGoal(w, ship);
	REQUIRE(first != nullptr);
	// E4 / H2: the Footman beside the Archer is taken first (the score: Priority 60 against 55)
	CHECK(first == &footman);

	ShoreWorld::Kill(footman);
	// after the kill the ship holds (wc2ai::ShipKillHold), then takes the Archer
	CHECK(StepUntilGoal(w, ship, wc2ai::ShipKillHold + 10) == &archer);
	w.Steps(ship, wc2ai::AttackMoveRetryWait + 5);
	REQUIRE(ship.CurrentOrder()->GetGoal() == &archer);

	ShoreWorld::Kill(archer);
	// nothing else in reach: the fight ends normally and the attack-move goes on to its point
	StepUntilStill(w, ship);
	CHECK(ship.CurrentAction() == UnitAction::Still);
	CHECK(Dist(ship.tilePos, SeaPoint) <= 1);
	CHECK_FALSE(ship.SavedOrderIsMemory());
}

// After a fight that ends normally the original's ship stays in its fight, without a target, for
// about 111 frames (= game cycles) after the kill: run 49 H1 112 / H2 111 to the re-issue (counter
// 20, sails on), H2 109 / H3 112 / ORIG47 E4 110 and 113 to the next target taken (6 cases).

TEST_CASE("ship attack-move: after the kill it holds ShipKillHold cycles, then takes the next target in reach (run 49 H2, ORIG47 E4)")
{
	ShoreWorld w;
	CUnit &ship = w.Ship(ShipTile);
	CUnit &footman = w.Enemy(w.footman, {55, 48}); // d 4
	CUnit &archer = w.Enemy(w.archer, {54, 50});   // d 2, in reach the whole time
	w.Order(ship, COrder::NewActionAttack(ship, SeaPoint));
	HitUnit(&archer, ship, 6);
	REQUIRE(StepUntilGoal(w, ship) == &footman);
	const Vec2i stood = ship.tilePos;

	ShoreWorld::Kill(footman);
	int cycles = 0;
	for (; cycles != wc2ai::ShipKillHold - 1; ++cycles) {
		w.Step(ship);
		REQUIRE(ship.CurrentAction() == UnitAction::Attack);
		REQUIRE_FALSE(ship.CurrentOrder()->HasGoal()); // no search while it holds
		REQUIRE(ship.tilePos == stood);
		if (cycles == 40) {
			HitUnit(&archer, ship, 1); // a hit does not end the hold either (H2 f435 - f544)
		}
	}
	for (; cycles != wc2ai::ShipKillHold + 10 && !ship.CurrentOrder()->HasGoal(); ++cycles) {
		w.Step(ship);
	}
	CHECK(ship.CurrentOrder()->GetGoal() == &archer);
	CHECK(cycles >= 109);
	CHECK(cycles <= 113);
	// taken in the fight, without a re-issue of the attack-move
	CHECK(AsAttack(*ship.CurrentOrder()).GetAttackMovePos() == SeaPoint);
}

TEST_CASE("ship attack-move: with nothing else in reach it sails on ShipKillHold cycles after the kill (run 49 H1 / H2)")
{
	ShoreWorld w;
	CUnit &ship = w.Ship(ShipTile);
	CUnit &footman = w.Enemy(w.footman, {55, 48}); // d 4
	w.Order(ship, COrder::NewActionAttack(ship, SeaPoint));
	REQUIRE(StepUntilGoal(w, ship) == &footman);
	const Vec2i stood = ship.tilePos;

	ShoreWorld::Kill(footman);
	int cycles = 0;
	for (; cycles != 200 && ship.CurrentOrder()->GetGoalPos() != SeaPoint; ++cycles) {
		w.Step(ship);
		REQUIRE(ship.CurrentAction() == UnitAction::Attack);
	}
	CHECK(cycles >= 109);
	CHECK(cycles <= 113);
	CHECK(ship.tilePos == stood);
	StepUntilStill(w, ship);
	CHECK(Dist(ship.tilePos, SeaPoint) <= 1);
}

TEST_CASE("ship attack-move: a shot's animation still playing at the kill ends first; the hold counts from the kill")
{
	ShoreWorld w;
	// the Destroyer's shot: unbreakable, the attack, 119 cycles, the end, 1 cycle (human/anim.lua)
	CAnimations shipAnims;
	auto add = [](std::vector<std::unique_ptr<CAnimation>> &list, std::unique_ptr<CAnimation> anim,
	              const char *arg) {
		anim->Init(arg, nullptr);
		list.push_back(std::move(anim));
	};
	add(shipAnims.Attack, std::make_unique<CAnimation_Unbreakable>(), "begin");
	add(shipAnims.Attack, std::make_unique<CAnimation_Attack>(), "");
	add(shipAnims.Attack, std::make_unique<CAnimation_Wait>(), "119");
	add(shipAnims.Attack, std::make_unique<CAnimation_Unbreakable>(), "end");
	add(shipAnims.Attack, std::make_unique<CAnimation_Wait>(), "1");
	add(shipAnims.Move, std::make_unique<CAnimation_Move>(), "32");
	add(shipAnims.Move, std::make_unique<CAnimation_Wait>(), "1");
	add(shipAnims.Still, std::make_unique<CAnimation_Wait>(), "1");
	w.ship.Animations = &shipAnims;

	CUnit &ship = w.Ship(ShipTile);
	CUnit &footman = w.Enemy(w.footman, {55, 48}); // d 4
	w.Order(ship, COrder::NewActionAttack(ship, SeaPoint));
	REQUIRE(StepUntilGoal(w, ship) == &footman);
	for (int i = 0; i != 20 && !ship.Anim.Unbreakable; ++i) {
		w.Step(ship);
	}
	REQUIRE(ship.Anim.Unbreakable); // the shot has just been fired

	// the ball lands 20 cycles later and kills: the animation then ends ~100 cycles after the kill,
	// before the hold does
	w.Steps(ship, 20);
	ShoreWorld::Kill(footman);
	int cycles = 0;
	bool shotAgain = false;
	int animEnded = -1;
	for (; cycles != 200 && ship.CurrentOrder()->GetGoalPos() != SeaPoint; ++cycles) {
		w.Step(ship);
		REQUIRE(ship.CurrentAction() == UnitAction::Attack);
		if (animEnded < 0 && !ship.Anim.Unbreakable) {
			animEnded = cycles;
		}
		// no new shot starts while the ship holds
		if (cycles > 110 && ship.Anim.CurrAnim == &shipAnims.Attack && ship.Anim.Unbreakable) {
			shotAgain = true;
		}
	}
	CHECK_FALSE(shotAgain);
	CHECK(animEnded >= 95);
	CHECK(animEnded <= 105); // the shot's end alone would have sent it on here
	CHECK(cycles >= 109);
	CHECK(cycles <= 113);
	w.ship.Animations = &w.animations;
}

TEST_CASE("land attack-move: no hold after the kill (ORIG47 A4: on 15 frames later)")
{
	ShoreWorld w;
	CUnit *footman = MakeUnitAndPlace({30, 30}, w.footman, &Players[0]);
	REQUIRE(footman != nullptr);
	CUnit &archer = w.Enemy(w.archer, {31, 31});
	const Vec2i point{10, 30};
	w.Order(*footman, COrder::NewActionAttack(*footman, point));
	HitUnit(&archer, *footman, 6);
	w.Step(*footman);
	REQUIRE(footman->CurrentOrder()->GetGoal() == &archer);

	ShoreWorld::Kill(archer);
	w.Steps(*footman, 3);
	REQUIRE(footman->CurrentAction() == UnitAction::Attack);
	CHECK(footman->CurrentOrder()->GetGoalPos() == point);
}

TEST_CASE("ship attack-move: a next target it cannot reach - it stands with the memory for good (run 49 H3, ORIG47 E3)")
{
	ShoreWorld w;
	CUnit &ship = w.Ship(ShipTile);
	CUnit &archer = w.Enemy(w.archer, {56, 46}); // 6 inland: the ship cannot come within 4 of it
	w.Order(ship, COrder::NewActionAttack(ship, SeaPoint));

	HitUnit(&archer, ship, 6);
	for (int i = 0; i != 50 && ship.CurrentAction() == UnitAction::Attack; ++i) {
		w.Step(ship);
	}
	CHECK(ship.CurrentAction() == UnitAction::Still);
	REQUIRE(ship.SavedOrder != nullptr);
	CHECK(ship.SavedOrderIsMemory());
	CHECK(ship.SavedOrder->GetGoalPos() == SeaPoint);
	const Vec2i stood = ship.tilePos;
	w.Steps(ship, 200);
	CHECK(ship.CurrentAction() == UnitAction::Still);
	CHECK(ship.tilePos == stood);
}

TEST_CASE("ship standing with the memory, attacked again (ORIG51 s.2 H3): the later fight's end re-issues the attack-move after the hold")
{
	ShoreWorld w;
	CUnit &ship = w.Ship(ShipTile);
	CUnit &archer = w.Enemy(w.archer, {56, 46}); // the unreachable pick: the ship stands with the memory
	w.Order(ship, COrder::NewActionAttack(ship, SeaPoint));
	HitUnit(&archer, ship, 6);
	for (int i = 0; i != 50 && ship.CurrentAction() == UnitAction::Attack; ++i) {
		w.Step(ship);
	}
	REQUIRE(ship.CurrentAction() == UnitAction::Still);
	REQUIRE(ship.SavedOrderIsMemory());
	ShoreWorld::Kill(archer);
	w.Steps(ship, 2186); // live: 2186 frames standing
	REQUIRE(ship.CurrentAction() == UnitAction::Still);
	const Vec2i stood = ship.tilePos;

	// a new enemy Destroyer comes into reach: the standing ship takes it (live order 0x10)
	CUnit &enemy = w.Enemy(w.ship, ShipTile + Vec2i(4, 0));
	int taken = 0;
	for (; taken != 60 && ship.CurrentAction() == UnitAction::Still; ++taken) {
		w.Step(ship);
	}
	MESSAGE("the standing ship takes the new enemy after " << taken << " cycles");
	REQUIRE(ship.CurrentAction() == UnitAction::Attack);
	REQUIRE(StepUntilGoal(w, ship) == &enemy);
	w.Steps(ship, 10);
	ShoreWorld::Kill(enemy);
	int cycles = 0;
	for (; cycles != 300 && !(ship.CurrentAction() == UnitAction::Attack && ship.CurrentOrder()->GetGoalPos() == SeaPoint);
	     ++cycles) {
		w.Step(ship);
	}
	MESSAGE("re-issued " << cycles << " cycles after the kill (live 109; normal fight ends 109 / 109 / 112)");
	CHECK(cycles >= 109);
	CHECK(cycles <= 113);
	CHECK(ship.tilePos == stood);
	StepUntilStill(w, ship);
	CHECK(Dist(ship.tilePos, SeaPoint) <= 1); // sails on to its point
}

TEST_CASE("land attack-move: resumes to its point after the fight")
{
	ShoreWorld w;
	CUnit *footman = MakeUnitAndPlace({30, 30}, w.footman, &Players[0]);
	REQUIRE(footman != nullptr);
	CUnit &archer = w.Enemy(w.archer, {31, 31});
	const Vec2i point{10, 30};
	w.Order(*footman, COrder::NewActionAttack(*footman, point));

	HitUnit(&archer, *footman, 6);
	w.Step(*footman);
	REQUIRE(footman->CurrentAction() == UnitAction::Attack);
	CHECK(footman->CurrentOrder()->GetGoal() == &archer);

	ShoreWorld::Kill(archer);
	w.Step(*footman);
	REQUIRE(footman->CurrentAction() == UnitAction::Attack);
	CHECK_FALSE(footman->CurrentOrder()->HasGoal());
	CHECK(footman->CurrentOrder()->GetGoalPos() == point);
}

TEST_CASE("attack-move into unexplored open ground: fights what it meets, then reaches the point (owner)")
{
	ShoreWorld w;
	w.footman.ReactRangePerson = 6;
	CUnit *footman = MakeUnitAndPlace({40, 30}, w.footman, &Players[0]);
	REQUIRE(footman != nullptr);
	const Vec2i point{10, 30};
	// nothing on the way is explored by the person
	for (int x = 0; x != 36; ++x) {
		for (int y = 25; y != 36; ++y) {
			REQUIRE_FALSE(Map.Field(x, y)->playerInfo.IsExplored(Players[0]));
		}
	}
	CUnit &archer = w.Enemy(w.archer, {22, 31}); // on the way, seen only when the footman comes near
	archer.VisCount[0] = 0;
	w.Order(*footman, COrder::NewActionAttack(*footman, point));

	// the free point is the goal as clicked (the pull leaves it), and the footman walks
	w.Step(*footman);
	REQUIRE(footman->CurrentAction() == UnitAction::Attack);
	CHECK(footman->CurrentOrder()->GetGoalPos() == point);
	for (int i = 0; i != 200 && Dist(footman->tilePos, archer.tilePos) > 6; ++i) {
		w.Step(*footman);
		REQUIRE(footman->CurrentAction() == UnitAction::Attack);
		REQUIRE_FALSE(footman->CurrentOrder()->HasGoal());
	}
	REQUIRE(Dist(footman->tilePos, archer.tilePos) <= 6);
	archer.VisCount[0] = 1; // now in its sight
	CHECK(StepUntilGoal(w, *footman) == &archer);

	ShoreWorld::Kill(archer);
	StepUntilStill(w, *footman);
	CHECK(footman->CurrentAction() == UnitAction::Still);
	CHECK(footman->tilePos == point);
	CHECK_FALSE(footman->SavedOrderIsMemory());
}

TEST_CASE("idle ship, ORIG47 C7 / run 49 K2: it takes an enemy building in its attack range and fires at it")
{
	ShoreWorld w;
	CUnit &ship = w.Ship(ShipTile);
	CUnit &farm = w.Enemy(w.farm, {56, 49}); // d 3
	REQUIRE(ship.CurrentAction() == UnitAction::Still);

	w.Steps(ship, 3);
	REQUIRE(ship.CurrentAction() == UnitAction::Attack);
	CHECK(ship.CurrentOrder()->GetGoal() == &farm);
	w.Steps(ship, 10);
	CHECK(ship.CurrentOrder()->GetGoal() == &farm);
	CHECK(ship.tilePos == ShipTile);
}

namespace
{
struct LuaFixture
{
	LuaFixture() { InitLua(); }
	~LuaFixture()
	{
		lua_close(Lua);
		Lua = nullptr;
	}
};

std::string ReadAndRemove(const std::filesystem::path &path)
{
	std::ifstream in(path);
	std::stringstream ss;
	ss << in.rdbuf();
	in.close();
	std::filesystem::remove(path);
	return ss.str();
}

std::unique_ptr<COrder> ParseOrderText(const std::string &body, CUnit &unit)
{
	const std::string chunk = "return " + body;
	REQUIRE(luaL_loadstring(Lua, chunk.c_str()) == 0);
	REQUIRE(lua_pcall(Lua, 0, 1, 0) == 0);
	std::unique_ptr<COrder> order = CclParseOrder(Lua, unit);
	lua_pop(Lua, 1);
	return order;
}

std::string OrderText(const COrder &order, const CUnit &unit)
{
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "ship_hit_order_test.lua";
	{
		CFile file;
		REQUIRE(file.open(path.string().c_str(), CL_OPEN_WRITE) != -1);
		order.Save(file, unit);
		file.close();
	}
	return ReadAndRemove(path);
}
} // namespace


TEST_CASE("ship attack-move: a save from an older build with ship-fight / last-attacker still loads")
{
	LuaFixture lua;
	ShoreWorld w;
	CUnit &ship = w.Ship(ShipTile);
	w.Order(ship, COrder::NewActionAttack(ship, SeaPoint));

	const std::string text = OrderText(*ship.CurrentOrder(), ship);
	CHECK(text.find("ship-fight") == std::string::npos);

	// the key of the removed fight state is read and ignored; the keys after it still count
	auto old = ParseOrderText("{\"action-attack\", \"range\", 0, \"min-range\", 0, \"tile\", {20, 60},"
	                          " \"amove-tile\", {20, 60}, \"goal-pulled\", 1, \"ship-fight\", 1,"
	                          " \"state\", 1}",
	                          ship);
	REQUIRE(old != nullptr);
	REQUIRE(old->Action == UnitAction::Attack);
	CHECK(old->GetGoalPos() == SeaPoint);
	CHECK(AsAttack(*old).GetAttackMovePos() == SeaPoint);
	CHECK(AsAttack(*old).GetGoalPulled());

	// a unit is written without a last attacker
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "ship_hit_unit_test.lua";
	{
		CFile file;
		REQUIRE(file.open(path.string().c_str(), CL_OPEN_WRITE) != -1);
		SaveUnit(ship, file);
		file.close();
	}
	CHECK(ReadAndRemove(path).find("last-attacker") == std::string::npos);
}

TEST_CASE("ship attack-move: the hold's kill cycle is saved and read back")
{
	LuaFixture lua;
	ShoreWorld w;
	CUnit &ship = w.Ship(ShipTile);
	auto order = ParseOrderText("{\"action-attack\", \"range\", 0, \"min-range\", 0, \"tile\", {55, 48},"
	                            " \"amove-tile\", {20, 60}, \"goal-pulled\", 0, \"kill-seen-at\", 1234,"
	                            " \"state\", 1}",
	                            ship);
	REQUIRE(order != nullptr);
	const std::string text = OrderText(*order, ship);
	CHECK(text.find("\"kill-seen-at\", 1234,") != std::string::npos);

	w.Order(ship, COrder::NewActionAttack(ship, SeaPoint));
	CHECK(OrderText(*ship.CurrentOrder(), ship).find("kill-seen-at") == std::string::npos);
}

// COMPUTER-HOME-LEASH, the remembered attacker (UNIT_RULES_IMPL.md section 5; PASSIVE_AI
// EVIDENCE-PAI-007, 0x495C90): a hit stores the attacker unless a living one is remembered, and
// only a hit that stores it calls the idle friends; friends that remember a living attacker of
// their own are not called. On land rows of the shore map; player 1 is a computer.

#include "home_leash.h"

namespace
{
struct GateWorld
{
	GateWorld()
	{
		Players[1].Type = PlayerTypes::PlayerComputer;
		farm = &Place(w.farm, {20, 20}, 1);
		guard = &Place(w.footman, {23, 20}, 1);
		a = &Attacker({20, 17});
		b = &Attacker({17, 20});
	}

	CUnit &Place(const CUnitType &type, const Vec2i &pos, int player)
	{
		CUnit *unit = MakeUnitAndPlace(pos, type, &Players[player]);
		REQUIRE(unit != nullptr);
		return *unit;
	}
	/// An attacker of player 0 that player 1 sees.
	CUnit &Attacker(const Vec2i &pos)
	{
		CUnit &unit = Place(w.footman, pos, 0);
		unit.VisCount[1] = 1;
		return unit;
	}
	/// The guard stands idle again, nothing kept.
	void Idle(CUnit &unit)
	{
		w.Order(unit, COrder::NewActionStill());
		unit.SavedOrder = nullptr;
	}
	static CUnit *GoalOf(const CUnit &unit)
	{
		// A command is queued behind the standing order until the next cycle.
		const COrder &last = *unit.Orders.back();
		return last.Action == UnitAction::Attack ? last.GetGoal() : nullptr;
	}

	ShoreWorld w;
	CUnit *farm = nullptr;
	CUnit *guard = nullptr;
	CUnit *a = nullptr;
	CUnit *b = nullptr;
};

/// The Lua `Unit(slot, {...})` of a saved game, on a unit that exists.
bool RunUnitLine(const std::string &line)
{
	if (luaL_loadstring(Lua, line.c_str()) != 0) {
		lua_pop(Lua, 1);
		return false;
	}
	const bool ok = lua_pcall(Lua, 0, 0, 0) == 0;
	if (!ok) {
		lua_pop(Lua, 1);
	}
	return ok;
}

std::string SavedUnitText(const CUnit &unit)
{
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "attacker_gate_unit_test.lua";
	{
		CFile file;
		REQUIRE(file.open(path.string().c_str(), CL_OPEN_WRITE) != -1);
		SaveUnit(unit, file);
		file.close();
	}
	return ReadAndRemove(path);
}
} // namespace

TEST_CASE("attacker gate: two hits by the same living attacker call the friends once")
{
	GateWorld g;

	HitUnit(g.a, *g.farm, 1);
	CHECK(g.farm->LastAttacker.get() == g.a);
	REQUIRE(GateWorld::GoalOf(*g.guard) == g.a);

	g.Idle(*g.guard);
	HitUnit(g.a, *g.farm, 1);
	CHECK(g.guard->IsIdle());
	// another attacker while the first lives: no store, no call
	HitUnit(g.b, *g.farm, 1);
	CHECK(g.guard->IsIdle());
	CHECK(g.farm->LastAttacker.get() == g.a);
}

TEST_CASE("attacker gate: after the remembered attacker's death a new hit stores and calls again")
{
	GateWorld g;

	HitUnit(g.a, *g.farm, 1);
	REQUIRE(GateWorld::GoalOf(*g.guard) == g.a);
	g.Idle(*g.guard);

	ShoreWorld::Kill(*g.a);
	REQUIRE_FALSE(g.a->IsAlive());
	CHECK(RememberedAttacker(*g.farm) == nullptr); // a dead one is forgotten
	CHECK(g.farm->LastAttacker.get() == nullptr);

	HitUnit(g.b, *g.farm, 1);
	CHECK(g.farm->LastAttacker.get() == g.b);
	CHECK(GateWorld::GoalOf(*g.guard) == g.b);
}

TEST_CASE("attacker gate: a friend that remembers a living attacker is not called; a dead one is forgotten")
{
	GateWorld g;
	CUnit &c = g.Attacker({26, 20});

	g.guard->LastAttacker = &c;
	HitUnit(g.a, *g.farm, 1);
	CHECK(g.farm->LastAttacker.get() == g.a); // stored all the same
	CHECK(g.guard->IsIdle());
	CHECK(g.guard->LastAttacker.get() == &c);

	// its own attacker dies: the next call takes it
	ShoreWorld::Kill(c);
	ShoreWorld::Kill(*g.a);
	HitUnit(g.b, *g.farm, 1);
	CHECK(g.farm->LastAttacker.get() == g.b);
	CHECK(GateWorld::GoalOf(*g.guard) == g.b);
	CHECK(g.guard->LastAttacker.get() == nullptr);
}

TEST_CASE("attacker gate: the memory holds a reference; RememberAttacker refuses while one lives")
{
	GateWorld g;
	const int refs = g.a->Refs;

	CHECK(RememberAttacker(*g.guard, *g.a));
	CHECK(g.a->Refs == refs + 1);
	CHECK_FALSE(RememberAttacker(*g.guard, *g.b));
	CHECK(g.guard->LastAttacker.get() == g.a);
	g.guard->LastAttacker = nullptr;
	CHECK(g.a->Refs == refs);
}

TEST_CASE("attacker gate: the leash's walk home forgets the attacker")
{
	GateWorld g;
	CUnit &guard = *g.guard;

	g.w.footman.ReactRangeComputer = 6; // limit 6 / 2 + 4 = 7 tiles
	guard.MapFixed = 1;
	guard.HomeRole = HomeRoleGuard;
	guard.HomePos = Vec2i(40, 20); // 17 tiles away
	guard.LastAttacker = g.a;
	REQUIRE(UnitHeldByHome(guard));

	UnitsHomeLeashPass({&guard});
	CHECK(guard.Orders.back()->Action == UnitAction::Move);
	CHECK(guard.LastAttacker.get() == nullptr);
}

TEST_CASE("attacker gate: saved as a unit reference and read back; a save without it remembers none")
{
	LuaFixture lua;
	UnitCclRegister();
	GateWorld g;

	CHECK(SavedUnitText(*g.farm).find("remembered-attacker") == std::string::npos);
	CHECK(RememberedAttackerText(*g.farm).empty());

	g.farm->LastAttacker = g.a;
	const std::string part = RememberedAttackerText(*g.farm);
	CHECK(part == " \"remembered-attacker\", \"" + UnitReference(*g.a) + "\",");
	CHECK(SavedUnitText(*g.farm).find(part) != std::string::npos);

	// read back onto the unit: the same attacker
	g.farm->LastAttacker = nullptr;
	const std::string slot = std::to_string(UnitNumber(*g.farm));
	REQUIRE(RunUnitLine("Unit(" + slot + ", {" + part + "})"));
	CHECK(g.farm->LastAttacker.get() == g.a);

	// an older save: no such key (and the removed ship key is still read and ignored)
	g.farm->LastAttacker = nullptr;
	REQUIRE(RunUnitLine("Unit(" + slot + ", {\"home-role\", 0,})"));
	CHECK(g.farm->LastAttacker.get() == nullptr);
	REQUIRE(RunUnitLine("Unit(" + slot + ", {\"last-attacker\", \"" + UnitReference(*g.a) + "\",})"));
	CHECK(g.farm->LastAttacker.get() == nullptr);
	// a reference to no unit is refused, as the other references are
	CHECK_FALSE(RunUnitLine("Unit(" + slot + ", {\"remembered-attacker\", \"UFFFF\",})"));
}

TEST_CASE("attacker gate: a farm forgets its attacker one cycle after the hit (live run 72)")
{
	// Run 72 A: a farm's +0x54 was set on every hit and cleared 1-3 frames later (5 farms, 13 / 13);
	// a second attacker was stored at once (a6). The minimum, 1 cycle, is taken.
	GateWorld g;

	HitUnit(g.a, *g.farm, 1);
	REQUIRE(GateWorld::GoalOf(*g.guard) == g.a);
	g.Idle(*g.guard);
	// the same cycle: still remembered, a second hit stores nothing and calls nobody
	HitUnit(g.b, *g.farm, 1);
	CHECK(g.farm->LastAttacker.get() == g.a);
	CHECK(g.guard->IsIdle());

	// the next cycle: forgotten, the same living attacker's hit is a fresh hit and calls again
	++GameCycle;
	CHECK(RememberedAttacker(*g.farm) == nullptr);
	HitUnit(g.a, *g.farm, 1);
	CHECK(g.farm->LastAttacker.get() == g.a);
	CHECK(GateWorld::GoalOf(*g.guard) == g.a);

	// a second attacker a cycle later is stored at once (a6: 1594, 1594, then 1584 stored)
	++GameCycle;
	HitUnit(g.b, *g.farm, 1);
	CHECK(g.farm->LastAttacker.get() == g.b);
}

TEST_CASE("attacker gate: a fighter keeps its attacker while it lives (the farm rule is for buildings only)")
{
	GateWorld g;

	HitUnit(g.a, *g.guard, 1);
	CHECK(g.guard->LastAttacker.get() == g.a);
	GameCycle += 200;
	CHECK(RememberedAttacker(*g.guard) == g.a);
}

TEST_CASE("HelpCall: run 72 farm hit - a map-placed Grunt 8 tiles off is not called, a free one is")
{
	// Run 72 a4-a6: idle Grunt 1543 ~8 / ~12 / ~17 tiles from the struck farm never moved (3 hits
	// series, 0 calls). Our call: 13-tile box, but a unit fixed by the map only from 4 tiles
	// (PASSIVE_AI section 3); a map-placed Grunt 8+ tiles off is not called.
	GateWorld g;
	CUnit &placed = g.Place(g.w.footman, {28, 20}, 1);
	CUnit &free = g.Place(g.w.footman, {20, 28}, 1);
	placed.MapFixed = 1;
	g.Idle(*g.guard);
	g.guard->MapFixed = 1; // 3 tiles: still called

	HitUnit(g.a, *g.farm, 1);
	CHECK(placed.IsIdle());
	CHECK(GateWorld::GoalOf(free) == g.a);
	CHECK(GateWorld::GoalOf(*g.guard) == g.a);
}

TEST_CASE("HelpCall: run 73 the person's struck farm / Peasant - idle own units 5-15 tiles off do not come")
{
	// Run 73 H: the person's Farm hit 62 times by a Grunt (normal damage, one hit per 25 frames), 2 idle
	// own units at 5, 8, 10 and 13 tiles from its edge: none moved or changed order. A struck Peasant
	// (~54 hits), the same 8 units at 7-15 tiles: none came. Ours: a person's call reaches 2 tiles (4
	// for a struck ship; static decode, BATCH_B1_C / PASSIVE_AI s.3) - these units are outside it. The
	// 2-tile call itself was not tried live (no helper nearer than 5); it stays.
	GateWorld g;
	Players[1].Type = PlayerTypes::PlayerPerson;
	g.Idle(*g.guard); // 3 tiles off: outside the person's 2 tiles
	std::vector<CUnit *> helpers;
	for (const int d : {5, 8, 10, 13}) {
		helpers.push_back(&g.Place(g.w.footman, {20 + d, 20}, 1));
		helpers.push_back(&g.Place(g.w.footman, {20, 20 + d}, 1));
	}
	for (int hit = 0; hit != 8; ++hit) {
		HitUnit(g.a, *g.farm, 1);
		GameCycle += 25;
	}
	for (const CUnit *h : helpers) {
		CHECK(h->IsIdle());
		CHECK(h->Orders.size() == 1);
	}
	CHECK(g.guard->IsIdle());

	// A struck worker of the person (a Footman type stands in; the call does not look at the struck type
	// but for ships), the helpers 7-15 tiles off.
	CUnit &peasant = g.Place(g.w.footman, {30, 30}, 1);
	std::vector<CUnit *> far;
	for (const int d : {7, 10, 12, 15}) {
		far.push_back(&g.Place(g.w.footman, {30 + d, 30}, 1));
		far.push_back(&g.Place(g.w.footman, {30, 30 + d}, 1));
	}
	for (int hit = 0; hit != 8; ++hit) {
		HitUnit(g.a, peasant, 1);
		GameCycle += 25;
	}
	for (const CUnit *h : far) {
		CHECK(h->IsIdle());
		CHECK(h->Orders.size() == 1);
	}

	// The kept static rule: one 2 tiles off is called.
	CUnit &near = g.Place(g.w.footman, {22, 20}, 1);
	GameCycle += 25;
	HitUnit(g.a, *g.farm, 1);
	CHECK(GateWorld::GoalOf(near) == g.a);
}

TEST_CASE("HelpCall: run 75 the person's call - struck farm 3 / 5 tiles none, struck Footman 1 tile yes, 3 no, no worker")
{
	// Run 75 (live, Human II): the person's Farm hit by a Troll from 4 tiles (god mode on and off), idle
	// units 5 tiles from its edge (84 hits) and 3 tiles (2 Footmen, the last 46 hits), 8-10 from the
	// Troll: none came. A struck
	// Footman: the 2 idle Footmen 1 tile off took the attacker in the hit's own frame; a Footman 3 tiles
	// off (8 hits) and a Peasant 1 tile off did not move. The struck Peasant then called the Footman 2
	// tiles off in the hit's frame. Farm helpers 1-2 tiles off could not be set up live: the computer's
	// attacker turns on any unit coming within 6-7 tiles. Ours already does all of it: 2 tiles for a
	// person, no harvester answers; no change.
	GateWorld g;
	Players[1].Type = PlayerTypes::PlayerPerson;
	g.Idle(*g.guard); // 3 tiles off the farm
	CUnit &d3 = g.Place(g.w.footman, {20, 23}, 1);
	CUnit &d5a = g.Place(g.w.footman, {25, 20}, 1);
	CUnit &d5b = g.Place(g.w.footman, {20, 25}, 1);
	for (int hit = 0; hit != 8; ++hit) {
		HitUnit(g.a, *g.farm, 1);
		GameCycle += 65;
	}
	for (const CUnit *h : {g.guard, &d3, &d5a, &d5b}) {
		CHECK(h->IsIdle());
		CHECK(h->Orders.size() == 1);
	}

	// A worker of the person: the archer type stands in, marked as a harvester.
	g.w.archer.BoolFlag[HARVESTER_INDEX].value = true;
	CUnit &struck = g.Place(g.w.footman, {30, 30}, 1);
	CUnit &below = g.Place(g.w.footman, {30, 31}, 1);
	CUnit &diagonal = g.Place(g.w.footman, {29, 31}, 1);
	CUnit &three = g.Place(g.w.footman, {33, 30}, 1);
	CUnit &worker = g.Place(g.w.archer, {31, 31}, 1);
	HitUnit(g.b, struck, 1);
	CHECK(GateWorld::GoalOf(below) == g.b);
	CHECK(GateWorld::GoalOf(diagonal) == g.b);
	CHECK(three.IsIdle());
	CHECK(three.Orders.size() == 1);
	CHECK(worker.IsIdle());
	CHECK(worker.Orders.size() == 1);

	// The struck worker calls a Footman 2 tiles off.
	CUnit &peasant = g.Place(g.w.archer, {40, 40}, 1);
	CUnit &two = g.Place(g.w.footman, {42, 41}, 1);
	HitUnit(g.b, peasant, 1);
	CHECK(GateWorld::GoalOf(two) == g.b);
}
