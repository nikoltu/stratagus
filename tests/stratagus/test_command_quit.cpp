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
/**@name test_command_quit.cpp - The units of a player who is out of a network game. */
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

#include <doctest.h>

#include "stratagus.h"

#include "actions.h"
#include "commands.h"
#include "construct.h"
#include "map.h"
#include "player.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"

#include <memory>
#include <string>
#include <vector>

namespace
{

/**
**  A map of 32 x 32 fields and four players:
**  0 = the one who stays and fights, 1 = the one who goes, 2 = another one who stays (enemy of 0),
**  3 = the player of this device, who owns nothing.
**
**  No Lua, no fonts, no sound, no graphics: the unit types are filled in by hand and have no
**  animation and no corpse, so a unit which dies stands in its death order and is not put back on
**  the map.
*/
class QuitWorld
{
public:
	QuitWorld()
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		savedGameCycle = GameCycle;
		if (UnitManager == nullptr) {
			UnitManager = new CUnitManager;
		}
		UnitManager->Init();
		UnitTypeVar.Init();

		Map.Info.MapWidth = 32;
		Map.Info.MapHeight = 32;
		Map.Create();

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
		ThisPlayer = &Players[3];
		Players[0].SetDiplomacyEnemyWith(Players[2]);
		Players[2].SetDiplomacyEnemyWith(Players[0]);

		MakeType(worker, "unit-test-worker", 0, false, false, 30);
		MakeType(soldier, "unit-test-soldier", 1, false, true, 50);
		MakeType(transport, "unit-test-transport", 2, false, false, 50);
		transport.MaxOnBoard = 6;
		MakeType(tower, "unit-test-tower", 3, true, true, 95);
		MakeType(farm, "unit-test-farm", 4, true, false, 100);
		MakeType(barracks, "unit-test-barracks", 5, true, false, 160);
		barracks.Construction = &construction;
		construction.Frames.resize(1);

		// Player 1, the one who goes.
		units.push_back(MakeUnitAndPlace({2, 2}, worker, &Players[1]));
		units.push_back(MakeUnitAndPlace({4, 2}, soldier, &Players[1]));
		units.push_back(MakeUnitAndPlace({6, 2}, transport, &Players[1]));
		units.push_back(Inside(soldier, 1, *units[2], true));
		units.push_back(MakeUnitAndPlace({8, 2}, tower, &Players[1]));
		units.push_back(MakeUnitAndPlace({10, 2}, farm, &Players[1]));
		units.push_back(MakeUnitAndPlace({12, 2}, barracks, &Players[1]));
		units[6]->Orders[0] = COrder::NewActionTrain(*units[6], soldier);
		units.push_back(MakeUnitAndPlace({14, 2}, barracks, &Players[1]));
		units.push_back(Inside(worker, 1, *units[7], false));
		units[7]->Orders[0] = COrder::NewActionBuilt(*units[8], *units[7]);

		// Player 0: a soldier to attack with. Player 2: a farm, a barracks, a site.
		attacker = MakeUnitAndPlace({2, 10}, soldier, &Players[0]);
		otherFarm = MakeUnitAndPlace({10, 20}, farm, &Players[2]);
		otherBarracks = MakeUnitAndPlace({12, 20}, barracks, &Players[2]);
		otherSite = MakeUnitAndPlace({14, 20}, barracks, &Players[2]);
		otherSiteWorker = Inside(worker, 2, *otherSite, false);
		otherSite->Orders[0] = COrder::NewActionBuilt(*otherSiteWorker, *otherSite);
	}

	~QuitWorld()
	{
		for (int i = 0; i < 4; ++i) {
			Players[i].Clear();
		}
		UnitManager->Init();
		Map.Fields.clear();
		Map.Info.MapWidth = 0;
		Map.Info.MapHeight = 0;
		NumPlayers = savedNumPlayers;
		ThisPlayer = savedThisPlayer;
		GameCycle = savedGameCycle;
	}

	/// The units of the one who goes, by name
	CUnit &Worker() { return *units[0]; }
	CUnit &Soldier() { return *units[1]; }
	CUnit &Transport() { return *units[2]; }
	CUnit &Passenger() { return *units[3]; }
	CUnit &Tower() { return *units[4]; }
	CUnit &Farm() { return *units[5]; }
	CUnit &Barracks() { return *units[6]; }
	CUnit &Site() { return *units[7]; }
	CUnit &SiteWorker() { return *units[8]; }

	/// The living units of a player in slot order: "slot:type:action"
	static std::vector<std::string> Living(int player)
	{
		std::vector<std::pair<int, std::string>> list;

		for (const CUnit *unit : Players[player].GetUnits()) {
			if (unit->IsAlive()) {
				list.emplace_back(UnitNumber(*unit),
				                  unit->Type->Ident + ":" + std::to_string(static_cast<int>(unit->CurrentAction())));
			}
		}
		ranges::sort(list);
		std::vector<std::string> res;
		for (const auto &[slot, text] : list) {
			res.push_back(std::to_string(slot) + ":" + text);
		}
		return res;
	}

	/// Score, kills, razings, units and buildings made, of the four players
	static std::vector<int> Numbers()
	{
		std::vector<int> res;

		for (int i = 0; i < 4; ++i) {
			res.insert(res.end(), {Players[i].Score, Players[i].TotalKills, Players[i].TotalRazings,
			                       Players[i].TotalUnits, Players[i].TotalBuildings});
		}
		return res;
	}

	static bool Dead(const CUnit &unit) { return !unit.IsAlive(); }

	CUnitType worker;
	CUnitType soldier;
	CUnitType transport;
	CUnitType tower;
	CUnitType farm;
	CUnitType barracks;
	CConstruction construction;
	std::vector<CUnit *> units;
	CUnit *attacker = nullptr;
	CUnit *otherFarm = nullptr;
	CUnit *otherBarracks = nullptr;
	CUnit *otherSite = nullptr;
	CUnit *otherSiteWorker = nullptr;

private:
	static void MakeType(CUnitType &type, const char *ident, int slot, bool building, bool canAttack, int points)
	{
		type.Ident = ident;
		type.Name = ident;
		type.Slot = slot;
		type.Building = building;
		type.CanAttack = canAttack;
		type.TileWidth = 1;
		type.TileHeight = 1;
		type.BoardSize = 1;
		type.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		type.DefaultStat.Variables.resize(UnitTypeVar.GetNumberVariable());
		type.DefaultStat.Variables[HP_INDEX].Max = 400;
		type.DefaultStat.Variables[HP_INDEX].Value = 400;
		type.DefaultStat.Variables[HP_INDEX].Enable = 1;
		type.DefaultStat.Variables[POINTS_INDEX].Max = points;
		type.DefaultStat.Variables[POINTS_INDEX].Value = points;
		type.DefaultStat.Variables[POINTS_INDEX].Enable = 1;
		type.DefaultStat.Costs[TimeCost] = 100;
		type.MapDefaultStat = type.DefaultStat;
		for (int i = 0; i < PlayerMax; ++i) {
			type.Stats[i] = type.DefaultStat;
		}
	}

	/// A unit of a player inside another unit: a passenger, or the worker inside a site
	static CUnit *Inside(const CUnitType &type, int player, CUnit &host, bool boarded)
	{
		CUnit *unit = MakeUnit(type, &Players[player]);

		unit->Removed = 0; // CUnit::Remove takes a unit which is on the map
		unit->tilePos = host.tilePos;
		Map.Insert(*unit);
		unit->Remove(&host);
		if (boarded) {
			unit->Boarded = 1;
			host.BoardCount += type.BoardSize;
		}
		return unit;
	}

	CPlayer *savedThisPlayer = nullptr;
	int savedNumPlayers = 0;
	unsigned long savedGameCycle = 0;
};

} // namespace

TEST_CASE("quit: the mobile units and the towers of a gone player die, credited to nobody")
{
	QuitWorld world;

	REQUIRE(QuitWorld::Living(1).size() == 9);
	REQUIRE(world.Barracks().CurrentAction() == UnitAction::Train);
	REQUIRE(world.Site().CurrentAction() == UnitAction::Built);
	REQUIRE(world.Transport().BoardCount == 1);
	REQUIRE(world.Passenger().Container == &world.Transport());
	REQUIRE(world.SiteWorker().Container == &world.Site());
	const std::vector<int> numbersBefore = QuitWorld::Numbers();
	const std::vector<std::string> others0 = QuitWorld::Living(0);
	const std::vector<std::string> others2 = QuitWorld::Living(2);

	CommandQuitUnits(Players[1]);

	CHECK(Players[1].Gone);
	// Dead: worker, soldier, transport with its passenger, tower, the worker inside the site.
	CHECK(QuitWorld::Dead(world.Worker()));
	CHECK(QuitWorld::Dead(world.Soldier()));
	CHECK(QuitWorld::Dead(world.Transport()));
	CHECK(QuitWorld::Dead(world.Passenger()));
	CHECK((world.Passenger().Destroyed != 0));
	CHECK(QuitWorld::Dead(world.Tower()));
	CHECK(QuitWorld::Dead(world.SiteWorker()));
	CHECK(world.Site().InsideUnits.empty());
	// Left: farm, barracks (no training any more), the site as a site.
	const std::vector<std::string> left = QuitWorld::Living(1);
	REQUIRE(left.size() == 3);
	CHECK(world.Farm().IsAlive());
	CHECK_FALSE((world.Farm().Removed != 0));
	CHECK(world.Barracks().IsAlive());
	CHECK(world.Barracks().CurrentAction() == UnitAction::Still);
	CHECK(world.Barracks().Orders.size() == 1);
	CHECK(world.Site().IsAlive());
	CHECK(world.Site().CurrentAction() == UnitAction::Built);
	// Nobody got anything, and the others' units are as they were.
	CHECK(QuitWorld::Numbers() == numbersBefore);
	CHECK(QuitWorld::Living(0) == others0);
	CHECK(QuitWorld::Living(2) == others2);

	SUBCASE("the site stays as it is, another player's site goes on") {
		const int hp = world.Site().Variable[HP_INDEX].Value;
		const int otherHp = world.otherSite->Variable[HP_INDEX].Value;

		for (int i = 0; i < 50; ++i) {
			world.Site().CurrentOrder()->Execute(world.Site());
			world.otherSite->CurrentOrder()->Execute(*world.otherSite);
		}
		CHECK(world.Site().Variable[HP_INDEX].Value == hp);
		CHECK(world.Site().CurrentAction() == UnitAction::Built);
		CHECK(world.otherSite->Variable[HP_INDEX].Value > otherHp);
	}

	SUBCASE("what is left takes no command") {
		// The same command on another player's building, to see that it does something.
		CommandStandGround(*world.otherBarracks, EFlushMode::On);
		CHECK(world.otherBarracks->NewOrder != nullptr);
		CommandTrainUnit(world.Barracks(), world.soldier, EFlushMode::On);
		CHECK(world.Barracks().Orders.size() == 1);
		CHECK(world.Barracks().CurrentAction() == UnitAction::Still);

		// (A dismiss which is taken kills the unit: the farm would be dead.)
		CommandDismiss(world.Farm());
		CHECK(world.Farm().IsAlive());

		// Cancel of a construction: the order only notes it, the next cycle of the site acts.
		CommandDismiss(world.Site());
		CommandStopUnit(world.Site());
		world.Site().CurrentOrder()->Execute(world.Site());
		CHECK(world.Site().IsAlive());
		CHECK(world.Site().CurrentAction() == UnitAction::Built);

		CommandUpgradeTo(world.Barracks(), world.tower, EFlushMode::On);
		CommandMove(world.Barracks(), {1, 1}, EFlushMode::On);
		CommandAttack(world.Barracks(), {1, 1}, world.attacker, EFlushMode::On);
		CommandStandGround(world.Barracks(), EFlushMode::On);
		CommandCancelTraining(world.Barracks(), -1, nullptr);
		CommandCancelUpgradeTo(world.Barracks());
		CommandCancelResearch(world.Barracks());
		CHECK(world.Barracks().Orders.size() == 1);
		CHECK(world.Barracks().CurrentAction() == UnitAction::Still);
		CHECK(world.Barracks().NewOrder == nullptr);
		CHECK(world.Barracks().CriticalOrder == nullptr);
		CHECK(QuitWorld::Living(1) == left);
	}

	SUBCASE("destroying what is left: the razing counts, no points") {
		// For comparison the farm of an enemy who is in the game: razing and points.
		HitUnit(world.attacker, *world.otherFarm, 10000);
		CHECK(QuitWorld::Dead(*world.otherFarm));
		CHECK(Players[0].TotalRazings == 1);
		CHECK(Players[0].Score == 100);
		CHECK(world.attacker->Variable[KILL_INDEX].Value == 1);
		const int xp = world.attacker->Variable[XP_INDEX].Max;

		// A hit which does not destroy changes nothing.
		HitUnit(world.attacker, world.Farm(), 10);
		CHECK(world.Farm().IsAlive());
		CHECK(Players[0].TotalRazings == 1);

		HitUnit(world.attacker, world.Farm(), 10000);
		CHECK(QuitWorld::Dead(world.Farm()));
		CHECK(Players[0].TotalRazings == 2);
		CHECK(Players[0].TotalKills == 0);
		CHECK(Players[0].Score == 100);
		CHECK(world.attacker->Variable[XP_INDEX].Max == xp);
		CHECK(world.attacker->Variable[KILL_INDEX].Value == 2);

		// The site too, with the dead worker's place in it.
		HitUnit(world.attacker, world.Site(), 10000);
		CHECK(QuitWorld::Dead(world.Site()));
		CHECK(Players[0].TotalRazings == 3);
		CHECK(Players[0].Score == 100);

		CHECK(Players[1].Score == 0);
		CHECK(Players[1].TotalKills == 0);
		CHECK(Players[1].TotalRazings == 0);
		CHECK(Players[2].Score == 0);
		CHECK(Players[3].Score == 0);
		CHECK(QuitWorld::Living(1).size() == 1);
	}

	SUBCASE("nobody becomes his enemy again, nobody shares vision with him again") {
		CommandDiplomacy(0, EDiplomacy::Enemy, 1);
		CommandDiplomacy(1, EDiplomacy::Enemy, 0);
		CHECK_FALSE(Players[0].IsEnemy(Players[1]));
		CHECK_FALSE(Players[1].IsEnemy(Players[0]));
		CommandSharedVision(0, true, 1);
		CHECK_FALSE(Players[0].HasSharedVisionWith(Players[1]));
		// Between players who are in the game the command works.
		CommandDiplomacy(0, EDiplomacy::Neutral, 2);
		CHECK_FALSE(Players[0].IsEnemy(Players[2]));
	}

	SUBCASE("a second run changes nothing") {
		CommandQuitUnits(Players[1]);
		CHECK(QuitWorld::Living(1) == left);
		CHECK(QuitWorld::Numbers() == numbersBefore);
	}
}

TEST_CASE("quit: the same world gives the same result")
{
	std::vector<std::string> first;
	std::vector<int> firstNumbers;
	{
		QuitWorld world;

		CommandQuitUnits(Players[1]);
		first = QuitWorld::Living(1);
		firstNumbers = QuitWorld::Numbers();
	}
	QuitWorld world;

	CommandQuitUnits(Players[1]);
	CHECK(QuitWorld::Living(1) == first);
	CHECK(QuitWorld::Numbers() == firstNumbers);
	CHECK(first.size() == 3);
}

TEST_CASE("quit: a player who is in the game keeps everything")
{
	QuitWorld world;

	// Without the quit nothing of this file's rule acts: the commands are taken, a kill pays.
	CHECK_FALSE(Players[1].Gone);
	CommandStopUnit(world.Barracks());
	for (int i = 0; i < 50; ++i) {
		world.Site().CurrentOrder()->Execute(world.Site());
	}
	CHECK(world.Site().Variable[HP_INDEX].Value > 1);
	Players[0].SetDiplomacyEnemyWith(Players[1]);
	Players[1].SetDiplomacyEnemyWith(Players[0]);
	HitUnit(world.attacker, world.Farm(), 10000);
	CHECK(Players[0].TotalRazings == 1);
	CHECK(Players[0].Score == 100);
	CHECK(QuitWorld::Living(1).size() == 8);
}
