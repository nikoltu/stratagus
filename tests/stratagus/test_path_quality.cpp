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
/**@name test_path_quality.cpp - The length of the A* way against the shortest way. */
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

#include "actions.h"
#include "map.h"
#include "pathfinder.h"
#include "player.h"
#include "tileset.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <string>
#include <tuple>
#include <vector>

// Owner report 2026-10-21: "Pathfinding is terribly non-optimal". These cases build walls and
// forest corridors on a 128 x 128 map and measure the way the A* of the engine finds (one search,
// and a walk tile by tile through NextPathElement with its 28-step cache) against the true shortest
// way (a breadth-first search over the 8 neighbours: every step, straight or diagonal, costs the
// same in the engine and in the game). The ground has the move cost of the game (1 << speed 0 = 1),
// so a step costs 2 in the search, as on the device. Settings as the Wargus Lua sets them:
// AStar("fixed-unit-cost", 1000, "moving-unit-cost", 20, "know-unseen-terrain", ...).

extern int AStarFindPath(const Vec2i &startPos, const Vec2i &goalPos, int gw, int gh,
                         int tilesizex, int tilesizey, int minrange, int maxrange,
                         char *path, int pathlen, const CUnit &unit, bool squareRange);

namespace
{
constexpr tile_flags QualityMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding | MapFieldCoastAllowed
                                   | MapFieldWaterAllowed | MapFieldUnpassable | MapFieldForest
                                   | MapFieldRocks | MapFieldWall;
constexpr int Size = 128;

class QualityWorld
{
public:
	explicit QualityWorld(int size = Size) : n(size)
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		if (UnitManager == nullptr) {
			UnitManager = new CUnitManager;
		}
		UnitManager->Init();
		UnitTypeVar.Init();
		Map.Info.MapWidth = n;
		Map.Info.MapHeight = n;
		Map.Create();
		InitPathfinder();
		savedKnow = AStarKnowUnseenTerrain;
		savedMoving = AStarMovingUnitCrossingCost;
		AStarKnowUnseenTerrain = true;
		AStarMovingUnitCrossingCost = 20;
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
		// one ground tile of speed 0: move cost 1, as every land tile of the Wargus tilesets
		tileset.tiles.resize(2);
		tileset.tiles[1].tile = 1;
		tileset.tiles[1].flag = MapFieldLandAllowed;
		for (int y = 0; y < n; ++y) {
			for (int x = 0; x < n; ++x) {
				Map.Field(x, y)->setTileIndex(tileset, 1, 0, 0);
			}
		}
		footman.Ident = "unit-test-footman";
		footman.Name = footman.Ident;
		footman.Slot = 0;
		footman.TileWidth = 1;
		footman.TileHeight = 1;
		footman.GridStep = 1;
		footman.NumDirections = 8;
		footman.MoveType = EMovement::Land;
		footman.MovementMask = QualityMask;
		footman.FieldFlags = MapFieldLandUnit;
		footman.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		footman.DefaultStat.Variables.resize(UnitTypeVar.GetNumberVariable());
		footman.DefaultStat.Variables[HP_INDEX].Max = 60;
		footman.DefaultStat.Variables[HP_INDEX].Value = 60;
		footman.DefaultStat.Variables[HP_INDEX].Enable = 1;
		footman.MapDefaultStat = footman.DefaultStat;
		for (int i = 0; i < PlayerMax; ++i) {
			footman.Stats[i] = footman.DefaultStat;
		}
		walker.Player = &Players[0];
		walker.Type = &footman;
	}

	~QualityWorld()
	{
		walker.Orders.clear();
		for (int i = 0; i < 2; ++i) {
			Players[i].Clear();
		}
		UnitManager->Init();
		AStarKnowUnseenTerrain = savedKnow;
		AStarMovingUnitCrossingCost = savedMoving;
		FreePathfinder();
		Map.Fields.clear();
		Map.Info.MapWidth = 0;
		Map.Info.MapHeight = 0;
		NumPlayers = savedNumPlayers;
		ThisPlayer = savedThisPlayer;
	}

	void Block(int x0, int y0, int x1, int y1, tile_flags flag = MapFieldUnpassable)
	{
		for (int y = y0; y <= y1; ++y) {
			for (int x = x0; x <= x1; ++x) {
				Map.Field(x, y)->setFlag(flag);
			}
		}
	}
	void Open(int x0, int y0, int x1, int y1)
	{
		for (int y = y0; y <= y1; ++y) {
			for (int x = x0; x <= x1; ++x) {
				Map.Field(x, y)->resetFlag(MapFieldUnpassable | MapFieldForest);
			}
		}
	}
	static bool Free(int x, int y)
	{
		return x >= 0 && y >= 0 && x < Map.Info.MapWidth && y < Map.Info.MapHeight && !Map.Field(x, y)->CheckMask(QualityMask);
	}

	/// The true shortest way in steps (-1: none).
	static int Shortest(const Vec2i &from, const Vec2i &to)
	{
		const int w = Map.Info.MapWidth;
		std::vector<int> dist(w * Map.Info.MapHeight, -1);
		std::deque<Vec2i> queue;
		dist[from.y * w + from.x] = 0;
		queue.push_back(from);
		while (!queue.empty()) {
			const Vec2i p = queue.front();
			queue.pop_front();
			if (p == to) {
				return dist[p.y * w + p.x];
			}
			for (int i = 0; i != 8; ++i) {
				const int x = p.x + Heading2X[i];
				const int y = p.y + Heading2Y[i];
				if (Free(x, y) && dist[y * w + x] < 0) {
					dist[y * w + x] = dist[p.y * w + p.x] + 1;
					queue.emplace_back(x, y);
				}
			}
		}
		return -1;
	}

	CUnit &Place(const Vec2i &pos)
	{
		CUnit *unit = MakeUnitAndPlace(pos, footman, &Players[0]);
		REQUIRE(unit != nullptr);
		return *unit;
	}

	int n;
	CTileset tileset;
	CUnitType footman;
	CUnit walker;

private:
	CPlayer *savedThisPlayer = nullptr;
	int savedNumPlayers = 0;
	bool savedKnow = false;
	int savedMoving = 0;
};

/// One search, the whole way kept.
struct Search {
	int length = 0;      /// steps of the way, or a PF_ code
	bool toGoal = false; /// the way ends on the goal (false: the search stopped early)
	double micros = 0;   /// time of one search on this host
};

Search SearchOnce(QualityWorld &w, const Vec2i &start, const Vec2i &goal)
{
	static char path[4096];
	Search s;
	w.walker.tilePos = start;
	const auto t0 = std::chrono::steady_clock::now();
	constexpr int Repeats = 5;
	for (int i = 0; i != Repeats; ++i) {
		s.length = AStarFindPath(start, goal, 0, 0, 1, 1, 0, 0, path, sizeof(path), w.walker, false);
	}
	const auto t1 = std::chrono::steady_clock::now();
	s.micros = std::chrono::duration<double, std::micro>(t1 - t0).count() / Repeats;
	if (s.length > 0) {
		Vec2i p = start;
		for (int i = 0; i != s.length; ++i) {
			// the first step is stored last
			const int element = path[s.length - 1 - i];
			p.x += Heading2X[PathElementHeading(element)];
			p.y += Heading2Y[PathElementHeading(element)];
		}
		s.toGoal = p == goal;
	}
	return s;
}

/// A walk tile by tile through NextPathElement, the way a move order takes it.
struct Walk {
	int steps = 0;
	int searches = 0;
	bool reached = false;
};

Walk WalkOnce(QualityWorld &w, const Vec2i &start, const Vec2i &goal)
{
	Walk walk;
	CUnit &u = w.walker;
	u.tilePos = start;
	u.Orders.clear();
	u.Orders.push_back(COrder::NewActionMove(goal));
	u.pathFinderData->output = PathFinderOutput();
	u.pathFinderData->input.SetUnit(u);
	int before = 0;
	for (int i = 0; i != 3000; ++i) {
		const auto [d, dir] = NextPathElement(u);
		if (d == PF_REACHED) {
			walk.reached = true;
			break;
		}
		if (d < 0 || d == PF_WAIT) {
			break;
		}
		if (u.pathFinderData->output.Length >= before) {
			++walk.searches;
		}
		before = u.pathFinderData->output.Length;
		u.tilePos += dir;
		++walk.steps;
	}
	u.Orders.clear();
	return walk;
}

struct Scene {
	const char *name;
	Vec2i start;
	Vec2i goal;
	void (*build)(QualityWorld &);
};

void BuildOpen(QualityWorld &) {}

/// A wall across the whole map with one gap far north of the line start - goal.
void BuildWallGap(QualityWorld &w)
{
	w.Block(64, 0, 65, Size - 1);
	w.Open(64, 8, 65, 9);
}

/// A cup open toward the start: the straight line runs into it.
void BuildCup(QualityWorld &w)
{
	w.Block(70, 40, 71, 88);  // bottom of the cup
	w.Block(46, 40, 71, 41);  // north arm
	w.Block(46, 87, 71, 88);  // south arm
}

/// A forest band with a near gap (20 tiles off the line) and a far one.
void BuildForestBand(QualityWorld &w)
{
	w.Block(60, 0, 67, Size - 1, MapFieldForest);
	w.Open(60, 40, 67, 41);
	w.Open(60, 110, 67, 111);
}

/// Forest rows with the gap at alternate ends: a long serpentine.
void BuildSerpentine(QualityWorld &w)
{
	for (int i = 0; i < 5; ++i) {
		const int y = 20 + i * 20;
		w.Block(0, y, Size - 1, y + 1, MapFieldForest);
		if (i % 2 == 0) {
			w.Open(Size - 4, y, Size - 1, y + 1);
		} else {
			w.Open(0, y, 3, y + 1);
		}
	}
}

/// Two forest blocks with a 1-tile corridor between them on the straight line, and an open way
/// round them.
void BuildCorridor(QualityWorld &w)
{
	w.Block(50, 30, 80, 63, MapFieldForest);
	w.Block(50, 65, 80, 98, MapFieldForest);
}

const Scene Scenes[] = {
	{"open diagonal", {10, 10}, {100, 60}, BuildOpen},
	{"wall, gap far north", {40, 100}, {90, 100}, BuildWallGap},
	{"cup facing the start", {30, 64}, {100, 64}, BuildCup},
	{"forest band, near gap", {40, 60}, {90, 60}, BuildForestBand},
	{"serpentine forest", {64, 10}, {64, 110}, BuildSerpentine},
	{"1-tile corridor", {40, 64}, {90, 64}, BuildCorridor},
};

std::string Row(const Scene &scene, int shortest, const Search &s, const Walk &walk)
{
	char text[256];
	std::snprintf(text, sizeof(text),
	              "%-24s shortest %4d | search %4d (%s, +%d%%, %.0f us) | walk %4d steps, %d searches%s",
	              scene.name, shortest, s.length, s.toGoal ? "to goal" : "STOPPED EARLY",
	              shortest > 0 && s.length > 0 ? (s.length - shortest) * 100 / shortest : 0, s.micros,
	              walk.steps, walk.searches, walk.reached ? "" : " NOT REACHED");
	return text;
}
} // namespace

TEST_CASE("path quality: A* way against the shortest way")
{
	for (const Scene &scene : Scenes) {
		QualityWorld w;
		scene.build(w);
		const int shortest = QualityWorld::Shortest(scene.start, scene.goal);
		REQUIRE(shortest > 0);
		const Search s = SearchOnce(w, scene.start, scene.goal);
		const Walk walk = WalkOnce(w, scene.start, scene.goal);
		MESSAGE(Row(scene, shortest, s, walk));
		CAPTURE(scene.name);
		CHECK(s.toGoal);
		CHECK(walk.reached);
		// the found way is at most 10 % longer than the shortest
		CHECK(s.length * 10 <= shortest * 11);
		CHECK(walk.steps * 10 <= shortest * 11);
	}
}

TEST_CASE("path quality: own units in a 1-tile corridor")
{
	// The corridor (40,64)-(90,64) through forest, the way round is long. A unit of the same
	// player stands in the corridor: standing (Moving 0, also a group mate waiting in the queue
	// between two steps) it is a wall to the search; moving it is crossed at cost 20.
	const Vec2i start(40, 64);
	const Vec2i goal(90, 64);
	for (int moving = 0; moving != 2; ++moving) {
		QualityWorld w;
		BuildCorridor(w);
		CUnit &mate = w.Place({65, 64});
		mate.Moving = moving;
		const int shortest = QualityWorld::Shortest(start, goal); // the mate is a wall here
		const Search s = SearchOnce(w, start, goal);
		char text[160];
		std::snprintf(text, sizeof(text), "mate %s: search %d steps (corridor 50, way round %d)",
		              moving ? "moving" : "standing", s.length, shortest);
		MESSAGE(text);
		if (moving) {
			CHECK(s.length == 50);
		} else {
			CHECK(s.length == shortest);
		}
	}
}

// C5 (work/PATHFINDING_2026-10-21.md): before the fix 3 of 19 elements were lost after 3 waits.
// Held together with a working "WAIT expired" search (holding the step alone left the 4th ship of
// "folded ships" waiting for ever).
TEST_CASE("path quality: a blocked step is not dropped from the cached way")
{
	// A crossing unit stands on the next tile for three calls: the step is not taken, so the
	// cached way must still begin with it when the tile is free again.
	QualityWorld w;
	CUnit &u = w.walker;
	u.tilePos = {10, 64};
	const Vec2i goal(30, 54);
	u.Orders.push_back(COrder::NewActionMove(goal));
	u.pathFinderData->output = PathFinderOutput();
	u.pathFinderData->input.SetUnit(u);
	auto [d, dir] = NextPathElement(u);
	REQUIRE(d > 0);
	u.tilePos += dir;
	// the next tile on the way
	const PathFinderOutput &out = u.pathFinderData->output;
	REQUIRE(out.Length >= 2);
	const int nextElement = out.Path[out.Length - 2];
	const Vec2i next = u.tilePos + Vec2i(Heading2X[PathElementHeading(nextElement)], Heading2Y[PathElementHeading(nextElement)]);
	CUnit &crosser = w.Place(next);
	crosser.Moving = 1;
	const int lengthBefore = out.Length - 1;
	for (int i = 0; i != 3; ++i) {
		std::tie(d, dir) = NextPathElement(u);
		CHECK(d == PF_WAIT);
	}
	crosser.Remove(nullptr);
	std::tie(d, dir) = NextPathElement(u);
	REQUIRE(d > 0);
	char text[160];
	std::snprintf(text, sizeof(text), "after 3 waits: cached %d elements (before %d), step taken (%d,%d), wanted (%d,%d)",
	              int(out.Length), lengthBefore, dir.x, dir.y, next.x - u.tilePos.x, next.y - u.tilePos.y);
	MESSAGE(text);
	CHECK(u.tilePos + dir == next);
	CHECK(int(out.Length) == lengthBefore);
	u.Orders.clear();
}

TEST_CASE("path quality: the wait for a blocked step expires and the way is searched again")
{
	// A unit stands on the next tile and stays: the first blocked call starts the wait, each further
	// one adds the wait the call before stood for (PathWaitDefaultCycles when the caller sets none),
	// and the call that finds PathWaitExpiryCycles waited searches round the unit (C5: that search
	// was dead; live run 67: about 1 s = 38 cycles). 10 + 10 + 10 + 8 cycles: 4 waits.
	QualityWorld w;
	CUnit &u = w.walker;
	u.tilePos = {10, 64};
	const Vec2i goal(30, 64);
	u.Orders.push_back(COrder::NewActionMove(goal));
	u.pathFinderData->output = PathFinderOutput();
	u.pathFinderData->input.SetUnit(u);
	auto [d, dir] = NextPathElement(u);
	REQUIRE(d > 0);
	u.tilePos += dir;
	const PathFinderOutput &out = u.pathFinderData->output;
	REQUIRE(out.Length >= 2);
	const int nextElement = out.Path[out.Length - 2];
	const Vec2i next = u.tilePos + Vec2i(Heading2X[PathElementHeading(nextElement)], Heading2Y[PathElementHeading(nextElement)]);
	CUnit &stander = w.Place(next);
	stander.Moving = 0;
	int waits = 0;
	for (int i = 0; i != 30; ++i) {
		std::tie(d, dir) = NextPathElement(u);
		if (d != PF_WAIT) {
			break;
		}
		++waits;
	}
	CHECK(waits == (PathWaitExpiryCycles + PathWaitDefaultCycles - 1) / PathWaitDefaultCycles);
	REQUIRE(d > 0);
	CHECK(u.tilePos + dir != next);
	CHECK(UnitCanBeAt(u, u.tilePos + dir));
	stander.Remove(nullptr);
	u.Orders.clear();
}

TEST_CASE("path quality: search budget")
{
	// The nodes a search needs on the scenes, and the way the walk takes with budgets from the old
	// fixed 5120 up to no limit. 0 is the default: twice the tiles of the map.
	const int saved = AStarMaxSearchIterations;
	for (const Scene &scene : Scenes) {
		for (int budget : {1024 * 5, Size * Size, 0, 1 << 30}) {
			QualityWorld w;
			scene.build(w);
			AStarMaxSearchIterations = budget;
			const int shortest = QualityWorld::Shortest(scene.start, scene.goal);
			const Search s = SearchOnce(w, scene.start, scene.goal);
			const int used = AStarLastSearchIterations;
			const Walk walk = WalkOnce(w, scene.start, scene.goal);
			char text[320];
			std::snprintf(text, sizeof(text), "budget %10d: %s | nodes %d", budget,
			              Row(scene, shortest, s, walk).c_str(), used);
			MESSAGE(text);
			if (budget == 0) {
				CAPTURE(scene.name);
				CHECK(s.toGoal);
				CHECK(walk.reached);
			}
		}
	}
	AStarMaxSearchIterations = saved;
}

// Search cost 2026-10-21 (work/PATHFINDING_2026-10-21.md): with the budget of the map a search for
// a goal that cannot be reached explores the whole area the unit stands in before it fails. Timed
// here on 128 and 256 maps: one search, 20 units in one cycle, and the Range++ retry of COrder_Move
// (PF_UNREACHABLE -> Range + 1 -> a new search the next cycle, until the goal area touches the area
// of the unit).
namespace
{
/// A forest ring round the goal, 3 x 3 open inside.
void Pocket(QualityWorld &w, const Vec2i &goal)
{
	w.Block(goal.x - 2, goal.y - 2, goal.x + 2, goal.y + 2, MapFieldForest);
	w.Open(goal.x - 1, goal.y - 1, goal.x + 1, goal.y + 1);
}

void CostOpen(QualityWorld &w, Vec2i &goal)
{
	goal = Vec2i(w.n - 10, w.n - 10);
	Pocket(w, goal);
}

void CostMaze(QualityWorld &w, Vec2i &goal)
{
	for (int y = 8, i = 0; y + 1 < w.n - 8; y += 16, ++i) {
		w.Block(0, y, w.n - 1, y + 1, MapFieldForest);
		if (i % 2 == 0) {
			w.Open(w.n - 4, y, w.n - 1, y + 1);
		} else {
			w.Open(0, y, 3, y + 1);
		}
	}
	goal = Vec2i(w.n / 2, w.n - 5);
	Pocket(w, goal);
}

/// Sea everywhere but the west half (the units) and an island east of a 16-tile channel.
void CostIsland(QualityWorld &w, Vec2i &goal)
{
	for (int y = 0; y < w.n; ++y) {
		for (int x = w.n / 2; x < w.n; ++x) {
			if (x < w.n / 2 + 16) {
				Map.Field(x, y)->setFlag(MapFieldWaterAllowed);
			}
		}
	}
	goal = Vec2i(w.n / 2 + 24, w.n / 2);
}

struct CostScene {
	const char *name;
	void (*build)(QualityWorld &, Vec2i &goal);
};

const CostScene CostScenes[] = {
	{"open land, goal in a forest pocket", CostOpen},
	{"forest maze, goal in a pocket", CostMaze},
	{"goal on another island", CostIsland},
};

int SearchTo(QualityWorld &w, const Vec2i &start, const Vec2i &goal, int maxrange)
{
	static char path[4096];
	w.walker.tilePos = start;
	return AStarFindPath(start, goal, 0, 0, 1, 1, 0, maxrange, path, sizeof(path), w.walker, false);
}

double Millis(std::chrono::steady_clock::time_point t0)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
} // namespace

TEST_CASE("path quality: search cost of an unreachable goal")
{
	for (int n : {128, 256}) {
		for (const CostScene &scene : CostScenes) {
			QualityWorld w(n);
			Vec2i goal;
			scene.build(w, goal);
			const Vec2i start(5, 3);
			// (a) one search
			constexpr int Repeats = 3;
			int ret = 0;
			auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i != Repeats; ++i) {
				ret = SearchTo(w, start, goal, 0);
			}
			const double one = Millis(t0) / Repeats;
			const int nodes = AStarLastSearchIterations;
			CHECK(ret == PF_UNREACHABLE);
			// (b) 20 units searching in the same cycle
			t0 = std::chrono::steady_clock::now();
			for (int i = 0; i != 20; ++i) {
				CHECK(SearchTo(w, Vec2i(5 + 2 * (i % 5), 1 + 2 * (i / 5)), goal, 0) == PF_UNREACHABLE);
			}
			const double twenty = Millis(t0);
			// (c) the Range++ retry of one unit: one search a cycle until the goal area is reached
			int retries = 0;
			double worst = 0;
			t0 = std::chrono::steady_clock::now();
			for (int range = 0; range != 400; ++range) {
				const auto s0 = std::chrono::steady_clock::now();
				ret = SearchTo(w, start, goal, range);
				worst = std::max(worst, Millis(s0));
				++retries;
				if (ret != PF_UNREACHABLE) {
					break;
				}
			}
			const double loop = Millis(t0);
			CHECK(ret > 0);
			// one search right after a change of the map far away (a tree on the corner): the
			// regions are rebuilt before the answer
			Map.Field(n - 1, 0)->setFlag(MapFieldForest);
			t0 = std::chrono::steady_clock::now();
			CHECK(SearchTo(w, start, goal, 0) == PF_UNREACHABLE);
			const double rebuilt = Millis(t0);
			Map.Field(n - 1, 0)->resetFlag(MapFieldForest);
			WARN(twenty < 8.0);
			char text[320];
			std::snprintf(text, sizeof(text),
			              "%dx%d %-36s | one %.3f ms (%d nodes) | 20 units %.3f ms | Range++ %d searches %.2f ms (worst %.2f ms), way %d | after a map change %.3f ms",
			              n, n, scene.name, one, nodes, twenty, retries, loop, worst, ret, rebuilt);
			MESSAGE(text);
		}
	}
}

TEST_CASE("path quality: re-search rate of a unit held by a crossing unit")
{
	// 1-tile corridor, the way round is 2x as long: a moving unit that stays on the next tile is
	// crossed at a cost, so every new way still goes through it. Under C5 the wait expires when
	// PathWaitExpiryCycles (38) have been waited and the way is searched again; COrder_Move sets a
	// CrossingUnitWait (15) per call, cut to what is left of the expiry (15 + 15 + 8).
	QualityWorld w;
	BuildCorridor(w);
	CUnit &u = w.walker;
	u.tilePos = {55, 64};
	const Vec2i goal(90, 64);
	u.Orders.push_back(COrder::NewActionMove(goal));
	u.pathFinderData->output = PathFinderOutput();
	u.pathFinderData->input.SetUnit(u);
	auto [d, dir] = NextPathElement(u);
	REQUIRE(d > 0);
	u.tilePos += dir;
	CUnit &crosser = w.Place(u.tilePos + Vec2i(1, 0));
	crosser.Moving = 1;
	const int callsBefore = AStarSearchCalls;
	constexpr int Calls = 110;
	int waits = 0;
	int cycles = 0;
	const auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i != Calls; ++i) {
		std::tie(d, dir) = NextPathElement(u);
		waits += d == PF_WAIT;
		if (d == PF_WAIT) {
			SetPathWait(u, 15);
			cycles += u.Wait;
		}
	}
	const double ms = Millis(t0);
	const int searches = AStarSearchCalls - callsBefore;
	char text[200];
	std::snprintf(text, sizeof(text), "%d blocked calls: %d waits, %d cycles, %d searches (%.2f ms in all) -> %.2f searches/s at 38.5 cycles/s",
	              Calls, waits, cycles, searches, ms, searches * 38.5 / cycles);
	MESSAGE(text);
	CHECK(waits == Calls);
	// one search each PathWaitExpiryCycles waited (the wait set by the last call is not over yet)
	CHECK(searches == (cycles - 1) / PathWaitExpiryCycles);
	crosser.Remove(nullptr);
	u.Orders.clear();
}
