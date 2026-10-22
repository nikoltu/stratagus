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
/**@name astar.cpp - The a* path finder routines. */
//
//      (c) Copyright 1999-2008 by Lutz Sammer, Fabrice Rossi, Russell Smith,
//                                 Francois Beerten, Jimmy Salmon.
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

// timfel: How to debug the pathfinder.
// In debug builds, there is a lua function "DumpNextAStar". The way I use this
// is to enter a game with stdout redirected to a file (that's the default on
// Windows), make sure nothing is moving, then hit "Return/Enter" to write a
// cheat code, and then type "eval DumpNextAStar()". Then send a unit somewhere.
// Next, open the stdout file using "less -R -z64 stdout.txt". Replace the "64"
// with the height of your map. The entire map will have been dumped using RGB colors
// to stdout. I use the arrow keys to position the first line of the first A*
// iteration at the top of my terminal, and then hit "Space" repeatedly. If the
// "-z64" argument used the correct map height and the terminal is wide enough,
// each time I hit space, it will scroll exactly far enough to have the next A*
// iteration first line at the top of my terminal, giving a sort of poor animation.

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "map.h"
#include "pathfinder.h"
#include "settings.h"
#include "stratagus.h"
#include "tileset.h"
#include "actions.h"
#include "unit.h"
#include "unit_find.h"

#if defined(DEBUG_ASTAR)
# include "font.h"
# include "viewport.h"
#endif

#include <cstdio>

/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/

struct Node {
	int32_t GetCostFromStart() const;
	void SetCostFromStart(uint64_t cost);
	int32_t GetCostToGoal() const;
	void SetCostToGoal(uint64_t cost);
	bool IsInGoal() const;
	void SetInGoal();
	uint8_t GetDirection() const;
	void SetDirection(uint8_t dir);
private:
	int32_t CostFromStart = 0; /// Real costs to reach this point
	uint16_t CostToGoal = 0;   /// Estimated cost to goal
	int8_t InGoal = 0;         /// is this point in the goal
	int8_t Direction = 0;      /// Direction for trace back
};

struct Open {
	uint32_t GetCosts() const;
	void SetCosts(uint64_t costs);
	uint32_t GetOffset() const;
	Vec2i pos;
private:
	uint32_t Costs; /// complete costs to goal
};

/// Tiles covered by one path element of the unit the current search is for (CUnitType::GridStep).
/// Such a unit visits only the tiles a whole number of steps away from its start tile.
static int AStarStep = 1;
/// Start tile of the current search
static Vec2i AStarStart;
/// The current search is for a transporter on a grid: it may also stand on any coast tile and
/// takes one-tile steps (PathShortStep) onto, along and off coast tiles.
static bool AStarDocker = false;

/// Tile a unit on the grid of the current search can stand on, by its coordinates alone
static inline bool AStarOnGrid(int x, int y)
{
	if (AStarDocker) {
		return !(x % AStarStep) && !(y % AStarStep);
	}
	return !((x - AStarStart.x) % AStarStep) && !((y - AStarStart.y) % AStarStep);
}

/// The goal area of the running search (AStarFindPath): size, range, the unit's size.
static int AStarGoalW = 1;
static int AStarGoalH = 1;
static int AStarGoalRange = 0;
static int AStarUnitW = 1;
static int AStarUnitH = 1;
/// The cheapest step of the running search: 1 + the least tile move cost (1 on a map from a tileset:
/// 2; the bare fields of host-test maps have 0: 1).
static int AStarStepCost = 2;

/**
**  Heuristic cost function for A*: the steps still to go at the least step cost of the game.
**
**  A step costs 1 + the tile's move cost (1 on plain land, CostMoveToCallBack_Default): 2. The
**  estimate is the Chebyshev distance from the unit to the goal area less the range, in steps of
**  the unit's grid, x the cheapest step (AStarStepCost) - never more than the real cost, so the
**  search returns the shortest way.
**  The old estimate (Manhattan to the goal's top-left tile x 8, up to 4-8 x the real cost) made
**  the search greedy: live run 68 the Peasants left the hall's west side (29,43) into the lane
**  with a first step east (29,43 -> 30,44, about 11 steps to the mine; 55 of 69 legs); the greedy
**  search ran west along the forest and took the way round (26 steps): host "lane 68" 44 of 96
**  legs round (work/PATHFINDING_2026-10-21.md C7, work/CALIB_RUN68.md).
*/
static inline int AStarCosts(const Vec2i &pos, const Vec2i &goalPos)
{
	const int dx = std::max({0, goalPos.x - (pos.x + AStarUnitW - 1), pos.x - (goalPos.x + AStarGoalW - 1)});
	const int dy = std::max({0, goalPos.y - (pos.y + AStarUnitH - 1), pos.y - (goalPos.y + AStarGoalH - 1)});
	const int tiles = std::max(0, std::max(dx, dy) - AStarGoalRange);
	return AStarStepCost * ((tiles + AStarStep - 1) / AStarStep);
}

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

//  Convert heading into direction.
//                      //  N NE  E SE  S SW  W NW
const int Heading2X[9] = {  0, +1, +1, +1, 0, -1, -1, -1, 0 };
const int Heading2Y[9] = { -1, -1, 0, +1, +1, +1, 0, -1, 0 };
int Heading2O[9];//heading to offset
const int XY2Heading[3][3] = { {7, 6, 5}, {0, 0, 4}, {1, 2, 3}};

/// cost matrix
static std::vector<Node> AStarMatrix;

/// a list of close nodes, helps to speed up the matrix cleaning
#define MAX_CLOSE_SET_RATIO 4
#define MAX_OPEN_SET_RATIO 8 // 10,16 to small

/// see pathfinder.h
int AStarFixedUnitCrossingCost;// = MaxMapWidth * MaxMapHeight;
int AStarMovingUnitCrossingCost = 5;
/// 0: the budget follows the map (AStarSearchBudget). The fixed 5120 of Wargus ran out before a
/// way round a long wall or a forest serpentine on a 128 x 128 map was found: the search then
/// answered a part way to the wall, the unit walked it, searched again from there and went to
/// and fro without end (host test "path quality": 137 searches, 3000 steps, goal never reached).
int AStarMaxSearchIterations = 0;
/// Forget the regions of the map (see AStarGoalOutOfRegion)
static void AStarClearRegions();
/// Nodes generated or improved by the last search (host tests)
int AStarLastSearchIterations = 0;
/// Calls of AStarFindPath (host tests)
int AStarSearchCalls = 0;
bool AStarKnowUnseenTerrain = false;
int AStarUnknownTerrainCost = 2;
/// Used to temporary make enemy units unpassable (needs for correct path length calculating for automatic targeting algorithm)
static bool AStarFixedEnemyUnitsUnpassable = false;
/// A unit the search treats as standing (a wall) even while it moves: PathRoundUnit (live run 67 M1).
const CUnit *AStarWallUnit = nullptr;
/// PathThroughWaitingMates: own units standing on their way are crossed at a cost, not refused.
bool AStarCrossMatesOnTheWay = false;
/// The last search failed after its flood and refused a tile held by a standing unit of the
/// searching unit's player that is on its way (see AStarLastFailByUnits in pathfinder.h).
bool AStarLastFailByUnits = false;
bool AStarLastFailBoxed = false;
bool AStarLastFailGoalHeldForGood = false;
/// A unit standing for good (not on its way) was refused by the running search.
static bool AStarRefusedUnitForGood = false;
/// Why the last AStarFindPath failed (see pathfinder.h, AStarFailReason).
int AStarLastFailReason = 0;
int AStarBudgetOuts = 0;
/// A standing unit (not the searching one) was refused by the running search.
static bool AStarRefusedUnit = false;
/// A unit refused by the running search is such a unit on its way.
static bool AStarRefusedUnitOnItsWay = false;

/// A standing unit of the same player with an order still to go (a Move to another tile, a
/// worker on its trip): it stands between two steps or waits for a step of its own, and leaves
/// its tile soon. Units that stand for good (Still, a finished order, buildings) are not.
static bool StandingUnitOnItsWay(const CUnit &unit, const CUnit &holder)
{
	if (holder.Player != unit.Player || holder.Type->Building || holder.Orders.empty()) {
		return false;
	}
	const COrder &order = *holder.CurrentOrder();
	if (order.Finished) {
		return false;
	}
	if (order.Action == UnitAction::Move) {
		return order.GetGoalPos() != holder.tilePos;
	}
	return order.Action == UnitAction::Resource;
}

static int AStarMapWidth;
static int AStarMapHeight;

static int AStarGoalX;
static int AStarGoalY;

/**
**  The Open set is handled by a stored array
**  the end of the array holds the item with the smallest cost.
*/

/// The set of Open nodes
static std::vector<Open> OpenSet;
/// The size of the open node set
static int OpenSetSize;

static std::vector<int32_t> CostMoveToCache;
static constexpr int CacheNotSet = -1;

/*----------------------------------------------------------------------------
--  Profile
----------------------------------------------------------------------------*/

#ifdef ASTAR_PROFILE

#include <map>
#ifdef USE_WIN32
#include <windows.h>
#else

union LARGE_INTEGER {
	uint64_t QuadPart;
	uint32_t DoublePart[2];
};
inline int QueryPerformanceCounter(LARGE_INTEGER *ptr)
{
	unsigned int lo, hi;
	__asm__ __volatile__(       // serialize
		"xorl %%eax,%%eax \n        cpuid"
		::: "%rax", "%rbx", "%rcx", "%rdx");
	/* We cannot use "=A", since this would use %rax on x86_64 */
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	ptr->DoublePart[0] = lo;
	ptr->DoublePart[1] = hi;
	return 1;
};

inline int QueryPerformanceFrequency(LARGE_INTEGER *ptr)
{
	ptr->QuadPart = 1000;
	return 1;
}

#endif

#undef max
#undef min
static std::map<const char *const, LARGE_INTEGER> functionTimerMap;
struct ProfileData {
	unsigned long Calls;
	unsigned long TotalTime;
};
static std::map<const char *const, ProfileData> functionProfiles;

inline void ProfileInit()
{
	functionTimerMap.clear();
	functionProfiles.clear();
}

inline void ProfileBegin(const char *const function)
{
	LARGE_INTEGER counter;
	if (!QueryPerformanceCounter(&counter)) {
		return;
	}
	functionTimerMap[function] = counter;
}

inline void ProfileEnd(const char *const function)
{
	LARGE_INTEGER counter;
	if (!QueryPerformanceCounter(&counter)) {
		return;
	}
	unsigned long time = (unsigned long)(counter.QuadPart - functionTimerMap[function].QuadPart);
	ProfileData *data = &functionProfiles[function];
	data->Calls++;
	data->TotalTime += time;
}

static bool compProfileData(const ProfileData *lhs, const ProfileData *rhs)
{
	return (lhs->TotalTime > rhs->TotalTime);
}


inline void ProfilePrint()
{
	LARGE_INTEGER frequency;
	if (!QueryPerformanceFrequency(&frequency)) {
		return;
	}
	std::vector<ProfileData *> prof;
	for (auto &[key, data] : functionProfiles) {
		prof.insert(ranges::upper_bound(prof, &data, compProfileData), &data);
	}

	FILE *fd = fopen("profile.txt", "wb");
	fprintf(fd, "    total\t    calls\t      per\tname\n");

	for (ProfileData *data : prof) {
		fprintf(fd, "%9.3f\t%9lu\t%9.3f\t",
				(double)data->TotalTime / frequency.QuadPart * 1000.0,
				data->Calls,
				(double)data->TotalTime / frequency.QuadPart * 1000.0 / data->Calls);
		for (const auto &[key, data2] : functionProfiles) {
			if (data == &data2) {
				fprintf(fd, "%s\n", key);
			}
		}
	}

	fclose(fd);
}

#else
#define ProfileInit()
#define ProfileBegin(f)
#define ProfileEnd(f)
#define ProfilePrint()
#endif

/*----------------------------------------------------------------------------
--  Methods
----------------------------------------------------------------------------*/
int32_t Node::GetCostFromStart() const {
	return this->CostFromStart;
}

void Node::SetCostFromStart(uint64_t cost) {
	if (cost > INT32_MAX) {
		this->CostFromStart = INT32_MAX;
	} else {
		this->CostFromStart = cost;
	}
}

int32_t Node::GetCostToGoal() const {
	return this->CostToGoal;
}

void Node::SetCostToGoal(uint64_t cost) {
	if (cost > UINT16_MAX) {
		this->CostToGoal = UINT16_MAX;
	} else {
		this->CostToGoal = cost;
	}
}

bool Node::IsInGoal() const {
	return this->InGoal;
}

void Node::SetInGoal() {
	this->InGoal = 1;
}

uint8_t Node::GetDirection() const {
	return this->Direction;
}

void Node::SetDirection(uint8_t dir) {
	this->Direction = dir;
}

uint32_t Open::GetCosts() const {
	return this->Costs;
}

void Open::SetCosts(uint64_t costs) {
	if (costs > UINT32_MAX) {
		this->Costs = UINT32_MAX;
	} else {
		this->Costs = costs;
	}
}

uint32_t Open::GetOffset() const {
	return pos.y * AStarMapWidth + pos.x;
}

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

/**
**  Init A* data structures
*/
void InitAStar(int mapWidth, int mapHeight)
{
	// Should only be called once
	Assert(AStarMatrix.empty());

	AStarMapWidth = mapWidth;
	AStarMapHeight = mapHeight;

	AStarMatrix.resize(AStarMapWidth * AStarMapHeight);
#ifdef DEBUG
	for (auto& node : AStarMatrix) {
		node.SetDirection(-1);
	}
#endif
	// A tile is at most once in the open set: room for every tile, so a search with the budget of
	// the map never ends on "raise Open Set Max Size" (the old 1/8 of the map did not hold it).
	OpenSet.resize(AStarMapWidth * AStarMapHeight + 1);
	AStarClearRegions();
	CostMoveToCache.resize(AStarMapWidth * AStarMapHeight, CacheNotSet);

	for (int i = 0; i < 9; ++i) {
		Heading2O[i] = Heading2Y[i] * AStarMapWidth;
	}

	ProfileInit();
}

/**
**  Free A* data structure
*/
void FreeAStar()
{
	AStarMatrix.clear();
	OpenSet.clear();
	AStarClearRegions();
	OpenSetSize = 0;
	CostMoveToCache.clear();

	ProfilePrint();
}

/**
**  Prepare pathfinder.
*/
static void AStarPrepare()
{
	ranges::fill(AStarMatrix, Node{});
#ifdef DEBUG
	for (auto& node : AStarMatrix) {
		node.SetDirection(-1);
	}
#endif
}

/**
**  Clean up A*
*/
static void CostMoveToCacheCleanUp();
static void AStarCleanUp()
{
	ProfileBegin("AStarCleanUp");
	AStarPrepare();
	CostMoveToCacheCleanUp();
	ProfileEnd("AStarCleanUp");
}

static void CostMoveToCacheCleanUp()
{
	ranges::fill(CostMoveToCache, CacheNotSet);
}

/**
**  Find the best node in the current open node set
**  Returns the position of this node in the open node set
*/
#define AStarFindMinimum() (OpenSetSize - 1)


/**
**  Remove the minimum from the open node set
*/
static void AStarRemoveMinimum(int pos)
{
	Assert(pos == OpenSetSize - 1);

	OpenSetSize--;
}

/**
**  Add a new node to the open set (and update the heap structure)
**
**  @return  0 or PF_FAILED
*/
static inline int AStarAddNode(const Vec2i &pos, int64_t costs)
{
	ProfileBegin("AStarAddNode");

	int32_t bigi = 0, smalli = OpenSetSize;
	int32_t midi;
	uint32_t midcost;
	int32_t midCostToGoal;
	int32_t midDist;
	const Open *open;

	if (OpenSetSize + 1 >= OpenSet.size()) {
		ErrorPrint("A* internal error: raise Open Set Max Size (current value %d)\n",
		           (int)OpenSet.size());
		ProfileEnd("AStarAddNode");
		return PF_FAILED;
	}

	// Ties of the whole cost go to the node nearer the goal: its own estimate, which every caller
	// stores before adding it. (It was the whole cost itself, always above the estimate of the node
	// it was compared with: a tie went behind all nodes of the same cost, first in first out.)
	const int costToGoal = AStarMatrix[pos.y * AStarMapWidth + pos.x].GetCostToGoal();
	const int dist = std::abs(pos.x - AStarGoalX) + std::abs(pos.y - AStarGoalY);

	// find where we should insert this node.
	// binary search where to insert the new node
	while (bigi < smalli) {
		midi = (smalli + bigi) >> 1;
		open = &OpenSet[midi];
		midcost = open->GetCosts();
		midCostToGoal = AStarMatrix[open->GetOffset()].GetCostToGoal();
		midDist = std::abs(open->pos.x - AStarGoalX) + std::abs(open->pos.y - AStarGoalY);
		if (costs > midcost || (costs == midcost
								&& (costToGoal > midCostToGoal || (costToGoal == midCostToGoal
																   && dist > midDist)))) {
			smalli = midi;
		} else if (costs < midcost || (costs == midcost
									   && (costToGoal < midCostToGoal || (costToGoal == midCostToGoal
											   && dist < midDist)))) {
			if (bigi == midi) {
				bigi++;
			} else {
				bigi = midi;
			}
		} else {
			bigi = midi;
			smalli = midi;
		}
	}

	if (OpenSetSize > bigi) {
		// free a the slot for our node
		memmove(&OpenSet[bigi + 1], &OpenSet[bigi], (OpenSetSize - bigi) * sizeof(Open));
	}

	// fill our new node
	OpenSet[bigi].pos = pos;
	OpenSet[bigi].SetCosts(costs);
	++OpenSetSize;

	ProfileEnd("AStarAddNode");

	return 0;
}

/**
**  Change the cost associated to an open node.
**  Can be further optimized knowing that the new cost MUST BE LOWER
**  than the old one.
*/
static void AStarReplaceNode(int pos, int64_t costs)
{
	ProfileBegin("AStarReplaceNode");

	Open node;

	// Remove the outdated node
	node = OpenSet[pos];
	OpenSetSize--;
	memmove(&OpenSet[pos], &OpenSet[pos+1], sizeof(Open) * (OpenSetSize-pos));

	// Re-add the node with the new cost (it was re-added with its old cost, so a node reached by a
	// cheaper way kept the place of the dearer one in the open set)
	AStarAddNode(node.pos, costs);
	ProfileEnd("AStarReplaceNode");
}


/**
**  Check if a node is already in the open set.
**
**  @return  -1 if not found and the position of the node in the table if found.
*/
static int AStarFindNode(int eo)
{
	ProfileBegin("AStarFindNode");

	for (int i = 0; i < OpenSetSize; ++i) {
		if (OpenSet[i].GetOffset() == eo) {
			ProfileEnd("AStarFindNode");
			return i;
		}
	}
	ProfileEnd("AStarFindNode");
	return -1;
}

#define GetIndex(x, y) (x) + (y) * AStarMapWidth

/* build-in costmoveto code */
static int CostMoveToCallBack_Default(unsigned int index, const CUnit &unit)
{
#ifdef DEBUG
	{
		Vec2i pos;
		pos.y = index / Map.Info.MapWidth;
		pos.x = index - pos.y * Map.Info.MapWidth;
		Assert(Map.Info.IsPointOnMap(pos));
	}
#endif
	int cost = 0;
	const int mask = unit.Type->MovementMask;
	const CUnitTypeFinder unit_finder(unit.Type->MoveType);

	// verify each tile of the unit.
	int h = unit.Type->TileHeight;
	const int w = unit.Type->TileWidth;
	do {
		const CMapField *mf = Map.Field(index);
		int i = w;
		do {
			const int flag = mf->Flags & mask;
			if (flag && (AStarKnowUnseenTerrain || mf->playerInfo.IsExplored(*unit.Player))) {
				if (flag & ~(MapFieldLandUnit | MapFieldAirUnit | MapFieldSeaUnit)) {
					// we can't cross fixed units and other unpassable things
#ifdef DEBUG
					const_cast<CMapField *>(mf)->lastAStarCost = -1;
#endif
					return -1;
				}
				auto it = ranges::find_if(mf->UnitCache, unit_finder);
				CUnit *goal = it != mf->UnitCache.end() ? *it : nullptr;
				if (!goal) {
					// Shouldn't happen, mask says there is something on this tile
					Assert(0);
#ifdef DEBUG
					const_cast<CMapField *>(mf)->lastAStarCost = -1;
#endif
					return -1;
				}
				if (goal->Moving && goal != AStarWallUnit) {
					// moving unit are crossable
					cost += AStarMovingUnitCrossingCost;
				} else {
					// for non moving unit Always Fail unless goal is unit, or unit can attack the target
					if (&unit != goal) {
						if (GetAStarFixedEnemyUnitsUnpassable() == true) {
#ifdef DEBUG
							const_cast<CMapField *>(mf)->lastAStarCost = -1;
#endif
							return -1;
						}
						if (goal->Player->IsEnemy(unit) && unit.IsAggressive() && CanTarget(*unit.Type, *goal->Type)
							&& goal->Variable[UNHOLYARMOR_INDEX].Value == 0 && goal->IsVisibleAsGoal(*unit.Player)) {
								cost += 2 * AStarMovingUnitCrossingCost;
						} else if (AStarCrossMatesOnTheWay && goal != AStarWallUnit && StandingUnitOnItsWay(unit, *goal)) {
							// (PathHasWayRound: the wall unit itself is never crossed)
							// PathThroughWaitingMates: an own unit waiting on its way is crossed at
							// twice the moving cost (UNMEASURED, round 3.4.81 head-on deadlock).
							cost += 2 * AStarMovingUnitCrossingCost;
						} else {
							AStarRefusedUnit = true;
							if (StandingUnitOnItsWay(unit, *goal)) {
								AStarRefusedUnitOnItsWay = true;
							} else {
								AStarRefusedUnitForGood = true;
							}
						// FIXME: Need support for moving a fixed unit to add cost
#ifdef DEBUG
							const_cast<CMapField *>(mf)->lastAStarCost = -1;
#endif
							return -1;
						}
						//cost += AStarFixedUnitCrossingCost;
					}
				}
			}
			// Add cost of crossing unknown tiles if required
			if (!AStarKnowUnseenTerrain && !mf->playerInfo.IsExplored(*unit.Player)) {
				// Tend against unknown tiles.
				cost += AStarUnknownTerrainCost;
			}
			// Add tile movement cost
			cost += mf->getMoveCost();
#ifdef DEBUG
			const_cast<CMapField *>(mf)->lastAStarCost = cost;
#endif
			++mf;
		} while (--i);
		index += AStarMapWidth;
	} while (--h);
	return cost / (unit.Type->TileWidth * unit.Type->TileHeight);
}


/**
**  Compute the cost of crossing tile (x,y)
**
**  @param x     X tile where to move.
**  @param y     Y tile where to move.
**  @param data  user data.
**
**  @return      -1 -> impossible to cross.
**                0 -> no induced cost, except move
**               >0 -> costly tile
*/
static inline int CostMoveTo(unsigned int index, const CUnit &unit)
{
	int32_t *c = &CostMoveToCache[index];
	if (*c != CacheNotSet) {
		// for performance reasons, CostMoveToCache uses -1 to
		// indicate it is unset, but the algorithm is simpler
		// if the range of costs is [-1, INT_MAX]. so we always
		// store everything +1
		return *c - 1;
	}
	*c = CostMoveToCallBack_Default(index, unit) + 1;
#ifdef DEBUG
	Assert(*c >= 0);
#endif
	return *c - 1;
}

class AStarGoalMarker
{
public:
	AStarGoalMarker(const CUnit &unit) : unit(unit) {}

	void operator()(int offset)
	{
		if (AStarStep > 1) {
			// only tiles the unit can stand on
			const int x = offset % AStarMapWidth;
			const int y = offset / AStarMapWidth;
			if (!AStarOnGrid(x, y) && !(AStarDocker && Map.Field(offset)->CoastOnMap())) {
				return;
			}
		}
		if (CostMoveTo(offset, unit) >= 0) {
			AStarMatrix[offset].SetInGoal();
			goal_reachable = true;
		}
	}

	bool isGoalReachable() const { return goal_reachable; }

private:
	const CUnit &unit;
	bool goal_reachable = false;
};


template <typename T>
class MinMaxRangeVisitor
{
public:
	explicit MinMaxRangeVisitor(T &func) : func(func) {}

	void SetGoal(Vec2i goalTopLeft, Vec2i goalBottomRight)
	{
		this->goalTopLeft = goalTopLeft;
		this->goalBottomRight = goalBottomRight;
	}

	void SetRange(int minrange, int maxrange)
	{
		this->minrange = minrange;
		this->maxrange = maxrange;
	}

	/// The max range is the larger axis gap to the goal: its area is a square, not a circle
	void SetSquareRange(bool squareRange) { this->squareRange = squareRange; }

	void SetUnitSize(const Vec2i &tileSize)
	{
		this->unitExtraTileSize.x = tileSize.x - 1;
		this->unitExtraTileSize.y = tileSize.y - 1;
	}

	void Visit()
	{
		TopHemicycle();
		TopHemicycleNoMinRange();
		Center();
		BottomHemicycleNoMinRange();
		BottomHemicycle();
	}

private:
	int GetMaxOffsetX(int dy, int range) const
	{
		return isqrt(square(range + 1) - square(dy) - 1);
	}

	int GetMaxRangeOffsetX(int dy) const
	{
		return squareRange ? maxrange : GetMaxOffsetX(dy, maxrange);
	}

	// Distance are computed between bottom of unit and top of goal
	void TopHemicycle()
	{
		const int miny = std::max(0, goalTopLeft.y - maxrange - unitExtraTileSize.y);
		const int maxy = std::min(goalTopLeft.y - minrange - unitExtraTileSize.y, goalTopLeft.y - 1 - unitExtraTileSize.y);
		for (int y = miny; y <= maxy; ++y) {
			const int offsetx = GetMaxRangeOffsetX(y - goalTopLeft.y);
			const int minx = std::max(0, goalTopLeft.x - offsetx - unitExtraTileSize.x);
			const int maxx = std::min(Map.Info.MapWidth - 1 - unitExtraTileSize.x, goalBottomRight.x + offsetx);
			Vec2i mpos(minx, y);
			const unsigned int offset = mpos.y * Map.Info.MapWidth;

			for (mpos.x = minx; mpos.x <= maxx; ++mpos.x) {
				func(offset + mpos.x);
			}
		}
	}

	void HemiCycleRing(int y, int offsetminx, int offsetmaxx)
	{
		const int minx = std::max(0, goalTopLeft.x - offsetmaxx - unitExtraTileSize.x);
		const int maxx = std::min(Map.Info.MapWidth - 1 - unitExtraTileSize.x, goalBottomRight.x + offsetmaxx);
		Vec2i mpos(minx, y);
		const unsigned int offset = mpos.y * Map.Info.MapWidth;

		for (mpos.x = minx; mpos.x <= goalTopLeft.x - offsetminx - unitExtraTileSize.x; ++mpos.x) {
			func(offset + mpos.x);
		}
		for (mpos.x = goalBottomRight.x + offsetminx; mpos.x <= maxx; ++mpos.x) {
			func(offset + mpos.x);
		}
	}

	void TopHemicycleNoMinRange()
	{
		const int miny = std::max(0, goalTopLeft.y - (minrange - 1) - unitExtraTileSize.y);
		const int maxy = goalTopLeft.y - 1 - unitExtraTileSize.y;
		for (int y = miny; y <= maxy; ++y) {
			const int offsetmaxx = GetMaxRangeOffsetX(y - goalTopLeft.y);
			const int offsetminx = GetMaxOffsetX(y - goalTopLeft.y, minrange - 1) + 1;

			HemiCycleRing(y, offsetminx, offsetmaxx);
		}
	}

	void Center()
	{
		const int miny = std::max(0, goalTopLeft.y - unitExtraTileSize.y);
		const int maxy = std::min<int>(Map.Info.MapHeight - 1 - unitExtraTileSize.y, goalBottomRight.y);
		const int minx = std::max(0, goalTopLeft.x - maxrange - unitExtraTileSize.x);
		const int maxx = std::min<int>(Map.Info.MapWidth - 1 - unitExtraTileSize.x, goalBottomRight.x + maxrange);

		if (minrange == 0) {
			for (int y = miny; y <= maxy; ++y) {
				Vec2i mpos(minx, y);
				const unsigned int offset = mpos.y * Map.Info.MapWidth;

				for (mpos.x = minx; mpos.x <= maxx; ++mpos.x) {
					func(offset + mpos.x);
				}
			}
		} else {
			for (int y = miny; y <= maxy; ++y) {
				Vec2i mpos(minx, y);
				const unsigned int offset = mpos.y * Map.Info.MapWidth;

				for (mpos.x = minx; mpos.x <= goalTopLeft.x - minrange - unitExtraTileSize.x; ++mpos.x) {
					func(offset + mpos.x);
				}
				for (mpos.x = goalBottomRight.x + minrange; mpos.x <= maxx; ++mpos.x) {
					func(offset + mpos.x);
				}
			}
		}
	}

	void BottomHemicycleNoMinRange()
	{
		const int miny = goalBottomRight.y + 1;
		const int maxy = std::min(Map.Info.MapHeight - 1 - unitExtraTileSize.y, goalBottomRight.y + (minrange - 1));

		for (int y = miny; y <= maxy; ++y) {
			const int offsetmaxx = GetMaxRangeOffsetX(y - goalBottomRight.y);
			const int offsetminx = GetMaxOffsetX(y - goalBottomRight.y, minrange - 1) + 1;

			HemiCycleRing(y, offsetminx, offsetmaxx);
		}
	}

	void BottomHemicycle()
	{
		const int miny = std::max(goalBottomRight.y + minrange, goalBottomRight.y + 1);
		const int maxy = std::min(Map.Info.MapHeight - 1 - unitExtraTileSize.y, goalBottomRight.y + maxrange);
		for (int y = miny; y <= maxy; ++y) {
			const int offsetx = GetMaxRangeOffsetX(y - goalBottomRight.y);
			const int minx = std::max(0, goalTopLeft.x - offsetx - unitExtraTileSize.x);
			const int maxx = std::min(Map.Info.MapWidth - 1 - unitExtraTileSize.x, goalBottomRight.x + offsetx);
			Vec2i mpos(minx, y);
			const unsigned int offset = mpos.y * Map.Info.MapWidth;

			for (mpos.x = minx; mpos.x <= maxx; ++mpos.x) {
				func(offset + mpos.x);
			}
		}
	}

private:
	T& func;
	Vec2i goalTopLeft;
	Vec2i goalBottomRight;
	Vec2i unitExtraTileSize;
	int minrange = 0;
	int maxrange = 0;
	bool squareRange = false;
};

/**
**  MarkAStarGoal
*/
static bool AStarMarkGoal(const Vec2i &goal,
                          int gw,
                          int gh,
                          int tilesizex,
                          int tilesizey,
                          int minrange,
                          int maxrange,
                          const CUnit &unit,
                          bool squareRange)
{
	ProfileBegin("AStarMarkGoal");

	if (minrange == 0 && maxrange == 0 && gw == 0 && gh == 0) {
		if (goal.x + tilesizex > AStarMapWidth || goal.y + tilesizey > AStarMapHeight) {
			ProfileEnd("AStarMarkGoal");
			return false;
		}
		unsigned int offset = GetIndex(goal.x, goal.y);
		if (CostMoveTo(offset, unit) >= 0) {
			AStarMatrix[offset].SetInGoal();
			ProfileEnd("AStarMarkGoal");
			return true;
		} else {
			ProfileEnd("AStarMarkGoal");
			return false;
		}
	}

	gw = std::max(gw, 1);
	gh = std::max(gh, 1);

	AStarGoalMarker aStarGoalMarker(unit);
	MinMaxRangeVisitor<AStarGoalMarker> visitor(aStarGoalMarker);

	const Vec2i goalBottomRigth(goal.x + gw - 1, goal.y + gh - 1);
	visitor.SetGoal(goal, goalBottomRigth);
	visitor.SetRange(minrange, maxrange);
	visitor.SetSquareRange(squareRange);
	const Vec2i tileSize(tilesizex, tilesizey);
	visitor.SetUnitSize(tileSize);

	const Vec2i extratilesize(tilesizex - 1, tilesizey - 1);

	visitor.Visit();

	ProfileEnd("AStarMarkGoal");
	return aStarGoalMarker.isGoalReachable();
}

/**
**  Save the path
**
**  @return  The length of the path
*/
static int AStarSavePath(const Vec2i &startPos, const Vec2i &endPos, char *path, int pathLen)
{
	ProfileBegin("AStarSavePath");

	int fullPathLength;
	int pathPos;
	int direction;

	// Figure out the full path length
	fullPathLength = 0;
	Vec2i curr = endPos;
	int currO = curr.y * AStarMapWidth;
	while (curr != startPos) {
		direction = AStarMatrix[currO + curr.x].GetDirection();
#ifdef DEBUG
		Assert(direction >= 0 && direction != 8);
#endif
		const int length = PathElementLength(direction, AStarStep);
		curr.x -= Heading2X[PathElementHeading(direction)] * length;
		curr.y -= Heading2Y[PathElementHeading(direction)] * length;
		currO -= Heading2O[PathElementHeading(direction)] * length;
		fullPathLength++;
	}

	// Save as much of the path as we can
	if (path) {
		pathLen = std::min<int>(fullPathLength, pathLen);
		pathPos = fullPathLength;
		curr = endPos;
		currO = curr.y * AStarMapWidth;
		while (curr != startPos) {
			direction = AStarMatrix[currO + curr.x].GetDirection();
#ifdef DEBUG
			Assert(direction >= 0 && direction != 8);
#endif
			const int length = PathElementLength(direction, AStarStep);
			curr.x -= Heading2X[PathElementHeading(direction)] * length;
			curr.y -= Heading2Y[PathElementHeading(direction)] * length;
			currO -= Heading2O[PathElementHeading(direction)] * length;
			--pathPos;
			if (pathPos < pathLen) {
				path[pathLen - pathPos - 1] = direction;
			}
		}
	}

	ProfileEnd("AStarSavePath");
	return fullPathLength;
}

/**
**  Optimization to find a simple path
**  Check if we're at the goal or if it's 1 tile away
*/
static int AStarFindSimplePath(const Vec2i &startPos, const Vec2i &goal, int gw, int gh,
							   int, int, int minrange, int maxrange,
							   char *path, const CUnit &unit, bool squareRange)
{
	ProfileBegin("AStarFindSimplePath");
	// At exact destination point already
	if (goal == startPos && minrange == 0) {
		ProfileEnd("AStarFindSimplePath");
		return PF_REACHED;
	}

	// Don't allow unit inside destination area
	if (goal.x <= startPos.x && startPos.x <= goal.x + gw - 1
		&& goal.y <= startPos.y && startPos.y <= goal.y + gh - 1) {
		return PF_FAILED;
	}

	const Vec2i diff = goal - startPos;
	const int distance = isqrt(square(diff.x) + square(diff.y));
	const int maxDistance = squareRange ? std::max(std::abs(diff.x), std::abs(diff.y)) : distance;

	// Within range of destination
	if (minrange <= distance && maxDistance <= maxrange) {
		ProfileEnd("AStarFindSimplePath");
		return PF_REACHED;
	}

	if (!AStarDocker && std::abs(diff.x) <= AStarStep && std::abs(diff.y) <= AStarStep
		&& diff.x % AStarStep == 0 && diff.y % AStarStep == 0) {
		// Move to adjacent cell (one step away)
		if (CostMoveTo(GetIndex(goal.x, goal.y), unit) == -1) {
			ProfileEnd("AStarFindSimplePath");
			return PF_UNREACHABLE;
		}

		if (path) {
			path[0] = XY2Heading[diff.x / AStarStep + 1][diff.y / AStarStep + 1];
		}
		ProfileEnd("AStarFindSimplePath");
		return 1;
	}

	ProfileEnd("AStarFindSimplePath");
	return PF_FAILED;
}

/**
**  The straight way of a unit on a grid (AStarStep > 1) to a single tile.
**
**  The line runs from the start to the goal tile rounded down to the grid of the given step
**  (the unit's own, or 1 with the PathShortStep flag for a transporter beside its coast tile). With a the longer
**  and b the shorter distance in steps, each step adds b to an error term; the step is diagonal
**  when twice the error reaches a, and a is then taken off. The way ends on the first tile in
**  range of the goal tile itself.
**
**  @return  path length, or PF_FAILED when a tile of the line cannot be entered or the start is
**           off the grid (the caller then searches).
*/
static int AStarFindGridLine(const Vec2i &startPos, const Vec2i &goal, int minrange, int maxrange,
							 char *path, int pathLen, const CUnit &unit, int step, int flag)
{
	const Vec2i target(goal.x - goal.x % step, goal.y - goal.y % step);
	const Vec2i diff = target - startPos;

	if (diff.x % step || diff.y % step) {
		return PF_FAILED;
	}
	const Vec2i dir((diff.x > 0) - (diff.x < 0), (diff.y > 0) - (diff.y < 0));
	const int nx = std::abs(diff.x) / step;
	const int ny = std::abs(diff.y) / step;
	const int a = std::max(nx, ny);
	const int b = std::min(nx, ny);
	const char diagonal = XY2Heading[dir.x + 1][dir.y + 1];
	const char straight = nx >= ny ? XY2Heading[dir.x + 1][1] : XY2Heading[1][dir.y + 1];

	std::vector<char> line;
	Vec2i pos = startPos;
	int e = 0;
	bool inRange = false;
	for (int i = 0; i < a && !inRange; ++i) {
		char heading = straight;
		e += b;
		if (2 * e >= a) {
			e -= a;
			heading = diagonal;
		}
		pos.x += Heading2X[(int) heading] * step;
		pos.y += Heading2Y[(int) heading] * step;
		if (CostMoveTo(GetIndex(pos.x, pos.y), unit) < 0) {
			return PF_FAILED;
		}
		line.push_back(heading | flag);
		const Vec2i rest = goal - pos;
		const int distance = isqrt(square(rest.x) + square(rest.y));
		inRange = minrange <= distance && distance <= maxrange;
	}
	if (!inRange) {
		return PF_FAILED;
	}
	const int length = static_cast<int>(line.size());
	if (path) {
		// stored as AStarSavePath does: the first steps of the way, the next one last
		const int stored = std::min(length, pathLen);
		for (int i = 0; i < stored; ++i) {
			path[stored - 1 - i] = line[i];
		}
	}
	return length;
}

#ifdef DEBUG
extern bool DumpNextAStar;
void AStarDumpStats();
#endif

/*----------------------------------------------------------------------------
--  Regions: a goal no way can reach fails at once
----------------------------------------------------------------------------*/

// With the budget of the map (C1) a search for a goal that cannot be reached explores the whole
// region the unit stands in before it fails, up to twice the tiles of the map: 10 ms on a 128 map
// and 70 ms on a 256 map on the host, 20 units ordered there 0.25 s / 1.4 s in one cycle, and the
// Range++ retry of COrder_Move repeats it every cycle (host test "path quality: search cost of an
// unreachable goal", work/PATHFINDING_2026-10-21.md "Search cost").
//
// A region is an 8-neighbour area of tiles CostMoveTo does not refuse for their fixed flags
// (terrain, forest, rocks, walls, buildings; the units on the map left out). A way of the search
// never leaves the region of its start, whatever the units do, so a goal area without a tile of
// that region is unreachable and the search is not run. Regions are kept per movement mask. A
// "joined" answer runs the search as before, so stale regions cost at most the old flood; before an
// "apart" answer the flags are compared with a fingerprint and the regions rebuilt if anything
// changed (a tree cut, a wall or a building gone).

namespace
{
constexpr tile_flags AStarUnitFlags = MapFieldLandUnit | MapFieldAirUnit | MapFieldSeaUnit;
/// Larger goal areas (a long Range++ retry, a wide attack range) are left to the search.
constexpr int AStarRegionMaxGoalArea = 64 * 64;

struct AStarRegionMap {
	tile_flags mask = 0;       /// the fixed flags that refuse a tile (movement mask without units)
	uint64_t fingerprint = 0;  /// of the masked flags when the regions were built
	bool verify = false;       /// a search failed after a flood: compare the fingerprint first
	std::vector<int> region;   /// per tile; 0: refused by its flags
};
std::vector<AStarRegionMap> AStarRegionMaps;

enum class ERegion { Joined, Apart, Unknown };
}

static uint64_t AStarRegionFingerprint(tile_flags mask)
{
	uint64_t hash = 14695981039346656037ull ^ ((uint64_t(AStarMapWidth) << 32) | uint64_t(AStarMapHeight));
	const int count = AStarMapWidth * AStarMapHeight;
	for (int i = 0; i != count; ++i) {
		hash = (hash ^ uint64_t(Map.Field(i)->Flags & mask)) * 1099511628211ull;
	}
	return hash;
}

static void AStarBuildRegions(AStarRegionMap &regions)
{
	const int w = AStarMapWidth;
	const int h = AStarMapHeight;
	regions.region.assign(w * h, 0);
	std::vector<int> stack;
	int next = 0;
	for (int i = 0; i != w * h; ++i) {
		if (regions.region[i] != 0 || (Map.Field(i)->Flags & regions.mask)) {
			continue;
		}
		regions.region[i] = ++next;
		stack.push_back(i);
		while (!stack.empty()) {
			const int o = stack.back();
			stack.pop_back();
			const int x = o % w;
			const int y = o / w;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const int nx = x + dx;
					const int ny = y + dy;
					if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
						continue;
					}
					const int no = ny * w + nx;
					if (regions.region[no] == 0 && !(Map.Field(no)->Flags & regions.mask)) {
						regions.region[no] = next;
						stack.push_back(no);
					}
				}
			}
		}
	}
	regions.fingerprint = AStarRegionFingerprint(regions.mask);
	regions.verify = false;
}

/**
**  Whether no way at all can join the start and the goal area of a search.
**
**  The goal area taken is the rectangle round the goal and its range (a superset of the tiles
**  AStarMarkGoal marks), so "true" holds for every tile the search could end on.
*/
static bool AStarGoalOutOfRegion(const Vec2i &startPos, const Vec2i &goalPos, int gw, int gh,
								 int tilesizex, int tilesizey, int maxrange, const CUnit &unit)
{
	if (!AStarKnowUnseenTerrain) {
		// an unexplored tile is crossed whatever it holds: the regions would depend on the player
		return false;
	}
	if (AStarStep > std::min<int>(unit.Type->TileWidth, unit.Type->TileHeight)) {
		// a step longer than the unit could jump over a refused tile
		return false;
	}
	const tile_flags mask = unit.Type->MovementMask & ~AStarUnitFlags;
	if (mask == 0) {
		return false;
	}
	const int minx = std::max(0, goalPos.x - maxrange - (tilesizex - 1));
	const int miny = std::max(0, goalPos.y - maxrange - (tilesizey - 1));
	const int maxx = std::min(AStarMapWidth - 1, goalPos.x + std::max(gw, 1) - 1 + maxrange);
	const int maxy = std::min(AStarMapHeight - 1, goalPos.y + std::max(gh, 1) - 1 + maxrange);
	if (minx > maxx || miny > maxy
		|| int64_t(maxx - minx + 1) * (maxy - miny + 1) > AStarRegionMaxGoalArea) {
		return false;
	}
	auto it = ranges::find_if(AStarRegionMaps, [&](const AStarRegionMap &r) { return r.mask == mask; });
	if (it == AStarRegionMaps.end()) {
		if (AStarRegionMaps.size() >= 8) {
			AStarRegionMaps.clear();
		}
		AStarRegionMaps.emplace_back();
		it = AStarRegionMaps.end() - 1;
		it->mask = mask;
		AStarBuildRegions(*it);
	}
	AStarRegionMap &regions = *it;
	if (regions.verify) {
		regions.verify = false;
		if (AStarRegionFingerprint(mask) != regions.fingerprint) {
			AStarBuildRegions(regions);
		}
	}
	const auto decide = [&]() {
		const int own = regions.region[startPos.y * AStarMapWidth + startPos.x];
		if (own == 0) {
			return ERegion::Unknown;
		}
		for (int y = miny; y <= maxy; ++y) {
			const int *row = &regions.region[y * AStarMapWidth];
			for (int x = minx; x <= maxx; ++x) {
				if (row[x] == own) {
					return ERegion::Joined;
				}
			}
		}
		return ERegion::Apart;
	};
	ERegion answer = decide();
	if (answer == ERegion::Joined) {
		return false;
	}
	if (AStarRegionFingerprint(mask) != regions.fingerprint) {
		AStarBuildRegions(regions);
		answer = decide();
	}
	return answer == ERegion::Apart;
}

static void AStarClearRegions()
{
	AStarRegionMaps.clear();
}

/// A search failed after a flood: the regions may be older than the map (a way closed).
static void AStarVerifyRegions()
{
	for (AStarRegionMap &regions : AStarRegionMaps) {
		regions.verify = true;
	}
}

/// Nodes one search may generate or improve: the Lua value, or twice the tiles of the map
/// (a search may reach a tile again by a cheaper way).
static int AStarSearchBudget()
{
	if (AStarMaxSearchIterations > 0) {
		return AStarMaxSearchIterations;
	}
	return std::max(1024 * 5, 2 * AStarMapWidth * AStarMapHeight);
}

/**
**  Find path.
*/
int AStarFindPath(const Vec2i &startPos, const Vec2i &goalPosIn, int gw, int gh,
				  int tilesizex, int tilesizey, int minrange, int maxrange,
				  char *path, int pathlen, const CUnit &unit, bool squareRange)
{
	Assert(Map.Info.IsPointOnMap(startPos));

	ProfileBegin("AStarFindPath");
	++AStarSearchCalls;
	AStarLastFailByUnits = false;
	AStarLastFailBoxed = false;
	AStarLastFailGoalHeldForGood = false;
	AStarLastFailReason = AStarFailNone;

	AStarStep =std::max<int>(1, unit.Type->GridStep);
	AStarStart = startPos;
	// A unit on a grid cannot stand on every tile: sent to a single tile, it has arrived on a grid
	// tile within one tile less than its step of that tile.
	AStarDocker = AStarStep > 1 && unit.Type->CanTransport();
	// A transporter sent to a coast tile stands on that tile itself.
	const bool dockPoint = AStarDocker && gw == 0 && gh == 0 && minrange == 0
						   && Map.Info.IsPointOnMap(goalPosIn) && Map.Field(goalPosIn)->CoastOnMap();
	const bool gridPoint = AStarStep > 1 && gw == 0 && gh == 0 && !dockPoint;
	if (gridPoint) {
		maxrange = std::max(maxrange, AStarStep - 1);
	}

	Vec2i goalPos = goalPosIn;
	/*
	// possible optimization: never search farther than to the next N tiles
	// this loses the property of A* that it will always find a path if there is one...
	constexpr int clusterSize = 64;
	const int minMapX = std::max(0, startPos.x - clusterSize);
	const int minMapY = std::max(0, startPos.y - clusterSize);
	const int maxMapX = std::min(startPos.x + clusterSize, AStarMapWidth + 1 - tilesizex);
	const int maxMapY = std::min(startPos.y + clusterSize, AStarMapHeight + 1 - tilesizey);
	goalPos.x = std::min(maxMapX, std::max(minMapX, static_cast<int>(goalPos.x)));
	goalPos.y = std::min(maxMapY, std::max(minMapY, static_cast<int>(goalPos.y)));
	*/
	const int minMapX = 0;
	const int minMapY = 0;
	const int maxMapX = AStarMapWidth + 1 - tilesizex;
	const int maxMapY = AStarMapHeight + 1 - tilesizey;

	AStarGoalX = goalPos.x;
	AStarGoalY = goalPos.y;
	AStarGoalW = std::max(1, gw);
	AStarGoalH = std::max(1, gh);
	AStarGoalRange = std::max(0, maxrange);
	AStarUnitW = std::max(1, tilesizex);
	AStarUnitH = std::max(1, tilesizey);
	AStarStepCost = 1 + std::min(1, int(Map.Field(startPos)->getMoveCost()));

	//  Check for simple cases first
	int ret = AStarFindSimplePath(startPos, goalPos, gw, gh, tilesizex, tilesizey,
								  minrange, maxrange, path, unit, squareRange);
	if (ret != PF_FAILED) {
		if (ret == PF_UNREACHABLE) {
			AStarLastFailReason = AStarFailSimple;
		}
		ProfileEnd("AStarFindPath");
		return ret;
	}

	// No way can join the region of the unit and the goal area: fail without the flood.
	if (AStarGoalOutOfRegion(startPos, goalPos, gw, gh, tilesizex, tilesizey, maxrange, unit)) {
		AStarLastFailReason = AStarFailRegion;
		AStarLastSearchIterations = 0;
		ProfileEnd("AStarFindPath");
		return PF_UNREACHABLE;
	}

	//  Initialize
	AStarCleanUp();
	AStarRefusedUnitOnItsWay = false;
	AStarRefusedUnit = false;
	AStarRefusedUnitForGood = false;

	if (gridPoint) {
		ret = AStarFindGridLine(startPos, goalPos, minrange, maxrange, path, pathlen, unit, AStarStep, 0);
		if (ret != PF_FAILED) {
			ProfileEnd("AStarFindPath");
			return ret;
		}
	} else if (dockPoint && std::abs(goalPos.x - startPos.x) <= AStarStep
			   && std::abs(goalPos.y - startPos.y) <= AStarStep) {
		// no whole step brings it nearer: the last tiles to the coast tile are one-tile steps on the line
		ret = AStarFindGridLine(startPos, goalPos, 0, 0, path, pathlen, unit, 1, PathShortStep);
		if (ret != PF_FAILED) {
			ProfileEnd("AStarFindPath");
			return ret;
		}
	}

	OpenSetSize = 0;

	if (!AStarMarkGoal(goalPos, gw, gh, tilesizex, tilesizey, minrange, maxrange, unit, squareRange)) {
		// goal is not reachable
		ret = PF_UNREACHABLE;
		AStarLastFailReason = AStarRefusedUnitOnItsWay ? AStarFailGoalUnitsOnTheWay : AStarFailGoalFull;
		AStarLastFailGoalHeldForGood = AStarRefusedUnitForGood; // live run 78 (pathfinder.h)
		AStarLastFailByUnits = AStarRefusedUnitOnItsWay;
		ProfileEnd("AStarFindPath");
		return ret;
	}

	int eo = startPos.y * AStarMapWidth + startPos.x;
	// it is quite important to start from 1 rather than 0, because we use
	// 0 as a way to represent nodes that we have not visited yet.
	AStarMatrix[eo].SetCostFromStart(1);
	// 8 to say we are came from nowhere.
	AStarMatrix[eo].SetDirection(8);

	// place start point in open, it that failed, try another pathfinder
	int costToGoal = AStarCosts(startPos, goalPos);
	AStarMatrix[eo].SetCostToGoal(costToGoal);
	if (AStarAddNode(startPos, 1 + costToGoal) == PF_FAILED) {
		ret = PF_FAILED;
		ProfileEnd("AStarFindPath");
		return ret;
	}
	if (AStarMatrix[eo].IsInGoal()) {
		ret = PF_REACHED;
		ProfileEnd("AStarFindPath");
		return ret;
	}
	Vec2i endPos;

	const int budget = AStarSearchBudget();
	int counter = budget;
	AStarLastSearchIterations = 0;

	//  Begin search
	while (1) {
		// Find the best node of from the open set
#ifdef DEBUG
		if (DumpNextAStar) {
			AStarDumpStats();
		}
#endif
		const int shortest = AStarFindMinimum();
		const int x = OpenSet[shortest].pos.x;
		const int y = OpenSet[shortest].pos.y;
		const int o = OpenSet[shortest].GetOffset();

		AStarRemoveMinimum(shortest);

		// If we have reached the goal, then exit.
		if (AStarMatrix[o].IsInGoal()) {
			endPos.x = x;
			endPos.y = y;
			break;
		}

		// If we have looked too long, then exit.
		if (counter <= 0) {
			AStarVerifyRegions();
			++AStarBudgetOuts;
			AstarDebugPrint("way too long\n");
			ProfileEnd("AStarFindPath");
			// return current best
			// see http://theory.stanford.edu/~amitp/GameProgramming/ImplementationNotes.html#early-exit
			endPos.x = x;
			endPos.y = y;
			break;
		}

		// Generate successors of this node.

		// Node that this node was generated from.
#ifdef DEBUG
		Assert(AStarMatrix[o].GetDirection() >= 0 && (AStarMatrix[o].GetDirection() != 8 || (x == startPos.x && y == startPos.y)));
#endif
		// 8: the start tile, it came from nowhere
		const int cameBy = AStarMatrix[o].GetDirection();
		const int cameLength = cameBy == 8 ? 0 : PathElementLength(cameBy, AStarStep);
		const int px = x - Heading2X[PathElementHeading(cameBy)] * cameLength;
		const int py = y - Heading2Y[PathElementHeading(cameBy)] * cameLength;
		const bool nodeOnGrid = AStarOnGrid(x, y);

		// a transporter on a grid: the 8 whole steps, then the 8 one-tile steps
		for (int element = 0; element < (AStarDocker ? 16 : 8); ++element) {
			const int i = element & 7;
			const bool shortStep = element >= 8;
			const int length = shortStep ? 1 : AStarStep;
			if (AStarDocker && !shortStep && !nodeOnGrid) {
				continue;
			}
			endPos.x = x + Heading2X[i] * length;
			endPos.y = y + Heading2Y[i] * length;

			// Don't check the tile we came from, it's not going to be better
			// Should reduce load on A*

			if (endPos.x == px && endPos.y == py) {
				continue;
			}

			// Outside the map or can't be entered.
			if (endPos.x < minMapX || endPos.x >= maxMapX
				|| endPos.y < minMapY || endPos.y >= maxMapY) {
				continue;
			}

			//eo = GetIndex(ex, ey);
			eo = o + (Heading2X[i] + Heading2O[i]) * length;

			if (eo < 0 || eo >= CostMoveToCache.size()) {
				// inaccessible tile
				continue;
			}
			if (shortStep && !Map.Field(eo)->CoastOnMap() && (nodeOnGrid || !AStarOnGrid(endPos.x, endPos.y))) {
				// one tile only onto a coast tile, or from a tile off the grid back onto the grid
				continue;
			}
			const int direction = shortStep ? (i | PathShortStep) : i;

			// if the point is "move to"-able and
			// if we have not reached this point before,
			// or if we have a better path to it, we add it to open set
			int new_cost = CostMoveTo(eo, unit);
			if (new_cost == -1) {
				// uncrossable tile
				continue;
			}

			// Add a cost for walking to make paths more realistic for the user.
			new_cost++;
			new_cost += AStarMatrix[o].GetCostFromStart();
			if (AStarMatrix[eo].GetCostFromStart() == 0) {
				--counter;
				// we are sure the current node has not been already visited
				AStarMatrix[eo].SetCostFromStart(new_cost);
				AStarMatrix[eo].SetDirection(direction);
				costToGoal = AStarCosts(endPos, goalPos);
				AStarMatrix[eo].SetCostToGoal(costToGoal);
				if (AStarAddNode(endPos, new_cost + costToGoal) == PF_FAILED) {
					ret = PF_FAILED;
					AStarLastFailReason = AStarFailOpenSet;
					ProfileEnd("AStarFindPath");
					return ret;
				}
			} else if (new_cost < AStarMatrix[eo].GetCostFromStart()) {
				--counter;
				// Already visited node, but we have here a better path
				// I know, it's redundant (but simpler like this)
				AStarMatrix[eo].SetCostFromStart(new_cost);
				AStarMatrix[eo].SetDirection(direction);
				// this point might be already in the OpenSet
				const int j = AStarFindNode(eo);
				if (j == -1) {
					costToGoal = AStarCosts(endPos, goalPos);
					AStarMatrix[eo].SetCostToGoal(costToGoal);
					if (AStarAddNode(endPos, new_cost + costToGoal) == PF_FAILED) {
						ret = PF_FAILED;
					AStarLastFailReason = AStarFailOpenSet;
						ProfileEnd("AStarFindPath");
						return ret;
					}
				} else {
					costToGoal = AStarCosts(endPos, goalPos);
					AStarMatrix[eo].SetCostToGoal(costToGoal);
					AStarReplaceNode(j, new_cost + costToGoal);
				}
				// we don't have to add this point to the close set
			}
		}
		if (OpenSetSize <= 0) { // no new nodes generated
			ret = PF_UNREACHABLE;
			AStarLastFailByUnits = AStarRefusedUnitOnItsWay;
			AStarLastFailReason = AStarRefusedUnitOnItsWay ? AStarFailFloodUnitsOnTheWay
			                      : AStarRefusedUnit ? AStarFailFloodStandingUnits : AStarFailFloodTerrain;
			AStarLastSearchIterations = budget - counter;
			// Not one tile entered and a unit standing for good among those round it: boxed in.
			AStarLastFailBoxed = AStarLastSearchIterations == 0 && AStarRefusedUnitForGood;
			AStarVerifyRegions();
			ProfileEnd("AStarFindPath");
			return ret;
		}
	}

#ifdef DEBUG
	DumpNextAStar = false;
#endif
	AStarLastSearchIterations = budget - counter;
	AstarDebugPrint("AStar counter %d/%d\n", counter, budget);
	const int path_length = AStarSavePath(startPos, endPos, path, pathlen);

	ret = path_length;

	ProfileEnd("AStarFindPath");
	return ret;
}

void AStarDumpStats()
{
	int32_t maxCostFromHome = 0;
	int32_t minCostFromHome = INT_MAX;
	int32_t maxCostToGoal = 0;
	int32_t minCostToGoal = INT_MAX;

	for (const Node &m : AStarMatrix) {
		
		maxCostFromHome = std::max(maxCostFromHome, m.GetCostFromStart());
		maxCostToGoal = std::max(maxCostToGoal, m.GetCostToGoal());
		minCostFromHome = m.GetCostFromStart() ? std::min(minCostFromHome, m.GetCostFromStart()) : minCostFromHome;
		minCostToGoal = m.GetCostToGoal() ? std::min(minCostToGoal, m.GetCostToGoal()) : minCostToGoal;
	}
	if (minCostToGoal) minCostToGoal--;
	maxCostToGoal++;
	maxCostFromHome++;
	if (minCostFromHome) minCostFromHome--;

	int i = 0;
	for (const Node &m : AStarMatrix) {
		int r = 0;
		int g = 0;
		if (m.GetCostFromStart() && maxCostFromHome - minCostFromHome) {
			g = (1.0 - ((double)(m.GetCostFromStart() - minCostFromHome) / (maxCostFromHome - minCostFromHome))) * 255;
		}
		if (m.GetCostToGoal() && maxCostToGoal - minCostToGoal) {
			r = (1.0 - ((double)(m.GetCostToGoal() - minCostToGoal) / (maxCostToGoal - minCostToGoal))) * 255;
		}
		const char *direction;
		//  N NE  E SE  S SW  W NW
		switch (m.GetDirection()) {
			case 0:
				direction = "v";
				break;
			case 1:
				direction = "⌞";
				break;
			case 2:
				direction = "<";
				break;
			case 3:
				direction = "⌜";
				break;
			case 4:
				direction = "^";
				break;
			case 5:
				direction = "⌝";
				break;
			case 6:
				direction = ">";
				break;
			case 7:
				direction = "⌟";
				break;
			case 8:
				direction = "o";
				break;
			default:
				direction = " ";
		}
		if (m.IsInGoal()) {
			direction = "X";
		}
		fprintf(stdout, "\33[48;2;%d;%d;0m%s\33[49m", r, g, direction);
		if (!(i++ % AStarMapWidth)) {
			fprintf(stdout, "\n");
		}
	}
}

void DrawLastAStar(const CViewport& vp)
{
#if defined(DEBUG_ASTAR)
	for (auto y = vp.MapPos.y; y != vp.MapPos.y + vp.MapHeight; ++y) {
		for (auto x = vp.MapPos.x; x != vp.MapPos.x + vp.MapWidth; ++x) {
			const auto &node = AStarMatrix[GetIndex(x, y)];
			const auto direction = node.GetDirection();
			if (direction == 255) {
				continue;
			}
			const int length = direction == 8 ? 0 : PathElementLength(direction, 1);
			const auto nextX = x - Heading2X[PathElementHeading(direction)] * length;
			const auto nextY = y - Heading2Y[PathElementHeading(direction)] * length;
			const auto pixel1 = vp.TilePosToScreen_Center(Vec2i(x, y));
			const auto pixel2 = vp.TilePosToScreen_Center(Vec2i(nextX, nextY));

			if (0 <= pixel2.x && pixel2.x < Video.Width && 0 <= pixel2.y
			    && pixel2.y < Video.Height) {
				Video.DrawLine(ColorLightGray, pixel1.x, pixel1.y, pixel2.x, pixel2.y);
				CLabel(GetSmallFont()).Draw(pixel1.x - 10, pixel1.y - 10, node.GetCostFromStart() + node.GetCostToGoal());
			}
		}
	}
#endif
}

/*----------------------------------------------------------------------------
--  Configurable costs
----------------------------------------------------------------------------*/

// AStarFixedUnitCrossingCost
void SetAStarFixedUnitCrossingCost(int cost)
{
	if (cost <= 3) {
		ErrorPrint("AStarFixedUnitCrossingCost must be greater than 3\n");
	}
}
int GetAStarFixedUnitCrossingCost()
{
	return AStarFixedUnitCrossingCost;
}

// AStarMovingUnitCrossingCost
void SetAStarMovingUnitCrossingCost(int cost)
{
	if (cost <= 3) {
		ErrorPrint("AStarMovingUnitCrossingCost must be greater than 3\n");
	}
}
int GetAStarMovingUnitCrossingCost()
{
	return AStarMovingUnitCrossingCost;
}

// AStarUnknownTerrainCost
void SetAStarUnknownTerrainCost(int cost)
{
	if (cost < 0) {
		ErrorPrint("AStarUnknownTerrainCost must be non-negative\n");
		return;
	}
	AStarUnknownTerrainCost = cost;
}
int GetAStarUnknownTerrainCost()
{
	return AStarUnknownTerrainCost;
}

void SetAStarFixedEnemyUnitsUnpassable(const bool value)
{
	AStarFixedEnemyUnitsUnpassable = value;
}

bool GetAStarFixedEnemyUnitsUnpassable()
{
	return AStarFixedEnemyUnitsUnpassable;
}
//@}
