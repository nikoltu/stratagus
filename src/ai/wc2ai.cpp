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
/**@name wc2ai.cpp - The original game's computer-player definitions in the engine. */
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

#include "wc2ai.h"

#include "../ai/ai_local.h"
#include "action/action_resource.h"
#include "commands.h"
#include "depend.h"
#include "iolib.h"
#include "script.h"
#include "player.h"
#include "settings.h"
#include "unit.h"
#include "unit_find.h"
#include "unittype.h"
#include "upgrade.h"
#include "upgrade_structs.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <set>

namespace wc2ai
{

/// Read Data/Rez/ai.bin of the game folder once. The file is the player's own; nothing of it is
/// shipped. Absent or not decodable: an empty program, and the callers keep their own behaviour.
const Program &GameProgram()
{
	static Program program;
	static bool tried = false;
	if (tried) {
		return program;
	}
	tried = true;
	const fs::path resolved = LibraryFileName("Data/Rez/ai.bin");
	if (resolved.empty()) {
		LogPrint("wc2ai: no Data/Rez/ai.bin in the game folder\n");
		return program;
	}
	std::error_code ec;
	const uintmax_t size = fs::file_size(resolved, ec);
	if (ec || size == 0 || size > 0xFFFF) {
		LogPrint("wc2ai: Data/Rez/ai.bin has an unexpected size\n");
		return program;
	}
	std::vector<uint8_t> data(static_cast<size_t>(size));
	CFile file;
	if (file.open(resolved.string().c_str(), CL_OPEN_READ) == -1) {
		LogPrint("wc2ai: Data/Rez/ai.bin cannot be opened\n");
		return program;
	}
	const bool whole = static_cast<size_t>(file.read(data.data(), data.size())) == data.size();
	file.close();
	std::string error;
	if (!whole || !program.Parse(data.data(), data.size(), &error)) {
		LogPrint("wc2ai: Data/Rez/ai.bin not used: %s\n", whole ? error.c_str() : "short read");
		return program;
	}
	LogPrint("wc2ai: %d scripts\n", ScriptCount);
	return program;
}

} // namespace wc2ai

static std::array<int, PlayerMax> Wc2AiIds = [] {
	std::array<int, PlayerMax> ids;
	ids.fill(-1);
	return ids;
}();

int Wc2AiIdOf(int player)
{
	return (player >= 0 && player < PlayerMax) ? Wc2AiIds[player] : -1;
}

static std::array<wc2ai::State, PlayerMax> Wc2AiStates;

/// Units already sent on an attack, by unit number, per player: they do not count as free
/// fighters again (the original marks them the same way). Not saved with the game yet.
static std::array<std::set<int>, PlayerMax> Wc2AiCommitted;

void Wc2AiClearIds()
{
	Wc2AiIds.fill(-1);
	Wc2AiStates.fill(wc2ai::State());
	for (std::set<int> &committed : Wc2AiCommitted) {
		committed.clear();
	}
}

/// The player's fighters of one kind (land, sea, air) that have not been sent yet: alive, on the
/// map, able to attack, not buildings and not workers.
static std::vector<CUnit *> FreeFighters(int index, EMovement kind)
{
	const CPlayer &player = Players[index];
	const CUnitType *peasant = UnitTypeByIdentOrNull("unit-peasant");
	const CUnitType *peon = UnitTypeByIdentOrNull("unit-peon");
	std::vector<CUnit *> free;
	for (CUnit *unit : player.GetUnits()) {
		if (!unit || !unit->IsAliveOnMap() || !unit->Type) {
			continue;
		}
		const CUnitType &type = *unit->Type;
		if (type.Building || !type.CanAttack || type.MoveType != kind || &type == peasant
			|| &type == peon) {
			continue;
		}
		if (Wc2AiCommitted[index].count(UnitNumber(*unit))) {
			continue;
		}
		free.push_back(unit);
	}
	return free;
}

/// How many units of the named types a player has.
static int CountOf(const CPlayer &player, std::initializer_list<const char *> idents)
{
	int count = 0;
	for (const char *ident : idents) {
		if (const CUnitType *type = UnitTypeByIdentOrNull(ident)) {
			count += player.UnitTypesCount[type->Slot];
		}
	}
	return count;
}

/// The conditions a script can wait for. 0..3 are answered from the player's own counts; 4..6
/// (enough free fighters, ships, flyers for the next wave) and 7 are not answered yet: a script
/// stops at them, which is where the original's wave launcher would take over.
static bool ConditionHolds(int condition, void *context)
{
	const int index = *static_cast<int *>(context);
	const CPlayer &player = Players[index];
	switch (condition) {
		case 0: return CountOf(player, {"unit-human-shipyard", "unit-orc-shipyard"}) > 0;
		case 1: return CountOf(player, {"unit-keep", "unit-stronghold"}) > 0;
		case 2: return CountOf(player, {"unit-castle", "unit-fortress"}) > 0;
		case 3: return CountOf(player, {"unit-peasant", "unit-peon"}) >= Wc2AiStates[index].params[0x13];
		case 4: {
			const wc2ai::State &state = Wc2AiStates[index];
			return static_cast<int>(FreeFighters(index, EMovement::Land).size()) >= state.params[0x0D] * state.params[0x0E];
		}
		case 5: {
			const wc2ai::State &state = Wc2AiStates[index];
			return static_cast<int>(FreeFighters(index, EMovement::Naval).size()) >= state.params[0x0F] * state.params[0x10];
		}
		case 6: {
			const wc2ai::State &state = Wc2AiStates[index];
			return static_cast<int>(FreeFighters(index, EMovement::Fly).size()) >= state.params[0x11] * state.params[0x12];
		}
		default: return false;
	}
}

void Wc2AiCycle(int player)
{
	if (player < 0 || player >= PlayerMax || Wc2AiIds[player] < 0) {
		return;
	}
	const wc2ai::Program &program = wc2ai::GameProgram();
	if (!program.Loaded()) {
		return;
	}
	wc2ai::State &state = Wc2AiStates[player];
	if (state.id != Wc2AiIds[player]) {
		if (state.id == -1 && state.executed != 0) {
			return; // the script stopped on a damaged state: stay stopped
		}
		if (!wc2ai::Start(program, Wc2AiIds[player], state)) {
			Wc2AiIds[player] = -1;
			return;
		}
	}
	// The sent marks are unit numbers, and the number of a dead unit is given to a new unit later:
	// keep only the marks of this player's living units, so that a new unit does not start as sent.
	if (GameCycle % 50 == 0 && !Wc2AiCommitted[player].empty()) {
		std::set<int> kept;
		for (const CUnit *unit : Players[player].GetUnits()) {
			if (unit && unit->IsAlive() && Wc2AiCommitted[player].count(UnitNumber(*unit))) {
				kept.insert(UnitNumber(*unit));
			}
		}
		Wc2AiCommitted[player].swap(kept);
	}

	int index = player;
	wc2ai::Step(program, state, ConditionHolds, &index);

	// The wave launcher, every 50 cycles: a set "attack by land / sea / air now" sends `count`
	// groups of up to `size` free fighters of that kind at a player that is not a computer, then the
	// trigger is cleared. Where the original aims a group and how it travels is not established:
	// the group is ordered to attack towards the target's start position with the engine's own orders.
	if (GameCycle % 50 == 0 && (state.params[0x09] | state.params[0x0A] | state.params[0x0B]) != 0) {
		const CPlayer &self = Players[player];
		const CPlayer *target = nullptr;
		for (int i = 0; i < PlayerMax; ++i) {
			const CPlayer &other = Players[i];
			if (other.Type == PlayerTypes::PlayerPerson && self.IsEnemy(other) && other.GetUnitCount() > 0
				&& (!target || other.GetUnitCount() > target->GetUnitCount())) {
				target = &other;
			}
		}
		static const struct { int trigger, size, count; EMovement kind; } waves[] = {
			{0x09, 0x0D, 0x0E, EMovement::Land},
			{0x0A, 0x0F, 0x10, EMovement::Naval},
			{0x0B, 0x11, 0x12, EMovement::Fly},
		};
		for (const auto &wave : waves) {
			if (state.params[wave.trigger] == 0) {
				continue;
			}
			if (target) {
				std::vector<CUnit *> free = FreeFighters(player, wave.kind);
				size_t next = 0;
				for (int group = 0; group < state.params[wave.count]; ++group) {
					for (int n = 0; n < state.params[wave.size] && next < free.size(); ++n, ++next) {
						CUnit &unit = *free[next];
						Wc2AiCommitted[player].insert(UnitNumber(unit));
						CommandAttack(unit, target->StartPos, nullptr, EFlushMode::On);
					}
				}
			}
			state.params[wave.trigger] = 0;
		}
	}
}

/// The building types of a build list, as the file numbers them: 0x3A + 2 * index, the human type
/// even and the orc type odd.
static const char *const BuildingIdents[][2] = {
	{"unit-farm", "unit-pig-farm"},                        // 0x3A
	{"unit-human-barracks", "unit-orc-barracks"},          // 0x3C
	{"unit-church", "unit-altar-of-storms"},               // 0x3E
	{"unit-human-watch-tower", "unit-orc-watch-tower"},    // 0x40
	{"unit-stables", "unit-ogre-mound"},                   // 0x42
	{"unit-inventor", "unit-alchemist"},                   // 0x44
	{"unit-gryphon-aviary", "unit-dragon-roost"},          // 0x46
	{"unit-human-shipyard", "unit-orc-shipyard"},          // 0x48
	{"unit-town-hall", "unit-great-hall"},                 // 0x4A
	{"unit-elven-lumber-mill", "unit-troll-lumber-mill"},  // 0x4C
	{"unit-human-foundry", "unit-orc-foundry"},            // 0x4E
	{"unit-mage-tower", "unit-temple-of-the-damned"},      // 0x50
	{"unit-human-blacksmith", "unit-orc-blacksmith"},      // 0x52
	{"unit-human-refinery", "unit-orc-refinery"},          // 0x54
	{"unit-human-oil-platform", "unit-orc-oil-platform"},  // 0x56
};

/// The research entries of a build list, 0x80 + index, human / orc; 0x98 and 0x99 are the hall's
/// two upgrades and are handled as buildings to upgrade to.
static const char *const ResearchIdents[][2] = {
	{"upgrade-arrow1", "upgrade-throwing-axe1"},                         // 0x80
	{"upgrade-arrow2", "upgrade-throwing-axe2"},                         // 0x81
	{"upgrade-ranger", "upgrade-berserker"},                             // 0x82
	{"upgrade-ranger-marksmanship", "upgrade-berserker-regeneration"},   // 0x83
	{"upgrade-longbow", "upgrade-light-axes"},                           // 0x84
	{"upgrade-ranger-scouting", "upgrade-berserker-scouting"},           // 0x85
	{"upgrade-sword1", "upgrade-battle-axe1"},                           // 0x86
	{"upgrade-sword2", "upgrade-battle-axe2"},                           // 0x87
	{"upgrade-human-shield1", "upgrade-orc-shield1"},                    // 0x88
	{"upgrade-human-shield2", "upgrade-orc-shield2"},                    // 0x89
	{"upgrade-ballista1", "upgrade-catapult1"},                          // 0x8A
	{"upgrade-ballista2", "upgrade-catapult2"},                          // 0x8B
	{"upgrade-human-ship-cannon1", "upgrade-orc-ship-cannon1"},          // 0x8C
	{"upgrade-human-ship-cannon2", "upgrade-orc-ship-cannon2"},          // 0x8D
	{"upgrade-human-ship-armor1", "upgrade-orc-ship-armor1"},            // 0x8E
	{"upgrade-human-ship-armor2", "upgrade-orc-ship-armor2"},            // 0x8F
	{"upgrade-paladin", "upgrade-ogre-mage"},                            // 0x90
	{"upgrade-healing", "upgrade-bloodlust"},                            // 0x91
	{"upgrade-exorcism", "upgrade-runes"},                               // 0x92
	{"upgrade-flame-shield", "upgrade-raise-dead"},                      // 0x93
	{"upgrade-slow", "upgrade-haste"},                                   // 0x94
	{"upgrade-invisibility", "upgrade-whirlwind"},                       // 0x95
	{"upgrade-polymorph", "upgrade-unholy-armor"},                       // 0x96
	{"upgrade-blizzard", "upgrade-death-and-decay"},                     // 0x97
};

/// Queue a research once (the engine drops it from its checks when it is done).
static void WantResearch(PlayerAi &ai, const char *ident)
{
	CUpgrade *upgrade = nullptr;
	for (const auto &known : AllUpgrades) {
		if (known && known->Ident == ident) {
			upgrade = known.get();
			break;
		}
	}
	if (upgrade && std::find(ai.ResearchRequests.begin(), ai.ResearchRequests.end(), upgrade) == ai.ResearchRequests.end()) {
		ai.ResearchRequests.push_back(upgrade);
	}
}

/// Ask for the upgrade of the hall to another type, or take the request back while it may not
/// start. The original's hall trains workers while fewer than wanted, and otherwise upgrades: to
/// the keep / stronghold with a barracks, to the castle / fortress with stables / ogre mound,
/// blacksmith and lumber mill (local evidence record AI-HALL-001). Those buildings are the
/// types' own dependency rules here, so the rules are asked. The engine's upgrade path of a
/// computer does not ask them (its setting for that is off by default), hence the check here.
static void WantUpgradeTo(PlayerAi &ai, const CPlayer &player, const char *ident, bool workersThere)
{
	CUnitType *type = UnitTypeByIdentOrNull(ident);
	if (!type) {
		return;
	}
	const auto found = std::find(ai.UpgradeToRequests.begin(), ai.UpgradeToRequests.end(), type);
	if (workersThere && CheckDependByType(player, *type)) {
		if (found == ai.UpgradeToRequests.end()) {
			ai.UpgradeToRequests.push_back(type);
		}
	} else if (found != ai.UpgradeToRequests.end()) {
		ai.UpgradeToRequests.erase(found);
	}
}

/// Ask the engine's production for `count` units of a type (the request table is a list of
/// "have this many"; the resource manager trains and builds towards it).
static void Want(PlayerAi &ai, const char *ident, unsigned count)
{
	CUnitType *type = UnitTypeByIdentOrNull(ident);
	if (!type) {
		return;
	}
	for (AiRequestType &request : ai.UnitTypeRequests) {
		if (request.Type == type) {
			request.Count = count;
			return;
		}
	}
	if (count == 0) {
		return;
	}
	AiRequestType request;
	request.Type = type;
	request.Count = count;
	ai.UnitTypeRequests.push_back(request);
}

bool Wc2AiSecond(int player)
{
	if (player < 0 || player >= PlayerMax || Wc2AiIds[player] < 0 || !Players[player].Ai) {
		return false;
	}
	const wc2ai::Program &program = wc2ai::GameProgram();
	const wc2ai::State &state = Wc2AiStates[player];
	const wc2ai::Script *script = program.Get(state.id);
	if (!program.Loaded() || !script) {
		return false;
	}
	PlayerAi &ai = *Players[player].Ai;
	const int orc = (PlayerRaces.Name[Players[player].Race] == "orc") ? 1 : 0;

	// The active part of the build list: entries below 0x80 are buildings, 0x80..0x97 research,
	// 0x98 / 0x99 the hall's upgrades. The engine's own builder finds the worker and the
	// place; where the original places a building is not established.
	const size_t limit = std::min<size_t>(state.params[wc2ai::ParamLast], script->buildList.size());
	// The requests are served in the order they are made, so they are made in the list's order:
	// the first building (the hall), then the workers, then the rest, then the fighters.
	const char *workers = orc ? "unit-peon" : "unit-peasant";
	const CPlayer &owner = Players[player];
	const bool workersThere = CountOf(owner, {workers}) >= state.params[0x13];

	// Farms. No build list of the definition file holds a farm: the original's worker, once its
	// player has a hall, builds one by itself, before it looks at the build list, when
	//     supply + 4 x (farms being built) <= demand + 3   and   supply < 200
	// (local evidence record AI-FARM-001; 4 is what a farm gives there). Asked for here in the
	// engine's terms, first in the request table: as many farms as leave more than 3 food free,
	// the request table itself counting the ones that are being built.
	const char *farmIdent = orc ? "unit-pig-farm" : "unit-farm";
	if (const CUnitType *farm = UnitTypeByIdentOrNull(farmIdent)) {
		const int gives = farm->Stats[player].Variables[SUPPLY_INDEX].Value;
		const bool hasHall = orc ? CountOf(owner, {"unit-great-hall", "unit-stronghold", "unit-fortress"}) > 0
		                         : CountOf(owner, {"unit-town-hall", "unit-keep", "unit-castle"}) > 0;
		int more = 0;
		if (hasHall && gives > 0 && owner.Supply < 200 && owner.Supply <= owner.Demand + 3) {
			more = (owner.Demand + 3 - owner.Supply) / gives + 1;
		}
		Want(ai, farmIdent, static_cast<unsigned>(owner.UnitTypesAiActiveCount[farm->Slot] + more));
	}

	unsigned wanted[std::size(BuildingIdents)] = {};
	bool workersAsked = false;
	for (size_t i = 0; i < limit; ++i) {
		const unsigned entry = script->buildList[i];
		if (entry >= 0x3A && entry < 0x3A + 2 * std::size(BuildingIdents)) {
			const size_t index = (entry - 0x3A) / 2;
			Want(ai, BuildingIdents[index][orc], ++wanted[index]);
			if (!workersAsked) {
				Want(ai, workers, state.params[0x13]);
				workersAsked = true;
			}
		} else if (entry >= 0x80 && entry < 0x80 + std::size(ResearchIdents)) {
			WantResearch(ai, ResearchIdents[entry - 0x80][orc]);
		} else if (entry == 0x98) {
			WantUpgradeTo(ai, owner, orc ? "unit-stronghold" : "unit-keep", workersThere);
		} else if (entry == 0x99) {
			WantUpgradeTo(ai, owner, orc ? "unit-fortress" : "unit-castle", workersThere);
		}
	}
	// Workers and the first fighter class, by the script's wanted numbers. The other classes
	// (archers 1 in 4, ballistas 1 in 8, knights, ships, casters, flyers) follow with their rules.
	Want(ai, workers, state.params[0x13]);
	Want(ai, orc ? "unit-grunt" : "unit-footman", state.params[0x14]);
	// The other land classes, in the order the original's barracks considers them: knights, then
	// archers, then ballistas. KNOWN DIFFERENCE: the original trains archers with a chance of 1 in
	// 4 and ballistas 1 in 8 per decision; the engine's request table has no dice, so here they are
	// simply trained towards the wanted number. A class counts its upgraded form too (rangers with
	// archers, paladins with knights), as the original's class counters do.
	const auto remaining = [&](unsigned wantedCount, const char *upgraded) {
		const unsigned have = static_cast<unsigned>(CountOf(owner, {upgraded}));
		return wantedCount > have ? wantedCount - have : 0u;
	};
	Want(ai, orc ? "unit-ogre" : "unit-knight", remaining(state.params[0x17], orc ? "unit-ogre-mage" : "unit-paladin"));
	Want(ai, orc ? "unit-axethrower" : "unit-archer",
	     remaining(state.params[0x15], orc ? "unit-berserker" : "unit-ranger"));
	Want(ai, orc ? "unit-catapult" : "unit-ballista", state.params[0x16]);
	// Sea, casters and air, by the same wanted numbers (the shipyard's and the other buildings'
	// dice of the original are not reproduced either; see above).
	Want(ai, orc ? "unit-orc-oil-tanker" : "unit-human-oil-tanker", state.params[0x18]);
	// The oil platform is in no build list either: the original's tanker, when its player has no
	// platform to use, builds one on the oil patch nearest to its depot (local evidence record
	// AI-OIL-001). Here: one platform is wanted as soon as the player has a tanker.
	Want(ai, orc ? "unit-orc-oil-platform" : "unit-human-oil-platform",
	     CountOf(owner, {orc ? "unit-orc-oil-tanker" : "unit-human-oil-tanker"}) > 0 ? 1 : 0);
	Want(ai, orc ? "unit-orc-destroyer" : "unit-human-destroyer", state.params[0x19]);
	Want(ai, orc ? "unit-orc-transport" : "unit-human-transport", state.params[0x1A]);
	Want(ai, orc ? "unit-ogre-juggernaught" : "unit-battleship", state.params[0x1B]);
	Want(ai, orc ? "unit-orc-submarine" : "unit-human-submarine", state.params[0x1C]);
	Want(ai, orc ? "unit-death-knight" : "unit-mage", state.params[0x1D]);
	Want(ai, orc ? "unit-zeppelin" : "unit-balloon", state.params[0x1E]);
	Want(ai, orc ? "unit-goblin-sappers" : "unit-dwarves", state.params[0x1F]);
	Want(ai, orc ? "unit-dragon" : "unit-gryphon-rider", state.params[0x20]);
	return true;
}

std::string Wc2AiSaveText()
{
	std::string text;
	for (int i = 0; i < PlayerMax; ++i) {
		const wc2ai::State &state = Wc2AiStates[i];
		if (Wc2AiIds[i] < 0) {
			continue;
		}
		text += "if SetWc2AiState then SetWc2AiState(" + std::to_string(i) + ", " + std::to_string(Wc2AiIds[i])
		        + ", " + std::to_string(state.id) + ", " + std::to_string(state.ip) + ", "
		        + std::to_string(state.wait) + ", " + std::to_string(state.executed) + ", {";
		for (int p = 0; p <= wc2ai::ParamLast; ++p) {
			text += std::to_string(state.params[p]) + (p < wc2ai::ParamLast ? ", " : "");
		}
		text += "}) end\n";
		// The units already sent on an attack, so that a loaded game does not send them again.
		if (!Wc2AiCommitted[i].empty()) {
			text += "if SetWc2AiSent then SetWc2AiSent(" + std::to_string(i) + ", {";
			for (const int number : Wc2AiCommitted[i]) {
				text += std::to_string(number) + ", ";
			}
			text += "}) end\n";
		}
	}
	return text;
}

/// Lua: SetWc2AiState(player, mapId, scriptId, ip, wait, executed, {params}) -- from a saved game.
static int CclSetWc2AiState(lua_State *l)
{
	LuaCheckArgs(l, 7);
	const int player = LuaToNumber(l, 1);
	if (player < 0 || player >= PlayerMax || !lua_istable(l, 7)) {
		return 0;
	}
	Wc2AiIds[player] = LuaToNumber(l, 2);
	wc2ai::State &state = Wc2AiStates[player];
	state = wc2ai::State();
	state.id = LuaToNumber(l, 3);
	state.ip = static_cast<uint16_t>(LuaToNumber(l, 4));
	state.wait = static_cast<uint32_t>(LuaToNumber(l, 5));
	state.executed = static_cast<uint32_t>(LuaToNumber(l, 6));
	for (int p = 0; p <= wc2ai::ParamLast; ++p) {
		lua_rawgeti(l, 7, p + 1);
		state.params[p] = static_cast<uint8_t>(lua_isnumber(l, -1) ? lua_tonumber(l, -1) : 0);
		lua_pop(l, 1);
	}
	return 0;
}

/// Lua: SetWc2AiSent(player, {unit numbers}) -- from a saved game.
static int CclSetWc2AiSent(lua_State *l)
{
	LuaCheckArgs(l, 2);
	const int player = LuaToNumber(l, 1);
	if (player < 0 || player >= PlayerMax || !lua_istable(l, 2)) {
		return 0;
	}
	Wc2AiCommitted[player].clear();
	const int count = static_cast<int>(lua_rawlen(l, 2));
	for (int i = 1; i <= count; ++i) {
		lua_rawgeti(l, 2, i);
		if (lua_isnumber(l, -1)) {
			Wc2AiCommitted[player].insert(static_cast<int>(lua_tonumber(l, -1)));
		}
		lua_pop(l, 1);
	}
	return 0;
}

/// Lua: GetWc2AiSentCount(player) -> how many units are marked as sent.
static int CclGetWc2AiSentCount(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int player = LuaToNumber(l, 1);
	lua_pushnumber(l, (player >= 0 && player < PlayerMax) ? static_cast<int>(Wc2AiCommitted[player].size()) : 0);
	return 1;
}

/// Lua: GetWc2AiState(player) -> scriptId, ip, wait, executed, {params by number}
static int CclGetWc2AiState(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int player = LuaToNumber(l, 1);
	static const wc2ai::State none;
	const wc2ai::State &state = (player >= 0 && player < PlayerMax) ? Wc2AiStates[player] : none;
	lua_pushnumber(l, state.id);
	lua_pushnumber(l, state.ip);
	lua_pushnumber(l, state.wait);
	lua_pushnumber(l, state.executed);
	lua_newtable(l);
	for (int p = wc2ai::ParamFirst; p <= wc2ai::ParamLast; ++p) {
		lua_pushnumber(l, state.params[p]);
		lua_rawseti(l, -2, p);
	}
	return 5;
}

/// Lua: GetWc2AiQueue(player) -> text, one line per entry of the engine's production queue of the
/// player ("<type> <made>/<want> wait <cycle>"), then its upgrade and research requests and its
/// supply / demand. For checks through the debug bridge.
static int CclGetWc2AiQueue(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int player = LuaToNumber(l, 1);
	std::string text;
	if (player >= 0 && player < PlayerMax && Players[player].Ai) {
		const PlayerAi &ai = *Players[player].Ai;
		for (const AiBuildQueue &queue : ai.UnitTypeBuilt) {
			text += queue.Type->Ident + " " + std::to_string(queue.Made) + "/" + std::to_string(queue.Want)
			        + " wait " + std::to_string(queue.Wait) + "\n";
		}
		for (const CUnitType *type : ai.UpgradeToRequests) {
			text += "upgrade-to " + type->Ident + "\n";
		}
		for (const CUpgrade *upgrade : ai.ResearchRequests) {
			text += "research " + upgrade->Ident + "\n";
		}
		text += "supply " + std::to_string(Players[player].Supply) + " demand "
		        + std::to_string(Players[player].Demand) + " cycle " + std::to_string(GameCycle) + "\n";
	}
	lua_pushstring(l, text.c_str());
	return 1;
}

/// Lua: GetWc2AiWorkers(player) -> text, for checks through the debug bridge. Read-only.
///   "stock gold G wood W oil O"
///   "upgrade-to <type> cost g/w/o missing <names|none> depend <0|1>" per upgrade request
///   "hall <number> <type> <x>,<y> action <n> orders <k> idle <0|1>" per building that takes resources in
///   "gold-from <hall number>: mine <number> <x>,<y> stock <n>" or "gold-from <hall number>: none"
///       -- what the worker assignment finds when it looks for gold from that hall
///   "worker <number> <x>,<y> action <n> orders <k> res <id> held <n> state <-|new|started|waiting|returning> in <type|->"
///   "workers N idle I gold A wood B other C carrying K"
/// `action` is the engine's order number (1 still, 5 move, 10 train, 11 upgrade-to, 13 being built,
/// 17 build, 19 repair, 20 gather).
static int CclGetWc2AiWorkers(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int index = LuaToNumber(l, 1);
	std::string text;
	if (index < 0 || index >= PlayerMax) {
		lua_pushstring(l, text.c_str());
		return 1;
	}
	const CPlayer &player = Players[index];
	const auto stock = [&](int resource) { return player.Resources[resource] + player.StoredResources[resource]; };
	text += "stock gold " + std::to_string(stock(GoldCost)) + " wood " + std::to_string(stock(WoodCost))
	        + " oil " + std::to_string(stock(OilCost)) + "\n";
	if (player.Ai) {
		for (const CUnitType *type : player.Ai->UpgradeToRequests) {
			const int *costs = type->Stats[index].Costs;
			std::string missing;
			for (int c = GoldCost; c <= OilCost; ++c) {
				if (stock(c) < costs[c]) {
					missing += (missing.empty() ? "" : ",") + DefaultResourceNames[c];
				}
			}
			text += "upgrade-to " + type->Ident + " cost " + std::to_string(costs[GoldCost]) + "/"
			        + std::to_string(costs[WoodCost]) + "/" + std::to_string(costs[OilCost]) + " missing "
			        + (missing.empty() ? "none" : missing) + " depend "
			        + std::to_string(CheckDependByType(player, *type) ? 1 : 0) + "\n";
		}
	}
	const CUnit *anyWorker = nullptr;
	for (const CUnit *unit : player.GetUnits()) {
		if (unit && unit->IsAlive() && unit->Type && unit->Type->BoolFlag[HARVESTER_INDEX].value
			&& unit->Type->ResInfo[GoldCost]) {
			anyWorker = unit;
			break;
		}
	}
	int workers = 0, idle = 0, onGold = 0, onWood = 0, other = 0, carrying = 0;
	std::string workerLines;
	for (const CUnit *unit : player.GetUnits()) {
		if (!unit || !unit->IsAlive() || !unit->Type) {
			continue;
		}
		const CUnitType &type = *unit->Type;
		const std::string place = std::to_string(unit->tilePos.x) + "," + std::to_string(unit->tilePos.y);
		const std::string order = " action " + std::to_string(static_cast<int>(unit->CurrentAction()))
		                          + " orders " + std::to_string(unit->Orders.size());
		if (type.Building && type.CanStore[GoldCost]) {
			text += "hall " + std::to_string(UnitNumber(*unit)) + " " + type.Ident + " " + place + order
			        + " idle " + std::to_string(unit->IsIdle() ? 1 : 0) + "\n";
			if (anyWorker) {
				const CUnit *mine = UnitFindResource(*anyWorker, *unit, 1000, GoldCost, true);
				text += "gold-from " + std::to_string(UnitNumber(*unit)) + ": "
				        + (mine ? "mine " + std::to_string(UnitNumber(*mine)) + " "
				                      + std::to_string(mine->tilePos.x) + "," + std::to_string(mine->tilePos.y)
				                      + " stock " + std::to_string(mine->ResourcesHeld)
				                : std::string("none"))
				        + "\n";
			}
		}
		if (!type.BoolFlag[HARVESTER_INDEX].value) {
			continue;
		}
		++workers;
		std::string state = "-";
		int resource = unit->CurrentResource;
		if (unit->CurrentAction() == UnitAction::Resource) {
			const COrder_Resource &gather = *static_cast<const COrder_Resource *>(unit->CurrentOrder());
			resource = gather.GetCurrentResource();
			state = gather.IsGatheringFinished() ? "returning"
			        : gather.IsGatheringWaiting() ? "waiting" : gather.IsGatheringStarted() ? "started" : "new";
			++(resource == GoldCost ? onGold : resource == WoodCost ? onWood : other);
		} else if (unit->IsIdle()) {
			++idle;
		} else {
			++other;
		}
		if (unit->ResourcesHeld) {
			++carrying;
		}
		workerLines += "worker " + std::to_string(UnitNumber(*unit)) + " " + place + order + " res "
		               + std::to_string(resource) + " held " + std::to_string(unit->ResourcesHeld) + " state "
		               + state + " in " + (unit->Container ? unit->Container->Type->Ident : std::string("-")) + "\n";
	}
	text += workerLines;
	text += "workers " + std::to_string(workers) + " idle " + std::to_string(idle) + " gold "
	        + std::to_string(onGold) + " wood " + std::to_string(onWood) + " other " + std::to_string(other)
	        + " carrying " + std::to_string(carrying) + "\n";
	lua_pushstring(l, text.c_str());
	return 1;
}

/// Lua: SetWc2AiId(player, id) -- the map's AI byte of a player slot.
static int CclSetWc2AiId(lua_State *l)
{
	LuaCheckArgs(l, 2);
	const int player = LuaToNumber(l, 1);
	const int id = LuaToNumber(l, 2);
	if (player >= 0 && player < PlayerMax) {
		Wc2AiIds[player] = (id >= 0 && id <= 255) ? id : -1;
	}
	return 0;
}

/// Lua: GetWc2AiId(player) -> the id, or -1.
static int CclGetWc2AiId(lua_State *l)
{
	LuaCheckArgs(l, 1);
	lua_pushnumber(l, Wc2AiIdOf(LuaToNumber(l, 1)));
	return 1;
}

/// Lua: GetWc2AiInfo([id]) -> scripts loaded (0 when the file is absent); with an id also the
/// script's build-list length and its instruction count (-1 when there is no such script).
static int CclGetWc2AiInfo(lua_State *l)
{
	const wc2ai::Program &program = wc2ai::GameProgram();
	lua_pushnumber(l, program.Loaded() ? wc2ai::ScriptCount : 0);
	if (lua_gettop(l) < 2) {
		return 1;
	}
	const int id = LuaToNumber(l, 1);
	const wc2ai::Script *script = program.Get(id);
	lua_pushnumber(l, script ? static_cast<int>(script->buildList.size()) : -1);
	lua_pushnumber(l, script ? program.InstructionCount(id) : -1);
	return 3;
}

void Wc2AiCclRegister()
{
	lua_register(Lua, "SetWc2AiId", CclSetWc2AiId);
	lua_register(Lua, "GetWc2AiId", CclGetWc2AiId);
	lua_register(Lua, "GetWc2AiInfo", CclGetWc2AiInfo);
	lua_register(Lua, "SetWc2AiState", CclSetWc2AiState);
	lua_register(Lua, "GetWc2AiState", CclGetWc2AiState);
	lua_register(Lua, "SetWc2AiSent", CclSetWc2AiSent);
	lua_register(Lua, "GetWc2AiSentCount", CclGetWc2AiSentCount);
	lua_register(Lua, "GetWc2AiQueue", CclGetWc2AiQueue);
	lua_register(Lua, "GetWc2AiWorkers", CclGetWc2AiWorkers);
}
