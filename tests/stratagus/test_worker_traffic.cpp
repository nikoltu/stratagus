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
/**@name test_worker_traffic.cpp - Workers going in and out of a gold mine and a hall for thousands of cycles. */
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
#include "action/action_resource.h"
#include "actions.h"
#include "ai.h"
#include "commands.h"
#include "animation.h"
#include "animation/animation_move.h"
#include "animation/animation_wait.h"
#include "map.h"
#include "pathfinder.h"
#include "player.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"

#include "../../src/ai/ai_local.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Round 3.4.76 (work/c1127/ROUND_3476d_EMU.md): after loading emu3473 one Peasant stood > 60 s with a
// harvest order. 3.4.76 changed the pathfinder (work/PATHFINDING_2026-10-21.md C1-C5: search budget,
// open set, node cost, tie-break, the held step and the real WAIT expiry). These cases run the whole
// harvest (COrder_Resource::Execute, as HandleUnitAction calls it) with several workers between a
// mine and a hall, through open ground and through narrow approaches, for thousands of cycles: every
// worker must keep delivering and none may stand long on one tile outside the buildings.
// Report: work/WORKER_STUCK_3476.md.

namespace
{
constexpr tile_flags TrafficLandMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding
                                       | MapFieldCoastAllowed | MapFieldWaterAllowed | MapFieldUnpassable;

struct WorkerStats
{
	int trips = 0;          /// times it went into the hall
	int longestStall = 0;   /// longest run of cycles on one tile outside the buildings
	bool lostOrder = false; /// the harvest order ended
	Vec2i last{-1, -1};
	int run = 0;
	bool wasInHall = false;
};

/// A synthetic map with real units and the real harvest action (no Lua, no graphics): the move
/// animation walks a tile per cycle; mining and storing wait the Wargus 150 cycles each.
class TrafficWorld
{
public:
	explicit TrafficWorld(int size = 64)
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		savedGameCycle = GameCycle;
		savedKnow = AStarKnowUnseenTerrain;
		savedMoving = AStarMovingUnitCrossingCost;
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
		AStarMovingUnitCrossingCost = 20;
		for (int i : {0, 1, int(PlayerNumNeutral)}) {
			Players[i].Clear();
			Players[i].Index = i;
			Players[i].Type = i == PlayerNumNeutral ? PlayerTypes::PlayerNeutral : PlayerTypes::PlayerPerson;
			Players[i].Team = i;
			Players[i].UnitLimit = 100;
			Players[i].BuildingLimit = 100;
			Players[i].TotalUnitLimit = 100;
		}
		Players[0].Incomes[GoldCost] = 100;
		NumPlayers = 2;
		ThisPlayer = &Players[0];
		GameCycle = 1000;
		Fill(0, 0, size - 1, size - 1, MapFieldLandAllowed);
		// explored by the workers' player (1 = explored, as the sight counters keep it)
		for (int y = 0; y < size; ++y) {
			for (int x = 0; x < size; ++x) {
				Map.Field(x, y)->playerInfo.Visible[0] = 1;
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

		MakeType(peasant, "unit-test-peasant", 0, MapFieldLandUnit, 1);
		auto gold = std::make_unique<ResourceInfo>();
		gold->ResourceId = GoldCost;
		gold->FinalResource = GoldCost;
		gold->ResourceCapacity = 100;
		gold->WaitAtResource = 150;
		gold->WaitAtDepot = 150;
		peasant.ResInfo[GoldCost] = std::move(gold);
		peasant.BoolFlag[HARVESTER_INDEX].value = true;

		MakeType(mineType, "unit-test-gold-mine", 1, MapFieldBuilding, 3);
		mineType.Building = true;
		mineType.Animations = nullptr;
		mineType.GivesResource = GoldCost;
		mineType.BoolFlag[CANHARVEST_INDEX].value = true;

		MakeType(hallType, "unit-test-hall", 2, MapFieldBuilding, 4);
		hallType.Building = true;
		hallType.Animations = nullptr;
		hallType.CanStore[GoldCost] = 1;
	}

	~TrafficWorld()
	{
		for (CUnit *w : workers) {
			w->Orders.clear();
		}
		for (int i : {0, 1, int(PlayerNumNeutral)}) {
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
		GameCycle = savedGameCycle;
	}

	void Fill(int x0, int y0, int x1, int y1, tile_flags flags)
	{
		for (int y = y0; y <= y1; ++y) {
			for (int x = x0; x <= x1; ++x) {
				Map.Field(x, y)->Flags = flags;
			}
		}
	}
	void Forest(int x0, int y0, int x1, int y1)
	{
		Fill(x0, y0, x1, y1, MapFieldLandAllowed | MapFieldForest | MapFieldUnpassable);
	}
	void Ground(int x0, int y0, int x1, int y1) { Fill(x0, y0, x1, y1, MapFieldLandAllowed); }

	CUnit &Place(const CUnitType &type, const Vec2i &pos, int player = 0)
	{
		CUnit *unit = MakeUnitAndPlace(pos, type, &Players[player]);
		REQUIRE(unit != nullptr);
		return *unit;
	}

	void Buildings(const Vec2i &minePos, const Vec2i &hallPos)
	{
		mine = &Place(mineType, minePos, PlayerNumNeutral);
		mine->ResourcesHeld = 600000;
		hall = &Place(hallType, hallPos, 0);
	}

	CUnit &Worker(const Vec2i &pos)
	{
		CUnit &w = Place(peasant, pos, 0);
		w.Orders.clear();
		w.Orders.push_back(COrder::NewActionResource(w, *mine));
		w.pathFinderData->output.Length = 0;
		w.Wait = int(workers.size()) * stagger; // starts apart: no lockstep, mates meet head-on
		workers.push_back(&w);
		stats.emplace_back();
		return w;
	}

	/// HandleUnitAction for every worker, in creation order, `cycles` times.
	void Run(int cycles)
	{
		for (int i = 0; i != cycles; ++i) {
			++GameCycle;
			// visibility has no fog here: the mine and the hall are seen by the workers' player
			mine->VisCount[0] = 1;
			hall->VisCount[0] = 1;
			for (size_t k = 0; k != workers.size(); ++k) {
				CUnit &w = *workers[k];
				WorkerStats &s = stats[k];
				if (w.Orders[0]->Finished && w.Orders[0]->Action != UnitAction::Still) {
					w.Orders[0] = COrder::NewActionStill();
					s.lostOrder = true;
				}
				if (w.CurrentAction() != UnitAction::Still) {
					w.Orders[0]->Execute(w);
					if (oldStepDrop && !w.Removed && w.Wait == 10 && w.Moving == 0
					    && w.CurrentAction() == UnitAction::Resource) {
						// 3.4.75 NextPathElement on a blocked step (DoActionMove set Wait 10 for its
						// PF_WAIT): the step was dropped, not kept, and the wait never expired into a
						// new search. 3.4.76 stores the step twice and counts Fast down: take the copy
						// away and keep the counter up.
						PathFinderOutput &out = w.pathFinderData->output;
						if (out.Length > 0) {
							out.Length--;
						}
						out.Fast = PathFinderOutput::MAX_FAST;
						out.Waited = 0; // live run 67 expiry counts waited cycles: never let it run out
					}
				}
				if (lostAt.size() < workers.size()) {
					lostAt.resize(workers.size(), Vec2i(-1, -1));
				}
				if (s.lostOrder && lostAt[k].x < 0) {
					lostAt[k] = w.tilePos;
				}
				const bool inHall = w.Removed && w.Container == hall;
				if (inHall && !s.wasInHall) {
					++s.trips;
				}
				s.wasInHall = inHall;
				if (w.Removed) {
					s.run = 0;
					s.last = Vec2i(-1, -1);
				} else if (w.tilePos == s.last) {
					s.longestStall = std::max(s.longestStall, ++s.run);
				} else {
					s.run = 0;
					s.last = w.tilePos;
				}
			}
			if (sample) {
				sample(*this);
			}
		}
	}

	/// The move animation of the live client: 16 cycles a step (live run 68: step 16 frames,
	/// 0.41 s), not one tile a cycle. Before the workers are placed.
	void SlowSteps()
	{
		animations.Move.clear();
		auto move = std::make_unique<CAnimation_Move>();
		move->Init("2", nullptr);
		auto wait = std::make_unique<CAnimation_Wait>();
		wait->Init("1", nullptr);
		animations.Move.push_back(std::move(move));
		animations.Move.push_back(std::move(wait));
	}

	int FewestTrips() const
	{
		int fewest = 1 << 30;
		for (const WorkerStats &s : stats) {
			fewest = std::min(fewest, s.trips);
		}
		return fewest;
	}
	int LongestStall() const
	{
		int longest = 0;
		for (const WorkerStats &s : stats) {
			longest = std::max(longest, s.longestStall);
		}
		return longest;
	}
	bool AnyLostOrder() const
	{
		return std::any_of(stats.begin(), stats.end(), [](const WorkerStats &s) { return s.lostOrder; });
	}
	std::string Summary() const
	{
		std::string res;
		for (size_t k = 0; k != stats.size(); ++k) {
			const CUnit &w = *workers[k];
			res += "w" + std::to_string(k) + ": trips " + std::to_string(stats[k].trips) + ", stall "
			       + std::to_string(stats[k].longestStall) + (stats[k].lostOrder ? ", ORDER LOST" : "") + ", at ("
			       + std::to_string(w.tilePos.x) + "," + std::to_string(w.tilePos.y) + ")"
			       + (w.Removed ? " inside" : "") + "; ";
		}
		return res;
	}

	CUnitType peasant;
	CUnitType mineType;
	CUnitType hallType;
	CAnimations animations;
	CUnit *mine = nullptr;
	CUnit *hall = nullptr;
	int stagger = 0; /// cycles between the starts of two workers
	bool oldStepDrop = false; /// emulate the 3.4.75 blocked-step handling (see Run)
	std::vector<CUnit *> workers;
	std::vector<WorkerStats> stats;
	std::vector<Vec2i> lostAt; /// where each worker's order ended ((-1,-1): not lost)
	std::function<void(TrafficWorld &)> sample; /// called after each cycle

private:
	void MakeType(CUnitType &type, const char *ident, int slot, tile_flags fieldFlags, int size)
	{
		type.Ident = ident;
		type.Name = ident;
		type.Slot = slot;
		type.TileWidth = size;
		type.TileHeight = size;
		type.GridStep = 1;
		type.NumDirections = 8;
		type.MoveType = EMovement::Land;
		type.MovementMask = TrafficLandMask;
		type.FieldFlags = fieldFlags;
		type.Animations = &animations;
		type.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		type.DefaultStat.Variables.resize(UnitTypeVar.GetNumberVariable());
		type.DefaultStat.Variables[HP_INDEX].Max = 60;
		type.DefaultStat.Variables[HP_INDEX].Value = 60;
		type.DefaultStat.Variables[HP_INDEX].Enable = 1;
		type.DefaultStat.Variables[GIVERESOURCE_INDEX].Max = 100000;
		type.MapDefaultStat = type.DefaultStat;
		for (int i = 0; i < PlayerMax; ++i) {
			type.Stats[i] = type.DefaultStat;
		}
	}

	CPlayer *savedThisPlayer = nullptr;
	int savedNumPlayers = 0;
	unsigned long savedGameCycle = 0;
	bool savedKnow = false;
	int savedMoving = 0;
};

/// A trip here: about 2 x 150 cycles inside plus the walk. 4000 cycles give 10+ to a lone worker.
constexpr int Cycles = 4000;
/// Longest stand on one tile outside the buildings that is not a stuck worker: the stand at the exit
/// plus a few 10-cycle waits behind a mate and the 110-165 cycles of the C5 expiry (a start delay of
/// `stagger` x index counts too).
constexpr int StallLimit = 400;

int Chebyshev(const Vec2i &a, const Vec2i &b)
{
	return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

enum class Scene { Open, Lane2, Lane1, Emu3473, Emu3473Closed, HallRing };

const char *SceneName(Scene scene)
{
	switch (scene) {
		case Scene::Open: return "open ground";
		case Scene::Lane2: return "mine pocket, 2-tile lane";
		case Scene::Lane1: return "mine pocket, 1-tile lane";
		case Scene::Emu3473: return "emu3473, 1-tile gap at the hall";
		case Scene::Emu3473Closed: return "emu3473, strip touching the hall";
		case Scene::HallRing: return "hall in a forest ring, 1-tile entry";
	}
	return "";
}

/// Lay the scene and its workers (`count`; the emu3473 scenes always have their six).
void Lay(TrafficWorld &w, Scene scene, int count)
{
	switch (scene) {
		case Scene::Open:
			w.Buildings({20, 31}, {30, 30});
			for (int i = 0; i != count; ++i) {
				w.Worker({29 + i % 3, 35 + i / 3});
			}
			break;
		case Scene::Lane2:
		case Scene::Lane1:
			// forest box 7..18 x 26..36 round the mine 10..12 x 30..32, a 1-tile ring round the mine,
			// a lane east to open ground: rows 30-31 (2 wide) or row 31 (1 wide), 5 long
			w.Forest(7, 26, 18, 36);
			w.Ground(9, 29, 13, 33);
			w.Ground(14, scene == Scene::Lane2 ? 30 : 31, 18, 31);
			w.Buildings({10, 30}, {24, 29});
			for (int i = 0; i != count; ++i) {
				w.Worker({22 + i % 4, 34 + i / 4});
			}
			break;
		case Scene::Emu3473:
		case Scene::Emu3473Closed:
			// The save of round 3.4.73: hall, gold mine to the west, a forest strip from below the
			// mine up to the hall's south-west corner, Peasant 14 south of it (10,16), the others north
			// (synthetic tiles after the shots h1 / h3 / l3). Emu3473: one free column (x 9) between
			// the strip's tip and the hall; Closed: none, the way round is the hall's east side.
			w.Forest(2, 16, 7, 22);
			w.Forest(5, 15, scene == Scene::Emu3473 ? 8 : 9, 17);
			w.Forest(8, 14, scene == Scene::Emu3473 ? 8 : 9, 15);
			w.Buildings({3, 12}, {10, 11});
			for (const Vec2i &pos : {Vec2i(7, 12), Vec2i(8, 13), Vec2i(7, 11), Vec2i(8, 12), Vec2i(6, 11),
			                         Vec2i(10, 16)}) {
				w.Worker(pos);
			}
			break;
		case Scene::HallRing:
			// hall 30..33 x 28..31; forest on the ring round it, one ring tile free: 29,29 (west)
			w.Forest(29, 27, 34, 27);
			w.Forest(29, 32, 34, 32);
			w.Forest(34, 27, 34, 32);
			w.Forest(29, 27, 29, 32);
			w.Ground(29, 29, 29, 29);
			w.Buildings({10, 30}, {30, 28});
			for (int i = 0; i != count; ++i) {
				w.Worker({20 + i, 36});
			}
			break;
	}
}

struct SceneRow
{
	Scene scene;
	int count;
	int minTrips;
};

const SceneRow Rows[] = {
	{Scene::Open, 6, 5},     {Scene::Lane2, 4, 3},    {Scene::Lane2, 8, 3},         {Scene::Lane1, 2, 2},
	{Scene::Lane1, 4, 2},    {Scene::Emu3473, 6, 3},  {Scene::Emu3473Closed, 6, 3}, {Scene::HallRing, 4, 3},
};

bool Passes(const TrafficWorld &w, int minTrips)
{
	return !w.AnyLostOrder() && w.FewestTrips() >= minTrips && w.LongestStall() <= StallLimit;
}

// ---- Live run 68 M3 (work/agent-orig-68/ORIG68.md): 8 Peasants harvesting through a 2-tile lane ----

/// The Human II scene of run 68 in its own tile numbers (synthetic tiles after the unit tracks of
/// log/m3t1.tsv and capture s1): Town Hall 30-33 x 40-43, Gold Mine 19-21 x 46-48, Farms 26,44 /
/// 28,44 / 26,46 / 25,48. The lane (2 wide, diagonal) 30-31,44-45 -> 29-30,46 -> 28-29,47 ->
/// 27-28,48-49 -> 26-27,50 -> 24-25,50-51 -> 22-24,48-49 between the Farms and the shore, about
/// 10 steps hall -> mine; the other way is round the west of the forest (x 14, rows 38-50),
/// 24-45 steps. The pocket 25,47 (forest N / W, Farms E / S) is kept.
void LayRun68(TrafficWorld &w, int count)
{
	auto water = [&](int x0, int y0, int x1, int y1) {
		w.Fill(x0, y0, x1, y1, MapFieldWaterAllowed);
	};
	// the shore east and south of the lane
	water(35, 44, 63, 44);
	water(32, 45, 63, 45);
	water(31, 46, 63, 46);
	water(30, 47, 63, 47);
	water(29, 48, 63, 49);
	water(28, 50, 63, 50);
	water(0, 51, 63, 63);
	w.Ground(24, 51, 26, 51);
	water(0, 40, 9, 50);
	// the forest between the hall and the mine
	w.Forest(17, 39, 19, 39);
	w.Forest(16, 40, 21, 40);
	w.Forest(15, 41, 21, 41);
	w.Forest(15, 42, 22, 43);
	w.Forest(15, 44, 23, 44);
	w.Forest(16, 45, 25, 45);
	w.Forest(15, 46, 18, 47);
	w.Forest(22, 46, 25, 46);
	w.Forest(23, 47, 24, 47);
	w.Forest(15, 48, 16, 48);
	// the Farms (walls)
	for (const Vec2i &farm : {Vec2i(26, 44), Vec2i(28, 44), Vec2i(26, 46), Vec2i(25, 48)}) {
		w.Fill(farm.x, farm.y, farm.x + 1, farm.y + 1, MapFieldLandAllowed | MapFieldUnpassable);
	}
	w.Buildings({19, 46}, {30, 40});
	// the 8 Peasants of t1 at frame 0 (1545-1551, 1564)
	const Vec2i starts[8] = {{32, 44}, {31, 44}, {30, 44}, {29, 43}, {29, 42}, {29, 41}, {29, 40}, {34, 41}};
	for (int i = 0; i != count; ++i) {
		w.Worker(starts[i]);
	}
}

/// What run 68 counted: legs (lane / round / pocket), turn-backs (A -> B -> A), longest stand while
/// walking (not the exit pause, not the start delay), tile swaps and side-by-side passes of two
/// workers going opposite ways in the lane.
struct Lane68Metrics
{
	struct Track
	{
		Vec2i pos{-1, -1};
		Vec2i prev{-1, -1};   /// tile before pos
		Vec2i prev2{-1, -1};  /// tile before prev
		bool outside = false;
		unsigned long freeAt = 0;
		int stand = 0;
		int longestStand = 0;
		int legSteps = 0;
		int legMinX = 1000;
		bool legLane = false;
		bool legPocket = false;
		std::vector<Vec2i> legTiles;
	};
	std::string roundTrace; /// the first round legs, tile by tile
	std::vector<Track> tracks;
	std::vector<int> passSign; /// per pair: sign of (hall distance i - j) while close, 0 = not close
	std::vector<char> passCounted; /// per pair: this meeting already counted as a pass
	std::vector<int> hallDist;
	int legs = 0, laneLegs = 0, roundLegs = 0, pocketLegs = 0, turnBacks = 0, swaps = 0, passes = 0;
	int stagger = 0;

	static bool LaneTile(const Vec2i &p) { return p.y >= 44 && p.x >= 22; }

	void Init(const TrafficWorld &w)
	{
		const int size = Map.Info.MapWidth;
		hallDist.assign(size * size, 1 << 20);
		std::vector<Vec2i> open;
		const Vec2i h = w.hall->tilePos;
		for (int y = h.y - 1; y <= h.y + 4; ++y) {
			for (int x = h.x - 1; x <= h.x + 4; ++x) {
				if (Map.Info.IsPointOnMap(Vec2i(x, y)) && !Map.Field(x, y)->CheckMask(MapFieldUnpassable | MapFieldWaterAllowed | MapFieldBuilding)) {
					hallDist[x + y * size] = 0;
					open.push_back(Vec2i(x, y));
				}
			}
		}
		for (size_t i = 0; i != open.size(); ++i) {
			const Vec2i p = open[i];
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const Vec2i q(p.x + dx, p.y + dy);
					if (!Map.Info.IsPointOnMap(q) || Map.Field(q)->CheckMask(MapFieldUnpassable | MapFieldWaterAllowed | MapFieldBuilding)) {
						continue;
					}
					int &d = hallDist[q.x + q.y * size];
					if (d > hallDist[p.x + p.y * size] + 1) {
						d = hallDist[p.x + p.y * size] + 1;
						open.push_back(q);
					}
				}
			}
		}
		tracks.resize(w.workers.size());
		for (size_t k = 0; k != tracks.size(); ++k) {
			tracks[k].pos = w.workers[k]->tilePos;
			tracks[k].outside = true;
			tracks[k].freeAt = GameCycle + k * stagger + 1;
		}
		passSign.assign(tracks.size() * tracks.size(), 0);
		passCounted.assign(tracks.size() * tracks.size(), 0);
	}

	int Dist(const Vec2i &p) const { return hallDist[p.x + p.y * Map.Info.MapWidth]; }

	void EndLeg(Track &t)
	{
		if (t.legSteps == 0) {
			return;
		}
		++legs;
		if (t.legPocket) {
			++pocketLegs;
		} else if (t.legMinX <= 16) {
			++roundLegs;
			if (roundLegs <= 3) {
				roundTrace += "round leg at cycle " + std::to_string(GameCycle) + ":";
				for (const Vec2i &p : t.legTiles) {
					roundTrace += " " + std::to_string(p.x) + "," + std::to_string(p.y);
				}
				roundTrace += "; ";
			}
		} else if (t.legLane) {
			++laneLegs;
		}
		t.legSteps = 0;
		t.legMinX = 1000;
		t.legLane = t.legPocket = false;
		t.legTiles.clear();
	}

	void Sample(const TrafficWorld &w)
	{
		const size_t n = tracks.size();
		std::vector<Vec2i> before(n);
		for (size_t k = 0; k != n; ++k) {
			before[k] = tracks[k].pos;
		}
		for (size_t k = 0; k != n; ++k) {
			const CUnit &u = *w.workers[k];
			Track &t = tracks[k];
			const bool active = !u.Removed && u.CurrentAction() == UnitAction::Resource;
			if (!active) {
				if (t.outside) {
					EndLeg(t);
				}
				t.outside = false;
				t.stand = 0;
				t.pos = Vec2i(-1, -1);
				continue;
			}
			if (!t.outside) { // came out of a building: the exit pause is not a stand
				t.outside = true;
				t.freeAt = GameCycle + WC2_WORKER_EXIT_STAND_FRAMES + 1;
				t.pos = u.tilePos;
				t.prev = t.prev2 = Vec2i(-1, -1);
				t.stand = 0;
				continue;
			}
			if (u.tilePos != t.pos) {
				if (u.tilePos == t.prev) {
					++turnBacks;
				}
				t.prev2 = t.prev;
				t.prev = t.pos;
				t.pos = u.tilePos;
				t.stand = 0;
				++t.legSteps;
				t.legTiles.push_back(u.tilePos);
				t.legMinX = std::min<int>(t.legMinX, u.tilePos.x);
				t.legLane |= u.tilePos.x >= 26 && u.tilePos.x <= 30 && u.tilePos.y >= 46 && u.tilePos.y <= 50;
				t.legPocket |= u.tilePos == Vec2i(25, 47);
			} else if (GameCycle > t.freeAt) {
				t.longestStand = std::max(t.longestStand, ++t.stand);
			}
		}
		for (size_t i = 0; i != n; ++i) {
			for (size_t j = i + 1; j != n; ++j) {
				const Track &a = tracks[i];
				const Track &b = tracks[j];
				int &sign = passSign[i * n + j];
				const bool opposite = (w.workers[i]->ResourcesHeld > 0) != (w.workers[j]->ResourcesHeld > 0);
				if (a.pos.x < 0 || b.pos.x < 0 || !opposite || !LaneTile(a.pos) || !LaneTile(b.pos)
				    || Chebyshev(a.pos, b.pos) > 2) {
					sign = 0;
					passCounted[i * n + j] = 0;
					continue;
				}
				if (a.pos == before[j] && b.pos == before[i] && a.pos != b.pos) {
					++swaps;
					sign = 0;
					continue;
				}
				const int diff = Dist(a.pos) - Dist(b.pos);
				const int now = diff > 0 ? 1 : diff < 0 ? -1 : 0;
				if (now != 0) {
					if (sign != 0 && now != sign && !passCounted[i * n + j]) {
						++passes; // once a meeting: side by side the order may flip to and fro
						passCounted[i * n + j] = 1;
					}
					sign = now;
				}
			}
		}
	}

	int LongestStand() const
	{
		int longest = 0;
		for (const Track &t : tracks) {
			longest = std::max(longest, t.longestStand);
		}
		return longest;
	}
};

struct Run68Result
{
	int lost = 0;
	int working = 0;      /// workers still on their order at the end
	int fewestWorking = 0; /// fewest trips of a worker still on its order
	int trips = 0;
	std::string text;
};

/// One run 68 trial: 8 workers, `stagger` cycles apart, 5764 cycles (150 s of the original).
Run68Result RunLane68(int stagger, Lane68Metrics &m, int crossingCost = 10)
{
	TrafficWorld w;
	// A step costs the search 1 + the tile's move cost: 2 in the game (moveCost 1), 1 here (fields
	// of the synthetic map have 0). The game's moving-unit cost 20 (stratagus.lua) is 10 steps; 10
	// here keeps that ratio.
	AStarMovingUnitCrossingCost = crossingCost;
	w.SlowSteps();
	w.stagger = stagger;
	LayRun68(w, 8);
	m.stagger = stagger;
	m.Init(w);
	w.sample = [&m](TrafficWorld &world) { m.Sample(world); };
	w.Run(5764);
	Run68Result r;
	r.fewestWorking = 1 << 30;
	for (size_t k = 0; k != w.stats.size(); ++k) {
		r.trips += w.stats[k].trips;
		if (w.stats[k].lostOrder) {
			++r.lost;
			r.text += "lost w" + std::to_string(k) + " at (" + std::to_string(w.lostAt[k].x) + ","
			          + std::to_string(w.lostAt[k].y) + ")" + (w.workers[k]->ResourcesHeld ? " loaded" : " empty")
			          + "; ";
		} else {
			++r.working;
			r.fewestWorking = std::min(r.fewestWorking, w.stats[k].trips);
		}
	}
	r.text += std::string("crossing cost ") + std::to_string(crossingCost) + ", ";
	r.text += "stagger " + std::to_string(stagger) + ": lost " + std::to_string(r.lost) + ", deliveries "
	          + std::to_string(r.trips) + ", fewest of the working " + std::to_string(r.fewestWorking)
	          + ", longest stand " + std::to_string(m.LongestStand()) + ", legs " + std::to_string(m.legs)
	          + " (lane " + std::to_string(m.laneLegs) + ", round " + std::to_string(m.roundLegs) + ", pocket "
	          + std::to_string(m.pocketLegs) + "), turn-backs " + std::to_string(m.turnBacks) + ", swaps "
	          + std::to_string(m.swaps) + ", passes " + std::to_string(m.passes) + " | " + w.Summary();
	return r;
}
} // namespace

TEST_CASE("worker traffic: every scene, workers starting together and 37 cycles apart, all keep delivering")
{
	for (const SceneRow &row : Rows) {
		for (int stagger : {0, 37}) {
			CAPTURE(SceneName(row.scene));
			CAPTURE(row.count);
			CAPTURE(stagger);
			TrafficWorld w;
			w.stagger = stagger;
			Lay(w, row.scene, row.count);
			w.Run(Cycles);
			MESSAGE(SceneName(row.scene) << " x" << row.count << " stagger " << stagger << ": " << w.Summary());
			CHECK_FALSE(w.AnyLostOrder());
			CHECK(w.FewestTrips() >= row.minTrips);
			CHECK(w.LongestStall() <= StallLimit);
		}
	}
}

TEST_CASE("worker traffic: eight workers 37 cycles apart in the closed mine pocket (2-tile lane, no way round)")
{
	// work/WORKER_STUCK_3476.md D2. No live case: the original was measured with a way round (run 68,
	// below); this one is a report (with and without the run 68 rule), the checks are on run 68.
	for (int slow = 0; slow != 2; ++slow) {
		for (bool rule : {false, true}) {
			HarvestTurnsAway = rule;
			TrafficWorld w;
			if (slow) {
				w.SlowSteps();
			}
			w.stagger = 37;
			Lay(w, Scene::Lane2, 8);
			w.Run(slow ? 5764 : Cycles);
			MESSAGE((rule ? "now" : "before") << (slow ? ", 16 cycles a step: " : ", 1 tile a cycle: ")
			        << w.Summary());
			HarvestTurnsAway = true;
		}
	}
}

TEST_CASE("lane 68 report: with and without the held-step rule")
{
	// mode 0: no HarvestStepHeld; 1: now. (The old greedy A* estimate was removed 2026-10-21:
	// work/CALIB_RUN68.md has its numbers.)
	for (int mode = 0; mode != 2; ++mode) {
		for (int stagger : {0, 37}) {
			HarvestTurnsAway = mode != 0;
			Lane68Metrics m;
			const Run68Result r = RunLane68(stagger, m);
			HarvestTurnsAway = true;
			MESSAGE("mode " << mode << (mode ? " rule" : " no rule") << ", " << r.text << " TRACE " << m.roundTrace);
		}
	}
}

TEST_CASE("lane 68: eight workers through a 2-tile lane keep harvesting, pass side by side and go round")
{
	// Live run 68 M3 t1 (1 trial, 8 Peasants sent at once, 150 s = 5764 frames, 69 legs): 2 of 8
	// orders lost on the first return (1 at the mine exit 3 frames into the return with a mate on
	// the exit tile, 1 in the dead-end pocket 25,47 - a scene artefact); the other 6 made 5
	// deliveries each (30); no jam: longest stand while walking 72 frames (1.9 s); 0 tile swaps,
	// 15 side-by-side opposite passes; 55 lane legs, 13 round the west (24-45 steps), 1 pocket;
	// 22 turn-backs (step toward the lane, step back, then round).
	int passes = 0;
	int roundLegs = 0;
	for (int stagger : {0, 37}) {
		CAPTURE(stagger);
		Lane68Metrics m;
		const Run68Result r = RunLane68(stagger, m);
		MESSAGE(r.text);
		// live 2 of 8 (1 exit + 1 pocket): no more than that (0 is not required, nor added)
		CHECK(r.lost <= 2);
		// live the 6 still harvesting made 5 each
		CHECK(r.working >= 6);
		CHECK(r.fewestWorking >= 4);
		// live 30 deliveries
		CHECK(r.trips >= 24);
		// live 72 frames (1.9 s) at most while walking: no jam, a held worker does not queue
		// (bound 2 x; before the run 68 rule 57 here, the closed pocket jammed for good)
		CHECK(m.LongestStand() <= 144);
		// live 0 swaps
		CHECK(m.swaps == 0);
		// live 13 of 69 legs round (19 %); before (greedy A* estimate) 44 of 96 here
		CHECK(m.roundLegs * 100 <= 35 * m.legs);
		passes += m.passes;
		roundLegs += m.roundLegs;
	}
	// live 15 side-by-side passes in one trial; before 0 and 3
	CHECK(passes >= 5);
	// live 13 legs round: a held worker does go round
	CHECK(roundLegs >= 1);
}

TEST_CASE("worker traffic: 3.4.76 against the 3.4.75 blocked-step handling, every scene, four start spreads")
{
	// The same scenes with the 3.4.75 NextPathElement emulated (the blocked step dropped, no expiry;
	// TrafficWorld::Run). Counts the runs where any worker lost its order, stood > StallLimit or
	// delivered too little. A regression of 3.4.76 would show as more failed runs than 3.4.75.
	int failed[2] = {0, 0};
	int runs = 0;
	std::string lines;
	for (const SceneRow &row : Rows) {
		for (int stagger : {0, 13, 37, 59}) {
			++runs;
			for (int mode = 0; mode != 2; ++mode) {
				TrafficWorld w;
				w.stagger = stagger;
				w.oldStepDrop = mode == 1;
				Lay(w, row.scene, row.count);
				w.Run(Cycles);
				if (!Passes(w, row.minTrips)) {
					++failed[mode];
					lines += std::string(mode == 0 ? "3.4.76" : "3.4.75") + " FAILS " + SceneName(row.scene) + " x"
					         + std::to_string(row.count) + " stagger " + std::to_string(stagger) + ": "
					         + w.Summary() + "\n";
				}
			}
		}
	}
	MESSAGE("runs " << runs << ", failed 3.4.76: " << failed[0] << ", failed 3.4.75: " << failed[1] << "\n" << lines);
	CHECK(failed[0] <= failed[1]);
}

TEST_CASE("worker traffic: a worker that cannot reach the mine keeps the order and stands (retry loop)")
{
	// Documents the only engine path found that leaves a worker standing with its harvest order and
	// the line to the mine: every way is unreachable. MoveToResource answers -1, State counts up from
	// SUB_MOVE_TO_RESOURCE (5) to SUB_UNREACHABLE_RESOURCE (31), 5 cycles apart, then
	// FindAnotherResource searches from the MINE (ResourceUnitFinder pushes the mine's own tiles), finds
	// the same mine and starts the count again - for ever. Same in 3.4.73 (old pathfinder) and 3.4.76.
	SUBCASE("enclosed by forest")
	{
		TrafficWorld w;
		w.Buildings({20, 31}, {30, 30});
		w.Forest(38, 38, 42, 42);
		w.Ground(40, 40, 40, 40);
		CUnit &stuck = w.Worker({40, 40});
		w.Run(2000);
		MESSAGE(w.Summary());
		CHECK(stuck.tilePos == Vec2i(40, 40));
		CHECK(stuck.CurrentAction() == UnitAction::Resource);
		CHECK_FALSE(w.AnyLostOrder());
	}
	SUBCASE("the only gap held by a standing unit; it walks off: the worker goes on")
	{
		TrafficWorld w;
		w.Buildings({20, 31}, {30, 30});
		w.Forest(38, 38, 42, 42);
		w.Ground(39, 39, 41, 41);
		w.Ground(38, 40, 38, 40); // the gap, west side
		CUnit &blocker = w.Place(w.peasant, {38, 40}, 0);
		CUnit &stuck = w.Worker({40, 40});
		w.Run(1500);
		CHECK(stuck.CurrentAction() == UnitAction::Resource);
		CHECK(Chebyshev(stuck.tilePos, Vec2i(40, 40)) <= 1);
		// the blocker leaves
		blocker.Remove(nullptr);
		w.Run(1500);
		MESSAGE(w.Summary());
		CHECK(w.stats.back().trips >= 1);
	}
}

// ---- AI-LANE-DEADLOCK / AI-CUT-OFF-RECOVERY (work/AI_LANE_IMPL.md): the computer's workers ----
//
// Cycle 679: two computer workers going opposite ways in a one-tile road between hall and mine
// never resolved; loaded workers stood idle. The same kind of scenes as above, but the workers
// belong to a computer player (AiEnabled, a PlayerAi): the harvest's PF_WAIT branch then counts
// Range to 5 and calls AiCanNotMove -> AiMoveUnitInTheWay. Every 4 s (COLLECT_RESOURCES_INTERVAL)
// the harness does what AiCollectResources does with an idle worker: loaded -> CommandReturnGoods,
// empty -> back to the mine (the gold branch of AiAssignHarvesterFromUnit).

namespace
{
struct ComputerResult
{
	int trips = 0;
	int fewest = 1 << 30;
	int longestStall = 0;
	int longestIdleLoaded = 0; /// longest run of cycles a worker stood with goods and no order
	int idleLoadedCycles = 0;
	int idleCycles = 0;        /// cycles any worker stood with no order
	int movedAside = 0;        /// cycles a worker had a Move order (AiMoveUnitInTheWay)
	std::string text;
};

enum class Road { Lane1Pocket, Closed679, Open679 };

const char *RoadName(Road road)
{
	switch (road) {
		case Road::Lane1Pocket: return "mine pocket, 1-tile lane";
		case Road::Closed679: return "679 road, 1 tile x 7, no way round";
		case Road::Open679: return "679 road, 1 tile x 7, way round";
	}
	return "";
}

/// Mine 26..28 x 30..32, hall 38..41 x 30..33; the road row 31, x 30..36 between walls of the
/// computer's own buildings (unpassable), the columns 29 and 37 beside the mine and the hall free.
/// Closed: the wall runs the whole map height; open: rows
/// 22..40 only, the way round about 20 steps longer. Even workers by the hall (empty, to the mine),
/// odd ones by the mine with goods (to the hall): they meet head-on in the road.
void LayRoad679(TrafficWorld &w, bool closed, int count)
{
	const int y0 = closed ? 0 : 22;
	const int y1 = closed ? 63 : 40;
	w.Fill(30, y0, 36, 30, MapFieldLandAllowed | MapFieldUnpassable);
	w.Fill(30, 32, 36, y1, MapFieldLandAllowed | MapFieldUnpassable);
	w.Buildings({26, 30}, {38, 30});
	const Vec2i hallSide[4] = {{42, 34}, {43, 34}, {42, 35}, {43, 35}};
	const Vec2i mineSide[4] = {{25, 33}, {24, 33}, {25, 34}, {24, 34}};
	for (int i = 0; i != count; ++i) {
		const bool loaded = i % 2 == 1;
		CUnit &u = w.Worker(loaded ? mineSide[i / 2] : hallSide[i / 2]);
		if (loaded) {
			u.CurrentResource = GoldCost;
			u.ResourcesHeld = 100;
			u.Orders[0] = COrder::NewActionReturnGoods(u, w.hall);
		}
	}
}

ComputerResult RunComputer(Road road, int count, int stagger, int cycles, bool computer)
{
	TrafficWorld w;
	w.stagger = stagger;
	if (computer) {
		Players[0].Type = PlayerTypes::PlayerComputer;
		Players[0].AiEnabled = true;
		Players[0].Ai = std::make_unique<PlayerAi>();
		Players[0].Ai->Player = &Players[0];
		AiPlayer = Players[0].Ai.get();
	}
	if (road == Road::Lane1Pocket) {
		Lay(w, Scene::Lane1, count);
	} else {
		LayRoad679(w, road == Road::Closed679, count);
	}
	ComputerResult r;
	std::vector<int> idleLoaded(w.workers.size(), 0);
	w.sample = [&](TrafficWorld &world) {
		const bool collect = (GameCycle % (4 * CYCLES_PER_SECOND)) == 0;
		if (collect && computer) {
			// a Move given by AiMoveUnitInTheWay behind a still order: the still order would end in
			// HandleUnitAction; the harness runs only the order
			for (CUnit *u : world.workers) {
				if (u->Orders.size() > 1 && u->Orders[0]->Action == UnitAction::Still) {
					u->Orders.erase(u->Orders.begin());
					u->pathFinderData->output.Length = 0;
				}
			}
		}
		for (size_t k = 0; k != world.workers.size(); ++k) {
			CUnit &u = *world.workers[k];
			if (u.CurrentAction() == UnitAction::Move) {
				++r.movedAside;
			}
			const bool idle = !u.Removed && u.CurrentAction() == UnitAction::Still;
			if (idle) {
				++r.idleCycles;
			}
			if (idle && u.ResourcesHeld) {
				++r.idleLoadedCycles;
				r.longestIdleLoaded = std::max(r.longestIdleLoaded, ++idleLoaded[k]);
			} else {
				idleLoaded[k] = 0;
			}
			if (collect && idle && computer) {
				// AiCollectResources (ai_resource.cpp): goods home, an empty worker to the mine
				if (u.ResourcesHeld) {
					CommandReturnGoods(u, nullptr, EFlushMode::On);
				} else {
					CommandResource(u, *world.mine, EFlushMode::On);
				}
				// the still order would end in HandleUnitAction; the harness runs only the order
				if (u.Orders.size() > 1 && u.Orders[0]->Action == UnitAction::Still) {
					u.Orders.erase(u.Orders.begin());
					u.pathFinderData->output.Length = 0;
				}
			}
		}
	};
	w.Run(cycles);
	for (const WorkerStats &s : w.stats) {
		r.trips += s.trips;
		r.fewest = std::min(r.fewest, s.trips);
	}
	r.longestStall = w.LongestStall();
	r.text = std::string(computer ? "computer " : "person ") + RoadName(road) + " x" + std::to_string(count)
	         + " stagger " + std::to_string(stagger) + ": deliveries " + std::to_string(r.trips) + ", fewest "
	         + std::to_string(r.fewest) + ", longest stand " + std::to_string(r.longestStall) + ", idle "
	         + std::to_string(r.idleCycles) + ", idle loaded " + std::to_string(r.idleLoadedCycles) + " (longest "
	         + std::to_string(r.longestIdleLoaded) + "), move cycles " + std::to_string(r.movedAside)
	         + " | " + w.Summary();
	AiPlayer = nullptr;
	return r;
}
} // namespace

TEST_CASE("computer road 679 report: computer and person workers head-on in a 1-tile road" * doctest::skip())
{
	for (Road road : {Road::Lane1Pocket, Road::Closed679, Road::Open679}) {
		for (int count : {2, 4, 8}) {
			for (int stagger : {0, 37}) {
				MESSAGE(RunComputer(road, count, stagger, Cycles, false).text);
				MESSAGE("today: " << RunComputer(road, count, stagger, Cycles, true).text);
			}
		}
	}
}

TEST_CASE("computer road 679: the computer's workers head-on in a 1-tile road keep harvesting")
{
	// AI-LANE-DEADLOCK with today's rules: 2 and 4 computer workers, half of them
	// loaded and coming from the mine side, head-on in a 1-tile road with and without a way round,
	// and the 1-tile lane of the mine pocket. Each keeps delivering (9-11 trips in 4000 cycles in the
	// run of 2026-10-21); a loaded worker stands without an order at most one AiCollectResources
	// period (4 s). Left out: 2 against 2 starting together in the closed road - a jam for the
	// person's workers too (0 deliveries either way, work/AI_LANE_IMPL.md), a movement item.
	for (Road road : {Road::Lane1Pocket, Road::Closed679, Road::Open679}) {
		for (int count : {2, 4}) {
			for (int stagger : {0, 37}) {
				if (road == Road::Closed679 && count == 4 && stagger == 0) {
					continue;
				}
				CAPTURE(RoadName(road));
				CAPTURE(count);
				CAPTURE(stagger);
				const ComputerResult person = RunComputer(road, count, stagger, Cycles, false);
				const ComputerResult r = RunComputer(road, count, stagger, Cycles, true);
				MESSAGE(person.text);
				MESSAGE(r.text);
				CHECK(r.fewest >= 2);
				// 431 cycles the longest (closed road, 2 workers): one re-issue after a lost order
				CHECK(r.longestStall <= 2 * StallLimit);
				CHECK(r.longestIdleLoaded <= 4 * CYCLES_PER_SECOND);
				// not worse than the person's workers in the same scene
				CHECK(r.trips * 4 >= person.trips * 3);
			}
		}
	}
}

// ---- Live run 77 A (work/agent-orig-77/ORIG77.md, CALIB_RUN77.md): a gold harvester whose way is closed ----
// A1 / A2: a Peasant in the pocket 50-52,40-41 (Human II; the run 73 neck 52,42 and the north side held by
// idle Footmen, trees round it) sent to the gold mine: one step, then at its next look the goal turned to
// the nearest tree (beside it) and it chopped; A3: boxed in by 8 idle units in open land: the goal turned
// to the nearest tree (3 off) 3 frames after the order began, it stood 30 s with the harvest order, walked
// 1 frame after a ring unit stepped off, chopped, brought the lumber home and went back to the trees.
// Never back to the mine (0 of 3).
extern bool GoldHarvestTurnsToWood;

namespace
{
struct Turn77
{
	bool wood = false;    /// the worker's order became a wood harvest
	Vec2i treeGoal{-1, -1};
	int turnedAt = -1;    /// cycle of the turn
	int firstStepAfterOpen = -1;
	int chopAt = -1;      /// cycle it began to chop
	Vec2i end{-1, -1};
	int endDistToTree = 99;
};

/// Pocket (A1 / A2) or idle ring (A3). openAt < 0: never opened.
Turn77 RunTurn77(bool pocket, bool rule, int openAt)
{
	GoldHarvestTurnsToWood = rule;
	TrafficWorld w;
	auto wood = std::make_unique<ResourceInfo>();
	wood->ResourceId = WoodCost;
	wood->FinalResource = WoodCost;
	wood->ResourceCapacity = 100;
	wood->WaitAtResource = 150;
	wood->WaitAtDepot = 150;
	wood->TerrainHarvester = true;
	w.peasant.ResInfo[WoodCost] = std::move(wood);
	w.hallType.CanStore[WoodCost] = 1;
	auto chop = std::make_unique<CAnimation_Wait>();
	chop->Init("1", nullptr);
	w.animations.Harvest[WoodCost].push_back(std::move(chop));
	w.Buildings({6, 40}, {50, 50});
	Vec2i start;
	std::vector<CUnit *> seal;
	if (pocket) {
		// forest 30..40 x 10..20, pocket 34..36 x 14..15, neck 36,16 then a road 36,17..20 to the ground
		w.Forest(30, 10, 40, 20);
		w.Ground(34, 14, 36, 15);
		w.Ground(36, 16, 36, 20);
		start = Vec2i(35, 15);
		seal.push_back(&w.Place(w.peasant, {36, 16}, 0));
	} else {
		w.Forest(20, 10, 40, 12); // the nearest trees 3 rows north of the worker
		start = Vec2i(30, 15);
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (dx || dy) {
					seal.push_back(&w.Place(w.peasant, start + Vec2i(dx, dy), 0));
				}
			}
		}
	}
	CUnit &u = w.Place(w.peasant, start, 0);
	u.Orders.clear();
	u.Orders.push_back(COrder::NewActionResource(u, *w.mine));
	u.pathFinderData->output.Length = 0;
	Turn77 r;
	for (int cycle = 0; cycle != 600; ++cycle) {
		++GameCycle;
		w.mine->VisCount[0] = 1;
		w.hall->VisCount[0] = 1;
		if (cycle == openAt) {
			seal.front()->Remove(nullptr); // the seal / a ring unit leaves
		}
		if (u.Orders[0]->Finished) {
			if (u.Orders.size() > 1) {
				u.Orders.erase(u.Orders.begin());
			} else {
				u.Orders[0] = COrder::NewActionStill();
			}
		}
		const Vec2i at = u.tilePos;
		if (u.CurrentAction() == UnitAction::Resource) {
			u.Orders[0]->Execute(u);
		}
		if (u.CurrentAction() == UnitAction::Resource || u.Orders.size() > 1) {
			const auto *res = static_cast<const COrder_Resource *>(
				u.Orders.size() > 1 && u.Orders[0]->Finished ? u.Orders[1].get() : u.Orders[0].get());
			if (res->GetCurrentResource() == WoodCost && !r.wood) {
				r.wood = true;
				r.turnedAt = cycle;
				r.treeGoal = res->GetGoalPos();
			}
		}
		if (openAt >= 0 && cycle >= openAt && u.tilePos != at && r.firstStepAfterOpen < 0) {
			r.firstStepAfterOpen = cycle - openAt;
		}
		if (r.wood && u.CurrentAction() == UnitAction::Resource && !u.Orders[0]->Finished
		    && static_cast<const COrder_Resource *>(u.Orders[0].get())->GetState() >= 55) {
			r.chopAt = cycle; // SUB_START_GATHERING: chopping (the cut itself needs the tileset)
			break;
		}
	}
	r.end = u.tilePos;
	if (r.wood) {
		r.endDistToTree = Chebyshev(r.end, r.treeGoal);
	}
	u.Orders.clear();
	GoldHarvestTurnsToWood = true;
	return r;
}
} // namespace

TEST_CASE("harvest 77: a gold harvester whose way is closed by standing units turns to the nearest tree")
{
	// A1 / A2: the pocket - the goal turns to a tree beside it at the first look, no stand, no drop.
	const Turn77 a1 = RunTurn77(true, true, -1);
	MESSAGE("pocket: wood " << a1.wood << " at cycle " << a1.turnedAt << ", tree " << a1.treeGoal.x << "," << a1.treeGoal.y
	        << ", end " << a1.end.x << "," << a1.end.y);
	CHECK(a1.wood);
	CHECK(a1.turnedAt >= 0);
	CHECK(a1.turnedAt <= 20); // live: at the look after its first step (16 frames) or at once
	CHECK(Chebyshev(a1.treeGoal, Vec2i(35, 15)) <= 2);
	CHECK(a1.chopAt >= 0); // chops a tree beside the pocket (live A1 / A2)
	CHECK(a1.endDistToTree <= 1);

	// A3: boxed in by 8 idle units - turns at once, stands with the order, walks when a ring unit leaves.
	const Turn77 a3 = RunTurn77(false, true, 200);
	MESSAGE("ring: wood " << a3.wood << " at cycle " << a3.turnedAt << ", tree " << a3.treeGoal.x << "," << a3.treeGoal.y
	        << ", first step " << a3.firstStepAfterOpen << " after the opening, chop at " << a3.chopAt << ", end "
	        << a3.end.x << "," << a3.end.y);
	CHECK(a3.wood);
	CHECK(a3.turnedAt <= 5); // live: 3 frames after the order began
	CHECK(a3.treeGoal.y == 12); // the nearest row of trees
	CHECK(a3.firstStepAfterOpen >= 0);
	CHECK(a3.firstStepAfterOpen <= 12); // live 1 frame; ours at the 10-cycle look of the wood order
	CHECK(a3.chopAt > 200);              // stood with the order until the opening, then chopped
	CHECK(a3.endDistToTree <= 1);

	// The rule off (ours before run 77): the order stays a gold harvest.
	const Turn77 off = RunTurn77(true, false, -1);
	MESSAGE("pocket, rule off: wood " << off.wood << ", end " << off.end.x << "," << off.end.y);
	CHECK_FALSE(off.wood);
}

// ---- Live run 78 (work/agent-orig-78/ORIG78.md, CALIB_RUN78.md) ----
// G1-G3 / L1-L3: a Peasant with gold / lumber boxed in by 8 idle units, a Return order to the hall: the
// order ended 3 frames after it began (6 of 6; run 77 A1 the same at the natural return), the load was
// kept, nothing more happened when a ring unit walked off (watched 8-15 s). M1 / M2: a gold mine whose
// free tiles were all held by idle units: the Peasant turned to wood at a tree near the mine (2 of 2).
// R1-R3 (+ run 77 A3): a boxed Peasant walked 1 frame after a ring unit left (4 of 4).
extern bool DepotClosedDropsOrder;
extern bool WoodBoxedLooksEachCycle;

namespace
{
void AddWood(TrafficWorld &w)
{
	auto wood = std::make_unique<ResourceInfo>();
	wood->ResourceId = WoodCost;
	wood->FinalResource = WoodCost;
	wood->ResourceCapacity = 100;
	wood->WaitAtResource = 150;
	wood->WaitAtDepot = 150;
	wood->TerrainHarvester = true;
	w.peasant.ResInfo[WoodCost] = std::move(wood);
	w.hallType.CanStore[WoodCost] = 1;
	auto chop = std::make_unique<CAnimation_Wait>();
	chop->Init("1", nullptr);
	w.animations.Harvest[WoodCost].push_back(std::move(chop));
}

void Ring(TrafficWorld &w, const Vec2i &c, std::vector<CUnit *> &ring)
{
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			if (dx || dy) {
				ring.push_back(&w.Place(w.peasant, c + Vec2i(dx, dy), 0));
			}
		}
	}
}

struct Depot78
{
	int endedAt = -1;  /// cycle the Return order ended
	int held = -1;     /// load after it ended
	bool moved = false;
	bool resumed = false; /// a resource order again after the ring opened
};

/// A loaded worker boxed by 8 idle units gets a Return order to the hall; a ring unit leaves at cycle 40.
Depot78 RunDepot78(int resource, bool rule)
{
	DepotClosedDropsOrder = rule;
	TrafficWorld w;
	AddWood(w);
	w.Buildings({6, 40}, {10, 10});
	const Vec2i start(30, 30);
	std::vector<CUnit *> ring;
	Ring(w, start, ring);
	CUnit &u = w.Place(w.peasant, start, 0);
	u.CurrentResource = resource;
	u.ResourcesHeld = 100;
	u.Orders.clear();
	u.Orders.push_back(COrder::NewActionReturnGoods(u, w.hall));
	u.pathFinderData->output.Length = 0;
	Depot78 r;
	for (int cycle = 0; cycle != 400; ++cycle) {
		++GameCycle;
		w.mine->VisCount[0] = 1;
		w.hall->VisCount[0] = 1;
		if (cycle == 40) {
			ring.front()->Remove(nullptr);
		}
		if (u.Orders[0]->Finished) {
			u.Orders[0] = COrder::NewActionStill();
			if (r.endedAt < 0) {
				r.endedAt = cycle;
				r.held = u.ResourcesHeld;
			}
		}
		if (u.CurrentAction() == UnitAction::Resource) {
			if (r.endedAt >= 0) {
				r.resumed = true;
			}
			u.Orders[0]->Execute(u);
		}
		if (u.tilePos != start) {
			r.moved = true;
		}
	}
	u.Orders.clear();
	DepotClosedDropsOrder = true;
	return r;
}

struct Ringed78
{
	bool wood = false;
	Vec2i tree{-1, -1};
	int turnedAt = -1;
};

/// A gold mine (30..32 x 30..32) whose 16 free tiles are held by idle units; forest west of it
/// (25..26 x 24..38) and nearer to the worker (52..56 x 18..20); the worker starts at 50,31.
Ringed78 RunRinged78(bool rule)
{
	GoldHarvestTurnsToWood = rule;
	TrafficWorld w;
	AddWood(w);
	w.Forest(25, 24, 26, 38);
	w.Forest(52, 18, 56, 20);
	w.Buildings({30, 30}, {6, 50});
	for (int y = 29; y <= 33; ++y) {
		for (int x = 29; x <= 33; ++x) {
			if (x == 29 || x == 33 || y == 29 || y == 33) {
				w.Place(w.peasant, {x, y}, 0);
			}
		}
	}
	CUnit &u = w.Place(w.peasant, {50, 31}, 0);
	u.Orders.clear();
	u.Orders.push_back(COrder::NewActionResource(u, *w.mine));
	u.pathFinderData->output.Length = 0;
	Ringed78 r;
	for (int cycle = 0; cycle != 60 && !r.wood; ++cycle) {
		++GameCycle;
		w.mine->VisCount[0] = 1;
		w.hall->VisCount[0] = 1;
		if (u.Orders[0]->Finished) {
			if (u.Orders.size() > 1) {
				u.Orders.erase(u.Orders.begin());
			} else {
				u.Orders[0] = COrder::NewActionStill();
			}
		}
		if (u.CurrentAction() == UnitAction::Resource) {
			u.Orders[0]->Execute(u);
		}
		for (const auto &o : u.Orders) {
			if (o->Action == UnitAction::Resource && !o->Finished
			    && static_cast<const COrder_Resource &>(*o).GetCurrentResource() == WoodCost) {
				r.wood = true;
				r.turnedAt = cycle;
				r.tree = static_cast<const COrder_Resource &>(*o).GetGoalPos();
			}
		}
	}
	u.Orders.clear();
	GoldHarvestTurnsToWood = true;
	return r;
}
} // namespace

TEST_CASE("harvest 78: a loaded worker whose way home is closed by standing units drops the order and keeps the load")
{
	for (int res : {int(GoldCost), int(WoodCost)}) {
		CAPTURE(res);
		const Depot78 on = RunDepot78(res, true);
		MESSAGE("res " << res << ": ended at " << on.endedAt << ", held " << on.held << ", moved " << on.moved
		        << ", resumed " << on.resumed);
		CHECK(on.endedAt >= 0);
		CHECK(on.endedAt <= 3); // live: 3 frames after the return began (7 of 7)
		CHECK(on.held == 100);  // the load is kept (live: later Return orders, delivered when open)
		CHECK_FALSE(on.moved);  // stays idle when the ring opens (live 7 of 7)
		CHECK_FALSE(on.resumed);
		const Depot78 off = RunDepot78(res, false);
		MESSAGE("res " << res << ", rule off: ended at " << off.endedAt << ", held " << off.held);
		CHECK(off.endedAt != on.endedAt); // ours before: retried (ring open at 40: walked home, or gave up later)
	}
}

TEST_CASE("harvest 78: a gold mine ringed by idle units - the harvester turns to wood near the mine")
{
	const Ringed78 on = RunRinged78(true);
	MESSAGE("ringed mine: wood " << on.wood << " at cycle " << on.turnedAt << ", tree " << on.tree.x << "," << on.tree.y);
	CHECK(on.wood);
	CHECK(on.turnedAt <= 2);
	CHECK(on.tree.x <= 26); // the forest beside the mine, not the one nearer to the worker (live M1 / M2)
	CHECK(on.tree.x >= 25);
	const Ringed78 off = RunRinged78(false);
	CHECK_FALSE(off.wood); // ours before (S20+ 3.4.86): kept the gold order and stood
}

TEST_CASE("harvest 78: a boxed wood harvester walks the cycle after a ring unit leaves")
{
	int worstOn = 0;
	int worstOff = 0;
	for (int open = 200; open != 210; ++open) {
		CAPTURE(open);
		const Turn77 on = RunTurn77(false, true, open);
		CHECK(on.wood);
		CHECK(on.firstStepAfterOpen >= 0);
		worstOn = std::max(worstOn, on.firstStepAfterOpen);
		WoodBoxedLooksEachCycle = false;
		const Turn77 off = RunTurn77(false, true, open);
		WoodBoxedLooksEachCycle = true;
		worstOff = std::max(worstOff, off.firstStepAfterOpen);
	}
	MESSAGE("first step after the opening, worst of 10 phases: " << worstOn << " (rule off " << worstOff << ")");
	CHECK(worstOn <= 2); // live 1 frame (4 of 4)
	CHECK(worstOff > worstOn);
}
