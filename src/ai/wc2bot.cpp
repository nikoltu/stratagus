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
/**@name wc2bot.cpp - A data-described computer player in the game: its view, its orders, its save. */
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

#include "stratagus.h"

#include "wc2bot.h"

#include "../ai/ai_local.h"
#include "actions.h"
#include "commands.h"
#include "depend.h"
#include "filesystem.h"
#include "game.h"
#include "json.h"
#include "map.h"
#include "mods.h"
#include "parameters.h"
#include "player.h"
#include "script.h"
#include "settings.h"
#include "sha256.h"
#include "spells.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"
#include "upgrade.h"
#include "upgrade_structs.h"
#include "util.h"
#include "tile.h"
#include "wc2ai.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>

namespace
{

/// A bot seat: its profile (a copy: the file is not read again during the game) and its memory.
struct Seat {
	std::string mod;
	std::string id;
	std::string hash;              ///< SHA-256 of the mod's defs/bots.json as read at the start
	wc2bot::Profile profile;
	wc2bot::State state;
	bool alarm = false;            ///< an own unit was attacked since the last second
	wc2bot::Point alarmAt;
	wc2bot::Point alarmTo;         ///< where the attacker was seen (the attacked place when not seen)
	int lumberWorkers = 0;
	bool holdFarms = false;        ///< the engine's farms are not asked this second (Wc2BotFarmAllowed)
	/// The map's terrain as the map preview shows it to every person before the start (land pieces,
	/// 0 water), read once; not saved (the same map gives the same numbers after a load).
	std::vector<uint16_t> regions;
	int waterShare = 0;
	bool terrainRead = false;
	int logSeconds = 0;            ///< bot debug log: a line every this many game seconds (0: off)
};

std::array<std::unique_ptr<Seat>, PlayerMax> Seats;
std::string Refusal;

/// The engine's type per kind, human / orc (wc2bot::Kind order).
const char *const KindIdents[wc2bot::KindCount][2] = {
	{"unit-peasant", "unit-peon"},
	{"unit-footman", "unit-grunt"},
	{"unit-archer", "unit-axethrower"},
	{"unit-knight", "unit-ogre"},
	{"unit-farm", "unit-pig-farm"},
	{"unit-human-barracks", "unit-orc-barracks"},
	{"unit-elven-lumber-mill", "unit-troll-lumber-mill"},
	{"unit-human-blacksmith", "unit-orc-blacksmith"},
	{"unit-stables", "unit-ogre-mound"},
	{"unit-town-hall", "unit-great-hall"},
	{"unit-keep", "unit-stronghold"},
	{"unit-castle", "unit-fortress"},
	{"unit-human-watch-tower", "unit-orc-watch-tower"},
	{"unit-mage-tower", "unit-temple-of-the-damned"},
	{"unit-church", "unit-altar-of-storms"},
	{"unit-ballista", "unit-catapult"},
	{"unit-mage", "unit-death-knight"},
	{"unit-human-shipyard", "unit-orc-shipyard"},
	{"unit-human-refinery", "unit-orc-refinery"},
	{"unit-human-foundry", "unit-orc-foundry"},
	{"unit-human-oil-platform", "unit-orc-oil-platform"},
	{"unit-inventor", "unit-alchemist"},
	{"unit-gryphon-aviary", "unit-dragon-roost"},
	{"unit-human-oil-tanker", "unit-orc-oil-tanker"},
	{"unit-human-destroyer", "unit-orc-destroyer"},
	{"unit-battleship", "unit-ogre-juggernaught"},
	{"unit-human-submarine", "unit-orc-submarine"},
	{"unit-human-transport", "unit-orc-transport"},
	{"unit-balloon", "unit-zeppelin"},
	{"unit-gryphon-rider", "unit-dragon"},
};
static_assert(std::size(KindIdents) == static_cast<size_t>(wc2bot::KindCount), "one row per kind");

/// The hall class: every type that is a hall, upgraded or not.
const char *const HallIdents[] = {"unit-town-hall", "unit-keep", "unit-castle",
                                  "unit-great-hall", "unit-stronghold", "unit-fortress"};
const char *const KeepIdents[] = {"unit-keep", "unit-castle", "unit-stronghold", "unit-fortress"};
const char *const PlainKeepIdents[] = {"unit-keep", "unit-stronghold"};
const char *const CastleIdents[] = {"unit-castle", "unit-fortress"};
const char *const TowerIdents[] = {"unit-human-watch-tower", "unit-orc-watch-tower",   "unit-human-guard-tower",
                                   "unit-orc-guard-tower",   "unit-human-cannon-tower", "unit-orc-cannon-tower"};
/// What a watch tower becomes, human / orc: guard tower, cannon tower.
const char *const TowerUpgradeIdents[2][2] = {{"unit-human-guard-tower", "unit-orc-guard-tower"},
                                              {"unit-human-cannon-tower", "unit-orc-cannon-tower"}};

/// The research per line and level, human / orc (nullptr: no such research for that race / level).
const char *const LineIdents[wc2bot::LineCount][wc2bot::MaxLevel][2] = {
	{{"upgrade-sword1", "upgrade-battle-axe1"}, {"upgrade-sword2", "upgrade-battle-axe2"}},
	{{"upgrade-human-shield1", "upgrade-orc-shield1"}, {"upgrade-human-shield2", "upgrade-orc-shield2"}},
	{{"upgrade-arrow1", "upgrade-throwing-axe1"}, {"upgrade-arrow2", "upgrade-throwing-axe2"}},
	{{"upgrade-paladin", "upgrade-ogre-mage"}, {nullptr, nullptr}},
	{{"upgrade-healing", nullptr}, {nullptr, nullptr}},
	{{nullptr, "upgrade-bloodlust"}, {nullptr, nullptr}},
	{{"upgrade-slow", nullptr}, {nullptr, nullptr}},
	{{nullptr, "upgrade-haste"}, {nullptr, nullptr}},
	{{"upgrade-blizzard", nullptr}, {nullptr, nullptr}},
	{{nullptr, "upgrade-death-and-decay"}, {nullptr, nullptr}},
	{{"upgrade-polymorph", nullptr}, {nullptr, nullptr}},
	{{nullptr, "upgrade-death-coil"}, {nullptr, nullptr}},
	{{nullptr, "upgrade-unholy-armor"}, {nullptr, nullptr}},
	{{"upgrade-exorcism", nullptr}, {nullptr, nullptr}},
	{{"upgrade-invisibility", nullptr}, {nullptr, nullptr}},
	{{"upgrade-human-ship-cannon1", "upgrade-orc-ship-cannon1"}, {"upgrade-human-ship-cannon2", "upgrade-orc-ship-cannon2"}},
	{{"upgrade-human-ship-armor1", "upgrade-orc-ship-armor1"}, {"upgrade-human-ship-armor2", "upgrade-orc-ship-armor2"}},
};
static_assert(std::size(LineIdents) == static_cast<size_t>(wc2bot::LineCount), "one row per line");

/// The spells a bot may cast (wc2bot::Spell order).
const char *const SpellIdents[wc2bot::SpellCount] = {"spell-bloodlust", "spell-healing",         "spell-slow",
                                                     "spell-haste",     "spell-blizzard",        "spell-death-and-decay",
                                                     "spell-polymorph", "spell-death-coil",
                                                     "spell-unholy-armor", "spell-exorcism", "spell-invisibility"};

/// The fighters a wave or the defence may take, with their kind: the kinds and their upgraded forms.
struct FighterType {
	const char *ident;
	wc2bot::Kind kind;
};
const FighterType FighterTypes[] = {
	{"unit-footman", wc2bot::Kind::Soldier},  {"unit-grunt", wc2bot::Kind::Soldier},
	{"unit-archer", wc2bot::Kind::Shooter},   {"unit-axethrower", wc2bot::Kind::Shooter},
	{"unit-ranger", wc2bot::Kind::Shooter},   {"unit-berserker", wc2bot::Kind::Shooter},
	{"unit-knight", wc2bot::Kind::Cavalry},   {"unit-ogre", wc2bot::Kind::Cavalry},
	{"unit-paladin", wc2bot::Kind::Cavalry},  {"unit-ogre-mage", wc2bot::Kind::Cavalry},
	{"unit-ballista", wc2bot::Kind::Siege},   {"unit-catapult", wc2bot::Kind::Siege},
	{"unit-mage", wc2bot::Kind::Caster},      {"unit-death-knight", wc2bot::Kind::Caster},
};
/// The own ships the bot gives orders to (tankers are the engine's gathering), with their kind.
const FighterType ShipTypes[] = {
	{"unit-human-destroyer", wc2bot::Kind::Destroyer}, {"unit-orc-destroyer", wc2bot::Kind::Destroyer},
	{"unit-battleship", wc2bot::Kind::Battleship},     {"unit-ogre-juggernaught", wc2bot::Kind::Battleship},
	{"unit-human-submarine", wc2bot::Kind::Submarine}, {"unit-orc-submarine", wc2bot::Kind::Submarine},
	{"unit-human-transport", wc2bot::Kind::Transport}, {"unit-orc-transport", wc2bot::Kind::Transport},
};
/// The own flyers, with their kind.
const FighterType FlyerTypes[] = {
	{"unit-balloon", wc2bot::Kind::Balloon},       {"unit-zeppelin", wc2bot::Kind::Balloon},
	{"unit-gryphon-rider", wc2bot::Kind::Rider},   {"unit-dragon", wc2bot::Kind::Rider},
};

/// What an enemy type is, for the bot's target order, counters and casting.
struct ClassType {
	const char *ident;
	wc2bot::Class cls;
};
const ClassType ClassTypes[] = {
	{"unit-peasant", wc2bot::Class::Worker},         {"unit-peon", wc2bot::Class::Worker},
	{"unit-attack-peasant", wc2bot::Class::Worker},  {"unit-attack-peon", wc2bot::Class::Worker},
	{"unit-footman", wc2bot::Class::Soldier},        {"unit-grunt", wc2bot::Class::Soldier},
	{"unit-archer", wc2bot::Class::Shooter},         {"unit-axethrower", wc2bot::Class::Shooter},
	{"unit-ranger", wc2bot::Class::Shooter},         {"unit-berserker", wc2bot::Class::Shooter},
	{"unit-knight", wc2bot::Class::Cavalry},         {"unit-ogre", wc2bot::Class::Cavalry},
	{"unit-paladin", wc2bot::Class::Cavalry},        {"unit-ogre-mage", wc2bot::Class::Cavalry},
	{"unit-ballista", wc2bot::Class::Siege},         {"unit-catapult", wc2bot::Class::Siege},
	{"unit-mage", wc2bot::Class::Caster},            {"unit-death-knight", wc2bot::Class::Caster},
	{"unit-dwarves", wc2bot::Class::Sapper},         {"unit-goblin-sappers", wc2bot::Class::Sapper},
	{"unit-human-barracks", wc2bot::Class::Barracks}, {"unit-orc-barracks", wc2bot::Class::Barracks},
	{"unit-human-oil-tanker", wc2bot::Class::Tanker}, {"unit-orc-oil-tanker", wc2bot::Class::Tanker},
	{"unit-human-transport", wc2bot::Class::Transport}, {"unit-orc-transport", wc2bot::Class::Transport},
	{"unit-human-oil-platform", wc2bot::Class::Platform}, {"unit-orc-oil-platform", wc2bot::Class::Platform},
	{"unit-human-shipyard", wc2bot::Class::Shipyard}, {"unit-orc-shipyard", wc2bot::Class::Shipyard},
	{"unit-human-foundry", wc2bot::Class::Foundry},   {"unit-orc-foundry", wc2bot::Class::Foundry},
	{"unit-human-refinery", wc2bot::Class::Refinery}, {"unit-orc-refinery", wc2bot::Class::Refinery},
};

bool IsOneOf(const CUnitType &type, const char *const *idents, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		if (type.Ident == idents[i]) {
			return true;
		}
	}
	return false;
}

/// The kind of a type of one of the tables, or false for any other type.
template <size_t N>
bool KindIn(const FighterType (&table)[N], const CUnitType &type, wc2bot::Kind &kind)
{
	for (const FighterType &entry : table) {
		if (type.Ident == entry.ident) {
			kind = entry.kind;
			return true;
		}
	}
	return false;
}

/// The kind of an own fighter's type, or false for any other type.
bool FighterKindOf(const CUnitType &type, wc2bot::Kind &kind)
{
	return KindIn(FighterTypes, type, kind);
}

/// Read the map's terrain once: each tile's piece of land (eight neighbours, water between them),
/// 0 for water. The map preview of the launcher shows every person this picture before the start,
/// so it is no hidden knowledge (fog and units are not read here).
void ReadTerrain(Seat &seat)
{
	if (seat.terrainRead) {
		return;
	}
	seat.terrainRead = true;
	const int width = Map.Info.MapWidth;
	const int height = Map.Info.MapHeight;
	if (width <= 0 || height <= 0) {
		return;
	}
	const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height);
	std::vector<uint16_t> regions(size, 0);
	std::vector<char> water(size, 0);
	size_t wet = 0;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (Map.Field(Vec2i(x, y))->WaterOnMap()) {
				water[static_cast<size_t>(y) * width + x] = 1;
				++wet;
			}
		}
	}
	uint16_t next = 0;
	std::vector<int> open;
	for (size_t start = 0; start < size; ++start) {
		if (water[start] || regions[start] != 0) {
			continue;
		}
		if (next < 0xFFFF) {
			++next;
		}
		regions[start] = next;
		open.assign(1, static_cast<int>(start));
		while (!open.empty()) {
			const int tile = open.back();
			open.pop_back();
			const int tx = tile % width;
			const int ty = tile / width;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const int nx = tx + dx;
					const int ny = ty + dy;
					if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
						continue;
					}
					const size_t n = static_cast<size_t>(ny) * width + nx;
					if (!water[n] && regions[n] == 0) {
						regions[n] = next;
						open.push_back(static_cast<int>(n));
					}
				}
			}
		}
	}
	seat.regions = std::move(regions);
	seat.waterShare = static_cast<int>(wet * 100 / size);
}

wc2bot::Class ClassOf(const CUnitType &type)
{
	if (type.Building) {
		if (IsOneOf(type, HallIdents, std::size(HallIdents))) {
			return wc2bot::Class::Hall;
		}
		if (IsOneOf(type, TowerIdents, std::size(TowerIdents))) {
			return wc2bot::Class::Tower;
		}
	}
	for (const ClassType &entry : ClassTypes) {
		if (type.Ident == entry.ident) {
			return entry.cls;
		}
	}
	if (type.Building) {
		return wc2bot::Class::Building;
	}
	if (type.AirUnit) {
		return wc2bot::Class::Flyer;
	}
	if (type.SeaUnit) {
		return wc2bot::Class::Ship;
	}
	return wc2bot::Class::Other;
}

int RaceColumn(const CPlayer &player)
{
	return PlayerRaces.Name[player.Race] == "orc" ? 1 : 0;
}

long GameSecond()
{
	return static_cast<long>(GameCycle / CYCLES_PER_SECOND);
}

/// What the engine's request table counts for a type: the units of it and of its equivalent types.
int Standing(const CPlayer &player, const CUnitType &type)
{
	int count = player.UnitTypesAiActiveCount[type.Slot];
	if (static_cast<size_t>(type.Slot) < AiHelpers.Equiv().size()) {
		for (const CUnitType *equiv : AiHelpers.Equiv()[type.Slot]) {
			count += player.UnitTypesAiActiveCount[equiv->Slot];
		}
	}
	return count;
}

int CountOf(const CPlayer &player, const char *const *idents, size_t count)
{
	int total = 0;
	for (size_t i = 0; i < count; ++i) {
		if (const CUnitType *type = UnitTypeByIdentOrNull(idents[i])) {
			total += player.UnitTypesAiActiveCount[type->Slot];
		}
	}
	return total;
}

/// Ask the engine's production to have `count` of a type (its request table is a list of
/// "have this many"; the resource manager trains and builds towards it).
void Want(PlayerAi &ai, CUnitType &type, int count)
{
	for (AiRequestType &request : ai.UnitTypeRequests) {
		if (request.Type == &type) {
			request.Count = static_cast<unsigned>(std::max(0, count));
			return;
		}
	}
	if (count <= 0) {
		return;
	}
	AiRequestType request;
	request.Type = &type;
	request.Count = static_cast<unsigned>(count);
	ai.UnitTypeRequests.push_back(request);
}

CUpgrade *UpgradeByIdent(const char *ident)
{
	if (ident == nullptr) {
		return nullptr;
	}
	for (const auto &upgrade : AllUpgrades) {
		if (upgrade && upgrade->Ident == ident) {
			return upgrade.get();
		}
	}
	return nullptr;
}

/// The spell of that ident, or nullptr (SpellTypeByIdent ends the program on an unknown one).
const SpellType *SpellByIdent(const char *ident)
{
	for (const auto &spell : SpellTypeTable) {
		if (spell && spell->Ident == ident) {
			return spell.get();
		}
	}
	return nullptr;
}

CUnit *SlotUnit(int slot)
{
	if (slot < 0 || static_cast<unsigned>(slot) >= UnitManager->GetUsedSlotCount()) {
		return nullptr;
	}
	return &UnitManager->GetSlotUnit(slot);
}

/// The game's own synchronised random numbers.
int SyncDice(int sides, void *)
{
	return sides > 0 ? SyncRand(sides) : 0;
}

int Percent(int value, int max)
{
	return max > 0 ? std::clamp(value * 100 / max, 0, 100) : 100;
}

/// A hall this far from the player's start position is an expansion (its escort goes there).
constexpr int FarHallTiles = 12;
/// The main mine is the gold mine nearest the start position within this many tiles.
constexpr int MainMineTiles = 12;
/// A building hit within this many seconds is under attack (not repaired by a careful profile).
constexpr unsigned long AttackedSeconds = 5;

/// The bot's view of this second: its own player's numbers, what its own units see, the map's
/// start positions. Nothing a person playing the seat could not know.
wc2bot::World BuildWorld(const CPlayer &player, const PlayerAi &ai, const Seat &seat)
{
	using wc2bot::Kind;
	using wc2bot::KindIndex;
	wc2bot::World world;
	world.second = GameSecond();
	const int orc = RaceColumn(player);
	const std::array<int, UnitTypeMax> queued = AiGetBuildRequestsCount(ai);

	for (int k = 0; k < wc2bot::KindCount; ++k) {
		if (const CUnitType *type = UnitTypeByIdentOrNull(KindIdents[k][orc])) {
			world.have[k] = Standing(player, *type);
			world.queued[k] = queued[type->Slot];
			// A building asked and not begun whose last try found no place (or no worker to put it
			// there): the production waits (AiCheckingWork sets Wait on such a try, clears it on a
			// begun one).
			if (type->Building) {
				for (const AiBuildQueue &queue : ai.UnitTypeBuilt) {
					if (queue.Type == type && queue.Want > queue.Made && queue.Wait != 0) {
						world.unplaced[k] = true;
					}
					// asked, no worker sent to it yet (gold, wood, place or worker missing)
					if (queue.Type == type && queue.Want > queue.Made) {
						world.waiting[k] += queue.Want - queue.Made;
					}
				}
			}
		}
	}
	world.have[KindIndex(Kind::Hall)] = CountOf(player, HallIdents, std::size(HallIdents));
	world.have[KindIndex(Kind::Keep)] = CountOf(player, KeepIdents, std::size(KeepIdents));
	world.have[KindIndex(Kind::Castle)] = CountOf(player, CastleIdents, std::size(CastleIdents));
	world.have[KindIndex(Kind::Tower)] = CountOf(player, TowerIdents, std::size(TowerIdents));

	const auto allowed = [&](Kind kind) {
		const CUnitType *type = UnitTypeByIdentOrNull(KindIdents[KindIndex(kind)][orc]);
		return type != nullptr && CheckDependByType(player, *type);
	};
	const bool workers = world.have[KindIndex(Kind::Worker)] > 0;
	world.can[KindIndex(Kind::Worker)] = allowed(Kind::Worker) && world.have[KindIndex(Kind::Hall)] > 0;
	for (const Kind kind : {Kind::Soldier, Kind::Shooter, Kind::Cavalry, Kind::Siege}) {
		world.can[KindIndex(kind)] = allowed(kind) && world.have[KindIndex(Kind::Barracks)] > 0;
	}
	world.can[KindIndex(Kind::Caster)] = allowed(Kind::Caster) && world.have[KindIndex(Kind::MageTower)] > 0;
	// ships at a shipyard, flying machines at the inventor, gryphons at the aviary
	for (const Kind kind : {Kind::Tanker, Kind::Destroyer, Kind::Battleship, Kind::Submarine, Kind::Transport}) {
		world.can[KindIndex(kind)] = allowed(kind) && world.have[KindIndex(Kind::Shipyard)] > 0;
	}
	world.can[KindIndex(Kind::Balloon)] = allowed(Kind::Balloon) && world.have[KindIndex(Kind::Inventor)] > 0;
	world.can[KindIndex(Kind::Rider)] = allowed(Kind::Rider) && world.have[KindIndex(Kind::Aviary)] > 0;
	for (int k = 0; k < wc2bot::KindCount; ++k) {
		if (wc2bot::IsBuilding(static_cast<Kind>(k))) {
			world.can[k] = allowed(static_cast<Kind>(k)) && workers;
		}
	}
	{
		const CUnitType *hall = UnitTypeByIdentOrNull(KindIdents[KindIndex(Kind::Hall)][orc]);
		world.can[KindIndex(Kind::Keep)] = allowed(Kind::Keep) && hall != nullptr
		                                    && player.UnitTypesAiActiveCount[hall->Slot] > 0
		                                    && world.have[KindIndex(Kind::Keep)] == 0;
		world.can[KindIndex(Kind::Castle)] = allowed(Kind::Castle)
		                                      && CountOf(player, PlainKeepIdents, std::size(PlainKeepIdents)) > 0
		                                      && world.have[KindIndex(Kind::Castle)] == 0;
	}

	for (int line = 0; line < wc2bot::LineCount; ++line) {
		const int levels = wc2bot::LineLevels(static_cast<wc2bot::Line>(line));
		if (LineIdents[line][0][orc] == nullptr || UpgradeByIdent(LineIdents[line][0][orc]) == nullptr) {
			world.foreign[line] = true; // not of this race (or not in this game's data)
			continue;
		}
		int level = 0;
		for (int lv = 1; lv <= levels; ++lv) {
			const char *ident = LineIdents[line][lv - 1][orc];
			if (ident != nullptr && UpgradeIdentAllowed(player, ident) == 'R') {
				level = lv;
			}
		}
		world.level[line] = level;
		if (level < levels) {
			const char *next = LineIdents[line][level][orc];
			world.canResearch[line] = next != nullptr && UpgradeByIdent(next) != nullptr
			                          && UpgradeIdentAllowed(player, next) == 'A' && CheckDependByIdent(player, next);
		}
	}

	world.supply = player.Supply;
	world.demand = player.Demand;
	world.gold = player.Resources[GoldCost];
	world.wood = player.Resources[WoodCost];
	// Prices (what the production pays; a hall upgrade: the price of the upgraded type).
	for (int k = 0; k < wc2bot::KindCount; ++k) {
		if (const CUnitType *type = UnitTypeByIdentOrNull(KindIdents[k][orc])) {
			world.goldCost[k] = std::max(0, type->Stats[player.Index].Costs[GoldCost]);
			world.woodCost[k] = std::max(0, type->Stats[player.Index].Costs[WoodCost]);
		}
	}
	for (int line = 0; line < wc2bot::LineCount; ++line) {
		const int level = world.level[line];
		if (world.foreign[line] || level >= wc2bot::LineLevels(static_cast<wc2bot::Line>(line))) {
			continue;
		}
		if (const char *next = LineIdents[line][level][orc]) {
			if (const CUpgrade *upgrade = UpgradeByIdent(next)) {
				world.researchGold[line] = std::max(0, upgrade->Costs[GoldCost]);
			}
		}
	}
	world.mapWidth = Map.Info.MapWidth;
	world.mapHeight = Map.Info.MapHeight;
	if (!seat.regions.empty()) {
		world.regions = &seat.regions;
		world.waterShare = seat.waterShare;
	}
	if (const CUnitType *transport = UnitTypeByIdentOrNull(KindIdents[KindIndex(Kind::Transport)][orc])) {
		world.transportSlots = std::max(1, transport->MaxOnBoard);
	}
	if (const CUnitType *worker = UnitTypeByIdentOrNull(KindIdents[KindIndex(Kind::Worker)][orc])) {
		const int cycles = worker->Stats[player.Index].Costs[TimeCost] * WC2_FRAMES_PER_TIME_UNIT;
		world.workerBuildSeconds = std::max(1, cycles / CYCLES_PER_SECOND);
	}

	// Start positions: the map preview shows every person where each player starts.
	if (Map.Info.IsPointOnMap(player.StartPos)) {
		world.home = {player.StartPos.x, player.StartPos.y};
	}
	for (int i = 0; i < PlayerNumNeutral; ++i) {
		const CPlayer &other = Players[i];
		if (i == player.Index || other.Type == PlayerTypes::PlayerNobody || !player.IsEnemy(i)
		    || !Map.Info.IsPointOnMap(other.StartPos)) {
			continue;
		}
		world.enemyStarts.push_back({other.StartPos.x, other.StartPos.y});
	}

	// Own fighters, workers, damaged buildings, idle watch towers and far halls on the map.
	const SpellType *spells[wc2bot::SpellCount];
	for (int s = 0; s < wc2bot::SpellCount; ++s) {
		spells[s] = SpellByIdent(SpellIdents[s]);
		world.spellRange[s] = spells[s] != nullptr ? std::max(0, spells[s]->Range) : 0;
	}
	const CUnitType *watchTower = UnitTypeByIdentOrNull(KindIdents[KindIndex(Kind::Tower)][orc]);
	const CUnitType *workerType = UnitTypeByIdentOrNull(KindIdents[KindIndex(Kind::Worker)][orc]);
	std::vector<std::pair<int, wc2bot::Point>> farHalls;
	for (const CUnit *unit : player.GetUnits()) {
		if (!unit || !unit->Type || !unit->IsAlive()) {
			continue;
		}
		const CUnitType &type = *unit->Type;
		const int slot = static_cast<int>(UnitNumber(*unit));
		const wc2bot::Point at{unit->tilePos.x, unit->tilePos.y};
		if (type.Building && IsOneOf(type, HallIdents, std::size(HallIdents)) && world.home.Valid()
		    && wc2bot::Distance(at, world.home) >= FarHallTiles) {
			farHalls.emplace_back(slot, at);
		}
		if (type.Building && unit->CurrentAction() == UnitAction::Built && unit->IsAliveOnMap()
		    && IsOneOf(type, HallIdents, std::size(HallIdents))) {
			++world.hallSites;
		}
		wc2bot::Kind kind = Kind::Soldier;
		if (!unit->IsAliveOnMap()) {
			// a land fighter in an own transport: listed aboard, at the transport's tile
			const CUnit *ferry = unit->Container;
			if (ferry && ferry->IsAliveOnMap() && ferry->Player == &player && FighterKindOf(type, kind)) {
				wc2bot::Fighter fighter;
				fighter.slot = slot;
				fighter.at = {ferry->tilePos.x, ferry->tilePos.y};
				fighter.kind = kind;
				fighter.aboard = true;
				fighter.hp = Percent(unit->Variable[HP_INDEX].Value, unit->Variable[HP_INDEX].Max);
				world.fighters.push_back(fighter);
			}
			continue;
		}
		if (KindIn(ShipTypes, type, kind) || KindIn(FlyerTypes, type, kind)) {
			wc2bot::Fighter craft;
			craft.slot = slot;
			craft.at = at;
			craft.idle = unit->IsIdle();
			craft.kind = kind;
			craft.hp = Percent(unit->Variable[HP_INDEX].Value, unit->Variable[HP_INDEX].Max);
			craft.cargo = unit->BoardCount;
			craft.invisible = unit->Variable[INVISIBLE_INDEX].Value > 0;
			(type.AirUnit ? world.flyers : world.ships).push_back(craft);
			continue;
		}
		if (FighterKindOf(type, kind)) {
			wc2bot::Fighter fighter;
			fighter.slot = slot;
			fighter.at = at;
			fighter.idle = unit->IsIdle();
			fighter.kind = kind;
			fighter.hp = Percent(unit->Variable[HP_INDEX].Value, unit->Variable[HP_INDEX].Max);
			fighter.buffed = unit->Variable[BLOODLUST_INDEX].Value > 0 || unit->Variable[HASTE_INDEX].Value > 0;
			fighter.armoured = unit->Variable[UNHOLYARMOR_INDEX].Value > 0;
			for (int s = 0; s < wc2bot::SpellCount; ++s) {
				const SpellType *spell = spells[s];
				if (spell == nullptr || static_cast<size_t>(spell->Slot) >= type.CanCastSpell.size()
				    || !type.CanCastSpell[spell->Slot] || !SpellIsAvailable(player, spell->Slot)
				    || unit->Variable[MANA_INDEX].Value < spell->ManaCost
				    || (static_cast<size_t>(spell->Slot) < unit->SpellCoolDownTimers.size()
				        && unit->SpellCoolDownTimers[spell->Slot] != 0)) {
					continue;
				}
				fighter.spells |= wc2bot::SpellBit(static_cast<wc2bot::Spell>(s));
			}
			world.fighters.push_back(fighter);
		} else if (workerType != nullptr && &type == workerType) {
			wc2bot::Worker worker;
			worker.slot = slot;
			worker.at = at;
			worker.idle = unit->IsIdle();
			worker.repairing = unit->CurrentAction() == UnitAction::Repair;
			world.workers.push_back(worker);
		} else if (type.Building && unit->CurrentAction() != UnitAction::Built) {
			if (unit->Variable[HP_INDEX].Value < unit->Variable[HP_INDEX].Max) {
				wc2bot::Damaged damaged;
				damaged.slot = slot;
				damaged.at = at;
				damaged.hp = Percent(unit->Variable[HP_INDEX].Value, unit->Variable[HP_INDEX].Max);
				damaged.tower = IsOneOf(type, TowerIdents, std::size(TowerIdents));
				damaged.attacked =
					unit->Attacked != 0 && unit->Attacked + AttackedSeconds * CYCLES_PER_SECOND >= GameCycle;
				world.damaged.push_back(damaged);
			}
			if (&type == watchTower && unit->CurrentAction() == UnitAction::Still) {
				++world.plainTowers;
			}
		}
	}
	const auto fighterSlot = [](const wc2bot::Fighter &a, const wc2bot::Fighter &b) { return a.slot < b.slot; };
	std::sort(world.fighters.begin(), world.fighters.end(), fighterSlot);
	std::sort(world.workers.begin(), world.workers.end(),
	          [](const wc2bot::Worker &a, const wc2bot::Worker &b) { return a.slot < b.slot; });
	std::sort(world.damaged.begin(), world.damaged.end(),
	          [](const wc2bot::Damaged &a, const wc2bot::Damaged &b) { return a.slot < b.slot; });
	std::sort(world.ships.begin(), world.ships.end(), fighterSlot);
	std::sort(world.flyers.begin(), world.flyers.end(), fighterSlot);
	std::sort(farHalls.begin(), farHalls.end(),
	          [](const std::pair<int, wc2bot::Point> &a, const std::pair<int, wc2bot::Point> &b) { return a.first < b.first; });
	for (const auto &hall : farHalls) {
		world.farHalls.push_back(hall.second);
	}
	// A watch tower may become a guard / cannon tower now: allowed and paid for (else the bot would
	// pay an order each second for an upgrade the engine refuses).
	for (int u = 0; u < 2; ++u) {
		const CUnitType *upgrade = UnitTypeByIdentOrNull(TowerUpgradeIdents[u][orc]);
		world.canTower[u] = upgrade != nullptr && CheckDependByType(player, *upgrade)
		                    && player.CheckCosts(upgrade->Stats[player.Index].Costs, false) == 0;
	}

	// Enemy buildings and units in the sight of the player's own units (no shared vision, no radar;
	// a cloaked or invisible unit is not counted); the main mine's gold; the oil patches on tiles its
	// units have explored.
	int mineDistance = -1;
	bool mineNear = false; // a gold mine near home, explored or not
	const int undeadFlag = UnitTypeVar.BoolFlagNameLookup["isundead"];
	for (const CUnit *unit : UnitManager->GetUnits()) {
		// a gold mine counts while it is alive, also in a moment it is off the map
		if (!unit || !unit->Type || !(unit->IsAliveOnMap() || (unit->IsAlive() && unit->Type->GivesResource == GoldCost))) {
			continue;
		}
		if (unit->Type->GivesResource == OilCost && unit->Type->Ident == "unit-oil-patch") {
			if (Map.Field(unit->tilePos)->playerInfo.IsExplored(player)) {
				++world.oilKnown;
			}
			continue;
		}
		if (unit->Type->GivesResource == GoldCost && world.home.Valid()) {
			const wc2bot::Point at{unit->tilePos.x, unit->tilePos.y};
			const int d = wc2bot::Distance(at, world.home);
			if (d <= MainMineTiles) {
				mineNear = true;
				if ((mineDistance < 0 || d < mineDistance) && Map.Field(unit->tilePos)->playerInfo.IsExplored(player)) {
					mineDistance = d;
					world.mineGold = unit->ResourcesHeld;
				}
			}
			continue;
		}
		if (!player.IsEnemy(*unit) || unit->VisCount[player.Index] == 0) {
			continue;
		}
		wc2bot::Known seen;
		seen.slot = static_cast<int>(UnitNumber(*unit));
		seen.at = {unit->tilePos.x, unit->tilePos.y};
		seen.cls = ClassOf(*unit->Type);
		if (unit->Type->Building) {
			world.seen.push_back(seen);
		} else if (!unit->Type->BoolFlag[PERMANENTCLOAK_INDEX].value && unit->Variable[INVISIBLE_INDEX].Value <= 0
		           && !unit->Type->BoolFlag[REVEALER_INDEX].value) {
			seen.hp = Percent(unit->Variable[HP_INDEX].Value, unit->Variable[HP_INDEX].Max);
			seen.affected = unit->Variable[SLOW_INDEX].Value > 0;
			seen.undead = undeadFlag >= 0 && static_cast<size_t>(undeadFlag) < unit->Type->BoolFlag.size()
			              && unit->Type->BoolFlag[undeadFlag].value;
			world.seenUnits.push_back(seen);
		}
	}
	// No gold mine near home at all: the main mine is gone (a real read, 0). One there but not
	// explored stays -1 (not read; never counts as low).
	if (world.home.Valid() && !mineNear) {
		world.mineGold = 0;
	}
	const auto bySlot = [](const wc2bot::Known &a, const wc2bot::Known &b) { return a.slot < b.slot; };
	std::sort(world.seen.begin(), world.seen.end(), bySlot);
	std::sort(world.seenUnits.begin(), world.seenUnits.end(), bySlot);
	// A known building whose place the player's units see now without it is gone.
	for (const wc2bot::Known &known : seat.state.known) {
		const Vec2i pos(known.at.x, known.at.y);
		if (!Map.Info.IsPointOnMap(pos) || !Map.Field(pos)->playerInfo.IsVisible(player)) {
			continue;
		}
		const CUnit *unit = SlotUnit(known.slot);
		const bool there = unit && unit->IsAliveOnMap() && unit->Type && unit->Type->Building
		                   && player.IsEnemy(*unit) && unit->tilePos.x == pos.x && unit->tilePos.y == pos.y;
		if (!there) {
			world.gone.push_back(known.slot);
		}
	}

	// An oil platform needs a tanker to build it and a patch its units have seen.
	world.can[KindIndex(Kind::Platform)] = allowed(Kind::Platform) && world.have[KindIndex(Kind::Tanker)] > 0
	                                        && world.oilKnown > 0;

	world.alarm = seat.alarm;
	world.alarmAt = seat.alarmAt;
	world.alarmTo = seat.alarmTo;
	return world;
}

/// One own unit's order of the decision (the unit and its goal checked again here).
void Give(CPlayer &player, const wc2bot::Command &command)
{
	CUnit *unit = SlotUnit(command.slot);
	const Vec2i to(command.to.x, command.to.y);
	if (!unit || !unit->IsAliveOnMap() || unit->Player != &player || !Map.Info.IsPointOnMap(to)) {
		return;
	}
	CUnit *target = SlotUnit(command.targetSlot);
	if (target && (!target->IsAliveOnMap() || !target->Type)) {
		target = nullptr;
	}
	switch (command.order) {
		case wc2bot::Order::Move:
			CommandMove(*unit, to, EFlushMode::On);
			break;
		case wc2bot::Order::Repair:
			if (target && target->Player == &player) {
				CommandRepair(*unit, target->tilePos, target, EFlushMode::On);
			}
			break;
		case wc2bot::Order::Board:
			if (target && target->Player == &player && target->Type->CanTransport()
			    && target->BoardCount < target->Type->MaxOnBoard) {
				CommandBoard(*unit, *target, EFlushMode::On);
			}
			break;
		case wc2bot::Order::Unload:
			if (unit->Type->CanTransport() && unit->BoardCount > 0) {
				CommandUnload(*unit, to, nullptr, EFlushMode::On);
			}
			break;
		case wc2bot::Order::Cast: {
			if (command.spell < 0 || command.spell >= wc2bot::SpellCount) {
				break;
			}
			const SpellType *spell = SpellByIdent(SpellIdents[command.spell]);
			if (spell == nullptr) {
				break;
			}
			CUnit *dest = spell->Target == ETarget::Unit ? target : nullptr;
			const Vec2i pos = target && spell->Target == ETarget::Unit ? target->tilePos : to;
			if (CanCastSpell(*unit, *spell, dest, pos)) {
				CommandSpellCast(*unit, pos, dest, *spell, EFlushMode::On);
			}
			break;
		}
		case wc2bot::Order::Attack:
		default:
			if (target && player.IsEnemy(*target)) {
				CommandAttack(*unit, target->tilePos, target, EFlushMode::On);
			} else {
				CommandAttack(*unit, to, nullptr, EFlushMode::On);
			}
			break;
	}
}

/// Seconds between the lines of the bot debug log: the preference BotLogSeconds (wc2.preferences:
/// the user's preferences.lua, or set from Lua before the game starts), read at the seat's start.
/// 0 or none: no log, nothing done.
int BotLogSeconds()
{
	int seconds = 0;
	lua_getglobal(Lua, GameName.c_str());
	if (lua_istable(Lua, -1)) {
		lua_getfield(Lua, -1, "preferences");
		if (lua_istable(Lua, -1)) {
			lua_getfield(Lua, -1, "BotLogSeconds");
			if (lua_isnumber(Lua, -1)) {
				seconds = static_cast<int>(lua_tonumber(Lua, -1));
			}
			lua_pop(Lua, 1);
		}
		lua_pop(Lua, 1);
	}
	lua_pop(Lua, 1);
	return std::max(0, seconds);
}

/// The bot debug log, <user dir>/<game>/logs/bots.log, opened at its first line and appended to.
FILE *BotLog = nullptr;

/// One line of the bot debug log: cycle, player, profile, wc2bot::LogLine, the engine's build queue
/// (type made/wanted). A log that cannot be opened switches the seat's log off.
void WriteBotLog(const CPlayer &player, const PlayerAi &ai, Seat &seat, const wc2bot::World &world)
{
	if (BotLog == nullptr) {
		fs::path path = Parameters::Instance.GetUserDirectory();
		if (!GameName.empty()) {
			path /= GameName;
		}
		path /= "logs";
		std::error_code error;
		fs::create_directories(path, error);
		path /= "bots.log";
		BotLog = fopen(path.string().c_str(), "a");
		LogPrint("AI: bot log %s%s\n", path.string().c_str(), BotLog == nullptr ? " cannot be opened" : "");
		if (BotLog == nullptr) {
			seat.logSeconds = 0;
			return;
		}
	}
	std::string queue;
	for (const AiBuildQueue &entry : ai.UnitTypeBuilt) {
		queue += " " + (entry.Type ? entry.Type->Ident : std::string("?")) + " " + std::to_string(entry.Made) + "/"
		         + std::to_string(entry.Want);
	}
	fprintf(BotLog, "c%lu p%d %s %s | queue%s\n", GameCycle, player.Index, seat.id.c_str(),
	        wc2bot::LogLine(world, seat.state).c_str(), queue.c_str());
	fflush(BotLog);
}

/// Hand a decision to the engine: the request table, the hall's upgrades, a tower's upgrade,
/// research, unit orders.
void Apply(CPlayer &player, PlayerAi &ai, Seat &seat, const wc2bot::Decision &decision)
{
	using wc2bot::Kind;
	using wc2bot::KindIndex;
	const int orc = RaceColumn(player);
	const std::array<int, UnitTypeMax> queued = AiGetBuildRequestsCount(ai);

	// Every kind is written each second as "what stands for it now", raised by what was released:
	// a unit lost is not trained again unless the bot asks for it (and pays the order).
	for (int k = 0; k < wc2bot::KindCount; ++k) {
		if (wc2bot::IsHallUpgrade(static_cast<Kind>(k))) {
			continue;
		}
		if (CUnitType *type = UnitTypeByIdentOrNull(KindIdents[k][orc])) {
			Want(ai, *type, Standing(player, *type) + queued[type->Slot] + decision.release[k]);
		}
	}
	for (const Kind upgrade : {Kind::Keep, Kind::Castle}) {
		if (!(upgrade == Kind::Keep ? decision.keep : decision.castle)) {
			continue;
		}
		if (CUnitType *type = UnitTypeByIdentOrNull(KindIdents[KindIndex(upgrade)][orc])) {
			if (std::find(ai.UpgradeToRequests.begin(), ai.UpgradeToRequests.end(), type) == ai.UpgradeToRequests.end()) {
				ai.UpgradeToRequests.push_back(type);
			}
		}
	}
	if (decision.towerUpgrade == 1 || decision.towerUpgrade == 2) {
		CUnitType *watch = UnitTypeByIdentOrNull(KindIdents[KindIndex(Kind::Tower)][orc]);
		CUnitType *into = UnitTypeByIdentOrNull(TowerUpgradeIdents[decision.towerUpgrade - 1][orc]);
		if (watch != nullptr && into != nullptr && CheckDependByType(player, *into)
		    && player.CheckCosts(into->Stats[player.Index].Costs, false) == 0) {
			for (CUnit *unit : player.GetUnits()) {
				if (unit && unit->Type == watch && unit->IsAliveOnMap() && unit->CurrentAction() == UnitAction::Still) {
					CommandUpgradeTo(*unit, *into, EFlushMode::On);
					break;
				}
			}
		}
	}
	for (const auto &[line, level] : decision.research) {
		CUpgrade *upgrade = UpgradeByIdent(LineIdents[static_cast<int>(line)][level - 1][orc]);
		if (upgrade
		    && std::find(ai.ResearchRequests.begin(), ai.ResearchRequests.end(), upgrade) == ai.ResearchRequests.end()) {
			ai.ResearchRequests.push_back(upgrade);
		}
	}
	for (const wc2bot::Command &command : decision.commands) {
		Give(player, command);
	}
	seat.lumberWorkers = decision.lumberWorkers;
	seat.holdFarms = decision.holdFarms;
}

} // namespace

bool Wc2BotReadProfiles(const std::string &mod, wc2bot::ProfileSet &set, std::string &hash, std::string &error)
{
	set = wc2bot::ProfileSet();
	hash.clear();
	const std::string file = "mods/" + mod + "/defs/bots.json";
	const long size = ModsFileSize(file);
	if (size <= 0 || size > 1024 * 1024) {
		error = "the mod '" + mod + "' is not enabled or has no defs/bots.json";
		return false;
	}
	std::ifstream in(ModsLocate(file), std::ios::binary);
	if (!in) {
		error = file + " cannot be read";
		return false;
	}
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	content::JsonValue root;
	std::string parseError;
	if (!content::parseJson(text, root, parseError)) {
		error = file + ": " + parseError;
		return false;
	}
	if (!wc2bot::ParseProfiles(root, set, error)) {
		error = file + ": " + error;
		return false;
	}
	hash = content::Sha256Hex(text);
	return true;
}

bool Wc2BotInit(CPlayer &player, PlayerAi &ai)
{
	std::string mod;
	std::string id;
	if (player.Index < 0 || player.Index >= PlayerMax || !wc2bot::SplitSeatName(player.AiName, mod, id)) {
		return false;
	}
	Seats[player.Index] = nullptr;
	auto seat = std::make_unique<Seat>();
	wc2bot::ProfileSet set;
	std::string error;
	if (Wc2BotReadProfiles(mod, set, seat->hash, error)) {
		const wc2bot::Profile *profile = wc2bot::FindProfile(set, id);
		if (profile != nullptr && profile->playable) {
			seat->mod = mod;
			seat->id = id;
			seat->profile = *profile;
			seat->lumberWorkers = profile->lumberWorkers;
			seat->logSeconds = BotLogSeconds();
			// Further halls only by the bot's own expansion rule (paid, timed): the engine's
			// "build a depot nearer the resource" is not used for a bot seat.
			ai.BuildDepots = false;
			// The map's original script of this slot would run beside the bot: it is dropped.
			Wc2AiDropId(player.Index);
			LogPrint("AI: player %d: computer player '%s' of the mod '%s'\n", player.Index, id.c_str(), mod.c_str());
			Seats[player.Index] = std::move(seat);
			return true;
		}
		error = profile == nullptr ? "the mod has no computer player of that id" : "that computer player is not ready";
	}
	LogPrint("AI: player %d: computer player '%s' of the mod '%s' cannot run (%s); the seat plays as it would "
	         "without the mod\n",
	         player.Index, id.c_str(), mod.c_str(), error.c_str());
	player.AiName = DEFAULT_ACTIVE_AI;
	return false;
}

bool Wc2BotRuns(int player)
{
	return player >= 0 && player < PlayerMax && Seats[player] != nullptr && Players[player].Ai != nullptr;
}

bool Wc2BotSecond(int player)
{
	if (!Wc2BotRuns(player)) {
		return false;
	}
	Seat &seat = *Seats[player];
	CPlayer &owner = Players[player];
	PlayerAi &ai = *owner.Ai;
	ReadTerrain(seat);
	const wc2bot::World world = BuildWorld(owner, ai, seat);
	seat.alarm = false;
	const wc2bot::Decision decision = wc2bot::Decide(seat.profile, world, seat.state, SyncDice, nullptr);
	Apply(owner, ai, seat, decision);
	if (seat.logSeconds > 0 && world.second % seat.logSeconds == 0) {
		WriteBotLog(owner, ai, seat, world);
	}
	return true;
}

bool Wc2BotHelpMe(const CUnit &attacker, const CUnit &defender)
{
	const int index = defender.Player->Index;
	if (!Wc2BotRuns(index)) {
		return false;
	}
	Seat &seat = *Seats[index];
	const wc2bot::State &state = seat.state;
	const int slot = static_cast<int>(UnitNumber(defender));
	const auto in = [slot](const std::vector<int> &slots) {
		return std::find(slots.begin(), slots.end(), slot) != slots.end();
	};
	// A unit out (wave, raid, landing, fleet, riders) or on the water / in the air is no alarm for the
	// land defence at home (the fleet meets ships near home by itself).
	const bool away = in(state.wave) || in(state.raid) || in(state.landing) || in(state.fleet) || in(state.riders)
	                  || defender.Type->SeaUnit || defender.Type->AirUnit;
	if (!seat.alarm && !away && defender.Player->IsEnemy(*attacker.Player)) {
		seat.alarm = true;
		// The attacker's place when the bot's own units see it (a person sees it too), else the
		// attacked unit's: defenders sent to the attacked place stand there while a unit that
		// outranges them (a ballista) shoots them one by one.
		const bool attackerSeen = attacker.IsAliveOnMap() && attacker.VisCount[index] > 0;
		const Vec2i &to = attackerSeen ? attacker.tilePos : defender.tilePos;
		seat.alarmAt = {defender.tilePos.x, defender.tilePos.y};
		seat.alarmTo = {to.x, to.y};
	}
	return true;
}

bool Wc2BotTake(int player)
{
	if (!Wc2BotRuns(player)) {
		return true;
	}
	Seat &seat = *Seats[player];
	const long now = GameSecond();
	if (seat.profile.singleAttention && seat.state.armySecond == now) {
		return false; // the army has this second
	}
	return seat.state.rate.Take(now, seat.profile.ordersPerMinute);
}

bool Wc2BotMayTake(int player)
{
	if (!Wc2BotRuns(player)) {
		return true;
	}
	Seat &seat = *Seats[player];
	const long now = GameSecond();
	if (seat.profile.singleAttention && seat.state.armySecond == now) {
		return false;
	}
	return seat.state.rate.CanTake(now, seat.profile.ordersPerMinute);
}

bool Wc2BotFarmAllowed(int player)
{
	return !Wc2BotRuns(player) || !Seats[player]->holdFarms;
}

int Wc2BotLumberWanted(int player)
{
	return Wc2BotRuns(player) ? Seats[player]->lumberWorkers : 0;
}

std::string Wc2BotSaveText()
{
	std::string text;
	for (int i = 0; i < PlayerMax; ++i) {
		if (!Seats[i]) {
			continue;
		}
		const Seat &seat = *Seats[i];
		text += "if SetWc2BotState then SetWc2BotState(" + std::to_string(i) + ", \""
		        + wc2bot::SeatName(seat.mod, seat.id) + "\", \"" + seat.hash + "\", \""
		        + wc2bot::StateText(seat.state) + "\") end\n";
	}
	return text;
}

void Wc2BotClear()
{
	for (auto &seat : Seats) {
		seat = nullptr;
	}
	Refusal.clear();
}

const std::string &Wc2BotRefusal()
{
	return Refusal;
}

/// Lua: SetWc2BotState(player, "mod-bot:<mod>:<id>", hash, state) -- from a saved game. The mod
/// must be enabled with the same defs/bots.json; otherwise the game is refused (Wc2BotRefusal).
static int CclSetWc2BotState(lua_State *l)
{
	LuaCheckArgs(l, 4);
	const int player = LuaToNumber(l, 1);
	const std::string name(LuaToString(l, 2));
	const std::string savedHash(LuaToString(l, 3));
	const std::string text(LuaToString(l, 4));
	if (player < 0 || player >= PlayerMax) {
		return 0;
	}
	std::string mod;
	std::string id;
	if (!wc2bot::SplitSeatName(name, mod, id)) {
		if (Refusal.empty()) {
			Refusal = "This saved game names a computer player this version cannot read ('" + name + "').";
		}
		return 0;
	}
	wc2bot::ProfileSet set;
	std::string currentHash;
	std::string error;
	const bool read = Wc2BotReadProfiles(mod, set, currentHash, error);
	std::string message;
	if (!wc2bot::SavedSeatLoads(read ? &set : nullptr, mod, id, savedHash, currentHash, message)) {
		if (Refusal.empty()) {
			Refusal = message;
		}
		return 0;
	}
	auto seat = std::make_unique<Seat>();
	seat->mod = mod;
	seat->id = id;
	seat->hash = currentHash;
	seat->profile = *wc2bot::FindProfile(set, id);
	seat->lumberWorkers = seat->profile.lumberWorkers;
	if (!wc2bot::StateFromText(text, seat->state)) {
		if (Refusal.empty()) {
			Refusal = "The saved state of the computer player '" + id + "' is damaged.";
		}
		return 0;
	}
	if (Players[player].Ai) {
		Players[player].Ai->BuildDepots = false;
	}
	Wc2AiDropId(player);
	Seats[player] = std::move(seat);
	return 0;
}

/// Lua: Wc2BotRefusal() -- why the saved game just read may not be played, or nil.
static int CclWc2BotRefusal(lua_State *l)
{
	LuaCheckArgs(l, 0);
	if (Refusal.empty()) {
		lua_pushnil(l);
	} else {
		lua_pushlstring(l, Refusal.data(), Refusal.size());
	}
	return 1;
}

void Wc2BotCclRegister()
{
	lua_register(Lua, "SetWc2BotState", CclSetWc2BotState);
	lua_register(Lua, "Wc2BotRefusal", CclWc2BotRefusal);
}
