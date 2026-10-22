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
/**@name wc2bot.h - Computer players described by a mod's data (defs/bots.json). */
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

#ifndef WC2BOT_H
#define WC2BOT_H

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace content
{
class JsonValue;
}

/// A computer seat runs a bot when its AI name is "mod-bot:<mod id>:<profile id>" and the mod is
/// enabled for this start. The mod holds data only: the profiles in defs/bots.json. Nothing of
/// this module runs for a seat with any other AI name; with no bot seat the game is unchanged.
///
/// The decisions are made once a game second from a view of the player's own state, of what its
/// own units see and of the map's start positions (wc2bot::World), and are integers only: the
/// same view, state and synchronised dice give the same decision on every machine.
namespace wc2bot
{

/// The prefix of a bot seat's AI name.
constexpr const char *SeatPrefix = "mod-bot:";
/// The one format of defs/bots.json this engine reads.
constexpr int Format = 1;
/// Fairness ceilings no profile may pass: orders per game minute and the shortest reaction.
constexpr int MaxOrdersPerMinute = 180;
constexpr int MinReactionSeconds = 1;
/// The vision every profile must declare: its own units' sight plus the map's start positions.
constexpr const char *FairVision = "own-sight+start-positions";

/// What a profile names, resolved per race by the engine (no race-specific idents in the data; the
/// names are the human side's, the engine takes the orc type of an orc player).
/// The first eleven are those of mod 0.1.x, the next six of mod 0.2's land play; the order is kept so
/// that their saves still read.
enum class Kind : uint8_t {
	Worker,
	Soldier,    ///< footman / grunt
	Shooter,    ///< archer / axe thrower
	Cavalry,    ///< knight / ogre (paladin / ogre-mage counted in)
	Farm,
	Barracks,
	LumberMill,
	Blacksmith,
	Stables,    ///< stables / ogre mound
	Hall,       ///< town hall / great hall (the class: keeps and castles count in)
	Keep,       ///< the hall's first upgrade
	Castle,     ///< the hall's second upgrade (castle / fortress)
	Tower,      ///< watch tower (guard / cannon towers counted in)
	MageTower,  ///< mage tower / temple of the damned
	Church,     ///< church / altar of storms
	Siege,      ///< ballista / catapult
	Caster,     ///< mage / death knight
	// sea and air (mod 0.2, part B)
	Shipyard,
	Refinery,
	Foundry,
	Platform,   ///< oil platform (a tanker builds it on an oil patch)
	Inventor,   ///< gnomish inventor / goblin alchemist
	Aviary,     ///< gryphon aviary / dragon roost
	Tanker,     ///< oil tanker
	Destroyer,
	Battleship, ///< battleship / ogre juggernaught
	Submarine,  ///< gnomish submarine / giant turtle
	Transport,
	Balloon,    ///< gnomish flying machine / goblin zeppelin
	Rider,      ///< gryphon rider / dragon
	Count
};
constexpr int KindCount = static_cast<int>(Kind::Count);
/// Kinds of the saves of mod 0.1.x (worker .. keep).
constexpr int OldKindCount = 11;
/// Kinds of the saves of mod 0.2's land play (worker .. caster).
constexpr int LandKindCount = 17;
constexpr int KindIndex(Kind kind) { return static_cast<int>(kind); }
constexpr bool IsFighter(Kind kind)
{
	return kind == Kind::Soldier || kind == Kind::Shooter || kind == Kind::Cavalry || kind == Kind::Siege
	       || kind == Kind::Caster;
}
/// A building on land of the land play (farm .. church): what a base cleared for a stuck building means.
constexpr bool IsLandBuilding(Kind kind)
{
	return (kind >= Kind::Farm && kind <= Kind::Hall) || kind == Kind::Tower || kind == Kind::MageTower
	       || kind == Kind::Church;
}
constexpr bool IsBuilding(Kind kind)
{
	return IsLandBuilding(kind) || (kind >= Kind::Shipyard && kind <= Kind::Aviary);
}
/// A warship of the fleet (not a tanker, not a transport).
constexpr bool IsWarship(Kind kind)
{
	return kind == Kind::Destroyer || kind == Kind::Battleship || kind == Kind::Submarine;
}
/// The warship kinds in the order of a profile's fleet mix.
constexpr int FleetKinds = 3;
constexpr Kind FleetKindList[FleetKinds] = {Kind::Destroyer, Kind::Battleship, Kind::Submarine};
/// Units a transport carries at most (the game's own number is read by the engine side).
constexpr int TransportSlots = 6;
/// The hall's upgrades (asked of the hall, not built).
constexpr bool IsHallUpgrade(Kind kind) { return kind == Kind::Keep || kind == Kind::Castle; }
/// The fighter kinds in the order of a profile's army mix.
constexpr int FighterKinds = 5;
constexpr Kind FighterKindList[FighterKinds] = {Kind::Soldier, Kind::Shooter, Kind::Cavalry, Kind::Siege, Kind::Caster};
/// The data name of a kind ("worker", "soldier", ..., "lumber-mill", "keep", "mage-tower"), and back.
const char *KindName(Kind kind);
bool KindFromName(const std::string &name, Kind &kind);

/// Research lines. The first three have levels 1 and 2, the others one level. A line may exist for
/// one race only (healing is the human's, bloodlust the orc's): for the other race it is skipped.
enum class Line : uint8_t {
	Weapons,
	Armour,
	Missile,
	Elite,          ///< knights to paladins / ogres to ogre-magi
	Healing,        ///< human
	Bloodlust,      ///< orc
	Slow,           ///< human
	Haste,          ///< orc
	Blizzard,       ///< human
	DeathAndDecay,  ///< orc
	Polymorph,      ///< human
	DeathCoil,      ///< orc
	UnholyArmour,   ///< orc
	Exorcism,       ///< human
	Invisibility,   ///< human
	ShipWeapons,    ///< ship cannons
	ShipArmour,     ///< ship plating
	Count
};
constexpr int LineCount = static_cast<int>(Line::Count);
constexpr int MaxLevel = 2;
/// Levels of a line (2 for weapons / armour / missile and the two ship lines, 1 for the others).
int LineLevels(Line line);
const char *LineName(Line line);
bool LineFromName(const std::string &name, Line &line);

/// Spells a bot may cast (each is researched on the line of the same name).
enum class Spell : uint8_t {
	Bloodlust,
	Healing,
	Slow,
	Haste,
	Blizzard,
	DeathAndDecay,
	Polymorph,
	DeathCoil,
	UnholyArmour,   ///< on an own fighter in a fight (it costs the fighter half its hit points)
	Exorcism,       ///< on an enemy undead
	Invisibility,   ///< on an own loaded transport of a landing
	Count
};
constexpr int SpellCount = static_cast<int>(Spell::Count);
constexpr uint32_t SpellBit(Spell spell) { return 1u << static_cast<int>(spell); }
const char *SpellName(Spell spell);
bool SpellFromName(const std::string &name, Spell &spell);

/// What an enemy unit or building seen is, for the target order, the counters and the casting.
enum class Class : uint8_t {
	Other,
	Worker,
	Soldier,
	Shooter,
	Cavalry,
	Siege,
	Caster,
	Sapper,
	Flyer,
	Ship,
	Hall,
	Barracks,
	Tower,
	Building,   ///< any other building
	// mod 0.2, part B (Ship above is a warship)
	Tanker,
	Transport,
	Platform,   ///< oil platform
	Shipyard,
	Foundry,
	Refinery,
	Count
};
constexpr int ClassCount = static_cast<int>(Class::Count);
/// Classes of the saves of mod 0.2's land play (other .. building).
constexpr int LandClassCount = 14;
/// A unit on the water.
constexpr bool IsSeaUnit(Class cls) { return cls == Class::Ship || cls == Class::Tanker || cls == Class::Transport; }
/// A building the fleet goes for (on the water or the shore).
constexpr bool IsSeaBuilding(Class cls)
{
	return cls == Class::Platform || cls == Class::Shipyard || cls == Class::Foundry || cls == Class::Refinery;
}
/// A unit class that fights on land (what "army" means in a target order).
constexpr bool IsArmy(Class cls)
{
	return cls == Class::Soldier || cls == Class::Shooter || cls == Class::Cavalry || cls == Class::Siege
	       || cls == Class::Caster || cls == Class::Sapper;
}

/// The classes a profile's target order may name.
enum class Aim : uint8_t {
	Worker,
	Soldier,
	Shooter,
	Cavalry,
	Siege,
	Caster,
	Sapper,
	Army,       ///< any land fighter
	Hall,
	Expansion,  ///< a hall away from every enemy start position
	Barracks,
	Tower,
	Building,   ///< any building
	// mod 0.2, part B
	Ship,       ///< any unit on the water
	Transport,
	Tanker,
	Platform,
	Shipyard,
	Foundry,
	Refinery,
	Flyer,
	Count
};
bool AimFromName(const std::string &name, Aim &aim);

struct OpeningStep {
	Kind what = Kind::Farm;
	int atSecond = 0;   ///< not before this game second
	int atWorkers = 0;  ///< not before this many workers
	int atFood = 0;     ///< not before this much food is used
};

struct ResearchStep {
	Line line = Line::Weapons;
	int level = 1;
	int atSecond = 0;
};

/// A further hall: when the main mine is low or at a time, whichever comes first.
struct ExpansionStep {
	int atSecond = 0;      ///< 0: no time
	int mineBelow = 0;     ///< per cent of the main mine's first gold; 0: not looked at
	int escort = 0;        ///< home units sent to the new hall
};

/// One computer player of defs/bots.json. Times are game seconds at the normal game speed.
struct Profile {
	std::string id;          ///< lower-case letters, digits, '-'
	std::string nameKey;     ///< the launcher's string key of its name
	bool playable = false;   ///< false: a placeholder, listed but not offered

	// fairness
	int ordersPerMinute = 30;
	int reactionSeconds = 1;
	bool singleAttention = true;   ///< economy or army in one second, never both

	// economy
	int workers = 12;              ///< workers trained up to this many, then none
	int workersAfterKeep = 12;     ///< ... and up to this many once the hall is a keep (default: workers)
	bool workersPerHall = false;   ///< every hall trains workers at once (else one at a time)
	int lumberWorkers = 2;         ///< idle workers go to lumber while fewer cut it
	int lumberShare = 0;           ///< ... or while fewer than this per cent of the workers (the larger)
	int workerGapBuilds = 2;       ///< a new worker only every this many worker build times
	int supplySlack = 0;           ///< a farm is asked while free food is below this (0: the engine's own late farm)
	int halls = 1;                 ///< most halls at once
	bool expand = false;           ///< may build further halls (the expansions below)
	std::vector<ExpansionStep> expansions;
	int barracksMax = 1;           ///< barracks it may stand at once (the opening's one, more for spending)
	int spendGold = 0;             ///< a further barracks is asked while this much gold lies unspent (0: never)
	int repairWorkers = 0;         ///< most workers repairing at once (0: no repair)
	bool repairUnderAttack = false; ///< repairs a tower under attack (else only buildings not attacked)
	/// Buildings of the opening that come before anything extra (a further barracks, farms ahead of
	/// need): extras wait until the opening's ones of these kinds stand, and while one of them is asked
	/// and finds no place no farm is asked (it would take the room). Empty: no such rule.
	std::vector<Kind> beforeExtras;
	std::vector<OpeningStep> opening;

	// army: soldier, shooter, cavalry, siege, caster (FighterKindList)
	int armyMax = 10;
	int weight[FighterKinds] = {2, 1, 0, 0, 0};
	int fromSecond[FighterKinds] = {0, 0, 0, 0, 0};  ///< the kind is in the plan from this second
	int maxOf[FighterKinds] = {-1, -1, 0, 0, 0};      ///< most of the kind (-1: its share only)
	/// A kind that cannot be trained yet keeps its places in the army (true), or the army fills with
	/// the kinds that can (false).
	bool holdPlaces = true;
	bool siegeVsTowers = false;    ///< siege only while an enemy tower is known
	bool counters = false;         ///< the mix and the towers answer what its units have seen

	// waves
	int firstWaveSecond = 600;
	int nearTiles = 0;             ///< an enemy start this close to home (tiles) ...
	int nearFirstSecond = 0;       ///< ... brings the first wave to this second (0: no such rule)
	int firstWaveMin = 3, firstWaveMax = 4;
	int waveGapSeconds = 240;
	int waveMin = 4, waveMax = 6;
	int rallyTiles = 0;            ///< a wave gathers this far from home toward its target first (0: no gathering)
	int gatherSeconds = 0;         ///< longest wait at the rally point before the wave goes anyway
	std::vector<Aim> targets;      ///< target order (empty: the nearest known building, then units)
	bool focusFire = false;        ///< the wave attacks one enemy unit at a time, by the target order
	int pullHp = 0;                ///< a wave unit below this per cent of its hit points goes home (0: never)
	int retreatLost = 0;           ///< a wave that lost this per cent and is outnumbered comes home (0: never)
	int returnSeconds = 0;         ///< after a retreat the next wave is due this soon (0: the gap)
	bool avoidTowers = false;      ///< a wave without siege does not attack into known towers

	// raid group (fast units on the enemy's workers)
	int raidSize = 0;              ///< 0: no raids
	int raidFromSecond = 0;
	int raidGapSeconds = 0;

	// scouting
	int scoutWorkerSecond = 0;     ///< one worker to the nearest enemy start at this second (0: none)
	int scoutGapSeconds = 0;       ///< one fighter scouts every this many seconds (0: none)

	// spells
	std::vector<Spell> casts;      ///< the spells it casts, in this order of preference
	bool castBest = false;         ///< the best target (cluster, target order); else the nearest
	int fightMin = 3;              ///< own units in a fight before a buff is cast

	// towers
	bool cannonTowers = false;     ///< towers are made cannon towers (else guard towers, cannon by counter)

	// defence
	int defenceDelaySeconds = 20;
	int defenceRadius = 10;
	int defenceWorkers = 0;        ///< workers that join the defence when the attackers are few (0: none)
	int defenceWorkersVs = 0;      ///< ... at most this many enemy fighters at the attacked place

	// sea: only on a sea map (SeaMap)
	int seaWater = 0;              ///< a map with this per cent of water is a sea map (0: only when an enemy
	                               ///< start lies on other land than its own)
	int shipyardSecond = -1;       ///< the first shipyard not before this second (-1: no sea play)
	int shipyards = 0;             ///< most shipyards, one at a time
	int tankers = 0;               ///< oil tankers kept
	int platforms = 0;             ///< oil platforms, one at a time (on an oil patch its units have seen)
	bool refinery = false;         ///< a refinery once two shipyards stand
	int foundrySecond = -1;        ///< a foundry not before this second (-1: never)
	int fleetMax = 0;              ///< warships at most
	int fleetWeight[FleetKinds] = {0, 0, 0};       ///< destroyer, battleship, submarine
	int fleetFrom[FleetKinds] = {0, 0, 0};
	int fleetMaxOf[FleetKinds] = {-1, -1, -1};
	int fleetMin = 0;              ///< warships at home before the fleet sets out (0: it stays home, defends)
	int fleetGapSeconds = 0;       ///< the fleet sets out again after this
	std::vector<Aim> fleetTargets; ///< the fleet's target order (empty: the nearest)
	std::vector<ResearchStep> seaResearch;  ///< research on a sea map, in its own order
	int transports = 0;            ///< transports kept
	int landingMin = 0, landingMax = 0;     ///< units of a landing (0: no landings)
	int landingFromSecond = 0;
	int landingGapSeconds = 0;
	bool landingClear = false;     ///< a landing only when no enemy warship was seen for a while
	int landingCover = 0;          ///< own warships at home before a landing; they sail with it

	// air
	int airFromSecond = 0;         ///< the inventor and the flyers not before this second
	int airScouts = 0;             ///< flying machines / zeppelins kept as scouts (0: none)
	int airScoutGapSeconds = 0;    ///< one scout flight every this many seconds (0: a picket, always flying)
	int riderGroup = 0;            ///< gryphons / dragons attack in groups of this many (0: none built)
	int riderMax = 0;
	std::vector<Aim> airTargets;   ///< the riders' target order

	// research and rebuilding
	std::vector<ResearchStep> research;
	int rebuildDelaySeconds = 180;  ///< a lost building is asked for again only after this
};

struct ProfileSet {
	int format = 0;
	std::vector<Profile> bots;
};

/// Read one profile / the whole file. False with a one-line reason on anything malformed, on an
/// unknown kind or research line, and on any fairness value beyond the ceilings above.
bool ParseProfile(const content::JsonValue &value, Profile &profile, std::string &error);
bool ParseProfiles(const content::JsonValue &root, ProfileSet &set, std::string &error);
/// The profile of that id, or nullptr.
const Profile *FindProfile(const ProfileSet &set, const std::string &id);

/// "mod-bot:<mod>:<id>" -> mod, id. False for any other name.
bool SplitSeatName(const std::string &aiName, std::string &mod, std::string &id);
std::string SeatName(const std::string &mod, const std::string &id);
bool IsSeatName(const std::string &aiName);

/// Orders of the last 60 game seconds, one bucket per second.
struct RateCap {
	std::array<uint16_t, 60> used{};
	long second = -1;

	/// Empty the buckets of the seconds that passed since the last call.
	void Advance(long now);
	/// Orders in the 60 seconds ending with `now`.
	int UsedInWindow(long now);
	/// Spend one order at `now` if the window holds fewer than `cap`.
	bool Take(long now, int cap);
	/// Would Take succeed now (nothing spent)?
	bool CanTake(long now, int cap);
	bool operator==(const RateCap &other) const { return used == other.used && second == other.second; }
};

struct Point {
	int x = -1, y = -1;
	bool Valid() const { return x >= 0 && y >= 0; }
	bool operator==(const Point &other) const { return x == other.x && y == other.y; }
};
/// Chebyshev distance in tiles.
int Distance(const Point &a, const Point &b);

/// An enemy unit or building the player's own units see (or have seen), by unit slot and tile.
struct Known {
	int slot = -1;
	Point at;
	Class cls = Class::Other;
	int hp = 100;              ///< per cent of its hit points (units seen now)
	bool affected = false;     ///< under a slowing / weakening spell already (units seen now)
	bool undead = false;       ///< exorcism hurts it (units seen now)
	bool operator==(const Known &other) const { return slot == other.slot && at == other.at; }
};

/// One of the player's own fighters on the map.
struct Fighter {
	int slot = -1;
	Point at;
	bool idle = false;
	Kind kind = Kind::Soldier;
	int hp = 100;              ///< per cent of its hit points
	uint32_t spells = 0;       ///< SpellBit of each spell it can cast now (known, researched, mana, no cool-down)
	bool buffed = false;       ///< under bloodlust / haste
	bool armoured = false;     ///< under unholy armour
	bool aboard = false;       ///< in an own transport (`at` is the transport's tile)
	int cargo = 0;             ///< a transport: units aboard
	bool invisible = false;
};

/// One of the player's own workers on the map.
struct Worker {
	int slot = -1;
	Point at;
	bool idle = false;
	bool repairing = false;
};

/// One of the player's own buildings that has lost hit points (finished buildings only).
struct Damaged {
	int slot = -1;
	Point at;
	int hp = 100;              ///< per cent
	bool tower = false;
	bool attacked = false;     ///< hit in the last seconds
};

/// What the bot may know in one second. Filled by the engine side; nothing in it is beyond a
/// person's view of the same game.
struct World {
	long second = 0;                    ///< game seconds since the start
	int have[KindCount] = {};           ///< units / buildings of the kind (equivalent types counted in)
	int queued[KindCount] = {};         ///< asked of the production, not there yet
	bool unplaced[KindCount] = {};      ///< a building asked of the production whose last try found no place
	int waiting[KindCount] = {};        ///< buildings asked of the production with no worker sent yet (0: not known)
	bool can[KindCount] = {};           ///< the rules and the player's buildings allow it now
	int level[LineCount] = {};          ///< research level done
	bool canResearch[LineCount] = {};   ///< the next level may be researched now
	bool foreign[LineCount] = {};       ///< the line does not exist for the player's race
	int supply = 0, demand = 0;
	int workerBuildSeconds = 15;        ///< from the unit data
	int gold = 0;                       ///< the player's own gold
	int wood = -1;                      ///< the player's own lumber (-1: not known)
	int goldCost[KindCount] = {};       ///< price of one more of the kind (0: not known; a hall upgrade: its upgrade)
	int woodCost[KindCount] = {};
	int researchGold[LineCount] = {};   ///< gold of the line's next level (0: not known)
	int mapWidth = 0, mapHeight = 0;    ///< map size in tiles (0: no scouting points)
	Point home;                         ///< own start position
	std::vector<Point> enemyStarts;     ///< start positions of the enemies (the map preview's)
	std::vector<Fighter> fighters;      ///< own land fighters (those in an own transport too), slot order
	std::vector<Worker> workers;        ///< own workers, slot order
	std::vector<Damaged> damaged;       ///< own finished buildings below their hit points, slot order
	std::vector<Known> seen;            ///< enemy buildings own units see now
	std::vector<Known> seenUnits;       ///< enemy units (not buildings) own units see now, slot order
	std::vector<int> gone;              ///< known slots whose tile own units see without that building
	bool alarm = false;                 ///< an own unit was attacked since the last second
	Point alarmAt;                      ///< the attacked unit's place
	Point alarmTo;                      ///< the attacker's place when own units see it, else invalid
	int plainTowers = 0;                ///< own finished watch towers standing idle (not upgraded yet)
	bool canTower[2] = {};              ///< a watch tower may become a guard tower / a cannon tower now
	/// Gold left in the main mine (the explored gold mine nearest home): a real read only. 0: no
	/// gold mine near home any more (gone); -1: not read (none explored there yet), never "low".
	int mineGold = -1;
	/// Own halls under construction (a classic map can give the start a hall site with its only
	/// worker inside); have[] counts only standing ones.
	int hallSites = 0;
	std::vector<Point> farHalls;        ///< own halls away from home (expansions), slot order
	int spellRange[SpellCount] = {};    ///< from the spell data (0: the spell is not in the game)
	// mod 0.2, part B
	std::vector<Fighter> ships;         ///< own warships and transports, slot order
	std::vector<Fighter> flyers;        ///< own flying machines / zeppelins and gryphons / dragons, slot order
	/// The map's terrain as the map preview shows it to every person before the start: per tile
	/// (mapWidth x mapHeight, row by row) 0 for water, else the number of its piece of land. nullptr:
	/// not known (every place counts as one land).
	const std::vector<uint16_t> *regions = nullptr;
	int waterShare = 0;                 ///< per cent of the map's tiles that are water
	int oilKnown = 0;                   ///< free oil patches on tiles its own units have explored
	int transportSlots = TransportSlots; ///< units a transport carries (from the unit data)
};

/// Buildings of a bot that have no place rule of their own (farms, barracks, towers ...) go near
/// its hall: the engine's place finder is tried within these tiles of the hall in turn, the next
/// only when nothing fits in the one before; 0 = no limit (the whole ground round the hall).
constexpr int NearHallRadii[] = {6, 10, 16, 0};
/// Tries `fits(radius, context)` for each of NearHallRadii in turn; true at the first that fits
/// (`used` gets that radius), false when none does.
bool PlaceNearHall(bool (*fits)(int radius, void *context), void *context, int *used = nullptr);

/// Do two places lie on the same piece of land (a land unit can walk from one to the other, trees
/// and rocks aside)? Always true when the terrain is not known.
bool SameLand(const World &world, const Point &a, const Point &b);

/// A gold mine as the expansion choice sees it: its top-left tile, the gold it holds, and
/// whether the worker's ground reaches it.
struct Mine {
	Point at;
	int gold = 0;
	bool reachable = true;
};
/// A hall (of any player) this near a gold mine (top-left tiles) serves it: the bot's main mine is
/// read within the same distance of home.
constexpr int MineServedTiles = 12;
/// The gold mines a further hall may go beside, the nearest to `home` first (the first of equals):
/// gold left, reachable, no hall in `halls` within MineServedTiles. Indexes into `mines`.
std::vector<int> ExpansionMines(const std::vector<Mine> &mines, const std::vector<Point> &halls, const Point &home);
/// Is the map a sea map for the profile: an enemy start on other land, or water enough?
bool SeaMap(const Profile &profile, const World &world);

/// The bot's memory, saved with the game.
struct State {
	int opening = 0;                    ///< opening steps released
	int planned[KindCount] = {};        ///< buildings the opening asked for
	int peak[KindCount] = {};           ///< most buildings of the kind there were at once
	long lostAt[KindCount];             ///< second a loss was noticed, -1 none
	int research = 0;                   ///< research steps released
	long nextWorkerAt = 0;
	long nextWaveAt = -1;               ///< -1: not set yet (the profile's first wave)
	int waves = 0;
	int waveSize = 0;                   ///< size drawn for the coming wave, 0 not drawn
	std::vector<int> wave;              ///< unit slots out on a wave
	Point target;                       ///< where the wave goes
	uint32_t visitedStarts = 0;         ///< enemy start positions reached and found empty (bit per index)
	uint32_t scouted = 0;               ///< scouting points reached (bit per point of the 3 x 3 map grid)
	Point rally;                        ///< where the wave gathers
	long gatherUntil = -1;              ///< the wave gathers at the rally point until this second; -1 it marches
	Point idleAt;                       ///< the place the wave was last sent to (idle short of it later: cannot get there)
	int targetSlot = -1;                ///< the unit / building aimed at; -1 for a place to look at
	std::vector<int> ignored;           ///< units / buildings this wave could not get to (forgotten when it ends)
	long unplacedSince[KindCount];      ///< second a building asked first found no place, -1 none
	long clearedAt = -1;                ///< last second the home units were moved out of the base for a building
	long alarmDue = -1;
	Point alarmAt;
	Point alarmTo;                      ///< where the defenders go (the attacker seen), else alarmAt
	std::vector<Known> known;           ///< enemy buildings seen (with their class)
	long armySecond = -1;               ///< last second the army had the attention
	RateCap rate;
	// mod 0.2: Captain / Warlord
	int launchSize = 0;                 ///< units the wave set out with
	int focusSlot = -1;                 ///< the enemy unit the wave attacks together
	int retreats = 0;                   ///< waves that came home outnumbered
	std::vector<int> raid;              ///< unit slots of the raid group
	Point raidTarget;
	int raidSlot = -1;                  ///< the worker / hall the raid goes for
	long nextRaidAt = -1;               ///< -1: not set yet (the profile's first raid)
	int scouts = 0;                     ///< scouts sent (the worker counts as the first)
	long nextScoutAt = -1;              ///< -1: not set yet
	int scoutIndex = 0;                 ///< next place a fighter scout goes (starts, then the 3 x 3 points)
	int expansions = 0;                 ///< expansion steps done
	int escorted = 0;                   ///< far halls that had their escort
	long mineFirst = -1;                ///< gold of the main mine when first seen
	int seenPeak[ClassCount] = {};      ///< most enemy units of a class seen at once
	uint32_t counterTowers = 0;         ///< towers asked by a counter (bit 0 flyers, bit 1 cavalry)
	std::vector<std::pair<int, long>> castBusy;  ///< casters given a cast order, and when
	// mod 0.2, part B: sea and air
	std::vector<int> fleet;             ///< warships out with the fleet
	Point fleetTarget;
	int fleetSlot = -1;
	int fleetLaunch = 0;                ///< warships the fleet set out with
	long nextFleetAt = -1;              ///< -1: not set yet
	uint32_t fleetVisited = 0;          ///< the water by each enemy start looked at (bit per start)
	std::vector<int> fleetIgnored;      ///< ships / buildings the fleet could not get to
	Point fleetIdleAt;
	long enemyShipAt = -1;              ///< last second an enemy warship was seen
	int landingPhase = 0;               ///< 0 none, 1 boarding, 2 sailing, 3 sailing back home
	long landingSince = -1;
	long nextLandingAt = -1;            ///< -1: not set yet
	int landingSize = 0;                ///< size drawn for the coming landing, 0 not drawn
	int landings = 0;
	std::vector<int> landing;           ///< land units of the landing
	std::vector<int> ferry;             ///< transports of the landing
	Point landingShore;                 ///< where the transports unload
	Point landingWater;                 ///< the water beside it (the cover goes there)
	int seaResearch = 0;                ///< sea research steps released
	long nextAirScoutAt = -1;           ///< -1: not set yet
	int airScoutIndex = 0;
	std::vector<int> riders;            ///< gryphons / dragons out together
	Point riderTarget;
	int riderSlot = -1;
	int riderLaunch = 0;

	State();
	bool operator==(const State &other) const;
};

/// What an order of one own unit is.
enum class Order : uint8_t { Attack, Move, Repair, Cast, Board, Unload };

/// An order of one own unit: an attack-move to a place (or an attack on the unit `targetSlot`), a
/// move, a repair of the building `targetSlot`, a spell cast at a unit or a place, boarding the own
/// transport `targetSlot`, a transport unloading everything at the place.
struct Command {
	int slot = -1;
	Point to;
	int targetSlot = -1;
	Order order = Order::Attack;
	int spell = -1;          ///< Spell index for Order::Cast
};

/// What one second decides.
struct Decision {
	int release[KindCount] = {};        ///< one more of the kind asked for (0 / 1)
	bool keep = false;                  ///< ask the hall's upgrade
	bool castle = false;                ///< ask the keep's upgrade
	int towerUpgrade = 0;               ///< one watch tower made a guard (1) / cannon (2) tower
	std::vector<std::pair<Line, int>> research;  ///< research asked (line, level)
	std::vector<Command> commands;
	int lumberWorkers = 0;
	bool holdFarms = false;             ///< the engine's own farms are not asked (a building before them has no room)
	int orders = 0;                     ///< orders spent this second
	bool army = false;                  ///< the army had the attention
};

/// Dice: a number 0 .. sides - 1 (the game's synchronised random numbers in play).
using Dice = int (*)(int sides, void *context);

/// One second of a bot. Changes `state`; never spends more orders than the rate cap allows.
Decision Decide(const Profile &profile, const World &world, State &state, Dice dice, void *context);

/// The state as one line of text without quotes or backslashes, and back. False on bad text.
std::string StateText(const State &state);
bool StateFromText(const std::string &text, State &state);

/// The bot debug log's line of one second (no cycle, no engine build queue): the second, gold and
/// wood, food, units per kind, what is asked and not there yet, and the wave.
std::string LogLine(const World &world, const State &state);

/// May a saved game with a bot seat be loaded? `set` nullptr: the mod is not enabled / has no
/// readable profiles. False with the message for the player.
bool SavedSeatLoads(const ProfileSet *set, const std::string &mod, const std::string &id,
                    const std::string &savedHash, const std::string &currentHash, std::string &message);

} // namespace wc2bot

class CPlayer;
class CUnit;
class PlayerAi;

/// Read the profiles of an enabled mod (mods/<mod>/defs/bots.json) and their SHA-256. False with
/// a reason when the mod is not enabled or the file is absent or malformed.
bool Wc2BotReadProfiles(const std::string &mod, wc2bot::ProfileSet &set, std::string &hash, std::string &error);
/// At AI start of a computer seat: a bot name with a playable profile makes the seat a bot (the
/// map's original script id of the slot is dropped) and returns true. A bot name without one
/// logs why, falls back to the default active AI name and returns false. Any other name: false.
bool Wc2BotInit(CPlayer &player, PlayerAi &ai);
/// Is the player a bot seat?
bool Wc2BotRuns(int player);
/// Once a game second for a bot seat: decide and hand the decision to the engine's production
/// and to the units. False (and nothing done) for a player that is no bot seat.
bool Wc2BotSecond(int player);
/// An own unit of a bot seat was attacked: noted for the bot's defence, which answers after the
/// profile's delay. True when the defender's player is a bot seat (the engine's help is not sent).
bool Wc2BotHelpMe(const CUnit &attacker, const CUnit &defender);
/// One order of the engine's production for a bot seat (sending a worker to gather, asking a farm):
/// true when the rate cap and the attention rule allow it. Always true for other players.
bool Wc2BotTake(int player);
/// Would Wc2BotTake allow an order now (nothing spent)? The engine asks first and pays with
/// Wc2BotTake only for an order it really gave (a worker sent, a farm placed).
bool Wc2BotMayTake(int player);
/// May the engine's production ask a farm for a bot seat now? False while a building the profile
/// wants first finds no place (the farm would take its room). Always true for other players.
bool Wc2BotFarmAllowed(int player);
/// Idle workers of a bot seat sent to lumber while fewer than this cut it.
int Wc2BotLumberWanted(int player);
/// The bot seats' states as guarded Lua lines, for the saved game; empty with no bot seat.
std::string Wc2BotSaveText();
/// Forget every bot seat and any refusal (a game ends, or a saved game is about to be read).
void Wc2BotClear();
/// The reason the saved game just read may not be played (its bot's mod is missing or differs);
/// empty when it may.
const std::string &Wc2BotRefusal();
/// Lua: SetWc2BotState(player, seatName, hash, state), Wc2BotRefusal().
void Wc2BotCclRegister();

#endif // WC2BOT_H
