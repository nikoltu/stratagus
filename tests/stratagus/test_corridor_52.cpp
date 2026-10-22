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
/**@name test_corridor_52.cpp - A Footman ordered past a Farm wall with a 2-tile dead-end gap (ORIG52 item 1). */
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

#include "action/action_move.h"
#include "actions.h"
#include "animation.h"
#include "animation/animation_move.h"
#include "animation/animation_wait.h"
#include "goal_pull.h"
#include "group_move.h"
#include "map.h"
#include "pathfinder.h"
#include "player.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"

#include <algorithm>
#include <string>
#include <vector>

// Live run 52, retry 2 (ORIG52.md, "Item 1"): structure S2' on a 128 x 128 map. Farms (2 x 2) on
// row 112-113 at x 30, 32, 34 | gap x 36 | 37, 39, 41, 43, 45; cap Farms 35,114 and 37,114. The gap
// 36,112-36,113 is a dead end open to the north; the free wall ends are x 29 (W, short) and x 47
// (E, long). The live map round the wall is open ground; here the whole map is.

namespace
{
constexpr tile_flags LandMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding | MapFieldCoastAllowed
                                | MapFieldWaterAllowed | MapFieldUnpassable;

/// The move animation goes a whole tile at once: the unit stands one cycle on each tile of a free
/// walk here (16 frames live). "Back after one step time, no pause" is one cycle on the last tile.
constexpr int CyclesPerStep = 1;

class CorridorWorld
{
public:
	CorridorWorld()
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		savedGameCycle = GameCycle;
		if (UnitManager == nullptr) {
			UnitManager = new CUnitManager;
		}
		UnitManager->Init();
		UnitTypeVar.Init();
		Map.Info.MapWidth = 128;
		Map.Info.MapHeight = 128;
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
		for (int y = 0; y < 128; ++y) {
			for (int x = 0; x < 128; ++x) {
				Map.Field(x, y)->Flags = MapFieldLandAllowed;
			}
		}

		auto move = std::make_unique<CAnimation_Move>();
		move->Init("32", nullptr);
		auto wait = std::make_unique<CAnimation_Wait>();
		wait->Init("1", nullptr);
		animations.Move.push_back(std::move(move));
		animations.Move.push_back(std::move(wait));
		auto still = std::make_unique<CAnimation_Wait>();
		still->Init("1", nullptr);
		animations.Still.push_back(std::move(still));

		MakeType(footman, "unit-test-footman", 0, 1, MapFieldLandUnit);
		MakeType(farm, "unit-test-farm", 1, 2, MapFieldBuilding);
		farm.Building = true;
		farm.Animations = nullptr;

		for (short x : {30, 32, 34, 37, 39, 41, 43, 45}) {
			Place(farm, {x, 112});
		}
		Place(farm, {35, 114});
		Place(farm, {37, 114});
	}

	~CorridorWorld()
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

	/// One cycle of the unit's order loop.
	void Cycle(CUnit &unit)
	{
		++GameCycle;
		COrder *order = unit.CurrentOrder();
		if (order->Action == UnitAction::Still) {
			return;
		}
		if (unit.Wait) {
			--unit.Wait;
			return;
		}
		order->Execute(unit);
		if (unit.CurrentOrder()->Finished) {
			unit.Orders.erase(unit.Orders.begin());
			if (unit.Orders.empty()) {
				unit.Orders.push_back(COrder::NewActionStill());
			}
		}
	}

	CUnitType footman;
	CUnitType farm;
	CAnimations animations;

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

/// The measures of ORIG52: arrival, the end gone round, the dead end and the time on its last tile.
struct Walk {
	std::vector<Vec2i> tiles;   /// tiles stood on, in order (the first is the start)
	std::vector<int> cycles;    /// cycles stood on each
	bool still = false;         /// the order ended
	int longestStand = 0;       /// the longest stand on one tile, in cycles
	int depth = 0;              /// dead-end tiles entered from the north: 1 = 36,112, 2 = 36,113
	int deepestCycles = 0;      /// cycles on the deepest dead-end tile before leaving it
	char side = '-';            /// 'W' round x 29, 'E' round x 47
	int firstDeadEndStep = -1;  /// the step that first entered the gap (-1 never)
};

Walk WalkIt(CorridorWorld &w, CUnit &unit)
{
	Walk walk;
	walk.tiles.push_back(unit.tilePos);
	walk.cycles.push_back(0);
	for (int i = 0; i != 2000 && unit.CurrentAction() != UnitAction::Still; ++i) {
		w.Cycle(unit);
		if (unit.tilePos == walk.tiles.back()) {
			++walk.cycles.back();
		} else {
			walk.tiles.push_back(unit.tilePos);
			walk.cycles.push_back(1);
		}
	}
	walk.still = unit.CurrentAction() == UnitAction::Still;
	for (size_t i = 0; i != walk.tiles.size(); ++i) {
		const Vec2i &p = walk.tiles[i];
		if (i + 1 != walk.tiles.size()) {
			walk.longestStand = std::max(walk.longestStand, walk.cycles[i]);
		}
		if (p.x == 36 && (p.y == 112 || p.y == 113)) {
			const int d = p.y - 111;
			if (walk.firstDeadEndStep < 0) {
				walk.firstDeadEndStep = int(i);
			}
			if (d > walk.depth) {
				walk.depth = d;
				walk.deepestCycles = walk.cycles[i];
			}
		}
		if (p.x <= 29 && p.y >= 111 && p.y <= 114) {
			walk.side = 'W';
		} else if (p.x >= 47 && p.y >= 111 && p.y <= 114) {
			walk.side = 'E';
		}
	}
	return walk;
}

std::string Text(const std::vector<Vec2i> &tiles)
{
	std::string text;
	for (const Vec2i &p : tiles) {
		text += std::to_string(p.x) + "," + std::to_string(p.y) + " ";
	}
	return text;
}

struct Case {
	const char *name;
	Vec2i start;
	Vec2i goal;
	int liveSteps;
	int liveDepth;   /// 0: round from the first step; 2: to the last dead-end tile
};

// ORIG52 retry 2, the 8 clean cases (N1b: the order was given at 37,105).
const Case cases[] = {
	{"N1a", {36, 104}, {36, 121}, 23, 0},
	{"N1c", {36, 104}, {36, 121}, 21, 0},
	{"N5a", {36, 105}, {36, 121}, 20, 0},
	{"S1a", {36, 121}, {36, 104}, 24, 1}, // passes 36,112 for one step from the side, then N
	{"N2a", {37, 105}, {36, 121}, 29, 2},
	{"N2b", {37, 105}, {36, 121}, 29, 2},
	{"N1b", {37, 105}, {36, 121}, 29, 2},
	{"N7a", {35, 105}, {36, 121}, 29, 2},
};
} // namespace

TEST_CASE("corridor 52: the Farm wall and the dead-end gap are laid as live")
{
	CorridorWorld w;
	CHECK(Map.Field(36, 112)->CheckMask(MapFieldBuilding) == false);
	CHECK(Map.Field(36, 113)->CheckMask(MapFieldBuilding) == false);
	CHECK(Map.Field(36, 114)->CheckMask(MapFieldBuilding));
	CHECK(Map.Field(35, 112)->CheckMask(MapFieldBuilding));
	CHECK(Map.Field(37, 113)->CheckMask(MapFieldBuilding));
	CHECK(Map.Field(30, 112)->CheckMask(MapFieldBuilding));
	CHECK(Map.Field(46, 113)->CheckMask(MapFieldBuilding));
	CHECK(Map.Field(29, 112)->CheckMask(MapFieldBuilding) == false);
	CHECK(Map.Field(47, 112)->CheckMask(MapFieldBuilding) == false);
	CHECK(Map.Field(38, 115)->CheckMask(MapFieldBuilding));
	CHECK(Map.Field(34, 114)->CheckMask(MapFieldBuilding) == false);
}

TEST_CASE("corridor 52: never a stop, always round the short (W) end, 8 of 8")
{
	for (const Case &c : cases) {
		CAPTURE(std::string(c.name));
		CorridorWorld w;
		CUnit &mover = w.Place(w.footman, c.start);
		mover.Orders.clear();
		mover.Orders.push_back(COrder::NewActionMove(c.goal));
		mover.pathFinderData->output.Length = 0;
		const Walk walk = WalkIt(w, mover);
		MESSAGE(std::string(c.name) << " ours " << walk.tiles.size() - 1 << " steps (live " << c.liveSteps
		                            << ", " << walk.cycles.size() << " tiles in " << GameCycle - 1000
		                            << " cycles), side " << std::string(1, walk.side) << ", dead-end depth " << walk.depth
		                            << " (live " << c.liveDepth << "), on the deepest tile "
		                            << walk.deepestCycles << " cycles, longest stand " << walk.longestStand
		                            << ": " << Text(walk.tiles));
		CHECK(walk.still);
		CHECK(mover.tilePos == c.goal);                   // reaches the goal (8 of 8)
		CHECK(walk.side == 'W');                          // the short end (8 of 8)
		CHECK(walk.longestStand <= CyclesPerStep);        // never waits (8 of 8)
	}
}

TEST_CASE("corridor 52: from the gap's column round the end from the first step (4 of 4)")
{
	for (const Case &c : cases) {
		if (c.liveDepth == 2) {
			continue;
		}
		CAPTURE(std::string(c.name));
		CorridorWorld w;
		CUnit &mover = w.Place(w.footman, c.start);
		mover.Orders.clear();
		mover.Orders.push_back(COrder::NewActionMove(c.goal));
		mover.pathFinderData->output.Length = 0;
		const Walk walk = WalkIt(w, mover);
		CHECK(walk.depth <= c.liveDepth); // N: never in the gap; S1a: 36,112 from the side at most
		CHECK(int(walk.tiles.size()) - 1 <= c.liveSteps);
	}
}

// Owner decision 86 (2026-10-18): the original walks into the dead end from one column off (4 of 4
// live); WargusHD keeps the Wargus pathfinder, which plans the whole way round at the order. Guarded so
// that choice stays: no entry into the dead end, and the goal is reached.
TEST_CASE("corridor 52: from one column off WargusHD goes round at once (owner decision 86)")
{
	for (const Case &c : cases) {
		if (c.liveDepth != 2) {
			continue;
		}
		CAPTURE(std::string(c.name));
		CorridorWorld w;
		CUnit &mover = w.Place(w.footman, c.start);
		mover.Orders.clear();
		mover.Orders.push_back(COrder::NewActionMove(c.goal));
		mover.pathFinderData->output.Length = 0;
		const Walk walk = WalkIt(w, mover);
		CHECK(walk.firstDeadEndStep < 0);
		CHECK(mover.tilePos == c.goal);
	}
}


// Live run 53, side rule B: in the original a move order 70 - 100 tiles away left the group idle
// near the straight-line closest reachable point (5 stalls of 5); waypoints 14 - 25 steps apart
// always arrived (~30 of 30). WargusHD keeps the Wargus pathfinder (owner decision 86): a long
// order, here across the Farm wall, reaches its goal (a search cut at AStarMaxSearchIterations
// walks to the best node found and plans again from there). Guarded until the owner decides.
TEST_CASE("long order 53: 70 - 100 tiles away WargusHD arrives, no stop short (owner to decide)")
{
	const Case longCases[] = {
		{"L81", {36, 40}, {36, 121}, 0, 0},
		{"L91", {5, 30}, {90, 121}, 0, 0},
		{"L100", {120, 20}, {30, 120}, 0, 0},
		{"W20", {36, 101}, {36, 121}, 0, 0}, // a waypoint-sized order across the wall
	};
	for (const Case &c : longCases) {
		CAPTURE(std::string(c.name));
		CorridorWorld w;
		CUnit &mover = w.Place(w.footman, c.start);
		mover.Orders.clear();
		mover.Orders.push_back(COrder::NewActionMove(c.goal));
		mover.pathFinderData->output.Length = 0;
		const Walk walk = WalkIt(w, mover);
		MESSAGE(std::string(c.name) << " ours " << walk.tiles.size() - 1 << " steps");
		CHECK(walk.still);
		CHECK(mover.tilePos == c.goal);
		CHECK(walk.longestStand <= CyclesPerStep);
	}
}

// Live run 54 item 5 (ORIG54.md): a 1-tile road y 42, x 24..31 (Farms 24..31 on rows 40-41 and
// 43-44, both ends open), two Peasants of one player sent head-on to beyond the other end, the
// second ~0.4 s (about one step) after the first. The original: the unit ordered second backs out
// of the whole road and goes round the wall, the first walks through; no swap, no deadlock, no
// order given up (3 of 3: L2, L3, L4). WargusHD is measured here against that.
namespace
{
struct LaneCase {
	const char *name;
	Vec2i firstStart, firstGoal;
	Vec2i secondStart, secondGoal;
	int delay; /// cycles between the two orders
};

const LaneCase laneCases[] = {
	{"L2 W first", {21, 42}, {35, 42}, {34, 42}, {20, 42}, 1},
	{"L3 E first", {34, 42}, {20, 42}, {21, 42}, {35, 42}, 1},
	{"L4 W first from further", {15, 42}, {35, 42}, {33, 42}, {20, 42}, 1},
};

bool InRoad(const Vec2i &p)
{
	return p.y == 42 && p.x >= 24 && p.x <= 31;
}

struct LaneWalk {
	std::vector<Vec2i> tiles;
	bool arrived = false;
	bool leftRoadBackwards = false; /// entered the road, then left it by the end it came in by
	int arrivalCycle = -1;
};

void Track(LaneWalk &walk, const CUnit &unit, const Vec2i &goal, int cycle)
{
	if (walk.tiles.empty() || walk.tiles.back() != unit.tilePos) {
		walk.tiles.push_back(unit.tilePos);
	}
	if (!walk.arrived && unit.tilePos == goal && unit.CurrentAction() == UnitAction::Still) {
		walk.arrived = true;
		walk.arrivalCycle = cycle;
	}
}

void Backed(LaneWalk &walk, int startX)
{
	bool entered = false;
	for (const Vec2i &p : walk.tiles) {
		if (InRoad(p)) {
			entered = true;
		} else if (entered && ((startX < 24 && p.x < 24) || (startX > 31 && p.x > 31))) {
			walk.leftRoadBackwards = true;
			return;
		}
	}
}
} // namespace

TEST_CASE("lane 54: head-on in a 1-tile road, the second backs out, the first walks through (3 of 3 live)")
{
	for (const LaneCase &c : laneCases) {
		CAPTURE(std::string(c.name));
		CorridorWorld w;
		for (short x : {24, 26, 28, 30}) {
			w.Place(w.farm, {x, 40});
			w.Place(w.farm, {x, 43});
		}
		CUnit &first = w.Place(w.footman, c.firstStart);
		CUnit &second = w.Place(w.footman, c.secondStart);
		first.Orders.clear();
		first.Orders.push_back(COrder::NewActionMove(c.firstGoal));
		first.pathFinderData->output.Length = 0;
		LaneWalk a, b;
		bool swapped = false;
		int cycle = 0;
		for (; cycle != 3000 && !(a.arrived && b.arrived); ++cycle) {
			if (cycle == c.delay) {
				second.Orders.clear();
				second.Orders.push_back(COrder::NewActionMove(c.secondGoal));
				second.pathFinderData->output.Length = 0;
			}
			const Vec2i pa = first.tilePos, pb = second.tilePos;
			w.Cycle(first);
			--GameCycle; // one game cycle for both units
			w.Cycle(second);
			if (first.tilePos == pb && second.tilePos == pa) {
				swapped = true;
			}
			Track(a, first, c.firstGoal, cycle);
			Track(b, second, c.secondGoal, cycle);
		}
		Backed(a, c.firstStart.x);
		Backed(b, c.secondStart.x);
		MESSAGE(std::string(c.name) << ": first " << std::string(a.arrived ? "arrived" : "NOT arrived") << " at cycle "
		        << a.arrivalCycle << " in " << a.tiles.size() << " steps, backed out " << a.leftRoadBackwards
		        << ": " << Text(a.tiles));
		MESSAGE(std::string(c.name) << ": second " << std::string(b.arrived ? "arrived" : "NOT arrived") << " at cycle "
		        << b.arrivalCycle << " in " << b.tiles.size() << " steps, backed out " << b.leftRoadBackwards
		        << ": " << Text(b.tiles));
		CHECK(a.arrived);   // no deadlock, no order given up
		CHECK(b.arrived);
		CHECK_FALSE(swapped);
		CHECK_FALSE(a.leftRoadBackwards); // the first walks through
		CHECK(b.leftRoadBackwards);       // the second backs out of the whole road
		CHECK(int(a.tiles.size()) == std::abs(c.firstGoal.x - c.firstStart.x)); // straight through (start not listed)
	}
}

// Live run 67 M1 (ORIG67 Retry: 3 trials, 12 unit runs): 4 Footmen in a 2 x 2 block, one order
// through a 1-tile gap in a Farm wall (gap 2 deep). Live: the front pair goes through; the rear
// units do not queue: when the gap tile is held by a moving mate they go round at once (5 of the
// 6 rear runs; 4 W, 1 E), one rear run stood 1.24 s (48 frames) and then took the gap. Here the
// Farm wall x 28-37 | gap 38 | 39-48 on rows 41-42, the way round about 3 x the way through (live
// 24 against 6). The move animation is slowed so that a unit is Moving for most of a step.
/// One M1 trial from `starts` (the first is a front unit); returns how many went round.
static int QueueTrial(const Vec2i (&starts)[4])
{
	CAnimations slow; // outlives the units of the world
	CorridorWorld w;
	auto move = std::make_unique<CAnimation_Move>();
	move->Init("2", nullptr);
	auto wait = std::make_unique<CAnimation_Wait>();
	wait->Init("1", nullptr);
	slow.Move.push_back(std::move(move));
	slow.Move.push_back(std::move(wait));
	auto still = std::make_unique<CAnimation_Wait>();
	still->Init("1", nullptr);
	slow.Still.push_back(std::move(still));
	w.footman.Animations = &slow;
	for (short x : {28, 30, 32, 34, 36, 39, 41, 43, 45, 47}) {
		w.Place(w.farm, {x, 41});
	}
	REQUIRE_FALSE(Map.Field(38, 41)->CheckMask(MapFieldBuilding));
	REQUIRE(Map.Field(37, 41)->CheckMask(MapFieldBuilding));
	REQUIRE(Map.Field(39, 42)->CheckMask(MapFieldBuilding));
	const Vec2i goal(34, 37);
	std::vector<CUnit *> units;
	for (const Vec2i &s : starts) {
		units.push_back(&w.Place(w.footman, s));
	}
	for (CUnit *u : units) { // one group order: the same cycle
		u->Orders.clear();
		u->Orders.push_back(COrder::NewActionMove(goal));
		u->pathFinderData->output.Length = 0;
	}
	struct Track {
		std::vector<Vec2i> tiles;
		int stand = 0;
		int longestStand = 0;
		bool gap = false;
		int endCycle = -1;
	} tracks[4];
	for (int i = 0; i != 4; ++i) {
		tracks[i].tiles.push_back(starts[i]);
	}
	int cycle = 0;
	for (; cycle != 4000; ++cycle) {
		bool all = true;
		for (int i = 0; i != 4; ++i) {
			CUnit &u = *units[i];
			Track &t = tracks[i];
			if (u.CurrentAction() == UnitAction::Still) {
				if (t.endCycle < 0) {
					t.endCycle = cycle;
				}
				continue;
			}
			all = false;
			w.Cycle(u);
			--GameCycle;
			if (u.tilePos == t.tiles.back()) {
				t.longestStand = std::max(t.longestStand, ++t.stand);
			} else {
				t.tiles.push_back(u.tilePos);
				t.stand = 0;
				if (u.tilePos.x == 38 && (u.tilePos.y == 41 || u.tilePos.y == 42)) {
					t.gap = true;
				}
			}
		}
		++GameCycle;
		if (all) {
			break;
		}
	}
	int round = 0;
	for (int i = 0; i != 4; ++i) {
		const Track &t = tracks[i];
		const CUnit &u = *units[i];
		round += !t.gap;
		MESSAGE("unit " << i << " from " << starts[i].x << "," << starts[i].y << ": "
		        << std::string(t.gap ? "through the gap" : "ROUND") << ", " << t.tiles.size() - 1
		        << " steps, done at cycle " << t.endCycle << ", longest stand " << t.longestStand
		        << " cycles, ends " << u.tilePos.x << "," << u.tilePos.y << ": " << Text(t.tiles));
		CHECK(u.CurrentAction() == UnitAction::Still);
		CHECK(std::max(std::abs(u.tilePos.x - goal.x), std::abs(u.tilePos.y - goal.y)) <= 2);
		// the longest stand live: 1.24 s = 48 frames (t3, 1 of 12)
		CHECK(t.longestStand <= 48);
	}
	MESSAGE("went round: " << round << " of 4");
	CHECK(tracks[0].gap); // a front unit goes through (live 10 of 10 front runs, runs 67 + 68)
	CHECK(round <= 2);    // only rear units
	return round;
}

TEST_CASE("queue 67: a group through a 1-tile gap - rear units held by a moving mate go round")
{
	// Live run 67 t1-t3 start block 38-39,45-46: 12 unit runs, 7 gap, 5 round.
	const Vec2i starts[4] = {{38, 45}, {39, 45}, {38, 46}, {39, 46}};
	const int round = QueueTrial(starts);
	CHECK(round >= 1); // a rear unit goes round rather than queue
}

TEST_CASE("queue 68: M1 ratio over both start blocks against runs 67 + 68 (13 gap / 7 round)")
{
	// Live run 68 x1 (start 37-39,45 / 38,46) 3 gap / 1 round, x2 (38,44 / 37,45 / 38,45 / 39,45)
	// 3 gap / 1 round; with run 67 (12 runs, 7 / 5): 20 unit runs, 13 through the gap, 7 round
	// (35 %). Here one trial per start block: 8 runs.
	const Vec2i block67[4] = {{38, 45}, {39, 45}, {38, 46}, {39, 46}};
	const Vec2i block68x2[4] = {{38, 44}, {37, 45}, {38, 45}, {39, 45}};
	const int round = QueueTrial(block67) + QueueTrial(block68x2);
	MESSAGE("M1 harness: " << 8 - round << " gap / " << round << " round of 8 (live 13 / 7 of 20)");
	// live 7 of 20 round: 2.8 of 8 expected; at least 1 and no more than the 4 rear runs
	CHECK(round >= 1);
	CHECK(round <= 4);
}

// Live run 67 M2b (1 case): a unit ordered later stops on the road one tile ahead of a walker
// (1-tile road). Live: the walker waits, then re-routes 14 frames (0.37 s) after the blocker stood:
// it turns back and goes round; no push, no give-up. Ours: the held step of a standing unit drops
// the way at the walker's next blocked call (at most CrossingUnitWait 15 cycles later) and the
// next cycle searches round it (C6: a standing own unit is a wall).
TEST_CASE("road 67: a unit stops on the road ahead - the walker re-routes soon after it stands")
{
	CAnimations slow; // outlives the units of the world
	CorridorWorld w;
	auto move = std::make_unique<CAnimation_Move>();
	move->Init("2", nullptr);
	auto wait = std::make_unique<CAnimation_Wait>();
	wait->Init("1", nullptr);
	slow.Move.push_back(std::move(move));
	slow.Move.push_back(std::move(wait));
	auto still = std::make_unique<CAnimation_Wait>();
	still->Init("1", nullptr);
	slow.Still.push_back(std::move(still));
	w.footman.Animations = &slow;
	for (short x : {24, 26, 28, 30}) {
		w.Place(w.farm, {x, 40});
		w.Place(w.farm, {x, 43});
	}
	const Vec2i walkerGoal(36, 42);
	CUnit &walker = w.Place(w.footman, {20, 42});
	CUnit &blocker = w.Place(w.footman, {34, 42});
	walker.Orders.clear();
	walker.Orders.push_back(COrder::NewActionMove(walkerGoal));
	walker.pathFinderData->output.Length = 0;
	int stood = -1;
	int turned = -1;
	Vec2i last = walker.tilePos;
	Vec2i heldAt(-1, -1);
	int cycle = 0;
	for (; cycle != 3000 && walker.CurrentAction() != UnitAction::Still; ++cycle) {
		if (cycle == 100) {
			blocker.Orders.clear();
			blocker.Orders.push_back(COrder::NewActionMove({30, 42}));
			blocker.pathFinderData->output.Length = 0;
		}
		w.Cycle(walker);
		--GameCycle;
		w.Cycle(blocker);
		if (cycle > 100 && stood < 0 && blocker.CurrentAction() == UnitAction::Still) {
			stood = cycle;
			heldAt = walker.tilePos;
		}
		if (walker.tilePos != last) {
			if (stood >= 0 && turned < 0 && walker.tilePos.x < last.x) {
				turned = cycle;
			}
			last = walker.tilePos;
		}
	}
	MESSAGE("blocker stood at cycle " << stood << " on " << blocker.tilePos.x << "," << blocker.tilePos.y
	        << ", walker on " << heldAt.x << "," << heldAt.y << " turned back at cycle " << turned << " ("
	        << turned - stood << " cycles after; live 14 frames), walker ends " << walker.tilePos.x << ","
	        << walker.tilePos.y);
	REQUIRE(stood >= 0);
	REQUIRE(std::max(std::abs(heldAt.x - blocker.tilePos.x), std::abs(heldAt.y - blocker.tilePos.y)) == 1); // held one tile before the blocker
	CHECK(turned > stood);
	CHECK(turned - stood <= 17);  // live 14 frames: within one CrossingUnitWait poll (+1, +1 to step)
	CHECK(walker.CurrentAction() == UnitAction::Still);
	CHECK(walker.tilePos == walkerGoal); // goes round, no give-up
}

// Round 3.4.80b (ROUND_3480b_EMU.md item 1, work/GROUP_STOP_SHORT_3480.md): groups ordered across a
// map with crossing traffic of other own units, and a group sent to a forest tile. Live in the
// emulator 3 of 10 Archers stopped for good 16 tiles short of a reachable goal while 20 Footmen
// came the other way through the same gaps; with a goal one tile into a forest 9 of 20 Footmen
// stopped 25-27 tiles short. A unit must arrive (a reachable goal: within the crowd round it) or
// end at the nearest reachable tiles (the forest goal).
namespace
{
struct GroupEnd {
	Vec2i end;
	int maxRange = 0;
	bool still = false;
};

/// Run the units given (each with its order already set) until all stand, at most `cycles`.
std::vector<GroupEnd> RunGroups(CorridorWorld &w, const std::vector<CUnit *> &units, int cycles)
{
	std::vector<GroupEnd> ends(units.size());
	for (int cycle = 0; cycle != cycles; ++cycle) {
		bool all = true;
		for (size_t i = 0; i != units.size(); ++i) {
			CUnit &u = *units[i];
			if (u.CurrentAction() == UnitAction::Move) {
				ends[i].maxRange = std::max(ends[i].maxRange,
				                            static_cast<const COrder_Move *>(u.CurrentOrder())->GetRange());
			}
			if (u.CurrentAction() == UnitAction::Still) {
				continue;
			}
			all = false;
			w.Cycle(u);
			--GameCycle;
		}
		++GameCycle;
		if (all) {
			break;
		}
	}
	for (size_t i = 0; i != units.size(); ++i) {
		ends[i].end = units[i]->tilePos;
		ends[i].still = units[i]->CurrentAction() == UnitAction::Still;
	}
	return ends;
}

void SlowFootman(CorridorWorld &w, CAnimations &slow)
{
	// 16 cycles a step, as a Footman live (16 frames)
	auto move = std::make_unique<CAnimation_Move>();
	move->Init("2", nullptr);
	auto wait = std::make_unique<CAnimation_Wait>();
	wait->Init("1", nullptr);
	slow.Move.push_back(std::move(move));
	slow.Move.push_back(std::move(wait));
	auto still = std::make_unique<CAnimation_Wait>();
	still->Init("1", nullptr);
	slow.Still.push_back(std::move(still));
	w.footman.Animations = &slow;
}

void OrderAll(const std::vector<CUnit *> &units, const Vec2i &goal)
{
	for (CUnit *u : units) {
		u->Orders.clear();
		u->Orders.push_back(COrder::NewActionMove(goal));
		u->pathFinderData->output.Length = 0;
	}
}

int Cheb(const Vec2i &a, const Vec2i &b)
{
	return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}
} // namespace

/// Units of a group sent across with a head-on group: the worst end distance from the goal of
/// each group, the largest Range of each.
struct CrossResult {
	int worstFirst = 0;
	int worstSecond = 0;
	int rangeFirst = 0;
	int rangeSecond = 0;
	bool allStill = true;
	std::string reasons; /// Range++ by AStarFailReason, the jam waits, the budget outs
};

/// The Range++ of the Move orders since the last call, by reason, and the jam waits.
std::string TakeRangeReasons()
{
	static const char *names[] = {"none", "simple", "region", "goal-full", "goal-units-on-way", "flood-units-on-way",
	                              "flood-standing-units", "flood-terrain", "open-set", "no-search"};
	std::string text = "Range++:";
	for (int i = 0; i <= AStarFailCount; ++i) {
		if (MoveRangeReasons[i] != 0) {
			text += std::string(" ") + names[i] + " " + std::to_string(MoveRangeReasons[i]);
		}
		MoveRangeReasons[i] = 0;
	}
	text += "; jam waits " + std::to_string(MoveJamWaits) + "; budget outs " + std::to_string(AStarBudgetOuts);
	MoveJamWaits = 0;
	AStarBudgetOuts = 0;
	return text;
}

/// A farm line on rows 60-61 from x 20 to 69 with 1-tile gaps at `gaps`; forest beside it to the
/// map edges, so the gaps are the only ways (the farm line of the round). `first` units go north
/// from the block x0-x0+9 rows 72-73 (20, the Footmen); `second` (10, the Archers) go south from
/// the block x1-x1+4 rows 44-45, ordered a cycle later.
CrossResult CrossTrial(const std::vector<short> &gaps, short x0, short x1, const Vec2i &firstGoal,
                       const Vec2i &secondGoal)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	for (int y = 60; y <= 61; ++y) {
		for (int x = 0; x < 128; ++x) {
			if (x < 20 || x > 69) {
				Map.Field(x, y)->Flags = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
			}
		}
	}
	const auto gap = [&](int x) { return std::find(gaps.begin(), gaps.end(), x) != gaps.end(); };
	for (short x = 20; x <= 69;) {
		if (gap(x)) {
			++x;
			continue;
		}
		if (x == 69 || gap(x + 1)) { // no room for a Farm: one forest column
			Map.Field(x, 60)->Flags = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
			Map.Field(x, 61)->Flags = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
			++x;
			continue;
		}
		w.Place(w.farm, {x, 60});
		x += 2;
	}
	for (int x = 20; x <= 69; ++x) {
		REQUIRE(gap(x) == !Map.Field(x, 60)->CheckMask(MapFieldBuilding | MapFieldUnpassable));
	}
	std::vector<CUnit *> first;
	std::vector<CUnit *> second;
	for (short y = 72; y <= 73; ++y) {
		for (short x = x0; x <= x0 + 9; ++x) {
			first.push_back(&w.Place(w.footman, {x, y}));
		}
	}
	for (short y = 44; y <= 45; ++y) {
		for (short x = x1; x <= x1 + 4; ++x) {
			second.push_back(&w.Place(w.footman, {x, y}));
		}
	}
	OrderAll(first, firstGoal);
	++GameCycle; // the second order a cycle later
	OrderAll(second, secondGoal);
	std::vector<CUnit *> all = first;
	all.insert(all.end(), second.begin(), second.end());
	TakeRangeReasons();
	const std::vector<GroupEnd> ends = RunGroups(w, all, 8000);
	CrossResult r;
	for (size_t i = 0; i != all.size(); ++i) {
		const bool isSecond = i >= first.size();
		const int d = Cheb(ends[i].end, isSecond ? secondGoal : firstGoal);
		if (isSecond) {
			r.worstSecond = std::max(r.worstSecond, d);
			r.rangeSecond = std::max(r.rangeSecond, ends[i].maxRange);
		} else {
			r.worstFirst = std::max(r.worstFirst, d);
			r.rangeFirst = std::max(r.rangeFirst, ends[i].maxRange);
		}
		r.allStill = r.allStill && ends[i].still;
		if (d > 3) {
			MESSAGE(std::string(isSecond ? "second " : "first ") << i << " ends " << ends[i].end.x << ","
			        << ends[i].end.y << " (" << d << " from the goal), largest Range " << ends[i].maxRange);
		}
	}
	r.reasons = TakeRangeReasons();
	return r;
}

TEST_CASE("group 3480: 10 units cross 20 others through a farm line with 1-tile gaps - all arrive")
{
	struct Trial {
		std::vector<short> gaps;
		short x0, x1;
		int kind; /// 0: all arrive; 1: head-on jam in 1-tile gaps that does not clear (reported); 2: no gap
	};
	const Trial trials[] = {
		{{30, 39, 48, 57}, 32, 35, 0},
		{{39}, 32, 35, 1},
		{{39, 66}, 34, 37, 0},
		{{37, 40}, 33, 36, 0},
		{{39, 42}, 36, 38, 0}, // was 17 / 14 short (head-on); arrives since the 3.4.81 step-aside rule
		{{66, 69}, 34, 37, 0}, // the same
		{{}, 32, 35, 2},
	};
	int t = 0;
	for (const Trial &trial : trials) {
		// the goals: the second group ends where the first started and back
		const CrossResult r = CrossTrial(trial.gaps, trial.x0, trial.x1, {short(trial.x1 + 2), 45},
		                                 {short(trial.x0 + 5), 73});
		MESSAGE("trial " << t << ": 20 units worst " << r.worstFirst << " from the goal (Range up to "
		        << r.rangeFirst << "), 10 units worst " << r.worstSecond << " (Range up to " << r.rangeSecond << "); " << r.reasons);
		CHECK(r.allStill); // a jam that does not clear still ends (MaxUnitBlockedWaits)
		if (trial.kind == 0) {
			// 10 / 20 units round one tile (without the jam wait trial 3 ends 17 / 14 short, Range++
			// "flood-units-on-way" 305 times). The bound was 3 (the 5 x 5 square holds 25: our own
			// arithmetic, not a live case); re-pinned to 4 after live run 69 H: a unit whose way near
			// its goal is held by standing units drops its order at d3-d4 (H2 d4, H5 d3; 5 of 32
			// orders lost, 0 deadlocks) - MoveStopsAtStandingCrowd (on) ends the rear of the 20 so
			CHECK(r.worstFirst <= 4);
			CHECK(r.worstSecond <= 4);
		}
		++t;
	}
}

TEST_CASE("group 3480: 20 units sent to a forest tile end at the nearest land, not far short")
{
	for (const int neckWidth : {3, 1}) {
		CAnimations slow;
		CorridorWorld w;
		SlowFootman(w, slow);
		// Forest on rows 70-127 but for a neck from x 38 on rows 70-85 into a land pocket x 30-49 on
		// rows 86-99 (the neck at the farms / the land by the forest of the round). The goal is the
		// first forest tile east of the pocket in row 95, land at 49,95 beside it.
		for (int y = 70; y < 128; ++y) {
			for (int x = 0; x < 128; ++x) {
				const bool neck = y <= 85 && x >= 38 && x < 38 + neckWidth;
				const bool pocket = y >= 86 && y <= 99 && x >= 30 && x <= 49;
				if (!neck && !pocket) {
					Map.Field(x, y)->Flags = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
				}
			}
		}
		const Vec2i goal(50, 95);
		std::vector<CUnit *> footmen;
		for (short y = 55; y <= 56; ++y) {
			for (short x = 30; x <= 39; ++x) {
				footmen.push_back(&w.Place(w.footman, {x, y}));
			}
		}
		TakeRangeReasons();
		OrderAll(footmen, goal);
		const std::vector<GroupEnd> ends = RunGroups(w, footmen, 12000);
		const std::string reasons = TakeRangeReasons();
		int worst = 0;
		for (size_t i = 0; i != footmen.size(); ++i) {
			const int d = Cheb(ends[i].end, goal);
			worst = std::max(worst, d);
			if (d > 4) {
				MESSAGE("neck " << neckWidth << ": footman " << i << " ends " << ends[i].end.x << "," << ends[i].end.y
				        << " (" << d << " from the goal), largest Range " << ends[i].maxRange
				        << std::string(ends[i].still ? "" : " STILL MOVING"));
			}
			CHECK(ends[i].still);
			// Was d <= 4 (the pocket's land within 4 holds 36 tiles: our arithmetic, not a live case).
			// Re-pinned to the live forest-goal groups: run 70 G (2 trials, 18 unit runs) stop 4-8 from
			// the clicked tile, run 69 F5 3-7; the round 3.4.80b fault (25-27 short) stays caught
			CHECK(d <= 8);
		}
		MESSAGE("neck " << neckWidth << ": worst " << worst << " tiles from the goal; " << reasons);
	}
}

TEST_CASE("group 3480 report: crossing trials with / without the jam wait" * doctest::skip())
{
	const std::vector<short> gaps[] = {{30, 39, 48, 57}, {39}, {39, 66}, {37, 40}, {39, 42}, {66, 69}};
	const short x0s[] = {32, 32, 34, 33, 36, 34};
	const short x1s[] = {35, 35, 37, 36, 38, 37};
	for (int variant = 0; variant != 2; ++variant) {
		MoveWaitsOutUnitJam = variant == 0;
		for (int t = 0; t != 6; ++t) {
			const CrossResult r = CrossTrial(gaps[t], x0s[t], x1s[t], {short(x1s[t] + 2), 45}, {short(x0s[t] + 5), 73});
			MESSAGE("jam wait " << std::string(variant ? "off" : "on")
			        << ", trial " << t << ": 20 worst " << r.worstFirst << " (Range " << r.rangeFirst << "), 10 worst "
			        << r.worstSecond << " (Range " << r.rangeSecond << ")" << std::string(r.allStill ? "" : " NOT ALL STILL") << "; " << r.reasons);
		}
	}
	MoveWaitsOutUnitJam = true;
}

// ---- Round 3.4.81 (ROUND_3481_EMU.md items 1b, 1d, 2; work/FOREST_HEADON_3481.md) ------------
// The live terrain of the round (sh3480b/terrain_grid.txt, x 0-63, y 40-114: T forest, ~ water,
// comma coast - not walkable, dot land), the Barracks of the round 5-7,94-96, the Farms {4,79}
// {7,79} {10,79}. Outside the dump: forest.
namespace
{
const char *const Live3481Rows[] = {
	"TTTTTTTTTTT......,,~~~~~~~~~~~~~~~~~~~~~~~~~,,.TTTTTTTTTTTTTTTTT", // 40
	"TTTTTTTTTTT......,~~~~~~~~~~~~~~~~~~~~~~~~~~,..TTTTTTTTTTTTTT...", // 41
	"TTTTTTTTTT.......,~~~~~~~~~~~~~~~~~~~~~~~~~~,..TTTTTT.TTTTTT....", // 42
	"TTTTTTTTTT.......,~~~~~~~~~~~~~~~~~~~~~~~~~~,..TTTTTTT.TTTT..,,,", // 43
	"TTTTTTTTT........,,,~~~~~~~~~~~~~~~~~~~~~~~~,,...TTTTT.......,~~", // 44
	"...TTTTTT..........,~~~~~~~~~~~~~~~~~~~~~~~~~,....TTT...,,,,,,~~", // 45
	",,...TTTT..........,,~~~~~~~~~~~~~~~~~~~~~~~~,,,......,,,~~~~~~~", // 46
	"~,,,..TTTTT.........,~~~~~~~~~~~~~~~~~~~~~~~~~~,,,,,,,,~~~~~~~~~", // 47
	"~~~,,..TTTTTT.......,,,~~~~~~~~~~,,,,,~~~~~~~~~~~~~~~~~~~~~~~~~~", // 48
	"~~~~,,..TTTTTTT.......,,,,~~~~~,,,...,,~~~~~~~~~~~~~~~~~~~~~~~~~", // 49
	"~~~~~,,...TTTTT..........,,,,,,,......,~~~~~~~~~~~~~~~~~~~~~~~~,", // 50
	"~~~~~~,,,..TTTTTT.....................,,~~~~~~~~~~~~~~~~~~~~~~,,", // 51
	"~~~~~~~~,,..TTTTT......................,~~~~~~~~~~~~~~~~~~~~~~,.", // 52
	"~~~~~~~~~,,..TTTT......................,~~~~~~~~~~~~~~~~~~,,,,,.", // 53
	"~~~~~~~~~~,,..TTTT.....................,~~~~~~~~~~~~~~~~,,,.....", // 54
	"~~~~~~~~~~~,,..TTT....................,,~~~~~~~~~~~~,,,,,...TTTT", // 55
	",,,~~~~~~~~~,,..TTTTT...............,,,~~~~~~~~~~~,,,.....TTTTTT", // 56
	"..,,,~~~~~~~~,,.TTTTTT.....TTTT.....,~~~~~~~~~~~~,,...TTTTTTTTTT", // 57
	"T...,~~~~~~~~~,..TTTTT.TTTTTTTT...,,,~~~~~~~~~~~,,..TTTTTTTTTTT.", // 58
	"TTT.,,~~~~~~~~,,..TTTT.TTTTTTT...,,~~~~~~~~~~~,,,..TTTTTTTTTT...", // 59
	"TTT..,,~~~~~~~~,,..TTTT.TTTTT....,~~~~~~~~~~~,,...TTTTTTTTTT....", // 60
	"TTTT..,,~~~~~~~~,...TTTTTTT.....,,~~~~~~~~~~,,...TTTTTTT........", // 61
	"TTTTT..,,~~~~~~~,...TTTTTTT...,,,~~~~~~~~~,,,..TTTTTT...........", // 62
	"TTTTTT..,~~~~~~~,,...TTTTT..,,,~~~~~~~~~~~,....TTTTT............", // 63
	"TTTTTT..,~~~~~~~~,,...TTT..,,~~~~~~~~~~~,,,....TTTTT............", // 64
	"TTTTTTT.,~~~~~~~~~,,,.....,,~~~~~~~~~~~~,.......TT..............", // 65
	"TTTTTTT.,,,~~~~~~~~~,,,.,,,~~~~~~~~~~~,,,.......................", // 66
	"TTT.......,,,~~~~~~~~~,,,~~~~~~~~~~~~~,......................TTT", // 67
	"TTTT........,,~~~~~~~~~~~~~~~~~~~~~,,,,..............TT...TTTTTT", // 68
	"TTTT.........,~~~~~~~~~~~~~~~~~~~,,,................TTT..TTTTTTT", // 69
	"TTT..........,,,~~~~~~~~~~~~~~~~~,..................TTTTTTTTTTTT", // 70
	"...............,,~~~~~~~~~~~~~~,,,....TTT..........TTTTTTTTTTTTT", // 71
	"................,~~~~~~~~~~~~~~,......TTTTTTT....TTTTTTTTT.TTT..", // 72
	"................,,~~~~~~~~~~~~~,,......TTTTTT...TTTTTTTT........", // 73
	".................,,~~~~~~~~~~~~~,,,,....TTTTTTT.TTTTT....,,,,,,,", // 74
	"TT...............,,~~~~~~~~~~~~~~~~,,,,..TTTTTTTTTT.....,,~~~~~~", // 75
	"TT...............,~~~~~~~~~~~~~~~~~~~~,,...TTTTTTTT..,,,,~~~~~~~", // 76
	"TTT.............,,~~~~~~~~~~~~~~~~~~~~~,,,..TTTTTT...,~~~~~~~~~~", // 77
	"TTT.............,~~~~~~~~~~~~~~~~~~~~~~~~,,..TTTT..,,,~~~~~~~~~~", // 78
	"TTT............,,~~~~~~~~~~~~~~~~~~~~~~~~~,,..TTT.,,~~~~~~~~~~~~", // 79
	"TT............,,~~~~~~~~,,,~~~~~~~~~~~~~~~~,......,~~~~~~~~~~~~~", // 80
	"TTTT.........,,~~~~~~~~,,.,,,,~~~~~~~~~~~~~,,,...,,~~~~~~~~~~~~~", // 81
	"TTTTT.......,,~~~~~~~~~,.....,,,~~~~~~~~~~~~~,,,,,~~~~~~~~~~~~~,", // 82
	"TTTTT......,,~~~~~~~~~,,.......,,~~~~~~~~~~~~~~~~~~~~~~~~~~~~~,,", // 83
	"TTTTT....,,,~~~~~~~~,,,.........,~~~~~~~~~~~~~~~~~~~~~~~~~~~~~,.", // 84
	"TTTT....,,~~~~~~~~~~,...........,~~~~~~~~~~~~~~~~~~~~~~~~~~~~,,.", // 85
	"TTTT....,~~~~~~~~~~,,..........,,~~~~~~~~~~~~~~~~~~~~~~~~~~~,,..", // 86
	"TT.....,,~~~~~~~~~~,..........,,~~~~~~~~~~~~~~~~~~~~~~~~~~~~,..T", // 87
	"T......,~~~~~~~~~~,,..........,~~~~~~~~~~~~~~~~~~~~~~~~~~~,,,..T", // 88
	".......,~~~~~~~~~,,...TTT.....,,~~,,,,,~~~~~~~~~~~~~~~~~~~,....T", // 89
	".......,,~~~~~~~~,,...TTTTT....,,,,...,,~~~~~~~~~~~~~~~~~~,.....", // 90
	"........,,~~~~~~~~,...TTTTTT...........,,~~~~~~~~~~~~~~~~,,.....", // 91
	".........,~~~~~~~~,,....TTTT............,~~~~~~~~~~~~~~~~,......", // 92
	".........,,~~~~~~~~,,,,..TTTTT..........,,~~~~~~~~~~~~~~~,......", // 93
	"..........,,,~~~~~~~~~,...TTTT...........,,~~~~~~~~~~~~~,,..TTT.", // 94
	"............,,~~~~~~~~,,..TTTT............,~~~~~~~~~~~~~,...TTT.", // 95
	".............,~~~~~~~~~,...TTTTT..........,,,~~~~~~~~~~~,...TTTT", // 96
	".....TTT.....,~~~~~~~~~,...TTTTT............,~~~~~~~~~~~,,,..TTT", // 97
	"....TTTT.....,~~~~~~~~~,,..TTTTT............,~~~~~~~~~~~~~,,.TTT", // 98
	"....TTTT...,,,~~~~~~~~~~,,..TTTTTT..........,,,~~~~~~~~~~~~,...T", // 99
	".TTTTTT..,,,~~~~~~~~~~~~~,..TTTTTT............,~~~~~~~~~~~~,,..T", // 100
	"TTTTTT..,,~~~~~~~~~~~~~~~,,..TTTTT.TTT........,,~~~~~~~~~~~~,,..", // 101
	"TTTT...,,~~~~~~~~~~~~~~~~~,,..TTTTTTTTTT.......,,~~~~~~~~~~~~,,.", // 102
	"TTT..,,,~~~~~~~~~~~~~~~~~~~,...TTTTTTTTTTT......,~~~~~~~~~~~~~,.", // 103
	"T...,,~~~~~~~~~~~~~~~~~~~~~,,,.TTTTTTTTTTTT.....,,~~~~~~~~~~~~,,", // 104
	"..,,,~~~~~~~~~~,,,,~~~~~~~~~~,.TTTTTTTTTTTTT.....,,~~~~~~~~~~~~~", // 105
	",,,~~~~~~~~~~,,,..,,,,~~~~~~~,.TTTTTT..TTTTT......,,~~~~~~~~~~~~", // 106
	",~~~~~~~~~~~,,.......,,~~~~~~,.TTTTT.....TTT.......,,,~~~~~~~~~~", // 107
	"~~~~~~~~~~~,,.........,,~~~~~,..TTTT.................,,,~~~~~~~~", // 108
	"~~~~~~~~~,,,...........,~~~~~,,..TTTT.TTT..............,,,~~~~~~", // 109
	"~~~~~~~,,,.............,~~~~~~,..TTTTTTTTTT..............,,~~~~~", // 110
	"~~~~~~,,..............,,~~~~~~,,..TTTTTTTTT...............,~~~~~", // 111
	"~~,,,,,...............,~~~~~~~~,..TTTTTTT............TTTT.,,~~~~", // 112
	"~,,..................,,~~~~~~~,,..TTTTTT............TTTTT..,~~~~", // 113
	",,...TT..............,~~~~~~~~,..TTTTTT.............TTTTT..,~~~~", // 114
};

void LayLive3481()
{
	for (int y = 0; y < 128; ++y) {
		for (int x = 0; x < 128; ++x) {
			CMapField &f = *Map.Field(x, y);
			const tile_flags keep = f.Flags & (MapFieldBuilding | MapFieldLandUnit);
			char c = 'T';
			if (y >= 40 && y <= 114 && x < 64) {
				c = Live3481Rows[y - 40][x];
			}
			tile_flags t = MapFieldLandAllowed;
			if (c == 'T') {
				t = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
			} else if (c == '~') {
				t = MapFieldWaterAllowed;
			} else if (c == ',') {
				t = MapFieldCoastAllowed;
			}
			f.Flags = t | keep;
		}
	}
	const auto building = [](int x0, int y0, int size) {
		for (int y = y0; y < y0 + size; ++y) {
			for (int x = x0; x < x0 + size; ++x) {
				Map.Field(x, y)->Flags |= MapFieldBuilding;
			}
		}
	};
	building(5, 94, 3); // Barracks {5,95}
	building(4, 79, 2);
	building(7, 79, 2);
	building(10, 79, 2);
}

struct Live3481Unit {
	CUnit *unit = nullptr;
	Vec2i goal;            /// the ordered tile
	Vec2i pulled{-1, -1};  /// the goal after the pull
	int maxRange = 0;
	int maxBlockedWaits = 0;
};

/// Run until every unit stands (or `cycles`); returns the last cycle a unit changed its tile.
int RunLive3481(CorridorWorld &w, std::vector<Live3481Unit> &units, int cycles, int *stuckLongest = nullptr)
{
	std::vector<Vec2i> last(units.size());
	for (size_t i = 0; i != units.size(); ++i) {
		last[i] = units[i].unit->tilePos;
	}
	int lastMove = 0;
	int longest = 0;
	for (int cycle = 0; cycle != cycles; ++cycle) {
		bool all = true;
		for (size_t i = 0; i != units.size(); ++i) {
			Live3481Unit &lu = units[i];
			CUnit &u = *lu.unit;
			if (u.CurrentAction() == UnitAction::Move) {
				const auto &order = static_cast<const COrder_Move &>(*u.CurrentOrder());
				if (order.GetGoalPulled() && lu.pulled.x < 0) {
					lu.pulled = order.GetGoalPos();
				}
				lu.maxRange = std::max(lu.maxRange, order.GetRange());
				lu.maxBlockedWaits = std::max(lu.maxBlockedWaits, order.GetUnitBlockedWaits());
			}
			if (u.CurrentAction() == UnitAction::Still) {
				continue;
			}
			all = false;
			w.Cycle(u);
			--GameCycle;
			if (u.tilePos != last[i]) {
				last[i] = u.tilePos;
				lastMove = cycle;
			}
		}
		++GameCycle;
		if (all) {
			break;
		}
		longest = std::max(longest, cycle - lastMove);
	}
	if (stuckLongest) {
		*stuckLongest = longest;
	}
	return lastMove;
}

std::string Live3481State(const Live3481Unit &lu)
{
	const CUnit &u = *lu.unit;
	std::string s = std::to_string(u.tilePos.x) + "," + std::to_string(u.tilePos.y);
	if (u.CurrentAction() == UnitAction::Move) {
		const auto &order = static_cast<const COrder_Move &>(*u.CurrentOrder());
		const PathFinderOutput &o = u.pathFinderData->output;
		s += " MOVE goal " + std::to_string(order.GetGoalPos().x) + "," + std::to_string(order.GetGoalPos().y)
		     + " range " + std::to_string(order.GetRange()) + " jam " + std::to_string(order.GetUnitBlockedWaits())
		     + " len " + std::to_string(o.Length) + " fast " + std::to_string(o.Fast) + " wait " + std::to_string(u.Wait)
		     + " moving " + std::to_string(u.Moving);
	} else {
		s += " still";
	}
	return s;
}

/// Item 1b / 1d: a group sent to a forest tile. Returns {worst, best} end distance of that group.
std::pair<int, int> Forest3481(int scene, bool pull)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	LayLive3481();
	MoveGoalPull = pull;
	// the tiles of the round's samples 2 s after the order (sh3481/samples1.txt)
	const Vec2i footmen1b[20] = {{6, 64}, {7, 65}, {7, 64}, {7, 63}, {6, 63}, {6, 62}, {5, 62}, {7, 66}, {3, 67}, {5, 61},
	                             {4, 61}, {4, 67}, {5, 67}, {6, 67}, {7, 67}, {8, 67}, {9, 67}, {4, 60}, {3, 60}, {4, 68}};
	const Vec2i archers1b[10] = {{3, 59}, {3, 58}, {2, 58}, {1, 58}, {1, 57}, {0, 57}, {5, 68}, {6, 68}, {7, 68}, {8, 68}};
	const Vec2i footmen1d[20] = {{6, 69}, {6, 70}, {4, 67}, {6, 71}, {5, 70}, {7, 68}, {7, 67}, {4, 69}, {5, 71}, {7, 70},
	                             {4, 71}, {4, 68}, {7, 69}, {5, 69}, {3, 70}, {5, 67}, {6, 67}, {5, 68}, {6, 68}, {4, 70}};
	const Vec2i archers1d[10] = {{2, 92}, {2, 91}, {3, 92}, {4, 91}, {2, 90}, {4, 92}, {3, 91}, {4, 89}, {3, 90}, {4, 90}};
	std::vector<Live3481Unit> group;  // the group sent to the forest tile
	std::vector<Live3481Unit> others; // the other group (standing in 1b, crossing in 1d)
	const Vec2i forestGoal = scene == 0 ? Vec2i(6, 100) : Vec2i(4, 62);
	const Vec2i otherGoal(10, 97);
	REQUIRE(Map.Field(forestGoal)->CheckMask(MapFieldForest));
	for (const Vec2i &p : scene == 0 ? footmen1b : footmen1d) {
		Live3481Unit lu;
		lu.unit = &w.Place(w.footman, p);
		(scene == 0 ? group : others).push_back(lu);
	}
	for (const Vec2i &p : scene == 0 ? archers1b : archers1d) {
		Live3481Unit lu;
		lu.unit = &w.Place(w.footman, p);
		(scene == 0 ? others : group).push_back(lu);
	}
	for (Live3481Unit &lu : group) {
		lu.goal = forestGoal;
		lu.unit->Orders.clear();
		lu.unit->Orders.push_back(COrder::NewActionMove(forestGoal));
		lu.unit->pathFinderData->output.Length = 0;
	}
	std::vector<Live3481Unit> all = group;
	if (scene == 1) {
		for (Live3481Unit &lu : others) {
			lu.goal = otherGoal;
			lu.unit->Orders.clear();
			lu.unit->Orders.push_back(COrder::NewActionMove(otherGoal));
			lu.unit->pathFinderData->output.Length = 0;
		}
		all.insert(all.end(), others.begin(), others.end());
	}
	TakeRangeReasons();
	RunLive3481(w, all, 12000);
	int worst = 0;
	int best = 99;
	std::string text;
	for (size_t i = 0; i != group.size(); ++i) {
		const Live3481Unit &lu = all[i];
		const int d = Cheb(lu.unit->tilePos, forestGoal);
		worst = std::max(worst, d);
		best = std::min(best, d);
		text += " " + std::to_string(lu.unit->tilePos.x) + "," + std::to_string(lu.unit->tilePos.y) + "(d" + std::to_string(d)
		        + " pulled " + std::to_string(lu.pulled.x) + "," + std::to_string(lu.pulled.y) + " R" + std::to_string(lu.maxRange)
		        + std::string(lu.unit->CurrentAction() == UnitAction::Still ? "" : " MOVING") + ")";
	}
	MESSAGE("forest 3481 scene " << std::string(scene == 0 ? "1b {6,100}" : "1d {4,62}") << ", pull "
	        << std::string(pull ? "on" : "off") << ": end distance " << best << "-" << worst << "; " << TakeRangeReasons()
	        << "\n ends:" << text);
	MoveGoalPull = true;
	return {worst, best};
}

/// Item 2: 4 units north of the 1-tile gap 7,85 (walls 4-6,85) to {5,90}, 4 south of it to {6,81}.
struct HeadOn3481 {
	int lastMove = 0;
	int longestStuck = 0; /// the longest time no unit changed its tile while some still had orders
	int worstNorth = 0;   /// end distance of the units sent north
	int worstSouth = 0;
	bool allStill = true;
};

HeadOn3481 RunHeadOn3481(int northEarlier, int cycles = 4000)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	LayLive3481();
	for (int x = 4; x <= 6; ++x) {
		Map.Field(x, 85)->Flags |= MapFieldBuilding; // the walls 323-325
	}
	REQUIRE_FALSE(Map.Field(7, 85)->CheckMask(LandMask));
	REQUIRE(Map.Field(8, 85)->CheckMask(LandMask));
	// the tiles of the round's sample 2 s after the order (sh3481/samples2.txt 18:37:58)
	const Vec2i knights[4] = {{6, 82}, {6, 83}, {7, 82}, {6, 81}};
	const Vec2i rangers[4] = {{5, 89}, {5, 90}, {5, 88}, {4, 89}};
	const Vec2i southGoal(5, 90);
	const Vec2i northGoal(6, 81);
	std::vector<Live3481Unit> all;
	for (const Vec2i &p : knights) {
		Live3481Unit lu;
		lu.unit = &w.Place(w.footman, p);
		lu.goal = southGoal;
		all.push_back(lu);
	}
	for (const Vec2i &p : rangers) {
		Live3481Unit lu;
		lu.unit = &w.Place(w.footman, p);
		lu.goal = northGoal;
		all.push_back(lu);
	}
	const unsigned long cycle0 = GameCycle;
	for (int i = 0; i != 8; ++i) {
		// the south group (ordered second here) gets its order northEarlier cycles after the north one
		GameCycle = i < 4 ? cycle0 + std::max(0, -northEarlier) : cycle0 + std::max(0, northEarlier);
		CUnit &u = *all[i].unit;
		u.Orders.clear();
		u.Orders.push_back(COrder::NewActionMove(all[i].goal));
		u.pathFinderData->output.Length = 0;
	}
	TakeRangeReasons();
	HeadOn3481 r;
	r.lastMove = RunLive3481(w, all, cycles, &r.longestStuck);
	std::string text;
	for (int i = 0; i != 8; ++i) {
		const int d = Cheb(all[i].unit->tilePos, all[i].goal);
		int &worst = i < 4 ? r.worstSouth : r.worstNorth;
		worst = std::max(worst, d);
		r.allStill = r.allStill && all[i].unit->CurrentAction() == UnitAction::Still;
		text += std::string("\n ") + (i < 4 ? "N->S " : "S->N ") + Live3481State(all[i]) + " d" + std::to_string(d)
		        + " maxR " + std::to_string(all[i].maxRange) + " maxJam " + std::to_string(all[i].maxBlockedWaits);
	}
	MESSAGE("head-on 3481 (north group ordered " << northEarlier << " cycles earlier): last tile change at cycle "
	        << r.lastMove << ", longest stand of all " << r.longestStuck << " cycles; worst end N->S " << r.worstSouth
	        << ", S->N " << r.worstNorth << "; " << TakeRangeReasons() << text);
	return r;
}
} // namespace

TEST_CASE("live 3481 report: forest goal and head-on gap with the rules of the day on / off" * doctest::skip())
{
	for (int scene = 0; scene != 2; ++scene) {
		Forest3481(scene, true);
		Forest3481(scene, false);
	}
	// variant 0: today's tree before the fix; 1-3: one rule of the day off; 4: all three off (about
	// 3.4.75); 5: the fix (way through waiting mates + step aside); 6: the fix without the way through
	for (int variant = 0; variant != 7; ++variant) {
		PathKeepsHeldStep = variant != 1 && variant != 4;
		MoveWaitsOutUnitJam = variant != 2 && variant != 4;
		MoveGoesRoundMates = variant != 3 && variant != 4;
		MoveJamTakesWayThroughMates = variant == 5;
		MoveHeadOnStepsAside = variant >= 5;
		MESSAGE("variant " << variant << ": held step " << PathKeepsHeldStep << ", jam wait " << MoveWaitsOutUnitJam
		        << ", go round mates " << MoveGoesRoundMates << ", way through mates " << MoveJamTakesWayThroughMates
		        << ", step aside " << MoveHeadOnStepsAside);
		RunHeadOn3481(0);
		RunHeadOn3481(1);
		RunHeadOn3481(-1);
	}
	PathKeepsHeldStep = true;
	MoveWaitsOutUnitJam = true;
	MoveGoesRoundMates = true;
	MoveJamTakesWayThroughMates = true;
	MoveHeadOnStepsAside = true;
}

TEST_CASE("live 3481: a group sent to a forest tile ends round the goal pulled toward it (ORIG47 pull)")
{
	// Items 1b / 1d of the round reproduced: the pull (0x4C2630, 12 of 12 live goals of ORIG47)
	// moves {6,100} onto the Barracks 6,94 (5,94 / 7,94) and {4,62} to 4,66; the group fills the
	// ring round that tile. Emulator round 3.4.81 (our port, not the original): 1b 4-9 / 4-11, 1d 3-6.
	// The worst bounds 9 / 6 pinned that port run; one shared goal for 20 (the Lua OrderUnit) was never
	// measured live. Re-pinned to the live forest-goal groups (run 69 F5, run 70 G: nobody walks round
	// to the free far side, the rear stops 7-8 short): best >= 4 / 2 keeps the free land beyond the
	// Barracks / the neck (d 1-3) empty; worst 10 / 8 = the crowd stopping where it stands
	// (MoveStopsAtStandingCrowd, on) instead of the widening ring.
	for (int scene = 0; scene != 2; ++scene) {
		const auto [worst, best] = Forest3481(scene, true);
		CHECK(best >= (scene == 0 ? 4 : 2));
		CHECK(worst <= (scene == 0 ? 10 : 8));
	}
}

TEST_CASE("live 3481: two groups head-on in a 1-tile gap blocked by each other, no way round - no deadlock")
{
	// Item 2 of the round: 4 units each side, one 1-tile gap (7,85), walls 4-6,85, coast 8,85.
	// Round 3.4.81: a unit stood in the gap WITH its order, nobody moved for 89 s. The original was
	// then unmeasured and the round's step aside made everybody arrive. Live run 73 R (closed 1-tile
	// road, 2 against 2, 3 of 3 head-on trials): the original does not resolve it either - every
	// blocked order is dropped within 16-38 frames and the units stand idle, the road jammed until the
	// player orders again. Here: every order ends (all idle) and no stand with an order left lasts
	// 5 s (190 cycles); the units that meet do not reach the far side (not checked: which pair meets
	// first depends on the order of the orders).
	for (const int northEarlier : {0, 1, -1}) {
		const HeadOn3481 r = RunHeadOn3481(northEarlier);
		CHECK(r.allStill);
		CHECK(r.longestStuck <= 190);
	}
}

// ---- Live run 69 (work/agent-orig-69/ORIG69.md; Human II scene of runs 67 / 68) -------------------
// H: two groups of 4 Footmen head-on through a 1-tile gap (x 38, rows 41-42) in a wall of Farms and
// the Town Hall; the way round W (x 29, 16-25 steps) or E round the forest (38-40 steps). F: groups
// and single units sent onto forest tiles. The tiles here are read from the run's tracks and grids
// (f0grid.png, f1endgrid.png, f5endgrid.png): Town Hall 30-33,40-43; Farms 34,41 36,41 | gap 38 |
// 39,41 41,41 43,41, 39,43 and 37,38; forest as LayRun69. Each H unit gets the goal stored live.
namespace
{
void LayRun69()
{
	const auto forest = [](int x0, int x1, int y) {
		for (int x = x0; x <= x1; ++x) {
			Map.Field(x, y)->Flags = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
		}
	};
	const auto building = [](int x0, int y0, int size) {
		for (int y = y0; y < y0 + size; ++y) {
			for (int x = x0; x < x0 + size; ++x) {
				Map.Field(x, y)->Flags |= MapFieldBuilding;
			}
		}
	};
	forest(40, 42, 33);
	forest(37, 43, 34);
	forest(36, 43, 35);
	forest(36, 44, 36);
	forest(37, 38, 37);
	// 39,37 free (an enclosed pocket: forest round it, the Farm 37,38 west), 39,38 forest - from the
	// live pulls: F1 1569 36,39 kept 42,36 (its line crosses 39,38), F1 1570 35,39 -> 40,37 and F2
	// 1569 36,39 -> 40,36 (their lines stop at the free 39,37 of the region map)
	forest(40, 45, 37);
	forest(39, 47, 38);
	forest(39, 50, 39);
	forest(39, 50, 40);
	forest(45, 51, 41); // east of the Farm wall
	forest(45, 51, 42);
	forest(41, 51, 43);
	forest(41, 48, 44);
	forest(41, 46, 45);
	forest(41, 46, 46);
	building(30, 40, 4); // Town Hall
	for (const int x : {34, 36, 39, 41, 43}) {
		building(x, 41, 2);
	}
	building(39, 43, 2);
	building(37, 38, 2);
	ClearGoalRegionMap();
}

struct H69Unit {
	Vec2i start;
	Vec2i goal;
};

struct H69Trial {
	const char *name;
	H69Unit first[4];  /// ordered first (one group order)
	H69Unit second[4]; /// ordered `delay` cycles later
	int delay;
};

// The live starts and stored goals (log/h2, h4, h5, h6.tsv); delay = frames between the two orders.
const H69Trial h69Trials[] = {
	{"H2", {{{36, 38}, {38, 45}}, {{36, 39}, {38, 46}}, {{37, 40}, {39, 47}}, {{36, 40}, {38, 47}}},
	 {{{37, 45}, {35, 40}}, {{38, 45}, {36, 40}}, {{38, 44}, {36, 39}}, {{37, 44}, {35, 39}}}, 72},
	{"H4", {{{33, 38}, {38, 46}}, {{34, 38}, {39, 46}}, {{34, 39}, {39, 47}}, {{33, 39}, {37, 46}}},
	 {{{37, 45}, {34, 38}}, {{38, 45}, {35, 38}}, {{37, 44}, {34, 37}}, {{38, 44}, {35, 37}}}, 70},
	{"H5", {{{38, 44}, {35, 38}}, {{37, 44}, {34, 38}}, {{38, 45}, {35, 39}}, {{37, 45}, {34, 39}}},
	 {{{36, 40}, {38, 47}}, {{37, 40}, {39, 47}}, {{36, 39}, {38, 46}}, {{36, 38}, {38, 45}}}, 72},
	{"H6", {{{31, 38}, {38, 46}}, {{32, 38}, {39, 46}}, {{32, 39}, {39, 47}}, {{31, 39}, {36, 45}}},
	 {{{37, 45}, {34, 39}}, {{36, 44}, {33, 38}}, {{37, 44}, {34, 38}}, {{38, 44}, {35, 38}}}, 72},
};

struct H69Result {
	int gap = 0, west = 0, east = 0, noMove = 0, lost = 0, notStill = 0;
	int longestStand = 0;      /// the longest stand on a tile (beyond the 16 cycles of a step)
	int faceToFace = 0;        /// pairs of the two groups meeting face to face (one backs off)
	int laterYielded = 0;      /// of them: the one that backed off was the later to arrive
	int yieldInterval = 0;     /// the longest tile change to tile change of the one backing off (live 17)
	int otherInterval = 0;     /// the same of the other one, till it steps on (live 38)
	int doneFirst = 0, doneSecond = 0; /// cycles from each group's order to its last unit's end
};

H69Result RunH69(const H69Trial &t)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	LayRun69();
	REQUIRE_FALSE(Map.Field(38, 41)->CheckMask(LandMask));
	REQUIRE_FALSE(Map.Field(38, 42)->CheckMask(LandMask));
	std::vector<CUnit *> units;
	std::vector<Vec2i> goals;
	for (const H69Unit &u : t.first) {
		units.push_back(&w.Place(w.footman, u.start));
		goals.push_back(u.goal);
	}
	for (const H69Unit &u : t.second) {
		units.push_back(&w.Place(w.footman, u.start));
		goals.push_back(u.goal);
	}
	struct Track {
		std::vector<Vec2i> tiles;
		std::vector<int> arrived; /// cycle of arrival on each tile
		int ordered = -1, ended = -1;
		bool gap = false;
		char side = '-';
	};
	std::vector<Track> tracks(8);
	const auto order = [&](int from, int cycle) {
		for (int i = from; i != from + 4; ++i) {
			units[i]->Orders.clear();
			units[i]->Orders.push_back(COrder::NewActionMove(goals[i]));
			units[i]->pathFinderData->output.Length = 0;
			tracks[i].ordered = cycle;
		}
	};
	for (int i = 0; i != 8; ++i) {
		tracks[i].tiles.push_back(units[i]->tilePos);
		tracks[i].arrived.push_back(0);
	}
	H69Result r;
	int cycle = 0;
	for (; cycle != 4000; ++cycle) {
		if (cycle == 0) {
			order(0, 0);
		} else if (cycle == t.delay) {
			order(4, cycle);
		}
		bool all = cycle >= t.delay;
		for (int i = 0; i != 8; ++i) {
			CUnit &u = *units[i];
			Track &tr = tracks[i];
			if (u.CurrentAction() == UnitAction::Still) {
				if (tr.ordered >= 0 && tr.ended < 0) {
					tr.ended = cycle;
				}
				continue;
			}
			all = false;
			w.Cycle(u);
			--GameCycle;
			if (u.tilePos != tr.tiles.back()) {
				tr.tiles.push_back(u.tilePos);
				tr.arrived.push_back(cycle);
				if (u.tilePos.x == 38 && (u.tilePos.y == 41 || u.tilePos.y == 42)) {
					tr.gap = true;
				}
				if (u.tilePos.x <= 29) {
					tr.side = 'W';
				} else if (u.tilePos.x >= 48) {
					tr.side = 'E';
				}
			}
		}
		++GameCycle;
		if (all) {
			break;
		}
	}
	// Face to face (as live H6): unit y steps onto T heading at O, a unit o of the other group stands
	// on O (there before y's step ended) and its next tile is T; y leaves T elsewhere first. Tile
	// changes are counted at the start of a step, live and here (16 cycles a step).
	for (int y = 0; y != 8; ++y) {
		const Track &ty = tracks[y];
		for (size_t k = 1; k + 1 < ty.tiles.size(); ++k) {
			const Vec2i T = ty.tiles[k];
			const Vec2i O = T + (T - ty.tiles[k - 1]);
			for (int o = y < 4 ? 4 : 0, oEnd = o + 4; o != oEnd; ++o) {
				const Track &to = tracks[o];
				for (size_t j = 0; j + 1 < to.tiles.size(); ++j) {
					if (to.tiles[j] == O && to.tiles[j + 1] == T && ty.tiles[k + 1] != O
					    && to.arrived[j] + 16 <= ty.arrived[k + 1] && to.arrived[j + 1] >= ty.arrived[k + 1]
					    && to.arrived[j] <= ty.arrived[k] + 16) {
						++r.faceToFace;
						r.laterYielded += ty.arrived[k] >= to.arrived[j];
						r.yieldInterval = std::max(r.yieldInterval, ty.arrived[k + 1] - ty.arrived[k]);
						r.otherInterval = std::max(r.otherInterval, to.arrived[j + 1] - to.arrived[j]);
					}
				}
			}
		}
	}
	std::string text;
	for (int i = 0; i != 8; ++i) {
		const Track &tr = tracks[i];
		const CUnit &u = *units[i];
		int longest = 0;
		for (size_t k = 0; k + 1 < tr.arrived.size(); ++k) {
			const int from = k == 0 ? tr.ordered : tr.arrived[k] + 16;
			longest = std::max(longest, tr.arrived[k + 1] - from);
		}
		r.longestStand = std::max(r.longestStand, longest);
		const int d = Cheb(u.tilePos, goals[i]);
		const bool still = u.CurrentAction() == UnitAction::Still;
		r.notStill += !still;
		r.noMove += tr.tiles.size() == 1;
		r.lost += still && d >= 2 && tr.tiles.size() > 1;
		r.gap += tr.gap;
		r.west += !tr.gap && tr.side == 'W';
		r.east += !tr.gap && tr.side == 'E';
		const int done = (tr.ended < 0 ? cycle : tr.ended) - std::max(0, tr.ordered);
		int &groupDone = i < 4 ? r.doneFirst : r.doneSecond;
		groupDone = std::max(groupDone, done);
		text += std::string("\n ") + (i < 4 ? "1st " : "2nd ") + std::to_string(tr.tiles[0].x) + ","
		        + std::to_string(tr.tiles[0].y) + (tr.gap ? " GAP" : tr.side == 'W' ? " W" : tr.side == 'E' ? " E" : " -")
		        + " steps " + std::to_string(tr.tiles.size() - 1) + " stand " + std::to_string(longest) + " end d"
		        + std::to_string(d) + (still ? "" : " NOT STILL") + ": " + Text(tr.tiles);
	}
	MESSAGE("head-on 69 " << std::string(t.name) << ": gap " << r.gap << ", W " << r.west << ", E " << r.east << ", no move "
	        << r.noMove << ", lost " << r.lost << ", not still " << r.notStill << "; longest stand " << r.longestStand
	        << " cycles; face to face " << r.faceToFace << " (later arrival backed off " << r.laterYielded
	        << ", its tile interval " << r.yieldInterval << ", the other's " << r.otherInterval << "); done " << r.doneFirst << " / " << r.doneSecond
	        << " cycles after the orders" << text);
	return r;
}

struct F69Trial {
	const char *name;
	std::vector<Vec2i> starts;
	std::vector<Vec2i> liveGoals; /// the goals stored live (log/f*.tsv)
	Vec2i target;
};

// log/f1-f6.tsv: starts and stored goals in the order of the unit numbers.
const F69Trial f69Trials[] = {
	{"F1", {{34, 39}, {34, 40}, {35, 40}, {36, 39}, {35, 39}, {36, 38}, {34, 38}, {36, 40}, {35, 38}},
	 {{40, 36}, {37, 39}, {37, 39}, {42, 36}, {40, 37}, {40, 36}, {37, 37}, {38, 39}, {37, 37}}, {41, 36}},
	{"F2", {{34, 39}, {34, 40}, {35, 40}, {36, 39}, {34, 38}, {36, 38}, {35, 38}, {36, 40}, {36, 37}},
	 {{37, 37}, {37, 38}, {40, 36}, {40, 36}, {36, 36}, {42, 33}, {37, 36}, {42, 35}, {42, 32}}, {41, 33}},
	{"F3", {{36, 39}}, {{41, 34}}, {41, 34}}, // stored 41,34 = the click (the line to it is blocked all the way)
	{"F4", {{35, 40}}, {{37, 39}}, {42, 36}},
	{"F5", {{34, 33}, {35, 31}, {36, 33}, {36, 32}, {34, 32}, {35, 32}, {34, 31}, {36, 31}, {35, 33}},
	 {{37, 35}, {40, 34}, {38, 34}, {39, 34}, {37, 34}, {38, 34}, {39, 34}, {40, 33}, {37, 34}}, {42, 36}},
	{"F6", {{37, 32}}, {{40, 34}}, {42, 36}},
};

struct F69Result {
	int noMove = 0, best = 99, worst = 0, settle = 0, goalsAsLive = 0, longestWalk = 0;
	int maxEndX = 0, minEndY = 999;
	bool allStill = true;
};

/// A group (or one unit) sent to a forest tile: each unit gets its group goal (GroupMoveGoals, as a
/// right click), the pull and the no-way drop of the first Execute. F6 is sent from the F5 ends.
F69Result RunF69(const F69Trial &t, bool run70 = false)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	LayRun69();
	if (run70) {
		// run 70: a new game, the Farm wall of run 69 not built (the Farm 37,38 = 1575 of the map stays)
		for (const Vec2i &farm : {Vec2i(34, 41), Vec2i(36, 41), Vec2i(39, 41), Vec2i(41, 41), Vec2i(43, 41), Vec2i(39, 43)}) {
			for (int y = farm.y; y != farm.y + 2; ++y) {
				for (int x = farm.x; x != farm.x + 2; ++x) {
					Map.Field(x, y)->Flags &= ~MapFieldBuilding;
				}
			}
		}
		// 39,40 is land: G2 1566 ended on it, and its stored goal 40,39 (line from 43,38 toward 28,44)
		// stops there (LayRun69 has it forest; the run 69 tests keep their layout)
		Map.Field(39, 40)->Flags = MapFieldLandAllowed;
		ClearGoalRegionMap();
	}
	REQUIRE(Map.Field(t.target)->CheckMask(MapFieldForest));
	std::vector<CUnit *> units;
	if (std::string(t.name) == "F6") {
		// the F5 clump round 1573 (37,32): 35,34 39,33 37,33 38,33 36,33 38,32 39,32 36,34
		for (const Vec2i &p : {Vec2i(35, 34), Vec2i(39, 33), Vec2i(37, 33), Vec2i(38, 33), Vec2i(36, 33),
		                       Vec2i(38, 32), Vec2i(39, 32), Vec2i(36, 34)}) {
			w.Place(w.footman, p);
		}
	}
	for (const Vec2i &s : t.starts) {
		units.push_back(&w.Place(w.footman, s));
	}
	const std::vector<Vec2i> goals = GroupMoveGoals(t.starts, t.target, 128, 128);
	for (size_t i = 0; i != units.size(); ++i) {
		units[i]->Orders.clear();
		units[i]->Orders.push_back(COrder::NewActionMove(goals[i]));
		units[i]->pathFinderData->output.Length = 0;
	}
	std::vector<Vec2i> pulled(units.size(), Vec2i(-1, -1));
	std::vector<int> steps(units.size(), 0);
	std::vector<Vec2i> last(units.size());
	for (size_t i = 0; i != units.size(); ++i) {
		last[i] = units[i]->tilePos;
	}
	F69Result r;
	for (int cycle = 0; cycle != 3000; ++cycle) {
		bool all = true;
		for (size_t i = 0; i != units.size(); ++i) {
			CUnit &u = *units[i];
			if (u.CurrentAction() == UnitAction::Still) {
				continue;
			}
			all = false;
			w.Cycle(u);
			--GameCycle;
			if (u.CurrentAction() == UnitAction::Move && pulled[i].x < 0) {
				const auto &order = static_cast<const COrder_Move &>(*u.CurrentOrder());
				if (order.GetGoalPulled()) {
					pulled[i] = order.GetGoalPos();
				}
			}
			if (u.tilePos != last[i]) {
				last[i] = u.tilePos;
				++steps[i];
				r.settle = cycle + 16;
			}
		}
		++GameCycle;
		if (all) {
			break;
		}
	}
	std::string text;
	for (size_t i = 0; i != units.size(); ++i) {
		const CUnit &u = *units[i];
		const int d = Cheb(u.tilePos, t.target);
		r.best = std::min(r.best, d);
		r.worst = std::max(r.worst, d);
		r.noMove += steps[i] == 0;
		r.longestWalk = std::max(r.longestWalk, steps[i]);
		r.allStill = r.allStill && u.CurrentAction() == UnitAction::Still;
		r.maxEndX = std::max(r.maxEndX, int(u.tilePos.x));
		r.minEndY = std::min(r.minEndY, int(u.tilePos.y));
		r.goalsAsLive += pulled[i] == t.liveGoals[i];
		text += "\n " + std::to_string(t.starts[i].x) + "," + std::to_string(t.starts[i].y) + " goal "
		        + std::to_string(goals[i].x) + "," + std::to_string(goals[i].y) + " pulled "
		        + std::to_string(pulled[i].x) + "," + std::to_string(pulled[i].y) + " (live "
		        + std::to_string(t.liveGoals[i].x) + "," + std::to_string(t.liveGoals[i].y) + ") steps "
		        + std::to_string(steps[i]) + " end " + std::to_string(u.tilePos.x) + "," + std::to_string(u.tilePos.y)
		        + " d" + std::to_string(d);
	}
	MESSAGE("forest 69 " << std::string(t.name) << " -> " << t.target.x << "," << t.target.y << ": no move " << r.noMove << " of "
	        << units.size() << ", end d " << r.best << "-" << r.worst << ", settled " << r.settle
	        << " cycles, goals as live " << r.goalsAsLive << ", longest walk " << r.longestWalk << text);
	return r;
}
} // namespace

TEST_CASE("head-on 69 report: live run 69 H with the head-on rules of round 3.4.81 and of run 69" * doctest::skip())
{
	for (int variant = 0; variant != 2; ++variant) {
		MoveHeadOnLaterArrivalYields = variant == 1;
		MESSAGE("variant " << variant << std::string(variant == 1 ? ": later arrival backs out (run 69)" : ": round 3.4.81"));
		for (const H69Trial &t : h69Trials) {
			RunH69(t);
		}
		RunHeadOn3481(0);
		RunHeadOn3481(1);
		RunHeadOn3481(-1);
	}
	MoveHeadOnLaterArrivalYields = true;
	for (int variant = 0; variant != 2; ++variant) {
		PathStepNoWayByGoalNeighbours = variant == 1;
		MESSAGE("no way by " << std::string(variant == 1 ? "the goal's neighbours (run 69)" : "the line (ORIG48)"));
		for (const F69Trial &t : f69Trials) {
			RunF69(t);
		}
	}
	PathStepNoWayByGoalNeighbours = true;
}

TEST_CASE("head-on 69: two groups through a 1-tile gap - no deadlock, mostly round, short stands")
{
	// Live run 69 H (4 trials, 32 unit runs): 9 through the gap, 22 round (18 W, 4 E), 1 never moved,
	// 5 orders lost (3-9 tiles short), 0 deadlocks; longest stand 1.6 s (62 frames; 3 cases 1.0-1.6
	// s); one face-to-face pair (H6): the later to arrive stepped off at its next step (17 frames),
	// the other stood 22 frames more. Both groups done 10-19 s (373-724 frames) after their orders.
	int gap = 0, round = 0, lost = 0;
	for (const H69Trial &t : h69Trials) {
		const H69Result r = RunH69(t);
		CHECK(r.notStill == 0); // no deadlock: every order ends
		gap += r.gap;
		round += r.west + r.east;
		lost += r.lost + r.noMove;
		CHECK(r.longestStand <= 77); // 2 s (live 1.6 s)
		// live H6: the later order (also the later to arrive) backed off at its next step (17 frames).
		// Here H6 meets the other way round in time (the later order is in the gap first): it backs
		// off at its next poll once the other stands waiting (34 = 16 + 18).
		CHECK(r.yieldInterval <= 16 + 20);
		CHECK(r.otherInterval <= 16 + 48);     // the other stood about 1 s (live 22 frames, a step 38)
		CHECK(r.doneFirst <= 900);
		CHECK(r.doneSecond <= 900);
	}
	MESSAGE("head-on 69: " << gap << " gap / " << round << " round / " << lost << " lost or no move of 32 (live 9 / 22 / 6)");
	CHECK(round >= 12); // mostly round when the detour exists (live 22 of 32)
	CHECK(lost <= 8);   // orders may end short as live (6 of 32), not required to be 0
}

TEST_CASE("forest 69: a group sent onto forest stops at the near edge, units with no way do not move")
{
	// Live run 69 F5 (9 units on open land NW of the forest -> 42,36): all moved, goals pulled to the
	// near edge, front 3-4 / rear 5-7 from the clicked tile, nobody round to the far side, all idle
	// 3.5 s (135 frames) after the order. F1 / F2: 4 and 5 of 9 did not move; F3, F6: no move.
	for (const F69Trial &t : f69Trials) {
		const F69Result r = RunF69(t);
		CHECK(r.allStill);
		const std::string name = t.name;
		if (name == "F5") {
			CHECK(r.noMove == 0);       // live all 9 moved (the line rule of ORIG48 kept 2 here)
			CHECK(r.goalsAsLive == 9);  // the group goals and the pull as live
			CHECK(r.best == 3);         // live front 3-4, rear 5-7 (3,4,4,4,5,5,6,6,7)
			CHECK(r.worst == 7);
			CHECK(r.settle <= 135 + 16); // live all idle 3.5 s after the order
			CHECK(r.longestWalk <= 7);  // nobody goes round to the far side (live 1-7 steps)
		}
		if (name == "F1") {
			CHECK(r.noMove == 4); // live 4 (goals 42,36 / 40,36 x2 / 40,37: no reachable free neighbour)
		}
		if (name == "F2") {
			// live 5; here 1567 (34,40) too: all mates round its goal (the Farm 37,38) still stand at
			// its order - live it was ordered last, 4 frames after the first, when they had left
			CHECK(r.noMove >= 5);
			CHECK(r.noMove <= 6);
		}
		if (name == "F3" || name == "F6") {
			CHECK(r.noMove == 1);
		}
	}
}

TEST_CASE("forest 70: a spread group sent onto forest walks straight to the near edge, nobody round")
{
	// Live run 70 G (work/agent-orig-70/ORIG70.md, log/g1.tsv, g2.tsv): the run 69 scene in a new game
	// (no Farm wall), 9 units spread x 23-29, y 35-44 (7 x 10 tiles) -> a deep forest tile. 2 trials,
	// 18 unit runs: all moved 6-16 steps straight east, 0 walked round, 0 lost; ends against the west
	// edge / the Farm, d 5-8 (G1) and 4-8 (G2); all idle 6.0 s / 6.8 s after the order (230 / 261
	// frames). Starts and stored goals in the order 1566-1569, 1570, 1571, 1573, 1574, 1576.
	// Ours: the pull gives the live stored goals; the units 11-17 off goals beside the closed pocket
	// (40,36 / 40,37 / 40,38) are not dropped (NoWayDropReach) and get PullGoalIntoReach.
	const F69Trial g70Trials[] = {
		{"G1", {{28, 44}, {24, 44}, {27, 39}, {29, 40}, {29, 41}, {25, 40}, {29, 36}, {23, 39}, {26, 35}},
		 {{37, 39}, {40, 37}, {40, 36}, {40, 37}, {40, 37}, {40, 37}, {36, 36}, {40, 36}, {36, 36}}, {42, 36}},
		{"G2", {{28, 44}, {24, 44}, {27, 39}, {29, 40}, {28, 42}, {25, 39}, {29, 35}, {23, 38}, {26, 36}},
		 {{40, 39}, {39, 39}, {37, 38}, {37, 39}, {38, 39}, {37, 38}, {40, 37}, {37, 38}, {40, 38}}, {43, 38}},
	};
	for (const F69Trial &t : g70Trials) {
		const F69Result r = RunF69(t, true);
		CHECK(r.allStill);
		CHECK(r.noMove == 0);       // live all 18 moved
		CHECK(r.best >= 4);         // live stops 4-8 from the clicked tile
		// Open: G2 has one unit at d9, one tile beyond the live worst (1 trial each) - kept visible.
		WARN(r.worst <= 8);
		CHECK(r.worst <= 9);
		CHECK(r.maxEndX <= 40);     // nobody round to the east / north of the forest (live 0 of 18)
		CHECK(r.minEndY >= 33);
		CHECK(r.longestWalk <= 16); // live 6-16 steps, 0 back-steps
		CHECK(r.settle <= 270);     // live all idle 6.0 / 6.8 s (~7 s = 269 frames)
	}
}

// ---------------------------------------------------------------------------------------------
// Round 3.4.82 (work/c1127/ROUND_3482_EMU.md, sh3482/samples1.txt): the forest goal on the
// emulator. The round gave its orders by the Lua `OrderUnit(0, type, {0,0,127,127}, tile, "move")`
// (script_unit.cpp CclOrderUnit -> CommandMove(*unit, tile) for each unit): every unit gets the
// SAME goal - no group spread. A player's tap / hold / right click goes DoRightButton (mouse.cpp)
// -> GroupMoveGoalsForUnits: each unit its own goal, as the original (run 69 F). Both entries
// on the round's tiles: the shared goal reproduces the round, the group order is what a player gets.
namespace
{
struct F3482Scene {
	const char *name;
	std::vector<Vec2i> group;  /// the units ordered, at the order (the sample of the order's call)
	std::vector<Vec2i> others; /// units standing
	Vec2i target;
};

const F3482Scene f3482Scenes[] = {
	{"2a run 1 Footmen {6,64} -> {6,100}",
	 {{6, 64}, {7, 64}, {7, 63}, {7, 65}, {6, 63}, {6, 62}, {5, 62}, {7, 66}, {5, 61}, {3, 67},
	  {4, 61}, {4, 67}, {5, 67}, {6, 67}, {7, 67}, {8, 67}, {9, 67}, {4, 68}, {4, 60}, {5, 68}},
	 {{3, 60}, {3, 59}, {3, 58}, {2, 58}, {1, 58}, {1, 57}, {0, 57}, {6, 68}, {7, 68}, {8, 68}},
	 {6, 100}},
	{"2a run 2 Footmen {5,68} -> {6,100}",
	 {{5, 70}, {3, 70}, {6, 70}, {7, 71}, {8, 67}, {6, 71}, {5, 71}, {7, 69}, {6, 69}, {4, 70},
	  {4, 69}, {8, 69}, {4, 71}, {6, 67}, {7, 70}, {6, 68}, {7, 66}, {7, 68}, {7, 67}, {5, 69}},
	 {{4, 90}, {3, 90}, {3, 91}, {4, 89}, {3, 92}, {4, 91}, {4, 92}, {2, 92}, {2, 91}, {2, 90}},
	 {6, 100}},
	{"2b run 2 Archers {3,91} -> {4,62}",
	 {{4, 90}, {3, 90}, {3, 91}, {4, 89}, {3, 92}, {4, 91}, {4, 92}, {2, 92}, {2, 91}, {2, 90}},
	 {{4, 93}, {5, 91}, {7, 91}, {6, 93}, {4, 97}, {5, 92}, {3, 93}, {3, 95}, {4, 95}, {7, 93},
	  {6, 92}, {3, 96}, {5, 93}, {4, 96}, {8, 92}, {7, 92}, {3, 94}, {6, 91}, {6, 90}, {4, 94}},
	 {4, 62}},
	{"near: 9 Footmen 3 x 3 on open land 8-10,95-97 -> {6,100}",
	 {{8, 95}, {9, 95}, {10, 95}, {8, 96}, {9, 96}, {10, 96}, {8, 97}, {9, 97}, {10, 97}},
	 {},
	 {6, 100}},
	{"near: 9 Footmen 3 x 3 on open land 5-7,67-69 -> {4,62}",
	 {{5, 67}, {6, 67}, {7, 67}, {5, 68}, {6, 68}, {7, 68}, {5, 69}, {6, 69}, {7, 69}},
	 {},
	 {4, 62}},
	{"12 of the 2a run 1 Footmen (the selection limit) -> {6,100}",
	 {{6, 64}, {7, 64}, {7, 63}, {7, 65}, {6, 63}, {6, 62}, {5, 62}, {7, 66}, {5, 61}, {3, 67}, {4, 61}, {4, 67}},
	 {{5, 67}, {6, 67}, {7, 67}, {8, 67}, {9, 67}, {4, 68}, {4, 60}, {5, 68}},
	 {6, 100}},
	// Round 3.4.83 item 1a (c1127/ROUND_3483_EMU.md, sh3483/samples1.txt): Footmen 88-96 in the
	// order of their ids, bridge `move 6 100` on the selection (GroupMoveGoalsForUnits).
	{"3483 1a: 9 Footmen tight 3 x 3 at 9-11,76-78 -> {6,100}",
	 {{9, 76}, {10, 76}, {11, 76}, {9, 77}, {10, 77}, {11, 77}, {9, 78}, {10, 78}, {11, 78}},
	 {},
	 {6, 100}},
};

struct F3482Result {
	int best = 99, worst = 0, noMove = 0;
	int lastStill = 0;  /// order -> the last unit of the group still (cycles)
	int firstStill = -1; /// order -> the first unit still
	int lastStep = 0;   /// order -> the last tile change of the group
	int longestWalk = 0;
	bool allStill = true;
	int distinctGoals = 0;
	std::vector<Vec2i> goals; /// the group goal of each unit (before the pull)
	std::vector<Vec2i> ends;  /// where each unit stands at the end
	std::vector<int> steps;   /// tile changes of each unit
};

/// `group`: true = the player's group order (GroupMoveGoalsForUnits, as DoRightButton / SendMove /
/// the bridge `move` verb); false = the Lua OrderUnit of the round (the target for every unit).
F3482Result RunF3482(const F3482Scene &s, bool group)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	LayLive3481();
	REQUIRE(Map.Field(s.target)->CheckMask(MapFieldForest));
	for (const Vec2i &p : s.others) {
		w.Place(w.footman, p);
	}
	std::vector<CUnit *> units;
	for (const Vec2i &p : s.group) {
		units.push_back(&w.Place(w.footman, p));
	}
	const std::vector<Vec2i> goals = group ? GroupMoveGoalsForUnits(units, s.target)
	                                       : std::vector<Vec2i>(units.size(), s.target);
	for (size_t i = 0; i != units.size(); ++i) {
		units[i]->Orders.clear();
		units[i]->Orders.push_back(COrder::NewActionMove(goals[i]));
		units[i]->pathFinderData->output.Length = 0;
	}
	F3482Result r;
	{
		std::vector<Vec2i> g = goals;
		std::sort(g.begin(), g.end(), [](const Vec2i &a, const Vec2i &b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
		r.distinctGoals = static_cast<int>(std::unique(g.begin(), g.end()) - g.begin());
	}
	std::vector<Vec2i> pulled(units.size(), Vec2i(-1, -1));
	std::vector<int> steps(units.size(), 0);
	std::vector<Vec2i> last(units.size());
	for (size_t i = 0; i != units.size(); ++i) {
		last[i] = units[i]->tilePos;
	}
	for (int cycle = 0; cycle != 6000; ++cycle) {
		bool all = true;
		for (size_t i = 0; i != units.size(); ++i) {
			CUnit &u = *units[i];
			if (u.CurrentAction() == UnitAction::Still) {
				continue;
			}
			all = false;
			w.Cycle(u);
			--GameCycle;
			if (u.CurrentAction() == UnitAction::Move && pulled[i].x < 0) {
				const auto &order = static_cast<const COrder_Move &>(*u.CurrentOrder());
				if (order.GetGoalPulled()) {
					pulled[i] = order.GetGoalPos();
				}
			}
			if (u.tilePos != last[i]) {
				last[i] = u.tilePos;
				++steps[i];
				r.lastStep = cycle + 16;
			}
			if (u.CurrentAction() == UnitAction::Still) {
				r.lastStill = cycle;
				if (r.firstStill < 0) {
					r.firstStill = cycle;
				}
			}
		}
		++GameCycle;
		if (all) {
			break;
		}
	}
	std::string text;
	for (size_t i = 0; i != units.size(); ++i) {
		const CUnit &u = *units[i];
		const int d = Cheb(u.tilePos, s.target);
		r.best = std::min(r.best, d);
		r.worst = std::max(r.worst, d);
		r.noMove += steps[i] == 0;
		r.longestWalk = std::max(r.longestWalk, steps[i]);
		r.allStill = r.allStill && u.CurrentAction() == UnitAction::Still;
		r.goals.push_back(goals[i]);
		r.ends.push_back(u.tilePos);
		r.steps.push_back(steps[i]);
		text += " " + std::to_string(goals[i].x) + "," + std::to_string(goals[i].y) + ">" + std::to_string(pulled[i].x) + ","
		        + std::to_string(pulled[i].y) + "@" + std::to_string(u.tilePos.x) + "," + std::to_string(u.tilePos.y) + "(d"
		        + std::to_string(d) + " s" + std::to_string(steps[i]) + ")";
	}
	MESSAGE("forest 3482 " << std::string(s.name) << ", " << std::string(group ? "group order" : "Lua OrderUnit")
	        << ": distinct goals " << r.distinctGoals << ", end d " << r.best << "-" << r.worst << ", no move " << r.noMove
	        << ", first still " << r.firstStill << " / last still " << r.lastStill << " / last step " << r.lastStep
	        << " cycles, longest walk " << r.longestWalk << "\n goal>pulled@end:" << text);
	return r;
}
} // namespace

TEST_CASE("forest 3482 report: the round's Lua OrderUnit (one goal for all) against the player's group order" * doctest::skip())
{
	for (const F3482Scene &s : f3482Scenes) {
		RunF3482(s, false);
		RunF3482(s, true);
	}
	// with the rule (on in the game) and without it (3.4.82: Range++ widens the ring)
	MoveStopsAtStandingCrowd = true;
	MESSAGE("variant: MoveStopsAtStandingCrowd on (in the game)");
	for (const F3482Scene &s : f3482Scenes) {
		RunF3482(s, false);
		RunF3482(s, true);
	}
	MoveStopsAtStandingCrowd = false;
	MESSAGE("variant: MoveStopsAtStandingCrowd off (3.4.82)");
	for (const F3482Scene &s : f3482Scenes) {
		RunF3482(s, false);
		RunF3482(s, true);
	}
	MoveStopsAtStandingCrowd = true;
}

TEST_CASE("forest 3482: a group order onto forest near the edge, no round walk, settles as live run 69 F5")
{
	// MoveStopsAtStandingCrowd: on in the game since the run 70 follow-up (FOREST_EMU_3482.md);
	// group 3480 / live 3481 re-pinned to the live forest-goal cases of runs 69 / 70.
	MoveStopsAtStandingCrowd = true;
	// The player's group order (as a tap / hold / right click) of 9 Footmen in a 3 x 3 block on open
	// land next to the forest of round 3.4.82. Live run 69 F5 (9 units, open land -> deep forest):
	// each stops within 1-2 tiles of its own goal, nobody walks round to the far side (1-7 steps),
	// all idle 3.5 s (135 frames) after the order.
	for (int i = 3; i != 5; ++i) {
		const F3482Result r = RunF3482(f3482Scenes[i], true);
		CHECK(r.allStill);
		CHECK(r.distinctGoals == 9);
		CHECK(r.longestWalk <= 7);
		CHECK(r.lastStill <= 135 + 16);
		CHECK(r.worst <= 7);
	}
	// The round's own order (Lua OrderUnit, one shared goal for 20) ends in the crowd round the
	// pulled goal; no unit walks more than a few steps beyond the straight way (no ring shuffle).
	for (int i = 0; i != 2; ++i) {
		const F3482Result r = RunF3482(f3482Scenes[i], false);
		CHECK(r.allStill);
		CHECK(r.longestWalk <= 40);
		CHECK(r.worst <= 10); // the rear joins the crowd, it does not stop in the neck on its way
	}
}

TEST_CASE("forest 3483: a tight group onto the forest's far edge - free group goals are walked to, the rest stop at the near edge")
{
	// Round 3.4.83 item 1a (emulator, 2 of 2 identical): ends 6,93 9,94 8,93 7,93 8,94 | 7,100 | 5,93 |
	// 6,101 7,101 - front d1 (3 units), rear d7, everyone moved, first -> last still 5.7 / 4.2 s,
	// order -> all still 12.4 / 11.7 s. Cause (FOREST_TIGHT_3483.md): the group offsets of the 3 x 3
	// (GroupMoveGoals, translated round the clicked tile 6,100) give units 93 / 95 / 96 the goals
	// 7,100 / 6,101 / 7,101, which are LAND (the clump 4-7,97-99 + 1-6,100 + 0-5,101 ends there); a
	// free goal is not pulled and the unit walks to it down the free column x 8. Live run 69 F2 does
	// the same: 1576's group goal 42,32 was free land on the far (north) edge of the forest, it walked
	// 11 steps round to it and ended d1 from the clicked tile 41,33 (host forest 69 F2: 9 steps, same
	// end). The 6 units with forest goals are pulled through the clump and the Barracks to the
	// Barracks' north side and stop there (d6-7), nobody of them round (live F5: nobody round).
	MoveStopsAtStandingCrowd = true;
	const F3482Scene &s = f3482Scenes[6];
	const F3482Result r = RunF3482(s, true);
	REQUIRE(r.ends.size() == 9);
	CHECK(r.allStill);
	CHECK(r.distinctGoals == 9);
	CHECK(r.noMove == 0); // emulator: everyone moved (goals 16-25 tiles off, beyond NoWayDropReach)
	const auto land = [](const Vec2i &p) {
		const char c = Live3481Rows[p.y - 40][p.x];
		const bool barracks = p.x >= 5 && p.x <= 7 && p.y >= 94 && p.y <= 96;
		return (c == '.' || c == ',') && !barracks;
	};
	int onFreeGoal = 0;
	for (size_t i = 0; i != r.ends.size(); ++i) {
		const int d = Cheb(r.ends[i], s.target);
		if (land(r.goals[i])) {
			CHECK(r.ends[i] == r.goals[i]); // live F2 1576: a free group goal is reached, also round
			++onFreeGoal;
		} else {
			CHECK(r.ends[i].y <= 97);       // the near (north) side: nobody with a forest goal round
			// live F5 3-7 (deep forest, no Barracks); the emulator 6-7 was the port before the stop step
			// beside a pulled goal (run 71 G, PulledGoalStopStep: one more step round the pulled goal)
			CHECK(d >= 3);
			CHECK(d <= 8);
		}
	}
	CHECK(onFreeGoal == 3); // 7,100 / 6,101 / 7,101
	CHECK(r.longestWalk <= 28);                  // emulator 22-25 tiles straight down
	CHECK(r.lastStill - r.firstStill <= 270);    // emulator 5.7 / 4.2 s, live F5 3.5 s (~7 s = 270)
	CHECK(r.lastStill <= 520);                   // emulator 12.4 / 11.7 s after the order
}

// ---- Boxed in by standing own units (S20+ round 3.4.83 item 6, BOXED_IN_IMPL.md) -----------------
// The valley of "All You Need BNE" round the S20+ group test: passable ('.') and blocked ('#') tiles
// of x 56-91, y 8-35 (the map's movement layer); outside the window open land. Units at the
// positions the round sampled (s20_3483/samples3.txt, game 3, 21:49:11 - the idle ones never moved).
namespace
{
const char *const S20Valley[] = {
	"####..............######..###.######", // 8
	"#####.............#######.###.#####.", // 9
	"#####...............#####.###..###..", // 10
	"#####................####.####.###.#", // 11
	"#####................####.####.....#", // 12
	"#####.................###..####...##", // 13
	"#####.................####.#####..##", // 14
	"######................####..########", // 15
	"#######................####..#######", // 16
	".#######..............#####..#######", // 17
	"...#####..............#####..####...", // 18
	"##.#####.............#####..#####.##", // 19
	"##.#####.............#####.#####..##", // 20
	"##..####.............#####.####..###", // 21
	"###.#####.........#######..####.####", // 22
	"###.######........#######.####..####", // 23
	"###.#######......########.####.#####", // 24
	"###.#######......#######..###..#####", // 25
	"##..#######......#######.####.######", // 26
	"##.######......########..####.######", // 27
	"##.######......########.####..######", // 28
	"##.#####.......#######..###..######.", // 29
	"#..####........#######.####.#######.", // 30
	"#.#####.........#####..####.#######.", // 31
	"#.#######.......#####.###...########", // 32
	"..#######......#####..###.##########", // 33
	".########.....######.####.##########", // 34
	".#######......#####..####.##########", // 35
};

void LayS20Valley()
{
	for (int row = 0; row != 28; ++row) {
		for (int col = 0; col != 36; ++col) {
			if (S20Valley[row][col] == '#') {
				Map.Field(56 + col, 8 + row)->Flags = MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable;
			}
		}
	}
	ClearGoalRegionMap();
}

// Game 3: group 57-76 (the 20 ordered to 87,18) and the idle 97-116; 77 / 79 / 81 of group 77-96.
const Vec2i S20Block[20] = {{67, 25}, {66, 23}, {67, 24}, {67, 23}, {67, 22}, {68, 25}, {67, 26}, {68, 26}, {68, 24}, {68, 23},
                            {68, 22}, {66, 22}, {65, 22}, {65, 27}, {66, 27}, {69, 26}, {67, 27}, {68, 27}, {68, 21}, {67, 21}};
const Vec2i S20Idle[20] = {{70, 23}, {70, 22}, {68, 20}, {70, 21}, {70, 20}, {67, 20}, {66, 20}, {65, 20}, {64, 20}, {71, 26},
                           {71, 25}, {71, 24}, {71, 23}, {70, 19}, {64, 29}, {65, 29}, {66, 29}, {67, 29}, {68, 29}, {69, 19}};
const Vec2i S20Boxed[3] = {{66, 21}, {65, 21}, {64, 21}}; // 77, 79, 81

struct BoxedResult {
	int moved = 0;       /// units that took a step
	int notStill = 0;    /// units still on their order at the end
	int lastEnd = 0;     /// cycles from the order to the last order's end
	int searches = 0;    /// AStarFindPath calls of the ordered units
	int maxRange = 0;    /// the largest Range of a Move order
	int noWayDrops = 0;  /// orders ended at the first Execute with no search (PathStepFindsNoWay)
	int boxedDrops = 0;  /// orders ended by the boxed-in rule
	std::string text;
};

/// Run the units given (each with its Move set) for at most `cycles`; count.
BoxedResult RunBoxed(CorridorWorld &w, const std::vector<CUnit *> &ordered, int cycles)
{
	BoxedResult r;
	std::vector<int> steps(ordered.size(), 0), ended(ordered.size(), -1);
	std::vector<Vec2i> start(ordered.size()), pulled(ordered.size(), Vec2i(-1, -1));
	std::vector<char> how(ordered.size(), '-');
	std::vector<bool> executed(ordered.size(), false);
	for (size_t i = 0; i != ordered.size(); ++i) {
		start[i] = ordered[i]->tilePos;
	}
	for (int cycle = 0; cycle != cycles; ++cycle) {
		bool all = true;
		for (size_t i = 0; i != ordered.size(); ++i) {
			CUnit &u = *ordered[i];
			if (u.CurrentAction() != UnitAction::Move) {
				continue;
			}
			all = false;
			r.maxRange = std::max(r.maxRange, static_cast<const COrder_Move *>(u.CurrentOrder())->GetRange());
			const bool first = !executed[i] && u.Wait == 0;
			const int before = AStarSearchCalls;
			const int firstLookDropsBefore = MoveFirstLookDrops;
			const Vec2i at = u.tilePos;
			w.Cycle(u);
			--GameCycle;
			executed[i] = executed[i] || first;
			r.searches += AStarSearchCalls - before;
			if (u.CurrentAction() == UnitAction::Move && pulled[i].x < 0
			    && static_cast<const COrder_Move *>(u.CurrentOrder())->GetGoalPulled()) {
				pulled[i] = static_cast<const COrder_Move *>(u.CurrentOrder())->GetGoalPos();
			}
			if (u.tilePos != at) {
				++steps[i];
			}
			if (u.CurrentAction() != UnitAction::Move) {
				ended[i] = cycle;
				if (first && AStarSearchCalls == before) {
					how[i] = 'N';
					++r.noWayDrops;
				} else if ((AStarSearchCalls != before && AStarLastFailBoxed) || MoveFirstLookDrops != firstLookDropsBefore) {
					// the boxed-in rule, or the first look's drop (run 73 S) that comes without a search
					how[i] = 'B';
					++r.boxedDrops;
				} else {
					how[i] = steps[i] ? 'W' : 'R';
				}
			}
		}
		++GameCycle;
		if (all) {
			break;
		}
	}
	for (size_t i = 0; i != ordered.size(); ++i) {
		const CUnit &u = *ordered[i];
		r.moved += steps[i] > 0;
		r.notStill += u.CurrentAction() == UnitAction::Move;
		r.lastEnd = std::max(r.lastEnd, ended[i]);
		r.text += "\n " + std::to_string(start[i].x) + "," + std::to_string(start[i].y) + " pulled "
		          + std::to_string(pulled[i].x) + "," + std::to_string(pulled[i].y) + " " + how[i] + " steps "
		          + std::to_string(steps[i]) + " end " + std::to_string(u.tilePos.x) + "," + std::to_string(u.tilePos.y)
		          + " at " + std::to_string(ended[i]);
	}
	return r;
}

void OrderGroup(const std::vector<CUnit *> &units, const Vec2i &target)
{
	std::vector<Vec2i> starts;
	for (const CUnit *u : units) {
		starts.push_back(u->tilePos);
	}
	const std::vector<Vec2i> goals = GroupMoveGoals(starts, target, 128, 128);
	for (size_t i = 0; i != units.size(); ++i) {
		units[i]->Orders.clear();
		units[i]->Orders.push_back(COrder::NewActionMove(goals[i]));
		units[i]->pathFinderData->output.Length = 0;
	}
}

/// Item 6 note A: 77 / 79 / 81 on row 21, closed in by the forest edge, the idle row 20 (102-105),
/// 76 and the block's row 22 - ordered as a group to {40,40}.
BoxedResult RunS20NoteA(bool dropsWhenBoxed)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	LayS20Valley();
	for (const Vec2i &p : S20Block) {
		w.Place(w.footman, p);
	}
	for (const Vec2i &p : S20Idle) {
		w.Place(w.footman, p);
	}
	std::vector<CUnit *> boxed;
	for (const Vec2i &p : S20Boxed) {
		boxed.push_back(&w.Place(w.footman, p));
	}
	MoveDropsWhenBoxedIn = dropsWhenBoxed;
	OrderGroup(boxed, Vec2i(40, 40));
	BoxedResult r = RunBoxed(w, boxed, 2000);
	MoveDropsWhenBoxedIn = true;
	return r;
}

/// One unit in open land boxed by 8 idle units, sent `dist` tiles east; the ring walks off at
/// `openAt` (-1 never).
BoxedResult RunIdleBox(bool dropsWhenBoxed, int dist, int openAt)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	const Vec2i c(20, 60);
	std::vector<CUnit *> ring;
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			if (dx || dy) {
				ring.push_back(&w.Place(w.footman, c + Vec2i(dx, dy)));
			}
		}
	}
	CUnit &unit = w.Place(w.footman, c);
	const Vec2i goal = c + Vec2i(dist, 0);
	MoveDropsWhenBoxedIn = dropsWhenBoxed;
	unit.Orders.clear();
	unit.Orders.push_back(COrder::NewActionMove(goal));
	unit.pathFinderData->output.Length = 0;
	BoxedResult r = RunBoxed(w, {&unit}, openAt >= 0 ? openAt : 2000);
	if (openAt >= 0) {
		OrderAll(ring, Vec2i(5, 40));
		std::vector<CUnit *> both = ring;
		both.push_back(&unit);
		for (int cycle = 0; cycle != 1500; ++cycle) {
			const Vec2i at = unit.tilePos;
			bool all = true;
			for (CUnit *u : both) {
				if (u->CurrentAction() != UnitAction::Still) {
					all = false;
					w.Cycle(*u);
					--GameCycle;
				}
			}
			++GameCycle;
			r.moved += unit.tilePos != at;
			if (all) {
				break;
			}
		}
		r.text += "\n after the box opened: steps " + std::to_string(r.moved) + ", end " + std::to_string(unit.tilePos.x)
		          + "," + std::to_string(unit.tilePos.y) + " d" + std::to_string(Cheb(unit.tilePos, goal));
	}
	MoveDropsWhenBoxedIn = true;
	return r;
}

/// Item 6 note B, game 3: group 57-76 sent to the rock tile 87,18 (Barracks 75,12). `closeRing`: every
/// free tile round the block held by an idle unit (the round's Archers / Peasants and the not yet
/// ordered group 77-96 stood round it; their tiles were not sampled).
BoxedResult RunS20NoteB(bool closeRing)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	LayS20Valley();
	for (int y = 12; y != 15; ++y) {
		for (int x = 75; x != 78; ++x) {
			Map.Field(x, y)->Flags |= MapFieldBuilding;
		}
	}
	ClearGoalRegionMap();
	std::vector<CUnit *> block;
	for (const Vec2i &p : S20Block) {
		block.push_back(&w.Place(w.footman, p));
	}
	for (const Vec2i &p : S20Idle) {
		w.Place(w.footman, p);
	}
	for (const Vec2i &p : S20Boxed) {
		w.Place(w.footman, p);
	}
	if (closeRing) {
		for (const Vec2i &p : S20Block) {
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const Vec2i n = p + Vec2i(dx, dy);
					if (!Map.Field(n)->CheckMask(LandMask)) {
						w.Place(w.footman, n);
					}
				}
			}
		}
	}
	OrderGroup(block, Vec2i(87, 18));
	return RunBoxed(w, block, 3000);
}

struct MateBoxResult {
	int xSteps = 0;       /// steps of the boxed unit X
	int xEnd = -1;        /// cycle X's order ended (-1: still on it)
	int mateFirstStep = -1;
	int xFirstStep = -1;
	Vec2i xAt;
};

/// Live run 72 m4-m6: X boxed by 8 Footmen, 7 idle; the east-middle one (the mate) gets its order
/// `mateDelay` cycles after X's (0: the same right click), both to the east. X is handled first in
/// each cycle unless `mateFirst` (run 72 m6: the mate 1573 below X 1576 was handled first in the
/// frame; run 73 S3: the rear 1567 below the front 1576 first).
MateBoxResult RunMateBox(int mateDelay, int cycles, bool mateFirst = false)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	const Vec2i c(20, 60);
	std::vector<CUnit *> ring;
	CUnit *mate = nullptr;
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			if (dx || dy) {
				CUnit &u = w.Place(w.footman, c + Vec2i(dx, dy));
				ring.push_back(&u);
				if (dx == 1 && dy == 0) {
					mate = &u;
				}
			}
		}
	}
	CUnit &x = w.Place(w.footman, c);
	x.Orders.clear();
	x.Orders.push_back(COrder::NewActionMove(c + Vec2i(14, -1)));
	x.pathFinderData->output.Length = 0;
	MateBoxResult r;
	for (int cycle = 0; cycle != cycles; ++cycle) {
		if (cycle == mateDelay) {
			mate->Orders.clear();
			mate->Orders.push_back(COrder::NewActionMove(c + Vec2i(14, 0)));
			mate->pathFinderData->output.Length = 0;
		}
		const Vec2i mateAt = mate->tilePos;
		if (mateFirst && mate->CurrentAction() == UnitAction::Move) {
			w.Cycle(*mate);
			--GameCycle;
		}
		const Vec2i xAt = x.tilePos;
		if (x.CurrentAction() == UnitAction::Move) {
			w.Cycle(x);
			--GameCycle;
			if (x.CurrentAction() != UnitAction::Move && r.xEnd < 0) {
				r.xEnd = cycle;
			}
		}
		if (x.tilePos != xAt) {
			if (r.xSteps == 0) {
				r.xFirstStep = cycle;
			}
			++r.xSteps;
		}
		if (!mateFirst && mate->CurrentAction() == UnitAction::Move) {
			w.Cycle(*mate);
			--GameCycle;
		}
		if (mate->tilePos != mateAt && r.mateFirstStep < 0) {
			r.mateFirstStep = cycle;
		}
		++GameCycle;
	}
	r.xAt = x.tilePos;
	return r;
}
} // namespace

TEST_CASE("boxed 3483: units closed in by idle own units drop the order at once, never move, no search loop")
{
	// S20+ round 3.4.83 item 6 note A: 77 / 79 / 81 never moved in either order (boxed on row 21).
	// Live (run 69 H5 1570): a unit closed in by standing units drops its order within 3 frames and
	// never moves; idle units never step aside (H2 / H5).
	const BoxedResult a = RunS20NoteA(true);
	MESSAGE("note A, boxed rule on: moved " << a.moved << ", boxed drops " << a.boxedDrops << ", searches " << a.searches
	        << ", last end " << a.lastEnd << ", max Range " << a.maxRange << a.text);
	CHECK(a.moved == 0);
	CHECK(a.notStill == 0);
	// the three hold each other (mates on their way): since run 73 S their first look decides and the
	// orders end 3 cycles after them (before: the jam waits, 10 x 38 cycles); no Range++ ramp (3.4.83:
	// 156 searches, Range up to 31)
	CHECK(a.boxedDrops == 3);
	// live run 77 C: a unit boxed by idle units and mates of its click looks again each cycle while
	// they may still leave, up to 64 cycles (CoreWaitsForItsClickCycles), then drops: 3 x 64 = 192
	// searches of a boxed unit (no tile entered; build 3 before run 77: 63)
	CHECK(a.searches <= 3 * 65);
	CHECK(a.lastEnd <= 10 * (PathWaitExpiryCycles + 1) + 2); // build 3: 390
	CHECK(a.maxRange == 0);

	const BoxedResult b = RunIdleBox(true, 100, -1);
	MESSAGE("8 idle round, goal 100 off, rule on: searches " << b.searches << ", end at " << b.lastEnd << ", max Range "
	        << b.maxRange << b.text);
	CHECK(b.moved == 0);
	CHECK(b.notStill == 0);
	CHECK(b.boxedDrops == 1);
	// run 72: the unit looks again each cycle up to MoveBoxedDropCycles after its order, then drops
	CHECK(b.searches <= static_cast<int>(MoveBoxedDropCycles) + 1);
	CHECK(b.lastEnd >= 2);
	CHECK(b.lastEnd <= static_cast<int>(MoveBoxedDropCycles));
}

TEST_CASE("boxed 3483 report: the boxed-in rule off (as 3.4.83) and the game 3 group to the rock tile" * doctest::skip())
{
	const BoxedResult a = RunS20NoteA(false);
	MESSAGE("note A, rule off: moved " << a.moved << ", searches " << a.searches << ", last end " << a.lastEnd
	        << ", max Range " << a.maxRange << ", not still " << a.notStill << a.text);
	const BoxedResult b = RunIdleBox(false, 100, -1);
	MESSAGE("8 idle round, goal 100 off, rule off: searches " << b.searches << ", end at " << b.lastEnd << ", max Range "
	        << b.maxRange << b.text);
	for (const bool on : {false, true}) {
		const BoxedResult o = RunIdleBox(on, 100, 40);
		MESSAGE("box opens at cycle 40, rule " << std::string(on ? "on" : "off") << ": searches " << o.searches << ", max Range "
		        << o.maxRange << o.text);
	}
	for (const bool ring : {false, true}) {
		const BoxedResult g = RunS20NoteB(ring);
		MESSAGE("note B game 3, 57-76 -> 87,18, ring " << std::string(ring ? "closed" : "as sampled") << ": moved " << g.moved
		        << ", no-way drops " << g.noWayDrops << ", boxed drops " << g.boxedDrops << ", searches " << g.searches
		        << ", last end " << g.lastEnd << ", max Range " << g.maxRange << ", not still " << g.notStill << g.text);
	}
}

TEST_CASE("boxed run 72: a boxed unit gets a free tile within 3 cycles of its order or drops it")
{
	// B1 (3 / 3): 8 idle round, X sent 13 off: dropped 2-3 frames after the order, never moved; the
	// east side walked off later and X stayed idle (no wait, no retry).
	const BoxedResult b1 = RunIdleBox(true, 13, 40);
	MESSAGE("B1: searches " << b1.searches << ", end at " << b1.lastEnd << b1.text);
	CHECK(b1.boxedDrops == 1);
	CHECK(b1.lastEnd >= 2);
	CHECK(b1.lastEnd <= static_cast<int>(MoveBoxedDropCycles));
	CHECK(b1.moved == 0);

	// m6: X and the mate ordered in the same frame, the mate (handled first) steps off, X follows
	// through the gap in the same frame.
	const MateBoxResult same = RunMateBox(0, 400, true);
	MESSAGE("same cycle, mate first: X steps " << same.xSteps << " (first at " << same.xFirstStep << "), X end "
	        << same.xEnd << ", mate first step " << same.mateFirstStep);
	CHECK(same.mateFirstStep == 0);
	CHECK(same.xSteps > 0);
	CHECK(same.xFirstStep == same.mateFirstStep);

	// The same frame, X handled before the mate. Was modelled on run 73 S3 (a drop), but S3's rear had
	// free tiles round it; live run 77 C (CALIB_RUN77.md): units boxed by idle units and mates of their
	// click waited 35-64 frames and walked, none dropped - and run 72 m6 walked. So X waits for the
	// mate and follows it 1 cycle later.
	const MateBoxResult sameXFirst = RunMateBox(0, 400, false);
	MESSAGE("same cycle, X first: X steps " << sameXFirst.xSteps << ", X end " << sameXFirst.xEnd << ", mate first step "
	        << sameXFirst.mateFirstStep << ", X first step " << sameXFirst.xFirstStep);
	CHECK(sameXFirst.mateFirstStep == 0);
	CHECK(sameXFirst.xSteps > 0);
	CHECK(sameXFirst.xFirstStep >= sameXFirst.mateFirstStep);
	CHECK(sameXFirst.xFirstStep <= sameXFirst.mateFirstStep + 2);

	// m4 / m5 as live: the mate ordered 2 frames after X - X drops, never moves.
	const MateBoxResult two = RunMateBox(2, 400);
	CHECK(two.xSteps == 0);
	CHECK(two.xEnd <= static_cast<int>(MoveBoxedDropCycles));

	// m4 / m5 (and run 69 H5 1570): the mate's step comes after X's window: X drops, never moves,
	// also when the mate has walked off.
	const MateBoxResult late = RunMateBox(static_cast<int>(MoveBoxedDropCycles), 400);
	MESSAGE("mate 3 cycles later: X steps " << late.xSteps << ", X end " << late.xEnd << ", mate first step "
	        << late.mateFirstStep);
	CHECK(late.mateFirstStep >= static_cast<int>(MoveBoxedDropCycles));
	CHECK(late.xEnd >= 2);
	CHECK(late.xEnd <= static_cast<int>(MoveBoxedDropCycles));
	CHECK(late.xSteps == 0);
}

// ---- Live run 73 (work/agent-orig-73/ORIG73.md; Human II, the natural neck 52,42) ----------------
// R: the neck 52,42 between forest (x 51) and coast (x 53) is the only link between a pocket
// 50-52,40-41 (sealed to the north by idle units and the coast) and the land south of it. 2 Footmen
// in the pocket, 2 south of the neck, each pair ordered through it to the other side; 16 frames a
// step, 38.5 frames a second. Live: 3 head-on trials (R2-R4): no back-out, no swap, nobody passes
// the face-to-face pair, every blocked order dropped within 16-38 frames, all four idle for good
// (2 of 12 unit runs passed before / round the block); R1 (pocket pair first): all passed.
// S: 2 Footmen in a column in the neck, the rear's only first step the front's tile.
namespace
{
void LayNeck73()
{
	for (int y = 0; y < 128; ++y) {
		for (int x = 0; x < 128; ++x) {
			const bool pocket = x >= 50 && x <= 52 && y >= 40 && y <= 41;
			const bool neck = x == 52 && y == 42;
			// 51,43 forest: no live track ever crossed it (all went 52,44 -> 52,43 -> 52,42)
			const bool south = x >= 47 && x <= 56 && y >= 43 && y <= 51 && !(x == 51 && y == 43);
			CMapField &f = *Map.Field(x, y);
			const tile_flags keep = f.Flags & (MapFieldBuilding | MapFieldLandUnit);
			f.Flags = (pocket || neck || south ? MapFieldLandAllowed
			                                   : MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable) | keep;
		}
	}
	ClearGoalRegionMap();
}

struct Neck73Unit {
	Vec2i start;
	Vec2i goal;
	int orderAt = 0; /// cycle of the order (relative)
	CUnit *unit = nullptr;
	std::vector<Vec2i> tiles;
	int end = -1;       /// cycle the order ended
	int firstStep = -1; /// cycle of the first step
	int backSteps = 0;  /// steps away from its goal's side (a back-out)
	bool pocketStart = false;
};

struct Neck73Result {
	int passed = 0;
	int stopped = 0;
	int backSteps = 0;
	bool swapped = false;
	bool allStill = true;
	int faceOff = -1;  /// first cycle a pocket unit and a south unit stood next to each other in the road, both on orders
	int lastEnd = 0;
	std::string text;
};

/// `units` in the order they are handled each cycle (the unit index live).
Neck73Result RunNeck73(std::vector<Neck73Unit> &units, int cycles)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	LayNeck73();
	for (Neck73Unit &n : units) {
		n.unit = &w.Place(w.footman, n.start);
		n.tiles.push_back(n.start);
		n.pocketStart = n.start.y <= 41;
	}
	Neck73Result r;
	for (int cycle = 0; cycle != cycles; ++cycle) {
		for (Neck73Unit &n : units) {
			if (cycle == n.orderAt) {
				n.unit->Orders.clear();
				n.unit->Orders.push_back(COrder::NewActionMove(n.goal));
				n.unit->pathFinderData->output.Length = 0;
			}
		}
		std::vector<Vec2i> before;
		for (Neck73Unit &n : units) {
			before.push_back(n.unit->tilePos);
		}
		for (Neck73Unit &n : units) {
			CUnit &u = *n.unit;
			if (u.CurrentAction() != UnitAction::Move) {
				continue;
			}
			const Vec2i at = u.tilePos;
			w.Cycle(u);
			--GameCycle;
			if (u.tilePos != at) {
				if (n.firstStep < 0) {
					n.firstStep = cycle;
				}
				n.tiles.push_back(u.tilePos);
				// the pocket pair goes south (y grows), the other north
				if (n.pocketStart ? u.tilePos.y < at.y : u.tilePos.y > at.y) {
					++n.backSteps;
				}
			}
			if (u.CurrentAction() != UnitAction::Move && n.end < 0) {
				n.end = cycle;
			}
		}
		for (size_t i = 0; i != units.size(); ++i) {
			for (size_t j = i + 1; j != units.size(); ++j) {
				if (units[i].unit->tilePos == before[j] && units[j].unit->tilePos == before[i]) {
					r.swapped = true;
				}
			}
		}
		if (r.faceOff < 0) {
			for (const Neck73Unit &p : units) {
				for (const Neck73Unit &s : units) {
					const CUnit &a = *p.unit;
					const CUnit &b = *s.unit;
					if (p.pocketStart && !s.pocketStart && a.Moving == 0 && b.Moving == 0
					    && a.CurrentAction() == UnitAction::Move && b.CurrentAction() == UnitAction::Move
					    && Cheb(a.tilePos, b.tilePos) == 1 && b.tilePos.y > a.tilePos.y
					    && a.tilePos.y >= 41 && b.tilePos.y <= 43) {
						r.faceOff = cycle;
					}
				}
			}
		}
		++GameCycle;
	}
	for (const Neck73Unit &n : units) {
		const CUnit &u = *n.unit;
		const bool passed = n.pocketStart ? u.tilePos.y >= 43 : u.tilePos.y <= 41;
		r.passed += passed;
		r.stopped += !passed;
		r.backSteps += n.backSteps;
		r.allStill = r.allStill && u.CurrentAction() == UnitAction::Still;
		r.lastEnd = std::max(r.lastEnd, n.end);
		r.text += "\n " + std::to_string(n.start.x) + "," + std::to_string(n.start.y) + " -> " + std::to_string(n.goal.x) + ","
		          + std::to_string(n.goal.y) + " ordered " + std::to_string(n.orderAt) + ": "
		          + std::string(passed ? "passed" : "STOPPED") + ", first step " + std::to_string(n.firstStep) + ", end "
		          + std::to_string(n.end) + " on " + std::to_string(u.tilePos.x) + "," + std::to_string(u.tilePos.y)
		          + ", back steps " + std::to_string(n.backSteps) + ": " + Text(n.tiles);
	}
	return r;
}

/// R2 / R3 / R4 / R1 with the live starts, stored goals and order gaps (the units in index order
/// 1566, 1567, 1574, 1576).
std::vector<Neck73Unit> Neck73Trial(int trial)
{
	const auto u = [](Vec2i s, Vec2i g, int at) {
		Neck73Unit n;
		n.start = s;
		n.goal = g;
		n.orderAt = at;
		return n;
	};
	switch (trial) {
		case 2: // south pair f281, pocket pair f355
			return {u({50, 40}, {49, 46}, 74), u({52, 40}, {51, 46}, 74), u({50, 47}, {51, 41}, 0), u({51, 46}, {52, 40}, 0)};
		case 3: // south pair f276-278, pocket pair f350
			return {u({50, 47}, {52, 40}, 1), u({51, 41}, {50, 47}, 74), u({49, 48}, {51, 41}, 0), u({52, 40}, {51, 46}, 74)};
		case 4: // as R3, the pocket pair f384
			return {u({50, 47}, {52, 40}, 1), u({51, 41}, {50, 47}, 108), u({49, 48}, {51, 41}, 0), u({52, 40}, {51, 46}, 108)};
		default: // R1: the pocket pair about 2 s first
			return {u({50, 47}, {52, 40}, 80), u({51, 41}, {50, 47}, 0), u({49, 48}, {51, 41}, 80), u({52, 40}, {51, 46}, 0)};
	}
}

/// S: front (1576) in the neck to 50,40, rear (1567) south of it to 50,41, 1566 idle on 51,41.
/// `rearDelay`: the rear's order relative to the front's; `rearFirst`: the rear is handled first.
struct Neck73S {
	int rearSteps = 0;
	int rearFirstStep = -1;
	int rearEnd = -1;
	int frontFirstStep = -1;
};

Neck73S RunNeck73S(int rearDelay, bool rearFirst)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	LayNeck73();
	w.Place(w.footman, {51, 41});
	CUnit &rear = w.Place(w.footman, {52, 43});
	CUnit &front = w.Place(w.footman, {52, 42});
	const int frontAt = std::max(0, -rearDelay);
	const int rearAt = std::max(0, rearDelay);
	Neck73S s;
	for (int cycle = 0; cycle != 400; ++cycle) {
		if (cycle == frontAt) {
			front.Orders.clear();
			front.Orders.push_back(COrder::NewActionMove({50, 40}));
			front.pathFinderData->output.Length = 0;
		}
		if (cycle == rearAt) {
			rear.Orders.clear();
			rear.Orders.push_back(COrder::NewActionMove({50, 41}));
			rear.pathFinderData->output.Length = 0;
		}
		CUnit *const handled[2] = {rearFirst ? &rear : &front, rearFirst ? &front : &rear};
		for (CUnit *unit : handled) {
			if (unit->CurrentAction() != UnitAction::Move) {
				continue;
			}
			const Vec2i at = unit->tilePos;
			w.Cycle(*unit);
			--GameCycle;
			if (unit->tilePos != at) {
				if (unit == &rear) {
					if (s.rearFirstStep < 0) {
						s.rearFirstStep = cycle;
					}
					++s.rearSteps;
				} else if (s.frontFirstStep < 0) {
					s.frontFirstStep = cycle;
				}
			}
			if (unit == &rear && unit->CurrentAction() != UnitAction::Move && s.rearEnd < 0) {
				s.rearEnd = cycle - rearAt;
			}
		}
		++GameCycle;
	}
	return s;
}
} // namespace

TEST_CASE("closed road 73: 2 against 2 head-on in a closed 1-tile road - no back-out, orders dropped, jam with idle units")
{
	int passedHeadOn = 0;
	for (const int trial : {2, 3, 4}) {
		std::vector<Neck73Unit> units = Neck73Trial(trial);
		const int before = MoveHeadOnClosedDropCount;
		const Neck73Result r = RunNeck73(units, 1200); // 30 s logged live: 1155 frames
		MESSAGE("R" << trial << ": passed " << r.passed << ", stopped " << r.stopped << ", back steps " << r.backSteps
		        << ", face-off at " << r.faceOff << ", last order end " << r.lastEnd << ", closed head-on drops "
		        << MoveHeadOnClosedDropCount - before << r.text);
		CHECK(r.allStill);       // every order ended: idle for good, no retry
		CHECK(r.backSteps == 0); // nobody backs out (live 0 of 12)
		CHECK_FALSE(r.swapped);  // no swap
		passedHeadOn += r.passed;
		// R2: 1576 through before the pocket pair moved, the other three stopped; R3: nobody passed.
		// R4 is not pinned: live 1576 stepped straight to 52,41 5 frames after its order, face to face
		// with 1574 in the neck; ours starts at once and takes the diagonal 51,41 (a tie of the
		// search), so the pocket lets everybody pass (4 of 4) - the timing of the meeting, not the rule.
		if (trial == 2) {
			CHECK(r.passed == 1);
		} else if (trial == 3) {
			CHECK(r.passed == 0);
		}
		if (r.faceOff >= 0) {
			// live: the last blocked order dropped 16-38 frames after the face-off; one step more here
			// for a unit behind (its search ends it by the crowd stop / Range rules)
			CHECK(r.lastEnd - r.faceOff <= 38 + 16);
		}
	}
	MESSAGE("R2-R4 passed " << passedHeadOn << " of 12 (live 2 of 12)");

	// R1: the pocket pair goes first, the meeting never comes - all four pass.
	std::vector<Neck73Unit> r1 = Neck73Trial(1);
	const Neck73Result a = RunNeck73(r1, 1200);
	MESSAGE("R1: passed " << a.passed << ", back steps " << a.backSteps << a.text);
	CHECK(a.allStill);
	CHECK(a.passed == 4);
}

TEST_CASE("closed road 73 S: same-frame mates in the neck - the first look decides (3 drop / 1 walk live)")
{
	// S3: both ordered in the same frame, the rear (1567, below 1576) handled first: dropped in the
	// frame the front stepped, never moved.
	const Neck73S s3 = RunNeck73S(0, true);
	MESSAGE("same cycle, rear first: rear steps " << s3.rearSteps << ", end " << s3.rearEnd << ", front first step "
	        << s3.frontFirstStep);
	CHECK(s3.rearSteps == 0);
	CHECK(s3.rearEnd >= 2);
	CHECK(s3.rearEnd <= static_cast<int>(MoveBoxedDropCycles));
	CHECK(s3.frontFirstStep == 0);
	// S1 / S4: the rear ordered 1 frame before the front: dropped 3 frames after its order.
	const Neck73S s1 = RunNeck73S(-1, true);
	MESSAGE("rear 1 cycle first: rear steps " << s1.rearSteps << ", end " << s1.rearEnd);
	CHECK(s1.rearSteps == 0);
	CHECK(s1.rearEnd <= static_cast<int>(MoveBoxedDropCycles));
	// S2: the front ordered 1 frame before the rear: the rear steps 1 frame after the front's step.
	const Neck73S s2 = RunNeck73S(1, true);
	MESSAGE("front 1 cycle first: rear first step " << s2.rearFirstStep << ", front first step " << s2.frontFirstStep
	        << ", rear steps " << s2.rearSteps);
	CHECK(s2.rearSteps > 0);
	CHECK(s2.rearFirstStep - s2.frontFirstStep == 1);
	// Run 72 m6's order of handling (the mate first in the frame): the rear walks, in the same cycle.
	const Neck73S m6 = RunNeck73S(0, false);
	MESSAGE("same cycle, front first: rear first step " << m6.rearFirstStep << ", front " << m6.frontFirstStep);
	CHECK(m6.rearSteps > 0);
	CHECK(m6.rearFirstStep == m6.frontFirstStep);
}

// ---- Live run 74 (work/agent-orig-74/ORIG74.md; Human II, north of the run 73 pocket) ----------
// A column of 4 Footmen in single file on x 51 ordered in one click (order frames 0-4 apart, the
// state flips logged) through the corridor x 51-53 rows 38-39 between forest (x <= 50) and the
// coast (x 54); idle walls on 52,38 / 53,38 / 52,39 / 53,39 (4a: a 1-tile neck x 51, 2 deep) or on
// 53,38 / 53,39 only (4b: a 2-tile neck x 51-52). A long way round exists: south through the run 73
// neck 52,42, west round the forest and north (live ~35 steps). Live 4a: 4 passed, 4 went round at
// once (one after a 33-frame stand behind a mate on its way round), 0 dropped; 4b: 8 passed, 0 round,
// 0 dropped, 3 stands of 31-32 frames for the lane tile.
// The scene is laid with the live topology (forest block, pocket, run 73 neck, coast), not the
// live forest tile by tile: the way round here is of the live order of length, not its exact tiles.
namespace
{
void LayNeck74()
{
	for (int y = 0; y < 128; ++y) {
		for (int x = 0; x < 128; ++x) {
			const bool north = x >= 30 && x <= 53 && y >= 25 && y <= 37;
			const bool corridor = x >= 51 && x <= 53 && y >= 38 && y <= 39;
			const bool pocket = x >= 50 && x <= 52 && y >= 40 && y <= 41;
			const bool neck = x == 52 && y == 42;
			const bool south = (x >= 47 && x <= 56 && y >= 43 && y <= 44 && !(x == 51 && y == 43))
			                   || (x >= 30 && x <= 56 && y >= 45 && y <= 51);
			const bool west = x >= 30 && x <= 38 && y >= 25 && y <= 51;
			CMapField &f = *Map.Field(x, y);
			const tile_flags keep = f.Flags & (MapFieldBuilding | MapFieldLandUnit);
			f.Flags = (north || corridor || pocket || neck || south || west
			               ? MapFieldLandAllowed
			               : MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable)
			          | keep;
		}
	}
	ClearGoalRegionMap();
}

struct Neck74Unit {
	int id = 0;
	Vec2i start;
	Vec2i goal;
	int orderAt = 0;
	char live = '?'; /// 'P' passed, 'R' round, 'D' dropped
	CUnit *unit = nullptr;
	std::vector<Vec2i> tiles;
	std::vector<int> stands; /// cycles on each tile
	int end = -1;
	int firstStep = -1;
	int minX = 128;
};

struct Neck74Result {
	int passed = 0;
	int round = 0;
	int dropped = 0;
	int asLive = 0;
	int longestStand = 0; /// the longest stay on a tile between the first step and the arrival
	bool allStill = true;
	std::string text;
};

/// `units` in the order they are handled each cycle (the unit index live: 1566, 1567, 1568, 1569).
/// `sealed`: an idle unit on the run 73 neck 52,42 closes the way round (the run 73 S situation).
Neck74Result RunNeck74(std::vector<Neck74Unit> &units, bool twoTile, int cycles, bool sealed = false)
{
	CAnimations slow;
	CorridorWorld w;
	SlowFootman(w, slow);
	LayNeck74();
	std::vector<Vec2i> walls = {{53, 38}, {53, 39}};
	if (!twoTile) {
		walls.push_back({52, 38});
		walls.push_back({52, 39});
	}
	if (sealed) {
		walls.push_back({52, 42});
	}
	for (const Vec2i &p : walls) {
		w.Place(w.footman, p);
	}
	for (Neck74Unit &n : units) {
		n.unit = &w.Place(w.footman, n.start);
		n.tiles.push_back(n.start);
		n.stands.push_back(0);
	}
	for (int cycle = 0; cycle != cycles; ++cycle) {
		for (Neck74Unit &n : units) {
			if (cycle == n.orderAt) {
				n.unit->Orders.clear();
				n.unit->Orders.push_back(COrder::NewActionMove(n.goal));
				n.unit->pathFinderData->output.Length = 0;
			}
		}
		for (Neck74Unit &n : units) {
			CUnit &u = *n.unit;
			if (u.CurrentAction() != UnitAction::Move) {
				continue;
			}
			const Vec2i at = u.tilePos;
			w.Cycle(u);
			--GameCycle;
			if (u.tilePos != at) {
				if (n.firstStep < 0) {
					n.firstStep = cycle;
				}
				n.tiles.push_back(u.tilePos);
				n.stands.push_back(0);
				n.minX = std::min(n.minX, int(u.tilePos.x));
			} else {
				++n.stands.back();
			}
			if (u.CurrentAction() != UnitAction::Move && n.end < 0) {
				n.end = cycle;
			}
		}
		++GameCycle;
	}
	Neck74Result r;
	for (const Neck74Unit &n : units) {
		const CUnit &u = *n.unit;
		const bool arrived = Cheb(u.tilePos, n.goal) <= 1;
		const char got = !arrived ? 'D' : n.minX <= 46 ? 'R' : 'P';
		r.passed += got == 'P';
		r.round += got == 'R';
		r.dropped += got == 'D';
		r.asLive += got == n.live;
		r.allStill = r.allStill && u.CurrentAction() == UnitAction::Still;
		int longest = 0;
		for (size_t i = 1; i + 1 < n.stands.size(); ++i) {
			longest = std::max(longest, n.stands[i]);
		}
		r.longestStand = std::max(r.longestStand, longest);
		r.text += "\n " + std::to_string(n.id) + " " + std::to_string(n.start.x) + "," + std::to_string(n.start.y) + " -> "
		          + std::to_string(n.goal.x) + "," + std::to_string(n.goal.y) + " ordered " + std::to_string(n.orderAt) + ": "
		          + got + " (live " + n.live + "), first step " + std::to_string(n.firstStep) + ", end "
		          + std::to_string(n.end) + ", steps " + std::to_string(n.tiles.size() - 1) + ", longest stand "
		          + std::to_string(longest) + ": " + Text(n.tiles);
	}
	return r;
}

/// The live trials: starts, goals (target + start offset), order frames relative to the first, the
/// outcome; in index order.
std::vector<Neck74Unit> Neck74Trial(const std::string &trial)
{
	const auto u = [](int id, Vec2i s, Vec2i g, int at, char live) {
		Neck74Unit n;
		n.id = id;
		n.start = s;
		n.goal = g;
		n.orderAt = at;
		n.live = live;
		return n;
	};
	if (trial == "n1") { // 4a north, f287-f291
		return {u(1566, {51, 38}, {50, 35}, 2, 'P'), u(1567, {51, 39}, {49, 35}, 1, 'R'),
		        u(1568, {51, 40}, {49, 36}, 4, 'P'), u(1569, {51, 41}, {49, 37}, 0, 'R')};
	}
	if (trial == "n2") { // 4a south, f273-f275
		return {u(1566, {51, 39}, {52, 43}, 2, 'P'), u(1567, {51, 37}, {52, 41}, 0, 'R'),
		        u(1568, {51, 38}, {52, 42}, 0, 'R'), u(1569, {50, 36}, {51, 40}, 2, 'P')};
	}
	if (trial == "w1") { // 4b north, f275-f277
		return {u(1566, {51, 40}, {49, 36}, 2, 'P'), u(1567, {51, 41}, {49, 37}, 0, 'P'),
		        u(1568, {51, 39}, {49, 35}, 2, 'P'), u(1569, {51, 38}, {50, 35}, 2, 'P')};
	}
	// w2: 4b south, f274-f277
	return {u(1566, {51, 37}, {52, 41}, 3, 'P'), u(1567, {50, 36}, {51, 40}, 0, 'P'),
	        u(1568, {51, 38}, {52, 42}, 3, 'P'), u(1569, {51, 39}, {52, 43}, 2, 'P')};
}
} // namespace

TEST_CASE("column 74: a column of 4 in one click through a 1-tile neck with a way round - nobody drops, held mates go round")
{
	int passed = 0;
	int round = 0;
	int asLive = 0;
	for (const char *trial : {"n1", "n2"}) {
		std::vector<Neck74Unit> units = Neck74Trial(trial);
		const Neck74Result r = RunNeck74(units, false, 1600);
		MESSAGE("4a " << std::string(trial) << ": passed " << r.passed << ", round " << r.round << ", dropped " << r.dropped
		        << ", as live " << r.asLive << " of 4" << r.text);
		CHECK(r.allStill);
		CHECK(r.dropped == 0); // live 0 of 8
		passed += r.passed;
		round += r.round;
		asLive += r.asLive;
	}
	MESSAGE("4a: passed " << passed << ", round " << round << ", as live " << asLive << " of 8 (live 4 passed / 4 round)");
	CHECK(passed >= 2);
	CHECK(round >= 2);

	// The reconciliation with run 73 S: the same column with the way round closed (an idle unit on
	// the run 73 neck 52,42). No way at the first look -> the held mates drop, nobody goes round; only
	// the front, whose next tile is free, passes. Run 73 S measured the 2-unit case (rear dropped);
	// the 3rd / 4th of a sealed column are the same rule, not measured (CALIB_RUN73 "needs").
	std::vector<Neck74Unit> sealed = Neck74Trial("n1");
	const Neck74Result s = RunNeck74(sealed, false, 600, true);
	MESSAGE("4a n1 sealed: passed " << s.passed << ", round " << s.round << ", dropped " << s.dropped << s.text);
	CHECK(s.allStill);
	CHECK(s.round == 0);
	CHECK(s.passed == 1);
	CHECK(s.dropped == 3);
}

TEST_CASE("column 74 report: a folded group of 10 in open land, one click to a far goal - who never moves" * doctest::skip())
{
	// The S20+ 3.4.85 one-off (3 units of a 10-selection never moved, a 2-tile gap on the way): a
	// core unit closed in by mates of the same click, handled before them, finds no way at its first
	// look and drops (the run 73 S3 / run 72 B1 rule). Not measured live for a crowd core.
	for (const bool coreFirst : {true, false}) {
		CAnimations slow;
		CorridorWorld w;
		SlowFootman(w, slow);
		for (int y = 0; y < 128; ++y) {
			for (int x = 0; x < 128; ++x) {
				CMapField &f = *Map.Field(x, y);
				const tile_flags keep = f.Flags & (MapFieldBuilding | MapFieldLandUnit);
				const bool land = x >= 20 && x <= 100 && y >= 50 && y <= 75 && (x != 68 || y == 62 || y == 63);
				f.Flags = (land ? MapFieldLandAllowed : MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable) | keep;
			}
		}
		ClearGoalRegionMap();
		std::vector<Vec2i> starts = {{30, 61}, {29, 60}, {30, 60}, {31, 60}, {29, 61}, {31, 61}, {29, 62}, {30, 62}, {31, 62}, {32, 61}};
		if (!coreFirst) {
			std::rotate(starts.begin(), starts.begin() + 1, starts.end());
		}
		std::vector<CUnit *> units;
		for (const Vec2i &p : starts) {
			units.push_back(&w.Place(w.footman, p));
		}
		for (CUnit *u : units) {
			u->Orders.clear();
			u->Orders.push_back(COrder::NewActionMove(Vec2i(85 + u->tilePos.x - 30, 63 + u->tilePos.y - 61)));
			u->pathFinderData->output.Length = 0;
		}
		std::vector<int> steps(units.size(), 0);
		for (int cycle = 0; cycle != 2000; ++cycle) {
			for (size_t i = 0; i != units.size(); ++i) {
				CUnit &u = *units[i];
				if (u.CurrentAction() != UnitAction::Move) {
					continue;
				}
				const Vec2i at = u.tilePos;
				w.Cycle(u);
				--GameCycle;
				steps[i] += u.tilePos != at;
			}
			++GameCycle;
		}
		std::string text;
		int never = 0;
		for (size_t i = 0; i != units.size(); ++i) {
			never += steps[i] == 0;
			text += "\n " + std::to_string(starts[i].x) + "," + std::to_string(starts[i].y) + ": steps " + std::to_string(steps[i])
			        + ", end " + std::to_string(units[i]->tilePos.x) + "," + std::to_string(units[i]->tilePos.y);
		}
		MESSAGE("crowd, core handled " << (coreFirst ? "first" : "last") << ": never moved " << never << text);
	}
}

/// action_move.cpp, live run 76 B: a unit closed in only by mates of its own click waits for them.
extern bool MoveCoreWaitsForItsClick;

TEST_CASE("crowd 76: a 3 x 3 group in one click - the core walks out also when handled first; alone it drops")
{
	// Live run 76 B (work/agent-orig-76/ORIG76.md, CALIB_RUN76.md): 9 Footmen / Archers in a 3 x 3 block
	// in open land, one drag box and one right click to a goal 7-11 tiles off (group goals = target +
	// start offset). B1 / B2 / B3 / B5: the core walked out every time (4 of 4), no unit dropped; its
	// first step came 1 frame after the mate on its first tile stepped off (B5: the core, lowest index,
	// started in the same frame as that mate). B4: the core ordered alone, 8 idle round: dropped 3 frames
	// after its order, never moved (as run 72 B1). Third pass: the run 76 rule off (as before run 76) -
	// the core handled first finds its box closed at the first look and drops.
	for (int pass = 0; pass != 3; ++pass) {
		const bool coreFirst = pass != 1;
		MoveCoreWaitsForItsClick = pass != 2;
		CAnimations slow;
		CorridorWorld w;
		SlowFootman(w, slow);
		for (int y = 0; y < 128; ++y) {
			for (int x = 0; x < 128; ++x) {
				CMapField &f = *Map.Field(x, y);
				const tile_flags keep = f.Flags & (MapFieldBuilding | MapFieldLandUnit);
				const bool land = x >= 20 && x <= 60 && y >= 50 && y <= 72;
				f.Flags = (land ? MapFieldLandAllowed : MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable) | keep;
			}
		}
		ClearGoalRegionMap();
		const Vec2i c(30, 61);
		std::vector<Vec2i> starts = {c};
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (dx || dy) {
					starts.push_back(c + Vec2i(dx, dy));
				}
			}
		}
		if (!coreFirst) {
			std::rotate(starts.begin(), starts.begin() + 1, starts.end());
		}
		std::vector<CUnit *> units;
		std::vector<Vec2i> goals;
		for (const Vec2i &p : starts) {
			units.push_back(&w.Place(w.footman, p));
			goals.push_back(Vec2i(41, 61) + (p - c));
		}
		for (size_t i = 0; i != units.size(); ++i) {
			units[i]->Orders.clear();
			units[i]->Orders.push_back(COrder::NewActionMove(goals[i]));
			units[i]->pathFinderData->output.Length = 0;
		}
		const size_t core = coreFirst ? 0 : units.size() - 1;
		std::vector<int> steps(units.size(), 0), first(units.size(), -1);
		for (int cycle = 0; cycle != 1500; ++cycle) {
			for (size_t i = 0; i != units.size(); ++i) {
				CUnit &u = *units[i];
				if (u.CurrentAction() != UnitAction::Move) {
					continue;
				}
				const Vec2i at = u.tilePos;
				w.Cycle(u);
				--GameCycle;
				if (u.tilePos != at) {
					first[i] = first[i] < 0 ? cycle : first[i];
					++steps[i];
				}
			}
			++GameCycle;
		}
		std::string text;
		int never = 0;
		int arrived = 0;
		int mateFirst = 1 << 30;
		for (size_t i = 0; i != units.size(); ++i) {
			never += steps[i] == 0;
			arrived += Cheb(units[i]->tilePos, goals[i]) <= 1;
			if (i != core && first[i] >= 0) {
				mateFirst = std::min(mateFirst, first[i]);
			}
			text += "\n " + std::to_string(starts[i].x) + "," + std::to_string(starts[i].y) + ": first step "
			        + std::to_string(first[i]) + ", steps " + std::to_string(steps[i]) + ", end "
			        + std::to_string(units[i]->tilePos.x) + "," + std::to_string(units[i]->tilePos.y);
		}
		MESSAGE("crowd 76, core handled " << std::string(coreFirst ? "first" : "last") << ", rule "
		        << std::string(MoveCoreWaitsForItsClick ? "on" : "off") << ": never moved " << never << ", arrived "
		        << arrived << " of 9" << text);
		if (!MoveCoreWaitsForItsClick) {
			MoveCoreWaitsForItsClick = true;
			CHECK(steps[core] == 0); // ours before run 76 (the CALIB_RUN74 report case)
			continue;
		}
		CHECK(never == 0);
		CHECK(steps[core] > 0);
		CHECK(first[core] >= 0);
		CHECK(first[core] <= mateFirst + 2); // live: 1 frame after the first mate off its way
		CHECK(arrived >= 8);                 // live: 9 of 9 within 1-2 of their goals
	}

	// B4: the core alone, 8 idle round it: drops within MoveBoxedDropCycles, never moves.
	const BoxedResult b4 = RunIdleBox(true, 11, -1);
	MESSAGE("B4: searches " << b4.searches << ", end at " << b4.lastEnd << b4.text);
	CHECK(b4.moved == 0);
	CHECK(b4.boxedDrops == 1);
	CHECK(b4.lastEnd <= static_cast<int>(MoveBoxedDropCycles));
}

TEST_CASE("crowd 77: a 3 x 3 group whose north-west side is held by idle units - nobody drops, all walk")
{
	// Live run 77 C1-C3 (work/agent-orig-77/ORIG77.md, CALIB_RUN77.md): the run 76 block (core 39,42) with
	// 5 idle Peasants on 37,40 38,40 39,40 37,41 37,42 (beyond its north-west corner), one drag box and
	// one right click to 36,32 (north-west). 27 unit runs: no drop; the held units waited 19-64 frames
	// and walked, the core 34 / 3 / 3. Pass 2: the rule off (ours before run 76): units held there drop.
	for (int pass = 0; pass != 2; ++pass) {
		MoveCoreWaitsForItsClick = pass == 0;
		CAnimations slow;
		CorridorWorld w;
		SlowFootman(w, slow);
		for (int y = 0; y < 128; ++y) {
			for (int x = 0; x < 128; ++x) {
				CMapField &f = *Map.Field(x, y);
				const tile_flags keep = f.Flags & (MapFieldBuilding | MapFieldLandUnit);
				const bool land = x >= 15 && x <= 60 && y >= 40 && y <= 72;
				f.Flags = (land ? MapFieldLandAllowed : MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable) | keep;
			}
		}
		ClearGoalRegionMap();
		const Vec2i c(30, 61);
		std::vector<Vec2i> starts = {c};
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (dx || dy) {
					starts.push_back(c + Vec2i(dx, dy));
				}
			}
		}
		const Vec2i idle[5] = {c + Vec2i(-2, -2), c + Vec2i(-1, -2), c + Vec2i(0, -2), c + Vec2i(-2, -1), c + Vec2i(-2, 0)};
		std::vector<CUnit *> walls;
		for (const Vec2i &p : idle) {
			walls.push_back(&w.Place(w.footman, p));
		}
		std::vector<CUnit *> units;
		std::vector<Vec2i> goals;
		for (const Vec2i &p : starts) {
			units.push_back(&w.Place(w.footman, p));
			goals.push_back(c + Vec2i(-3, -10) + (p - c));
		}
		for (size_t i = 0; i != units.size(); ++i) {
			units[i]->Orders.clear();
			units[i]->Orders.push_back(COrder::NewActionMove(goals[i]));
			units[i]->pathFinderData->output.Length = 0;
		}
		std::vector<int> steps(units.size(), 0), first(units.size(), -1);
		for (int cycle = 0; cycle != 1500; ++cycle) {
			for (size_t i = 0; i != units.size(); ++i) {
				CUnit &u = *units[i];
				if (u.CurrentAction() != UnitAction::Move) {
					continue;
				}
				const Vec2i at = u.tilePos;
				w.Cycle(u);
				--GameCycle;
				if (u.tilePos != at) {
					first[i] = first[i] < 0 ? cycle : first[i];
					++steps[i];
				}
			}
			++GameCycle;
		}
		std::string text;
		int never = 0;
		int arrived = 0;
		int latest = 0;
		for (size_t i = 0; i != units.size(); ++i) {
			never += steps[i] == 0;
			arrived += Cheb(units[i]->tilePos, goals[i]) <= 1;
			latest = std::max(latest, first[i]);
			text += "\n " + std::to_string(starts[i].x) + "," + std::to_string(starts[i].y) + ": first step "
			        + std::to_string(first[i]) + ", steps " + std::to_string(steps[i]) + ", end "
			        + std::to_string(units[i]->tilePos.x) + "," + std::to_string(units[i]->tilePos.y);
		}
		int wallsMoved = 0;
		for (size_t i = 0; i != walls.size(); ++i) {
			wallsMoved += walls[i]->tilePos != idle[i];
		}
		MESSAGE("crowd 77, rule " << std::string(MoveCoreWaitsForItsClick ? "on" : "off") << ": never moved " << never
		        << ", arrived " << arrived << " of 9, latest first step " << latest << ", idle moved " << wallsMoved << text);
		CHECK(wallsMoved == 0);
		if (pass == 0) {
			CHECK(never == 0);                                // live: 27 of 27 walked
			CHECK(latest <= 64); // live: the longest wait 64 frames (CoreWaitsForItsClickCycles)
			CHECK(arrived >= 8);
		}
	}
	MoveCoreWaitsForItsClick = true;
}

TEST_CASE("column 74: the same column through a 2-tile neck - all pass, nobody round, short stands for the lane")
{
	for (const char *trial : {"w1", "w2"}) {
		std::vector<Neck74Unit> units = Neck74Trial(trial);
		const Neck74Result r = RunNeck74(units, true, 1200);
		MESSAGE("4b " << std::string(trial) << ": passed " << r.passed << ", round " << r.round << ", dropped " << r.dropped
		        << ", longest stand " << r.longestStand << r.text);
		CHECK(r.allStill);
		CHECK(r.passed == 4); // live 8 of 8 passed, 0 round, 0 dropped
		// live: the longest stay on a tile 31-32 frames (a step of 16 + a wait of 15-16)
		CHECK(r.longestStand <= 3 * 16);
	}
}
