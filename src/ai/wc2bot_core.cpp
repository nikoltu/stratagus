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
/**@name wc2bot_core.cpp - The decisions of a data-described computer player, and its saved state. */
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

#include "wc2bot.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <iterator>

namespace wc2bot
{


// ---------------------------------------------------------------------------- rate cap

void RateCap::Advance(long now)
{
	if (second < 0 || now - second >= static_cast<long>(used.size())) {
		used.fill(0);
		second = now;
		return;
	}
	if (now <= second) {
		return;
	}
	for (long s = second + 1; s <= now; ++s) {
		used[static_cast<size_t>(s % static_cast<long>(used.size()))] = 0;
	}
	second = now;
}

int RateCap::UsedInWindow(long now)
{
	Advance(now);
	int sum = 0;
	for (const uint16_t n : used) {
		sum += n;
	}
	return sum;
}

bool RateCap::Take(long now, int cap)
{
	if (now < 0 || UsedInWindow(now) >= cap) {
		return false;
	}
	++used[static_cast<size_t>(now % static_cast<long>(used.size()))];
	return true;
}

bool RateCap::CanTake(long now, int cap)
{
	return now >= 0 && UsedInWindow(now) < cap;
}

int Distance(const Point &a, const Point &b)
{
	return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

// ---------------------------------------------------------------------------- state

State::State()
{
	for (long &at : lostAt) {
		at = -1;
	}
	for (long &at : unplacedSince) {
		at = -1;
	}
}

namespace
{
/// Known buildings equal with their class (Known's own == compares the slot and the tile only).
bool SameKnown(const std::vector<Known> &a, const std::vector<Known> &b)
{
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i) {
		if (!(a[i] == b[i]) || a[i].cls != b[i].cls) {
			return false;
		}
	}
	return true;
}
} // namespace

bool State::operator==(const State &other) const
{
	return opening == other.opening && std::equal(std::begin(planned), std::end(planned), std::begin(other.planned))
	       && std::equal(std::begin(peak), std::end(peak), std::begin(other.peak))
	       && std::equal(std::begin(lostAt), std::end(lostAt), std::begin(other.lostAt))
	       && research == other.research && nextWorkerAt == other.nextWorkerAt && nextWaveAt == other.nextWaveAt
	       && waves == other.waves && waveSize == other.waveSize && wave == other.wave && target == other.target
	       && visitedStarts == other.visitedStarts && scouted == other.scouted && rally == other.rally
	       && gatherUntil == other.gatherUntil && alarmTo == other.alarmTo && idleAt == other.idleAt && targetSlot == other.targetSlot && ignored == other.ignored
	       && std::equal(std::begin(unplacedSince), std::end(unplacedSince), std::begin(other.unplacedSince))
	       && clearedAt == other.clearedAt && alarmDue == other.alarmDue && alarmAt == other.alarmAt
	       && SameKnown(known, other.known) && armySecond == other.armySecond && rate == other.rate
	       && launchSize == other.launchSize && focusSlot == other.focusSlot && retreats == other.retreats
	       && raid == other.raid && raidTarget == other.raidTarget && raidSlot == other.raidSlot
	       && nextRaidAt == other.nextRaidAt && scouts == other.scouts && nextScoutAt == other.nextScoutAt
	       && scoutIndex == other.scoutIndex && expansions == other.expansions && escorted == other.escorted
	       && mineFirst == other.mineFirst
	       && std::equal(std::begin(seenPeak), std::end(seenPeak), std::begin(other.seenPeak))
	       && counterTowers == other.counterTowers && castBusy == other.castBusy
	       // sea and air
	       && fleet == other.fleet && fleetTarget == other.fleetTarget && fleetSlot == other.fleetSlot
	       && fleetLaunch == other.fleetLaunch && nextFleetAt == other.nextFleetAt && fleetVisited == other.fleetVisited
	       && fleetIgnored == other.fleetIgnored && fleetIdleAt == other.fleetIdleAt && enemyShipAt == other.enemyShipAt
	       && landingPhase == other.landingPhase && landingSince == other.landingSince
	       && nextLandingAt == other.nextLandingAt && landingSize == other.landingSize && landings == other.landings
	       && landing == other.landing && ferry == other.ferry && landingShore == other.landingShore
	       && landingWater == other.landingWater && seaResearch == other.seaResearch
	       && nextAirScoutAt == other.nextAirScoutAt && airScoutIndex == other.airScoutIndex && riders == other.riders
	       && riderTarget == other.riderTarget && riderSlot == other.riderSlot && riderLaunch == other.riderLaunch;
}

// ---------------------------------------------------------------------------- terrain

namespace
{

/// Is the map's terrain known (the preview's picture of land and water, as every person sees it)?
bool KnowsTerrain(const World &world)
{
	return world.regions != nullptr && world.mapWidth > 0 && world.mapHeight > 0
	       && world.regions->size() == static_cast<size_t>(world.mapWidth) * static_cast<size_t>(world.mapHeight);
}

int RegionAt(const World &world, int x, int y)
{
	return (*world.regions)[static_cast<size_t>(y) * static_cast<size_t>(world.mapWidth) + static_cast<size_t>(x)];
}

/// The piece of land of a place (0: water); -1 off the map or with the terrain not known.
int Region(const World &world, const Point &at)
{
	if (!KnowsTerrain(world) || !at.Valid() || at.x >= world.mapWidth || at.y >= world.mapHeight) {
		return -1;
	}
	return RegionAt(world, at.x, at.y);
}

/// How near a tile is to `at`: the distance in tiles, then the straight-line distance squared (so
/// that of the tiles in one ring the straightest wins, not the first row).
long long Nearness(const Point &at, int x, int y)
{
	const long long dx = x - at.x;
	const long long dy = y - at.y;
	return static_cast<long long>(Distance(at, {x, y})) * (1LL << 40) + dx * dx + dy * dy;
}

/// The water tile nearest to `at` (the first row by row on a tie); invalid when there is none.
Point NearestWater(const World &world, const Point &at)
{
	Point best;
	long long bestNear = -1;
	if (!KnowsTerrain(world) || !at.Valid()) {
		return best;
	}
	for (int y = 0; y < world.mapHeight; ++y) {
		for (int x = 0; x < world.mapWidth; ++x) {
			if (RegionAt(world, x, y) != 0) {
				continue;
			}
			const long long closeness = Nearness(at, x, y);
			if (bestNear < 0 || closeness < bestNear) {
				bestNear = closeness;
				best = {x, y};
			}
		}
	}
	return best;
}

/// The tile of `at`'s own land beside the water nearest to `at` - one at least `away` tiles from it
/// when there is one - and the water tile beside it (the first of its eight neighbours). False when
/// that land has no shore (or `at` is not on land).
bool ShoreOf(const World &world, const Point &at, int away, Point &land, Point &water)
{
	land = Point();
	water = Point();
	const int region = Region(world, at);
	if (region <= 0) {
		return false;
	}
	long long bestNear = -1;
	bool bestAway = false;
	for (int y = 0; y < world.mapHeight; ++y) {
		for (int x = 0; x < world.mapWidth; ++x) {
			if (RegionAt(world, x, y) != region) {
				continue;
			}
			Point beside;
			for (int dy = -1; dy <= 1 && !beside.Valid(); ++dy) {
				for (int dx = -1; dx <= 1 && !beside.Valid(); ++dx) {
					const int nx = x + dx;
					const int ny = y + dy;
					if ((dx != 0 || dy != 0) && nx >= 0 && ny >= 0 && nx < world.mapWidth && ny < world.mapHeight
					    && RegionAt(world, nx, ny) == 0) {
						beside = {nx, ny};
					}
				}
			}
			if (!beside.Valid()) {
				continue;
			}
			const bool distant = Distance(at, {x, y}) >= away;
			const long long closeness = Nearness(at, x, y);
			if (bestNear < 0 || (distant && !bestAway) || (distant == bestAway && closeness < bestNear)) {
				bestNear = closeness;
				bestAway = distant;
				land = {x, y};
				water = beside;
			}
		}
	}
	return land.Valid();
}

} // namespace

bool PlaceNearHall(bool (*fits)(int radius, void *context), void *context, int *used)
{
	for (const int radius : NearHallRadii) {
		if (fits(radius, context)) {
			if (used != nullptr) {
				*used = radius;
			}
			return true;
		}
	}
	return false;
}

bool SameLand(const World &world, const Point &a, const Point &b)
{
	if (!KnowsTerrain(world)) {
		return true;
	}
	const int region = Region(world, a);
	return region > 0 && region == Region(world, b);
}

std::vector<int> ExpansionMines(const std::vector<Mine> &mines, const std::vector<Point> &halls, const Point &home)
{
	std::vector<int> order;
	for (int i = 0; i < static_cast<int>(mines.size()); ++i) {
		const Mine &mine = mines[i];
		const bool served = std::any_of(halls.begin(), halls.end(),
		                                 [&mine](const Point &hall) { return Distance(hall, mine.at) <= MineServedTiles; });
		if (mine.gold > 0 && mine.reachable && !served) {
			order.push_back(i);
		}
	}
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		return Distance(mines[a].at, home) < Distance(mines[b].at, home);
	});
	return order;
}

bool SeaMap(const Profile &profile, const World &world)
{
	if (Region(world, world.home) <= 0) {
		return false; // terrain not known, or no home on land
	}
	for (const Point &start : world.enemyStarts) {
		if (!SameLand(world, world.home, start)) {
			return true;
		}
	}
	return profile.seaWater > 0 && world.waterShare >= profile.seaWater;
}

// ---------------------------------------------------------------------------- decisions

namespace
{

/// Idle units of a wave are given their next goal on these seconds only.
constexpr long RetargetSeconds = 5;
/// Workers the base needs before one goes scouting.
constexpr int ScoutWorkersMin = 4;
/// Below this many workers a new one is trained as soon as the last is out (no gap), and the
/// opening asks one building at a time (each waits until the one before it stands).
constexpr int FewWorkers = 4;
/// Food of one farm.
constexpr int FarmFood = 4;
/// Farms ahead of need on their way at once at most.
constexpr int FarmsOnTheWay = 2;
/// A farm is asked while this much food is still free (all levels; the profile's slack asks more).
constexpr int FoodLead = 2;

/// Everything one second may spend, and what it spent.
struct Spend {
	const Profile &profile;
	State &state;
	Decision &decision;
	long now;

	bool Take()
	{
		if (!state.rate.Take(now, profile.ordersPerMinute)) {
			return false;
		}
		++decision.orders;
		return true;
	}
	int Left() { return profile.ordersPerMinute - state.rate.UsedInWindow(now); }

	/// Gold / wood kept back this second for the next worker and the farm it needs (Reserve).
	int reserveGold = 0;
	int reserveWood = 0;

	/// May one more of the kind be asked without touching what is kept back? A price the view does
	/// not know (0) is no limit; wood is looked at only when the view knows the stock.
	/// The army is short of the wave due (ArmyShort): a fighter is not held back by the worker's
	/// reserve, and what is neither a fighter, a worker, a farm nor a barracks keeps back the gold
	/// of one fighter for each barracks too (`fighterGold`).
	bool armyFirst = false;
	int fighterGold = 0;

	bool Affords(const World &world, Kind kind) const
	{
		const int k = KindIndex(kind);
		const bool fighter = IsFighter(kind);
		const bool feedsArmy = kind == Kind::Farm || kind == Kind::Barracks;
		// fighters and the barracks need only their price; the rest keeps a fighter per barracks back
		const int gold = armyFirst ? (fighter || kind == Kind::Barracks ? 0 : reserveGold + (feedsArmy ? 0 : fighterGold))
		                           : reserveGold;
		const int wood = armyFirst && (fighter || kind == Kind::Barracks) ? 0 : reserveWood;
		if (world.goldCost[k] > 0 && world.gold - world.goldCost[k] < gold) {
			return false;
		}
		return world.wood < 0 || world.woodCost[k] <= 0 || world.wood - world.woodCost[k] >= wood;
	}
	bool AffordsResearch(const World &world, Line line) const
	{
		const int gold = world.researchGold[static_cast<int>(line)];
		return gold <= 0 || world.gold - gold >= reserveGold + (armyFirst ? fighterGold : 0);
	}
};

/// Workers it trains up to now (a larger number once the hall is a keep).
int WorkerCap(const Profile &profile, const World &world)
{
	return world.have[KindIndex(Kind::Keep)] > 0 ? profile.workersAfterKeep : profile.workers;
}

/// Free food: what the farms and halls give less what is used.
int FreeFood(const World &world)
{
	return world.supply - world.demand;
}

/// What is kept back while the workers are below the profile's number: the price of the next
/// worker, and of a farm when there is no food for that worker and no farm is on its way. Nothing
/// when the prices are not known (the view of a test without them).
void Reserve(const Profile &profile, const World &world, int &gold, int &wood)
{
	gold = 0;
	wood = 0;
	const int worker = KindIndex(Kind::Worker);
	const int farm = KindIndex(Kind::Farm);
	if (!world.can[worker] || world.have[worker] >= WorkerCap(profile, world)) {
		return;
	}
	gold = world.goldCost[worker];
	wood = world.woodCost[worker];
	if (FreeFood(world) <= FoodLead && world.queued[farm] == 0) {
		gold += world.goldCost[farm];
		wood += world.woodCost[farm];
	}
}

/// Wood below this is short: with gold lying unspent (StockLying above
/// what is kept back) a quarter of the workers (one at least) more go to lumber; with wood lying
/// that far above while gold does not, a quarter fewer.
constexpr int WoodShort = 400;
/// A stock this far above what is kept back lies unspent.
constexpr int StockLying = 800;

/// Workers on lumber this second. The profile's number is its number at the full worker count and
/// is scaled down with fewer workers; its share is a share of the workers there are (the larger of
/// the two). The stocks tip it: gold lying unspent while wood is short sends more to lumber, wood
/// piling up while gold is short fewer. At least one worker always mines gold, and half of them
/// while the workers are below the profile's number and the gold is below what is kept back for
/// the next worker (a bot whose workers all cut wood can never train another).
int LumberWanted(const Profile &profile, const World &world)
{
	const int workers = world.have[KindIndex(Kind::Worker)];
	if (workers <= 1) {
		return 0;
	}
	const int cap = std::max(1, WorkerCap(profile, world));
	int lumber = std::max(profile.lumberWorkers * std::min(workers, cap) / cap, workers * profile.lumberShare / 100);
	int reserveGold = 0;
	int reserveWood = 0;
	Reserve(profile, world, reserveGold, reserveWood);
	// The tip looks at the stocks with the same "lying" mark for both (round 3.4.67: the Captain
	// sat at 13/13 food with 1800-2600 gold and no wood for the farm - the mark for gold was its
	// spend_gold 1500 above the reserve; the Warlord had no gold while 1000-1200 wood lay).
	if (world.wood >= 0) {
		const int step = std::max(1, workers / 4);
		if (world.gold >= reserveGold + StockLying && world.wood < WoodShort) {
			lumber += step;
		} else if (world.wood >= reserveWood + StockLying && world.gold < reserveGold + StockLying) {
			lumber -= step;
		}
	}
	int goldMin = 1;
	if (workers < cap && world.gold < std::max(reserveGold, 1)) {
		goldMin = (workers + 1) / 2;
	}
	return std::clamp(lumber, 0, workers - goldMin);
}

bool InWave(const State &state, int slot)
{
	return std::find(state.wave.begin(), state.wave.end(), slot) != state.wave.end();
}

bool InRaid(const State &state, int slot)
{
	return std::find(state.raid.begin(), state.raid.end(), slot) != state.raid.end();
}

bool Contains(const std::vector<int> &slots, int slot)
{
	return std::find(slots.begin(), slots.end(), slot) != slots.end();
}

/// Out on a wave, a raid or a landing (not a home unit).
bool Busy(const State &state, int slot)
{
	return InWave(state, slot) || InRaid(state, slot) || Contains(state.landing, slot);
}

/// A land fighter free for a new task: not out, not aboard, on its home's land.
bool AtHome(const World &world, const State &state, const Fighter &f)
{
	return !Busy(state, f.slot) && !f.aboard && SameLand(world, world.home, f.at);
}

/// An own unit attacked this close to home or to an own far hall is an attack on the base.
constexpr int BaseAlarmTiles = 20;

/// Is the place part of the base (home or an expansion)? An own unit attacked farther out (a
/// scout, a worker far out, a unit on its way) is no alarm for the home army (round 3.4.66: the
/// Captain's and the Warlord's home units, 40 / 64 tiles of defence radius, walked one by one to
/// the person's start where a lone unit had been hit). An unknown place counts as the base.
bool AtBase(const World &world, const Point &at)
{
	if (!at.Valid() || !world.home.Valid() || Distance(at, world.home) <= BaseAlarmTiles) {
		return true;
	}
	return std::any_of(world.farHalls.begin(), world.farHalls.end(),
	                   [&at](const Point &hall) { return Distance(at, hall) <= BaseAlarmTiles; });
}

/// The army comes first from this long before the wave's second (a wave of 5 is ~3000 gold, two
/// minutes of a young base's income; 60 s gave waves 2-4 min late, round 3.4.67).
constexpr long ArmyLeadSeconds = 120;

/// Is the army short of the wave due? From ArmyLeadSeconds before the wave's second, while the
/// fighters free at home and in training are fewer than the wave's least size (round 3.4.66: the
/// Warlord's gold went to workers, farms, research and barracks first; 1-2 fighters at 10 minutes
/// on the classic maps, against the Captain's 3-6 and the Recruit's 8). "On low resources make
/// soldiers before the keep" (BOTS_DESIGN 1.1).
bool ArmyShort(const Profile &profile, const World &world, const State &state)
{
	// the first wave only: later the plan's tech (keep, castle) must get its gold too
	if (profile.firstWaveMin <= 0 || state.waves > 0 || state.nextWaveAt < 0
	    || world.second + ArmyLeadSeconds < state.nextWaveAt) {
		return false;
	}
	const int want = std::max(state.waves == 0 ? profile.firstWaveMin : profile.waveMin, state.waveSize);
	int army = 0;
	for (const Fighter &f : world.fighters) {
		if (!Busy(state, f.slot) && !f.aboard && SameLand(world, world.home, f.at)) {
			++army;
		}
	}
	for (int i = 0; i < FighterKinds; ++i) {
		if (profile.weight[i] > 0) {
			army += world.queued[KindIndex(FighterKindList[i])];
		}
	}
	return army < want;
}

/// A wave unit this close to a start position or a scouting point has been there.
constexpr int VisitRadius = 3;
/// A wave unit this close to the rally point has gathered.
constexpr int GatherRadius = 3;
/// Scouting points: the centres of a 3 x 3 grid over the map.
constexpr int ScoutGrid = 3;
constexpr int ScoutPoints = ScoutGrid * ScoutGrid;
/// Enemy units this close to the wave are "in its way" for the profile's target order.
constexpr int EngageRadius = 10;
/// The wave picks its focus among enemy units this close.
constexpr int FocusRadius = 8;
/// A known building this close to a known enemy tower stands in the tower's fire.
constexpr int TowerRadius = 7;
/// A hall this far from every enemy start position is an expansion.
constexpr int ExpansionRadius = 8;
/// Enemy fighters this close to the wave count against it when it thinks of retreat.
constexpr int OutnumberRadius = 8;
/// A raid unit below this per cent of its hit points brings the raid home.
constexpr int RaidRetreatHp = 50;
/// A caster given a cast order is left alone this long (it walks into range, casts).
constexpr long CastBusySeconds = 4;
/// Own units with an enemy fighter this close are "in a fight".
constexpr int FightRadius = 3;
/// An own unit below this per cent is healed.
constexpr int HealBelow = 60;
/// An area spell cast at the best place needs this many enemy units within one tile of it.
constexpr int ClusterMin = 3;
/// Polymorph is spent on enemies with at least this per cent of their hit points.
constexpr int PolymorphFrom = 50;

Point ScoutPoint(const World &world, int index)
{
	const int gx = index % ScoutGrid;
	const int gy = index / ScoutGrid;
	return {world.mapWidth * (2 * gx + 1) / (2 * ScoutGrid), world.mapHeight * (2 * gy + 1) / (2 * ScoutGrid)};
}

bool HasScoutPoints(const World &world)
{
	return world.mapWidth > 0 && world.mapHeight > 0;
}

/// The middle of the given own units of a list on the map (home when none is).
Point CentreOf(const World &world, const std::vector<Fighter> &units, const std::vector<int> &slots)
{
	long x = 0;
	long y = 0;
	long n = 0;
	for (const Fighter &f : units) {
		if (Contains(slots, f.slot)) {
			x += f.at.x;
			y += f.at.y;
			++n;
		}
	}
	if (n == 0) {
		return world.home;
	}
	// The unit nearest to the middle: the middle of units spread out on their ways may be a tile of
	// water or rock, from where nothing lies "on the same land" (round 3.4.67: the Captain's wave
	// on Unyielding, spread along two roads, found no target from there and went home).
	const Point middle{static_cast<int>(x / n), static_cast<int>(y / n)};
	Point best;
	long long bestNear = 0;
	for (const Fighter &f : units) {
		if (Contains(slots, f.slot)) {
			const long long near = Nearness(middle, f.at.x, f.at.y);
			if (!best.Valid() || near < bestNear) {
				best = f.at;
				bestNear = near;
			}
		}
	}
	return best;
}

/// The middle of the given own land fighters on the map (home when none is).
Point Centre(const World &world, const std::vector<int> &slots)
{
	return CentreOf(world, world.fighters, slots);
}

/// The middle of the wave's units on the map (home when none is).
Point WaveCentre(const World &world, const State &state)
{
	return Centre(world, state.wave);
}

/// A wave's goal: a place, and the unit / building there (-1: a place to look at).
struct Target {
	Point at;
	int slot = -1;
};

bool Ignored(const State &state, int slot)
{
	return std::find(state.ignored.begin(), state.ignored.end(), slot) != state.ignored.end();
}

/// Units / buildings a wave could not get to, at most this many remembered (the oldest goes).
constexpr size_t IgnoredMax = 32;

void Ignore(State &state, int slot)
{
	if (slot < 0 || Ignored(state, slot)) {
		return;
	}
	if (state.ignored.size() >= IgnoredMax) {
		state.ignored.erase(state.ignored.begin());
	}
	state.ignored.push_back(slot);
}

bool NearEnemyStart(const World &world, const Point &at)
{
	for (const Point &start : world.enemyStarts) {
		if (Distance(start, at) <= ExpansionRadius) {
			return true;
		}
	}
	return false;
}

/// Does a seen unit (building false) or a known building (true) belong to the class a profile names?
bool AimMatches(Aim aim, const Known &k, bool building, const World &world)
{
	switch (aim) {
		case Aim::Worker: return !building && k.cls == Class::Worker;
		case Aim::Soldier: return !building && k.cls == Class::Soldier;
		case Aim::Shooter: return !building && k.cls == Class::Shooter;
		case Aim::Cavalry: return !building && k.cls == Class::Cavalry;
		case Aim::Siege: return !building && k.cls == Class::Siege;
		case Aim::Caster: return !building && k.cls == Class::Caster;
		case Aim::Sapper: return !building && k.cls == Class::Sapper;
		case Aim::Army: return !building && IsArmy(k.cls);
		case Aim::Hall: return building && k.cls == Class::Hall;
		case Aim::Expansion: return building && k.cls == Class::Hall && !NearEnemyStart(world, k.at);
		case Aim::Barracks: return building && k.cls == Class::Barracks;
		case Aim::Tower: return building && k.cls == Class::Tower;
		case Aim::Building: return building;
		case Aim::Ship: return !building && IsSeaUnit(k.cls);
		case Aim::Transport: return !building && k.cls == Class::Transport;
		case Aim::Tanker: return !building && k.cls == Class::Tanker;
		case Aim::Platform: return building && k.cls == Class::Platform;
		case Aim::Shipyard: return building && k.cls == Class::Shipyard;
		case Aim::Foundry: return building && k.cls == Class::Foundry;
		case Aim::Refinery: return building && k.cls == Class::Refinery;
		case Aim::Flyer: return !building && k.cls == Class::Flyer;
		default: return false;
	}
}

/// Place of a seen unit / known building in the profile's target order (its length when unnamed).
int AimRank(const Profile &profile, const Known &k, bool building, const World &world)
{
	for (size_t i = 0; i < profile.targets.size(); ++i) {
		if (AimMatches(profile.targets[i], k, building, world)) {
			return static_cast<int>(i);
		}
	}
	return static_cast<int>(profile.targets.size());
}

bool KnowsTower(const State &state)
{
	return std::any_of(state.known.begin(), state.known.end(), [](const Known &k) { return k.cls == Class::Tower; });
}

bool NearTower(const State &state, const Point &at)
{
	return std::any_of(state.known.begin(), state.known.end(), [&at](const Known &k) {
		return k.cls == Class::Tower && Distance(k.at, at) <= TowerRadius;
	});
}

bool WaveHasSiege(const World &world, const State &state)
{
	return std::any_of(world.fighters.begin(), world.fighters.end(),
	                   [&state](const Fighter &f) { return f.kind == Kind::Siege && InWave(state, f.slot); });
}

/// The next place to attack. With a target order in the profile: class by class, an enemy unit its
/// own units see in the wave's way (nearest to `from`) or a building it knows. Then, and always for
/// a profile without one: the known enemy building nearest to home (the first on a tie); an enemy
/// unit its own units see now, nearest to `from`; the enemy start position nearest to home not
/// found empty yet; the scouting point nearest to `from` not reached yet. Nothing else is ever a
/// target: all of it comes from its own units' sight and the map's start positions. A unit or
/// building this wave could not get to is passed over; with "avoid towers" and no siege in the
/// wave, so are the known towers and the buildings in their fire. Only what lies on the land of
/// `from` counts (a land army does not walk over the sea; units on the water and oil platforms are
/// never its targets). Invalid: nothing is left to do.
Target PickTarget(const Profile &profile, const World &world, const State &state, const Point &from)
{
	Target best;
	int bestDistance = -1;
	const auto consider = [&](const Point &at, const Point &origin, int slot) {
		const int d = Distance(origin, at);
		if (bestDistance < 0 || d < bestDistance) {
			bestDistance = d;
			best.at = at;
			best.slot = slot;
		}
	};
	const bool avoid = profile.avoidTowers && !WaveHasSiege(world, state);
	const auto avoided = [&](const Known &b) { return avoid && (b.cls == Class::Tower || NearTower(state, b.at)); };
	const auto unitOk = [&](const Known &unit) {
		return !Ignored(state, unit.slot) && !IsSeaUnit(unit.cls) && SameLand(world, from, unit.at);
	};
	const auto buildingOk = [&](const Known &b) {
		return !Ignored(state, b.slot) && !avoided(b) && b.cls != Class::Platform && SameLand(world, from, b.at);
	};
	for (const Aim aim : profile.targets) {
		for (const Known &unit : world.seenUnits) {
			if (unitOk(unit) && Distance(from, unit.at) <= EngageRadius && AimMatches(aim, unit, false, world)) {
				consider(unit.at, from, unit.slot);
			}
		}
		for (const Known &known : state.known) {
			if (buildingOk(known) && AimMatches(aim, known, true, world)) {
				consider(known.at, from, known.slot);
			}
		}
		if (best.at.Valid()) {
			return best;
		}
	}
	for (const Known &known : state.known) {
		if (buildingOk(known)) {
			consider(known.at, world.home, known.slot);
		}
	}
	if (best.at.Valid()) {
		return best;
	}
	for (const Known &unit : world.seenUnits) {
		if (unitOk(unit)) {
			consider(unit.at, from, unit.slot);
		}
	}
	if (best.at.Valid()) {
		return best;
	}
	for (size_t i = 0; i < world.enemyStarts.size() && i < 32; ++i) {
		if (!(state.visitedStarts & (1u << i)) && SameLand(world, from, world.enemyStarts[i])) {
			consider(world.enemyStarts[i], world.home, -1);
		}
	}
	if (best.at.Valid() || !HasScoutPoints(world)) {
		return best;
	}
	for (int i = 0; i < ScoutPoints; ++i) {
		if (!(state.scouted & (1u << i)) && SameLand(world, from, ScoutPoint(world, i))) {
			consider(ScoutPoint(world, i), from, -1);
		}
	}
	return best;
}

/// The point `tiles` from home toward `to`, on the map.
Point Toward(const World &world, const Point &to, int tiles)
{
	const Point &home = world.home;
	const int dx = to.x - home.x;
	const int dy = to.y - home.y;
	const int length = std::max(std::abs(dx), std::abs(dy));
	Point at = home;
	if (length > 0) {
		const int step = std::min(tiles, length);
		at.x = home.x + dx * step / length;
		at.y = home.y + dy * step / length;
	}
	if (world.mapWidth > 0) {
		at.x = std::clamp(at.x, 0, world.mapWidth - 1);
	}
	if (world.mapHeight > 0) {
		at.y = std::clamp(at.y, 0, world.mapHeight - 1);
	}
	return at;
}

/// A wave that has no unit left forgets where it went.
void ClearIfEmpty(State &state)
{
	if (state.wave.empty()) {
		state.target = Point();
		state.targetSlot = -1;
		state.gatherUntil = -1;
		state.idleAt = Point();
		state.focusSlot = -1;
	}
}

/// What the player's own units see changes what it knows; nothing else does.
void UpdateKnowledge(const World &world, State &state)
{
	for (const int slot : world.gone) {
		state.known.erase(std::remove_if(state.known.begin(), state.known.end(),
		                                 [slot](const Known &known) { return known.slot == slot; }),
		                  state.known.end());
	}
	for (const Known &seen : world.seen) {
		auto it = std::find_if(state.known.begin(), state.known.end(),
		                       [&seen](const Known &known) { return known.slot == seen.slot; });
		if (it == state.known.end()) {
			Known known;
			known.slot = seen.slot;
			known.at = seen.at;
			known.cls = seen.cls;
			state.known.push_back(known);
		} else {
			it->at = seen.at;
			it->cls = seen.cls;
		}
	}
	// Units that died or are no longer on the map leave the wave and the raid.
	const auto gone = [&world](int slot) {
		return std::none_of(world.fighters.begin(), world.fighters.end(), [slot](const Fighter &f) { return f.slot == slot; });
	};
	state.wave.erase(std::remove_if(state.wave.begin(), state.wave.end(), gone), state.wave.end());
	ClearIfEmpty(state);
	state.raid.erase(std::remove_if(state.raid.begin(), state.raid.end(), gone), state.raid.end());
	if (state.raid.empty()) {
		state.raidTarget = Point();
		state.raidSlot = -1;
	}
	state.landing.erase(std::remove_if(state.landing.begin(), state.landing.end(), gone), state.landing.end());
	// Ships and flyers lost leave the fleet, the ferry and the riders.
	const auto noShip = [&world](int slot) {
		return std::none_of(world.ships.begin(), world.ships.end(), [slot](const Fighter &f) { return f.slot == slot; });
	};
	const auto noFlyer = [&world](int slot) {
		return std::none_of(world.flyers.begin(), world.flyers.end(), [slot](const Fighter &f) { return f.slot == slot; });
	};
	state.fleet.erase(std::remove_if(state.fleet.begin(), state.fleet.end(), noShip), state.fleet.end());
	if (state.fleet.empty()) {
		state.fleetTarget = Point();
		state.fleetSlot = -1;
		state.fleetIdleAt = Point();
	}
	state.ferry.erase(std::remove_if(state.ferry.begin(), state.ferry.end(), noShip), state.ferry.end());
	state.riders.erase(std::remove_if(state.riders.begin(), state.riders.end(), noFlyer), state.riders.end());
	if (state.riders.empty()) {
		state.riderTarget = Point();
		state.riderSlot = -1;
	}
	if (std::any_of(world.seenUnits.begin(), world.seenUnits.end(), [](const Known &k) { return k.cls == Class::Ship; })) {
		state.enemyShipAt = world.second;
	}
	// The most enemy units of each class seen at once (what the counters answer).
	int count[ClassCount] = {};
	for (const Known &unit : world.seenUnits) {
		++count[static_cast<int>(unit.cls)];
	}
	for (int c = 0; c < ClassCount; ++c) {
		state.seenPeak[c] = std::max(state.seenPeak[c], count[c]);
	}
	if (state.mineFirst <= 0 && world.mineGold > 0) {
		state.mineFirst = world.mineGold;
	}
	state.escorted = std::min(state.escorted, static_cast<int>(world.farHalls.size()));
	state.castBusy.erase(std::remove_if(state.castBusy.begin(), state.castBusy.end(),
	                                    [&](const std::pair<int, long> &busy) {
		                                    return world.second - busy.second >= CastBusySeconds || gone(busy.first);
	                                    }),
	                     state.castBusy.end());
}

/// Strike off the start positions and scouting points a wave unit has reached. One round of the
/// scouting points per wave: with every one struck off and nothing in sight the wave ends (EndWave).
void MarkVisited(const World &world, State &state)
{
	for (const Fighter &f : world.fighters) {
		if (!InWave(state, f.slot)) {
			continue;
		}
		for (size_t i = 0; i < world.enemyStarts.size() && i < 32; ++i) {
			if (Distance(f.at, world.enemyStarts[i]) <= VisitRadius) {
				state.visitedStarts |= 1u << i;
			}
		}
		if (HasScoutPoints(world)) {
			for (int i = 0; i < ScoutPoints; ++i) {
				if (Distance(f.at, ScoutPoint(world, i)) <= VisitRadius) {
					state.scouted |= 1u << i;
				}
			}
		}
	}
}

/// Is the place a start position or a scouting point (a place to look, not a thing to attack)?
bool IsLookout(const World &world, const Point &at)
{
	if (std::find(world.enemyStarts.begin(), world.enemyStarts.end(), at) != world.enemyStarts.end()) {
		return true;
	}
	if (HasScoutPoints(world)) {
		for (int i = 0; i < ScoutPoints; ++i) {
			if (ScoutPoint(world, i) == at) {
				return true;
			}
		}
	}
	return false;
}

/// Is a lookout place struck off already?
bool IsStruck(const World &world, const State &state, const Point &at)
{
	for (size_t i = 0; i < world.enemyStarts.size() && i < 32; ++i) {
		if (world.enemyStarts[i] == at && !(state.visitedStarts & (1u << i))) {
			return false;
		}
	}
	if (HasScoutPoints(world)) {
		for (int i = 0; i < ScoutPoints; ++i) {
			if (ScoutPoint(world, i) == at && !(state.scouted & (1u << i))) {
				return false;
			}
		}
	}
	return true;
}

/// Strike off a lookout place as if reached (its start bit or its scouting point bit).
void StrikeOff(const World &world, State &state, const Point &at)
{
	for (size_t i = 0; i < world.enemyStarts.size() && i < 32; ++i) {
		if (world.enemyStarts[i] == at) {
			state.visitedStarts |= 1u << i;
		}
	}
	if (HasScoutPoints(world)) {
		for (int i = 0; i < ScoutPoints; ++i) {
			if (ScoutPoint(world, i) == at) {
				state.scouted |= 1u << i;
			}
		}
	}
}

/// A building asked of the production has found no place for this long: the bot notices it.
constexpr long StuckSeconds = 20;
/// The home units are moved out of the base for a building with no place at most this often.
constexpr long ClearGapSeconds = 60;
/// Home units this close to home stand in the base.
constexpr int BaseRadius = 8;
/// How far from home the home units step out to (at least; the rally distance when farther).
constexpr int ClearTiles = 6;

bool Stuck(const State &state, int kind, long now)
{
	return state.unplacedSince[kind] >= 0 && now - state.unplacedSince[kind] >= StuckSeconds;
}

bool AnyStuck(const World &world, const State &state, long now)
{
	// the land play's buildings only: a shipyard or a platform with no place is no matter of the base;
	// nor a further hall (it goes beside a free gold mine or nowhere, never in the base)
	for (int k = 0; k < KindCount; ++k) {
		const bool furtherHall = k == KindIndex(Kind::Hall) && world.have[k] > 0;
		if (IsLandBuilding(static_cast<Kind>(k)) && !furtherHall && Stuck(state, k, now)) {
			return true;
		}
	}
	return false;
}

/// Is a building the profile wants before any extra asked now and without a place?
bool WantedFirstUnplaced(const Profile &profile, const World &world)
{
	for (const Kind kind : profile.beforeExtras) {
		if (world.unplaced[KindIndex(kind)]) {
			return true;
		}
	}
	return false;
}

/// Do the opening's buildings of the profile's "before extras" kinds all stand? (A kind the
/// opening does not plan is not waited for.)
bool WantedFirstStand(const Profile &profile, const World &world, const State &state)
{
	for (const Kind kind : profile.beforeExtras) {
		int steps = 0;
		int released = 0;
		for (size_t i = 0; i < profile.opening.size(); ++i) {
			if (profile.opening[i].what == kind) {
				++steps;
				if (static_cast<int>(i) < state.opening) {
					++released;
				}
			}
		}
		if (released < steps || world.have[KindIndex(kind)] < steps) {
			return false;
		}
	}
	return true;
}

/// Is the wave's goal a place to look at (a start position or a scouting point)?
bool AimsAtLookout(const World &world, const State &state)
{
	return state.target.Valid() && state.targetSlot < 0 && IsLookout(world, state.target);
}

/// The wave has nothing left to do (nothing in sight, nothing known, every place looked at): it
/// comes back to its gathering place and is a wave no more, so its units are at home for the next
/// one. A unit the rate window has no order for stays where it stands, at home all the same.
void EndWave(const World &world, Spend &spend, const std::vector<int> &sent)
{
	State &state = spend.state;
	const Point back = state.rally.Valid() ? state.rally : world.home;
	for (const Fighter &f : world.fighters) {
		if (!InWave(state, f.slot) || Distance(f.at, back) <= GatherRadius
		    || std::find(sent.begin(), sent.end(), f.slot) != sent.end()) {
			continue;
		}
		if (!spend.Take()) {
			break;
		}
		spend.decision.commands.push_back({f.slot, back});
	}
	state.wave.clear();
	state.target = Point();
	state.targetSlot = -1;
	state.gatherUntil = -1;
	state.idleAt = Point();
	state.scouted = 0;
	state.ignored.clear();
	state.focusSlot = -1;
}

/// Enemy land fighters own units see within `radius` of `at`.
int EnemyFightersNear(const World &world, const Point &at, int radius)
{
	int n = 0;
	for (const Known &unit : world.seenUnits) {
		if (IsArmy(unit.cls) && Distance(unit.at, at) <= radius) {
			++n;
		}
	}
	return n;
}

/// The orders of one second of the army, and the units that got one.
struct ArmyContext {
	const World &world;
	Spend &spend;
	std::vector<int> sent;
	bool acted = false;

	bool WasSent(int slot) const { return Contains(sent, slot); }
	/// One order to one own unit, paid from the rate window. False when the window is full.
	bool Give(const Command &command)
	{
		if (!spend.Take()) {
			return false;
		}
		spend.decision.commands.push_back(command);
		sent.push_back(command.slot);
		acted = true;
		return true;
	}
};

Command Make(int slot, const Point &to, Order order = Order::Attack, int targetSlot = -1, int spell = -1)
{
	Command command;
	command.slot = slot;
	command.to = to;
	command.order = order;
	command.targetSlot = targetSlot;
	command.spell = spell;
	return command;
}

/// Defence: the units at home near the attacked place, after the profile's delay.
void DefenceSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	if (state.alarmDue < 0 || army.spend.now < state.alarmDue) {
		return;
	}
	bool refused = false;
	const Point to = state.alarmTo.Valid() ? state.alarmTo : state.alarmAt;
	for (const Fighter &f : army.world.fighters) {
		if (!AtHome(army.world, state, f) || Distance(f.at, state.alarmAt) > profile.defenceRadius) {
			continue;
		}
		if (!army.Give(Make(f.slot, to))) {
			refused = true;
			break;
		}
	}
	// Few attackers: the workers nearest to them join (at most the profile's number). They go back
	// to work by themselves once idle (the engine's gathering).
	if (!refused && profile.defenceWorkers > 0) {
		const int attackers = EnemyFightersNear(army.world, to, OutnumberRadius);
		if (attackers > 0 && attackers <= profile.defenceWorkersVs) {
			std::vector<const Worker *> helpers;
			for (const Worker &w : army.world.workers) {
				if (!w.repairing && Distance(w.at, state.alarmAt) <= profile.defenceRadius) {
					helpers.push_back(&w);
				}
			}
			std::stable_sort(helpers.begin(), helpers.end(), [&to](const Worker *a, const Worker *b) {
				return Distance(a->at, to) < Distance(b->at, to);
			});
			for (size_t i = 0; i < helpers.size() && static_cast<int>(i) < profile.defenceWorkers; ++i) {
				if (!army.Give(Make(helpers[i]->slot, to))) {
					break;
				}
			}
		}
	}
	// A full rate window keeps the alarm for the next second instead of dropping it.
	if (!refused) {
		state.alarmDue = -1;
	}
}

/// A building asked long ago still finds no place: units standing in the base take the tiles it
/// could go on. The idle home units step out of the base (toward the next target, else the
/// middle of the map), at most once a minute while it lasts.
void ClearBaseSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	const World &world = army.world;
	if (!AnyStuck(world, state, army.spend.now) || (state.clearedAt >= 0 && army.spend.now - state.clearedAt < ClearGapSeconds)) {
		return;
	}
	state.clearedAt = army.spend.now;
	Point toward = PickTarget(profile, world, state, world.home).at;
	if (!toward.Valid() && HasScoutPoints(world)) {
		toward = {world.mapWidth / 2, world.mapHeight / 2};
	}
	const Point out = toward.Valid() ? Toward(world, toward, std::max(ClearTiles, profile.rallyTiles)) : Point();
	if (!out.Valid() || out == world.home) {
		return;
	}
	for (const Fighter &f : world.fighters) {
		if (!f.idle || !AtHome(world, state, f) || army.WasSent(f.slot) || Distance(f.at, world.home) > BaseRadius
		    || Distance(f.at, out) <= GatherRadius) {
			continue;
		}
		if (!army.Give(Make(f.slot, out))) {
			break;
		}
	}
}

/// Badly hurt wave units leave the wave and walk home (they are left out of the next wave until
/// healed).
void PullHurtSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	if (profile.pullHp <= 0 || state.wave.empty() || state.gatherUntil >= 0) {
		return;
	}
	for (const Fighter &f : army.world.fighters) {
		if (!InWave(state, f.slot) || f.hp >= profile.pullHp || army.WasSent(f.slot)) {
			continue;
		}
		if (!army.Give(Make(f.slot, army.world.home, Order::Move))) {
			break;
		}
		state.wave.erase(std::find(state.wave.begin(), state.wave.end(), f.slot));
	}
	ClearIfEmpty(state);
}

/// A wave that lost the profile's share of its units while the enemy it sees around it are at
/// least as many comes home (a move: it does not stop to fight on the way). The next wave may come
/// back soon (the profile's return time).
void RetreatSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	const World &world = army.world;
	if (profile.retreatLost <= 0 || state.wave.empty() || state.gatherUntil >= 0 || state.launchSize <= 0) {
		return;
	}
	const int left = static_cast<int>(state.wave.size());
	const int lost = state.launchSize - left;
	if (lost * 100 < profile.retreatLost * state.launchSize
	    || EnemyFightersNear(world, WaveCentre(world, state), OutnumberRadius) < left) {
		return;
	}
	for (const Fighter &f : world.fighters) {
		if (InWave(state, f.slot) && !army.WasSent(f.slot)) {
			if (!army.Give(Make(f.slot, world.home, Order::Move))) {
				break;
			}
		}
	}
	state.wave.clear();
	ClearIfEmpty(state);
	state.scouted = 0;
	state.ignored.clear();
	++state.retreats;
	if (profile.returnSeconds > 0) {
		state.nextWaveAt = std::min(state.nextWaveAt, army.spend.now + profile.returnSeconds);
	}
	army.acted = true;
}

/// The wave focuses on one enemy unit in its reach, chosen by the profile's target order, then the
/// fewest hit points, then the nearest. True while it has one (the idle units are not sent on).
bool FocusSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	const World &world = army.world;
	const Point centre = WaveCentre(world, state);
	for (const Known &unit : world.seenUnits) {
		if (unit.slot == state.focusSlot && Distance(centre, unit.at) <= FocusRadius) {
			return true; // still fighting it
		}
	}
	state.focusSlot = -1;
	const Known *best = nullptr;
	int bestRank = 0;
	int bestDistance = 0;
	for (const Known &unit : world.seenUnits) {
		const int d = Distance(centre, unit.at);
		if (Ignored(state, unit.slot) || unit.cls == Class::Flyer || IsSeaUnit(unit.cls) || d > FocusRadius
		    || !SameLand(world, centre, unit.at)) {
			continue;
		}
		const int rank = AimRank(profile, unit, false, world);
		if (best == nullptr || rank < bestRank || (rank == bestRank && unit.hp < best->hp)
		    || (rank == bestRank && unit.hp == best->hp && d < bestDistance)) {
			best = &unit;
			bestRank = rank;
			bestDistance = d;
		}
	}
	if (best == nullptr) {
		return false;
	}
	state.focusSlot = best->slot;
	for (const Fighter &f : world.fighters) {
		if (!InWave(state, f.slot) || f.kind == Kind::Caster || army.WasSent(f.slot)) {
			continue;
		}
		if (!army.Give(Make(f.slot, best->at, Order::Attack, best->slot))) {
			break;
		}
	}
	return true;
}

/// Is a unit of the wave on other land than home (a landed wave)?
bool WaveAbroad(const World &world, const State &state)
{
	return std::any_of(world.fighters.begin(), world.fighters.end(), [&](const Fighter &f) {
		return InWave(state, f.slot) && !SameLand(world, world.home, f.at);
	});
}

/// The wave due, its gathering and its next target.
void WaveSecond(ArmyContext &army, Dice dice, void *context)
{
	const World &world = army.world;
	Spend &spend = army.spend;
	const Profile &profile = spend.profile;
	State &state = spend.state;
	Decision &decision = spend.decision;
	std::vector<int> &sent = army.sent;

	// The wave due: as soon as enough units are at home and the orders for all of them are free;
	// not while a landing is under way or a landed wave fights on other land (the two never mix).
	if (spend.now >= state.nextWaveAt && state.landingPhase == 0 && !WaveAbroad(world, state)) {
		if (state.waveSize == 0) {
			const int low = state.waves == 0 ? profile.firstWaveMin : profile.waveMin;
			const int high = state.waves == 0 ? profile.firstWaveMax : profile.waveMax;
			int roll = high > low && dice ? dice(high - low + 1, context) : 0;
			if (roll < 0 || roll > high - low) {
				roll = 0;
			}
			state.waveSize = low + roll;
		}
		const Target target = PickTarget(profile, world, state, world.home);
		const Point rally =
			profile.rallyTiles > 0 && target.at.Valid() ? Toward(world, target.at, profile.rallyTiles) : Point();
		std::vector<const Fighter *> home;
		for (const Fighter &f : world.fighters) {
			if (AtHome(world, state, f) && !army.WasSent(f.slot) && !(profile.pullHp > 0 && f.hp < profile.pullHp)) {
				home.push_back(&f);
			}
		}
		// The units nearest the rally point (or home) go, the first by slot on a tie; siege first
		// when the profile does not walk into known towers without it.
		const Point gather = rally.Valid() ? rally : world.home;
		const bool siegeFirst = profile.avoidTowers && KnowsTower(state);
		std::stable_sort(home.begin(), home.end(), [&gather, siegeFirst](const Fighter *a, const Fighter *b) {
			if (siegeFirst && (a->kind == Kind::Siege) != (b->kind == Kind::Siege)) {
				return a->kind == Kind::Siege;
			}
			return Distance(a->at, gather) < Distance(b->at, gather);
		});
		if (target.at.Valid() && static_cast<int>(home.size()) >= state.waveSize && spend.Left() >= state.waveSize) {
			for (int i = 0; i < state.waveSize; ++i) {
				const Fighter &f = *home[i];
				state.wave.push_back(f.slot);
				if (rally.Valid() && Distance(f.at, rally) <= GatherRadius) {
					continue; // already there: no order
				}
				if (!spend.Take()) {
					break;
				}
				decision.commands.push_back(Make(f.slot, rally.Valid() ? rally : target.at));
				sent.push_back(f.slot);
			}
			state.target = target.at;
			state.targetSlot = target.slot;
			state.rally = rally;
			state.gatherUntil = rally.Valid() ? spend.now + profile.gatherSeconds : -1;
			state.idleAt = rally.Valid() ? Point() : target.at;
			state.launchSize = state.waveSize;
			state.focusSlot = -1;
			++state.waves;
			state.nextWaveAt = spend.now + profile.waveGapSeconds;
			state.waveSize = 0;
			army.acted = true;
		}
	}

	if (state.wave.empty()) {
		return;
	}

	// The wave gathering: it goes together once every unit is at the rally point, or when the wait
	// is over, and only when the orders for all of them are free.
	if (state.gatherUntil >= 0) {
		bool together = true;
		for (const Fighter &f : world.fighters) {
			if (InWave(state, f.slot) && Distance(f.at, state.rally) > GatherRadius) {
				together = false;
			}
		}
		if (!(together || spend.now >= state.gatherUntil) || !sent.empty()) {
			return;
		}
		const Target target = PickTarget(profile, world, state, state.rally);
		if (!target.at.Valid()) {
			EndWave(world, spend, sent);
			army.acted = true;
			return;
		}
		if (spend.Left() < static_cast<int>(state.wave.size())) {
			return;
		}
		state.target = target.at;
		state.targetSlot = target.slot;
		for (const Fighter &f : world.fighters) {
			if (InWave(state, f.slot) && spend.Take()) {
				decision.commands.push_back(Make(f.slot, state.target));
				sent.push_back(f.slot);
			}
		}
		state.idleAt = state.target;
		state.gatherUntil = -1;
		army.acted = true;
		return;
	}

	// The wave on its way. A start position or scouting point reached is struck off; units with
	// nothing left to do go on to the next target: a known building, an enemy unit in sight, a start
	// position not seen yet, else the next scouting point. (Retreat is the profile's: RetreatSecond.)
	MarkVisited(world, state);
	// Every unit of the wave idle short of the goal it was sent to, at a retarget second (none sent
	// this second): it cannot get there. A place is struck off, a unit or building passed over for
	// the rest of this wave.
	if (spend.now % RetargetSeconds == 0 && sent.empty() && state.target.Valid() && state.idleAt == state.target) {
		bool allIdle = true;
		for (const Fighter &f : world.fighters) {
			if (InWave(state, f.slot) && !f.idle) {
				allIdle = false;
			}
		}
		if (allIdle) {
			if (state.targetSlot >= 0) {
				Ignore(state, state.targetSlot);
			} else {
				StrikeOff(world, state, state.target);
			}
			state.idleAt = Point();
		}
	}
	const Point before = state.target;
	const Target next = PickTarget(profile, world, state, WaveCentre(world, state));
	// A place to look at is kept until it is reached (or struck off), so that the wave does not turn
	// to whichever lookout is nearest as it walks; anything to attack comes before it at once.
	const bool keep = AimsAtLookout(world, state) && !IsStruck(world, state, state.target) && next.at.Valid()
	                  && next.slot < 0;
	if (!keep) {
		state.target = next.at;
		state.targetSlot = next.slot;
	}
	if (!state.target.Valid()) {
		// Nothing in sight, nothing known, every place looked at (or out of reach): the wave ends.
		EndWave(world, spend, sent);
		army.acted = true;
		return;
	}
	// Focus fire: one enemy unit at a time while one is in reach.
	if (profile.focusFire && FocusSecond(army)) {
		return;
	}
	// Idle units are sent on every fifth second only (a unit that cannot reach its goal must not
	// hold the bot's attention every second), and at once when the wave turns to a new place to
	// look at. Orders for looking keep back what the wave due needs to set out.
	const bool turned = !(state.target == before) && state.targetSlot < 0;
	if (spend.now % RetargetSeconds == 0 || turned) {
		const int reserve = state.targetSlot < 0 && spend.now >= state.nextWaveAt ? state.waveSize : 0;
		bool any = false;
		for (const Fighter &f : world.fighters) {
			// a new place to look at: the whole wave turns, not only the idle units (else the ones
			// still walking go on to the old place and the wave falls apart)
			if ((!f.idle && !turned) || !InWave(state, f.slot) || Distance(f.at, state.target) <= 1 || army.WasSent(f.slot)) {
				continue;
			}
			if (spend.Left() <= reserve || !spend.Take()) {
				break;
			}
			decision.commands.push_back(Make(f.slot, state.target));
			army.acted = true;
			any = true;
		}
		if (any) {
			state.idleAt = state.target;
		}
	}
}

/// Where a raid goes: an enemy worker its own units see (nearest to `from`), else a known enemy
/// hall (an expansion first), else nowhere.
Target RaidTarget(const World &world, const State &state, const Point &from)
{
	Target best;
	int bestDistance = -1;
	for (const Known &unit : world.seenUnits) {
		if (unit.cls == Class::Worker && SameLand(world, from, unit.at)
		    && (bestDistance < 0 || Distance(from, unit.at) < bestDistance)) {
			bestDistance = Distance(from, unit.at);
			best.at = unit.at;
			best.slot = unit.slot;
		}
	}
	if (best.at.Valid()) {
		return best;
	}
	for (const bool expansionOnly : {true, false}) {
		for (const Known &known : state.known) {
			if (known.cls != Class::Hall || (expansionOnly && NearEnemyStart(world, known.at))
			    || !SameLand(world, from, known.at)) {
				continue;
			}
			if (bestDistance < 0 || Distance(from, known.at) < bestDistance) {
				bestDistance = Distance(from, known.at);
				best.at = known.at;
				best.slot = known.slot;
			}
		}
		if (best.at.Valid()) {
			return best;
		}
	}
	return best;
}

/// The raid group comes home (a move) and is a group no more.
void EndRaid(ArmyContext &army)
{
	State &state = army.spend.state;
	for (const Fighter &f : army.world.fighters) {
		if (InRaid(state, f.slot) && !army.WasSent(f.slot)) {
			if (!army.Give(Make(f.slot, army.world.home, Order::Move))) {
				break;
			}
		}
	}
	state.raid.clear();
	state.raidTarget = Point();
	state.raidSlot = -1;
	army.acted = true;
}

/// A few fast units beside the main army, on the enemy's workers: out when there is somewhere to
/// go and units enough at home; home again when hurt, halved, or with nothing left to hit.
void RaidSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	const World &world = army.world;
	const long now = army.spend.now;
	if (profile.raidSize <= 0) {
		return;
	}
	if (state.raid.empty()) {
		if (now < state.nextRaidAt) {
			return;
		}
		const Target target = RaidTarget(world, state, world.home);
		if (!target.at.Valid()) {
			state.nextRaidAt = now + 6 * RetargetSeconds; // look again later
			return;
		}
		std::vector<const Fighter *> home;
		for (const Fighter &f : world.fighters) {
			if (AtHome(world, state, f) && !army.WasSent(f.slot) && f.hp >= RaidRetreatHp && f.kind != Kind::Siege
			    && f.kind != Kind::Caster) {
				home.push_back(&f);
			}
		}
		// Twice its size at home (the rest defends), and the orders for all of it free.
		if (static_cast<int>(home.size()) < 2 * profile.raidSize || army.spend.Left() < profile.raidSize) {
			return;
		}
		std::stable_sort(home.begin(), home.end(), [&world](const Fighter *a, const Fighter *b) {
			if ((a->kind == Kind::Cavalry) != (b->kind == Kind::Cavalry)) {
				return a->kind == Kind::Cavalry; // the fast ones
			}
			return Distance(a->at, world.home) < Distance(b->at, world.home);
		});
		for (int i = 0; i < profile.raidSize; ++i) {
			if (!army.Give(Make(home[i]->slot, target.at, Order::Attack, target.slot))) {
				break;
			}
			state.raid.push_back(home[i]->slot);
		}
		state.raidTarget = target.at;
		state.raidSlot = target.slot;
		state.nextRaidAt = now + profile.raidGapSeconds;
		return;
	}
	// Hurt or halved: home.
	bool hurt = false;
	for (const Fighter &f : world.fighters) {
		if (InRaid(state, f.slot) && f.hp < RaidRetreatHp) {
			hurt = true;
		}
	}
	if (hurt || 2 * static_cast<int>(state.raid.size()) < profile.raidSize) {
		EndRaid(army);
		return;
	}
	if (now % RetargetSeconds != 0) {
		return;
	}
	const Point centre = Centre(world, state.raid);
	// A worker in sight near the raid: all of it on that worker.
	const Known *worker = nullptr;
	for (const Known &unit : world.seenUnits) {
		if (unit.cls == Class::Worker && Distance(centre, unit.at) <= EngageRadius
		    && (worker == nullptr || Distance(centre, unit.at) < Distance(centre, worker->at))) {
			worker = &unit;
		}
	}
	if (worker != nullptr) {
		if (worker->slot != state.raidSlot) {
			state.raidTarget = worker->at;
			state.raidSlot = worker->slot;
			for (const Fighter &f : world.fighters) {
				if (InRaid(state, f.slot) && !army.WasSent(f.slot)
				    && !army.Give(Make(f.slot, worker->at, Order::Attack, worker->slot))) {
					break;
				}
			}
		}
		return;
	}
	// No worker in sight: a hall it knows (the one it went for, else the next), else home.
	const bool targetKnown = std::any_of(state.known.begin(), state.known.end(),
	                                     [&state](const Known &k) { return k.slot == state.raidSlot; });
	bool allIdle = true;
	for (const Fighter &f : world.fighters) {
		if (InRaid(state, f.slot) && !f.idle) {
			allIdle = false;
		}
	}
	if (!targetKnown || (allIdle && Distance(centre, state.raidTarget) <= GatherRadius)) {
		const Target next = RaidTarget(world, state, centre);
		if (!next.at.Valid() || (next.slot == state.raidSlot && targetKnown)) {
			EndRaid(army);
			return;
		}
		state.raidTarget = next.at;
		state.raidSlot = next.slot;
	}
	for (const Fighter &f : world.fighters) {
		if (InRaid(state, f.slot) && f.idle && !army.WasSent(f.slot) && Distance(f.at, state.raidTarget) > 1
		    && !army.Give(Make(f.slot, state.raidTarget, Order::Attack))) {
			break;
		}
	}
}

/// Scouting: one worker to the nearest enemy start at the profile's second; then one fighter
/// (a fast one first) every gap, in turn to each enemy start and each scouting point.
void ScoutSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	const World &world = army.world;
	const long now = army.spend.now;
	// The worker scout goes once the base has workers to spare (from a one-worker start it waits).
	if (profile.scoutWorkerSecond > 0 && state.scouts == 0 && now >= profile.scoutWorkerSecond
	    && static_cast<int>(world.workers.size()) >= ScoutWorkersMin) {
		Point to;
		for (const Point &start : world.enemyStarts) {
			if (SameLand(world, world.home, start) && (!to.Valid() || Distance(world.home, start) < Distance(world.home, to))) {
				to = start;
			}
		}
		const Worker *scout = nullptr;
		for (const Worker &w : world.workers) {
			if (!w.repairing) {
				scout = &w;
				break;
			}
		}
		if (to.Valid() && scout != nullptr && army.Give(Make(scout->slot, to, Order::Move))) {
			state.scouts = 1;
		} else if (!to.Valid()) {
			state.scouts = 1; // no enemy start to look at (on its own land: the flyers and the fleet look further)
		}
	}
	if (profile.scoutGapSeconds <= 0 || now < state.nextScoutAt) {
		return;
	}
	const int starts = static_cast<int>(std::min<size_t>(world.enemyStarts.size(), 32));
	const int places = starts + (HasScoutPoints(world) ? ScoutPoints : 0);
	if (places == 0) {
		state.nextScoutAt = now + profile.scoutGapSeconds;
		return;
	}
	// A fast unit only (the design's "one fast unit every ~5 min"): a footman or an archer sent
	// alone to the enemy start was the army thrown away one by one (round 3.4.66: the Captain's and
	// the Warlord's only fighter walked to the person's start and died, again and again).
	const Fighter *scout = nullptr;
	for (const Fighter &f : world.fighters) {
		if (!AtHome(world, state, f) || army.WasSent(f.slot) || f.kind != Kind::Cavalry) {
			continue;
		}
		scout = &f;
		break;
	}
	if (scout == nullptr) {
		state.nextScoutAt = now + 2 * RetargetSeconds; // none at home: soon again
		return;
	}
	// the next place in turn that lies on its own land (a land scout does not walk over the sea)
	int index = state.scoutIndex % places;
	const auto placeAt = [&](int i) { return i < starts ? world.enemyStarts[i] : ScoutPoint(world, i - starts); };
	int tries = 0;
	while (tries < places && !SameLand(world, world.home, placeAt(index))) {
		index = (index + 1) % places;
		++tries;
	}
	if (tries == places) {
		state.nextScoutAt = now + profile.scoutGapSeconds;
		return;
	}
	const Point to = placeAt(index);
	if (army.Give(Make(scout->slot, to, Order::Move))) {
		state.scoutIndex = (index + 1) % places;
		++state.scouts;
		state.nextScoutAt = now + profile.scoutGapSeconds;
	}
}

/// A new hall away from home gets the profile's escort of home units, once.
void EscortSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	const World &world = army.world;
	if (profile.expansions.empty() || state.escorted >= static_cast<int>(world.farHalls.size())) {
		return;
	}
	const size_t step = std::min(static_cast<size_t>(state.escorted), profile.expansions.size() - 1);
	const int escort = profile.expansions[step].escort;
	const Point hall = world.farHalls[state.escorted];
	++state.escorted;
	std::vector<const Fighter *> home;
	for (const Fighter &f : world.fighters) {
		if (AtHome(world, state, f) && !army.WasSent(f.slot) && f.kind != Kind::Caster) {
			home.push_back(&f);
		}
	}
	std::stable_sort(home.begin(), home.end(), [&hall](const Fighter *a, const Fighter *b) {
		return Distance(a->at, hall) < Distance(b->at, hall);
	});
	for (int i = 0; i < escort && i < static_cast<int>(home.size()); ++i) {
		if (Distance(home[i]->at, hall) <= GatherRadius) {
			continue;
		}
		if (!army.Give(Make(home[i]->slot, hall))) {
			break;
		}
	}
}

bool CastBusy(const State &state, int slot)
{
	return std::any_of(state.castBusy.begin(), state.castBusy.end(),
	                   [slot](const std::pair<int, long> &busy) { return busy.first == slot; });
}

/// Own units with an enemy land fighter within the fight radius.
int OwnInFight(const World &world)
{
	int n = 0;
	for (const Fighter &f : world.fighters) {
		if (!f.aboard && EnemyFightersNear(world, f.at, FightRadius) > 0) {
			++n;
		}
	}
	return n;
}

/// Unholy armour costs half the hit points: only a fighter with this per cent at least gets it.
constexpr int UnholyFrom = 80;

/// One spell's target for one caster, or false. `used` holds the enemies already aimed at this
/// second (one weakening spell per enemy).
bool SpellTarget(const Profile &profile, const World &world, const State &state, const Fighter &caster, Spell spell,
                 const std::vector<int> &used, Command &out)
{
	const int range = world.spellRange[static_cast<int>(spell)];
	if (range <= 0) {
		return false;
	}
	switch (spell) {
		case Spell::Bloodlust:
		case Spell::Haste:
		case Spell::UnholyArmour: {
			// A buff when the fight starts: the own melee unit in reach nearest to an enemy fighter
			// (unholy armour only on one with most of its hit points: it costs half of them).
			if (OwnInFight(world) < profile.fightMin) {
				return false;
			}
			const bool armour = spell == Spell::UnholyArmour;
			const Fighter *best = nullptr;
			int bestDistance = 0;
			for (const Fighter &f : world.fighters) {
				if (f.aboard || (armour ? (f.armoured || f.hp < UnholyFrom) : f.buffed) || Distance(caster.at, f.at) > range
				    || ((spell == Spell::Bloodlust || armour) && f.kind != Kind::Soldier && f.kind != Kind::Cavalry)
				    || f.kind == Kind::Siege || f.kind == Kind::Caster) {
					continue;
				}
				int nearest = -1;
				for (const Known &unit : world.seenUnits) {
					if (IsArmy(unit.cls) && (nearest < 0 || Distance(f.at, unit.at) < nearest)) {
						nearest = Distance(f.at, unit.at);
					}
				}
				if (nearest < 0 || nearest > FightRadius) {
					continue;
				}
				if (best == nullptr || nearest < bestDistance) {
					best = &f;
					bestDistance = nearest;
				}
			}
			if (best == nullptr) {
				return false;
			}
			out = Make(caster.slot, best->at, Order::Cast, best->slot, static_cast<int>(spell));
			return true;
		}
		case Spell::Healing: {
			const Fighter *best = nullptr;
			for (const Fighter &f : world.fighters) {
				if (!f.aboard && f.hp < HealBelow && Distance(caster.at, f.at) <= range && (best == nullptr || f.hp < best->hp)) {
					best = &f;
				}
			}
			if (best == nullptr) {
				return false;
			}
			out = Make(caster.slot, best->at, Order::Cast, best->slot, static_cast<int>(spell));
			return true;
		}
		case Spell::Slow:
		case Spell::Polymorph:
		case Spell::DeathCoil:
		case Spell::Exorcism: {
			// One enemy unit in range: the nearest, or (best) by the target order, then the fewest
			// hit points, then the nearest. Exorcism only on the undead.
			const Known *best = nullptr;
			int bestRank = 0;
			int bestDistance = 0;
			for (const Known &unit : world.seenUnits) {
				const int d = Distance(caster.at, unit.at);
				if (d > range || Contains(used, unit.slot)
				    || !(IsArmy(unit.cls) || unit.cls == Class::Worker || (spell == Spell::Exorcism && unit.undead))) {
					continue;
				}
				if (spell == Spell::Exorcism && !unit.undead) {
					continue;
				}
				if (spell == Spell::Slow && unit.affected) {
					continue;
				}
				if (spell == Spell::Polymorph
				    && (unit.hp < PolymorphFrom || !(unit.cls == Class::Cavalry || unit.cls == Class::Caster))) {
					continue;
				}
				if (spell == Spell::DeathCoil && unit.cls == Class::Siege) {
					continue; // not alive: the coil does nothing to it
				}
				const int rank = profile.castBest ? AimRank(profile, unit, false, world) : 0;
				const bool better = best == nullptr || rank < bestRank
				                    || (rank == bestRank && profile.castBest && unit.hp < best->hp)
				                    || (rank == bestRank && (!profile.castBest || unit.hp == best->hp) && d < bestDistance);
				if (better) {
					best = &unit;
					bestRank = rank;
					bestDistance = d;
				}
			}
			if (best == nullptr) {
				return false;
			}
			out = Make(caster.slot, best->at, Order::Cast, best->slot, static_cast<int>(spell));
			return true;
		}
		case Spell::Blizzard:
		case Spell::DeathAndDecay: {
			// A place: the nearest enemy fighter's tile, or (best) the enemy fighter's tile with the
			// most enemy units around it (ClusterMin at least); never with an own unit beside it.
			const Known *best = nullptr;
			int bestCount = 0;
			int bestDistance = 0;
			for (const Known &unit : world.seenUnits) {
				const int d = Distance(caster.at, unit.at);
				if (d > range || !IsArmy(unit.cls)) {
					continue;
				}
				bool friendly = false;
				for (const Fighter &f : world.fighters) {
					if (Distance(f.at, unit.at) <= 1) {
						friendly = true;
					}
				}
				if (friendly) {
					continue;
				}
				int around = 0;
				for (const Known &other : world.seenUnits) {
					if (Distance(other.at, unit.at) <= 1) {
						++around;
					}
				}
				if (profile.castBest && around < ClusterMin) {
					continue;
				}
				const bool better = best == nullptr || (profile.castBest && around > bestCount)
				                    || ((!profile.castBest || around == bestCount) && d < bestDistance);
				if (better) {
					best = &unit;
					bestCount = around;
					bestDistance = d;
				}
			}
			if (best == nullptr) {
				return false;
			}
			out = Make(caster.slot, best->at, Order::Cast, -1, static_cast<int>(spell));
			return true;
		}
		case Spell::Invisibility: {
			// A loaded transport of the landing under way, in reach, not hidden yet (the first by slot).
			if (state.landingPhase != 1 && state.landingPhase != 2) {
				return false;
			}
			for (const Fighter &ship : world.ships) {
				if (Contains(state.ferry, ship.slot) && ship.cargo > 0 && !ship.invisible
				    && Distance(caster.at, ship.at) <= range) {
					out = Make(caster.slot, ship.at, Order::Cast, ship.slot, static_cast<int>(spell));
					return true;
				}
			}
			return false;
		}
		default: return false;
	}
}

/// Casting: every caster that can cast now and was not given a cast order in the last seconds
/// casts the first of the profile's spells it has a target for (one order each).
void SpellsSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	const World &world = army.world;
	if (profile.casts.empty()) {
		return;
	}
	std::vector<int> used;
	for (const Fighter &f : world.fighters) {
		if (f.spells == 0 || f.aboard || army.WasSent(f.slot) || CastBusy(state, f.slot)) {
			continue;
		}
		for (const Spell spell : profile.casts) {
			Command command;
			if (!(f.spells & SpellBit(spell)) || !SpellTarget(profile, world, state, f, spell, used, command)) {
				continue;
			}
			if (!army.Give(command)) {
				return;
			}
			state.castBusy.emplace_back(f.slot, army.spend.now);
			if (command.targetSlot >= 0) {
				used.push_back(command.targetSlot);
			}
			break;
		}
	}
}

/// The army's second. True when it spent an order or a wave began or ended.
// ---------------------------------------------------------------------------- sea and air

/// The units of a landing have this long to board; the transports to sail and unload.
constexpr long BoardSeconds = 60;
constexpr long SailSeconds = 180;
/// No enemy warship seen for this long: the sea counts as clear.
constexpr long SeaClearSeconds = 60;
/// The transports unload at least this far from the landing's goal when its shore allows.
constexpr int LandingAway = 6;
/// Transports are trained this long before the first landing is due.
constexpr long TransportLeadSeconds = 120;
/// Ships / buildings the fleet could not get to, at most this many remembered.
constexpr size_t FleetIgnoredMax = 32;

bool OfKind(const Fighter &f, Kind kind)
{
	return f.kind == kind;
}

/// Where a landing goes: a known enemy building on other land (a hall first, the nearest to home),
/// else an enemy start on other land not found empty yet (the nearest). Invalid: none.
Point LandingGoal(const World &world, const State &state)
{
	Point best;
	int bestDistance = -1;
	const auto consider = [&](const Point &at) {
		const int d = Distance(world.home, at);
		if (bestDistance < 0 || d < bestDistance) {
			bestDistance = d;
			best = at;
		}
	};
	const auto abroad = [&](const Point &at) { return Region(world, at) > 0 && !SameLand(world, world.home, at); };
	for (const bool hallsOnly : {true, false}) {
		for (const Known &known : state.known) {
			if ((!hallsOnly || known.cls == Class::Hall) && known.cls != Class::Platform && !Ignored(state, known.slot)
			    && abroad(known.at)) {
				consider(known.at);
			}
		}
		if (best.Valid()) {
			return best;
		}
	}
	for (size_t i = 0; i < world.enemyStarts.size() && i < 32; ++i) {
		if (!(state.visitedStarts & (1u << i)) && abroad(world.enemyStarts[i])) {
			consider(world.enemyStarts[i]);
		}
	}
	return best;
}

/// The landing ends: its transports sail back to the water by home (when `home`), its units are
/// free again (aboard ones stay aboard until a later landing's end puts them ashore).
void EndLanding(ArmyContext &army, bool home)
{
	State &state = army.spend.state;
	const World &world = army.world;
	Point land;
	Point water;
	if (home && ShoreOf(world, world.home, 0, land, water)) {
		for (const Fighter &ship : world.ships) {
			if (Contains(state.ferry, ship.slot) && !army.WasSent(ship.slot) && Distance(ship.at, water) > GatherRadius
			    && !army.Give(Make(ship.slot, water, Order::Move))) {
				break;
			}
		}
	}
	state.landing.clear();
	state.ferry.clear();
	state.landingPhase = 0;
	state.landingSince = -1;
	state.landingShore = Point();
	state.landingWater = Point();
	state.nextLandingAt = army.spend.now + army.spend.profile.landingGapSeconds;
	army.acted = true;
}

/// Transport landings on a sea map: home units board the transports at the shore by home, the
/// transports unload them on the shore of the goal's land, the landed units are the wave there.
/// The Captain waits until no enemy warship was seen for a while; the Warlord only for its cover.
void LandingSecond(ArmyContext &army, Dice dice, void *context)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	const World &world = army.world;
	const long now = army.spend.now;
	if (profile.landingMax <= 0) {
		return;
	}
	const int slots = std::max(1, world.transportSlots);
	if (state.landingPhase == 0) {
		if (now < state.nextLandingAt || !state.wave.empty() || !SeaMap(profile, world)) {
			return;
		}
		const Point goal = LandingGoal(world, state);
		if (!goal.Valid()) {
			state.nextLandingAt = now + 6 * RetargetSeconds;
			return;
		}
		if (profile.landingClear && state.enemyShipAt >= 0 && now - state.enemyShipAt < SeaClearSeconds) {
			return;
		}
		const int cover = static_cast<int>(std::count_if(world.ships.begin(), world.ships.end(), [&](const Fighter &s) {
			return IsWarship(s.kind) && !Contains(state.fleet, s.slot);
		}));
		if (cover < profile.landingCover) {
			return;
		}
		if (state.landingSize == 0) {
			int roll = profile.landingMax > profile.landingMin && dice
			               ? dice(profile.landingMax - profile.landingMin + 1, context)
			               : 0;
			if (roll < 0 || roll > profile.landingMax - profile.landingMin) {
				roll = 0;
			}
			state.landingSize = profile.landingMin + roll;
		}
		const int size = state.landingSize;
		const int needed = (size + slots - 1) / slots;
		std::vector<const Fighter *> ferry;
		for (const Fighter &ship : world.ships) {
			if (OfKind(ship, Kind::Transport) && ship.idle && ship.cargo == 0 && !army.WasSent(ship.slot)) {
				ferry.push_back(&ship);
			}
		}
		std::vector<const Fighter *> units;
		for (const Fighter &f : world.fighters) {
			if (AtHome(world, state, f) && !army.WasSent(f.slot) && !(profile.pullHp > 0 && f.hp < profile.pullHp)) {
				units.push_back(&f);
			}
		}
		if (static_cast<int>(ferry.size()) < needed || static_cast<int>(units.size()) < size
		    || army.spend.Left() < needed + size) {
			return;
		}
		Point homeLand;
		Point homeWater;
		Point shore;
		Point water;
		if (!ShoreOf(world, world.home, 0, homeLand, homeWater) || !ShoreOf(world, goal, LandingAway, shore, water)) {
			state.nextLandingAt = now + profile.landingGapSeconds;
			return;
		}
		std::stable_sort(ferry.begin(), ferry.end(), [&homeWater](const Fighter *a, const Fighter *b) {
			return Distance(a->at, homeWater) < Distance(b->at, homeWater);
		});
		std::stable_sort(units.begin(), units.end(), [&homeLand](const Fighter *a, const Fighter *b) {
			if ((a->kind == Kind::Cavalry) != (b->kind == Kind::Cavalry)) {
				return a->kind == Kind::Cavalry; // the strongest go
			}
			return Distance(a->at, homeLand) < Distance(b->at, homeLand);
		});
		for (int i = 0; i < needed; ++i) {
			state.ferry.push_back(ferry[i]->slot);
			if (Distance(ferry[i]->at, homeWater) > 1) {
				army.Give(Make(ferry[i]->slot, homeWater, Order::Move));
			}
		}
		for (int i = 0; i < size; ++i) {
			const Fighter &boat = *ferry[i / slots];
			state.landing.push_back(units[i]->slot);
			army.Give(Make(units[i]->slot, boat.at, Order::Board, boat.slot));
		}
		state.landingPhase = 1;
		state.landingSince = now;
		state.landingShore = shore;
		state.landingWater = water;
		state.landingSize = 0;
		army.acted = true;
		return;
	}
	if (state.landing.empty() || state.ferry.empty()) {
		EndLanding(army, true); // all of it lost
		return;
	}
	int aboard = 0;
	for (const Fighter &f : world.fighters) {
		if (Contains(state.landing, f.slot) && f.aboard) {
			++aboard;
		}
	}
	if (state.landingPhase == 1) {
		const bool timeUp = now - state.landingSince >= BoardSeconds;
		if (aboard == static_cast<int>(state.landing.size()) || (timeUp && aboard > 0)) {
			// Those not aboard stay home; the loaded transports sail and unload on the goal's shore.
			state.landing.erase(std::remove_if(state.landing.begin(), state.landing.end(),
			                                   [&world](int slot) {
				                                   return std::none_of(world.fighters.begin(), world.fighters.end(),
				                                                       [slot](const Fighter &f) { return f.slot == slot && f.aboard; });
			                                   }),
			                    state.landing.end());
			std::vector<int> sailing;
			for (const Fighter &ship : world.ships) {
				if (Contains(state.ferry, ship.slot) && ship.cargo > 0) {
					sailing.push_back(ship.slot);
					army.Give(Make(ship.slot, state.landingShore, Order::Unload));
				}
			}
			if (sailing.empty()) {
				EndLanding(army, true);
				return;
			}
			state.ferry = sailing;
			state.landingPhase = 2;
			state.landingSince = now;
			army.acted = true;
			return;
		}
		if (timeUp) {
			EndLanding(army, true); // nobody got aboard
			return;
		}
		if (now % RetargetSeconds == 0) {
			size_t next = 0;
			for (const Fighter &f : world.fighters) {
				if (!Contains(state.landing, f.slot) || f.aboard || !f.idle || army.WasSent(f.slot)) {
					continue;
				}
				const int boat = state.ferry[next++ % state.ferry.size()];
				const auto it = std::find_if(world.ships.begin(), world.ships.end(), [boat](const Fighter &s) { return s.slot == boat; });
				if (it != world.ships.end() && !army.Give(Make(f.slot, it->at, Order::Board, boat))) {
					break;
				}
			}
		}
		return;
	}
	// Sailing (2: to the goal, 3: back home with what could not be put ashore).
	if (aboard == 0) {
		if (state.landingPhase == 2) {
			// Landed: they are the wave on that land (its target is picked from where they stand).
			for (const int slot : state.landing) {
				if (!InWave(state, slot)) {
					state.wave.push_back(slot);
				}
			}
			state.target = Point();
			state.targetSlot = -1;
			state.gatherUntil = -1;
			state.idleAt = Point();
			state.focusSlot = -1;
			state.launchSize = static_cast<int>(state.wave.size());
			++state.landings;
		}
		EndLanding(army, true);
		return;
	}
	if (now - state.landingSince >= SailSeconds) {
		Point homeLand;
		Point homeWater;
		if (state.landingPhase == 2 && ShoreOf(world, world.home, 0, homeLand, homeWater)) {
			// The goal's shore cannot be reached: back home, ashore there.
			state.landingPhase = 3;
			state.landingSince = now;
			state.landingShore = homeLand;
			state.landingWater = homeWater;
			for (const Fighter &ship : world.ships) {
				if (Contains(state.ferry, ship.slot)) {
					army.Give(Make(ship.slot, homeLand, Order::Unload));
				}
			}
			army.acted = true;
			return;
		}
		EndLanding(army, false);
		return;
	}
	if (now % RetargetSeconds == 0) {
		for (const Fighter &ship : world.ships) {
			if (Contains(state.ferry, ship.slot) && ship.idle && ship.cargo > 0 && !army.WasSent(ship.slot)
			    && !army.Give(Make(ship.slot, state.landingShore, Order::Unload))) {
				break;
			}
		}
	}
}

/// The fleet's next goal from `from`. With a fleet target order: class by class, an enemy unit on
/// the water its units see in reach, or a sea building it knows. Then the nearest enemy unit on the
/// water in sight, the nearest known sea building, and the water by an enemy start not looked at
/// yet. Invalid: nothing left to do.
Target PickSeaTarget(const Profile &profile, const World &world, const State &state, const Point &from)
{
	Target best;
	int bestDistance = -1;
	const auto consider = [&](const Point &at, int slot) {
		const int d = Distance(from, at);
		if (bestDistance < 0 || d < bestDistance) {
			bestDistance = d;
			best.at = at;
			best.slot = slot;
		}
	};
	const auto unitOk = [&](const Known &unit) { return IsSeaUnit(unit.cls) && !Contains(state.fleetIgnored, unit.slot); };
	const auto buildingOk = [&](const Known &b) { return IsSeaBuilding(b.cls) && !Contains(state.fleetIgnored, b.slot); };
	for (const Aim aim : profile.fleetTargets) {
		for (const Known &unit : world.seenUnits) {
			if (unitOk(unit) && Distance(from, unit.at) <= EngageRadius && AimMatches(aim, unit, false, world)) {
				consider(unit.at, unit.slot);
			}
		}
		for (const Known &known : state.known) {
			if (buildingOk(known) && AimMatches(aim, known, true, world)) {
				consider(known.at, known.slot);
			}
		}
		if (best.at.Valid()) {
			return best;
		}
	}
	for (const Known &unit : world.seenUnits) {
		if (unitOk(unit)) {
			consider(unit.at, unit.slot);
		}
	}
	if (best.at.Valid()) {
		return best;
	}
	for (const Known &known : state.known) {
		if (buildingOk(known)) {
			consider(known.at, known.slot);
		}
	}
	if (best.at.Valid()) {
		return best;
	}
	for (size_t i = 0; i < world.enemyStarts.size() && i < 32; ++i) {
		if (!(state.fleetVisited & (1u << i))) {
			const Point water = NearestWater(world, world.enemyStarts[i]);
			if (water.Valid()) {
				consider(water, -1);
			}
		}
	}
	return best;
}

/// The fleet comes back to the water by home and is a fleet no more.
void FleetHome(ArmyContext &army)
{
	State &state = army.spend.state;
	const World &world = army.world;
	const Point water = NearestWater(world, world.home);
	for (const Fighter &ship : world.ships) {
		if (Contains(state.fleet, ship.slot) && water.Valid() && !army.WasSent(ship.slot)
		    && Distance(ship.at, water) > GatherRadius && !army.Give(Make(ship.slot, water, Order::Move))) {
			break;
		}
	}
	state.fleet.clear();
	state.fleetTarget = Point();
	state.fleetSlot = -1;
	state.fleetIdleAt = Point();
	state.fleetIgnored.clear();
	state.fleetVisited = 0;
	state.nextFleetAt = std::max(state.nextFleetAt, army.spend.now + 6 * RetargetSeconds);
	army.acted = true;
}

/// The warships: at home they meet an enemy unit on the water near home and cover a landing; with
/// the profile's number at home the fleet sets out for its targets (the Warlord: the oil platform
/// first), comes home when halved or with nothing left.
void FleetSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	const World &world = army.world;
	const long now = army.spend.now;
	std::vector<const Fighter *> home;
	for (const Fighter &ship : world.ships) {
		if (IsWarship(ship.kind) && !Contains(state.fleet, ship.slot) && !army.WasSent(ship.slot)) {
			home.push_back(&ship);
		}
	}
	if (home.empty() && state.fleet.empty()) {
		return;
	}
	// An enemy unit on the water near home: the warships at home go for the nearest one.
	const Known *threat = nullptr;
	for (const Known &unit : world.seenUnits) {
		if (IsSeaUnit(unit.cls) && Distance(world.home, unit.at) <= profile.defenceRadius
		    && (threat == nullptr || Distance(world.home, unit.at) < Distance(world.home, threat->at))) {
			threat = &unit;
		}
	}
	if (threat != nullptr) {
		for (const Fighter *ship : home) {
			if (ship->idle && !army.Give(Make(ship->slot, threat->at, Order::Attack, threat->slot))) {
				break;
			}
		}
	} else if (state.landingPhase == 2 && profile.landingCover > 0 && state.landingWater.Valid()
	           && now % RetargetSeconds == 0) {
		// The landing's cover: the warships at home sail to the water by the landing place.
		int n = 0;
		for (const Fighter *ship : home) {
			if (n++ >= profile.landingCover) {
				break;
			}
			if (ship->idle && Distance(ship->at, state.landingWater) > GatherRadius
			    && !army.Give(Make(ship->slot, state.landingWater, Order::Attack))) {
				break;
			}
		}
	}
	if (state.fleet.empty()) {
		if (profile.fleetMin <= 0 || now < state.nextFleetAt || threat != nullptr || state.landingPhase != 0) {
			return;
		}
		std::vector<const Fighter *> ready;
		for (const Fighter *ship : home) {
			if (ship->idle) {
				ready.push_back(ship);
			}
		}
		if (static_cast<int>(ready.size()) < profile.fleetMin || army.spend.Left() < static_cast<int>(ready.size())) {
			return;
		}
		const Point water = NearestWater(world, world.home);
		const Target target = PickSeaTarget(profile, world, state, water.Valid() ? water : world.home);
		if (!target.at.Valid()) {
			state.nextFleetAt = now + 6 * RetargetSeconds;
			return;
		}
		for (const Fighter *ship : ready) {
			if (!army.Give(Make(ship->slot, target.at, Order::Attack, target.slot))) {
				break;
			}
			state.fleet.push_back(ship->slot);
		}
		state.fleetTarget = target.at;
		state.fleetSlot = target.slot;
		state.fleetLaunch = static_cast<int>(state.fleet.size());
		state.fleetIdleAt = target.at;
		state.nextFleetAt = now + profile.fleetGapSeconds;
		return;
	}
	if (2 * static_cast<int>(state.fleet.size()) < state.fleetLaunch) {
		FleetHome(army); // halved
		return;
	}
	if (now % RetargetSeconds != 0) {
		return;
	}
	const Point centre = CentreOf(world, world.ships, state.fleet);
	// The water by an enemy start reached: looked at.
	for (size_t i = 0; i < world.enemyStarts.size() && i < 32; ++i) {
		const Point water = NearestWater(world, world.enemyStarts[i]);
		for (const Fighter &ship : world.ships) {
			if (water.Valid() && Contains(state.fleet, ship.slot) && Distance(ship.at, water) <= VisitRadius) {
				state.fleetVisited |= 1u << i;
			}
		}
	}
	// Every ship of the fleet idle short of the goal it was sent to: it cannot get there.
	const bool allIdle = std::all_of(world.ships.begin(), world.ships.end(),
	                                 [&](const Fighter &s) { return !Contains(state.fleet, s.slot) || s.idle; });
	if (allIdle && state.fleetTarget.Valid() && state.fleetIdleAt == state.fleetTarget
	    && Distance(centre, state.fleetTarget) > VisitRadius) {
		if (state.fleetSlot >= 0) {
			if (state.fleetIgnored.size() >= FleetIgnoredMax) {
				state.fleetIgnored.erase(state.fleetIgnored.begin());
			}
			state.fleetIgnored.push_back(state.fleetSlot);
		} else {
			for (size_t i = 0; i < world.enemyStarts.size() && i < 32; ++i) {
				if (NearestWater(world, world.enemyStarts[i]) == state.fleetTarget) {
					state.fleetVisited |= 1u << i;
				}
			}
		}
		state.fleetIdleAt = Point();
	}
	const Target next = PickSeaTarget(profile, world, state, centre);
	if (!next.at.Valid()) {
		FleetHome(army);
		return;
	}
	state.fleetTarget = next.at;
	state.fleetSlot = next.slot;
	bool any = false;
	for (const Fighter &ship : world.ships) {
		if (!Contains(state.fleet, ship.slot) || !ship.idle || army.WasSent(ship.slot) || Distance(ship.at, next.at) <= 1) {
			continue;
		}
		if (!army.Give(Make(ship.slot, next.at, Order::Attack, next.slot))) {
			break;
		}
		any = true;
	}
	if (any) {
		state.fleetIdleAt = state.fleetTarget;
	}
}

/// The riders' next goal: by the profile's air target order an enemy unit its units see in reach
/// or a building it knows; then the nearest known building; then the nearest enemy unit in sight.
/// Flyers go over land and sea alike.
Target PickAirTarget(const Profile &profile, const World &world, const State &state, const Point &from)
{
	Target best;
	int bestDistance = -1;
	const auto consider = [&](const Point &at, int slot) {
		const int d = Distance(from, at);
		if (bestDistance < 0 || d < bestDistance) {
			bestDistance = d;
			best.at = at;
			best.slot = slot;
		}
	};
	for (const Aim aim : profile.airTargets) {
		for (const Known &unit : world.seenUnits) {
			if (Distance(from, unit.at) <= EngageRadius && AimMatches(aim, unit, false, world)) {
				consider(unit.at, unit.slot);
			}
		}
		for (const Known &known : state.known) {
			if (AimMatches(aim, known, true, world)) {
				consider(known.at, known.slot);
			}
		}
		if (best.at.Valid()) {
			return best;
		}
	}
	for (const Known &known : state.known) {
		consider(known.at, known.slot);
	}
	if (best.at.Valid()) {
		return best;
	}
	for (const Known &unit : world.seenUnits) {
		consider(unit.at, unit.slot);
	}
	return best;
}

void RidersHome(ArmyContext &army)
{
	State &state = army.spend.state;
	for (const Fighter &f : army.world.flyers) {
		if (Contains(state.riders, f.slot) && !army.WasSent(f.slot) && !army.Give(Make(f.slot, army.world.home, Order::Move))) {
			break;
		}
	}
	state.riders.clear();
	state.riderTarget = Point();
	state.riderSlot = -1;
	army.acted = true;
}

/// The air: flying scouts in turn to every enemy start and scouting point (one flight every gap, or a
/// picket that always flies on), gryphons / dragons out in a group of the profile's size.
void AirSecond(ArmyContext &army)
{
	const Profile &profile = army.spend.profile;
	State &state = army.spend.state;
	const World &world = army.world;
	const long now = army.spend.now;
	if (profile.airScouts > 0) {
		const int starts = static_cast<int>(std::min<size_t>(world.enemyStarts.size(), 32));
		const int places = starts + (HasScoutPoints(world) ? ScoutPoints : 0);
		const bool picket = profile.airScoutGapSeconds == 0;
		if (places > 0 && (picket ? now % RetargetSeconds == 0 : now >= state.nextAirScoutAt)) {
			for (const Fighter &f : world.flyers) {
				if (!OfKind(f, Kind::Balloon) || !f.idle || army.WasSent(f.slot)) {
					continue;
				}
				const int index = state.airScoutIndex % places;
				const Point to = index < starts ? world.enemyStarts[index] : ScoutPoint(world, index - starts);
				if (!army.Give(Make(f.slot, to, Order::Move))) {
					break;
				}
				state.airScoutIndex = (index + 1) % places;
				if (!picket) {
					state.nextAirScoutAt = now + profile.airScoutGapSeconds;
					break; // one flight a gap
				}
			}
		}
	}
	if (profile.riderGroup <= 0) {
		return;
	}
	if (state.riders.empty()) {
		std::vector<const Fighter *> ready;
		for (const Fighter &f : world.flyers) {
			if (OfKind(f, Kind::Rider) && f.idle && !army.WasSent(f.slot)) {
				ready.push_back(&f);
			}
		}
		if (static_cast<int>(ready.size()) < profile.riderGroup || army.spend.Left() < static_cast<int>(ready.size())) {
			return;
		}
		const Target target = PickAirTarget(profile, world, state, world.home);
		if (!target.at.Valid()) {
			return;
		}
		for (const Fighter *f : ready) {
			if (!army.Give(Make(f->slot, target.at, Order::Attack, target.slot))) {
				break;
			}
			state.riders.push_back(f->slot);
		}
		state.riderTarget = target.at;
		state.riderSlot = target.slot;
		state.riderLaunch = static_cast<int>(state.riders.size());
		return;
	}
	if (2 * static_cast<int>(state.riders.size()) < state.riderLaunch) {
		RidersHome(army);
		return;
	}
	if (now % RetargetSeconds != 0) {
		return;
	}
	const Target next = PickAirTarget(profile, world, state, CentreOf(world, world.flyers, state.riders));
	if (!next.at.Valid()) {
		RidersHome(army);
		return;
	}
	state.riderTarget = next.at;
	state.riderSlot = next.slot;
	for (const Fighter &f : world.flyers) {
		if (Contains(state.riders, f.slot) && f.idle && !army.WasSent(f.slot) && Distance(f.at, next.at) > 1
		    && !army.Give(Make(f.slot, next.at, Order::Attack, next.slot))) {
			break;
		}
	}
}

bool ArmySecond(const World &world, Spend &spend, Dice dice, void *context)
{
	ArmyContext army{world, spend, {}, false};
	DefenceSecond(army);
	ClearBaseSecond(army);
	PullHurtSecond(army);
	RetreatSecond(army);
	LandingSecond(army, dice, context);
	WaveSecond(army, dice, context);
	RaidSecond(army);
	ScoutSecond(army);
	EscortSecond(army);
	FleetSecond(army);
	AirSecond(army);
	SpellsSecond(army);
	return army.acted;
}

/// Enemy cavalry seen at once from which the counters answer it.
constexpr int CounterCavalry = 3;
/// Enemy casters seen at once from which more cavalry is trained (to reach them fast).
constexpr int CounterCasters = 2;
/// Gold a repair needs at least (it costs a share of the building's price).
constexpr int RepairGold = 100;

/// The weight of a fighter kind (index into FighterKindList) now: the profile's, with siege only
/// while a tower is known when the profile says so, and raised by the counters to what its own
/// units have seen (many knights / ogres: more archers / axe throwers; flyers: more of them too;
/// casters: more cavalry to reach them).
int Weight(const Profile &profile, const State &state, int i)
{
	const Kind kind = FighterKindList[i];
	int weight = profile.weight[i];
	if (kind == Kind::Siege && profile.siegeVsTowers && !KnowsTower(state)) {
		return 0;
	}
	if (!profile.counters || weight <= 0) {
		return weight;
	}
	const int cavalry = state.seenPeak[static_cast<int>(Class::Cavalry)];
	const int soldiers = state.seenPeak[static_cast<int>(Class::Soldier)];
	if (kind == Kind::Shooter) {
		if (cavalry >= CounterCavalry && 2 * cavalry > soldiers) {
			weight += std::max(1, profile.weight[0]);
		}
		if (state.seenPeak[static_cast<int>(Class::Flyer)] > 0) {
			weight += 2;
		}
	}
	if (kind == Kind::Cavalry && state.seenPeak[static_cast<int>(Class::Caster)] >= CounterCasters) {
		weight += 1;
	}
	return weight;
}

/// The fighter kind to train next by the profile's mix: the one furthest below its share. Each kind
/// is held to its share of the army's size (rounded up) and to its own most, counted over the kinds
/// the plan has at this time. A kind that cannot be trained yet (no lumber mill for shooters)
/// keeps its places free when the profile holds places; otherwise the share is counted over the
/// kinds that can be trained now.
bool PickFighter(const Profile &profile, const State &state, const World &world, long now, Kind &pick)
{
	int weight[FighterKinds];
	bool inPlan[FighterKinds];
	int planWeight = 0;
	for (int i = 0; i < FighterKinds; ++i) {
		weight[i] = Weight(profile, state, i);
		inPlan[i] = weight[i] > 0 && now >= profile.fromSecond[i]
		            && (profile.holdPlaces || world.can[KindIndex(FighterKindList[i])]);
		if (inPlan[i]) {
			planWeight += weight[i];
		}
	}
	if (planWeight == 0) {
		return false;
	}
	int best = -1;
	for (int i = 0; i < FighterKinds; ++i) {
		const int k = KindIndex(FighterKindList[i]);
		if (!inPlan[i] || !world.can[k]) {
			continue;
		}
		int share = (profile.armyMax * weight[i] + planWeight - 1) / planWeight;
		if (profile.maxOf[i] >= 0) {
			share = std::min(share, profile.maxOf[i]);
		}
		if (world.have[k] + world.queued[k] >= share) {
			continue;
		}
		// have / weight smallest, compared without division
		if (best < 0 || world.have[k] * weight[best] < world.have[KindIndex(FighterKindList[best])] * weight[i]) {
			best = i;
		}
	}
	if (best < 0) {
		return false;
	}
	pick = FighterKindList[best];
	return true;
}

bool PlansTowers(const Profile &profile)
{
	return profile.counters
	       || std::any_of(profile.opening.begin(), profile.opening.end(),
	                      [](const OpeningStep &step) { return step.what == Kind::Tower; });
}

/// Towers: one more when the counters ask (guard towers against flyers seen, cannon towers against
/// many cavalry seen, each once), and a finished watch tower made a guard / cannon tower.
void TowersSecond(const World &world, Spend &spend)
{
	const Profile &profile = spend.profile;
	State &state = spend.state;
	Decision &decision = spend.decision;
	if (!PlansTowers(profile)) {
		return;
	}
	const int tower = KindIndex(Kind::Tower);
	if (profile.counters && world.can[tower] && !decision.release[tower] && world.queued[tower] == 0) {
		uint32_t want = 0;
		if (state.seenPeak[static_cast<int>(Class::Flyer)] > 0) {
			want |= 1u;
		}
		if (state.seenPeak[static_cast<int>(Class::Cavalry)] >= CounterCavalry) {
			want |= 2u;
		}
		const uint32_t fresh = want & ~state.counterTowers;
		if (fresh != 0 && spend.Affords(world, Kind::Tower) && spend.Take()) {
			decision.release[tower] = 1;
			++state.planned[tower];
			state.counterTowers |= (fresh & 1u) ? 1u : 2u;
		}
	}
	if (world.plainTowers > 0) {
		const bool flyers = state.seenPeak[static_cast<int>(Class::Flyer)] > 0;
		const bool cannon = (profile.cannonTowers || ((state.counterTowers & 2u) && !flyers)) && world.canTower[1];
		const int kind = cannon ? 2 : (world.canTower[0] ? 1 : 0);
		if (kind != 0 && spend.Take()) {
			decision.towerUpgrade = kind;
		}
	}
}

/// Repair: while fewer workers repair than the profile allows, the nearest free worker goes to the
/// building that needs it most (a tower first when the profile repairs under attack; otherwise only
/// buildings not under attack), one a second.
void RepairSecond(const World &world, Spend &spend)
{
	const Profile &profile = spend.profile;
	if (profile.repairWorkers <= 0 || world.gold < RepairGold || world.damaged.empty()) {
		return;
	}
	const int repairing = static_cast<int>(
		std::count_if(world.workers.begin(), world.workers.end(), [](const Worker &w) { return w.repairing; }));
	if (repairing >= profile.repairWorkers) {
		return;
	}
	const Damaged *best = nullptr;
	for (const Damaged &d : world.damaged) {
		if (d.attacked && !(profile.repairUnderAttack && d.tower)) {
			continue;
		}
		const bool better = best == nullptr || (profile.repairUnderAttack && d.tower && !best->tower)
		                    || ((d.tower == best->tower || !profile.repairUnderAttack) && d.hp < best->hp);
		if (better) {
			best = &d;
		}
	}
	if (best == nullptr) {
		return;
	}
	const Worker *worker = nullptr;
	for (const Worker &w : world.workers) {
		if (!w.repairing && (worker == nullptr || Distance(w.at, best->at) < Distance(worker->at, best->at))) {
			worker = &w;
		}
	}
	if (worker != nullptr && spend.Take()) {
		spend.decision.commands.push_back(Make(worker->slot, best->at, Order::Repair, best->slot));
	}
}

/// The next step of a research list (a line the race does not have is passed).
void ResearchNext(const World &world, Spend &spend, const std::vector<ResearchStep> &list, int &index)
{
	if (index >= static_cast<int>(list.size())) {
		return;
	}
	const ResearchStep &step = list[index];
	const int line = static_cast<int>(step.line);
	if (world.foreign[line] || world.level[line] >= step.level) {
		++index; // done already (the map gave it), or not of this race
	} else if (spend.now >= step.atSecond && world.level[line] + 1 == step.level && world.canResearch[line]
	           && spend.AffordsResearch(world, step.line) && spend.Take()) {
		spend.decision.research.emplace_back(step.line, step.level);
		++index;
	}
}

/// One more building of a kind, one at a time, up to `most`: asked and planned (a lost one is
/// asked again by the rebuild rule). Not while one is asked and not there yet.
bool AskBuilding(const World &world, Spend &spend, Kind kind, int most)
{
	const int k = KindIndex(kind);
	State &state = spend.state;
	if (spend.decision.release[k] || world.queued[k] > 0 || world.unplaced[k] || !world.can[k]
	    || world.have[k] + world.queued[k] >= most || world.have[k] < state.planned[k] || !spend.Affords(world, kind)
	    || !spend.Take()) {
		return false;
	}
	spend.decision.release[k] = 1;
	state.planned[k] = world.have[k] + 1;
	return true;
}

/// One more unit of a kind, one in training at a time, up to `most`.
bool AskUnit(const World &world, Spend &spend, Kind kind, int most)
{
	const int k = KindIndex(kind);
	if (spend.decision.release[k] || world.queued[k] > 0 || !world.can[k] || world.have[k] >= most
	    || !spend.Affords(world, kind) || !spend.Take()) {
		return false;
	}
	spend.decision.release[k] = 1;
	return true;
}

/// The warship to train next by the fleet mix: the kind furthest below its share of the fleet's
/// size (rounded up, held to its own most), over the kinds in the plan that can be trained now.
bool PickShip(const Profile &profile, const World &world, long now, Kind &pick)
{
	bool inPlan[FleetKinds];
	int planWeight = 0;
	for (int i = 0; i < FleetKinds; ++i) {
		inPlan[i] = profile.fleetWeight[i] > 0 && now >= profile.fleetFrom[i] && world.can[KindIndex(FleetKindList[i])];
		if (inPlan[i]) {
			planWeight += profile.fleetWeight[i];
		}
	}
	if (planWeight == 0) {
		return false;
	}
	int best = -1;
	for (int i = 0; i < FleetKinds; ++i) {
		const int k = KindIndex(FleetKindList[i]);
		if (!inPlan[i]) {
			continue;
		}
		int share = (profile.fleetMax * profile.fleetWeight[i] + planWeight - 1) / planWeight;
		if (profile.fleetMaxOf[i] >= 0) {
			share = std::min(share, profile.fleetMaxOf[i]);
		}
		if (world.have[k] + world.queued[k] >= share) {
			continue;
		}
		if (best < 0
		    || world.have[k] * profile.fleetWeight[best]
		           < world.have[KindIndex(FleetKindList[best])] * profile.fleetWeight[i]) {
			best = i;
		}
	}
	if (best < 0) {
		return false;
	}
	pick = FleetKindList[best];
	return true;
}

/// The sea, on a sea map: shipyards from the profile's second, tankers, oil platforms on a patch its
/// units have seen, the foundry, a refinery once two shipyards stand, the inventor for submarines,
/// warships by the fleet mix (one in training per shipyard), transports before the landings, and
/// the sea research in its own order.
void SeaEconomySecond(const World &world, Spend &spend)
{
	const Profile &profile = spend.profile;
	State &state = spend.state;
	const long now = spend.now;
	if (profile.shipyardSecond < 0 || !SeaMap(profile, world)) {
		return;
	}
	const int shipyard = KindIndex(Kind::Shipyard);
	if (now >= profile.shipyardSecond) {
		AskBuilding(world, spend, Kind::Shipyard, profile.shipyards);
	}
	if (world.have[shipyard] == 0) {
		return;
	}
	AskUnit(world, spend, Kind::Tanker, profile.tankers);
	if (world.oilKnown > 0 && world.have[KindIndex(Kind::Tanker)] > 0) {
		AskBuilding(world, spend, Kind::Platform, profile.platforms);
	}
	if (profile.foundrySecond >= 0 && now >= profile.foundrySecond) {
		AskBuilding(world, spend, Kind::Foundry, 1);
	}
	if (profile.refinery && world.have[shipyard] >= 2) {
		AskBuilding(world, spend, Kind::Refinery, 1);
	}
	if (profile.fleetWeight[2] > 0 && now >= profile.fleetFrom[2]) {
		AskBuilding(world, spend, Kind::Inventor, 1);
	}
	int fleet = 0;
	int queued = 0;
	for (const Kind kind : FleetKindList) {
		fleet += world.have[KindIndex(kind)];
		queued += world.queued[KindIndex(kind)];
	}
	Kind pick = Kind::Destroyer;
	if (queued < std::max(1, world.have[shipyard]) && fleet + queued < profile.fleetMax
	    && PickShip(profile, world, now, pick) && !spend.decision.release[KindIndex(pick)] && spend.Take()) {
		spend.decision.release[KindIndex(pick)] = 1;
	}
	if (profile.transports > 0 && profile.landingMax > 0 && now + TransportLeadSeconds >= profile.landingFromSecond
	    && world.have[KindIndex(Kind::Foundry)] > 0) {
		AskUnit(world, spend, Kind::Transport, profile.transports);
	}
	ResearchNext(world, spend, profile.seaResearch, state.seaResearch);
}

/// More of its halls than the enemy halls its units have seen (one at least): it out-mines them.
bool OutMining(const World &world, const State &state)
{
	const int halls =
		static_cast<int>(std::count_if(state.known.begin(), state.known.end(), [](const Known &k) { return k.cls == Class::Hall; }));
	return halls > 0 && world.have[KindIndex(Kind::Hall)] > halls;
}

/// The air, from the profile's second: the inventor and its flying scouts; an aviary and gryphons /
/// dragons only while it out-mines the enemy it has seen.
void AirEconomySecond(const World &world, Spend &spend)
{
	const Profile &profile = spend.profile;
	if (spend.now < profile.airFromSecond) {
		return;
	}
	if (profile.airScouts > 0) {
		AskBuilding(world, spend, Kind::Inventor, 1);
		if (world.have[KindIndex(Kind::Inventor)] > 0) {
			AskUnit(world, spend, Kind::Balloon, profile.airScouts);
		}
	}
	if (profile.riderGroup > 0 && OutMining(world, spend.state)) {
		AskBuilding(world, spend, Kind::Aviary, 1);
		if (world.have[KindIndex(Kind::Aviary)] > 0) {
			AskUnit(world, spend, Kind::Rider, profile.riderMax);
		}
	}
}

void EconomySecond(const World &world, Spend &spend)
{
	const Profile &profile = spend.profile;
	State &state = spend.state;
	Decision &decision = spend.decision;
	const long now = spend.now;
	const int hall = KindIndex(Kind::Hall);

	// No hall stands yet, but one is being built (a classic map gives the start a hall site with
	// its only worker inside; check 0.2.1: the farm asked from second 2 found no worker and no
	// place for ~100 s, a second hall was asked): nothing is asked until it stands - no farm (the
	// engine's neither), no building, no worker (no food, no trainer). The opening's hall step
	// counts the site. The second the hall stands the opening goes on.
	if (world.have[hall] == 0 && world.hallSites > 0) {
		if (state.opening < static_cast<int>(profile.opening.size())
		    && profile.opening[state.opening].what == Kind::Hall) {
			state.planned[hall] = std::max(state.planned[hall], 1);
			++state.opening;
		}
		decision.holdFarms = true;
		return;
	}

	// The opening, one step a second, in its order. A step counts: the second "farm" means two
	// farms; what the map already gave is not asked for again (and costs no order). A step may wait
	// for a time, a number of workers and food in use.
	// With few workers one building at a time: a step waits until the building asked before it
	// stands (or has found no place for a while), so the farm the first worker needs is not passed
	// by the barracks after it (round 3.4.65: barracks at ~110 s, the farm at ~195 s). A building other than a farm waits too while it would take the gold
	// kept back for the next worker and its farm.
	const int workersNow = world.have[KindIndex(Kind::Worker)];
	bool oneAtATime = false;
	if (workersNow < FewWorkers) {
		for (int k = 0; k < KindCount; ++k) {
			if (IsLandBuilding(static_cast<Kind>(k)) && world.queued[k] > 0 && world.have[k] < state.planned[k]
			    && !Stuck(state, k, now)) {
				oneAtATime = true;
			}
		}
	}
	// A building of the opening that is due and waits only for gold comes before the next worker
	// once the base has a few workers: the worker's reserve is not kept for it, and no worker is
	// asked while it waits (round 3.4.67: the Warlord's hall trained a worker the moment 400 gold
	// lay, so its due buildings never saw their price - nothing new for 3.1 min, wood piling up).
	bool openingFirst = false;
	if (state.opening < static_cast<int>(profile.opening.size())) {
		const OpeningStep &step = profile.opening[state.opening];
		const int k = KindIndex(step.what);
		bool affords = step.what == Kind::Farm || spend.Affords(world, step.what);
		const bool due = now >= step.atSecond && workersNow >= step.atWorkers && world.demand >= step.atFood;
		if (due && !affords && !spend.armyFirst && workersNow >= FewWorkers && IsLandBuilding(step.what) && world.can[k]
		    && !oneAtATime && world.have[k] + world.queued[k] < state.planned[k] + 1) {
			const bool wood = world.wood < 0 || world.woodCost[k] <= 0 || world.wood >= world.woodCost[k];
			affords = wood && (world.goldCost[k] <= 0 || world.gold >= world.goldCost[k]);
			// short of wood: holding the workers would not bring it; far from its gold: the workers
			// go on (a young base needs them to earn it)
			openingFirst = wood && !affords && 2 * world.gold >= world.goldCost[k];
		}
		if (due) {
			if (IsHallUpgrade(step.what)) {
				if (world.have[k] > 0) {
					++state.opening;
				} else if (world.can[k] && affords && spend.Take()) {
					(step.what == Kind::Keep ? decision.keep : decision.castle) = true;
					++state.opening;
				}
			} else if (world.have[k] + world.queued[k] >= state.planned[k] + 1) {
				++state.planned[k];
				++state.opening;
			} else if (world.can[k] && (!oneAtATime || (step.what == Kind::Farm && world.have[hall] > 0)) && affords
			           && spend.Take()) {
				decision.release[k] = 1;
				++state.planned[k];
				++state.opening;
			}
		}
	}

	// A building the profile wants before the extras has no place: no farm is asked (by the bot
	// here, or by the engine at a food block) until it stands, the room is kept for it.
	const int farm = KindIndex(Kind::Farm);
	// No hall stands yet (a classic start, the hall still to be placed): no farm either - it finds no
	// place round a hall not there, and the engine's own farm then waits in front of the barracks
	// (usC8: farm asked at ~10 s, the only peasant built barracks and blacksmith first, 1/1 food).
	decision.holdFarms = WantedFirstUnplaced(profile, world) || world.have[hall] == 0;
	const bool extras = WantedFirstStand(profile, world, state);

	// Buildings below what the opening asked for: a lost one after the rebuild delay, one that
	// never came up (its order was dropped) at once.
	for (int k = 0; k < KindCount; ++k) {
		if (!IsBuilding(static_cast<Kind>(k))) {
			continue;
		}
		state.peak[k] = std::max(state.peak[k], world.have[k]);
		if (decision.release[k] || (k == farm && decision.holdFarms)) {
			continue;
		}
		if (world.have[k] + world.queued[k] >= state.planned[k]) {
			state.lostAt[k] = -1;
			continue;
		}
		const bool lost = world.have[k] < state.peak[k];
		if (lost) {
			if (state.lostAt[k] < 0) {
				state.lostAt[k] = now;
			}
			if (now < state.lostAt[k] + profile.rebuildDelaySeconds) {
				continue;
			}
		}
		if (world.can[k] && (k == farm || spend.Affords(world, static_cast<Kind>(k))) && spend.Take()) {
			decision.release[k] = 1;
			state.lostAt[k] = -1;
		}
	}

	// No food for the next worker and no farm on its way: a farm first (the opening's next step
	// when that is a farm; otherwise one asked here).
	const int worker = KindIndex(Kind::Worker);
	const bool nextIsFarm =
		state.opening < static_cast<int>(profile.opening.size()) && profile.opening[state.opening].what == Kind::Farm;
	// The farm leads the food: it is asked while FoodLead food is still free, not when it is out (a
	// farm takes ~35 s and its walk; round 3.4.67: 5/5 food at ~3.6-4.4 min in every game). It is
	// not planned (the opening's farm steps count it as one of theirs).
	if (world.can[worker] && world.have[worker] < WorkerCap(profile, world) && FreeFood(world) <= FoodLead
	    && world.queued[farm] == 0 && !decision.release[farm] && !decision.holdFarms && !nextIsFarm && world.can[farm]
	    && spend.Take()) {
		decision.release[farm] = 1;
	}

	// Expansions: a further hall when the main mine runs low or at the profile's time. Low only
	// from a real read of the mine's gold (0 when it is gone); a mine not read (-1) is never low
	// (BOTS_FIX_3465: a missing read counted as empty could bring the Captain's hall at ~5 min).
	if (state.expansions < static_cast<int>(profile.expansions.size()) && !decision.release[hall]) {
		const ExpansionStep &step = profile.expansions[state.expansions];
		const int left = state.mineFirst > 0 && world.mineGold >= 0
		                     ? static_cast<int>(static_cast<long>(world.mineGold) * 100 / state.mineFirst)
		                     : -1;
		const bool due = (step.atSecond > 0 && now >= step.atSecond) || (step.mineBelow > 0 && left >= 0 && left < step.mineBelow);
		const int halls = world.have[hall] + world.queued[hall];
		if (due) {
			if (halls >= 2 + state.expansions) {
				++state.expansions; // there are halls enough already
				state.planned[hall] = std::max(state.planned[hall], halls);
			} else if (halls < profile.halls && world.can[hall] && spend.Affords(world, Kind::Hall) && spend.Take()) {
				decision.release[hall] = 1;
				state.planned[hall] = halls + 1;
				++state.expansions;
			}
		}
	}

	const int barracks = KindIndex(Kind::Barracks);

	// Farms ahead of need, when the profile asks for them (otherwise the engine's farm at a block),
	// once the buildings wanted before extras stand.
	// A farm ahead of need is not needed this second: it waits while it would take the gold kept
	// back for the next worker (round 3.4.66: the Warlord's slack farms took the gold of its 6th
	// worker for ~4 min; the farm for food that is out is asked above and never waits).
	// The food of farms on their way counts (two may be on the way at once), and the farms do not
	// wait for the extras: the room for those is kept by holdFarms (round 3.4.66: every level at
	// 5/5 food for ~1 min after the start, the Warlord at 5/5 and 9/9 for 2.6 / 3.1 min - its
	// slack farms waited for the lumber mill at ~8 min).
	if (profile.supplySlack > 0 && world.have[barracks] > 0 && !decision.holdFarms && !decision.release[farm]
	    && world.queued[farm] < FarmsOnTheWay
	    && world.can[farm]
	    && world.supply + FarmFood * world.queued[farm] - world.demand < profile.supplySlack
	    && (FreeFood(world) <= FoodLead || spend.Affords(world, Kind::Farm)) && spend.Take()) {
		decision.release[farm] = 1;
		++state.planned[farm];
	}

	TowersSecond(world, spend);
	RepairSecond(world, spend);

	// Workers, with the profile's gap, up to its number (a larger one once the hall is a keep); one
	// in training at a time, or one per hall when the profile trains at every hall.
	// Below FewWorkers no gap: the next one as soon as the last is out.
	const int workerCap = WorkerCap(profile, world);
	const int trainers = profile.workersPerHall ? std::max(1, world.have[hall]) : 1;
	// A farm asked for food that runs low and not begun for want of gold: the next fighter waits for
	// it (round 3.4.67 / toy: the farm waited 54 s for its 500 and the food ran out). Never the
	// worker (round usC8, fix 4: from the one-peasant start the farm waited while that peasant built
	// the barracks, and no worker came for ~4.5 min), and only with a second worker to build it.
	const bool farmFirst = world.waiting[farm] > 0 && FreeFood(world) <= FoodLead && world.goldCost[farm] > 0
	                       && world.gold < world.goldCost[worker] + world.goldCost[farm] && world.have[worker] >= 2;
	// The army short of the wave due: the next worker only with a fighter's gold beside its own, from
	// 12 workers on (fewer: the economy first; usC8 / fix 4: 12 workers by 7 min on a classic start).
	const bool armyWaits = spend.armyFirst && world.goldCost[worker] > 0 && world.have[worker] >= 3 * FewWorkers
	                       && world.gold < world.goldCost[worker] + world.goldCost[KindIndex(Kind::Soldier)];
	if (world.can[worker] && world.queued[worker] < trainers && !openingFirst && !armyWaits
	    && world.have[worker] + world.queued[worker] < workerCap
	    && (now >= state.nextWorkerAt || world.have[worker] < FewWorkers) && spend.Take()) {
		decision.release[worker] = 1;
		state.nextWorkerAt =
			now + static_cast<long>(profile.workerGapBuilds) * std::max(1, world.workerBuildSeconds) / trainers;
	}

	// Fighters, one at a time per barracks (and per mage tower for a profile with casters), up to
	// the army's size; the kinds of the profile's mix count.
	int fighters = 0;
	int queued = 0;
	for (int i = 0; i < FighterKinds; ++i) {
		if (profile.weight[i] > 0) {
			fighters += world.have[KindIndex(FighterKindList[i])];
			queued += world.queued[KindIndex(FighterKindList[i])];
		}
	}
	const int producers =
		world.have[barracks] + (profile.weight[4] > 0 ? world.have[KindIndex(Kind::MageTower)] : 0);
	Kind pick = Kind::Soldier;
	int fighterGold = 0;
	if (queued < std::max(1, producers) && fighters + queued < profile.armyMax && !farmFirst
	    && PickFighter(profile, state, world, now, pick) && spend.Affords(world, pick) && spend.Take()) {
		decision.release[KindIndex(pick)] = 1;
		++queued;
		fighterGold = world.goldCost[KindIndex(pick)];
	}

	// Gold lying unspent: a further barracks, up to the profile's number, one at a time and only
	// once the ones asked for stand, and the buildings wanted before extras too. Only while every
	// barracks is training already and the gold is still there after this second's fighter: the
	// fighters come first (round 3.4.66: the Warlord's spend_gold of 1000 was the very gold of its
	// next footman, so three spare barracks stood with one footman at 10 minutes).
	const bool producersBusy = producers > 0 && queued >= producers;
	if (profile.spendGold > 0 && world.gold - fighterGold >= profile.spendGold && producersBusy && extras
	    && !decision.release[barracks] && state.planned[barracks] >= 1 && state.planned[barracks] < profile.barracksMax
	    && world.queued[barracks] == 0 && world.have[barracks] >= state.planned[barracks] && world.can[barracks]
	    && spend.Affords(world, Kind::Barracks) && spend.Take()) {
		decision.release[barracks] = 1;
		++state.planned[barracks];
	}

	// Research, in the profile's order (a line the race does not have is passed).
	ResearchNext(world, spend, profile.research, state.research);

	SeaEconomySecond(world, spend);
	AirEconomySecond(world, spend);
}

} // namespace

Decision Decide(const Profile &profile, const World &world, State &state, Dice dice, void *context)
{
	Decision decision;
	decision.lumberWorkers = LumberWanted(profile, world);
	if (!profile.playable) {
		return decision;
	}
	Spend spend{profile, state, decision, world.second};
	Reserve(profile, world, spend.reserveGold, spend.reserveWood);

	UpdateKnowledge(world, state);
	// A building asked of the production that finds no place: since when (StuckSeconds).
	for (int k = 0; k < KindCount; ++k) {
		if (!world.unplaced[k] || !IsBuilding(static_cast<Kind>(k))) {
			state.unplacedSince[k] = -1;
		} else if (state.unplacedSince[k] < 0) {
			state.unplacedSince[k] = world.second;
		}
	}
	if (world.alarm && state.alarmDue < 0 && AtBase(world, world.alarmAt)) {
		state.alarmDue = world.second + std::max(profile.defenceDelaySeconds, profile.reactionSeconds);
		state.alarmAt = world.alarmAt;
		state.alarmTo = world.alarmTo.Valid() ? world.alarmTo : world.alarmAt;
	}
	if (state.nextWaveAt < 0) {
		state.nextWaveAt = profile.firstWaveSecond;
		// An enemy start near home: the profile's earlier first wave (pressure before it techs).
		if (profile.nearTiles > 0 && profile.nearFirstSecond > 0 && world.home.Valid()) {
			for (const Point &start : world.enemyStarts) {
				if (Distance(world.home, start) <= profile.nearTiles) {
					state.nextWaveAt = profile.nearFirstSecond;
				}
			}
		}
	}
	if (profile.raidSize > 0 && state.nextRaidAt < 0) {
		state.nextRaidAt = profile.raidFromSecond;
	}
	spend.armyFirst = ArmyShort(profile, world, state);
	// one fighter's gold for each barracks
	spend.fighterGold = world.goldCost[KindIndex(Kind::Soldier)] * std::max(1, world.have[KindIndex(Kind::Barracks)]);
	if (profile.scoutGapSeconds > 0 && state.nextScoutAt < 0) {
		state.nextScoutAt = std::max(profile.scoutWorkerSecond, 0) + profile.scoutGapSeconds;
	}
	if (profile.fleetMin > 0 && state.nextFleetAt < 0) {
		state.nextFleetAt = 0; // the fleet sets out once it has its number
	}
	if (profile.landingMax > 0 && state.nextLandingAt < 0) {
		state.nextLandingAt = profile.landingFromSecond;
	}
	if (profile.airScouts > 0 && profile.airScoutGapSeconds > 0 && state.nextAirScoutAt < 0) {
		state.nextAirScoutAt = profile.airFromSecond;
	}

	if (ArmySecond(world, spend, dice, context)) {
		decision.army = true;
		state.armySecond = world.second;
	}
	if (!(profile.singleAttention && decision.army)) {
		EconomySecond(world, spend);
	} else {
		decision.holdFarms = WantedFirstUnplaced(profile, world) || world.have[KindIndex(Kind::Hall)] == 0;
	}
	return decision;
}

// ---------------------------------------------------------------------------- saved state

namespace
{

std::string Join(const std::vector<long> &values)
{
	if (values.empty()) {
		return "-";
	}
	std::string text;
	for (size_t i = 0; i < values.size(); ++i) {
		text += (i ? "," : "") + std::to_string(values[i]);
	}
	return text;
}

template <typename T, size_t N>
std::vector<long> Of(const T (&values)[N])
{
	return std::vector<long>(std::begin(values), std::end(values));
}

bool Numbers(const std::string &text, std::vector<long> &out)
{
	out.clear();
	if (text == "-") {
		return true;
	}
	size_t from = 0;
	while (from <= text.size()) {
		size_t to = text.find(',', from);
		if (to == std::string::npos) {
			to = text.size();
		}
		const std::string part = text.substr(from, to - from);
		if (part.empty()) {
			return false;
		}
		char *end = nullptr;
		errno = 0;
		const long value = std::strtol(part.c_str(), &end, 10);
		if (errno != 0 || end == nullptr || *end != '\0') {
			return false;
		}
		out.push_back(value);
		from = to + 1;
	}
	return true;
}

/// A list per kind: of this engine's kinds, of the seventeen of mod 0.2's land play or of the eleven
/// of mod 0.1.x (the later kinds keep their defaults).
template <typename T>
bool IntoKinds(const std::vector<long> &values, T (&out)[KindCount])
{
	if (values.size() != static_cast<size_t>(KindCount) && values.size() != static_cast<size_t>(LandKindCount)
	    && values.size() != static_cast<size_t>(OldKindCount)) {
		return false;
	}
	for (size_t i = 0; i < values.size(); ++i) {
		out[i] = static_cast<T>(values[i]);
	}
	return true;
}

/// The most enemy units seen per class: of this engine's classes or of the fourteen of mod 0.2's land
/// play (the later classes keep 0).
bool IntoClasses(const std::vector<long> &values, int (&out)[ClassCount])
{
	if (values.size() != static_cast<size_t>(ClassCount) && values.size() != static_cast<size_t>(LandClassCount)) {
		return false;
	}
	for (size_t i = 0; i < values.size(); ++i) {
		out[i] = static_cast<int>(values[i]);
	}
	return true;
}

bool IntoPoint(const std::vector<long> &values, Point &out)
{
	if (values.size() != 2) {
		return false;
	}
	out.x = static_cast<int>(values[0]);
	out.y = static_cast<int>(values[1]);
	return true;
}

} // namespace

std::string StateText(const State &state)
{
	std::vector<long> wave(state.wave.begin(), state.wave.end());
	std::vector<long> known;
	std::vector<long> knownClass;
	for (const Known &k : state.known) {
		known.push_back(k.slot);
		known.push_back(k.at.x);
		known.push_back(k.at.y);
		knownClass.push_back(static_cast<long>(k.cls));
	}
	std::vector<long> rate{state.rate.second};
	rate.insert(rate.end(), state.rate.used.begin(), state.rate.used.end());
	std::vector<long> casts;
	for (const auto &[slot, second] : state.castBusy) {
		casts.push_back(slot);
		casts.push_back(second);
	}

	std::string text = "v1";
	text += " op=" + std::to_string(state.opening);
	text += " pl=" + Join(Of(state.planned));
	text += " pk=" + Join(Of(state.peak));
	text += " la=" + Join(Of(state.lostAt));
	text += " rs=" + std::to_string(state.research);
	text += " nw=" + std::to_string(state.nextWorkerAt);
	text += " nx=" + std::to_string(state.nextWaveAt);
	text += " wv=" + std::to_string(state.waves);
	text += " ws=" + std::to_string(state.waveSize);
	text += " w=" + Join(wave);
	text += " tg=" + Join({state.target.x, state.target.y});
	text += " vs=" + std::to_string(state.visitedStarts);
	text += " ad=" + std::to_string(state.alarmDue);
	text += " aa=" + Join({state.alarmAt.x, state.alarmAt.y});
	text += " k=" + Join(known);
	text += " as=" + std::to_string(state.armySecond);
	text += " rc=" + Join(rate);
	text += " sc=" + std::to_string(state.scouted);
	text += " rp=" + Join({state.rally.x, state.rally.y});
	text += " gu=" + std::to_string(state.gatherUntil);
	text += " ao=" + Join({state.alarmTo.x, state.alarmTo.y});
	text += " ia=" + Join({state.idleAt.x, state.idleAt.y});
	text += " ts=" + std::to_string(state.targetSlot);
	text += " ig=" + Join(std::vector<long>(state.ignored.begin(), state.ignored.end()));
	text += " us=" + Join(Of(state.unplacedSince));
	text += " cl=" + std::to_string(state.clearedAt);
	// mod 0.2
	text += " ls=" + std::to_string(state.launchSize);
	text += " fs=" + std::to_string(state.focusSlot);
	text += " rt=" + std::to_string(state.retreats);
	text += " rd=" + Join(std::vector<long>(state.raid.begin(), state.raid.end()));
	text += " rx=" + Join({state.raidTarget.x, state.raidTarget.y});
	text += " ry=" + std::to_string(state.raidSlot);
	text += " rn=" + std::to_string(state.nextRaidAt);
	text += " so=" + std::to_string(state.scouts);
	text += " sn=" + std::to_string(state.nextScoutAt);
	text += " si=" + std::to_string(state.scoutIndex);
	text += " ex=" + std::to_string(state.expansions);
	text += " es=" + std::to_string(state.escorted);
	text += " mf=" + std::to_string(state.mineFirst);
	text += " sp=" + Join(Of(state.seenPeak));
	text += " ct=" + std::to_string(state.counterTowers);
	text += " cb=" + Join(casts);
	text += " kc=" + Join(knownClass);
	// mod 0.2, part B: sea and air
	text += " fl=" + Join(std::vector<long>(state.fleet.begin(), state.fleet.end()));
	text += " ft=" + Join({state.fleetTarget.x, state.fleetTarget.y});
	text += " fo=" + std::to_string(state.fleetSlot);
	text += " fn=" + std::to_string(state.fleetLaunch);
	text += " fx=" + std::to_string(state.nextFleetAt);
	text += " fv=" + std::to_string(state.fleetVisited);
	text += " fi=" + Join(std::vector<long>(state.fleetIgnored.begin(), state.fleetIgnored.end()));
	text += " fa=" + Join({state.fleetIdleAt.x, state.fleetIdleAt.y});
	text += " eh=" + std::to_string(state.enemyShipAt);
	text += " lp=" + std::to_string(state.landingPhase);
	text += " lc=" + std::to_string(state.landingSince);
	text += " ln=" + std::to_string(state.nextLandingAt);
	text += " lz=" + std::to_string(state.landingSize);
	text += " lg=" + std::to_string(state.landings);
	text += " lu=" + Join(std::vector<long>(state.landing.begin(), state.landing.end()));
	text += " lf=" + Join(std::vector<long>(state.ferry.begin(), state.ferry.end()));
	text += " lo=" + Join({state.landingShore.x, state.landingShore.y});
	text += " lw=" + Join({state.landingWater.x, state.landingWater.y});
	text += " sr=" + std::to_string(state.seaResearch);
	text += " an=" + std::to_string(state.nextAirScoutAt);
	text += " ai=" + std::to_string(state.airScoutIndex);
	text += " rr=" + Join(std::vector<long>(state.riders.begin(), state.riders.end()));
	text += " rg=" + Join({state.riderTarget.x, state.riderTarget.y});
	text += " ro=" + std::to_string(state.riderSlot);
	text += " rl=" + std::to_string(state.riderLaunch);
	return text;
}

bool StateFromText(const std::string &text, State &out)
{
	State state;
	std::vector<std::string> tokens;
	size_t from = 0;
	while (from < text.size()) {
		size_t to = text.find(' ', from);
		if (to == std::string::npos) {
			to = text.size();
		}
		if (to > from) {
			tokens.push_back(text.substr(from, to - from));
		}
		from = to + 1;
	}
	if (tokens.empty() || tokens[0] != "v1") {
		return false;
	}
	const char *const keys[] = {"op", "pl", "pk", "la", "rs", "nw", "nx", "wv", "ws", "w",  "tg",
	                            "vs", "ad", "aa", "k",  "as", "rc", "sc", "rp", "gu", "ao", "ia",
	                            "ts", "ig", "us", "cl", "ls", "fs", "rt", "rd", "rx", "ry", "rn",
	                            "so", "sn", "si", "ex", "es", "mf", "sp", "ct", "cb", "kc", "fl",
	                            "ft", "fo", "fn", "fx", "fv", "fi", "fa", "eh", "lp", "lc", "ln",
	                            "lz", "lg", "lu", "lf", "lo", "lw", "sr", "an", "ai", "rr", "rg",
	                            "ro", "rl"};
	// the first 17 are required; the fields that came later (gathering, scouting, unreachable goals,
	// buildings with no place, the Captain's and Warlord's, sea and air) may be absent: a game saved
	// before reads them as their defaults
	constexpr size_t RequiredKeys = 17;
	std::vector<char> seen(std::size(keys), 0);
	std::vector<long> knownClass;
	bool hasKnownClass = false;
	for (size_t t = 1; t < tokens.size(); ++t) {
		const size_t eq = tokens[t].find('=');
		if (eq == std::string::npos) {
			return false;
		}
		const std::string key = tokens[t].substr(0, eq);
		std::vector<long> values;
		if (!Numbers(tokens[t].substr(eq + 1), values)) {
			return false;
		}
		const auto one = [&values](long &value) {
			if (values.size() != 1) {
				return false;
			}
			value = values[0];
			return true;
		};
		long value = 0;
		bool ok = false;
		size_t index = 0;
		for (; index < std::size(keys) && key != keys[index]; ++index) {
		}
		if (index == std::size(keys) || seen[index]) {
			return false; // unknown or repeated
		}
		seen[index] = 1;
		switch (index) {
			case 0: ok = one(value); state.opening = static_cast<int>(value); break;
			case 1: ok = IntoKinds(values, state.planned); break;
			case 2: ok = IntoKinds(values, state.peak); break;
			case 3: ok = IntoKinds(values, state.lostAt); break;
			case 4: ok = one(value); state.research = static_cast<int>(value); break;
			case 5: ok = one(state.nextWorkerAt); break;
			case 6: ok = one(state.nextWaveAt); break;
			case 7: ok = one(value); state.waves = static_cast<int>(value); break;
			case 8: ok = one(value); state.waveSize = static_cast<int>(value); break;
			case 9:
				ok = true;
				state.wave.assign(values.begin(), values.end());
				break;
			case 10: ok = IntoPoint(values, state.target); break;
			case 11: ok = one(value) && value >= 0; state.visitedStarts = static_cast<uint32_t>(value); break;
			case 12: ok = one(state.alarmDue); break;
			case 13: ok = IntoPoint(values, state.alarmAt); break;
			case 14:
				ok = values.size() % 3 == 0;
				for (size_t i = 0; ok && i < values.size(); i += 3) {
					Known known;
					known.slot = static_cast<int>(values[i]);
					known.at.x = static_cast<int>(values[i + 1]);
					known.at.y = static_cast<int>(values[i + 2]);
					state.known.push_back(known);
				}
				break;
			case 15: ok = one(state.armySecond); break;
			case 16:
				ok = values.size() == 1 + state.rate.used.size();
				if (ok) {
					state.rate.second = values[0];
					for (size_t i = 0; i < state.rate.used.size(); ++i) {
						ok = ok && values[i + 1] >= 0 && values[i + 1] <= 0xFFFF;
						state.rate.used[i] = static_cast<uint16_t>(values[i + 1]);
					}
				}
				break;
			case 17: ok = one(value) && value >= 0; state.scouted = static_cast<uint32_t>(value); break;
			case 18: ok = IntoPoint(values, state.rally); break;
			case 19: ok = one(state.gatherUntil); break;
			case 20: ok = IntoPoint(values, state.alarmTo); break;
			case 21: ok = IntoPoint(values, state.idleAt); break;
			case 22: ok = one(value); state.targetSlot = static_cast<int>(value); break;
			case 23:
				ok = values.size() <= IgnoredMax;
				state.ignored.assign(values.begin(), values.end());
				break;
			case 24: ok = IntoKinds(values, state.unplacedSince); break;
			case 25: ok = one(state.clearedAt); break;
			case 26: ok = one(value); state.launchSize = static_cast<int>(value); break;
			case 27: ok = one(value); state.focusSlot = static_cast<int>(value); break;
			case 28: ok = one(value); state.retreats = static_cast<int>(value); break;
			case 29:
				ok = true;
				state.raid.assign(values.begin(), values.end());
				break;
			case 30: ok = IntoPoint(values, state.raidTarget); break;
			case 31: ok = one(value); state.raidSlot = static_cast<int>(value); break;
			case 32: ok = one(state.nextRaidAt); break;
			case 33: ok = one(value); state.scouts = static_cast<int>(value); break;
			case 34: ok = one(state.nextScoutAt); break;
			case 35: ok = one(value) && value >= 0; state.scoutIndex = static_cast<int>(value); break;
			case 36: ok = one(value) && value >= 0; state.expansions = static_cast<int>(value); break;
			case 37: ok = one(value) && value >= 0; state.escorted = static_cast<int>(value); break;
			case 38: ok = one(state.mineFirst); break;
			case 39: ok = IntoClasses(values, state.seenPeak); break;
			case 40: ok = one(value) && value >= 0; state.counterTowers = static_cast<uint32_t>(value); break;
			case 41:
				ok = values.size() % 2 == 0;
				for (size_t i = 0; ok && i < values.size(); i += 2) {
					state.castBusy.emplace_back(static_cast<int>(values[i]), values[i + 1]);
				}
				break;
			case 42:
				ok = true;
				knownClass = values;
				hasKnownClass = true;
				break;
			case 43: ok = true; state.fleet.assign(values.begin(), values.end()); break;
			case 44: ok = IntoPoint(values, state.fleetTarget); break;
			case 45: ok = one(value); state.fleetSlot = static_cast<int>(value); break;
			case 46: ok = one(value) && value >= 0; state.fleetLaunch = static_cast<int>(value); break;
			case 47: ok = one(state.nextFleetAt); break;
			case 48: ok = one(value) && value >= 0; state.fleetVisited = static_cast<uint32_t>(value); break;
			case 49:
				ok = values.size() <= FleetIgnoredMax;
				state.fleetIgnored.assign(values.begin(), values.end());
				break;
			case 50: ok = IntoPoint(values, state.fleetIdleAt); break;
			case 51: ok = one(state.enemyShipAt); break;
			case 52: ok = one(value) && value >= 0 && value <= 3; state.landingPhase = static_cast<int>(value); break;
			case 53: ok = one(state.landingSince); break;
			case 54: ok = one(state.nextLandingAt); break;
			case 55: ok = one(value) && value >= 0; state.landingSize = static_cast<int>(value); break;
			case 56: ok = one(value) && value >= 0; state.landings = static_cast<int>(value); break;
			case 57: ok = true; state.landing.assign(values.begin(), values.end()); break;
			case 58: ok = true; state.ferry.assign(values.begin(), values.end()); break;
			case 59: ok = IntoPoint(values, state.landingShore); break;
			case 60: ok = IntoPoint(values, state.landingWater); break;
			case 61: ok = one(value) && value >= 0; state.seaResearch = static_cast<int>(value); break;
			case 62: ok = one(state.nextAirScoutAt); break;
			case 63: ok = one(value) && value >= 0; state.airScoutIndex = static_cast<int>(value); break;
			case 64: ok = true; state.riders.assign(values.begin(), values.end()); break;
			case 65: ok = IntoPoint(values, state.riderTarget); break;
			case 66: ok = one(value); state.riderSlot = static_cast<int>(value); break;
			case 67: ok = one(value) && value >= 0; state.riderLaunch = static_cast<int>(value); break;
			default: break;
		}
		if (!ok) {
			return false;
		}
	}
	if (std::find(seen.begin(), seen.begin() + RequiredKeys, 0) != seen.begin() + RequiredKeys) {
		return false; // a field is missing
	}
	if (hasKnownClass) {
		if (knownClass.size() != state.known.size()) {
			return false;
		}
		for (size_t i = 0; i < knownClass.size(); ++i) {
			if (knownClass[i] < 0 || knownClass[i] >= ClassCount) {
				return false;
			}
			state.known[i].cls = static_cast<Class>(knownClass[i]);
		}
	}
	out = state;
	return true;
}

std::string LogLine(const World &world, const State &state)
{
	std::string text = "s" + std::to_string(world.second) + " g" + std::to_string(world.gold) + " w"
	                   + std::to_string(world.wood) + " food" + std::to_string(world.demand) + "/"
	                   + std::to_string(world.supply) + " |";
	for (int k = 0; k < KindCount; ++k) {
		if (world.have[k] > 0) {
			text += std::string(" ") + KindName(static_cast<Kind>(k)) + "=" + std::to_string(world.have[k]);
		}
	}
	text += " fighters=" + std::to_string(world.fighters.size()) + " | asked";
	for (int k = 0; k < KindCount; ++k) {
		if (world.queued[k] > 0) {
			text += std::string(" ") + KindName(static_cast<Kind>(k)) + "=" + std::to_string(world.queued[k]);
		}
	}
	const char *phase = state.wave.empty() ? "home" : state.gatherUntil >= world.second ? "gather" : "march";
	text += " | wave " + std::string(phase) + " out=" + std::to_string(state.wave.size()) + "/"
	        + std::to_string(state.launchSize) + " size=" + std::to_string(state.waveSize) + " next="
	        + std::to_string(state.nextWaveAt) + " waves=" + std::to_string(state.waves) + " to="
	        + std::to_string(state.target.x) + "," + std::to_string(state.target.y);
	return text;
}

} // namespace wc2bot
