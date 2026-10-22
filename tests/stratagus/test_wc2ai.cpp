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
/**@name test_wc2ai.cpp - The computer player's script reader and its training decisions. */
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

#include "wc2ai.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace
{

/// Dice with prepared answers; it notes the sides it was asked for.
struct PreparedDice {
	std::vector<bool> answers;
	std::vector<int> asked;
};

bool Throw(int sides, void *context)
{
	PreparedDice &dice = *static_cast<PreparedDice *>(context);
	const bool answer = dice.asked.size() < dice.answers.size() && dice.answers[dice.asked.size()];
	dice.asked.push_back(sides);
	return answer;
}

bool Never(int, void *)
{
	return false;
}

bool Always(int, void *)
{
	return true;
}

/// A made-up definition file: every id runs the one script
///     limit := 2; footmen := 3; until 4; wait 5; back to the "until"
/// with the build list hall, tower, research 0x80 and a cadence table of 1, "never" for the towers.
std::vector<uint8_t> MadeUpFile()
{
	const uint16_t entry = (wc2ai::ScriptCount + 2) * 2;
	std::vector<uint8_t> file;
	const auto word = [&](unsigned value) {
		file.push_back(static_cast<uint8_t>(value & 0xFF));
		file.push_back(static_cast<uint8_t>(value >> 8));
	};
	for (int id = 0; id < wc2ai::ScriptCount; ++id) {
		word(entry);
	}
	word(0);
	word(0);
	const unsigned list = entry + 20u;
	const unsigned cadence = list + 4u;
	word(list);
	word(cadence);
	const unsigned until = entry + 10u;
	for (const unsigned byte : {0x00u, 0x22u, 0x02u,             // entry + 4
	                            0x00u, 0x14u, 0x03u,             // entry + 7
	                            0x03u, 0x04u,                    // entry + 10
	                            0x02u, 0x05u, 0x00u, 0x00u, 0x00u, // entry + 12
	                            0x01u, until & 0xFF, until >> 8, // entry + 17
	                            0x4Au, 0x40u, 0x80u, 0xFFu}) {   // entry + 20
		file.push_back(static_cast<uint8_t>(byte));
	}
	for (int i = 0; i < wc2ai::CadenceCount; ++i) {
		word((i == 6 || i == 7) ? 0xFFFF : 1);
	}
	return file;
}

struct Answer {
	bool holds = false;
	int asked = -1;
};

bool Holds(int condition, void *context)
{
	Answer &answer = *static_cast<Answer *>(context);
	answer.asked = condition;
	return answer.holds;
}

} // namespace

TEST_CASE("computer: a script sets its parameters, waits for its condition, waits and loops")
{
	const std::vector<uint8_t> file = MadeUpFile();
	wc2ai::Program program;
	std::string error;
	REQUIRE(program.Parse(file.data(), file.size(), &error));
	REQUIRE(program.Loaded());
	const wc2ai::Script *script = program.Get(0x19);
	REQUIRE(script != nullptr);
	CHECK(script->buildList == std::vector<uint8_t>{0x4A, 0x40, 0x80});
	CHECK(program.Get(wc2ai::ScriptCount) == nullptr);
	CHECK(program.InstructionCount(0) == 5);

	wc2ai::State state;
	CHECK_FALSE(wc2ai::Start(program, wc2ai::ScriptCount, state));
	REQUIRE(wc2ai::Start(program, 0, state));
	CHECK(state.params[wc2ai::ParamLast] == 0xFF); // the whole build list until the script says otherwise

	Answer answer;
	wc2ai::Step(program, state, Holds, &answer);
	CHECK(state.params[0x22] == 2);
	CHECK(state.params[0x14] == 3);
	CHECK(answer.asked == 4);
	CHECK(state.executed == 2);
	const uint16_t stoodAt = state.ip;
	wc2ai::Step(program, state, Holds, &answer); // asked again every cycle
	CHECK(state.ip == stoodAt);
	CHECK(state.executed == 2);

	answer.holds = true;
	wc2ai::Step(program, state, Holds, &answer);
	CHECK(state.wait == 5);
	CHECK(state.executed == 4);
	for (int cycle = 0; cycle < 5; ++cycle) {
		wc2ai::Step(program, state, Holds, &answer);
	}
	CHECK(state.wait == 0);
	CHECK(state.executed == 4);
	wc2ai::Step(program, state, Holds, &answer); // back to the condition, which holds: the wait again
	CHECK(state.wait == 5);
	CHECK(state.executed == 7);
}

TEST_CASE("computer: a file with a parameter or a condition out of range is not taken")
{
	std::vector<uint8_t> file = MadeUpFile();
	const size_t entry = (wc2ai::ScriptCount + 2) * 2;
	wc2ai::Program program;
	std::string error;
	file[entry + 5] = 0x23; // parameter beyond the last one
	CHECK_FALSE(program.Parse(file.data(), file.size(), &error));
	CHECK_FALSE(program.Loaded());
	CHECK_FALSE(error.empty());
	file[entry + 5] = 0x22;
	file[entry + 11] = 8; // there are eight conditions, 0..7
	CHECK_FALSE(program.Parse(file.data(), file.size(), &error));
	CHECK_FALSE(program.Parse(file.data(), 10, &error));
}

TEST_CASE("computer: a building type the cadence table marks never does not decide")
{
	const std::vector<uint8_t> file = MadeUpFile();
	wc2ai::Program program;
	REQUIRE(program.Parse(file.data(), file.size()));
	const wc2ai::Script &script = *program.Get(0);
	CHECK(wc2ai::BuildingDecides(script, 0x3C));       // barracks, human
	CHECK(wc2ai::BuildingDecides(script, 0x3D));       // barracks, orc
	CHECK_FALSE(wc2ai::BuildingDecides(script, 0x40)); // tower, human
	CHECK_FALSE(wc2ai::BuildingDecides(script, 0x41)); // tower, orc
	CHECK(wc2ai::BuildingDecides(script, 0x5C));       // the last type of the table
	CHECK(wc2ai::BuildingDecides(script, 0x39));       // outside the table
	CHECK(wc2ai::BuildingDecides(script, 0x5D));
}

TEST_CASE("computer: a barracks first trains the class that is wanted and missing")
{
	wc2ai::BarracksView view;
	view.wantFootmen = 2;
	view.wantArchers = 2;
	view.wantKnights = 2;
	view.wantBallistas = 1;
	view.archersCan = view.ballistasCan = view.knightsCan = true;
	CHECK(wc2ai::BarracksDecision(view, Always, nullptr) == wc2ai::Train::Footman);
	view.haveFootmen = 1;
	CHECK(wc2ai::BarracksDecision(view, Always, nullptr) == wc2ai::Train::Knight);
	view.knightsCan = false; // no blacksmith or no stables
	CHECK(wc2ai::BarracksDecision(view, Never, nullptr) == wc2ai::Train::Archer);
	view.archersCan = false; // no lumber mill
	view.ballistasCan = false;
	CHECK(wc2ai::BarracksDecision(view, Always, nullptr) == wc2ai::Train::Footman);
	view.haveFootmen = 2;
	CHECK(wc2ai::BarracksDecision(view, Always, nullptr) == wc2ai::Train::None);
	CHECK(wc2ai::BarracksDecision(wc2ai::BarracksView(), Always, nullptr) == wc2ai::Train::None);
}

TEST_CASE("computer: archers come with 1 in 4 and ballistas with 1 in 8, knights and footmen without dice")
{
	wc2ai::BarracksView view;
	view.wantFootmen = 6;
	view.haveFootmen = 1;
	view.wantArchers = 4;
	view.haveArchers = 1;
	view.wantBallistas = 2;
	view.haveBallistas = 0;
	view.wantKnights = 8;
	view.haveKnights = 1;
	view.archersCan = view.ballistasCan = view.knightsCan = true;

	PreparedDice archer{{true}, {}};
	CHECK(wc2ai::BarracksDecision(view, Throw, &archer) == wc2ai::Train::Archer);
	CHECK(archer.asked == std::vector<int>{4});

	PreparedDice ballista{{false, true}, {}};
	CHECK(wc2ai::BarracksDecision(view, Throw, &ballista) == wc2ai::Train::Ballista);
	CHECK(ballista.asked == std::vector<int>{4, 8});

	PreparedDice neither{{false, false}, {}};
	CHECK(wc2ai::BarracksDecision(view, Throw, &neither) == wc2ai::Train::Knight);
	view.haveKnights = 8;
	CHECK(wc2ai::BarracksDecision(view, Never, nullptr) == wc2ai::Train::Footman);

	// A class at its wanted number, or one that cannot be trained, asks no dice.
	view.haveArchers = 4;
	view.ballistasCan = false;
	PreparedDice none{{true, true}, {}};
	CHECK(wc2ai::BarracksDecision(view, Throw, &none) == wc2ai::Train::Footman);
	CHECK(none.asked.empty());

	// Nothing but the dice classes below their number: a decision may train nothing.
	view.haveFootmen = 6;
	view.haveArchers = 3;
	CHECK(wc2ai::BarracksDecision(view, Never, nullptr) == wc2ai::Train::None);
	CHECK(wc2ai::BarracksDecision(view, Always, nullptr) == wc2ai::Train::Archer);
}

TEST_CASE("computer: a shipyard trains tanker, destroyer, transport when missing, then by the dice")
{
	wc2ai::ShipyardView view;
	view.wantTankers = 3;
	view.wantDestroyers = 4;
	view.wantTransports = 2;
	view.wantBattleships = 2;
	view.wantSubmarines = 2;
	view.battleshipsCan = view.submarinesCan = true;
	CHECK(wc2ai::ShipyardDecision(view, Always, nullptr) == wc2ai::Train::Tanker);
	view.haveTankers = 1;
	CHECK(wc2ai::ShipyardDecision(view, Always, nullptr) == wc2ai::Train::Destroyer);
	view.haveDestroyers = 1;
	CHECK(wc2ai::ShipyardDecision(view, Never, nullptr) == wc2ai::Train::Transport);
	view.haveTransports = 1;

	PreparedDice destroyer{{true}, {}};
	CHECK(wc2ai::ShipyardDecision(view, Throw, &destroyer) == wc2ai::Train::Destroyer);
	CHECK(destroyer.asked == std::vector<int>{4});
	PreparedDice battleship{{false, true}, {}};
	CHECK(wc2ai::ShipyardDecision(view, Throw, &battleship) == wc2ai::Train::Battleship);
	CHECK(battleship.asked == std::vector<int>{4, 8});
	PreparedDice submarine{{false, false, true}, {}};
	CHECK(wc2ai::ShipyardDecision(view, Throw, &submarine) == wc2ai::Train::Submarine);
	PreparedDice transport{{false, false, false, true}, {}};
	CHECK(wc2ai::ShipyardDecision(view, Throw, &transport) == wc2ai::Train::Transport);
	CHECK(transport.asked == std::vector<int>{4, 8, 8, 8});

	// No dice falls: tankers, then destroyers, towards their numbers.
	CHECK(wc2ai::ShipyardDecision(view, Never, nullptr) == wc2ai::Train::Tanker);
	view.haveTankers = 3;
	CHECK(wc2ai::ShipyardDecision(view, Never, nullptr) == wc2ai::Train::Destroyer);

	// Without foundry and inventor the heavy ships ask no dice.
	view.battleshipsCan = view.submarinesCan = false;
	PreparedDice light{{false, false}, {}};
	CHECK(wc2ai::ShipyardDecision(view, Throw, &light) == wc2ai::Train::Destroyer);
	CHECK(light.asked == std::vector<int>{4, 8});
	view.haveDestroyers = 4;
	view.haveTransports = 2;
	CHECK(wc2ai::ShipyardDecision(view, Always, nullptr) == wc2ai::Train::None);
}

TEST_CASE("computer: over many decisions about one archer in four")
{
	// The dice of the test: a counter, every fourth throw of four sides falls.
	struct Counter {
		int throws = 0;
	} counter;
	const wc2ai::Dice everyFourth = [](int sides, void *context) {
		Counter &c = *static_cast<Counter *>(context);
		return sides == 4 && ++c.throws % 4 == 0;
	};
	wc2ai::BarracksView view;
	view.wantFootmen = 1000;
	view.haveFootmen = 1;
	view.wantArchers = 1000;
	view.haveArchers = 1;
	view.archersCan = true;
	int archers = 0, footmen = 0;
	for (int decision = 0; decision < 400; ++decision) {
		const wc2ai::Train train = wc2ai::BarracksDecision(view, everyFourth, &counter);
		archers += train == wc2ai::Train::Archer;
		footmen += train == wc2ai::Train::Footman;
	}
	CHECK(archers == 100);
	CHECK(footmen == 300);
}

// ---- The attack rates. Every file below is made up for the test.

TEST_CASE("computer: wave: an animation's length counts frames up to the second end")
{
	const std::vector<uint8_t> plain = {0x01, 0x05, 0x01, 0x03, 0x00, 0x01, 0x02, 0x00};
	CHECK(wc2ai::AnimationLength(plain.data(), plain.size(), 0) == 12);
	// a jump to a stream that is two ends
	const std::vector<uint8_t> jump = {0x01, 0x04, 0x03, 0x07, 0x00, 0xFF, 0xFF, 0x00, 0x00};
	CHECK(wc2ai::AnimationLength(jump.data(), jump.size(), 0) == 6);
	// operands that are skipped
	const std::vector<uint8_t> skips = {0x04, 0xFF, 0x06, 0xFF, 0xFF, 0x01, 0x02, 0x00, 0x00};
	CHECK(wc2ai::AnimationLength(skips.data(), skips.size(), 0) == 4);
	// outside the file, and no second end inside it
	CHECK(wc2ai::AnimationLength(plain.data(), plain.size(), 8) == -1);
	const std::vector<uint8_t> open = {0x01, 0x05, 0x00, 0x01, 0x02};
	CHECK(wc2ai::AnimationLength(open.data(), open.size(), 0) == -1);
	const std::vector<uint8_t> loop = {0x03, 0x00, 0x00};
	CHECK(wc2ai::AnimationLength(loop.data(), loop.size(), 0) == -1);
	// the length is kept as a byte
	const std::vector<uint8_t> wraps = {0x01, 0xFF, 0x01, 0x03, 0x00, 0x00};
	CHECK(wc2ai::AnimationLength(wraps.data(), wraps.size(), 0) == 4);
}

namespace
{

/// A made-up rate file: per type a table of five entries, the fifth pointing at an animation
/// of the wanted length (0: an animation the byte count of which wraps to 0).
std::vector<uint8_t> RateFile(const int lengths[wc2ai::RateTypes])
{
	std::vector<uint8_t> file(wc2ai::RateTypes * 2, 0);
	const auto put16 = [&](size_t at, size_t value) {
		file[at] = static_cast<uint8_t>(value & 0xFF);
		file[at + 1] = static_cast<uint8_t>(value >> 8);
	};
	for (int type = 0; type < wc2ai::RateTypes; ++type) {
		const size_t table = file.size();
		put16(static_cast<size_t>(type) * 2, table);
		file.resize(table + 10, 0);
		put16(table + 8, file.size());
		const int length = lengths[type];
		if (length == 0) {
			// 254 + 1 + 1 = 256, kept as a byte: 0
			file.insert(file.end(), {0x01, 0xFE, 0x00, 0x00});
		} else {
			// length - 2 frames, then the two ends
			file.insert(file.end(), {0x01, static_cast<uint8_t>(length - 2), 0x00, 0x00});
		}
	}
	return file;
}

} // namespace

TEST_CASE("computer: wave: a type's attack rate is ten times the first type's length by its own")
{
	int lengths[wc2ai::RateTypes];
	for (int &length : lengths) {
		length = 26;
	}
	lengths[8] = 66;
	lengths[0x0A] = 41;
	lengths[4] = 201;
	lengths[0x20] = 0;
	const std::vector<uint8_t> file = RateFile(lengths);
	uint8_t rates[wc2ai::RateTypes];
	REQUIRE(wc2ai::AttackRates(file.data(), file.size(), rates));
	CHECK(rates[0] == 10);
	CHECK(rates[1] == 10);
	CHECK(rates[6] == 10);
	CHECK(rates[8] == 3);
	CHECK(rates[0x0A] == 6);
	CHECK(rates[4] == 1);
	CHECK(rates[0x20] == 0);
	// a file that is too short, and one whose walk leaves the file: no rates
	CHECK_FALSE(wc2ai::AttackRates(file.data(), 20, rates));
	CHECK(rates[0] == 0);
	CHECK_FALSE(wc2ai::AttackRates(file.data(), file.size() - 1, rates));
	CHECK(rates[1] == 0);
}

TEST_CASE("computer: wave: the strength weight of a unit type")
{
	CHECK(wc2ai::StrengthWeight(6, 3, 10, 10) == 5);
	CHECK(wc2ai::StrengthWeight(0, 0, 10, 10) == 0);
	CHECK(wc2ai::StrengthWeight(9, 0, 3, 10) == 1);
	CHECK(wc2ai::StrengthWeight(6, 3, 10, 0) == 0);
	CHECK(wc2ai::StrengthWeight(6, 3, 0, 10) == 0);
	CHECK(wc2ai::StrengthWeight(80, 0, 1, 10) == 3);
}

// ---- Where a wave goes.

namespace
{

wc2ai::PlaceUnit PlaceAt(int x, int y)
{
	wc2ai::PlaceUnit unit;
	unit.x = x;
	unit.y = y;
	return unit;
}
wc2ai::PlaceUnit HallAt(int x, int y, int size = 1)
{
	wc2ai::PlaceUnit unit = PlaceAt(x, y);
	unit.w = unit.h = size;
	unit.hall = unit.building = true;
	return unit;
}
wc2ai::PlaceUnit ShipAt(int x, int y)
{
	wc2ai::PlaceUnit unit = PlaceAt(x, y);
	unit.ship = unit.fighter = true;
	return unit;
}
wc2ai::PlaceUnit PlatformAt(int x, int y)
{
	wc2ai::PlaceUnit unit = PlaceAt(x, y);
	unit.platform = unit.building = true;
	return unit;
}
wc2ai::PlaceUnit ShipyardAt(int x, int y)
{
	wc2ai::PlaceUnit unit = PlaceAt(x, y);
	unit.shipyard = unit.building = true;
	return unit;
}

} // namespace

TEST_CASE("computer: wave: a sea wave goes to the first oil platform, else the last shipyard, else the farthest ship")
{
	wc2ai::Place place = wc2ai::SeaPlace({HallAt(10, 10), ShipAt(30, 10), PlatformAt(50, 50), PlatformAt(20, 20),
	                                      ShipyardAt(12, 14)});
	CHECK((place.found && place.x == 50 && place.y == 50));
	place = wc2ai::SeaPlace({HallAt(10, 10), ShipyardAt(12, 14), ShipAt(40, 10), ShipyardAt(60, 60)});
	CHECK((place.found && place.x == 60 && place.y == 60));
	// distances to the hall 5, 30, 29
	place = wc2ai::SeaPlace({HallAt(10, 10), ShipAt(15, 10), ShipAt(10, 40), ShipAt(39, 12)});
	CHECK((place.found && place.x == 10 && place.y == 40));
	// equal distances: the first
	place = wc2ai::SeaPlace({HallAt(10, 10), ShipAt(30, 10), ShipAt(10, 30)});
	CHECK((place.found && place.x == 30 && place.y == 10));
	// the reference is the hall nearest to the first unit: (40,40), from which (10,10) is farther
	place = wc2ai::SeaPlace({PlaceAt(38, 38), HallAt(0, 0), HallAt(40, 40), ShipAt(45, 45), ShipAt(10, 10)});
	CHECK((place.found && place.x == 10 && place.y == 10));
	// nothing of the sea: no place
	CHECK_FALSE(wc2ai::SeaPlace({HallAt(10, 10), PlaceAt(12, 12)}).found);
	CHECK_FALSE(wc2ai::SeaPlace({}).found);
}

TEST_CASE("computer: wave: an air wave goes to the last hall, else to the unit farthest from the first")
{
	wc2ai::Place place = wc2ai::AirPlace({PlaceAt(5, 5), HallAt(10, 10), PlaceAt(30, 30), HallAt(50, 8)});
	CHECK((place.found && place.x == 50 && place.y == 8));
	place = wc2ai::AirPlace({PlaceAt(5, 5), PlaceAt(9, 5), PlaceAt(5, 30)});
	CHECK((place.found && place.x == 5 && place.y == 30));
	// equal distances: the first
	place = wc2ai::AirPlace({PlaceAt(5, 5), PlaceAt(9, 5), PlaceAt(5, 9)});
	CHECK((place.found && place.x == 9 && place.y == 5));
	place = wc2ai::AirPlace({PlaceAt(7, 3)});
	CHECK((place.found && place.x == 7 && place.y == 3));
	CHECK_FALSE(wc2ai::AirPlace({}).found);
}

// ---- The strength map (16 x 16 tiles: 2 x 2 cells).

namespace
{

wc2ai::CellUnit Footman(int slot, int x, int y, int hitPoints = 60)
{
	wc2ai::CellUnit unit;
	unit.x = x;
	unit.y = y;
	unit.slot = slot;
	unit.hitsLand = true;
	unit.hitPoints = hitPoints;
	unit.maxHitPoints = 60;
	unit.weight = 5;
	return unit;
}
wc2ai::CellUnit Worker(int slot, int x, int y)
{
	wc2ai::CellUnit unit;
	unit.x = x;
	unit.y = y;
	unit.slot = slot;
	unit.worker = true;
	return unit;
}

} // namespace

TEST_CASE("computer: wave: the strength map adds up each slot's units per cell and names the cell's holder")
{
	const std::vector<uint8_t> land(4, 0);
	wc2ai::CellMap map = wc2ai::BuildCellMap(16, 16, land, {Footman(1, 1, 1)});
	REQUIRE(map.cellsX == 2);
	REQUIRE(map.cells.size() == 4);
	CHECK(map.cells[0].land[1] == 5);
	CHECK(map.cells[0].sea[1] == 0);
	CHECK(map.cells[0].owner == 1);
	CHECK(map.cells[1].owner == -1);
	// by its health
	map = wc2ai::BuildCellMap(16, 16, land, {Footman(1, 1, 1, 30)});
	CHECK(map.cells[0].land[1] == 2);
	// workers alone hold a cell
	map = wc2ai::BuildCellMap(16, 16, land, {Worker(0, 2, 2), Worker(0, 3, 3)});
	CHECK(map.cells[0].land[0] == -4);
	CHECK(map.cells[0].sea[0] == -4);
	CHECK(map.cells[0].owner == 0);
	map = wc2ai::BuildCellMap(16, 16, land, {Worker(0, 2, 2), Footman(2, 3, 3)});
	CHECK(map.cells[0].owner == 2);
	// equal values: the lower slot
	map = wc2ai::BuildCellMap(16, 16, land, {Footman(1, 2, 2), Footman(0, 3, 3)});
	CHECK(map.cells[0].owner == 0);
	// a transport in a water cell, and in a cell that is neither
	wc2ai::CellUnit transport;
	transport.x = 9;
	transport.y = 1;
	transport.slot = 3;
	transport.transport = true;
	map = wc2ai::BuildCellMap(16, 16, {0, 1, 0, 0}, {transport});
	CHECK(map.cells[1].sea[3] == -5);
	CHECK(map.cells[1].owner == 3);
	map = wc2ai::BuildCellMap(16, 16, {0, 2, 0, 0}, {transport});
	CHECK(map.cells[1].sea[3] == -5);
	CHECK(map.cells[1].owner == -1);
	// a slot that is not counted
	map = wc2ai::BuildCellMap(16, 16, land, {Footman(15, 1, 1)});
	CHECK(map.cells[0].owner == -1);
	// a building of 2 x 2 tiles is entered once per tile
	std::vector<wc2ai::CellUnit> farm;
	for (int y = 8; y <= 9; ++y) {
		for (int x = 8; x <= 9; ++x) {
			wc2ai::CellUnit tile;
			tile.x = x;
			tile.y = y;
			tile.slot = 4;
			tile.mobile = false;
			farm.push_back(tile);
		}
	}
	map = wc2ai::BuildCellMap(16, 16, land, farm);
	CHECK(map.cells[3].land[4] == -4);
	// the order of the tests: a worker before a hall before a transport before a tanker
	wc2ai::CellUnit unit;
	unit.hall = unit.transport = unit.tanker = true;
	unit.mobile = false;
	CHECK(wc2ai::CellValue(unit, true) == -1);
	unit.hall = false;
	CHECK(wc2ai::CellValue(unit, true) == -5);
	unit.transport = false;
	CHECK(wc2ai::CellValue(unit, true) == -2);
	unit.alive = false;
	CHECK(wc2ai::CellValue(unit, true) == 0);
	CHECK(wc2ai::CellValue(Footman(1, 0, 0), false) == 0);
}

TEST_CASE("computer: wave: a land wave goes to the target's land cell farthest from its hall, the axes exchanged")
{
	wc2ai::CellMap map;
	map.cellsX = map.cellsY = 8;
	map.cells.resize(64);
	for (wc2ai::CellRecord &cell : map.cells) {
		cell.kind = 0;
	}
	const auto hold = [&](int cellX, int cellY, int slot) { map.cells[cellY * 8 + cellX].owner = static_cast<int8_t>(slot); };
	const std::vector<wc2ai::PlaceUnit> hallOnly = {HallAt(4, 4, 4)};
	REQUIRE(wc2ai::LandPlaceSwapsAxes);
	// no cell of the target: the hall's own cell, centre (4,4); the hall is the unit nearest to it
	wc2ai::Place place = wc2ai::LandPlace(0, hallOnly, map);
	CHECK((place.found && place.x == 4 && place.y == 4));
	// cells (0,0) and (6,1): measured at (4,4) and (12,52), 0 and 45 tiles from the hall's edge
	hold(0, 0, 0);
	hold(6, 1, 0);
	std::vector<wc2ai::PlaceUnit> target = {HallAt(4, 4, 4), PlaceAt(50, 13), ShipAt(52, 12)};
	place = wc2ai::LandPlace(0, target, map);
	CHECK((place.found && place.x == 50 && place.y == 13));
	// cell (1,6) is measured at (52,12), 45 tiles too: the first one in cell order stays
	hold(1, 6, 0);
	place = wc2ai::LandPlace(0, target, map);
	CHECK((place.found && place.x == 50 && place.y == 13));
	// a water cell of the target, and a cell of another slot, are not looked at
	map.cells[7 * 8 + 7].kind = 1;
	hold(7, 7, 0);
	hold(0, 7, 3);
	place = wc2ai::LandPlace(0, target, map);
	CHECK((place.found && place.x == 50 && place.y == 13));
	// no unit of the kind: the cell's centre
	place = wc2ai::LandPlace(0, {ShipAt(4, 4)}, map);
	CHECK((place.found && place.x == 52 && place.y == 12));
	CHECK_FALSE(wc2ai::LandPlace(0, {}, map).found);
}

TEST_CASE("computer: wave: a unit type's number is the place of its ident in the converter's table")
{
	const char *const names[] = {"unit-a", "unit-b", "", "unit-c", "unit-b"};
	CHECK(wc2ai::TypeNumberOfIdent(names, 5, "unit-a") == 0);
	CHECK(wc2ai::TypeNumberOfIdent(names, 5, "unit-c") == 3);
	CHECK(wc2ai::TypeNumberOfIdent(names, 5, "unit-b") == 1); // the first of two
	CHECK(wc2ai::TypeNumberOfIdent(names, 3, "unit-c") == -1); // beyond the types that have a rate
	CHECK(wc2ai::TypeNumberOfIdent(names, 5, "unit-d") == -1);
	CHECK(wc2ai::TypeNumberOfIdent(nullptr, 5, "unit-a") == -1);
	// with the number the rate and the weight follow: type 3 at rate 3 against type 1 at rate 10
	const uint8_t rates[wc2ai::RateTypes] = {10, 10, 0, 3};
	const int number = wc2ai::TypeNumberOfIdent(names, 5, "unit-c");
	CHECK(wc2ai::StrengthWeight(9, 0, rates[number], rates[1]) == 1);
}

TEST_CASE("computer: wave: a cell is land or water by the greater number of its tiles")
{
	CHECK(wc2ai::CellKind(33, 31) == 0);
	CHECK(wc2ai::CellKind(10, 31) == 1);
	CHECK(wc2ai::CellKind(20, 20) == 2); // the rest coast
	CHECK(wc2ai::CellKind(0, 0) == 2);
	CHECK(wc2ai::CellKind(1, 0) == 0);
}

TEST_CASE("computer: wave: a guard breaks off by the cell it stands in")
{
	// cells 0 (land), 1 (water); slot 2 holds both with 11 against slot 1's 4
	const std::vector<uint8_t> kinds = {0, 1, 0, 0};
	std::vector<wc2ai::CellUnit> units = {Footman(1, 1, 1, 48), Footman(2, 2, 2), Footman(2, 3, 3),
	                                      Footman(2, 4, 4, 12)};
	wc2ai::CellMap map = wc2ai::BuildCellMap(16, 16, kinds, units);
	REQUIRE(wc2ai::CellAt(map, 7, 7) == &map.cells[0]);
	REQUIRE(wc2ai::CellAt(map, 8, 7) == &map.cells[1]);
	REQUIRE(wc2ai::CellAt(map, 7, 8) == &map.cells[2]);
	CHECK(wc2ai::CellAt(map, 16, 0) == nullptr);
	CHECK(wc2ai::CellAt(map, 0, -1) == nullptr);
	CHECK(map.cells[0].land[1] == 4);
	CHECK(map.cells[0].land[2] == 11);
	CHECK(wc2ai::GuardRetreats(map, 1, 1, 1, false));
	CHECK_FALSE(wc2ai::GuardRetreats(map, 1, 1, 2, false)); // the holder's own unit
	CHECK_FALSE(wc2ai::GuardRetreats(map, 1, 1, 1, true));  // a ship asks the sea column: 0 against 0
	CHECK_FALSE(wc2ai::GuardRetreats(map, 1, 9, 1, false)); // nobody holds that cell
	CHECK_FALSE(wc2ai::GuardRetreats(map, 40, 1, 1, false));
	CHECK_FALSE(wc2ai::GuardRetreats(map, 1, 1, 9, false)); // a slot the map does not count
	// 10 against 4: not over 2.5 times
	units[3].hitPoints = 0;
	map = wc2ai::BuildCellMap(16, 16, kinds, units);
	CHECK(map.cells[0].land[2] == 10);
	CHECK_FALSE(wc2ai::GuardRetreats(map, 1, 1, 1, false));
	// without the rates every weight is 0: nobody holds a cell by strength, no guard breaks off
	for (wc2ai::CellUnit &unit : units) {
		unit.weight = 0;
	}
	map = wc2ai::BuildCellMap(16, 16, kinds, units);
	CHECK(map.cells[0].owner == -1);
	CHECK_FALSE(wc2ai::GuardRetreats(map, 1, 1, 1, false));
}

TEST_CASE("computer: wave: the last stand comes when at most one building that is no oil platform is left")
{
	// The numbers are what is left after the dying building.
	CHECK_FALSE(wc2ai::AllOutNow(3, 1)); // two buildings and a platform: measured, nothing moved
	CHECK(wc2ai::AllOutNow(2, 1));       // one building and a platform: measured, every unit went
	CHECK_FALSE(wc2ai::AllOutNow(2, 0));
	CHECK(wc2ai::AllOutNow(1, 0));
	CHECK(wc2ai::AllOutNow(3, 2));
	CHECK_FALSE(wc2ai::AllOutNow(4, 2));
	CHECK(wc2ai::AllOutNow(0, 0));
}

TEST_CASE("computer: wave: the fighters a condition counts and the ones a wave takes")
{
	CHECK(wc2ai::Counted(true, true, true, false));
	CHECK_FALSE(wc2ai::Counted(true, true, true, true));   // fixed by the map: not counted
	CHECK_FALSE(wc2ai::Counted(false, true, true, false));
	CHECK_FALSE(wc2ai::Counted(true, false, true, false));
	CHECK_FALSE(wc2ai::Counted(true, true, false, false));
	CHECK(wc2ai::Pickable(true, 0));
	CHECK_FALSE(wc2ai::Pickable(true, 2));                 // sent and held to its place: counted, not taken
	CHECK(wc2ai::Pickable(true, 4));                       // a wanderer: counted and taken
	CHECK_FALSE(wc2ai::Pickable(false, 0));
}

TEST_CASE("computer: cast: the pass falls on every fiftieth cycle, the first on cycle 1")
{
	CHECK(wc2ai::CastPassDue(1));
	CHECK(wc2ai::CastPassDue(51));
	CHECK(wc2ai::CastPassDue(101));
	CHECK_FALSE(wc2ai::CastPassDue(0));
	CHECK_FALSE(wc2ai::CastPassDue(2));
	CHECK_FALSE(wc2ai::CastPassDue(30));
	CHECK_FALSE(wc2ai::CastPassDue(50));
	CHECK_FALSE(wc2ai::CastPassDue(100));
}

TEST_CASE("attack-move: kept (blocked or at a re-issue) unless within 1 tile of its point; 20 is the cool-down")
{
	CHECK(wc2ai::AttackMoveRetryWait == 20);
	CHECK_FALSE(wc2ai::AttackMoveRetry(0, 0));
	CHECK_FALSE(wc2ai::AttackMoveRetry(1, -1));
	CHECK_FALSE(wc2ai::AttackMoveRetry(-1, 1));
	CHECK_FALSE(wc2ai::AttackMoveRetry(0, 1));
	CHECK(wc2ai::AttackMoveRetry(2, 0));
	CHECK(wc2ai::AttackMoveRetry(0, -2));
	CHECK(wc2ai::AttackMoveRetry(-2, 1));
	CHECK(wc2ai::AttackMoveRetry(1, 7));
}

TEST_CASE("computer: split: lumber workers by the two stocks")
{
	CHECK(wc2ai::LumberWorkers(499, 0, 4) == 0);      // poor and few: all on gold
	CHECK(wc2ai::LumberWorkers(499, 0, 5) == 2);
	CHECK(wc2ai::LumberWorkers(400, 600, 9) == 0);
	CHECK(wc2ai::LumberWorkers(999, 499, 9) == 4);
	CHECK(wc2ai::LumberWorkers(999, 500, 9) == 0);
	CHECK(wc2ai::LumberWorkers(1000, 499, 9) == 7);   // 3 * (9 + 1) / 4
	CHECK(wc2ai::LumberWorkers(1000, 499, 15) == 12);
	CHECK(wc2ai::LumberWorkers(1000, 500, 15) == 7);
	CHECK(wc2ai::LumberWorkers(5000, 2000, 15) == 0); // lumber enough
	CHECK(wc2ai::LumberWorkers(5000, 1999, 15) == 7);
	CHECK(wc2ai::LumberWorkers(0, 0, 1) == 0);
}

TEST_CASE("computer: split: a worker decides again after a delivery")
{
	// run 34, item 4: W 5, G 1200, L 50 wants 4 on lumber
	const int wanted = wc2ai::LumberWorkers(1200, 50, 5);
	REQUIRE(wanted == 4);
	CHECK(wc2ai::LumberNext(3, wanted, true, true));        // three others cut: the fourth goes too
	CHECK_FALSE(wc2ai::LumberNext(4, wanted, true, true));  // four others cut: gold
	// lumber enough (L >= 2000): a lumber worker turns to gold at its next delivery
	CHECK_FALSE(wc2ai::LumberNext(0, wc2ai::LumberWorkers(800, 2100, 15), true, true));
	// itself left out: alone on lumber with one wanted, it stays on lumber
	CHECK(wc2ai::LumberNext(0, 1, true, true));
	CHECK(wc2ai::LumberNext(9, 0, false, true));            // no gold to take
	CHECK_FALSE(wc2ai::LumberNext(0, 9, true, false));      // no lumber to take
}

namespace
{
/// A map for the placement walk: which tiles are on the worker's ground, which fit, what was asked.
struct WalkMap {
	bool ground = true;                          ///< every tile is on the worker's ground
	bool allFit = false;
	std::vector<std::pair<int, int>> fit;        ///< the tiles that fit when not all do
	std::vector<std::pair<int, int>> asked;      ///< every tile `fits` was asked for, in order
};

bool WalkGround(int, int, void *context)
{
	return static_cast<WalkMap *>(context)->ground;
}

bool WalkFits(int x, int y, void *context)
{
	WalkMap &map = *static_cast<WalkMap *>(context);
	map.asked.emplace_back(x, y);
	return map.allFit || std::find(map.fit.begin(), map.fit.end(), std::make_pair(x, y)) != map.fit.end();
}

wc2ai::Place Walk(WalkMap &map, int refX, int refY, int step, int side = 64)
{
	map.asked.clear();
	return wc2ai::PlaceWalk(refX, refY, step, side, WalkGround, WalkFits, &map);
}
} // namespace

TEST_CASE("computer: place: the first free candidate of the square ring, clockwise from the top left")
{
	WalkMap map;
	map.allFit = true;
	wc2ai::Place place = Walk(map, 30, 30, 6);
	CHECK((place.found && place.x == 24 && place.y == 24));
	place = Walk(map, 30, 30, 2);
	CHECK((place.found && place.x == 28 && place.y == 28));

	// the +x side comes first
	map.allFit = false;
	map.fit = {{24, 36}, {36, 24}};
	place = Walk(map, 30, 30, 6);
	CHECK((place.found && place.x == 36 && place.y == 24));

	// the whole ring in its order: four sides of three, each corner twice, the middle never
	map.fit.clear();
	place = Walk(map, 30, 30, 6, 44); // sides 18 and 30 against a limit of 29: one ring only
	CHECK_FALSE(place.found);
	const std::vector<std::pair<int, int>> ring = {{24, 24}, {30, 24}, {36, 24}, {36, 24}, {36, 30}, {36, 36},
	                                               {36, 36}, {30, 36}, {24, 36}, {24, 36}, {24, 30}, {24, 24}};
	CHECK(map.asked == ring);

	// nothing on ring 1: ring 2 starts at its own top left
	map.fit = {{18, 18}};
	place = Walk(map, 30, 30, 6);
	CHECK((place.found && place.x == 18 && place.y == 18));

	// candidates off the map are passed over
	map.fit = {{2, 8}, {8, 8}};
	place = Walk(map, 2, 2, 6);
	CHECK((place.found && place.x == 8 && place.y == 8));
	CHECK(map.asked == std::vector<std::pair<int, int>>{{8, 2}, {8, 8}});

	// no candidate of the ring on the worker's ground: no second ring
	map.ground = false;
	map.allFit = true;
	CHECK_FALSE(Walk(map, 30, 30, 6).found);
	CHECK(map.asked.empty());
	map.ground = true;
	map.allFit = false;

	// the limit: (greatest distance to an edge) - 1 = 33; sides 18 and 30 are walked, 42 is not
	map.fit = {{12, 12}};
	CHECK_FALSE(Walk(map, 30, 30, 6).found);
	map.fit = {{18, 42}};
	place = Walk(map, 30, 30, 6);
	CHECK((place.found && place.x == 18 && place.y == 42));

	CHECK_FALSE(wc2ai::PlaceWalk(30, 30, 0, 64, WalkGround, WalkFits, &map).found);
}

TEST_CASE("computer: place: the step of a building type")
{
	CHECK(wc2ai::PlaceStep(0x3A) == 2); // farm
	CHECK(wc2ai::PlaceStep(0x3B) == 2);
	CHECK(wc2ai::PlaceStep(0x3C) == 6); // barracks
	CHECK(wc2ai::PlaceStep(0x47) == 6);
	CHECK(wc2ai::PlaceStep(0x4C) == 6); // lumber mill
	CHECK(wc2ai::PlaceStep(0x53) == 6); // blacksmith
	CHECK(wc2ai::PlaceStep(0x60) == 6);
	CHECK(wc2ai::PlaceStep(0x63) == 6);
	CHECK(wc2ai::PlaceStep(0x48) == 1); // shipyard
	CHECK(wc2ai::PlaceStep(0x4A) == 1); // hall
	CHECK(wc2ai::PlaceStep(0x4E) == 1); // foundry
	CHECK(wc2ai::PlaceStep(0x55) == 1); // refinery
	CHECK(wc2ai::PlaceStep(0x5B) == 1);
	CHECK(wc2ai::PlaceStep(0x56) == 0); // oil platform: never placed by the walk
	CHECK(wc2ai::PlaceStep(0x5C) == 0); // gold mine
	CHECK(wc2ai::PlaceStep(0x02) == 0);
	CHECK(wc2ai::PlaceStep(-1) == 0);
}

TEST_CASE("computer: repair: the building under three quarters with the greatest priority")
{
	CHECK(wc2ai::RepairersAllowed(9) == 5);
	CHECK(wc2ai::RepairersAllowed(1) == 1);
	CHECK(wc2ai::RepairersAllowed(2) == 1);
	CHECK(wc2ai::RepairersAllowed(3) == 2);

	const auto building = [](int hp, int maxHp, int priority) {
		wc2ai::RepairView view;
		view.finishedBuilding = true;
		view.sameGround = true;
		view.hp = hp;
		view.maxHp = maxHp;
		view.priority = priority;
		return view;
	};
	// under three quarters: 3 * 400 / 4 = 300, strictly below
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, {building(299, 400, 20)}) == 0);
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, {building(300, 400, 20)}) == -1);
	// as many repair as may: nobody more
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 4, {building(299, 400, 20)}) == 0);
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 5, {building(299, 400, 20)}) == -1);
	CHECK(wc2ai::RepairPick(1000, 1000, 1, 1, {building(299, 400, 20)}) == -1);

	std::vector<wc2ai::RepairView> units = {building(100, 400, 20)};
	units[0].oilPlatform = true;
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, units) == -1);
	units[0].oilPlatform = false;
	units[0].enemyFighterNear = true;
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, units) == -1);
	units[0].enemyFighterNear = false;
	units[0].sameGround = false;
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, units) == -1);
	units[0].sameGround = true;
	units[0].finishedBuilding = false;
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, units) == -1);

	// poor (under 200 gold AND under 100 lumber): only the hall
	units = {building(100, 400, 35), building(100, 1200, 20)};
	units[1].hallClass = true;
	CHECK(wc2ai::RepairPick(199, 99, 9, 0, units) == 1);
	CHECK(wc2ai::RepairPick(199, 100, 9, 0, units) == 0);
	CHECK(wc2ai::RepairPick(200, 99, 9, 0, units) == 0);

	// the greatest priority, the first in the list of equals; 0 never
	units = {building(100, 400, 20), building(100, 400, 35), building(100, 400, 35)};
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, units) == 1);
	units = {building(100, 400, 0), building(100, 400, 0)};
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, units) == -1);
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, {}) == -1);
}

TEST_CASE("computer: repair: the priority is the type's annoy-computer factor")
{
	const auto building = [](int factor) {
		wc2ai::RepairView view;
		view.finishedBuilding = true;
		view.sameGround = true;
		view.hp = 100;
		view.maxHp = 400;
		view.priority = factor;
		return view;
	};
	// the original's factors: farm 45, keep 40, guard / cannon tower 50 (the port had 60 / 70)
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, {building(45), building(40), building(50)}) == 2);
	// two towers: the first of equals; a later farm does not pass a tower
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, {building(50), building(50), building(45)}) == 0);
	// a factor of 0 never wins
	CHECK(wc2ai::RepairPick(1000, 1000, 9, 0, {building(0)}) == -1);
}

TEST_CASE("computer: place: shipyard patch, shore works, planned sites and the tanker's platform")
{
	// the shore buildings and the halls go one tile apart
	CHECK(wc2ai::PlaceStep(0x48) == 1);
	CHECK(wc2ai::PlaceStep(0x49) == 1);
	CHECK(wc2ai::PlaceStep(0x4A) == 1);
	CHECK(wc2ai::PlaceStep(0x4B) == 1);
	CHECK(wc2ai::PlaceStep(0x4E) == 1);
	CHECK(wc2ai::PlaceStep(0x55) == 1);
	CHECK(wc2ai::PlaceStep(0x56) == 0);

	// nearest: the shipyard's patch takes the later of equals, the first hall's mine the first
	CHECK(wc2ai::NearestOf({7, 3, 5, 3}, true) == 3);
	CHECK(wc2ai::NearestOf({7, 3, 5, 3}, false) == 1);
	CHECK(wc2ai::NearestOf({-1, 9, -1}, true) == 1);
	CHECK(wc2ai::NearestOf({-1, -1}, false) == -1);
	CHECK(wc2ai::NearestOf({}, true) == -1);

	// foundry / refinery only with a finished shipyard; others never ask
	for (int type : {0x4E, 0x4F, 0x54, 0x55}) {
		CHECK_FALSE(wc2ai::ShipyardWorkAllowed(type, 0));
		CHECK(wc2ai::ShipyardWorkAllowed(type, 1));
	}
	CHECK(wc2ai::ShipyardWorkAllowed(0x48, 0));
	CHECK(wc2ai::ShipyardWorkAllowed(0x3C, 0));

	// a planned site blocks every square that overlaps it
	const std::vector<wc2ai::Site> sites = {{10, 10, 3, 3}};
	CHECK(wc2ai::SiteTaken(10, 10, 2, 2, sites));
	CHECK(wc2ai::SiteTaken(8, 8, 3, 3, sites));
	CHECK(wc2ai::SiteTaken(12, 12, 4, 4, sites));
	CHECK_FALSE(wc2ai::SiteTaken(13, 10, 2, 2, sites));
	CHECK_FALSE(wc2ai::SiteTaken(7, 7, 3, 3, sites));
	CHECK_FALSE(wc2ai::SiteTaken(10, 10, 3, 3, {}));

	// the tanker's platform: lowest d(depot, platform) + d(platform, tanker) / 2, first of equals
	const auto platform = [](bool usable, int toDepot, int toTanker) {
		wc2ai::PlatformView view;
		view.usable = usable;
		view.toDepot = toDepot;
		view.toTanker = toTanker;
		return view;
	};
	CHECK(wc2ai::PlatformPick({platform(true, 10, 2), platform(true, 6, 9)}) == 1);     // 11 against 10
	CHECK(wc2ai::PlatformPick({platform(true, 10, 3), platform(true, 10, 2)}) == 0);    // 11 and 11
	CHECK(wc2ai::PlatformPick({platform(false, 1, 1), platform(true, 30, 30)}) == 1);
	CHECK(wc2ai::PlatformPick({platform(false, 1, 1)}) == -1);

	// run 62: with none finished, the second tanker waits beside the one under construction
	const auto site = [](int toDepot, int toTanker) {
		wc2ai::PlatformView view;
		view.building = true;
		view.toDepot = toDepot;
		view.toTanker = toTanker;
		return view;
	};
	CHECK(wc2ai::PlatformWaitPick({site(10, 2)}) == 0);
	CHECK(wc2ai::PlatformWaitPick({platform(false, 1, 1), site(14, 4)}) == 1);
	CHECK(wc2ai::PlatformWaitPick({site(20, 2), site(10, 4)}) == 1);      // 21 against 12
	CHECK(wc2ai::PlatformWaitPick({site(10, 2), site(10, 3)}) == 0);      // first of equals
	CHECK(wc2ai::PlatformWaitPick({platform(true, 30, 30), site(1, 1)}) == -1);  // one is done: go there
	CHECK(wc2ai::PlatformWaitPick({platform(false, 1, 1)}) == -1);
	CHECK(wc2ai::PlatformWaitPick({}) == -1);
}

TEST_CASE("computer: hit: who runs and where to")
{
	for (int type : {0x02, 0x03, 0x0A, 0x0B, 0x0E, 0x0F, 0x15, 0x18}) {
		CHECK(wc2ai::FleeKind(type));
	}
	for (int type : {-1, 0x00, 0x01, 0x04, 0x08, 0x0C, 0x10, 0x1A, 0x1B}) {
		CHECK_FALSE(wc2ai::FleeKind(type));
	}

	CHECK(wc2ai::Direction8(0) == 0);    // north
	CHECK(wc2ai::Direction8(32) == 1);   // north-east
	CHECK(wc2ai::Direction8(64) == 2);   // east
	CHECK(wc2ai::Direction8(160) == 5);  // south-west
	CHECK(wc2ai::Direction8(240) == 0);  // nearer north than north-west
	CHECK(wc2ai::Direction8(255) == 0);

	// attacker facing east: four tiles east, -2..+5 on each axis
	wc2ai::Place place = wc2ai::FleePoint(20, 20, 2, 0, 0, 64, 64);
	CHECK(place.found);
	CHECK(place.x == 22);
	CHECK(place.y == 18);
	place = wc2ai::FleePoint(20, 20, 2, 7, 7, 64, 64);
	CHECK(place.x == 29);
	CHECK(place.y == 25);
	// south-west, the random value taken as 0..7
	place = wc2ai::FleePoint(20, 20, 5, 10, 2, 64, 64);
	CHECK(place.x == 16);
	CHECK(place.y == 24);
	// kept on the map
	place = wc2ai::FleePoint(1, 62, 5, 0, 7, 64, 64);
	CHECK(place.x == 0);
	CHECK(place.y == 63);
}

TEST_CASE("computer: further hall: the mine's stock, the hall window and the distance cut")
{
	// stock word >= 50, taken as hundreds of gold (NEEDS LIVE)
	CHECK(wc2ai::FurtherHallMineStock(5000));
	CHECK_FALSE(wc2ai::FurtherHallMineStock(4999));
	CHECK_FALSE(wc2ai::FurtherHallMineStock(4000));
	// an enemy (any) hall within 5 tiles of the mine refuses it: window (x - 5 .. x + 7) on both axes
	const int mx = 40;
	const int my = 40;
	CHECK(wc2ai::FurtherHallWindowHit(mx, my, 44, 44, 4, 4));
	CHECK(wc2ai::FurtherHallWindowHit(mx, my, 47, 40, 4, 4));   // left column on mine.x + 7
	CHECK_FALSE(wc2ai::FurtherHallWindowHit(mx, my, 48, 40, 4, 4));
	CHECK(wc2ai::FurtherHallWindowHit(mx, my, 32, 32, 4, 4));   // last tile on mine.x - 5
	CHECK_FALSE(wc2ai::FurtherHallWindowHit(mx, my, 31, 32, 4, 4));
	CHECK_FALSE(wc2ai::FurtherHallWindowHit(mx, my, 40, 31, 4, 4));
	// distance from the place's top-left tile to the mine's nearest tile (3 x 3 mine)
	CHECK(wc2ai::PointToBoxDistance(40, 40, mx, my, 3, 3) == 0);
	CHECK(wc2ai::PointToBoxDistance(43, 41, mx, my, 3, 3) == 1);
	CHECK(wc2ai::PointToBoxDistance(36, 30, mx, my, 3, 3) == 10);
	CHECK(wc2ai::PointToBoxDistance(53, 42, mx, my, 3, 3) == 11);
	// a place 11 tiles away is dropped
	CHECK(wc2ai::FurtherHallKeeps(10));
	CHECK_FALSE(wc2ai::FurtherHallKeeps(11));
	// the first of equal mines wins, a strictly nearer one replaces it
	CHECK(wc2ai::NearestOf({4, 4, 4}, false) == 0);
	CHECK(wc2ai::NearestOf({-1, 6, 3, 3}, false) == 2);
}

TEST_CASE("computer: reserved tiles: kinds, squares, roads and the saved text")
{
	using wc2ai::ReserveKind;
	CHECK(wc2ai::ReserveKindOf(0x3A) == ReserveKind::None);   // farm
	CHECK(wc2ai::ReserveKindOf(0x40) == ReserveKind::None);   // scout tower
	CHECK(wc2ai::ReserveKindOf(0x56) == ReserveKind::None);   // oil platform
	CHECK(wc2ai::ReserveKindOf(0x5C) == ReserveKind::None);   // gold mine
	CHECK(wc2ai::ReserveKindOf(0x3C) == ReserveKind::Border); // barracks
	CHECK(wc2ai::ReserveKindOf(0x4B) == ReserveKind::Border); // great hall
	CHECK(wc2ai::ReserveKindOf(0x5B) == ReserveKind::Border); // fortress
	CHECK(wc2ai::ReserveKindOf(0x48) == ReserveKind::Coast);  // shipyard
	CHECK(wc2ai::ReserveKindOf(0x4F) == ReserveKind::Coast);  // foundry
	CHECK(wc2ai::ReserveKindOf(0x55) == ReserveKind::Coast);  // refinery

	const int w = 16;
	const int h = 12;
	std::vector<uint8_t> tiles(w * h, 0);
	// a planned 3 x 3 site at (5, 5): the border only
	wc2ai::ReserveSquare(tiles, w, h, 5, 5, 3, 1, false);
	CHECK(wc2ai::ReservedAny(tiles, w, h, 4, 4, 1, 1));
	CHECK(wc2ai::ReservedAny(tiles, w, h, 8, 8, 1, 1));
	CHECK_FALSE(wc2ai::ReservedAny(tiles, w, h, 5, 5, 3, 3));
	CHECK_FALSE(wc2ai::ReservedAny(tiles, w, h, 9, 5, 3, 3));
	// a building touching the border is refused: the computer's buildings keep a gap of one tile
	CHECK(wc2ai::ReservedAny(tiles, w, h, 8, 5, 3, 3));
	// a map-placed building: the footprint too, cut at the map edge
	wc2ai::ReserveSquare(tiles, w, h, 0, 0, 2, 1, true);
	CHECK(wc2ai::ReservedAny(tiles, w, h, 0, 0, 1, 1));
	CHECK(wc2ai::ReservedAny(tiles, w, h, 2, 2, 1, 1));
	CHECK_FALSE(wc2ai::ReservedAny(tiles, w, h, 3, 0, 1, 3));
	// off the map is never reserved
	CHECK_FALSE(wc2ai::ReservedAny(tiles, w, h, -3, -3, 2, 2));

	// the road: a line from the depot toward the mine, each tile after its step
	// (0x4B7890 / 0x4B7630: error from half the major length, the minor step when it falls to 0)
	const auto road = wc2ai::RoadTiles(10, 10, 16, 13);
	REQUIRE(road.size() == 5);
	CHECK(road.front().x == 11);
	CHECK(road.front().y == 11);
	CHECK(road.back().x == 15);
	CHECK(road.back().y == 13);
	for (size_t i = 1; i < road.size(); ++i) {
		CHECK(std::abs(road[i].x - road[i - 1].x) <= 1);
		CHECK(std::abs(road[i].y - road[i - 1].y) <= 1);
	}
	// ends where the mine's row or column is reached
	CHECK((road.back().x == 16 || road.back().y == 13));
	const auto diagonal = wc2ai::RoadTiles(0, 0, 4, 4);
	REQUIRE(diagonal.size() == 4);
	CHECK(diagonal[3].x == 4);
	CHECK(diagonal[3].y == 4);
	// a steep line stops when it reaches the mine's column, short of its row
	const auto steep = wc2ai::RoadTiles(10, 10, 12, 20);
	REQUIRE(steep.size() == 8);
	CHECK(steep.back().x == 12);
	CHECK(steep.back().y == 18);
	// a long flat line: 15 tiles, the minor steps at x = 15 and x = 25, stops on the mine's row
	const auto flat = wc2ai::RoadTiles(10, 10, 30, 12);
	REQUIRE(flat.size() == 15);
	for (const wc2ai::Place &tile : flat) {
		CHECK(tile.y == (tile.x < 15 ? 10 : tile.x < 25 ? 11 : 12));
	}
	CHECK(flat.back().x == 25);
	// one tile each way: the error starts at 1, not 0
	const auto one = wc2ai::RoadTiles(10, 10, 11, 11);
	REQUIRE(one.size() == 1);
	CHECK(one[0].x == 11);
	CHECK(one[0].y == 11);

	// saved text and back
	CHECK(wc2ai::ReservedText(std::vector<uint8_t>(10, 0)).empty());
	const std::vector<uint8_t> marks{0, 1, 1, 0, 0, 1, 0, 1, 1, 1};
	CHECK(wc2ai::ReservedText(marks) == "1,2,5,1,7,3");
	CHECK(wc2ai::ReservedFromText("1,2,5,1,7,3", 10) == marks);
	CHECK(wc2ai::ReservedFromText(wc2ai::ReservedText(tiles), tiles.size()) == tiles);
	CHECK(wc2ai::ReservedFromText("8,5", 10) == std::vector<uint8_t>{0, 0, 0, 0, 0, 0, 0, 0, 1, 1});
	CHECK(wc2ai::ReservedFromText("", 3) == std::vector<uint8_t>(3, 0));
}

TEST_CASE("computer: reserved tiles: what the haul-way rule covers that the marks do not")
{
	// A depot and a mine on one row or one column: the original marks no road at all, so the
	// port's haul-way rule (AiPlaceOnHaulWay) is the only thing keeping that way free.
	CHECK(wc2ai::RoadTiles(10, 10, 20, 10).empty());
	CHECK(wc2ai::RoadTiles(10, 10, 10, 20).empty());
	// A road stops at the mine's row or column: the rest of the way is not marked.
	const auto road = wc2ai::RoadTiles(10, 10, 30, 12);
	REQUIRE_FALSE(road.empty());
	CHECK(road.back().y == 12);
	CHECK(road.back().x < 30);
}

namespace
{
bool HaulOpenAll(int, int, void *)
{
	return true;
}
bool HaulOpenNotRow7(int, int y, void *)
{
	return y != 7;
}
} // namespace

TEST_CASE("computer: haul way: a building beside the mine-hall line is allowed (live run 43, item 2)")
{
	// Plains of snow, run 43: gold mine 3x3 at (11, 6), Great Hall 4x4 at (17, 9).
	const wc2ai::Site hall{17, 9, 4, 4};
	const wc2ai::Site mine{11, 6, 3, 3};
	const auto line = wc2ai::HaulLineTiles(hall, mine);
	REQUIRE(line.size() == 3);
	CHECK((line[0].x == 16 && line[0].y == 9));
	CHECK((line[1].x == 15 && line[1].y == 9));
	CHECK((line[2].x == 14 && line[2].y == 8));
	// the same tiles either way round
	const auto back = wc2ai::HaulLineTiles(mine, hall);
	REQUIRE(back.size() == 3);
	CHECK((back[0].x == 14 && back[0].y == 8));
	CHECK((back[2].x == 16 && back[2].y == 9));

	// the original's Pig Farms (2x2): beside the line at (13, 9), south of the mine at (11, 9),
	// north of it at (15, 5) - all allowed
	CHECK_FALSE(wc2ai::HaulLineBlocked(13, 9, 2, 2, line, HaulOpenAll, nullptr));
	CHECK_FALSE(wc2ai::HaulLineBlocked(11, 9, 2, 2, line, HaulOpenAll, nullptr));
	CHECK_FALSE(wc2ai::HaulLineBlocked(15, 5, 2, 2, line, HaulOpenAll, nullptr));
	// touching the line only at a corner
	CHECK_FALSE(wc2ai::HaulLineBlocked(13, 9, 1, 1, line, HaulOpenAll, nullptr));
	CHECK_FALSE(wc2ai::HaulLineBlocked(15, 7, 1, 1, line, HaulOpenAll, nullptr));
	// on the line: refused
	CHECK(wc2ai::HaulLineBlocked(14, 7, 2, 2, line, HaulOpenAll, nullptr));
	CHECK(wc2ai::HaulLineBlocked(15, 9, 1, 1, line, HaulOpenAll, nullptr));
	// beside the line with the ground across it closed (the way would be one tile wide): refused
	CHECK(wc2ai::HaulLineBlocked(13, 9, 2, 2, line, HaulOpenNotRow7, nullptr));
	// a farm whose corner meets the line across closed ground is still allowed
	CHECK_FALSE(wc2ai::HaulLineBlocked(13, 9, 1, 1, line, HaulOpenNotRow7, nullptr));

	// a depot and a mine on one row: the line between them (the original marks no road there)
	const auto row = wc2ai::HaulLineTiles({10, 10, 4, 4}, {20, 10, 3, 3});
	REQUIRE(row.size() == 6);
	for (size_t i = 0; i < row.size(); ++i) {
		CHECK(row[i].x == 14 + static_cast<int>(i));
		CHECK(row[i].y == (i < 3 ? 12 : 11)); // centre rows 12 and 11: the step at half way
	}
}

namespace
{
bool HaulOpenNotFarm115_64(int x, int y, void *)
{
	return !(x >= 115 && x <= 116 && y >= 64 && y <= 65);
}
} // namespace

TEST_CASE("computer: haul way: farms touching the computer's own gold mine (live run 53, 2 of 2)")
{
	// Forsaken Isles BNE, run 53: Town Hall 4x4 at (119, 64), gold mine 3x3 at (112, 64). The
	// original's computer put Farms (2x2) touching the mine's E side: 115,66 and 115,68 (games 1
	// and 2), 115,62 and 115,64 (game 2).
	const wc2ai::Site hall{119, 64, 4, 4};
	const wc2ai::Site mine{112, 64, 3, 3};
	const auto line = wc2ai::HaulLineTiles(hall, mine);
	REQUIRE(line.size() == 4);
	CHECK((line[0].x == 118 && line[0].y == 66));
	CHECK((line[3].x == 115 && line[3].y == 65));
	// the hall and the mine share rows: the original marks no road (nothing reserved between them)
	CHECK(wc2ai::RoadTiles(hall.x, hall.y, mine.x, mine.y).empty());
	// touching the mine is allowed: 115,66, 115,68 and 115,62 pass the port's haul-way rule
	CHECK_FALSE(wc2ai::HaulLineBlocked(115, 66, 2, 2, line, HaulOpenAll, nullptr));
	CHECK_FALSE(wc2ai::HaulLineBlocked(115, 68, 2, 2, line, HaulOpenAll, nullptr));
	CHECK_FALSE(wc2ai::HaulLineBlocked(115, 62, 2, 2, line, HaulOpenAll, nullptr));
	// the one difference: 115,64 covers the mine-hall line; the port's safety rule (not the
	// original's) refuses it, and with it standing 115,66 would leave the way one tile wide
	CHECK(wc2ai::HaulLineBlocked(115, 64, 2, 2, line, HaulOpenAll, nullptr));
	CHECK(wc2ai::HaulLineBlocked(115, 66, 2, 2, line, HaulOpenNotFarm115_64, nullptr));
	// the four live sites lie on the farm grid (step 2) round the hall: the walk reaches each one
	for (const auto &site : std::vector<std::pair<int, int>>{{115, 66}, {115, 68}, {115, 62}, {115, 64}}) {
		WalkMap map;
		map.fit = {site};
		const wc2ai::Place place = Walk(map, hall.x, hall.y, wc2ai::PlaceStep(0x3A), 128);
		CHECK((place.found && place.x == site.first && place.y == site.second));
	}
}

TEST_CASE("target score: the classes, Priority less d * d / 4, the floor 1")
{
	// the four classes (Priority 0, distance 0)
	CHECK(wc2ai::TargetScore(false, false, true, 0, 0) == 1);
	CHECK(wc2ai::TargetScore(false, true, true, 0, 0) == 1);
	CHECK(wc2ai::TargetScore(true, false, false, 0, 0) == 0x10000);
	CHECK(wc2ai::TargetScore(true, true, false, 0, 0) == 0x10000);
	CHECK(wc2ai::TargetScore(true, false, true, 0, 0) == 0x20000);
	CHECK(wc2ai::TargetScore(true, true, true, 0, 0) == 0x30000);
	// a footman at 2 tiles beats a peasant at 1 (both in range): 60 - 1 against 50 - 0
	CHECK(wc2ai::TargetScore(true, false, true, 60, 2) == 0x20000 + 59);
	CHECK(wc2ai::TargetScore(true, false, true, 50, 1) == 0x20000 + 50);
	// out of range: a ballista at 6 (70 - 9) beats a footman at 4 (60 - 4)
	CHECK(wc2ai::TargetScore(true, false, false, 70, 6) == 0x10000 + 61);
	CHECK(wc2ai::TargetScore(true, false, false, 60, 4) == 0x10000 + 56);
	// any armed enemy beats any unarmed one; an armed one in range beats one out of range
	CHECK(wc2ai::TargetScore(true, false, false, 0, 20) > wc2ai::TargetScore(false, false, true, 255, 0));
	CHECK(wc2ai::TargetScore(true, false, true, 0, 4) > wc2ai::TargetScore(true, false, false, 255, 5));
	// d * d / 4 rounds down: 3 -> 2, 5 -> 6, 7 -> 12
	CHECK(wc2ai::TargetScore(true, false, true, 40, 3) == 0x20000 + 38);
	CHECK(wc2ai::TargetScore(true, false, true, 40, 5) == 0x20000 + 34);
	CHECK(wc2ai::TargetScore(true, false, true, 40, 7) == 0x20000 + 28);
	// the floor: an unarmed target far off still scores 1
	CHECK(wc2ai::TargetScore(false, false, false, 20, 30) == 1);
	CHECK(wc2ai::TargetScore(false, false, true, 20, 9) == 1);
	CHECK(wc2ai::TargetScore(false, false, true, 20, 8) == 4);

	// the minimum distance by type number
	CHECK(wc2ai::MinSearchDistance(0x04) == 2); // ballista
	CHECK(wc2ai::MinSearchDistance(0x05) == 2); // catapult
	CHECK(wc2ai::MinSearchDistance(0x1E) == 1); // destroyers
	CHECK(wc2ai::MinSearchDistance(0x1F) == 1);
	CHECK(wc2ai::MinSearchDistance(0x20) == 1); // battleship, juggernaut
	CHECK(wc2ai::MinSearchDistance(0x21) == 1);
	CHECK(wc2ai::MinSearchDistance(0x62) == 1); // cannon towers
	CHECK(wc2ai::MinSearchDistance(0x63) == 1);
	CHECK(wc2ai::MinSearchDistance(0x00) == 0); // footman
	CHECK(wc2ai::MinSearchDistance(0x60) == 0); // guard tower
	CHECK(wc2ai::MinSearchDistance(-1) == 0);
	CHECK(wc2ai::SubmarineKind(0x26));
	CHECK(wc2ai::SubmarineKind(0x27));
	CHECK_FALSE(wc2ai::SubmarineKind(0x20));
}

TEST_CASE("ship hit, ORIG47 E4: the Footman (0x20038) beats the Archer (0x20036) nearer to the ship")
{
	// ship top-left 56,52, range 4; Footman 55,48 d 4, Priority 60; Archer 54,50 d 2, Priority 55
	const int footman = wc2ai::TargetScore(true, false, true, 60, 4);
	const int archer = wc2ai::TargetScore(true, false, true, 55, 2);
	CHECK(footman == 0x20038);
	CHECK(archer == 0x20036);
	CHECK(footman > archer);
}

TEST_CASE("patrol memory: flyers and ships on a nearly straight leg are not remembered")
{
	// a footman: every leg
	CHECK(wc2ai::PatrolRememberLeg(false, 5, 0));
	CHECK(wc2ai::PatrolRememberLeg(false, 0, 1));
	CHECK(wc2ai::PatrolRememberLeg(false, 0, 0));
	// a ship on a horizontal or vertical leg, or one tile off it
	CHECK_FALSE(wc2ai::PatrolRememberLeg(true, 8, 0));
	CHECK_FALSE(wc2ai::PatrolRememberLeg(true, -8, 1));
	CHECK_FALSE(wc2ai::PatrolRememberLeg(true, 0, -6));
	CHECK_FALSE(wc2ai::PatrolRememberLeg(true, 1, 6));
	// a flyer off both axes by more than one tile
	CHECK(wc2ai::PatrolRememberLeg(true, 3, 3));
	CHECK(wc2ai::PatrolRememberLeg(true, -2, 2));
	CHECK_FALSE(wc2ai::PatrolRememberLeg(true, 2, 1));
}

TEST_CASE("patrol memory: the re-issue waits 40 game cycles after the last one")
{
	CHECK(wc2ai::PatrolReissueWait == 40);
	// counter at 0 (a new leg): at once
	CHECK(wc2ai::PatrolReissueReady(0, 0));
	CHECK(wc2ai::PatrolReissueReady(1234, 0));
	// re-issued at cycle 1000: not at 1039, at 1040
	const unsigned long at = 1000 + wc2ai::PatrolReissueWait;
	CHECK_FALSE(wc2ai::PatrolReissueReady(1000, at));
	CHECK_FALSE(wc2ai::PatrolReissueReady(1039, at));
	CHECK(wc2ai::PatrolReissueReady(1040, at));
	CHECK(wc2ai::PatrolReissueReady(1100, at));
}

TEST_CASE("attack-move memory: sent again from 2 tiles with a 20-cycle counter, forgotten within 1")
{
	using wc2ai::Reissue;
	// counter at 0: 2 or more tiles on either axis -> sent again
	CHECK(wc2ai::AttackMoveReissue(500, 0, 2, 0) == Reissue::Ready);
	CHECK(wc2ai::AttackMoveReissue(500, 0, 0, -2) == Reissue::Ready);
	CHECK(wc2ai::AttackMoveReissue(500, 0, -7, 1) == Reissue::Ready);
	// within 1 on both axes (or on the point) -> forgotten
	CHECK(wc2ai::AttackMoveReissue(500, 0, 0, 0) == Reissue::Forget);
	CHECK(wc2ai::AttackMoveReissue(500, 0, 1, -1) == Reissue::Forget);
	CHECK(wc2ai::AttackMoveReissue(500, 0, -1, 1) == Reissue::Forget);
	// re-issued at 1000: the counter runs until 1020 -> stand, keep it, whatever the distance
	const unsigned long at = 1000 + wc2ai::AttackMoveRetryWait;
	CHECK(wc2ai::AttackMoveReissue(1000, at, 5, 5) == Reissue::Wait);
	CHECK(wc2ai::AttackMoveReissue(1019, at, 5, 5) == Reissue::Wait);
	CHECK(wc2ai::AttackMoveReissue(1019, at, 0, 0) == Reissue::Wait);
	CHECK(wc2ai::AttackMoveReissue(1020, at, 5, 5) == Reissue::Ready);
	CHECK(wc2ai::AttackMoveReissue(1020, at, 1, 0) == Reissue::Forget);
}
