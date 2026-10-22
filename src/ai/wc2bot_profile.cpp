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
/**@name wc2bot_profile.cpp - Reading the computer-player profiles of a mod (defs/bots.json). */
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

#include "json.h"

#include <algorithm>
#include <cmath>

namespace wc2bot
{

namespace
{

const char *const KindNames[KindCount] = {"worker",  "soldier", "shooter",    "cavalry", "farm",   "barracks",
                                          "lumber-mill", "blacksmith", "stables", "hall", "keep", "castle",
                                          "tower",   "mage-tower", "church", "siege", "caster",
                                          "shipyard", "refinery", "foundry", "oil-platform", "inventor",
                                          "aviary", "tanker", "destroyer", "battleship", "submarine",
                                          "transport", "flying-machine", "gryphon"};
const char *const LineNames[LineCount] = {"weapons",  "armour",          "missile",   "elite",
                                          "healing",  "bloodlust",       "slow",      "haste",
                                          "blizzard", "death-and-decay", "polymorph", "death-coil",
                                          "unholy-armour", "exorcism", "invisibility", "ship-weapons",
                                          "ship-armour"};
const char *const SpellNames[SpellCount] = {"bloodlust", "healing",         "slow",      "haste",
                                            "blizzard",  "death-and-decay", "polymorph", "death-coil",
                                            "unholy-armour", "exorcism", "invisibility"};
const char *const AimNames[static_cast<int>(Aim::Count)] = {"worker", "soldier",   "shooter",  "cavalry", "siege",
                                                            "caster", "sapper",    "army",     "hall",
                                                            "expansion", "barracks", "tower", "building",
                                                            "ship", "transport", "tanker", "oil-platform",
                                                            "shipyard", "foundry", "refinery", "flyer"};

/// Largest number of seconds a profile may name (ten game hours).
constexpr int SecondsLimit = 36000;
/// Most halls a profile may stand at once (the main one and two expansions).
constexpr int MaxHalls = 3;

/// An id: lower-case letters, digits and '-', 3 to 40 characters (the mod id rule).
bool IsId(const std::string &id)
{
	if (id.size() < 3 || id.size() > 40) {
		return false;
	}
	for (const char ch : id) {
		if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) {
			return false;
		}
	}
	return true;
}

/// A whole number member in [low, high]. Absent: `out` keeps its value when `optional`.
bool ReadInt(const content::JsonValue &object, const char *key, int low, int high, bool optional, int &out,
             const std::string &where, std::string &error)
{
	const content::JsonValue *value = object.find(key);
	if (value == nullptr) {
		if (optional) {
			return true;
		}
		error = where + ": '" + key + "' is missing";
		return false;
	}
	if (!value->isNumber() || value->number != std::floor(value->number)) {
		error = where + ": '" + key + "' is not a whole number";
		return false;
	}
	if (value->number < low || value->number > high) {
		error = where + ": '" + key + "' is outside " + std::to_string(low) + " .. " + std::to_string(high);
		return false;
	}
	out = static_cast<int>(value->number);
	return true;
}

bool ReadBool(const content::JsonValue &object, const char *key, bool &out, const std::string &where,
              std::string &error)
{
	const content::JsonValue *value = object.find(key);
	if (value == nullptr) {
		return true;
	}
	if (!value->isBool()) {
		error = where + ": '" + key + "' is not true / false";
		return false;
	}
	out = value->boolean;
	return true;
}

/// A pair [low, high] of whole numbers in [1, limit], low <= high.
bool ReadRange(const content::JsonValue &object, const char *key, int limit, int &low, int &high,
               const std::string &where, std::string &error)
{
	const content::JsonValue *value = object.find(key);
	if (value == nullptr) {
		error = where + ": '" + key + "' is missing";
		return false;
	}
	const content::JsonArray &items = value->items();
	if (!value->isArray() || items.size() != 2 || !items[0].isNumber() || !items[1].isNumber()) {
		error = where + ": '" + key + "' is not a pair of numbers";
		return false;
	}
	const double a = items[0].number;
	const double b = items[1].number;
	if (a != std::floor(a) || b != std::floor(b) || a < 1 || b > limit || a > b) {
		error = where + ": '" + key + "' is not a range 1 .. " + std::to_string(limit);
		return false;
	}
	low = static_cast<int>(a);
	high = static_cast<int>(b);
	return true;
}

const content::JsonValue *Section(const content::JsonValue &object, const char *key, const std::string &where,
                                  std::string &error)
{
	const content::JsonValue *value = object.find(key);
	if (value == nullptr || !value->isObject()) {
		error = where + ": '" + key + "' is missing or not an object";
		return nullptr;
	}
	return value;
}

/// A list of target classes (absent: empty).
bool ReadAims(const content::JsonValue &object, const char *key, std::vector<Aim> &out, const std::string &where,
              std::string &error)
{
	const content::JsonValue *list = object.find(key);
	if (list == nullptr) {
		return true;
	}
	if (!list->isArray()) {
		error = where + ": '" + key + "' is not a list";
		return false;
	}
	for (const content::JsonValue &item : list->items()) {
		Aim aim = Aim::Army;
		if (!item.isString() || !AimFromName(item.string, aim)) {
			error = where + ": '" + key + "' names an unknown class"
			        + (item.isString() ? " ('" + item.string + "')" : std::string());
			return false;
		}
		out.push_back(aim);
	}
	return true;
}

/// A research list: lines in order, the level named for a line of two levels.
bool ReadResearch(const content::JsonValue &list, std::vector<ResearchStep> &out, const std::string &where,
                  std::string &error)
{
	if (!list.isArray()) {
		error = where + ": 'research' is not a list";
		return false;
	}
	for (const content::JsonValue &item : list.items()) {
		const content::JsonValue *line = item.isObject() ? item.find("line") : nullptr;
		ResearchStep step;
		if (line == nullptr || !line->isString() || !LineFromName(line->string, step.line)) {
			error = where + ": a research step names no known line";
			return false;
		}
		if (!ReadInt(item, "level", 1, LineLevels(step.line), true, step.level, where, error)
		    || !ReadInt(item, "at_s", 0, SecondsLimit, true, step.atSecond, where, error)) {
			return false;
		}
		if (LineLevels(step.line) > 1 && item.find("level") == nullptr) {
			error = where + ": the research step '" + line->string + "' has no 'level'";
			return false;
		}
		out.push_back(step);
	}
	return true;
}

/// The "sea" section: shipyards, oil, the fleet, transports and landings (used on a sea map only).
bool ReadSea(const content::JsonValue &sea, Profile &profile, const std::string &where, std::string &error)
{
	if (!sea.isObject()) {
		error = where + ": 'sea' is not an object";
		return false;
	}
	if (!ReadInt(sea, "water_min", 0, 100, true, profile.seaWater, where, error)
	    || !ReadInt(sea, "shipyard_s", 0, SecondsLimit, false, profile.shipyardSecond, where, error)
	    || !ReadInt(sea, "shipyards", 1, 4, false, profile.shipyards, where, error)
	    || !ReadInt(sea, "tankers", 0, 12, false, profile.tankers, where, error)
	    || !ReadInt(sea, "platforms", 0, 6, false, profile.platforms, where, error)
	    || !ReadBool(sea, "refinery", profile.refinery, where, error)
	    || !ReadInt(sea, "foundry_s", 0, SecondsLimit, true, profile.foundrySecond, where, error)
	    || !ReadInt(sea, "transports", 0, 6, true, profile.transports, where, error)) {
		return false;
	}
	if (profile.platforms > 0 && profile.tankers == 0) {
		error = where + ": oil platforms need 'tankers' (a tanker builds them)";
		return false;
	}
	const content::JsonValue *fleet = sea.find("fleet");
	if (fleet == nullptr || !fleet->isObject()) {
		error = where + ": 'sea' has no 'fleet'";
		return false;
	}
	if (!ReadInt(*fleet, "max", 0, 60, false, profile.fleetMax, where, error)
	    || !ReadInt(*fleet, "min", 0, 60, false, profile.fleetMin, where, error)
	    || !ReadInt(*fleet, "gap_s", 1, SecondsLimit, false, profile.fleetGapSeconds, where, error)
	    || !ReadAims(*fleet, "targets", profile.fleetTargets, where, error)) {
		return false;
	}
	const content::JsonValue *mix = Section(*fleet, "mix", where, error);
	if (mix == nullptr) {
		return false;
	}
	if (mix->object) {
		for (const auto &[name, weight] : *mix->object) {
			Kind kind;
			if (!KindFromName(name, kind) || !IsWarship(kind)) {
				error = where + ": the fleet mix names an unknown warship '" + name + "'";
				return false;
			}
			(void)weight;
		}
	}
	const char *const names[FleetKinds] = {"destroyer", "battleship", "submarine"};
	const char *const fromKeys[FleetKinds] = {"destroyer_from_s", "battleship_from_s", "submarine_from_s"};
	const char *const maxKeys[FleetKinds] = {"destroyer_max", "battleship_max", "submarine_max"};
	int weights = 0;
	for (int i = 0; i < FleetKinds; ++i) {
		if (!ReadInt(*mix, names[i], 0, 100, true, profile.fleetWeight[i], where, error)
		    || !ReadInt(*fleet, fromKeys[i], 0, SecondsLimit, true, profile.fleetFrom[i], where, error)
		    || !ReadInt(*fleet, maxKeys[i], 0, 60, true, profile.fleetMaxOf[i], where, error)) {
			return false;
		}
		weights += profile.fleetWeight[i];
	}
	if (weights == 0 && profile.fleetMax > 0) {
		error = where + ": the fleet mix is empty";
		return false;
	}
	if (profile.fleetMin > profile.fleetMax) {
		error = where + ": the fleet's 'min' is above its 'max'";
		return false;
	}
	if (const content::JsonValue *research = sea.find("research")) {
		if (!ReadResearch(*research, profile.seaResearch, where, error)) {
			return false;
		}
	}
	if (const content::JsonValue *landing = sea.find("landing")) {
		if (!landing->isObject()) {
			error = where + ": 'landing' is not an object";
			return false;
		}
		if (!ReadRange(*landing, "size", 60, profile.landingMin, profile.landingMax, where, error)
		    || !ReadInt(*landing, "from_s", 0, SecondsLimit, false, profile.landingFromSecond, where, error)
		    || !ReadInt(*landing, "gap_s", 1, SecondsLimit, false, profile.landingGapSeconds, where, error)
		    || !ReadBool(*landing, "sea_clear", profile.landingClear, where, error)
		    || !ReadInt(*landing, "cover", 0, 60, true, profile.landingCover, where, error)) {
			return false;
		}
		if (profile.landingMax > profile.transports * TransportSlots) {
			error = where + ": the landing is larger than its 'transports' carry";
			return false;
		}
	}
	return true;
}

/// The "air" section: flying scouts and gryphon / dragon groups.
bool ReadAir(const content::JsonValue &air, Profile &profile, const std::string &where, std::string &error)
{
	if (!air.isObject()) {
		error = where + ": 'air' is not an object";
		return false;
	}
	if (!ReadInt(air, "from_s", 0, SecondsLimit, true, profile.airFromSecond, where, error)
	    || !ReadInt(air, "scouts", 0, 6, true, profile.airScouts, where, error)
	    || !ReadInt(air, "scout_gap_s", 0, SecondsLimit, true, profile.airScoutGapSeconds, where, error)) {
		return false;
	}
	if (const content::JsonValue *riders = air.find("riders")) {
		if (!riders->isObject()) {
			error = where + ": 'riders' is not an object";
			return false;
		}
		if (!ReadInt(*riders, "group", 1, 30, false, profile.riderGroup, where, error)
		    || !ReadInt(*riders, "max", 1, 30, false, profile.riderMax, where, error)
		    || !ReadAims(*riders, "targets", profile.airTargets, where, error)) {
			return false;
		}
		if (profile.riderMax < profile.riderGroup) {
			error = where + ": the riders' 'max' is below their 'group'";
			return false;
		}
	}
	return true;
}

} // namespace

const char *KindName(Kind kind)
{
	const int index = KindIndex(kind);
	return index >= 0 && index < KindCount ? KindNames[index] : "";
}

bool KindFromName(const std::string &name, Kind &kind)
{
	for (int i = 0; i < KindCount; ++i) {
		if (name == KindNames[i]) {
			kind = static_cast<Kind>(i);
			return true;
		}
	}
	return false;
}

int LineLevels(Line line)
{
	return line == Line::Weapons || line == Line::Armour || line == Line::Missile || line == Line::ShipWeapons
	               || line == Line::ShipArmour
	           ? 2
	           : 1;
}

const char *SpellName(Spell spell)
{
	const int index = static_cast<int>(spell);
	return index >= 0 && index < SpellCount ? SpellNames[index] : "";
}

bool SpellFromName(const std::string &name, Spell &spell)
{
	for (int i = 0; i < SpellCount; ++i) {
		if (name == SpellNames[i]) {
			spell = static_cast<Spell>(i);
			return true;
		}
	}
	return false;
}

bool AimFromName(const std::string &name, Aim &aim)
{
	for (int i = 0; i < static_cast<int>(Aim::Count); ++i) {
		if (name == AimNames[i]) {
			aim = static_cast<Aim>(i);
			return true;
		}
	}
	return false;
}

const char *LineName(Line line)
{
	const int index = static_cast<int>(line);
	return index >= 0 && index < LineCount ? LineNames[index] : "";
}

bool LineFromName(const std::string &name, Line &line)
{
	for (int i = 0; i < LineCount; ++i) {
		if (name == LineNames[i]) {
			line = static_cast<Line>(i);
			return true;
		}
	}
	return false;
}

bool ParseProfile(const content::JsonValue &value, Profile &profile, std::string &error)
{
	profile = Profile();
	if (!value.isObject()) {
		error = "a bot is not an object";
		return false;
	}
	const content::JsonValue *id = value.find("id");
	if (id == nullptr || !id->isString() || !IsId(id->string)) {
		error = "a bot has no valid 'id'";
		return false;
	}
	profile.id = id->string;
	const std::string where = "bot '" + profile.id + "'";
	const content::JsonValue *nameKey = value.find("name_key");
	if (nameKey == nullptr || !nameKey->isString() || nameKey->string.empty()) {
		error = where + ": 'name_key' is missing";
		return false;
	}
	profile.nameKey = nameKey->string;
	const content::JsonValue *status = value.find("status");
	if (status == nullptr || !status->isString()
	    || (status->string != "playable" && status->string != "placeholder")) {
		error = where + ": 'status' is not \"playable\" or \"placeholder\"";
		return false;
	}
	profile.playable = status->string == "playable";
	if (!profile.playable) {
		return true; // listed only: nothing more is read or used
	}

	// Fairness: checked against the ceilings, never trusted.
	const content::JsonValue *fair = Section(value, "fairness", where, error);
	if (fair == nullptr
	    || !ReadInt(*fair, "orders_per_minute", 1, MaxOrdersPerMinute, false, profile.ordersPerMinute, where, error)
	    || !ReadInt(*fair, "reaction_s", MinReactionSeconds, 600, false, profile.reactionSeconds, where, error)
	    || !ReadBool(*fair, "single_attention", profile.singleAttention, where, error)) {
		return false;
	}
	int bonus = -1;
	if (!ReadInt(*fair, "resource_bonus", 0, 1000, false, bonus, where, error)) {
		return false;
	}
	if (bonus != 0) {
		error = where + ": 'resource_bonus' must be 0 (a bot gets no resources a person does not)";
		return false;
	}
	const content::JsonValue *vision = fair->find("vision");
	if (vision == nullptr || !vision->isString() || vision->string != FairVision) {
		error = where + ": 'vision' must be \"" + FairVision + "\"";
		return false;
	}

	const content::JsonValue *economy = Section(value, "economy", where, error);
	if (economy == nullptr || !ReadInt(*economy, "workers", 1, 200, false, profile.workers, where, error)
	    || !ReadInt(*economy, "lumber_workers", 0, 200, false, profile.lumberWorkers, where, error)
	    || !ReadInt(*economy, "worker_gap_builds", 1, 20, false, profile.workerGapBuilds, where, error)
	    || !ReadInt(*economy, "supply_slack", 0, 40, false, profile.supplySlack, where, error)
	    || !ReadInt(*economy, "halls", 1, MaxHalls, false, profile.halls, where, error)
	    || !ReadBool(*economy, "expand", profile.expand, where, error)
	    || !ReadInt(*economy, "barracks", 1, 10, true, profile.barracksMax, where, error)
	    || !ReadInt(*economy, "spend_gold", 0, 1000000, true, profile.spendGold, where, error)) {
		return false;
	}
	profile.workersAfterKeep = profile.workers;
	if (!ReadInt(*economy, "workers_after_keep", 1, 200, true, profile.workersAfterKeep, where, error)
	    || !ReadBool(*economy, "workers_per_hall", profile.workersPerHall, where, error)
	    || !ReadInt(*economy, "lumber_share", 0, 80, true, profile.lumberShare, where, error)
	    || !ReadInt(*economy, "repair", 0, 6, true, profile.repairWorkers, where, error)
	    || !ReadBool(*economy, "repair_under_attack", profile.repairUnderAttack, where, error)) {
		return false;
	}
	if (const content::JsonValue *list = economy->find("expansions")) {
		if (!list->isArray()) {
			error = where + ": 'expansions' is not a list";
			return false;
		}
		for (const content::JsonValue &item : list->items()) {
			ExpansionStep step;
			if (!item.isObject()) {
				error = where + ": an expansion is not an object";
				return false;
			}
			if (!ReadInt(item, "at_s", 0, SecondsLimit, true, step.atSecond, where, error)
			    || !ReadInt(item, "mine_below", 0, 100, true, step.mineBelow, where, error)
			    || !ReadInt(item, "escort", 0, 20, true, step.escort, where, error)) {
				return false;
			}
			if (step.atSecond == 0 && step.mineBelow == 0) {
				error = where + ": an expansion has neither 'at_s' nor 'mine_below'";
				return false;
			}
			profile.expansions.push_back(step);
		}
	}
	// One hall more per expansion; 'expand' says so in words, and the two must agree.
	if (profile.expand != !profile.expansions.empty()) {
		error = where + ": 'expand' needs its 'expansions' (and they need 'expand': true)";
		return false;
	}
	if (static_cast<int>(profile.expansions.size()) + 1 > profile.halls) {
		error = where + ": more 'expansions' than 'halls' allows";
		return false;
	}
	if (const content::JsonValue *first = economy->find("before_extras")) {
		if (!first->isArray()) {
			error = where + ": 'before_extras' is not a list";
			return false;
		}
		for (const content::JsonValue &item : first->items()) {
			Kind kind = Kind::Farm;
			if (!item.isString() || !KindFromName(item.string, kind) || !IsBuilding(kind) || kind == Kind::Hall
			    || kind == Kind::Farm) {
				error = where + ": 'before_extras' names no building it can wait for"
				        + (item.isString() ? " ('" + item.string + "')" : std::string());
				return false;
			}
			if (std::find(profile.beforeExtras.begin(), profile.beforeExtras.end(), kind) == profile.beforeExtras.end()) {
				profile.beforeExtras.push_back(kind);
			}
		}
	}

	const content::JsonValue *opening = value.find("opening");
	if (opening == nullptr || !opening->isArray()) {
		error = where + ": 'opening' is missing or not a list";
		return false;
	}
	for (const content::JsonValue &item : opening->items()) {
		const content::JsonValue *build = item.isObject() ? item.find("build") : nullptr;
		OpeningStep step;
		if (build == nullptr || !build->isString() || !KindFromName(build->string, step.what)
		    || !(IsBuilding(step.what) || IsHallUpgrade(step.what))) {
			error = where + ": an opening step names no known building"
			        + (build != nullptr && build->isString() ? " ('" + build->string + "')" : std::string());
			return false;
		}
		if (!ReadInt(item, "at_s", 0, SecondsLimit, true, step.atSecond, where, error)
		    || !ReadInt(item, "workers", 0, 200, true, step.atWorkers, where, error)
		    || !ReadInt(item, "food", 0, 400, true, step.atFood, where, error)) {
			return false;
		}
		profile.opening.push_back(step);
	}

	const content::JsonValue *army = Section(value, "army", where, error);
	if (army == nullptr || !ReadInt(*army, "max", 0, 200, false, profile.armyMax, where, error)) {
		return false;
	}
	const content::JsonValue *mix = Section(*army, "mix", where, error);
	if (mix == nullptr) {
		return false;
	}
	if (mix->object) {
		for (const auto &[name, weight] : *mix->object) {
			Kind kind;
			if (!KindFromName(name, kind) || !IsFighter(kind)) {
				error = where + ": the army mix names an unknown class '" + name + "'";
				return false;
			}
			(void)weight;
		}
	}
	for (int i = 0; i < FighterKinds; ++i) {
		profile.weight[i] = 0;
		if (!ReadInt(*mix, KindName(FighterKindList[i]), 0, 100, true, profile.weight[i], where, error)) {
			return false;
		}
	}
	// soldier / shooter: in the plan from the start, held to their share only; the others named here
	const char *const fromKeys[FighterKinds] = {nullptr, nullptr, "cavalry_from_s", "siege_from_s", "caster_from_s"};
	const char *const maxKeys[FighterKinds] = {nullptr, nullptr, "cavalry_max", "siege_max", "caster_max"};
	for (int i = 2; i < FighterKinds; ++i) {
		if (!ReadInt(*army, fromKeys[i], 0, SecondsLimit, true, profile.fromSecond[i], where, error)
		    || !ReadInt(*army, maxKeys[i], 0, 200, true, profile.maxOf[i], where, error)) {
			return false;
		}
	}
	if (!ReadBool(*army, "hold_places", profile.holdPlaces, where, error)
	    || !ReadBool(*army, "siege_vs_towers", profile.siegeVsTowers, where, error)
	    || !ReadBool(*army, "counters", profile.counters, where, error)) {
		return false;
	}
	int weights = 0;
	for (const int w : profile.weight) {
		weights += w;
	}
	if (weights == 0 && profile.armyMax > 0) {
		error = where + ": the army mix is empty";
		return false;
	}

	const content::JsonValue *waves = Section(value, "waves", where, error);
	if (waves == nullptr || !ReadInt(*waves, "first_s", 0, SecondsLimit, false, profile.firstWaveSecond, where, error)
	    || !ReadRange(*waves, "first_size", 200, profile.firstWaveMin, profile.firstWaveMax, where, error)
	    || !ReadInt(*waves, "gap_s", 1, SecondsLimit, false, profile.waveGapSeconds, where, error)
	    || !ReadRange(*waves, "size", 200, profile.waveMin, profile.waveMax, where, error)
	    || !ReadInt(*waves, "rally", 0, 64, true, profile.rallyTiles, where, error)
	    || !ReadInt(*waves, "gather_s", 0, 600, true, profile.gatherSeconds, where, error)
	    || !ReadInt(*waves, "near_tiles", 0, 256, true, profile.nearTiles, where, error)
	    || !ReadInt(*waves, "near_first_s", 0, SecondsLimit, true, profile.nearFirstSecond, where, error)
	    || !ReadBool(*waves, "focus", profile.focusFire, where, error)
	    || !ReadInt(*waves, "pull_hp", 0, 90, true, profile.pullHp, where, error)
	    || !ReadInt(*waves, "retreat_lost", 0, 100, true, profile.retreatLost, where, error)
	    || !ReadInt(*waves, "return_s", 0, SecondsLimit, true, profile.returnSeconds, where, error)
	    || !ReadBool(*waves, "avoid_towers", profile.avoidTowers, where, error)) {
		return false;
	}
	if (!ReadAims(*waves, "targets", profile.targets, where, error)) {
		return false;
	}

	if (const content::JsonValue *raid = value.find("raid")) {
		if (!raid->isObject()) {
			error = where + ": 'raid' is not an object";
			return false;
		}
		if (!ReadInt(*raid, "size", 1, 12, false, profile.raidSize, where, error)
		    || !ReadInt(*raid, "from_s", 0, SecondsLimit, false, profile.raidFromSecond, where, error)
		    || !ReadInt(*raid, "gap_s", 1, SecondsLimit, false, profile.raidGapSeconds, where, error)) {
			return false;
		}
	}
	if (const content::JsonValue *scout = value.find("scout")) {
		if (!scout->isObject()) {
			error = where + ": 'scout' is not an object";
			return false;
		}
		if (!ReadInt(*scout, "worker_s", 0, SecondsLimit, true, profile.scoutWorkerSecond, where, error)
		    || !ReadInt(*scout, "every_s", 0, SecondsLimit, true, profile.scoutGapSeconds, where, error)) {
			return false;
		}
	}
	if (const content::JsonValue *spells = value.find("spells")) {
		const content::JsonValue *cast = spells->isObject() ? spells->find("cast") : nullptr;
		if (cast == nullptr || !cast->isArray()) {
			error = where + ": 'spells' has no 'cast' list";
			return false;
		}
		for (const content::JsonValue &item : cast->items()) {
			Spell spell = Spell::Bloodlust;
			if (!item.isString() || !SpellFromName(item.string, spell)) {
				error = where + ": 'spells' names an unknown spell"
				        + (item.isString() ? " ('" + item.string + "')" : std::string());
				return false;
			}
			if (std::find(profile.casts.begin(), profile.casts.end(), spell) == profile.casts.end()) {
				profile.casts.push_back(spell);
			}
		}
		if (const content::JsonValue *aim = spells->find("aim")) {
			if (!aim->isString() || (aim->string != "nearest" && aim->string != "best")) {
				error = where + ": the spells' 'aim' is not \"nearest\" or \"best\"";
				return false;
			}
			profile.castBest = aim->string == "best";
		}
		if (!ReadInt(*spells, "fight_min", 1, 50, true, profile.fightMin, where, error)) {
			return false;
		}
	}
	if (const content::JsonValue *towers = value.find("towers")) {
		const content::JsonValue *upgrade = towers->isObject() ? towers->find("upgrade") : nullptr;
		if (upgrade == nullptr || !upgrade->isString() || (upgrade->string != "guard" && upgrade->string != "cannon")) {
			error = where + ": the towers' 'upgrade' is not \"guard\" or \"cannon\"";
			return false;
		}
		profile.cannonTowers = upgrade->string == "cannon";
	}

	const content::JsonValue *defence = Section(value, "defence", where, error);
	if (defence == nullptr
	    || !ReadInt(*defence, "delay_s", MinReactionSeconds, 600, false, profile.defenceDelaySeconds, where, error)
	    || !ReadInt(*defence, "radius", 0, 256, false, profile.defenceRadius, where, error)) {
		return false;
	}
	if (profile.defenceDelaySeconds < profile.reactionSeconds) {
		error = where + ": the defence delay is shorter than the reaction time";
		return false;
	}

	if (!ReadInt(*defence, "workers", 0, 12, true, profile.defenceWorkers, where, error)
	    || !ReadInt(*defence, "workers_vs", 0, 12, true, profile.defenceWorkersVs, where, error)) {
		return false;
	}

	// Sea and air: optional; without them the profile plays its land plan on every map.
	if (const content::JsonValue *sea = value.find("sea")) {
		if (!ReadSea(*sea, profile, where, error)) {
			return false;
		}
	}
	if (const content::JsonValue *air = value.find("air")) {
		if (!ReadAir(*air, profile, where, error)) {
			return false;
		}
	}

	const content::JsonValue *research = value.find("research");
	if (research != nullptr && !ReadResearch(*research, profile.research, where, error)) {
		return false;
	}
	return ReadInt(value, "rebuild_delay_s", 0, SecondsLimit, true, profile.rebuildDelaySeconds, where, error);
}

bool ParseProfiles(const content::JsonValue &root, ProfileSet &set, std::string &error)
{
	set = ProfileSet();
	if (!root.isObject()) {
		error = "bots.json is not an object";
		return false;
	}
	const content::JsonValue *format = root.find("format");
	if (format == nullptr || !format->isNumber() || format->number != Format) {
		error = "bots.json: 'format' is not " + std::to_string(Format);
		return false;
	}
	set.format = Format;
	const content::JsonValue *bots = root.find("bots");
	if (bots == nullptr || !bots->isArray() || bots->items().empty()) {
		error = "bots.json: 'bots' is missing or empty";
		return false;
	}
	for (const content::JsonValue &item : bots->items()) {
		Profile profile;
		if (!ParseProfile(item, profile, error)) {
			set = ProfileSet();
			return false;
		}
		if (FindProfile(set, profile.id) != nullptr) {
			error = "bots.json: the id '" + profile.id + "' is used twice";
			set = ProfileSet();
			return false;
		}
		set.bots.push_back(std::move(profile));
	}
	return true;
}

const Profile *FindProfile(const ProfileSet &set, const std::string &id)
{
	for (const Profile &profile : set.bots) {
		if (profile.id == id) {
			return &profile;
		}
	}
	return nullptr;
}

bool SplitSeatName(const std::string &aiName, std::string &mod, std::string &id)
{
	const std::string prefix(SeatPrefix);
	if (aiName.compare(0, prefix.size(), prefix) != 0) {
		return false;
	}
	const std::string rest = aiName.substr(prefix.size());
	const size_t colon = rest.find(':');
	if (colon == std::string::npos) {
		return false;
	}
	const std::string m = rest.substr(0, colon);
	const std::string i = rest.substr(colon + 1);
	if (!IsId(m) || !IsId(i)) {
		return false;
	}
	mod = m;
	id = i;
	return true;
}

std::string SeatName(const std::string &mod, const std::string &id)
{
	return std::string(SeatPrefix) + mod + ":" + id;
}

bool IsSeatName(const std::string &aiName)
{
	std::string mod;
	std::string id;
	return SplitSeatName(aiName, mod, id);
}

bool SavedSeatLoads(const ProfileSet *set, const std::string &mod, const std::string &id,
                    const std::string &savedHash, const std::string &currentHash, std::string &message)
{
	const Profile *profile = set != nullptr ? FindProfile(*set, id) : nullptr;
	if (profile == nullptr || !profile->playable) {
		message = "This saved game needs the mod '" + mod + "' with its computer player '" + id
		          + "'. Enable the mod to load the game.";
		return false;
	}
	if (savedHash != currentHash) {
		message = "This saved game was made with another version of the mod '" + mod
		          + "' (computer player '" + id + "'). It cannot be loaded with the version installed now.";
		return false;
	}
	message.clear();
	return true;
}

} // namespace wc2bot
