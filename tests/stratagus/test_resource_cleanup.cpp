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

#include <doctest.h>

#include "action/action_attack.h"
#include "action/action_repair.h"
#include "action/action_resource.h"
#include "action/action_spellcast.h"

#include "actions.h"
#include "commands.h"
#include "map.h"
#include "spells.h"
#include "stratagus.h"
#include "unit.h"
#include "unittype.h"
#include "upgrade_structs.h"
#include "wc2ai.h"

struct ResourceOrderTestAccess
{
	static void DropResource(COrder_Resource &order, CUnit &unit)
	{
		order.DropResource(unit);
	}

	static CUnit *Mine(COrder_Resource &order)
	{
		return order.Resource.Mine;
	}
};

namespace {

void PrepareUnit(CUnit &unit, CUnitType &type, CPlayer &player)
{
	unit.Type = &type;
	unit.Player = &player;
	unit.Refs = 1;
	unit.Orders.push_back(COrder::NewActionStill());
}

}

TEST_CASE("Resource order destructor deassigns live mine workers")
{
	CPlayer player;
	player.Index = 0;
	CUnitType workerType;
	CUnitType mineType;
	mineType.TileWidth = 1;
	mineType.TileHeight = 1;
	mineType.GivesResource = GoldCost;
	CUnit worker;
	CUnit mine;
	PrepareUnit(worker, workerType, player);
	PrepareUnit(mine, mineType, player);

	{
		auto order = COrder::NewActionResource(worker, mine);
		worker.AssignWorkerToMine(mine);

		REQUIRE(mine.Resource.AssignedWorkers.size() == 1);
		CHECK(mine.Resource.AssignedWorkers.front() == &worker);
		CHECK(worker.Refs == 3);
		CHECK(mine.Refs == 3);
	}

	CHECK(mine.Resource.AssignedWorkers.empty());
	CHECK(worker.Refs == 1);
	CHECK(mine.Refs == 1);
}

TEST_CASE("Dropping carried resources clears mine assignment")
{
	CPlayer player;
	player.Index = 0;
	CUnitType workerType;
	CUnitType mineType;
	auto goldInfo = std::make_unique<ResourceInfo>();
	goldInfo->TerrainHarvester = false;
	workerType.ResInfo[GoldCost] = std::move(goldInfo);
	mineType.TileWidth = 1;
	mineType.TileHeight = 1;
	mineType.GivesResource = GoldCost;
	CUnit worker;
	CUnit mine;
	PrepareUnit(worker, workerType, player);
	PrepareUnit(mine, mineType, player);

	auto baseOrder = COrder::NewActionResource(worker, mine);
	auto &order = static_cast<COrder_Resource &>(*baseOrder);
	worker.AssignWorkerToMine(mine);
	worker.CurrentResource = GoldCost;
	worker.ResourcesHeld = 100;

	ResourceOrderTestAccess::DropResource(order, worker);

	CHECK(worker.CurrentResource == 0);
	CHECK(worker.ResourcesHeld == 0);
	CHECK(mine.Resource.AssignedWorkers.empty());
	CHECK(ResourceOrderTestAccess::Mine(order) == nullptr);
	CHECK(worker.Refs == 2);
	CHECK(mine.Refs == 2);
}

TEST_CASE("carry picture: wood is shown outside the gathering state")
{
	CHECK(HarvesterShowsLoad(true, 40, true, 5));     // walking to the next tree
	CHECK(HarvesterShowsLoad(true, 40, true, 31));    // the tree cannot be reached
	CHECK_FALSE(HarvesterShowsLoad(true, 40, true, 60)); // swinging at the tree
	CHECK_FALSE(HarvesterShowsLoad(true, 100, true, 60));
	CHECK(HarvesterShowsLoad(true, 40, true, 65));
	CHECK(HarvesterShowsLoad(true, 40, true, 70));    // walking home
	CHECK_FALSE(HarvesterShowsLoad(true, 0, true, 70));
	CHECK(HarvesterShowsLoad(true, 40, false, 60));   // another order: the state is not read
	CHECK(HarvesterShowsLoad(false, 100, true, 5));
	CHECK_FALSE(HarvesterShowsLoad(false, 0, true, 60));
}

TEST_CASE("interrupted chop: the wood cut so far is lost (live run 36, one interrupt)")
{
	CHECK(InterruptedChopLoss(true, 40, 60, false) == 40);  // sent away mid-chop: nothing carried
	CHECK(InterruptedChopLoss(true, 2, 60, false) == 2);
	CHECK(InterruptedChopLoss(true, 0, 60, false) == 0);
	CHECK(InterruptedChopLoss(true, 100, 60, true) == 0);  // the load is complete: it is carried
	CHECK(InterruptedChopLoss(true, 60, 60, true) == 0);   // the tile fell under the axe: carried
	CHECK(InterruptedChopLoss(true, 40, 55, false) == 0);  // not yet swinging
	CHECK(InterruptedChopLoss(true, 40, 5, false) == 0);   // walking to the next tree with a load
	CHECK(InterruptedChopLoss(true, 40, 65, false) == 0);  // the chop is over
	CHECK(InterruptedChopLoss(true, 40, 70, false) == 0);  // walking home
	CHECK(InterruptedChopLoss(false, 40, 60, false) == 0); // gold is not chopped
}

TEST_CASE("loaded worker sent onto the other resource: refused, load kept (live run 43, item 1)")
{
	CHECK(HarvestOrderRefused(100, GoldCost, WoodCost));  // gold in hand, sent to a tree: no chop
	CHECK(HarvestOrderRefused(100, WoodCost, GoldCost));  // lumber in hand, sent to a mine (by symmetry)
	CHECK(HarvestOrderRefused(40, WoodCost, GoldCost));   // a part load too
	CHECK_FALSE(HarvestOrderRefused(0, 0, WoodCost));     // empty hands: harvests
	CHECK_FALSE(HarvestOrderRefused(0, GoldCost, WoodCost));
	CHECK_FALSE(HarvestOrderRefused(40, WoodCost, WoodCost)); // the same kind: goes on chopping
	CHECK_FALSE(HarvestOrderRefused(100, GoldCost, GoldCost));
}

TEST_CASE("per-trip amounts: base, hall tier, lumber mill (0x4B27E0; live runs 44 and timing)")
{
	CHECK(GoldPerTrip(0, 0) == 100);   // Town Hall / Great Hall
	CHECK(GoldPerTrip(0, 1) == 110);   // Keep / Stronghold (Tides Human XIII: +110 a trip)
	CHECK(GoldPerTrip(1, 0) == 120);   // Castle / Fortress
	CHECK(GoldPerTrip(1, 2) == 120);   // the best hall counts, no sum
	CHECK(GoldPerTrip(0, 3) == 110);
	CHECK(LumberPerTrip(0) == 100);
	CHECK(LumberPerTrip(1) == 125);    // Lumber Mill (Human XIII: +125 a trip)
	CHECK(LumberPerTrip(2) == 125);
	CHECK(OilPerTrip(0) == 100);
	CHECK(OilPerTrip(1) == 125);       // Refinery

	// A delivery credits the trip's amount, not the load scaled.
	CHECK(DeliveredPerTrip(100, GoldPerTrip(0, 1)) == 110);
	CHECK(DeliveredPerTrip(100, LumberPerTrip(1)) == 125);
	CHECK(DeliveredPerTrip(40, GoldPerTrip(0, 0)) == 100);  // the last gold of a mine: full trip
	CHECK(DeliveredPerTrip(1, LumberPerTrip(1)) == 125);
	CHECK(DeliveredPerTrip(0, GoldPerTrip(1, 0)) == 0);     // empty hands: nothing
}

TEST_CASE("command card: no Harvest button while a load is held (live run 43, item 1)")
{
	CHECK(HarvestButtonShown(0));
	CHECK_FALSE(HarvestButtonShown(100));
	CHECK_FALSE(HarvestButtonShown(40));
}

TEST_CASE("repair: a worker that starts to repair drops its load, without credit")
{
	CPlayer player;
	player.Index = 0;
	player.Resources[GoldCost] = 500;
	CUnitType workerType;
	CUnit worker;
	PrepareUnit(worker, workerType, player);
	worker.CurrentResource = GoldCost;
	worker.ResourcesHeld = 100;

	COrder_Repair::DropLoad(worker);

	CHECK(worker.ResourcesHeld == 0);
	CHECK(worker.CurrentResource == 0);
	CHECK(player.Resources[GoldCost] == 500);
	// an empty worker stays empty
	COrder_Repair::DropLoad(worker);
	CHECK(worker.ResourcesHeld == 0);
}

TEST_CASE("cast's end: the saved patrol is re-issued when its counter is 0, kept while it runs")
{
	CPlayer player;
	player.Index = 0;
	CUnitType casterType;
	CUnit caster;
	PrepareUnit(caster, casterType, player);
	GameCycle = 1000;

	// Counter running (re-issued 20 cycles ago): the patrol stays saved, the unit stands.
	caster.SavedOrder = COrder::NewActionPatrol(Vec2i(2, 2), Vec2i(9, 9));
	CHECK(caster.SavedOrder->ReissueReady(caster, 980) == wc2ai::Reissue::Ready);
	COrder_SpellCast cooling(true);
	CHECK_FALSE(cooling.RestoreAfterCast(caster));
	REQUIRE(caster.SavedOrder != nullptr);
	CHECK(caster.SavedOrder->Action == UnitAction::Patrol);
	CHECK(caster.Orders.size() == 1);

	// Counter at 0 (40 cycles later): the same patrol is restored into the order list.
	GameCycle = 1020;
	COrder_SpellCast ready(true);
	CHECK(ready.RestoreAfterCast(caster));
	CHECK(caster.SavedOrder == nullptr);
	REQUIRE(caster.Orders.size() == 2);
	CHECK(caster.Orders[1]->Action == UnitAction::Patrol);
	// and the re-issue started a new counter of 40
	CHECK(caster.Orders[1]->ReissueReady(caster, 1059) == wc2ai::Reissue::Wait);
	CHECK(caster.Orders[1]->ReissueReady(caster, 1060) == wc2ai::Reissue::Ready);
	GameCycle = 0;
}

TEST_CASE("cast's end: a saved attack-move is sent again from 2 tiles, kept while its counter runs, forgotten within 1")
{
	const int oldWidth = Map.Info.MapWidth;
	const int oldHeight = Map.Info.MapHeight;
	Map.Info.MapWidth = 16;
	Map.Info.MapHeight = 16;
	Map.Create();
	CPlayer player;
	player.Index = 0;
	CUnitType casterType;
	casterType.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
	CUnitStats stats;
	stats.Variables.resize(ATTACKRANGE_INDEX + 1);
	CUnit caster;
	PrepareUnit(caster, casterType, player);
	caster.Stats = &stats;
	GameCycle = 1000;

	// 3 tiles from the point, counter at 0: restored, counter 20.
	caster.tilePos = Vec2i(5, 5);
	caster.SavedOrder = COrder::NewActionAttack(caster, Vec2i(8, 5));
	REQUIRE(caster.SavedOrder->Action == UnitAction::Attack);
	COrder_SpellCast first(true);
	CHECK(first.RestoreAfterCast(caster));
	CHECK(caster.SavedOrder == nullptr);
	REQUIRE(caster.Orders.size() == 2);
	CHECK(caster.Orders[1]->Action == UnitAction::Attack);

	// The same attack-move saved again 10 cycles later: the counter runs, the unit stands, it is kept.
	caster.SavedOrder = std::move(caster.Orders[1]);
	caster.Orders.pop_back();
	GameCycle = 1010;
	COrder_SpellCast cooling(true);
	CHECK_FALSE(cooling.RestoreAfterCast(caster));
	REQUIRE(caster.SavedOrder != nullptr);
	CHECK(caster.Orders.size() == 1);
	CHECK(caster.SavedOrderIsMemory());

	// Counter at 0 (cycle 1020) but the unit now stands 1 tile from the point: forgotten.
	GameCycle = 1020;
	caster.tilePos = Vec2i(7, 6);
	COrder_SpellCast near(true);
	CHECK_FALSE(near.RestoreAfterCast(caster));
	CHECK(caster.SavedOrder == nullptr);
	CHECK(caster.Orders.size() == 1);

	// A saved attack on a unit is not an attack-move memory: put back at once.
	CUnit enemy;
	PrepareUnit(enemy, casterType, player);
	enemy.tilePos = Vec2i(6, 6);
	caster.SavedOrder = COrder::NewActionAttack(caster, enemy);
	CHECK_FALSE(caster.SavedOrderIsMemory());
	CHECK(caster.SavedOrder->ReissueReady(caster, 1020) == wc2ai::Reissue::Ready);
	caster.SavedOrder = nullptr;

	GameCycle = 0;
	Map.Fields.clear();
	Map.Info.MapWidth = oldWidth;
	Map.Info.MapHeight = oldHeight;
}

TEST_CASE("blocked attack-move: idle with the memory, no timed retry; sent again only at a fight's end (ORIG46)")
{
	const int oldWidth = Map.Info.MapWidth;
	const int oldHeight = Map.Info.MapHeight;
	Map.Info.MapWidth = 16;
	Map.Info.MapHeight = 16;
	Map.Create();
	CPlayer player;
	player.Index = 0;
	CUnitType knightType;
	knightType.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
	CUnitStats stats;
	stats.Variables.resize(ATTACKRANGE_INDEX + 1);
	CUnit knight;
	PrepareUnit(knight, knightType, player);
	knight.Stats = &stats;
	GameCycle = 1000;

	// The path fails 7 tiles from the point: the order ends at once, no wait, the attack-move is the memory.
	knight.tilePos = Vec2i(5, 5);
	auto order = COrder::NewActionAttack(knight, Vec2i(12, 5));
	REQUIRE(order->Action == UnitAction::Attack);
	auto &attackMove = static_cast<COrder_Attack &>(*order);
	REQUIRE(attackMove.IsRememberedAttackMove());
	attackMove.EndBlockedAttackMove(knight);
	CHECK(attackMove.Finished);
	CHECK(knight.Wait == 0);
	REQUIRE(knight.SavedOrder != nullptr);
	CHECK(knight.SavedOrderIsMemory());
	// The counter is at 0: a fight's end much later sends it again (and starts the counter of 20).
	GameCycle = 2200;
	CHECK(knight.RestoreOrder());
	CHECK(knight.SavedOrder == nullptr);
	REQUIRE(knight.Orders.size() == 2);
	CHECK(knight.Orders[1]->Action == UnitAction::Attack);
	CHECK(knight.Orders[1]->ReissueReady(knight, 2219) == wc2ai::Reissue::Wait);
	CHECK(knight.Orders[1]->ReissueReady(knight, 2220) == wc2ai::Reissue::Ready);
	knight.Orders.pop_back();
	knight.Orders[0]->Finished = false;

	// Blocked within 1 tile of the point: done, nothing remembered.
	knight.tilePos = Vec2i(11, 6);
	auto near = COrder::NewActionAttack(knight, Vec2i(12, 5));
	static_cast<COrder_Attack &>(*near).EndBlockedAttackMove(knight);
	CHECK(near->Finished);
	CHECK(knight.Wait == 0);
	CHECK(knight.SavedOrder == nullptr);

	GameCycle = 0;
	Map.Fields.clear();
	Map.Info.MapWidth = oldWidth;
	Map.Info.MapHeight = oldHeight;
}

TEST_CASE("cast with an unreachable target: the order finishes, the saved patrol stays saved")
{
	CPlayer player;
	player.Index = 0;
	CUnitType casterType; // no move animation: cannot move
	CUnit caster;
	PrepareUnit(caster, casterType, player);
	caster.Variable.resize(MANA_INDEX + 1);
	caster.SpellCoolDownTimers.resize(1);
	caster.tilePos = Vec2i(0, 0);
	SpellType spell(0, "spell-test-unreachable");
	spell.Range = 2;
	spell.Target = ETarget::Position;
	GameCycle = 1000;

	caster.SavedOrder = COrder::NewActionPatrol(Vec2i(2, 2), Vec2i(9, 9));
	caster.Orders.clear();
	caster.Orders.push_back(COrder::NewActionSpellCast(spell, Vec2i(10, 10), nullptr, false));
	caster.Orders[0]->Execute(caster);

	CHECK(caster.Orders[0]->Finished);
	CHECK(caster.Orders.size() == 1);
	REQUIRE(caster.SavedOrder != nullptr);
	CHECK(caster.SavedOrder->Action == UnitAction::Patrol);
	GameCycle = 0;
}

TEST_CASE("mine trips: a full 100 at each entry, the emptying entry collapses it (0x4B2AC6)")
{
	// 300 gold = three trips; the third entry collapses the mine.
	MineTrip trip = MineTripTake(300, 100);
	CHECK(trip.load == 100);
	CHECK(trip.stock == 200);
	CHECK_FALSE(trip.collapse);
	trip = MineTripTake(trip.stock, 100);
	CHECK(trip.load == 100);
	CHECK(trip.stock == 100);
	CHECK_FALSE(trip.collapse);
	trip = MineTripTake(trip.stock, 100);
	CHECK(trip.load == 100);
	CHECK(trip.stock == 0);
	CHECK(trip.collapse);

	// No part load: a stock of 1 still gives a full trip.
	trip = MineTripTake(1, 100);
	CHECK(trip.load == 100);
	CHECK(trip.stock == 0);
	CHECK(trip.collapse);

	// A remainder below a full trip counts as one more trip: 150 = two trips.
	trip = MineTripTake(150, 100);
	CHECK(trip.load == 100);
	CHECK(trip.stock == 50);
	CHECK_FALSE(trip.collapse);
	trip = MineTripTake(trip.stock, 100);
	CHECK(trip.load == 100);
	CHECK(trip.collapse);

	// Nothing left: nothing taken, no second collapse.
	trip = MineTripTake(0, 100);
	CHECK(trip.load == 0);
	CHECK(trip.stock == 0);
	CHECK_FALSE(trip.collapse);

	// The mine loses 100 a trip whatever the credit: with a Keep the depot credits 110.
	CHECK(MineTripTake(2500, 100).stock == 2400);
	CHECK(DeliveredPerTrip(MineTripTake(2500, 100).load, GoldPerTrip(0, 1)) == 110);
}

TEST_CASE("builder finishing a depot: its load is credited on the spot or lost (0x4D6E2F)")
{
	// Gold into a new Town Hall: the full trip, the builder holds nothing after.
	BuilderLoad load = BuilderFinishLoad(100, true, true, 100);
	CHECK(load.credited == 100);
	CHECK(load.emptied);
	// Lumber into a new Lumber Mill: the mill already counts (125).
	load = BuilderFinishLoad(100, true, true, LumberPerTrip(1));
	CHECK(load.credited == 125);
	CHECK(load.emptied);
	// Gold into a new Lumber Mill: lost.
	load = BuilderFinishLoad(100, true, false, 100);
	CHECK(load.credited == 0);
	CHECK(load.emptied);
	// An empty builder credits nothing.
	load = BuilderFinishLoad(0, true, true, 100);
	CHECK(load.credited == 0);
	CHECK_FALSE(load.emptied);
	// A building that is no depot leaves the load alone.
	load = BuilderFinishLoad(100, false, false, 100);
	CHECK(load.credited == 0);
	CHECK_FALSE(load.emptied);
}

TEST_CASE("autocast: a person's unit never casts by itself, the computer's may (DECODE_B11_ROUND2 4)")
{
	CHECK_FALSE(wc2ai::UnitMayAutoCast(true));
	CHECK(wc2ai::UnitMayAutoCast(false));

	CUnitType casterType;
	CPlayer person;
	person.Index = 0;
	person.Type = PlayerTypes::PlayerPerson;
	CUnit personCaster;
	PrepareUnit(personCaster, casterType, person);
	personCaster.AutoCastSpell.assign(2, false);
	CPlayer computer;
	computer.Index = 1;
	computer.Type = PlayerTypes::PlayerComputer;
	CUnit computerCaster;
	PrepareUnit(computerCaster, casterType, computer);
	computerCaster.AutoCastSpell.assign(2, false);

	// The toggle command (button, network, replay) does not turn it on for a person's unit.
	CommandAutoSpellCast(personCaster, 1, 1);
	CHECK_FALSE(personCaster.AutoCastSpell[1]);
	CommandAutoSpellCast(computerCaster, 1, 1);
	CHECK(computerCaster.AutoCastSpell[1]);
	// A flag already on (an old save) is never used: the per-unit autocast refuses at once.
	personCaster.AutoCastSpell[1] = true;
	CHECK_FALSE(AutoCast(personCaster));
	// and it can still be switched off
	CommandAutoSpellCast(personCaster, 1, 0);
	CHECK_FALSE(personCaster.AutoCastSpell[1]);
}

TEST_CASE("autorepair: a person's worker never repairs by itself, the computer's flag stays (owner 2026-10-15)")
{
	CHECK_FALSE(wc2ai::UnitMayAutoRepair(true));
	CHECK(wc2ai::UnitMayAutoRepair(false));

	CUnitType workerType;
	CPlayer person;
	person.Index = 0;
	person.Type = PlayerTypes::PlayerPerson;
	CUnit personWorker;
	PrepareUnit(personWorker, workerType, person);
	CPlayer computer;
	computer.Index = 1;
	computer.Type = PlayerTypes::PlayerComputer;
	CUnit computerWorker;
	PrepareUnit(computerWorker, workerType, computer);

	// The toggle command (network, replay) does not turn it on for a person's unit.
	CommandAutoRepair(personWorker, 1);
	CHECK_FALSE(static_cast<bool>(personWorker.AutoRepair));
	CommandAutoRepair(computerWorker, 1);
	CHECK(static_cast<bool>(computerWorker.AutoRepair));
	// A flag already on (an old save) is never used: the auto-repair refuses at once.
	personWorker.AutoRepair = 1;
	CHECK_FALSE(AutoRepair(personWorker));
	// and it can still be switched off
	CommandAutoRepair(personWorker, 0);
	CHECK_FALSE(static_cast<bool>(personWorker.AutoRepair));
}
