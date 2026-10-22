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
/**@name test_wc2bot.cpp - Computer players described by a mod: profiles, fairness, decisions, saves. */
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

#include "json.h"
#include "mods.h"
#include "settings.h"
#include "wc2bot.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

using namespace wc2bot;

namespace
{

/// The three profiles as the mod ships them (the same text as defs/bots.json of the mod source).
const char *const BotsJson = R"({
  "format": 1,
  "bots": [
    {
      "id": "recruit",
      "name_key": "bots.recruit",
      "status": "playable",
      "fairness": {"orders_per_minute": 30, "reaction_s": 1, "single_attention": true,
                   "resource_bonus": 0, "vision": "own-sight+start-positions"},
      "economy": {"workers": 12, "lumber_workers": 2, "lumber_share": 35, "worker_gap_builds": 2, "supply_slack": 0,
                  "halls": 1, "expand": false, "barracks": 2, "spend_gold": 1500,
                  "before_extras": ["lumber-mill", "blacksmith"]},
      "opening": [
        {"build": "hall"}, {"build": "farm"}, {"build": "barracks"}, {"build": "farm"},
        {"build": "lumber-mill", "at_s": 360}, {"build": "blacksmith", "at_s": 540},
        {"build": "keep", "at_s": 900}, {"build": "stables", "at_s": 960}
      ],
      "army": {"max": 10, "mix": {"soldier": 2, "shooter": 1, "cavalry": 1},
               "cavalry_from_s": 900, "cavalry_max": 4},
      "waves": {"first_s": 600, "first_size": [3, 4], "gap_s": 240, "size": [4, 6],
                "rally": 6, "gather_s": 40},
      "defence": {"delay_s": 20, "radius": 10},
      "sea": {"water_min": 30, "shipyard_s": 720, "shipyards": 1, "tankers": 1, "platforms": 1,
              "fleet": {"max": 3, "mix": {"destroyer": 1}, "min": 2, "gap_s": 240}},
      "research": [{"line": "weapons", "level": 1, "at_s": 660}, {"line": "armour", "level": 1, "at_s": 780}],
      "rebuild_delay_s": 180
    },
    {
      "id": "captain",
      "name_key": "bots.captain",
      "status": "playable",
      "fairness": {"orders_per_minute": 80, "reaction_s": 3, "single_attention": false,
                   "resource_bonus": 0, "vision": "own-sight+start-positions"},
      "economy": {"workers": 20, "workers_after_keep": 24, "lumber_workers": 3, "lumber_share": 35,
                  "worker_gap_builds": 1, "supply_slack": 3,
                  "halls": 2, "expand": true, "expansions": [{"at_s": 900, "mine_below": 40, "escort": 4}],
                  "barracks": 3, "spend_gold": 1500, "repair": 2,
                  "before_extras": ["blacksmith", "lumber-mill"]},
      "opening": [
        {"build": "hall"}, {"build": "farm"}, {"build": "barracks"}, {"build": "farm"},
        {"build": "blacksmith", "at_s": 150}, {"build": "lumber-mill", "at_s": 210},
        {"build": "barracks", "food": 16}, {"build": "keep", "workers": 18},
        {"build": "stables"}, {"build": "tower"},
        {"build": "castle", "at_s": 1080}, {"build": "church"}, {"build": "mage-tower"}
      ],
      "army": {"max": 24, "mix": {"soldier": 2, "shooter": 2, "cavalry": 6, "siege": 1, "caster": 1},
               "cavalry_from_s": 600, "cavalry_max": 14, "siege_from_s": 360, "siege_max": 2,
               "siege_vs_towers": true, "caster_from_s": 1080, "caster_max": 2,
               "hold_places": false, "counters": true},
      "waves": {"first_s": 420, "first_size": [5, 6], "gap_s": 180, "size": [8, 12],
                "rally": 7, "gather_s": 30, "targets": ["army", "worker", "hall", "barracks"],
                "retreat_lost": 60},
      "scout": {"worker_s": 120, "every_s": 300},
      "spells": {"cast": ["bloodlust", "healing", "slow", "death-coil", "blizzard"],
                 "aim": "nearest", "fight_min": 3},
      "defence": {"delay_s": 6, "radius": 40},
      "sea": {"water_min": 30, "shipyard_s": 360, "shipyards": 1, "tankers": 2, "platforms": 1, "refinery": true,
              "foundry_s": 600, "transports": 2,
              "fleet": {"max": 8, "mix": {"destroyer": 1, "battleship": 1}, "destroyer_max": 4,
                        "battleship_from_s": 600, "battleship_max": 4, "min": 3, "gap_s": 180,
                        "targets": ["ship", "transport", "tanker", "oil-platform", "shipyard"]},
              "research": [{"line": "ship-weapons", "level": 1}, {"line": "ship-armour", "level": 1}],
              "landing": {"size": [6, 8], "from_s": 900, "gap_s": 300, "sea_clear": true, "cover": 2}},
      "air": {"from_s": 600, "scouts": 1, "scout_gap_s": 300},
      "research": [
        {"line": "weapons", "level": 1, "at_s": 300}, {"line": "armour", "level": 1, "at_s": 420},
        {"line": "missile", "level": 1, "at_s": 480}, {"line": "weapons", "level": 2, "at_s": 720},
        {"line": "armour", "level": 2, "at_s": 840}, {"line": "missile", "level": 2, "at_s": 900},
        {"line": "elite", "at_s": 1140}, {"line": "healing"}, {"line": "bloodlust"},
        {"line": "slow"}, {"line": "death-coil"}, {"line": "blizzard"}
      ],
      "rebuild_delay_s": 60
    },
    {
      "id": "warlord",
      "name_key": "bots.warlord",
      "status": "playable",
      "fairness": {"orders_per_minute": 180, "reaction_s": 1, "single_attention": false,
                   "resource_bonus": 0, "vision": "own-sight+start-positions"},
      "economy": {"workers": 22, "workers_after_keep": 32, "workers_per_hall": true, "lumber_workers": 4,
                  "lumber_share": 40, "worker_gap_builds": 1, "supply_slack": 5,
                  "halls": 3, "expand": true,
                  "expansions": [{"at_s": 540, "mine_below": 60, "escort": 4}, {"at_s": 1200, "mine_below": 30, "escort": 4}],
                  "barracks": 5, "spend_gold": 1000, "repair": 3, "repair_under_attack": true,
                  "before_extras": ["blacksmith", "lumber-mill"]},
      "opening": [
        {"build": "hall"}, {"build": "farm"}, {"build": "barracks"}, {"build": "farm"},
        {"build": "barracks"}, {"build": "blacksmith", "at_s": 100}, {"build": "lumber-mill", "at_s": 150},
        {"build": "keep", "workers": 18},
        {"build": "stables"}, {"build": "tower"}, {"build": "castle", "at_s": 840},
        {"build": "church"}, {"build": "mage-tower"}, {"build": "tower"}
      ],
      "army": {"max": 36, "mix": {"soldier": 2, "shooter": 2, "cavalry": 6, "siege": 1, "caster": 2},
               "cavalry_from_s": 480, "cavalry_max": 18, "siege_from_s": 300, "siege_max": 3, "siege_vs_towers": true,
               "caster_from_s": 840, "caster_max": 4, "hold_places": false, "counters": true},
      "waves": {"first_s": 330, "near_tiles": 48, "near_first_s": 300, "first_size": [4, 5], "gap_s": 150,
                "size": [10, 16], "rally": 7, "gather_s": 25,
                "targets": ["caster", "sapper", "siege", "worker", "army", "expansion", "tower", "hall", "barracks"],
                "focus": true, "pull_hp": 30, "retreat_lost": 50, "return_s": 40, "avoid_towers": true},
      "raid": {"size": 3, "from_s": 600, "gap_s": 150},
      "scout": {"worker_s": 90, "every_s": 180},
      "spells": {"cast": ["bloodlust", "haste", "unholy-armour", "healing", "polymorph", "death-coil", "exorcism", "slow",
                          "blizzard", "death-and-decay", "invisibility"],
                 "aim": "best", "fight_min": 2},
      "defence": {"delay_s": 2, "radius": 64, "workers": 4, "workers_vs": 2},
      "sea": {"water_min": 30, "shipyard_s": 0, "shipyards": 2, "tankers": 3, "platforms": 2, "refinery": true,
              "foundry_s": 360, "transports": 2,
              "fleet": {"max": 12, "mix": {"destroyer": 2, "battleship": 2, "submarine": 1}, "destroyer_max": 5,
                        "battleship_from_s": 480, "battleship_max": 5, "submarine_from_s": 600, "submarine_max": 2,
                        "min": 2, "gap_s": 120,
                        "targets": ["oil-platform", "transport", "foundry", "ship", "tanker", "shipyard", "refinery"]},
              "research": [{"line": "ship-weapons", "level": 1}, {"line": "ship-armour", "level": 1},
                           {"line": "ship-weapons", "level": 2}, {"line": "ship-armour", "level": 2}],
              "landing": {"size": [8, 12], "from_s": 600, "gap_s": 240, "sea_clear": false, "cover": 2}},
      "air": {"from_s": 600, "scouts": 2, "scout_gap_s": 0,
              "riders": {"group": 5, "max": 8,
                         "targets": ["flyer", "ship", "caster", "siege", "worker", "army", "hall", "barracks", "building"]}},
      "research": [
        {"line": "weapons", "level": 1, "at_s": 180}, {"line": "armour", "level": 1, "at_s": 270},
        {"line": "missile", "level": 1, "at_s": 330}, {"line": "weapons", "level": 2, "at_s": 540},
        {"line": "armour", "level": 2, "at_s": 600}, {"line": "missile", "level": 2, "at_s": 660},
        {"line": "elite", "at_s": 900}, {"line": "bloodlust"}, {"line": "healing"}, {"line": "haste"},
        {"line": "slow"}, {"line": "death-coil"}, {"line": "polymorph"}, {"line": "blizzard"},
        {"line": "death-and-decay"}, {"line": "unholy-armour"}, {"line": "exorcism"}, {"line": "invisibility"}
      ],
      "rebuild_delay_s": 20
    }
  ]
})";

content::JsonValue Json(const std::string &text)
{
	content::JsonValue value;
	std::string error;
	REQUIRE(content::parseJson(text, value, error));
	return value;
}

Profile Recruit()
{
	ProfileSet set;
	std::string error;
	REQUIRE(ParseProfiles(Json(BotsJson), set, error));
	const Profile *profile = FindProfile(set, "recruit");
	REQUIRE(profile != nullptr);
	return *profile;
}

/// The Recruit's file with one piece of text replaced (the piece must be there).
std::string RecruitWith(const std::string &from, const std::string &to)
{
	std::string text = BotsJson;
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	return text.replace(at, from.size(), to);
}

bool Parses(const std::string &text, std::string &error)
{
	ProfileSet set;
	return ParseProfiles(Json(text), set, error);
}

/// Dice from a fixed seed (a linear congruential generator), so a run can be repeated.
struct SeededDice {
	unsigned long seed = 12345;
};

int Roll(int sides, void *context)
{
	SeededDice &dice = *static_cast<SeededDice *>(context);
	dice.seed = (dice.seed * 1103515245UL + 12345UL) & 0x7fffffffUL;
	return static_cast<int>(dice.seed % static_cast<unsigned long>(sides));
}

/// A start: a hall, five workers, one farm's worth of food, the enemy's start far away.
World StartWorld()
{
	World world;
	world.have[KindIndex(Kind::Hall)] = 1;
	world.have[KindIndex(Kind::Worker)] = 5;
	for (int k = 0; k < KindCount; ++k) {
		world.can[k] = true;
	}
	world.supply = 5;
	world.demand = 5;
	world.home = {10, 10};
	world.enemyStarts = {{60, 60}, {40, 10}};
	return world;
}

/// A toy game: what was released stands there the next second, a new fighter appears at home,
/// a unit sent goes to its goal at once and stands idle there. Enough to drive the decisions.
struct ToyGame {
	World world = StartWorld();
	int nextSlot = 100;

	void Step(const Decision &decision)
	{
		for (int k = 0; k < KindCount; ++k) {
			if (!decision.release[k]) {
				continue;
			}
			if (IsFighter(static_cast<Kind>(k))) {
				Fighter fighter;
				fighter.slot = nextSlot++;
				fighter.at = {11, 11};
				fighter.idle = true;
				fighter.kind = static_cast<Kind>(k);
				world.fighters.push_back(fighter);
			}
			++world.have[k];
			// food: a farm gives four, a hall one; every unit uses one
			if (k == KindIndex(Kind::Farm)) {
				world.supply += 4;
			} else if (k == KindIndex(Kind::Hall)) {
				world.supply += 1;
			} else if (!IsBuilding(static_cast<Kind>(k))) {
				world.demand += 1;
			}
		}
		if (decision.keep) {
			++world.have[KindIndex(Kind::Keep)];
		}
		if (decision.castle) {
			++world.have[KindIndex(Kind::Castle)];
		}
		for (const auto &[line, level] : decision.research) {
			world.level[static_cast<int>(line)] = level;
		}
		for (const Command &command : decision.commands) {
			for (Fighter &fighter : world.fighters) {
				if (fighter.slot == command.slot) {
					fighter.at = command.to;
					fighter.idle = true;
				}
			}
		}
		world.alarm = false;
		++world.second;
	}
};

std::vector<Decision> Run(const Profile &profile, ToyGame &game, State &state, SeededDice &dice, int seconds)
{
	std::vector<Decision> decisions;
	for (int i = 0; i < seconds; ++i) {
		decisions.push_back(Decide(profile, game.world, state, Roll, &dice));
		game.Step(decisions.back());
	}
	return decisions;
}

bool Same(const Decision &a, const Decision &b)
{
	if (a.keep != b.keep || a.orders != b.orders || a.army != b.army || a.lumberWorkers != b.lumberWorkers
	    || a.research != b.research || a.commands.size() != b.commands.size()) {
		return false;
	}
	for (int k = 0; k < KindCount; ++k) {
		if (a.release[k] != b.release[k]) {
			return false;
		}
	}
	for (size_t i = 0; i < a.commands.size(); ++i) {
		if (a.commands[i].slot != b.commands[i].slot || !(a.commands[i].to == b.commands[i].to)) {
			return false;
		}
	}
	return true;
}

/// A mods directory with the bots mod and a mod with a broken profile file; removed at the end.
struct BotModsFixture {
	fs::path root;

	BotModsFixture()
	{
		root = fs::temp_directory_path() / "stratagus_test_wc2bot";
		fs::remove_all(root);
		fs::create_directories(root / "wargushd-bots" / "defs");
		fs::create_directories(root / "broken-bots" / "defs");
		Write(root / "wargushd-bots" / "defs" / "bots.json", BotsJson);
		Write(root / "broken-bots" / "defs" / "bots.json", R"({"format": 1, "bots": [)");
		ModsSetRoot(root);
	}

	~BotModsFixture()
	{
		ModsSetEnabled({});
		ModsSetRoot({});
		std::error_code ec;
		fs::remove_all(root, ec);
	}

	static void Write(const fs::path &path, const std::string &text)
	{
		std::ofstream out(path, std::ios::binary);
		out << text;
	}
};

} // namespace

TEST_CASE("wc2bot: the Recruit profile reads with its numbers; Captain and Warlord are playable")
{
	ProfileSet set;
	std::string error;
	REQUIRE(ParseProfiles(Json(BotsJson), set, error));
	CHECK(set.bots.size() == 3);
	const Profile &recruit = *FindProfile(set, "recruit");
	CHECK(recruit.playable);
	CHECK(recruit.nameKey == "bots.recruit");
	CHECK(recruit.ordersPerMinute == 30);
	CHECK(recruit.workers == 12);
	CHECK(recruit.lumberWorkers == 2);
	CHECK(recruit.halls == 1);
	CHECK_FALSE(recruit.expand);
	REQUIRE(recruit.opening.size() == 8);
	CHECK(recruit.opening[0].what == Kind::Hall);
	CHECK(recruit.opening[4].what == Kind::LumberMill);
	CHECK(recruit.opening[4].atSecond == 360);
	CHECK(recruit.opening[6].what == Kind::Keep);
	CHECK(recruit.weight[0] == 2);
	CHECK(recruit.weight[1] == 1);
	CHECK(recruit.firstWaveSecond == 600);
	CHECK(recruit.firstWaveMin == 3);
	CHECK(recruit.firstWaveMax == 4);
	CHECK(recruit.waveMin == 4);
	CHECK(recruit.waveMax == 6);
	CHECK(recruit.defenceDelaySeconds == 20);
	CHECK(recruit.defenceRadius == 10);
	REQUIRE(recruit.research.size() == 2);
	CHECK(recruit.research[0].line == Line::Weapons);
	CHECK(recruit.rebuildDelaySeconds == 180);
	CHECK(recruit.barracksMax == 2);
	CHECK(recruit.spendGold == 1500);
	CHECK(recruit.rallyTiles == 6);
	CHECK(recruit.gatherSeconds == 40);
	REQUIRE(recruit.beforeExtras.size() == 2);
	CHECK(recruit.beforeExtras[0] == Kind::LumberMill);
	CHECK(recruit.beforeExtras[1] == Kind::Blacksmith);
	CHECK(recruit.workersAfterKeep == 12); // the fields that came with mod 0.2 keep the Recruit as it was
	CHECK(recruit.targets.empty());
	CHECK(recruit.casts.empty());
	CHECK(recruit.raidSize == 0);
	CHECK(recruit.holdPlaces);
	CHECK(FindProfile(set, "captain")->playable);
	CHECK(FindProfile(set, "warlord")->playable);
	CHECK(FindProfile(set, "general") == nullptr);
}

TEST_CASE("wc2bot: a malformed or unfair profile is refused with a reason")
{
	std::string error;
	CHECK_FALSE(Parses(RecruitWith("\"format\": 1", "\"format\": 2"), error));
	CHECK(error.find("format") != std::string::npos);

	CHECK_FALSE(Parses(RecruitWith("\"orders_per_minute\": 30", "\"orders_per_minute\": 181"), error));
	CHECK(error.find("orders_per_minute") != std::string::npos);
	CHECK(Parses(RecruitWith("\"orders_per_minute\": 30", "\"orders_per_minute\": 180"), error));

	CHECK_FALSE(Parses(RecruitWith("\"reaction_s\": 1", "\"reaction_s\": 0"), error));
	CHECK(error.find("reaction_s") != std::string::npos);

	CHECK_FALSE(Parses(RecruitWith("\"resource_bonus\": 0", "\"resource_bonus\": 20"), error));
	CHECK(error.find("resource_bonus") != std::string::npos);

	CHECK_FALSE(Parses(RecruitWith("own-sight+start-positions", "whole-map"), error));
	CHECK(error.find("vision") != std::string::npos);

	CHECK_FALSE(Parses(RecruitWith("{\"build\": \"blacksmith\"", "{\"build\": \"dragon-roost\""), error));
	CHECK(error.find("dragon-roost") != std::string::npos);

	CHECK_FALSE(Parses(RecruitWith("\"cavalry\": 1}", "\"catapult\": 1}"), error));
	CHECK(error.find("catapult") != std::string::npos);

	CHECK_FALSE(Parses(RecruitWith("\"line\": \"weapons\"", "\"line\": \"fireball\""), error));
	CHECK_FALSE(Parses(RecruitWith("\"delay_s\": 20", "\"delay_s\": 0"), error));
	CHECK_FALSE(Parses(RecruitWith("\"workers\": 12", "\"workers\": 12.5"), error));
	CHECK_FALSE(Parses(RecruitWith("\"first_size\": [3, 4]", "\"first_size\": [4, 3]"), error));
	CHECK_FALSE(Parses(RecruitWith("\"expand\": false", "\"expand\": true"), error));
	CHECK_FALSE(Parses(RecruitWith("[\"lumber-mill\", \"blacksmith\"]", "[\"lumber-mill\", \"grunt\"]"), error));
	CHECK(error.find("before_extras") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("[\"lumber-mill\", \"blacksmith\"]", "[\"farm\"]"), error));
	CHECK_FALSE(Parses(RecruitWith("[\"lumber-mill\", \"blacksmith\"]", "\"lumber-mill\""), error));
	CHECK(Parses(RecruitWith("[\"lumber-mill\", \"blacksmith\"]", "[]"), error));
	CHECK_FALSE(Parses(RecruitWith("\"id\": \"captain\"", "\"id\": \"recruit\""), error));
	CHECK(error.find("twice") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"status\": \"playable\"", "\"status\": \"later\""), error));
}

TEST_CASE("wc2bot: seat names")
{
	std::string mod;
	std::string id;
	CHECK(SplitSeatName("mod-bot:wargushd-bots:recruit", mod, id));
	CHECK(mod == "wargushd-bots");
	CHECK(id == "recruit");
	CHECK(SeatName(mod, id) == "mod-bot:wargushd-bots:recruit");
	CHECK(SeatName(mod, id).size() < 64); // fits the seat's AI script field of the network setup
	CHECK_FALSE(IsSeatName("ai-active"));
	CHECK_FALSE(IsSeatName("mod-bot:wargushd-bots"));
	CHECK_FALSE(IsSeatName("mod-bot:Wargushd:recruit"));
	CHECK_FALSE(IsSeatName("mod-bot:../x:recruit"));
	CHECK_FALSE(IsSeatName("mod-bot:wargushd-bots:"));
}

TEST_CASE("wc2bot: the rate cap never lets more orders through in any 60 seconds")
{
	RateCap rate;
	std::vector<int> taken;
	for (long second = 0; second < 600; ++second) {
		int n = 0;
		const int asked = static_cast<int>((second * 7) % 13); // an uneven demand
		for (int i = 0; i < asked; ++i) {
			n += rate.Take(second, 30) ? 1 : 0;
		}
		taken.push_back(n);
	}
	for (size_t end = 0; end < taken.size(); ++end) {
		int sum = 0;
		for (size_t s = end >= 59 ? end - 59 : 0; s <= end; ++s) {
			sum += taken[s];
		}
		CHECK(sum <= 30);
	}
	// After a quiet minute the whole budget is free again.
	RateCap later = rate;
	CHECK(later.UsedInWindow(700) == 0);
	CHECK(later.Take(700, 1));
	CHECK_FALSE(later.Take(700, 1));
}

TEST_CASE("wc2bot: a whole Recruit game in the toy world stays within 30 orders a minute")
{
	const Profile recruit = Recruit();
	ToyGame game;
	State state;
	SeededDice dice;
	const std::vector<Decision> decisions = Run(recruit, game, state, dice, 1800);
	for (size_t end = 0; end < decisions.size(); ++end) {
		int sum = 0;
		for (size_t s = end >= 59 ? end - 59 : 0; s <= end; ++s) {
			sum += decisions[s].orders;
		}
		CHECK(sum <= 30);
	}
	// One attention a second: an army second asks nothing of the economy.
	for (const Decision &decision : decisions) {
		if (decision.army) {
			for (int k = 0; k < KindCount; ++k) {
				CHECK(decision.release[k] == 0);
			}
			CHECK(decision.research.empty());
		}
	}
	// The worker number stops at 12; the army at 10.
	CHECK(game.world.have[KindIndex(Kind::Worker)] == 12);
	CHECK(game.world.have[KindIndex(Kind::Soldier)] + game.world.have[KindIndex(Kind::Shooter)]
	          + game.world.have[KindIndex(Kind::Cavalry)]
	      <= 10);
}

TEST_CASE("wc2bot: the same state, view and dice give the same decisions")
{
	const Profile recruit = Recruit();
	ToyGame gameA;
	ToyGame gameB;
	State stateA;
	State stateB;
	SeededDice diceA;
	SeededDice diceB;
	const std::vector<Decision> a = Run(recruit, gameA, stateA, diceA, 1500);
	const std::vector<Decision> b = Run(recruit, gameB, stateB, diceB, 1500);
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); ++i) {
		CHECK(Same(a[i], b[i]));
	}
	CHECK(stateA == stateB);
	CHECK(StateText(stateA) == StateText(stateB));
}

TEST_CASE("wc2bot: the opening, the worker gap and the first wave keep the Recruit's times")
{
	const Profile recruit = Recruit();
	ToyGame game;
	State state;
	SeededDice dice;
	const std::vector<Decision> decisions = Run(recruit, game, state, dice, 900);

	int firstWorker = -1;
	int secondWorker = -1;
	int firstMill = -1;
	int firstWave = -1;
	size_t firstWaveSize = 0;
	for (size_t s = 0; s < decisions.size(); ++s) {
		const Decision &d = decisions[s];
		if (d.release[KindIndex(Kind::Worker)]) {
			if (firstWorker < 0) {
				firstWorker = static_cast<int>(s);
			} else if (secondWorker < 0) {
				secondWorker = static_cast<int>(s);
			}
		}
		if (d.release[KindIndex(Kind::LumberMill)] && firstMill < 0) {
			firstMill = static_cast<int>(s);
		}
		if (firstWave < 0 && !d.commands.empty()) {
			firstWave = static_cast<int>(s);
			firstWaveSize = d.commands.size();
		}
	}
	REQUIRE(firstWorker >= 0);
	REQUIRE(secondWorker > firstWorker);
	CHECK(secondWorker - firstWorker >= 2 * 15); // two worker build times of the toy world
	CHECK(firstMill >= 360);
	REQUIRE(firstWave >= 0);
	CHECK(firstWave >= 600);
	CHECK(firstWave < 640);
	CHECK(firstWaveSize >= 3);
	CHECK(firstWaveSize <= 4);
	// No hall upgrade before 15 minutes.
	for (const Decision &d : decisions) {
		CHECK_FALSE(d.keep);
	}
}

TEST_CASE("wc2bot: a wave goes only where the bot has seen a building or to a start position")
{
	const Profile recruit = Recruit();
	State state;
	state.nextWaveAt = 0;
	World world = StartWorld();
	world.second = 600;
	for (int i = 0; i < 4; ++i) {
		Fighter fighter;
		fighter.slot = 10 + i;
		fighter.at = {12, 12};
		fighter.idle = true;
		world.fighters.push_back(fighter);
	}
	SeededDice dice;
	const Point nearStart{40, 10};
	const Point farStart{60, 60};
	const Point farm{35, 12};
	const Point rally{16, 10}; // 6 tiles from home toward the target
	// Nothing seen: the enemy start position nearest to home; the wave gathers first.
	Decision d = Decide(recruit, world, state, Roll, &dice);
	REQUIRE(d.commands.size() >= 3);
	CHECK(d.commands.size() <= 4);
	for (const Command &c : d.commands) {
		CHECK(c.to == rally);
	}
	CHECK(state.target == nearStart);
	// Gathered: it goes together.
	world.second = 601;
	for (Fighter &f : world.fighters) {
		f.at = rally;
	}
	d = Decide(recruit, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == state.wave.size());
	for (const Command &c : d.commands) {
		CHECK(c.to == nearStart);
	}
	CHECK(state.gatherUntil == -1);

	// Its units see an enemy farm on the way: the wave goes there.
	world.second = 605; // idle wave units are sent on every fifth second
	for (Fighter &f : world.fighters) {
		f.at = {30, 10};
		f.idle = f.slot == 10; // one has stopped (it fought on the way), the others walk on
	}
	world.seen.push_back({500, {35, 12}});
	d = Decide(recruit, world, state, Roll, &dice);
	CHECK(state.target == farm);
	REQUIRE_FALSE(d.commands.empty());
	for (const Command &c : d.commands) {
		CHECK(c.to == farm);
	}

	// The farm is seen gone: back to the start position.
	world.second = 606;
	world.seen.clear();
	world.gone.push_back(500);
	d = Decide(recruit, world, state, Roll, &dice);
	CHECK(state.known.empty());
	CHECK(state.target == nearStart);

	// A start position reached with nothing there is struck off: the other one is next.
	world.second = 607;
	world.gone.clear();
	for (Fighter &f : world.fighters) {
		f.at = {40, 11};
	}
	d = Decide(recruit, world, state, Roll, &dice);
	CHECK(state.target == farStart);
}

TEST_CASE("wc2bot: the defence answers after the delay, with the units near the attacked place only")
{
	const Profile recruit = Recruit();
	State state;
	state.nextWaveAt = 100000; // no wave in this test
	World world = StartWorld();
	// Nothing to build: in this static world the economy would ask again every second and fill
	// the rate window before the defence is due.
	for (int k = 0; k < KindCount; ++k) {
		world.can[k] = false;
	}
	world.second = 100;
	Fighter closeBy;
	closeBy.slot = 1;
	closeBy.at = {14, 14};
	closeBy.idle = true;
	Fighter farAway;
	farAway.slot = 2;
	farAway.at = {40, 40};
	farAway.idle = true;
	world.fighters = {closeBy, farAway};
	world.alarm = true;
	world.alarmAt = {10, 10};
	SeededDice dice;
	Decision d = Decide(recruit, world, state, Roll, &dice);
	CHECK(d.commands.empty());
	world.alarm = false;
	for (long s = 101; s < 120; ++s) {
		world.second = s;
		d = Decide(recruit, world, state, Roll, &dice);
		CHECK(d.commands.empty());
	}
	world.second = 120;
	d = Decide(recruit, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 1);
	CHECK(d.commands[0].slot == 1);
	CHECK(d.commands[0].to == world.alarmAt);
	CHECK(d.army);
}

TEST_CASE("wc2bot: a lost building is asked for again only after the rebuild delay")
{
	const Profile recruit = Recruit();
	ToyGame game;
	State state;
	SeededDice dice;
	Run(recruit, game, state, dice, 30); // hall, farm, barracks, farm asked and standing
	REQUIRE(game.world.have[KindIndex(Kind::Barracks)] == 1);
	game.world.have[KindIndex(Kind::Barracks)] = 0; // destroyed
	const long lostAt = game.world.second;
	bool asked = false;
	for (int i = 0; i < 200 && !asked; ++i) {
		const Decision d = Decide(recruit, game.world, state, Roll, &dice);
		if (d.release[KindIndex(Kind::Barracks)]) {
			asked = true;
			CHECK(game.world.second >= lostAt + 180);
		}
		game.Step(d);
	}
	CHECK(asked);
}

TEST_CASE("wc2bot: the saved state reads back and the game goes on the same")
{
	const Profile recruit = Recruit();
	ToyGame game;
	State state;
	SeededDice dice;
	Run(recruit, game, state, dice, 700); // past the first wave
	const std::string text = StateText(state);
	CHECK(text.find('"') == std::string::npos);
	CHECK(text.find('\\') == std::string::npos);
	State loaded;
	REQUIRE(StateFromText(text, loaded));
	CHECK(loaded == state);

	ToyGame gameCopy = game;
	SeededDice diceCopy = dice;
	const std::vector<Decision> a = Run(recruit, game, state, dice, 400);
	const std::vector<Decision> b = Run(recruit, gameCopy, loaded, diceCopy, 400);
	for (size_t i = 0; i < a.size(); ++i) {
		CHECK(Same(a[i], b[i]));
	}
	CHECK(state == loaded);

	State bad;
	CHECK_FALSE(StateFromText("", bad));
	CHECK_FALSE(StateFromText("v2 op=1", bad));
	CHECK_FALSE(StateFromText(text + " op=2", bad));                    // a field twice
	CHECK_FALSE(StateFromText(text.substr(0, text.find(" rc=")), bad)); // a field missing
	CHECK_FALSE(StateFromText(text + " zz=1", bad));                    // an unknown field
}

TEST_CASE("wc2bot: a saved game whose bot mod is missing or changed is refused with a message")
{
	ProfileSet set;
	std::string error;
	REQUIRE(ParseProfiles(Json(BotsJson), set, error));
	std::string message;
	CHECK_FALSE(SavedSeatLoads(nullptr, "wargushd-bots", "recruit", "aaa", "", message));
	CHECK(message.find("wargushd-bots") != std::string::npos);
	CHECK(message.find("recruit") != std::string::npos);
	CHECK_FALSE(SavedSeatLoads(&set, "wargushd-bots", "general", "aaa", "aaa", message));
	ProfileSet placeholders;
	REQUIRE(ParseProfiles(Json(R"({"format": 1, "bots": [{"id": "captain", "name_key": "bots.captain", "status": "placeholder"}]})"),
	                      placeholders, error));
	CHECK_FALSE(SavedSeatLoads(&placeholders, "wargushd-bots", "captain", "aaa", "aaa", message)); // placeholder
	CHECK_FALSE(SavedSeatLoads(&set, "wargushd-bots", "recruit", "aaa", "bbb", message));
	CHECK(message.find("another version") != std::string::npos);
	CHECK(SavedSeatLoads(&set, "wargushd-bots", "recruit", "aaa", "aaa", message));
	CHECK(message.empty());
}

TEST_CASE("wc2bot: profiles are read only from an enabled mod; with no bot seat nothing runs")
{
	BotModsFixture fixture;
	ProfileSet set;
	std::string hash;
	std::string error;
	CHECK_FALSE(Wc2BotReadProfiles("wargushd-bots", set, hash, error)); // not enabled
	CHECK(error.find("wargushd-bots") != std::string::npos);

	ModsSetEnabled({"wargushd-bots", "broken-bots"});
	REQUIRE(Wc2BotReadProfiles("wargushd-bots", set, hash, error));
	CHECK(set.bots.size() == 3);
	CHECK(hash.size() == 64);
	std::string again;
	ProfileSet set2;
	REQUIRE(Wc2BotReadProfiles("wargushd-bots", set2, again, error));
	CHECK(again == hash);
	CHECK_FALSE(Wc2BotReadProfiles("broken-bots", set, hash, error));
	CHECK_FALSE(Wc2BotReadProfiles("missing-bots", set, hash, error));

	// Zero effect while no seat is a bot.
	Wc2BotClear();
	for (int p = 0; p < PlayerMax; ++p) {
		CHECK_FALSE(Wc2BotRuns(p));
		CHECK(Wc2BotTake(p));
		CHECK(Wc2BotMayTake(p));
		CHECK(Wc2BotFarmAllowed(p));
		CHECK(Wc2BotLumberWanted(p) == 0);
	}
	CHECK(Wc2BotSaveText().empty());
	CHECK(Wc2BotRefusal().empty());
}

namespace
{
int Army(const World &world)
{
	return world.have[KindIndex(Kind::Soldier)] + world.have[KindIndex(Kind::Shooter)]
	       + world.have[KindIndex(Kind::Cavalry)];
}

/// A wave of `slots` out on its way (gathered, marching), nothing else to do this test.
State MarchingWave(const std::vector<int> &slots, const Point &target)
{
	State state;
	state.nextWaveAt = 100000;
	state.wave = slots;
	state.target = target;
	state.waves = 1;
	return state;
}
} // namespace

TEST_CASE("wc2bot: the army keeps the mix - soldiers stop at their share while shooters cannot be trained yet")
{
	const Profile recruit = Recruit();
	ToyGame game;
	game.world.can[KindIndex(Kind::Shooter)] = false; // no lumber mill yet
	game.world.can[KindIndex(Kind::Cavalry)] = false;
	State state;
	state.nextWaveAt = 100000; // no wave: the army stays at home
	SeededDice dice;
	Run(recruit, game, state, dice, 500);
	// 2 : 1 of an army of 10: 7 soldiers (rounded up), the other places kept for shooters
	CHECK(game.world.have[KindIndex(Kind::Soldier)] == 7);
	CHECK(game.world.have[KindIndex(Kind::Shooter)] == 0);

	game.world.can[KindIndex(Kind::Shooter)] = true; // the lumber mill stands
	Run(recruit, game, state, dice, 120);
	CHECK(game.world.have[KindIndex(Kind::Soldier)] == 7);
	CHECK(game.world.have[KindIndex(Kind::Shooter)] == 3);
	CHECK(Army(game.world) == 10);
}

TEST_CASE("wc2bot: gold lying unspent buys the profile's second barracks, once, and fighters from both")
{
	const Profile recruit = Recruit();
	ToyGame game;
	State state;
	state.nextWaveAt = 100000;
	SeededDice dice;
	Run(recruit, game, state, dice, 120);
	REQUIRE(game.world.have[KindIndex(Kind::Barracks)] == 1);
	game.world.gold = 1499;
	Run(recruit, game, state, dice, 60);
	CHECK(game.world.have[KindIndex(Kind::Barracks)] == 1);
	game.world.gold = 5000;
	Run(recruit, game, state, dice, 300);
	// not before the lumber mill and the blacksmith of the opening stand (540 s)
	CHECK(game.world.have[KindIndex(Kind::Barracks)] == 1);
	// the barracks there is idle (this toy's units come at once): no spare one (BOTS_FIX3)
	Run(recruit, game, state, dice, 200);
	CHECK(game.world.have[KindIndex(Kind::Barracks)] == 1);
	game.world.queued[KindIndex(Kind::Soldier)] = 1; // now it trains all the time
	Run(recruit, game, state, dice, 200);
	CHECK(game.world.have[KindIndex(Kind::Barracks)] == 2); // the profile's number, not more
	CHECK(Army(game.world) <= 10);

	// With two barracks two fighters may be in training at once.
	World world = game.world;
	for (const Kind kind : {Kind::Soldier, Kind::Shooter, Kind::Cavalry}) {
		world.have[KindIndex(kind)] = 0;
	}
	world.queued[KindIndex(Kind::Soldier)] = 1;
	world.second += 61; // a fresh rate window
	State copy = state;
	const Decision d = Decide(recruit, world, copy, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Soldier)] + d.release[KindIndex(Kind::Shooter)] == 1);
	world.queued[KindIndex(Kind::Soldier)] = 2;
	world.second += 1;
	const Decision e = Decide(recruit, world, copy, Roll, &dice);
	CHECK(e.release[KindIndex(Kind::Soldier)] + e.release[KindIndex(Kind::Shooter)] == 0);
}

TEST_CASE("wc2bot: a wave gathers at the rally point and goes together; the wait has an end")
{
	const Profile recruit = Recruit();
	const Point rally{16, 10};
	const Point target{40, 10};
	for (const bool arrive : {true, false}) {
		CAPTURE(arrive);
		State state;
		state.nextWaveAt = 0;
		World world = StartWorld();
		for (int k = 0; k < KindCount; ++k) {
			world.can[k] = false; // the economy spends nothing
		}
		world.second = 600;
		const Point places[4] = {{11, 11}, {12, 13}, {9, 9}, {10, 15}};
		for (int i = 0; i < 4; ++i) {
			Fighter f;
			f.slot = 10 + i;
			f.at = places[i];
			f.idle = true;
			world.fighters.push_back(f);
		}
		SeededDice dice;
		Decision d = Decide(recruit, world, state, Roll, &dice);
		REQUIRE(state.wave.size() >= 3);
		CHECK(state.rally == rally);
		CHECK(state.gatherUntil == 640);
		REQUIRE_FALSE(d.commands.empty());
		for (const Command &c : d.commands) {
			CHECK(c.to == rally);
		}
		const int first = state.wave[0];
		bool went = false;
		for (long s = 601; s <= 640 && !went; ++s) {
			world.second = s;
			for (Fighter &f : world.fighters) {
				// one unit is there at once; the others arrive at 605, or never
				if (f.slot == first || (arrive && s >= 605)) {
					f.at = rally;
				}
				f.idle = false;
			}
			d = Decide(recruit, world, state, Roll, &dice);
			for (const Command &c : d.commands) {
				if (c.to == target) {
					went = true;
				}
			}
			if (went) {
				CHECK(s == (arrive ? 605 : 640));
			}
		}
		REQUIRE(went);
		// The whole wave, in one second, to the same target.
		REQUIRE(d.commands.size() == state.wave.size());
		for (const Command &c : d.commands) {
			CHECK(c.to == target);
			CHECK(std::find(state.wave.begin(), state.wave.end(), c.slot) != state.wave.end());
		}
		CHECK(state.gatherUntil == -1);
	}
}

TEST_CASE("wc2bot: an empty enemy start reached - the wave goes on to a unit it sees, a building it knows, else scouts")
{
	const Profile recruit = Recruit();
	State state = MarchingWave({10, 11, 12}, {40, 10});
	World world = StartWorld();
	world.enemyStarts = {{40, 10}}; // one enemy, its hall gone
	world.mapWidth = 64;
	world.mapHeight = 64;
	for (int i = 0; i < 3; ++i) {
		Fighter f;
		f.slot = 10 + i;
		f.at = {40, 11};
		f.idle = true;
		world.fighters.push_back(f);
	}
	SeededDice dice;
	world.second = 605;
	Decision d = Decide(recruit, world, state, Roll, &dice);
	CHECK(state.visitedStarts == 1u);
	REQUIRE(state.target.Valid());
	CHECK_FALSE(state.target == Point{40, 10});
	CHECK(d.commands.size() == 3); // never left standing

	// An enemy unit in its own units' sight comes first.
	world.second = 610;
	world.seenUnits = {{700, {50, 40}}};
	d = Decide(recruit, world, state, Roll, &dice);
	CHECK(state.target == Point{50, 40});
	REQUIRE(d.commands.size() == 3);
	CHECK(d.commands[0].to == Point{50, 40});

	// A building it has seen comes before that.
	world.second = 611;
	world.seen = {{800, {20, 50}}};
	d = Decide(recruit, world, state, Roll, &dice);
	CHECK(state.target == Point{20, 50});
}

TEST_CASE("wc2bot: with nothing seen the wave scouts the whole map once, then comes home and ends")
{
	const Profile recruit = Recruit();
	State state = MarchingWave({10, 11, 12, 13}, {40, 10});
	World world = StartWorld();
	world.enemyStarts = {{40, 10}};
	world.mapWidth = 64;
	world.mapHeight = 64;
	for (int i = 0; i < 4; ++i) {
		Fighter f;
		f.slot = 10 + i;
		f.at = {40, 10};
		f.idle = true;
		world.fighters.push_back(f);
	}
	for (int k = 0; k < KindCount; ++k) {
		world.can[k] = false; // the economy spends nothing
	}
	SeededDice dice;
	std::vector<Point> places;
	long endedAt = -1;
	for (long s = 600; s < 900 && endedAt < 0; ++s) {
		world.second = s;
		const Decision d = Decide(recruit, world, state, Roll, &dice);
		if (state.wave.empty()) {
			endedAt = s;
			// the wave comes back (no rally point here: home), every unit, and is a wave no more
			CHECK(d.commands.size() == 4);
			for (const Command &c : d.commands) {
				CHECK(c.to == world.home);
			}
			CHECK_FALSE(state.target.Valid());
			CHECK(state.scouted == 0u);
			break;
		}
		CHECK(state.target.Valid()); // while it is out it always has a goal
		for (const Command &c : d.commands) {
			for (Fighter &f : world.fighters) {
				if (f.slot == c.slot) {
					f.at = c.to; // the toy: there at once
				}
			}
			if (std::find(places.begin(), places.end(), c.to) == places.end()) {
				places.push_back(c.to);
			}
		}
	}
	// every point of the 3 x 3 grid is looked at, once, and then the wave ends
	CHECK(places.size() >= 8);
	REQUIRE(endedAt > 0);
	CHECK(endedAt < 700);
}

TEST_CASE("wc2bot: a wave that can get nowhere ends and the next wave sets out (rate window not used up)")
{
	const Profile recruit = Recruit();
	// A closed corner: the wave's units never move (every goal is out of reach); seven units at home.
	State state = MarchingWave({10, 11, 12}, {40, 10});
	state.nextWaveAt = 900;
	state.idleAt = {40, 10}; // sent there already
	World world = StartWorld();
	world.enemyStarts = {{40, 10}};
	world.mapWidth = 64;
	world.mapHeight = 64;
	for (int k = 0; k < KindCount; ++k) {
		world.can[k] = false;
	}
	for (int i = 0; i < 10; ++i) {
		Fighter f;
		f.slot = 10 + i;
		f.at = i < 3 ? Point{20, 20} : Point{11, 11};
		f.idle = true;
		world.fighters.push_back(f);
	}
	SeededDice dice;
	long endedAt = -1;
	long secondWaveAt = -1;
	int maxWindow = 0;
	std::vector<int> perSecond;
	for (long s = 800; s < 1000; ++s) {
		world.second = s;
		const Decision d = Decide(recruit, world, state, Roll, &dice);
		perSecond.push_back(d.orders);
		int window = 0;
		for (size_t i = perSecond.size() >= 60 ? perSecond.size() - 60 : 0; i < perSecond.size(); ++i) {
			window += perSecond[i];
		}
		maxWindow = std::max(maxWindow, window);
		if (endedAt < 0 && state.waves == 1 && state.wave.empty()) {
			endedAt = s;
		}
		if (state.waves == 2 && secondWaveAt < 0) {
			secondWaveAt = s;
		}
	}
	CHECK(maxWindow <= 30);
	REQUIRE(endedAt > 0);
	CHECK(endedAt < 860); // a start and nine places, one try each
	REQUIRE(secondWaveAt > 0);
	CHECK(secondWaveAt >= 900);
	CHECK(secondWaveAt < 930);
}

TEST_CASE("wc2bot: an enemy unit in sight the wave cannot get to is passed over")
{
	const Profile recruit = Recruit();
	State state = MarchingWave({10, 11}, {40, 10});
	state.visitedStarts = 1u;
	World world = StartWorld();
	for (int k = 0; k < KindCount; ++k) {
		world.can[k] = false;
	}
	world.enemyStarts = {{40, 10}};
	world.mapWidth = 64;
	world.mapHeight = 64;
	world.seenUnits = {{700, {50, 40}}}; // across water: never reached
	Fighter a;
	a.slot = 10;
	a.at = {20, 20};
	a.idle = true;
	Fighter b = a;
	b.slot = 11;
	world.fighters = {a, b};
	SeededDice dice;
	world.second = 605;
	Decision d = Decide(recruit, world, state, Roll, &dice);
	CHECK(state.target == Point{50, 40});
	REQUIRE(d.commands.size() == 2);
	world.second = 610; // still idle where they stood: out of reach
	d = Decide(recruit, world, state, Roll, &dice);
	CHECK_FALSE(state.target == Point{50, 40});
	REQUIRE(state.target.Valid());
	int toUnit = 0;
	for (long s = 611; s < 700; ++s) {
		world.second = s;
		d = Decide(recruit, world, state, Roll, &dice);
		for (const Command &c : d.commands) {
			toUnit += c.to == Point{50, 40} ? 1 : 0;
		}
	}
	CHECK(toUnit == 0);
}

TEST_CASE("wc2bot: a wave standing idle short of a place it cannot reach strikes it off")
{
	const Profile recruit = Recruit();
	const Point lookout{32, 32}; // the middle scouting point of a 64 x 64 map
	State state = MarchingWave({10, 11}, lookout);
	state.visitedStarts = 1u;
	World world = StartWorld();
	for (int k = 0; k < KindCount; ++k) {
		world.can[k] = false;
	}
	world.enemyStarts = {{40, 10}};
	world.mapWidth = 64;
	world.mapHeight = 64;
	Fighter a;
	a.slot = 10;
	a.at = {20, 20};
	a.idle = true;
	Fighter b = a;
	b.slot = 11;
	b.at = {21, 20};
	world.fighters = {a, b};
	SeededDice dice;
	world.second = 604; // not a retarget second: nothing changes
	Decide(recruit, world, state, Roll, &dice);
	CHECK(state.target == lookout);
	// Idle at a retarget second: sent on once more (it may have stopped for a moment).
	world.second = 605;
	Decision d = Decide(recruit, world, state, Roll, &dice);
	CHECK(state.target == lookout);
	REQUIRE_FALSE(d.commands.empty());
	CHECK(d.commands[0].to == lookout);
	// Still idle there at the next one: it cannot get there.
	world.second = 610;
	d = Decide(recruit, world, state, Roll, &dice);
	CHECK_FALSE(state.target == lookout);
	CHECK(state.target.Valid());
	REQUIRE_FALSE(d.commands.empty());
	CHECK(d.commands[0].to == state.target);
}

TEST_CASE("wc2bot: the defence goes for the attacker it sees, with the units near the attacked place")
{
	const Profile recruit = Recruit();
	State state;
	state.nextWaveAt = 100000;
	World world = StartWorld();
	for (int k = 0; k < KindCount; ++k) {
		world.can[k] = false;
	}
	world.second = 100;
	Fighter near;
	near.slot = 1;
	near.at = {14, 14};
	near.idle = true;
	Fighter far = near;
	far.slot = 2;
	far.at = {30, 30};
	world.fighters = {near, far};
	world.alarm = true;
	world.alarmAt = {10, 10};
	world.alarmTo = {19, 10}; // a ballista shooting from 9 tiles
	SeededDice dice;
	Decide(recruit, world, state, Roll, &dice);
	world.alarm = false;
	Decision d;
	for (long s = 101; s <= 120; ++s) {
		world.second = s;
		d = Decide(recruit, world, state, Roll, &dice);
	}
	REQUIRE(d.commands.size() == 1);
	CHECK(d.commands[0].slot == 1);
	CHECK(d.commands[0].to == Point{19, 10});
}

TEST_CASE("wc2bot: the first wave ~10 min with 3-4 units, the next one larger after the gap")
{
	const Profile recruit = Recruit();
	ToyGame game;
	// an enemy building its units see from the start, where the waves go and stay fighting
	game.world.seen = {{900, {50, 50}}};
	State state;
	SeededDice dice;
	std::vector<std::pair<long, size_t>> launches;
	for (int i = 0; i < 1300; ++i) {
		const int waves = state.waves;
		const size_t before = state.wave.size();
		const Decision d = Decide(recruit, game.world, state, Roll, &dice);
		if (state.waves != waves) {
			launches.emplace_back(game.world.second, state.wave.size() - before);
		}
		game.Step(d);
	}
	REQUIRE(launches.size() >= 2);
	CHECK(launches[0].first >= 600);
	CHECK(launches[0].first < 640);
	CHECK(launches[0].second >= 3);
	CHECK(launches[0].second <= 4);
	CHECK(launches[1].first >= launches[0].first + 240);
	CHECK(launches[1].second >= 4);
	CHECK(launches[1].second <= 6);
}

TEST_CASE("wc2bot: a building with no place - no farms take its room, extras wait, the base is cleared")
{
	const Profile recruit = Recruit();
	State state;
	state.nextWaveAt = 100000;
	state.opening = 5; // hall, farm, barracks, farm, lumber mill released
	state.planned[KindIndex(Kind::Farm)] = 2;
	state.planned[KindIndex(Kind::Barracks)] = 1;
	state.planned[KindIndex(Kind::LumberMill)] = 1;
	state.nextWorkerAt = 100000;
	World world = StartWorld();
	world.have[KindIndex(Kind::Worker)] = 12;
	world.have[KindIndex(Kind::Farm)] = 2;
	world.have[KindIndex(Kind::Barracks)] = 1;
	world.queued[KindIndex(Kind::LumberMill)] = 1;
	world.unplaced[KindIndex(Kind::LumberMill)] = true;
	world.gold = 5000;
	world.can[KindIndex(Kind::Soldier)] = false;
	world.can[KindIndex(Kind::Shooter)] = false;
	world.can[KindIndex(Kind::Cavalry)] = false;
	for (int i = 0; i < 5; ++i) {
		Fighter f;
		f.slot = 30 + i;
		f.at = {11, 11};
		f.idle = true;
		world.fighters.push_back(f);
	}
	SeededDice dice;
	std::vector<long> cleared;
	for (long s = 400; s < 500; ++s) {
		world.second = s;
		const Decision d = Decide(recruit, world, state, Roll, &dice);
		CHECK(d.holdFarms);
		CHECK(d.release[KindIndex(Kind::Farm)] == 0);
		CHECK(d.release[KindIndex(Kind::Barracks)] == 0); // gold lies there, the mill comes first
		if (!d.commands.empty()) {
			cleared.push_back(s);
			CHECK(d.commands.size() == 5);
			for (const Command &c : d.commands) {
				CHECK(c.to == Point{16, 10}); // six tiles out, toward the enemy start nearest home
			}
		}
	}
	// noticed after 20 s without a place, then at most once a minute
	REQUIRE(cleared.size() == 2);
	CHECK(cleared[0] == 420);
	CHECK(cleared[1] == 480);

	// The mill stands: farms are asked again; the second barracks waits for the blacksmith too.
	world.unplaced[KindIndex(Kind::LumberMill)] = false;
	world.queued[KindIndex(Kind::LumberMill)] = 0;
	world.have[KindIndex(Kind::LumberMill)] = 1;
	world.second = 500;
	Decision d = Decide(recruit, world, state, Roll, &dice);
	CHECK_FALSE(d.holdFarms);
	CHECK(d.release[KindIndex(Kind::Barracks)] == 0);
	world.second = 540;
	d = Decide(recruit, world, state, Roll, &dice);
	REQUIRE(d.release[KindIndex(Kind::Blacksmith)] == 1);
	world.have[KindIndex(Kind::Blacksmith)] = 1;
	world.queued[KindIndex(Kind::Soldier)] = 1; // the barracks trains (a spare one only then, BOTS_FIX3)
	bool barracks = false;
	for (long s = 541; s < 560 && !barracks; ++s) {
		world.second = s;
		barracks = Decide(recruit, world, state, Roll, &dice).release[KindIndex(Kind::Barracks)] == 1;
	}
	CHECK(barracks);

	// A building of no "before extras" kind with no place holds no farm (only the base is cleared).
	State other;
	other.nextWaveAt = 100000;
	World w2 = world;
	w2.unplaced[KindIndex(Kind::Barracks)] = true;
	w2.second = 600;
	CHECK_FALSE(Decide(recruit, w2, other, Roll, &dice).holdFarms);
}

TEST_CASE("wc2bot: a state saved before the gathering and scouting fields still reads")
{
	State state;
	state.rally = {3, 4};
	state.gatherUntil = 77;
	state.scouted = 5;
	state.alarmTo = {1, 2};
	state.targetSlot = 9;
	state.ignored = {4, 7};
	state.unplacedSince[KindIndex(Kind::LumberMill)] = 400;
	state.clearedAt = 420;
	const std::string text = StateText(state);
	State full;
	REQUIRE(StateFromText(text, full));
	CHECK(full == state);
	State old;
	REQUIRE(StateFromText(text.substr(0, text.find(" sc=")), old));
	CHECK_FALSE(old.rally.Valid());
	CHECK(old.gatherUntil == -1);
	CHECK(old.scouted == 0u);
	CHECK_FALSE(old.alarmTo.Valid());
	// a state of mod 0.1.1 (no unreachable / no-place fields)
	State older;
	REQUIRE(StateFromText(text.substr(0, text.find(" ts=")), older));
	CHECK(older.rally == state.rally);
	CHECK(older.targetSlot == -1);
	CHECK(older.ignored.empty());
	CHECK(older.unplacedSince[KindIndex(Kind::LumberMill)] == -1);
	CHECK(older.clearedAt == -1);
}

// ---------------------------------------------------------------------------- Captain and Warlord (mod 0.2)

namespace
{

Profile Named(const std::string &id)
{
	ProfileSet set;
	std::string error;
	REQUIRE(ParseProfiles(Json(BotsJson), set, error));
	const Profile *profile = FindProfile(set, id);
	REQUIRE(profile != nullptr);
	REQUIRE(profile->playable);
	return *profile;
}

Fighter Own(int slot, Point at, Kind kind = Kind::Soldier, bool idle = true, int hp = 100)
{
	Fighter f;
	f.slot = slot;
	f.at = at;
	f.kind = kind;
	f.idle = idle;
	f.hp = hp;
	return f;
}

Known Enemy(int slot, Point at, Class cls, int hp = 100)
{
	Known k;
	k.slot = slot;
	k.at = at;
	k.cls = cls;
	k.hp = hp;
	return k;
}

Worker Peon(int slot, Point at, bool repairing = false)
{
	Worker w;
	w.slot = slot;
	w.at = at;
	w.repairing = repairing;
	return w;
}

/// The start world where the economy can spend nothing.
World QuietWorld()
{
	World world = StartWorld();
	for (int k = 0; k < KindCount; ++k) {
		world.can[k] = false;
	}
	return world;
}

/// A state with nothing due in the test: no wave, raid or scout; the opening and research done.
State Idle(const Profile &profile)
{
	State state;
	state.nextWaveAt = 100000;
	state.nextRaidAt = 100000;
	state.nextScoutAt = 100000;
	state.scouts = 1;
	state.opening = static_cast<int>(profile.opening.size());
	state.research = static_cast<int>(profile.research.size());
	return state;
}

/// Most orders in any 60 seconds of a run.
int MaxWindow(const std::vector<Decision> &decisions)
{
	int most = 0;
	for (size_t end = 0; end < decisions.size(); ++end) {
		int sum = 0;
		for (size_t s = end >= 59 ? end - 59 : 0; s <= end; ++s) {
			sum += decisions[s].orders;
		}
		most = std::max(most, sum);
	}
	return most;
}

/// A toy game on a 64 x 64 map where research can be done (the plan's comparisons).
ToyGame PlanGame()
{
	ToyGame game;
	game.world.mapWidth = 64;
	game.world.mapHeight = 64;
	for (int l = 0; l < LineCount; ++l) {
		game.world.canResearch[l] = true;
	}
	return game;
}

struct PlanResult {
	int workersAt600 = 0;
	long firstWave = -1;
	int waves = 0;
	long keepAt = -1;
	long castleAt = -1;
	int research = 0;
	int halls = 0;
	int army = 0;
	int maxWindow = 0;
};

/// 25 minutes of a profile in the toy world.
PlanResult Plan(const Profile &profile)
{
	ToyGame game = PlanGame();
	State state;
	SeededDice dice;
	PlanResult result;
	std::vector<Decision> decisions;
	for (int i = 0; i < 1500; ++i) {
		const int waves = state.waves;
		decisions.push_back(Decide(profile, game.world, state, Roll, &dice));
		const Decision &d = decisions.back();
		if (state.waves > waves && result.firstWave < 0) {
			result.firstWave = game.world.second;
		}
		if (d.keep && result.keepAt < 0) {
			result.keepAt = game.world.second;
		}
		if (d.castle && result.castleAt < 0) {
			result.castleAt = game.world.second;
		}
		result.research += static_cast<int>(d.research.size());
		game.Step(d);
		if (game.world.second == 600) {
			result.workersAt600 = game.world.have[KindIndex(Kind::Worker)];
		}
	}
	result.waves = state.waves;
	result.halls = game.world.have[KindIndex(Kind::Hall)];
	for (const Kind kind : FighterKindList) {
		result.army += game.world.have[KindIndex(kind)];
	}
	result.maxWindow = MaxWindow(decisions);
	return result;
}

bool Has(const std::vector<int> &slots, int slot)
{
	return std::find(slots.begin(), slots.end(), slot) != slots.end();
}

/// The text with the value of one field of a saved state replaced.
std::string WithField(const std::string &text, const std::string &key, const std::string &value)
{
	const size_t at = text.find(" " + key + "=");
	REQUIRE(at != std::string::npos);
	const size_t from = at + key.size() + 2;
	const size_t to = text.find(' ', from);
	return text.substr(0, from) + value + (to == std::string::npos ? std::string() : text.substr(to));
}

std::string Repeat(const std::string &value, int count)
{
	std::string text;
	for (int i = 0; i < count; ++i) {
		text += (i ? "," : "") + value;
	}
	return text;
}

} // namespace

TEST_CASE("wc2bot: Captain and Warlord read with their numbers, within the fairness ceilings")
{
	const Profile captain = Named("captain");
	const Profile warlord = Named("warlord");
	CHECK(captain.ordersPerMinute == 80);
	CHECK(warlord.ordersPerMinute == 180);
	CHECK(warlord.ordersPerMinute <= MaxOrdersPerMinute);
	CHECK(captain.reactionSeconds >= MinReactionSeconds);
	CHECK(warlord.reactionSeconds >= MinReactionSeconds);
	CHECK_FALSE(captain.singleAttention);
	CHECK(captain.workers == 20);
	CHECK(captain.workersAfterKeep == 24);
	CHECK(captain.lumberShare == 35);
	CHECK(captain.supplySlack == 3);
	CHECK(captain.repairWorkers == 2);
	CHECK_FALSE(captain.repairUnderAttack);
	CHECK(warlord.workers == 22);
	CHECK(warlord.workersAfterKeep == 32);
	CHECK(warlord.workersPerHall);
	CHECK(warlord.repairUnderAttack);
	CHECK(captain.expand);
	CHECK(captain.halls == 2);
	REQUIRE(captain.expansions.size() == 1);
	CHECK(captain.expansions[0].atSecond == 900);
	CHECK(captain.expansions[0].mineBelow == 40);
	CHECK(captain.expansions[0].escort == 4);
	CHECK(warlord.halls == 3);
	REQUIRE(warlord.expansions.size() == 2);
	CHECK(warlord.expansions[0].mineBelow == 60);
	REQUIRE(captain.opening.size() == 13);
	CHECK(captain.opening[6].what == Kind::Barracks);
	CHECK(captain.opening[6].atFood == 16);
	CHECK(captain.opening[7].what == Kind::Keep);
	CHECK(captain.opening[7].atWorkers == 18);
	CHECK(captain.opening[9].what == Kind::Tower);
	CHECK(captain.opening[10].what == Kind::Castle);
	CHECK(captain.opening[10].atSecond == 1080);
	CHECK(captain.opening[12].what == Kind::MageTower);
	CHECK(captain.armyMax == 24);
	CHECK(captain.weight[2] == 6);
	CHECK(captain.fromSecond[2] == 600);
	CHECK(captain.maxOf[2] == 14);
	CHECK(captain.maxOf[3] == 2);
	CHECK(captain.maxOf[4] == 2);
	CHECK(captain.maxOf[0] == -1);
	CHECK_FALSE(captain.holdPlaces);
	CHECK(captain.siegeVsTowers);
	CHECK(captain.counters);
	CHECK(captain.firstWaveSecond == 420);
	CHECK(captain.waveMin == 8);
	CHECK(captain.waveMax == 12);
	CHECK(captain.waveGapSeconds == 180);
	CHECK(captain.retreatLost == 60);
	CHECK_FALSE(captain.focusFire);
	CHECK_FALSE(captain.castBest);
	REQUIRE(captain.targets.size() == 4);
	CHECK(captain.targets[0] == Aim::Army);
	CHECK(captain.targets[1] == Aim::Worker);
	CHECK(captain.scoutWorkerSecond == 120);
	CHECK(captain.scoutGapSeconds == 300);
	REQUIRE_FALSE(captain.casts.empty());
	CHECK(captain.casts[0] == Spell::Bloodlust);
	CHECK(captain.defenceDelaySeconds == 6);
	CHECK(captain.raidSize == 0);
	CHECK(warlord.focusFire);
	CHECK(warlord.castBest);
	CHECK(warlord.fightMin == 2);
	CHECK(warlord.pullHp == 30);
	CHECK(warlord.retreatLost == 50);
	CHECK(warlord.returnSeconds == 40);
	CHECK(warlord.avoidTowers);
	CHECK(warlord.raidSize == 3);
	CHECK(warlord.raidFromSecond == 600);
	CHECK(warlord.raidGapSeconds == 150);
	CHECK(warlord.nearTiles == 48);
	CHECK(warlord.nearFirstSecond == 300);
	REQUIRE_FALSE(warlord.targets.empty());
	CHECK(warlord.targets[0] == Aim::Caster);
	CHECK(warlord.defenceDelaySeconds == 2);
	REQUIRE(warlord.research.size() == 18);
	CHECK(warlord.research[6].line == Line::Elite);
	CHECK(warlord.research[6].level == 1);
}

TEST_CASE("wc2bot: the new profile fields are refused when malformed or unfair")
{
	std::string error;
	CHECK_FALSE(Parses(RecruitWith("\"halls\": 2", "\"halls\": 1"), error));
	CHECK(error.find("expansions") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"expand\": true, \"expansions\": [{\"at_s\": 900, \"mine_below\": 40, \"escort\": 4}],",
	                               "\"expand\": true,"),
	                   error));
	CHECK(error.find("expand") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"mine_below\": 40", "\"mine_below\": 101"), error));
	CHECK(error.find("mine_below") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"targets\": [\"army\"", "\"targets\": [\"armies\""), error));
	CHECK(error.find("armies") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"cast\": [\"bloodlust\", \"healing\", \"slow\"",
	                               "\"cast\": [\"bloodlust\", \"healing\", \"fireball\""),
	                   error));
	CHECK(error.find("fireball") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"aim\": \"nearest\"", "\"aim\": \"random\""), error));
	CHECK(error.find("aim") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("{\"line\": \"elite\", \"at_s\": 1140}", "{\"line\": \"elite\", \"level\": 2, \"at_s\": 1140}"),
	                   error));
	CHECK(error.find("level") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("{\"line\": \"weapons\", \"level\": 1, \"at_s\": 300}", "{\"line\": \"weapons\", \"at_s\": 300}"),
	                   error));
	CHECK(error.find("level") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"raid\": {\"size\": 3,", "\"raid\": {\"size\": 0,"), error));
	CHECK(error.find("size") != std::string::npos);
	// the Warlord's fairness: never above 180 orders a minute, never under a second of reaction
	CHECK_FALSE(Parses(RecruitWith("\"orders_per_minute\": 180", "\"orders_per_minute\": 181"), error));
	CHECK(error.find("orders_per_minute") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"reaction_s\": 1, \"single_attention\": false", "\"reaction_s\": 0, \"single_attention\": false"),
	                   error));
	CHECK(error.find("reaction_s") != std::string::npos);
	CHECK(Parses(BotsJson, error));
}

TEST_CASE("wc2bot: Captain and Warlord games in the toy world stay within their order caps, the same twice")
{
	for (const char *id : {"captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		ToyGame gameA = PlanGame();
		ToyGame gameB = PlanGame();
		State stateA;
		State stateB;
		SeededDice diceA;
		SeededDice diceB;
		const std::vector<Decision> a = Run(profile, gameA, stateA, diceA, 1500);
		const std::vector<Decision> b = Run(profile, gameB, stateB, diceB, 1500);
		CHECK(MaxWindow(a) <= profile.ordersPerMinute);
		REQUIRE(a.size() == b.size());
		bool same = true;
		for (size_t i = 0; i < a.size(); ++i) {
			same = same && Same(a[i], b[i]);
		}
		CHECK(same);
		CHECK(stateA == stateB);
		State loaded;
		REQUIRE(StateFromText(StateText(stateA), loaded));
		CHECK(loaded == stateA);
	}
}

TEST_CASE("wc2bot: in plan the Warlord is ahead of the Captain, the Captain ahead of the Recruit")
{
	const PlanResult recruit = Plan(Recruit());
	const PlanResult captain = Plan(Named("captain"));
	const PlanResult warlord = Plan(Named("warlord"));
	// economy: more workers at 10 minutes, more halls
	CHECK(recruit.workersAt600 == 12);
	CHECK(captain.workersAt600 == 24);
	CHECK(warlord.workersAt600 == 32);
	CHECK(recruit.halls == 1);
	CHECK(captain.halls == 2);
	CHECK(warlord.halls == 3);
	// tech: the keep sooner, a castle (the Recruit never), more research
	REQUIRE(captain.keepAt > 0);
	REQUIRE(warlord.keepAt > 0);
	CHECK(captain.keepAt < 900);
	CHECK(recruit.keepAt >= 900);
	CHECK(recruit.castleAt < 0);
	CHECK(captain.castleAt >= 1080);
	CHECK(warlord.castleAt >= 840);
	CHECK(warlord.castleAt < captain.castleAt);
	CHECK(recruit.research == 2);
	CHECK(captain.research == 12);
	CHECK(warlord.research == 18);
	// army: earlier and more waves, a larger army
	REQUIRE(recruit.firstWave >= 600);
	CHECK(captain.firstWave >= 420);
	CHECK(captain.firstWave < recruit.firstWave);
	CHECK(warlord.firstWave >= 300); // an enemy start 30 tiles away: the near opening
	CHECK(warlord.firstWave < captain.firstWave);
	CHECK(recruit.waves < captain.waves);
	CHECK(captain.waves < warlord.waves);
	CHECK(recruit.army <= 10);
	CHECK(captain.army > recruit.army);
	CHECK(warlord.army > captain.army);
	// and each within its own order cap
	CHECK(recruit.maxWindow <= 30);
	CHECK(captain.maxWindow <= 80);
	CHECK(warlord.maxWindow <= 180);
}

TEST_CASE("wc2bot: an opening step waits for its food and its workers")
{
	const Profile captain = Named("captain");
	State state = Idle(captain);
	state.opening = 6; // the second barracks, at 16 food
	state.planned[KindIndex(Kind::Barracks)] = 1;
	World world = QuietWorld();
	world.have[KindIndex(Kind::Barracks)] = 1;
	world.can[KindIndex(Kind::Barracks)] = true;
	world.can[KindIndex(Kind::Keep)] = true;
	world.demand = 15;
	world.second = 300;
	SeededDice dice;
	Decision d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Barracks)] == 0);
	CHECK(state.opening == 6);
	world.demand = 16;
	world.second = 301;
	d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Barracks)] == 1);
	CHECK(state.opening == 7);
	// the keep: at 18 workers
	world.have[KindIndex(Kind::Barracks)] = 2;
	world.have[KindIndex(Kind::Worker)] = 17;
	world.second = 302;
	d = Decide(captain, world, state, Roll, &dice);
	CHECK_FALSE(d.keep);
	CHECK(state.opening == 7);
	world.have[KindIndex(Kind::Worker)] = 18;
	world.second = 303;
	d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.keep);
	CHECK(state.opening == 8);
}

TEST_CASE("wc2bot: an expansion comes when the main mine runs low or at its time; the new hall gets its escort")
{
	const Profile captain = Named("captain");
	SeededDice dice;
	{
		State state = Idle(captain);
		state.planned[KindIndex(Kind::Hall)] = 1;
		World world = QuietWorld();
		world.can[KindIndex(Kind::Hall)] = true;
		world.mineGold = 10000;
		world.second = 300;
		Decision d = Decide(captain, world, state, Roll, &dice);
		CHECK(d.release[KindIndex(Kind::Hall)] == 0);
		CHECK(state.mineFirst == 10000);
		world.mineGold = 4100; // 41 %
		world.second = 301;
		d = Decide(captain, world, state, Roll, &dice);
		CHECK(d.release[KindIndex(Kind::Hall)] == 0);
		world.mineGold = 3900; // 39 %: below the Captain's 40
		world.second = 302;
		d = Decide(captain, world, state, Roll, &dice);
		CHECK(d.release[KindIndex(Kind::Hall)] == 1);
		CHECK(state.expansions == 1);
		CHECK(state.planned[KindIndex(Kind::Hall)] == 2);
	}
	{
		State state = Idle(captain);
		state.planned[KindIndex(Kind::Hall)] = 1;
		World world = QuietWorld();
		world.can[KindIndex(Kind::Hall)] = true;
		world.mineGold = 10000;
		world.second = 899;
		CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Hall)] == 0);
		world.second = 900;
		CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Hall)] == 1);
	}
	{
		State state = Idle(captain);
		World world = QuietWorld();
		world.second = 950;
		for (int i = 0; i < 6; ++i) {
			world.fighters.push_back(Own(20 + i, {11, 11}));
		}
		world.farHalls = {{40, 40}};
		const Decision d = Decide(captain, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 4);
		for (const Command &c : d.commands) {
			CHECK(c.to == Point{40, 40});
			CHECK(c.order == Order::Attack);
		}
		CHECK(state.escorted == 1);
		world.second = 951;
		CHECK(Decide(captain, world, state, Roll, &dice).commands.empty()); // once
	}
}

TEST_CASE("wc2bot: the Warlord's wave focuses its fire by its target order; the Captain's does not")
{
	const Profile warlord = Named("warlord");
	State state = Idle(warlord);
	state.wave = {10, 11, 12, 13};
	state.target = {40, 10};
	state.waves = 1;
	state.launchSize = 4;
	World world = QuietWorld();
	world.mapWidth = 64;
	world.mapHeight = 64;
	world.second = 601;
	for (int i = 0; i < 4; ++i) {
		world.fighters.push_back(Own(10 + i, {30, 10}, Kind::Soldier, false));
	}
	world.seenUnits = {Enemy(700, {33, 10}, Class::Soldier, 40), Enemy(701, {34, 11}, Class::Caster),
	                   Enemy(702, {36, 10}, Class::Worker)};
	SeededDice dice;
	const State before = state;
	Decision d = Decide(warlord, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 4); // the whole wave on the caster
	for (const Command &c : d.commands) {
		CHECK(c.targetSlot == 701);
		CHECK(c.order == Order::Attack);
		CHECK(c.to == Point{34, 11});
	}
	CHECK(state.focusSlot == 701);
	world.second = 602;
	CHECK(Decide(warlord, world, state, Roll, &dice).commands.empty()); // still on it: no new orders
	// the caster is dead: the worker comes before the soldier in the Warlord's order
	world.seenUnits.erase(world.seenUnits.begin() + 1);
	world.second = 603;
	d = Decide(warlord, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 4);
	for (const Command &c : d.commands) {
		CHECK(c.targetSlot == 702);
	}

	const Profile captain = Named("captain");
	State other = before;
	World w2 = world;
	w2.second = 601;
	w2.seenUnits = {Enemy(700, {33, 10}, Class::Soldier, 40), Enemy(701, {34, 11}, Class::Caster)};
	CHECK(Decide(captain, w2, other, Roll, &dice).commands.empty());
	CHECK(other.focusSlot == -1);
}

TEST_CASE("wc2bot: a wave that lost its share and is outnumbered comes home; the Warlord's next one returns soon")
{
	const Profile captain = Named("captain");
	SeededDice dice;
	const auto wave = [](const Profile &profile, int launched) {
		State state = Idle(profile);
		state.wave = {10, 11};
		state.target = {40, 10};
		state.waves = 1;
		state.launchSize = launched;
		return state;
	};
	World world = QuietWorld();
	world.second = 601;
	world.fighters = {Own(10, {30, 10}, Kind::Soldier, false), Own(11, {31, 10}, Kind::Soldier, false)};
	world.seenUnits = {Enemy(700, {33, 10}, Class::Soldier), Enemy(701, {34, 10}, Class::Cavalry)};
	{
		State state = wave(captain, 5); // 3 of 5 lost: 60 %
		const Decision d = Decide(captain, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 2);
		for (const Command &c : d.commands) {
			CHECK(c.order == Order::Move);
			CHECK(c.to == world.home);
		}
		CHECK(state.wave.empty());
		CHECK(state.retreats == 1);
	}
	{
		State state = wave(captain, 3); // 1 of 3 lost: not enough
		Decide(captain, world, state, Roll, &dice);
		CHECK(state.wave.size() == 2);
		CHECK(state.retreats == 0);
	}
	{
		State state = wave(captain, 5);
		World alone = world;
		alone.seenUnits.pop_back(); // one enemy against two: not outnumbered
		Decide(captain, alone, state, Roll, &dice);
		CHECK(state.wave.size() == 2);
	}
	const Profile warlord = Named("warlord");
	State state = wave(warlord, 4); // half lost
	const Decision d = Decide(warlord, world, state, Roll, &dice);
	CHECK(d.commands.size() == 2);
	CHECK(state.wave.empty());
	CHECK(state.nextWaveAt == 641); // back in 40 s, not after the whole gap
}

TEST_CASE("wc2bot: a badly hurt Warlord unit leaves the wave and is left out of the next one")
{
	const Profile warlord = Named("warlord");
	SeededDice dice;
	State state = Idle(warlord);
	state.wave = {10, 11, 12};
	state.target = {40, 10};
	state.waves = 1;
	state.launchSize = 3;
	World world = QuietWorld();
	world.second = 601;
	world.fighters = {Own(10, {30, 10}, Kind::Soldier, false, 20), Own(11, {30, 11}, Kind::Soldier, false),
	                  Own(12, {31, 10}, Kind::Soldier, false)};
	const Decision d = Decide(warlord, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 1);
	CHECK(d.commands[0].slot == 10);
	CHECK(d.commands[0].order == Order::Move);
	CHECK(d.commands[0].to == world.home);
	CHECK(state.wave == std::vector<int>{11, 12});

	State next = Idle(warlord);
	next.nextWaveAt = 0;
	next.waves = 1;
	World home = QuietWorld();
	home.second = 700;
	home.fighters.push_back(Own(10, {10, 11}, Kind::Soldier, true, 20));
	for (int i = 0; i < 16; ++i) {
		home.fighters.push_back(Own(20 + i, {11, 11}));
	}
	Decide(warlord, home, next, Roll, &dice);
	REQUIRE(next.wave.size() >= 10);
	CHECK_FALSE(Has(next.wave, 10));
}

TEST_CASE("wc2bot: the Warlord's raid - three fast units on the workers it sees, home when hurt")
{
	const Profile warlord = Named("warlord");
	SeededDice dice;
	State state = Idle(warlord);
	state.nextRaidAt = 600;
	World world = QuietWorld();
	world.second = 600;
	for (int i = 0; i < 4; ++i) {
		world.fighters.push_back(Own(20 + i, {11, 11}, Kind::Cavalry));
	}
	for (int i = 0; i < 4; ++i) {
		world.fighters.push_back(Own(30 + i, {11, 11}, Kind::Soldier));
	}
	world.seenUnits = {Enemy(900, {50, 50}, Class::Worker)};
	Decision d = Decide(warlord, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 3);
	for (size_t i = 0; i < d.commands.size(); ++i) {
		CHECK(d.commands[i].slot == 20 + static_cast<int>(i)); // the cavalry first
		CHECK(d.commands[i].targetSlot == 900);
		CHECK(d.commands[i].order == Order::Attack);
	}
	CHECK(state.raid == std::vector<int>{20, 21, 22});
	CHECK(state.nextRaidAt == 750);
	// one of them badly hurt: all three come home
	world.fighters[1].hp = 40;
	world.second = 601;
	d = Decide(warlord, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 3);
	for (const Command &c : d.commands) {
		CHECK(c.order == Order::Move);
	}
	CHECK(state.raid.empty());

	// too few at home (twice its size, so that some stay for defence): no raid
	State few = Idle(warlord);
	few.nextRaidAt = 600;
	World small = world;
	small.fighters.resize(5);
	small.fighters[1].hp = 100;
	small.second = 600;
	CHECK(Decide(warlord, small, few, Roll, &dice).commands.empty());
	CHECK(few.raid.empty());
}

TEST_CASE("wc2bot: casting - bloodlust when the fight starts, healing the most hurt, an area spell nearest or at the best cluster")
{
	SeededDice dice;
	const auto ranges = [](World &world) {
		for (int s = 0; s < SpellCount; ++s) {
			world.spellRange[s] = 10;
		}
		world.spellRange[static_cast<int>(Spell::Bloodlust)] = 6;
		world.spellRange[static_cast<int>(Spell::Blizzard)] = 12;
	};
	const Profile warlord = Named("warlord");
	const Profile captain = Named("captain");
	{
		State state = Idle(warlord);
		World world = QuietWorld();
		ranges(world);
		world.second = 700;
		Fighter mage = Own(1, {20, 20}, Kind::Cavalry); // an ogre-mage
		mage.spells = SpellBit(Spell::Bloodlust);
		world.fighters = {mage, Own(2, {21, 20}), Own(3, {22, 20})};
		World calm = world;
		world.seenUnits = {Enemy(700, {23, 20}, Class::Soldier), Enemy(701, {24, 21}, Class::Soldier)};
		Decision d = Decide(warlord, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 1);
		CHECK(d.commands[0].slot == 1);
		CHECK(d.commands[0].order == Order::Cast);
		CHECK(d.commands[0].spell == static_cast<int>(Spell::Bloodlust));
		CHECK(d.commands[0].targetSlot == 3); // the one nearest the enemy
		world.second = 701;
		CHECK(Decide(warlord, world, state, Roll, &dice).commands.empty()); // it is casting: left alone
		// no fight: no buff
		State quiet = Idle(warlord);
		calm.seenUnits = {Enemy(700, {40, 40}, Class::Soldier)};
		CHECK(Decide(warlord, calm, quiet, Roll, &dice).commands.empty());
	}
	{
		State state = Idle(captain);
		World world = QuietWorld();
		ranges(world);
		world.second = 700;
		Fighter paladin = Own(1, {20, 20}, Kind::Cavalry);
		paladin.spells = SpellBit(Spell::Healing);
		world.fighters = {paladin, Own(2, {22, 20}, Kind::Soldier, true, 40), Own(3, {21, 20}, Kind::Soldier, true, 55)};
		const Decision d = Decide(captain, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 1);
		CHECK(d.commands[0].spell == static_cast<int>(Spell::Healing));
		CHECK(d.commands[0].targetSlot == 2);
	}
	{
		World world = QuietWorld();
		ranges(world);
		world.second = 700;
		Fighter mage = Own(1, {20, 20}, Kind::Caster);
		mage.spells = SpellBit(Spell::Blizzard);
		world.fighters = {mage};
		world.seenUnits = {Enemy(700, {25, 20}, Class::Soldier), Enemy(701, {29, 20}, Class::Soldier),
		                   Enemy(702, {30, 20}, Class::Soldier), Enemy(703, {29, 21}, Class::Soldier)};
		State nearest = Idle(captain);
		Decision d = Decide(captain, world, nearest, Roll, &dice);
		REQUIRE(d.commands.size() == 1);
		CHECK(d.commands[0].spell == static_cast<int>(Spell::Blizzard));
		CHECK(d.commands[0].to == Point{25, 20}); // the Captain: the nearest
		CHECK(d.commands[0].targetSlot == -1);
		State best = Idle(warlord);
		d = Decide(warlord, world, best, Roll, &dice);
		REQUIRE(d.commands.size() == 1);
		CHECK(d.commands[0].to == Point{29, 20}); // the Warlord: the cluster of three
		// never on its own units
		world.fighters.push_back(Own(2, {30, 21}));
		State careful = Idle(warlord);
		CHECK(Decide(warlord, world, careful, Roll, &dice).commands.empty());
	}
	{
		State state = Idle(warlord);
		World world = QuietWorld();
		ranges(world);
		world.second = 700;
		Fighter mage = Own(1, {20, 20}, Kind::Caster);
		mage.spells = SpellBit(Spell::Polymorph) | SpellBit(Spell::Slow);
		world.fighters = {mage};
		world.seenUnits = {Enemy(700, {25, 20}, Class::Cavalry), Enemy(701, {24, 20}, Class::Soldier)};
		const Decision d = Decide(warlord, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 1);
		CHECK(d.commands[0].spell == static_cast<int>(Spell::Polymorph));
		CHECK(d.commands[0].targetSlot == 700); // the knight, not the footman
	}
}

TEST_CASE("wc2bot: counters - many knights seen bring more archers and a cannon tower, flyers a guard tower")
{
	const Profile captain = Named("captain");
	for (const bool seen : {false, true}) {
		CAPTURE(seen);
		ToyGame game;
		for (int k = 0; k < KindCount; ++k) {
			game.world.can[k] = false;
		}
		for (const Kind kind : FighterKindList) {
			game.world.can[KindIndex(kind)] = true;
		}
		game.world.second = 700;
		if (seen) {
			game.world.seenUnits = {Enemy(900, {50, 50}, Class::Cavalry), Enemy(901, {50, 51}, Class::Cavalry),
			                        Enemy(902, {51, 50}, Class::Cavalry), Enemy(903, {51, 51}, Class::Cavalry)};
		}
		State state = Idle(captain);
		SeededDice dice;
		Run(captain, game, state, dice, 120);
		int army = 0;
		for (const Kind kind : FighterKindList) {
			army += game.world.have[KindIndex(kind)];
		}
		CHECK(army == 24);
		CHECK(game.world.have[KindIndex(Kind::Shooter)] == (seen ? 8 : 5));
		CHECK(game.world.have[KindIndex(Kind::Siege)] == 0); // no tower known: no siege
	}

	SeededDice dice;
	State state = Idle(captain);
	World world = QuietWorld();
	world.can[KindIndex(Kind::Tower)] = true;
	world.plainTowers = 1;
	world.canTower[0] = world.canTower[1] = true;
	world.second = 700;
	world.seenUnits = {Enemy(900, {50, 50}, Class::Cavalry), Enemy(901, {50, 51}, Class::Cavalry),
	                   Enemy(902, {51, 50}, Class::Cavalry)};
	Decision d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Tower)] == 1);
	CHECK(d.towerUpgrade == 2); // cannon
	CHECK(state.counterTowers == 2u);
	world.have[KindIndex(Kind::Tower)] = 1;
	world.seenUnits.push_back(Enemy(910, {52, 52}, Class::Flyer));
	world.second = 701;
	d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Tower)] == 1);
	CHECK(d.towerUpgrade == 1); // guard: it hits flyers
	CHECK(state.counterTowers == 3u);
	world.have[KindIndex(Kind::Tower)] = 2;
	world.second = 702;
	CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Tower)] == 0); // each once
}

TEST_CASE("wc2bot: a Warlord wave without siege does not attack into known towers; siege goes first")
{
	const Profile warlord = Named("warlord");
	SeededDice dice;
	for (const bool siege : {false, true}) {
		CAPTURE(siege);
		State state = Idle(warlord);
		state.wave = {10, 11};
		state.target = {40, 10};
		state.waves = 1;
		state.launchSize = 2;
		state.known = {Enemy(800, {40, 40}, Class::Tower), Enemy(801, {44, 40}, Class::Hall),
		               Enemy(802, {20, 50}, Class::Barracks)};
		World world = QuietWorld();
		world.second = 601;
		world.fighters = {Own(10, {30, 30}, Kind::Soldier, false),
		                  Own(11, {30, 31}, siege ? Kind::Siege : Kind::Soldier, false)};
		Decide(warlord, world, state, Roll, &dice);
		// the hall away from the starts (an expansion) stands by the tower: only with siege
		CHECK(state.target == (siege ? Point{44, 40} : Point{20, 50}));
	}
	State state = Idle(warlord);
	state.nextWaveAt = 0;
	state.waves = 1;
	state.known = {Enemy(800, {40, 40}, Class::Tower)};
	World world = QuietWorld();
	world.second = 700;
	for (int i = 0; i < 16; ++i) {
		world.fighters.push_back(Own(20 + i, {11, 11}));
	}
	world.fighters.push_back(Own(40, {30, 30}, Kind::Siege)); // far from the rally point, taken all the same
	Decide(warlord, world, state, Roll, &dice);
	REQUIRE_FALSE(state.wave.empty());
	CHECK(Has(state.wave, 40));
}

TEST_CASE("wc2bot: repair - the nearest free worker; not under attack for the Captain, the tower first for the Warlord")
{
	SeededDice dice;
	World world = QuietWorld();
	world.second = 100;
	world.gold = 500;
	world.workers = {Peon(1, {10, 12}), Peon(2, {30, 30}), Peon(3, {11, 12})};
	Damaged hall;
	hall.slot = 500;
	hall.at = {12, 12};
	hall.hp = 50;
	Damaged tower;
	tower.slot = 501;
	tower.at = {14, 14};
	tower.hp = 30;
	tower.tower = true;
	tower.attacked = true;
	world.damaged = {hall, tower};
	const Profile captain = Named("captain");
	State state = Idle(captain);
	Decision d = Decide(captain, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 1);
	CHECK(d.commands[0].slot == 3);
	CHECK(d.commands[0].order == Order::Repair);
	CHECK(d.commands[0].targetSlot == 500);

	const Profile warlord = Named("warlord");
	State other = Idle(warlord);
	d = Decide(warlord, world, other, Roll, &dice);
	REQUIRE(d.commands.size() == 1);
	CHECK(d.commands[0].slot == 3);
	CHECK(d.commands[0].targetSlot == 501);

	World busy = world;
	busy.workers[0].repairing = true;
	busy.workers[1].repairing = true;
	State full = Idle(captain);
	CHECK(Decide(captain, busy, full, Roll, &dice).commands.empty()); // two repair already
	World poor = world;
	poor.gold = 50;
	State none = Idle(captain);
	CHECK(Decide(captain, poor, none, Roll, &dice).commands.empty());
	State recruitState;
	recruitState.nextWaveAt = 100000;
	CHECK(Decide(Recruit(), world, recruitState, Roll, &dice).commands.empty()); // the Recruit never repairs
}

TEST_CASE("wc2bot: scouting - a worker to the nearest start at its time, then a fast unit every gap, in turn")
{
	const Profile captain = Named("captain");
	SeededDice dice;
	State state;
	state.nextWaveAt = 100000;
	state.opening = static_cast<int>(captain.opening.size());
	state.research = static_cast<int>(captain.research.size());
	World world = QuietWorld();
	world.workers = {Peon(5, {12, 12}), Peon(6, {13, 13})};
	world.fighters = {Own(20, {11, 11}), Own(21, {11, 12}, Kind::Cavalry)};
	world.second = 120;
	// two workers (a one-worker start): none can be spared, the worker scout waits
	CHECK(Decide(captain, world, state, Roll, &dice).commands.empty());
	CHECK(state.scouts == 0);
	world.workers.push_back(Peon(7, {13, 12}));
	world.workers.push_back(Peon(8, {12, 13}));
	world.second = 119;
	CHECK(Decide(captain, world, state, Roll, &dice).commands.empty());
	world.second = 120;
	Decision d = Decide(captain, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 1);
	CHECK(d.commands[0].slot == 5);
	CHECK(d.commands[0].order == Order::Move);
	CHECK(d.commands[0].to == Point{40, 10});
	CHECK(state.scouts == 1);
	CHECK(state.nextScoutAt == 420);
	world.second = 419;
	CHECK(Decide(captain, world, state, Roll, &dice).commands.empty());
	world.second = 420;
	d = Decide(captain, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 1);
	CHECK(d.commands[0].slot == 21); // the knight
	CHECK(d.commands[0].order == Order::Move);
	CHECK(d.commands[0].to == world.enemyStarts[0]);
	CHECK(state.nextScoutAt == 720);
	world.second = 720;
	d = Decide(captain, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 1);
	CHECK(d.commands[0].to == world.enemyStarts[1]);
}

TEST_CASE("wc2bot: an enemy start near home brings the Warlord's first wave earlier")
{
	SeededDice dice;
	const Profile warlord = Named("warlord");
	State closeBy;
	World world = StartWorld(); // an enemy start 30 tiles away
	Decide(warlord, world, closeBy, Roll, &dice);
	CHECK(closeBy.nextWaveAt == 300);
	State away;
	World distant = StartWorld();
	distant.enemyStarts = {{60, 60}};
	Decide(warlord, distant, away, Roll, &dice);
	CHECK(away.nextWaveAt == 330); // 0.2.3 (was 390)
	State captain;
	Decide(Named("captain"), world, captain, Roll, &dice);
	CHECK(captain.nextWaveAt == 420);
}

TEST_CASE("wc2bot: research passes a line of the other race; workers on lumber by the profile's share")
{
	SeededDice dice;
	const Profile captain = Named("captain");
	State state = Idle(captain);
	state.research = 7; // healing, then bloodlust
	World world = QuietWorld();
	world.second = 1300;
	world.foreign[static_cast<int>(Line::Healing)] = true; // an orc
	world.canResearch[static_cast<int>(Line::Bloodlust)] = true;
	Decision d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.research.empty());
	CHECK(state.research == 8);
	world.second = 1301;
	d = Decide(captain, world, state, Roll, &dice);
	REQUIRE(d.research.size() == 1);
	CHECK(d.research[0].first == Line::Bloodlust);
	CHECK(d.research[0].second == 1);
	CHECK(state.research == 9);

	World workers = QuietWorld();
	workers.have[KindIndex(Kind::Worker)] = 20;
	State a = Idle(captain);
	CHECK(Decide(captain, workers, a, Roll, &dice).lumberWorkers == 7); // 35 % of 20
	State b;
	b.nextWaveAt = 100000;
	CHECK(Decide(Recruit(), workers, b, Roll, &dice).lumberWorkers == 7); // its share too since 0.2.1 (35 %)
	workers.have[KindIndex(Kind::Worker)] = 30;
	const Profile warlord = Named("warlord");
	State c = Idle(warlord);
	CHECK(Decide(warlord, workers, c, Roll, &dice).lumberWorkers == 12); // 40 % of 30
}

TEST_CASE("wc2bot: the saved state keeps the Captain's and Warlord's fields; a 0.1.x state still reads")
{
	State state;
	state.known = {Enemy(5, {1, 2}, Class::Tower)};
	state.raid = {3, 4};
	state.raidTarget = {7, 8};
	state.raidSlot = 9;
	state.nextRaidAt = 750;
	state.scouts = 2;
	state.nextScoutAt = 600;
	state.scoutIndex = 1;
	state.expansions = 1;
	state.escorted = 1;
	state.mineFirst = 20000;
	state.seenPeak[static_cast<int>(Class::Cavalry)] = 4;
	state.counterTowers = 2;
	state.castBusy = {{1, 700L}};
	state.launchSize = 12;
	state.focusSlot = 77;
	state.retreats = 1;
	state.planned[KindIndex(Kind::Castle)] = 1;
	state.lostAt[KindIndex(Kind::Tower)] = 500;
	const std::string text = StateText(state);
	CHECK(text.find('"') == std::string::npos);
	State full;
	REQUIRE(StateFromText(text, full));
	CHECK(full == state);
	REQUIRE(full.known.size() == 1);
	CHECK(full.known[0].cls == Class::Tower);

	// mod 0.1.2: eleven numbers per kind, none of the 0.2 fields
	std::string old = text.substr(0, text.find(" ls="));
	old = WithField(old, "pl", Repeat("0", OldKindCount));
	old = WithField(old, "pk", Repeat("0", OldKindCount));
	old = WithField(old, "la", Repeat("-1", OldKindCount));
	old = WithField(old, "us", Repeat("-1", OldKindCount));
	State older;
	REQUIRE(StateFromText(old, older));
	CHECK(older.planned[KindIndex(Kind::Castle)] == 0);
	CHECK(older.lostAt[KindIndex(Kind::Tower)] == -1);
	CHECK(older.unplacedSince[KindIndex(Kind::Caster)] == -1);
	REQUIRE(older.known.size() == 1);
	CHECK(older.known[0].cls == Class::Other);
	CHECK(older.raid.empty());
	CHECK(older.nextRaidAt == -1);
	CHECK(older.focusSlot == -1);

	State bad;
	CHECK_FALSE(StateFromText(WithField(text, "kc", "2,3"), bad));   // classes for two buildings, one known
	CHECK_FALSE(StateFromText(WithField(text, "kc", "99"), bad));    // no such class
	CHECK_FALSE(StateFromText(WithField(text, "pl", "1,2,3"), bad)); // neither 30, 17 nor 11 kinds
	CHECK_FALSE(StateFromText(WithField(text, "cb", "1"), bad));     // pairs
}

// ---------------------------------------------------------------------------- sea and air (mod 0.2, part B)

namespace
{

/// A map of `width` x `height` tiles: land 1 left of column `from`, water from `from` to `to`, land 2
/// right of it (the terrain the map preview shows every person).
std::vector<uint16_t> Strait(int width, int height, int from, int to)
{
	std::vector<uint16_t> grid(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			grid[static_cast<size_t>(y) * width + x] = x < from ? 1 : (x > to ? 2 : 0);
		}
	}
	return grid;
}

/// The quiet start world on that map.
World SeaWorld(const std::vector<uint16_t> &grid, int width, int height)
{
	World world = QuietWorld();
	world.mapWidth = width;
	world.mapHeight = height;
	world.regions = &grid;
	int wet = 0;
	for (const uint16_t tile : grid) {
		wet += tile == 0 ? 1 : 0;
	}
	world.waterShare = wet * 100 / static_cast<int>(grid.size());
	return world;
}

/// An own ship or flyer.
Fighter Craft(int slot, Point at, Kind kind, int cargo = 0)
{
	Fighter f = Own(slot, at, kind);
	f.cargo = cargo;
	return f;
}

int Count(const Decision &d, Order order)
{
	return static_cast<int>(
		std::count_if(d.commands.begin(), d.commands.end(), [order](const Command &c) { return c.order == order; }));
}

} // namespace

TEST_CASE("wc2bot: map kind - an enemy start on other land or water enough make a sea map, from the preview's terrain")
{
	const Profile recruit = Recruit();
	CHECK(recruit.seaWater == 30);
	const std::vector<uint16_t> strait = Strait(64, 64, 30, 33);
	World world = SeaWorld(strait, 64, 64); // the enemy starts (60, 60) and (40, 10) lie over the water
	CHECK(world.waterShare == 6);
	CHECK(SameLand(world, {10, 10}, {20, 20}));
	CHECK_FALSE(SameLand(world, {10, 10}, {40, 10}));
	CHECK_FALSE(SameLand(world, {10, 10}, {31, 10})); // water is no land
	CHECK(SeaMap(recruit, world));

	// one land with a small lake: a land map; the same land with water enough: a sea map
	std::vector<uint16_t> lake(64 * 64, 1);
	for (int y = 20; y < 30; ++y) {
		for (int x = 20; x < 30; ++x) {
			lake[static_cast<size_t>(y) * 64 + x] = 0;
		}
	}
	World inland = SeaWorld(lake, 64, 64);
	CHECK(inland.waterShare == 2);
	CHECK(SameLand(inland, {10, 10}, {60, 60}));
	CHECK_FALSE(SeaMap(recruit, inland));
	inland.waterShare = 40;
	CHECK(SeaMap(recruit, inland));

	// terrain not known: one land, never a sea map
	const World unknown = StartWorld();
	CHECK(SameLand(unknown, {10, 10}, {40, 10}));
	CHECK_FALSE(SeaMap(recruit, unknown));
}

TEST_CASE("wc2bot: on an island map the Recruit's land waves never cross the water; an enemy building on its land is attacked")
{
	const Profile recruit = Recruit();
	SeededDice dice;
	const std::vector<uint16_t> strait = Strait(64, 64, 30, 33);
	State state = Idle(recruit);
	state.nextWaveAt = 0;
	World world = SeaWorld(strait, 64, 64);
	for (int i = 0; i < 6; ++i) {
		world.fighters.push_back(Own(10 + i, {12, 12}));
	}
	// Both enemy starts lie over the water: its waves only look round its own land.
	for (long s = 600; s < 900; ++s) {
		world.second = s;
		const Decision d = Decide(recruit, world, state, Roll, &dice);
		if (state.target.Valid()) {
			CHECK(SameLand(world, world.home, state.target));
		}
		for (const Command &c : d.commands) {
			CHECK(SameLand(world, world.home, c.to));
			for (Fighter &f : world.fighters) {
				if (f.slot == c.slot) {
					f.at = c.to;
					f.idle = true;
				}
			}
		}
	}
	CHECK(state.visitedStarts == 0u); // never reached, never struck off

	// An enemy expansion on its own land: the next wave goes there.
	State next = Idle(recruit);
	next.nextWaveAt = 0;
	next.known = {Enemy(800, {20, 40}, Class::Hall)};
	World base = SeaWorld(strait, 64, 64);
	for (int i = 0; i < 6; ++i) {
		base.fighters.push_back(Own(10 + i, {12, 12}));
	}
	base.second = 600;
	Decide(recruit, base, next, Roll, &dice);
	REQUIRE(next.wave.size() >= 3);
	CHECK(next.target == Point{20, 40});
}

TEST_CASE("wc2bot: the Captain's sea - a shipyard at its time, tankers, a platform on a seen patch, foundry, destroyers then battleships")
{
	const Profile captain = Named("captain");
	SeededDice dice;
	const std::vector<uint16_t> strait = Strait(64, 64, 30, 33);
	State state = Idle(captain);
	World world = SeaWorld(strait, 64, 64);
	for (const Kind kind : {Kind::Shipyard, Kind::Refinery, Kind::Foundry, Kind::Platform, Kind::Tanker, Kind::Destroyer,
	                        Kind::Battleship, Kind::Submarine, Kind::Transport}) {
		world.can[KindIndex(kind)] = true;
	}
	World land = world;
	land.regions = nullptr; // a land map: no sea play
	land.second = 400;
	State landState = Idle(captain);
	CHECK(Decide(captain, land, landState, Roll, &dice).release[KindIndex(Kind::Shipyard)] == 0);

	world.second = 359;
	CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Shipyard)] == 0);
	world.second = 360;
	Decision d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Shipyard)] == 1);
	CHECK(state.planned[KindIndex(Kind::Shipyard)] == 1);

	world.have[KindIndex(Kind::Shipyard)] = 1;
	world.second = 361;
	d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Shipyard)] == 0); // one is the Captain's number
	CHECK(d.release[KindIndex(Kind::Tanker)] == 1);
	CHECK(d.release[KindIndex(Kind::Destroyer)] == 1);
	CHECK(d.release[KindIndex(Kind::Battleship)] == 0); // not before 10 minutes
	CHECK(d.release[KindIndex(Kind::Platform)] == 0);   // no tanker yet

	world.have[KindIndex(Kind::Tanker)] = 1;
	world.second = 362;
	CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Platform)] == 0); // no patch seen
	world.oilKnown = 1;
	world.second = 363;
	CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Platform)] == 1);

	world.have[KindIndex(Kind::Platform)] = 1;
	world.have[KindIndex(Kind::Tanker)] = 2;
	world.have[KindIndex(Kind::Destroyer)] = 4;
	world.second = 600;
	d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Foundry)] == 1);
	CHECK(d.release[KindIndex(Kind::Battleship)] == 1); // four destroyers: the battleships now
	CHECK(d.release[KindIndex(Kind::Destroyer)] == 0);
	CHECK(d.release[KindIndex(Kind::Refinery)] == 0);  // only with two shipyards
	CHECK(d.release[KindIndex(Kind::Transport)] == 0); // no foundry yet
	CHECK(d.release[KindIndex(Kind::Tanker)] == 0);

	world.have[KindIndex(Kind::Foundry)] = 1;
	world.second = 779;
	CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Transport)] == 0);
	world.second = 780; // two minutes before the first landing
	CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Transport)] == 1);
}

TEST_CASE("wc2bot: the Recruit's sea - one shipyard after 12 minutes, destroyers only; the Warlord's two shipyards and a refinery")
{
	SeededDice dice;
	const std::vector<uint16_t> strait = Strait(64, 64, 30, 33);
	World world = SeaWorld(strait, 64, 64);
	for (const Kind kind :
	     {Kind::Shipyard, Kind::Refinery, Kind::Foundry, Kind::Destroyer, Kind::Battleship, Kind::Submarine, Kind::Transport}) {
		world.can[KindIndex(kind)] = true;
	}
	const Profile recruit = Recruit();
	State state = Idle(recruit);
	world.second = 719;
	CHECK(Decide(recruit, world, state, Roll, &dice).release[KindIndex(Kind::Shipyard)] == 0);
	world.second = 720;
	CHECK(Decide(recruit, world, state, Roll, &dice).release[KindIndex(Kind::Shipyard)] == 1);
	world.have[KindIndex(Kind::Shipyard)] = 1;
	world.second = 721;
	Decision d = Decide(recruit, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Destroyer)] == 1);
	world.have[KindIndex(Kind::Destroyer)] = 3;
	bool more = false;
	for (long s = 722; s < 1500; ++s) {
		world.second = s;
		d = Decide(recruit, world, state, Roll, &dice);
		for (const Kind kind : {Kind::Destroyer, Kind::Battleship, Kind::Transport, Kind::Foundry, Kind::Shipyard}) {
			more = more || d.release[KindIndex(kind)] != 0;
		}
	}
	CHECK_FALSE(more); // three destroyers at most, nothing else on the water

	const Profile warlord = Named("warlord");
	State other = Idle(warlord);
	World sea = world;
	sea.have[KindIndex(Kind::Destroyer)] = 0;
	sea.second = 10;
	CHECK(Decide(warlord, sea, other, Roll, &dice).release[KindIndex(Kind::Refinery)] == 0);
	CHECK(other.planned[KindIndex(Kind::Shipyard)] == 2); // the second shipyard asked
	sea.have[KindIndex(Kind::Shipyard)] = 2;
	sea.second = 11;
	CHECK(Decide(warlord, sea, other, Roll, &dice).release[KindIndex(Kind::Refinery)] == 1);
}

TEST_CASE("wc2bot: the Warlord's fleet goes for the oil platform first, the Captain's for the ship in its way")
{
	SeededDice dice;
	const std::vector<uint16_t> strait = Strait(128, 128, 58, 63);
	World world = SeaWorld(strait, 128, 128);
	world.enemyStarts = {{100, 100}};
	world.second = 605;
	world.ships = {Craft(50, {60, 40}, Kind::Destroyer), Craft(51, {60, 41}, Kind::Destroyer),
	               Craft(52, {61, 40}, Kind::Battleship)};
	world.seenUnits = {Enemy(700, {61, 44}, Class::Ship)};
	for (const char *id : {"warlord", "captain"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		State state = Idle(profile);
		state.fleet = {50, 51, 52};
		state.fleetLaunch = 3;
		state.fleetTarget = {60, 40};
		state.known = {Enemy(800, {61, 90}, Class::Platform)};
		const Decision d = Decide(profile, world, state, Roll, &dice);
		const int target = std::string(id) == "warlord" ? 800 : 700;
		REQUIRE(d.commands.size() == 3);
		for (const Command &c : d.commands) {
			CHECK(c.order == Order::Attack);
			CHECK(c.targetSlot == target);
		}
		CHECK(state.fleetSlot == target);
		CHECK(state.enemyShipAt == 605);
	}
}

TEST_CASE("wc2bot: warships at home meet an enemy transport near home; with its number the fleet sets out")
{
	SeededDice dice;
	const Profile captain = Named("captain");
	const std::vector<uint16_t> strait = Strait(64, 64, 30, 33);
	State state = Idle(captain);
	World world = SeaWorld(strait, 64, 64);
	world.second = 700;
	world.ships = {Craft(50, {31, 12}, Kind::Destroyer), Craft(51, {31, 13}, Kind::Destroyer)};
	world.seenUnits = {Enemy(900, {32, 20}, Class::Transport)};
	Decision d = Decide(captain, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 2);
	for (const Command &c : d.commands) {
		CHECK(c.targetSlot == 900);
	}
	CHECK(state.fleet.empty()); // they stay home

	// no enemy near: three at home and an enemy shipyard known - the fleet sets out
	world.seenUnits.clear();
	world.ships.push_back(Craft(52, {31, 14}, Kind::Battleship));
	state.known = {Enemy(801, {40, 20}, Class::Shipyard)};
	world.second = 701;
	d = Decide(captain, world, state, Roll, &dice);
	REQUIRE(d.commands.size() == 3);
	for (const Command &c : d.commands) {
		CHECK(c.targetSlot == 801);
		CHECK(c.order == Order::Attack);
	}
	CHECK(state.fleet.size() == 3);
	CHECK(state.nextFleetAt == 701 + 180);
	// with two only it stays home
	State few = Idle(captain);
	few.known = state.known;
	World two = world;
	two.ships.pop_back();
	CHECK(Decide(captain, two, few, Roll, &dice).commands.empty());
}

TEST_CASE("wc2bot: a Captain landing waits for a clear sea, boards at the home shore, unloads on the goal's shore, is the wave there")
{
	SeededDice dice;
	const Profile captain = Named("captain");
	const std::vector<uint16_t> strait = Strait(64, 64, 30, 33);
	State state = Idle(captain);
	state.enemyShipAt = 870; // an enemy warship seen half a minute ago
	World world = SeaWorld(strait, 64, 64);
	world.second = 900;
	world.ships = {Craft(60, {31, 10}, Kind::Transport), Craft(61, {31, 11}, Kind::Transport),
	               Craft(62, {31, 15}, Kind::Destroyer), Craft(63, {31, 16}, Kind::Destroyer)};
	for (int i = 0; i < 10; ++i) {
		world.fighters.push_back(Own(10 + i, {12, 12}));
	}
	world.fighters.push_back(Own(30, {14, 14}, Kind::Cavalry));
	CHECK(Decide(captain, world, state, Roll, &dice).commands.empty());
	CHECK(state.landingPhase == 0);

	state.enemyShipAt = 800; // the sea clear for 100 s
	world.second = 901;
	Decision d = Decide(captain, world, state, Roll, &dice);
	REQUIRE(state.landingPhase == 1);
	const size_t size = state.landing.size();
	REQUIRE(size >= 6);
	REQUIRE(size <= 8);
	CHECK(Has(state.landing, 30)); // the knight first
	CHECK(Count(d, Order::Board) == static_cast<int>(size));
	CHECK(state.landingShore == Point{34, 10}); // the shore of the goal (40, 10)'s land, six tiles before it
	CHECK(state.landingWater == Point{33, 9});
	for (const Command &c : d.commands) {
		if (c.order == Order::Board) {
			CHECK((c.targetSlot == 60 || c.targetSlot == 61));
		}
	}

	// all aboard (six to a transport): the loaded transports sail and unload there
	int aboard[2] = {0, 0};
	for (Fighter &f : world.fighters) {
		if (Has(state.landing, f.slot)) {
			const long place = std::find(state.landing.begin(), state.landing.end(), f.slot) - state.landing.begin();
			const int boat = place < 6 ? 0 : 1;
			f.aboard = true;
			f.at = world.ships[boat].at;
			++aboard[boat];
		}
	}
	world.ships[0].cargo = aboard[0];
	world.ships[1].cargo = aboard[1];
	world.second = 902;
	d = Decide(captain, world, state, Roll, &dice);
	CHECK(state.landingPhase == 2);
	CHECK(Count(d, Order::Unload) == (aboard[1] > 0 ? 2 : 1));
	for (const Command &c : d.commands) {
		if (c.order == Order::Unload) {
			CHECK(c.to == Point{34, 10});
		}
	}

	// ashore on the other land: the wave there; it goes for the enemy start, the transports go home
	for (Fighter &f : world.fighters) {
		if (f.aboard) {
			f.aboard = false;
			f.at = {35, 10};
			f.idle = true;
		}
	}
	world.ships[0].cargo = 0;
	world.ships[1].cargo = 0;
	world.ships[0].at = {33, 20};
	world.ships[1].at = {33, 21};
	world.second = 903;
	d = Decide(captain, world, state, Roll, &dice);
	CHECK(state.landingPhase == 0);
	CHECK(state.landings == 1);
	CHECK(state.wave.size() == size);
	CHECK(state.target == Point{40, 10});
	CHECK(state.nextLandingAt == 903 + 300);
	CHECK(Count(d, Order::Attack) == static_cast<int>(size));
	CHECK(Count(d, Order::Move) >= 1);
}

TEST_CASE("wc2bot: air - the Captain's flying scout every 300 s, the Warlord's picket always flying, riders in a group of five")
{
	SeededDice dice;
	const Profile captain = Named("captain");
	{
		State state = Idle(captain);
		World world = QuietWorld();
		world.mapWidth = 64;
		world.mapHeight = 64;
		world.flyers = {Craft(70, {10, 10}, Kind::Balloon)};
		world.second = 599;
		CHECK(Decide(captain, world, state, Roll, &dice).commands.empty());
		world.second = 600;
		Decision d = Decide(captain, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 1);
		CHECK(d.commands[0].slot == 70);
		CHECK(d.commands[0].order == Order::Move);
		CHECK(d.commands[0].to == world.enemyStarts[0]);
		CHECK(state.nextAirScoutAt == 900);
		world.second = 605;
		CHECK(Decide(captain, world, state, Roll, &dice).commands.empty());
		world.second = 900;
		d = Decide(captain, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 1);
		CHECK(d.commands[0].to == world.enemyStarts[1]);
	}
	const Profile warlord = Named("warlord");
	{
		State state = Idle(warlord);
		World world = QuietWorld();
		world.mapWidth = 64;
		world.mapHeight = 64;
		world.flyers = {Craft(70, {10, 10}, Kind::Balloon), Craft(71, {10, 11}, Kind::Balloon)};
		world.second = 600;
		Decision d = Decide(warlord, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 2);
		CHECK(d.commands[0].to == world.enemyStarts[0]);
		CHECK(d.commands[1].to == world.enemyStarts[1]);
		world.second = 601;
		CHECK(Decide(warlord, world, state, Roll, &dice).commands.empty());
		world.second = 605; // idle again: on to the next places at once
		d = Decide(warlord, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 2);
		CHECK(d.commands[0].to == Point{64 / 6, 64 / 6}); // the first scouting point
	}
	{
		State state = Idle(warlord);
		state.known = {Enemy(800, {50, 50}, Class::Hall)};
		World world = QuietWorld();
		world.second = 701;
		for (int i = 0; i < 4; ++i) {
			world.flyers.push_back(Craft(80 + i, {11, 11}, Kind::Rider));
		}
		CHECK(Decide(warlord, world, state, Roll, &dice).commands.empty()); // four: no group yet
		world.flyers.push_back(Craft(84, {11, 12}, Kind::Rider));
		world.second = 702;
		const Decision d = Decide(warlord, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 5);
		for (const Command &c : d.commands) {
			CHECK(c.targetSlot == 800);
		}
		CHECK(state.riders.size() == 5);
	}
}

TEST_CASE("wc2bot: flying scouts from the profile's time; gryphons / dragons only while the Warlord out-mines the enemy it saw")
{
	SeededDice dice;
	const Profile warlord = Named("warlord");
	State state = Idle(warlord);
	World world = QuietWorld();
	for (const Kind kind : {Kind::Inventor, Kind::Balloon, Kind::Aviary, Kind::Rider}) {
		world.can[KindIndex(kind)] = true;
	}
	world.second = 599;
	CHECK(Decide(warlord, world, state, Roll, &dice).release[KindIndex(Kind::Inventor)] == 0);
	world.second = 600;
	CHECK(Decide(warlord, world, state, Roll, &dice).release[KindIndex(Kind::Inventor)] == 1);
	world.have[KindIndex(Kind::Inventor)] = 1;
	world.second = 601;
	Decision d = Decide(warlord, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Balloon)] == 1);
	CHECK(d.release[KindIndex(Kind::Aviary)] == 0); // no enemy hall seen: nothing to out-mine

	state.known = {Enemy(800, {50, 50}, Class::Hall)};
	world.second = 602;
	CHECK(Decide(warlord, world, state, Roll, &dice).release[KindIndex(Kind::Aviary)] == 0); // one hall each
	world.have[KindIndex(Kind::Hall)] = 2;
	world.second = 603;
	CHECK(Decide(warlord, world, state, Roll, &dice).release[KindIndex(Kind::Aviary)] == 1);
	world.have[KindIndex(Kind::Aviary)] = 1;
	world.second = 604;
	CHECK(Decide(warlord, world, state, Roll, &dice).release[KindIndex(Kind::Rider)] == 1);

	const Profile captain = Named("captain");
	State none = Idle(captain);
	none.known = state.known;
	world.second = 605;
	CHECK(Decide(captain, world, none, Roll, &dice).release[KindIndex(Kind::Rider)] == 0); // the Captain builds none
}

TEST_CASE("wc2bot: casting - unholy armour on a whole fighter in a fight, exorcism on the undead, invisibility on a loaded transport")
{
	SeededDice dice;
	const Profile warlord = Named("warlord");
	const auto ranges = [](World &world) {
		for (int s = 0; s < SpellCount; ++s) {
			world.spellRange[s] = 10;
		}
	};
	{
		State state = Idle(warlord);
		World world = QuietWorld();
		ranges(world);
		world.second = 700;
		Fighter knight = Own(1, {20, 20}, Kind::Caster);
		knight.spells = SpellBit(Spell::UnholyArmour);
		world.fighters = {knight, Own(2, {21, 20}), Own(3, {22, 20}, Kind::Soldier, true, 60)};
		world.seenUnits = {Enemy(700, {23, 20}, Class::Soldier), Enemy(701, {24, 21}, Class::Soldier)};
		const Decision d = Decide(warlord, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 1);
		CHECK(d.commands[0].spell == static_cast<int>(Spell::UnholyArmour));
		CHECK(d.commands[0].targetSlot == 2); // not the hurt one nearer the enemy
	}
	{
		State state = Idle(warlord);
		World world = QuietWorld();
		ranges(world);
		world.second = 700;
		Fighter paladin = Own(1, {20, 20}, Kind::Cavalry);
		paladin.spells = SpellBit(Spell::Exorcism);
		world.fighters = {paladin};
		Known knight = Enemy(700, {25, 20}, Class::Caster);
		knight.undead = true;
		world.seenUnits = {Enemy(701, {24, 20}, Class::Soldier), knight};
		const Decision d = Decide(warlord, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 1);
		CHECK(d.commands[0].spell == static_cast<int>(Spell::Exorcism));
		CHECK(d.commands[0].targetSlot == 700);
	}
	{
		State state = Idle(warlord);
		state.landingPhase = 1;
		state.landingSince = 690;
		state.landing = {40};
		state.ferry = {60};
		World world = QuietWorld();
		ranges(world);
		world.second = 700;
		Fighter mage = Own(1, {20, 20}, Kind::Caster);
		mage.spells = SpellBit(Spell::Invisibility);
		Fighter rider = Own(40, {22, 20}, Kind::Cavalry);
		rider.aboard = true;
		world.fighters = {mage, rider};
		world.ships = {Craft(60, {22, 20}, Kind::Transport, 1)};
		const Decision d = Decide(warlord, world, state, Roll, &dice);
		CHECK(state.landingPhase == 2); // all aboard: it sails
		CHECK(Count(d, Order::Unload) == 1);
		REQUIRE(Count(d, Order::Cast) == 1);
		for (const Command &c : d.commands) {
			if (c.order == Order::Cast) {
				CHECK(c.spell == static_cast<int>(Spell::Invisibility));
				CHECK(c.targetSlot == 60);
			}
		}
	}
}

TEST_CASE("wc2bot: few attackers - the Warlord's nearest workers join the defence; not against many, never the Captain's")
{
	SeededDice dice;
	const Profile warlord = Named("warlord");
	const auto attack = [&dice](const Profile &profile, int attackers) {
		State state = Idle(profile);
		World world = QuietWorld();
		world.second = 100;
		world.alarm = true;
		world.alarmAt = {12, 12};
		world.alarmTo = {14, 14};
		for (int i = 0; i < attackers; ++i) {
			world.seenUnits.push_back(Enemy(700 + i, {14, 14 + i}, Class::Soldier));
		}
		world.workers = {Peon(1, {12, 12}), Peon(2, {13, 13}), Peon(3, {40, 40})};
		Decide(profile, world, state, Roll, &dice);
		world.alarm = false;
		Decision d;
		for (long s = 101; s <= 110 && d.commands.empty(); ++s) {
			world.second = s;
			d = Decide(profile, world, state, Roll, &dice);
		}
		return d;
	};
	const Decision d = attack(warlord, 2);
	REQUIRE(d.commands.size() == 3);
	CHECK(d.commands[0].slot == 2); // the nearest to the attackers first
	CHECK(d.commands[1].slot == 1);
	CHECK(d.commands[2].slot == 3);
	for (const Command &c : d.commands) {
		CHECK(c.to == Point{14, 14});
	}
	CHECK(attack(warlord, 3).commands.empty()); // three: too many for workers
	CHECK(attack(Named("captain"), 1).commands.empty());
}

TEST_CASE("wc2bot: the sea and air fields read with their numbers; malformed ones are refused")
{
	const Profile recruit = Recruit();
	const Profile captain = Named("captain");
	const Profile warlord = Named("warlord");
	CHECK(recruit.shipyardSecond == 720);
	CHECK(recruit.fleetMax == 3);
	CHECK(recruit.fleetMin == 2);
	CHECK(recruit.transports == 0);
	CHECK(recruit.landingMax == 0);
	CHECK(recruit.airScouts == 0);
	CHECK(captain.shipyardSecond == 360);
	CHECK(captain.tankers == 2);
	CHECK(captain.platforms == 1);
	CHECK(captain.refinery);
	CHECK(captain.foundrySecond == 600);
	CHECK(captain.fleetWeight[1] == 1);
	CHECK(captain.fleetMaxOf[0] == 4);
	CHECK(captain.fleetFrom[1] == 600);
	REQUIRE_FALSE(captain.fleetTargets.empty());
	CHECK(captain.fleetTargets[0] == Aim::Ship);
	REQUIRE(captain.seaResearch.size() == 2);
	CHECK(captain.seaResearch[0].line == Line::ShipWeapons);
	CHECK(captain.landingMin == 6);
	CHECK(captain.landingMax == 8);
	CHECK(captain.landingClear);
	CHECK(captain.landingCover == 2);
	CHECK(captain.airScouts == 1);
	CHECK(captain.airScoutGapSeconds == 300);
	CHECK(captain.riderGroup == 0);
	CHECK(warlord.shipyards == 2);
	REQUIRE_FALSE(warlord.fleetTargets.empty());
	CHECK(warlord.fleetTargets[0] == Aim::Platform); // the enemy's oil platform first
	CHECK(warlord.fleetWeight[2] == 1);
	CHECK_FALSE(warlord.landingClear);
	CHECK(warlord.landingMax == 12);
	CHECK(warlord.airScoutGapSeconds == 0);
	CHECK(warlord.riderGroup == 5);
	CHECK(warlord.riderMax == 8);
	REQUIRE_FALSE(warlord.airTargets.empty());
	CHECK(warlord.airTargets[0] == Aim::Flyer);
	CHECK(warlord.defenceWorkers == 4);
	CHECK(warlord.defenceWorkersVs == 2);
	CHECK(std::find(warlord.casts.begin(), warlord.casts.end(), Spell::Invisibility) != warlord.casts.end());

	std::string error;
	CHECK_FALSE(Parses(RecruitWith("\"size\": [6, 8], \"from_s\": 900", "\"size\": [6, 13], \"from_s\": 900"), error));
	CHECK(error.find("carry") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"targets\": [\"ship\", \"transport\"", "\"targets\": [\"whale\", \"transport\""), error));
	CHECK(error.find("whale") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"mix\": {\"destroyer\": 1}", "\"mix\": {\"frigate\": 1}"), error));
	CHECK(error.find("frigate") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"tankers\": 1, \"platforms\": 1", "\"tankers\": 0, \"platforms\": 1"), error));
	CHECK(error.find("tankers") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"group\": 5, \"max\": 8", "\"group\": 5, \"max\": 4"), error));
	CHECK_FALSE(Parses(RecruitWith("\"max\": 3, \"mix\"", "\"max\": 1, \"mix\""), error));
	CHECK(error.find("min") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("{\"line\": \"ship-weapons\", \"level\": 1}", "{\"line\": \"ship-weapons\"}"), error));
	CHECK(error.find("level") != std::string::npos);
	CHECK_FALSE(Parses(RecruitWith("\"shipyard_s\": 720, ", ""), error));
	CHECK(error.find("shipyard_s") != std::string::npos);
}

TEST_CASE("wc2bot: on a sea map in the toy world the three stay within their caps, the same twice, with their sea plan")
{
	const std::vector<uint16_t> strait = Strait(64, 64, 30, 33);
	for (const char *id : {"recruit", "captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		const auto game = [&strait]() {
			ToyGame toy = PlanGame();
			toy.world.regions = &strait;
			toy.world.waterShare = 6;
			toy.world.oilKnown = 2;
			return toy;
		};
		ToyGame gameA = game();
		ToyGame gameB = game();
		State stateA;
		State stateB;
		SeededDice diceA;
		SeededDice diceB;
		const std::vector<Decision> a = Run(profile, gameA, stateA, diceA, 1500);
		const std::vector<Decision> b = Run(profile, gameB, stateB, diceB, 1500);
		CHECK(MaxWindow(a) <= profile.ordersPerMinute);
		bool same = a.size() == b.size();
		for (size_t i = 0; same && i < a.size(); ++i) {
			same = Same(a[i], b[i]);
		}
		CHECK(same);
		CHECK(stateA == stateB);
		State loaded;
		REQUIRE(StateFromText(StateText(stateA), loaded));
		CHECK(loaded == stateA);
		const World &w = gameA.world;
		CHECK(w.have[KindIndex(Kind::Shipyard)] == profile.shipyards);
		CHECK(w.have[KindIndex(Kind::Platform)] == profile.platforms);
		CHECK(w.have[KindIndex(Kind::Destroyer)] >= 1);
		CHECK(w.have[KindIndex(Kind::Destroyer)] + w.have[KindIndex(Kind::Battleship)] + w.have[KindIndex(Kind::Submarine)]
		      <= profile.fleetMax);
		CHECK(w.have[KindIndex(Kind::Transport)] == profile.transports);
		CHECK(w.have[KindIndex(Kind::Refinery)] == (profile.refinery && profile.shipyards >= 2 ? 1 : 0));
	}
}

TEST_CASE("wc2bot: the saved state keeps the sea and air fields; a state of mod 0.2's land play still reads")
{
	State state;
	state.fleet = {50, 51};
	state.fleetTarget = {60, 40};
	state.fleetSlot = 800;
	state.fleetLaunch = 3;
	state.nextFleetAt = 900;
	state.fleetVisited = 1;
	state.fleetIgnored = {801};
	state.fleetIdleAt = {60, 40};
	state.enemyShipAt = 870;
	state.landingPhase = 2;
	state.landingSince = 880;
	state.nextLandingAt = 1200;
	state.landingSize = 7;
	state.landings = 1;
	state.landing = {10, 11};
	state.ferry = {60};
	state.landingShore = {34, 10};
	state.landingWater = {33, 9};
	state.seaResearch = 1;
	state.nextAirScoutAt = 900;
	state.airScoutIndex = 2;
	state.riders = {80, 81};
	state.riderTarget = {50, 50};
	state.riderSlot = 800;
	state.riderLaunch = 5;
	state.planned[KindIndex(Kind::Shipyard)] = 2;
	state.seenPeak[static_cast<int>(Class::Transport)] = 1;
	const std::string text = StateText(state);
	State full;
	REQUIRE(StateFromText(text, full));
	CHECK(full == state);

	// mod 0.2's land play: seventeen numbers per kind, fourteen classes, none of the sea / air fields
	std::string land = text.substr(0, text.find(" fl="));
	land = WithField(land, "pl", Repeat("0", LandKindCount));
	land = WithField(land, "pk", Repeat("0", LandKindCount));
	land = WithField(land, "la", Repeat("-1", LandKindCount));
	land = WithField(land, "us", Repeat("-1", LandKindCount));
	land = WithField(land, "sp", Repeat("0", LandClassCount));
	State older;
	REQUIRE(StateFromText(land, older));
	CHECK(older.planned[KindIndex(Kind::Shipyard)] == 0);
	CHECK(older.lostAt[KindIndex(Kind::Platform)] == -1);
	CHECK(older.fleet.empty());
	CHECK(older.landingPhase == 0);
	CHECK(older.nextLandingAt == -1);
	CHECK(older.riderSlot == -1);

	State bad;
	CHECK_FALSE(StateFromText(WithField(text, "lp", "4"), bad));    // no such landing phase
	CHECK_FALSE(StateFromText(WithField(text, "sp", "1,2,3"), bad)); // neither 20 nor 14 classes
	CHECK_FALSE(StateFromText(text + " fl=1", bad));                 // a field twice
}

// ---------------------------------------------------------------------------- economy from one worker

namespace
{
/// Price and time of a kind in the toy economy: the game's prices; the times are the game's build
/// times in seconds at the normal speed (a time unit is a third of a second), rounded.
struct Price {
	int gold = 0;
	int wood = 0;
	int seconds = 1;
};

Price PriceOf(Kind kind)
{
	switch (kind) {
		case Kind::Worker: return {400, 0, 15};
		case Kind::Soldier: return {600, 0, 20};
		case Kind::Shooter: return {500, 50, 23};
		case Kind::Cavalry: return {800, 100, 30};
		case Kind::Siege: return {900, 300, 83};
		case Kind::Caster: return {1200, 0, 40};
		case Kind::Farm: return {500, 250, 33};
		case Kind::Barracks: return {700, 450, 67};
		case Kind::LumberMill: return {600, 450, 50};
		case Kind::Blacksmith: return {800, 450, 67};
		case Kind::Stables: return {1000, 300, 50};
		case Kind::Hall: return {1200, 800, 85};
		case Kind::Keep: return {2000, 1000, 67};
		case Kind::Castle: return {2500, 1200, 67};
		case Kind::Tower: return {550, 200, 20};
		case Kind::MageTower: return {1000, 200, 42};
		case Kind::Church: return {900, 500, 58};
		case Kind::Inventor: return {1000, 400, 50};
		case Kind::Aviary: return {1000, 400, 50};
		default: return {1000, 500, 60};
	}
}

/// What a kind needs standing (finished) before it can be asked, as the game's rules have it.
bool Allowed(const int (&done)[KindCount], Kind kind)
{
	const auto has = [&done](Kind k) { return done[KindIndex(k)] > 0; };
	switch (kind) {
		case Kind::Worker: return has(Kind::Hall);
		case Kind::Soldier: return has(Kind::Barracks);
		case Kind::Shooter: return has(Kind::Barracks) && has(Kind::LumberMill);
		case Kind::Cavalry: return has(Kind::Barracks) && has(Kind::Stables);
		case Kind::Siege: return has(Kind::Barracks) && has(Kind::Blacksmith) && has(Kind::LumberMill);
		case Kind::Caster: return has(Kind::MageTower);
		case Kind::Farm: return true; // the game's farm needs nothing (the engine allows it before a hall)
		case Kind::Barracks:
		case Kind::LumberMill:
		case Kind::Blacksmith: return has(Kind::Hall);
		case Kind::Hall: return true; // the first one too (a classic map's start: one peasant, no hall)
		case Kind::Stables:
		case Kind::Inventor: return has(Kind::Keep) && has(Kind::Barracks);
		case Kind::Tower: return has(Kind::LumberMill);
		case Kind::Keep: return has(Kind::Barracks) && !has(Kind::Keep);
		case Kind::Castle:
			return has(Kind::Keep) && has(Kind::Stables) && has(Kind::Blacksmith) && has(Kind::LumberMill)
			       && !has(Kind::Castle);
		case Kind::MageTower:
		case Kind::Church:
		case Kind::Aviary: return has(Kind::Castle);
		default: return false; // the sea and the rest: not on this land map
	}
}

/// The toy economy behind a bot, from a start with one worker and a hall (the classic maps'
/// "Units: Map Default"). It plays the engine's part as the bot meets it in the game:
/// - the request table: every kind released is a want; wants are worked off in the order asked,
///   each once its price is there (a unit also needs its trainer free and food, a building a
///   worker - an idle one, else one that gathers);
/// - a building counts once finished (as the engine's count), a unit too; a unit's food is used
///   from the start of its training;
/// - the engine's own farm when the food is used up and no farm is asked (unless held);
/// - workers: an idle one goes to lumber while fewer cut it than the bot wants, else to gold; one
///   gathering worker a second moves between gold and lumber when the bot's number changed (the
///   engine's rebalance), each assignment an order of the bot's rate window;
/// - a gold trip brings 100 gold in 18 s, a lumber trip 100 wood in 30 s; a worker switched loses
///   its trip.
/// Units sent somewhere stand there at once (as ToyGame); nobody fights.
struct EcoGame {
	enum Task { Idle, Gold, Wood, Build, Away };
	struct Peasant {
		int slot = -1;
		Task task = Idle;
		int trip = 0;      ///< seconds into the current trip
		long until = -1;   ///< end of a walk away
	};
	struct Want {
		Kind kind = Kind::Worker;
		bool started = false;
		long doneAt = -1;
		int peasant = -1;  ///< the builder's slot
	};

	World world;
	std::vector<Peasant> peasants;
	std::vector<Want> wants;
	int done[KindCount] = {};        ///< finished
	int begun[KindCount] = {};       ///< buildings begun (finished ones in; the test's record only)
	long hallBusyUntil = -1;
	long barracksBusyUntil[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
	long mageBusyUntil = -1;
	std::vector<std::pair<Line, int>> research;  ///< asked, not paid yet
	int nextSlot = 10;
	int trainingFood = 0;
	long noPlaceUntil[KindCount] = {}; ///< a try to place the kind finds no place before this second
	long hallSiteUntil = -1;           ///< the start's hall is a site, its only worker inside, until this second
	// what the test looks at
	std::vector<std::pair<long, Kind>> begunAt;  ///< every building begun, with its second
	long idleWorkerSeconds = 0;
	int longestIdle = 0;                          ///< longest stretch any worker stood idle (s)
	std::vector<int> idleRun;
	long goldTrips = 0;
	long woodTrips = 0;
	long noIncomeRun = 0;
	long longestNoIncome = 0;                     ///< seconds with no worker on gold, gold < 900, workers < target
	long hallIdleRun = 0;
	long longestHallIdle = 0;                     ///< seconds the hall stood idle, gold >= 1000, food free, workers < target
	long barracksIdleRun = 0;
	long longestBarracksIdle = 0;                 ///< seconds every barracks stood idle, gold >= 2400, army < its size
	long spentGold[KindCount + 1] = {};           ///< gold paid per kind (the last: research), the test's record only
	long blockedFood = 0;                         ///< seconds a unit asked could not start for want of food
	long blockedFoodEarly = 0;                    ///< ... of them in the first 6 minutes, once a farm stands
	long foodRichRun = 0;
	long longestFoodBlockRich = 0;                ///< longest food block with 1500+ gold lying, once a farm stands
	long woodNoGoldRun = 0;
	long longestWoodNoGold = 0;                   ///< longest stretch with 1000+ wood and under 200 gold

	EcoGame(int gold, int wood)
	{
		world.gold = gold;
		world.wood = wood;
		world.home = {10, 10};
		world.enemyStarts = {{60, 60}, {40, 50}};
		world.mapWidth = 64;
		world.mapHeight = 64;
		done[KindIndex(Kind::Hall)] = 1;
		begun[KindIndex(Kind::Hall)] = 1;
		Peasant first;
		first.slot = nextSlot++;
		peasants.push_back(first);
		for (int k = 0; k < KindCount; ++k) {
			const Price p = PriceOf(static_cast<Kind>(k));
			world.goldCost[k] = p.gold;
			world.woodCost[k] = p.wood;
		}
		for (int l = 0; l < LineCount; ++l) {
			world.researchGold[l] = 800;
		}
		Sync();
	}

	/// A classic map's start: the hall is a site its only worker builds until `until`.
	void HallSite(long until)
	{
		done[KindIndex(Kind::Hall)] = 0;
		hallSiteUntil = until;
		peasants[0].task = Build;
		Sync();
	}

	/// A classic map's start with Units: Map Default: one peasant and no hall; the bot places and pays
	/// its hall (5000 / 2000 on Resources Medium).
	void NoHall()
	{
		done[KindIndex(Kind::Hall)] = 0;
		begun[KindIndex(Kind::Hall)] = 0;
		Sync();
	}

	int Supply() const { return std::min(200, done[KindIndex(Kind::Hall)] + 4 * done[KindIndex(Kind::Farm)]); }
	int Units() const
	{
		int n = static_cast<int>(peasants.size());
		for (const Kind kind : FighterKindList) {
			n += done[KindIndex(kind)];
		}
		return n;
	}

	/// The bot's view of this second.
	void Sync()
	{
		for (int k = 0; k < KindCount; ++k) {
			const Kind kind = static_cast<Kind>(k);
			world.have[k] = done[k]; // the engine counts a building once it is finished
			world.queued[k] = 0;
			world.waiting[k] = 0;
			world.unplaced[k] = false;
			world.can[k] = Allowed(done, kind) && (!IsBuilding(kind) || !peasants.empty());
		}
		world.have[KindIndex(Kind::Worker)] = static_cast<int>(peasants.size());
		world.have[KindIndex(Kind::Keep)] = done[KindIndex(Kind::Keep)];
		world.have[KindIndex(Kind::Castle)] = done[KindIndex(Kind::Castle)];
		world.hallSites = hallSiteUntil >= 0 ? 1 : 0;
		for (const Want &w : wants) {
			if (w.kind == Kind::Hall && w.started && done[KindIndex(Kind::Hall)] == 0) {
				world.hallSites = 1; // its own first hall being built
			}
		}
		for (const Want &w : wants) {
			++world.queued[KindIndex(w.kind)];
			if (!w.started && IsBuilding(w.kind)) {
				++world.waiting[KindIndex(w.kind)];
			}
			if (!w.started && IsBuilding(w.kind)
			    && (world.second < noPlaceUntil[KindIndex(w.kind)] || (w.kind != Kind::Hall && done[KindIndex(Kind::Hall)] == 0))) {
				world.unplaced[KindIndex(w.kind)] = true;
			}
		}
		world.supply = Supply();
		world.demand = Units() + trainingFood;
		for (int l = 0; l < LineCount; ++l) {
			const Line line = static_cast<Line>(l);
			const bool pending =
				std::any_of(research.begin(), research.end(), [line](const std::pair<Line, int> &r) { return r.first == line; });
			bool building = false;
			if (line == Line::Weapons || line == Line::Armour) {
				building = done[KindIndex(Kind::Blacksmith)] > 0;
			} else if (line == Line::Missile) {
				building = done[KindIndex(Kind::LumberMill)] > 0;
			} else if (line == Line::Elite) {
				building = done[KindIndex(Kind::Castle)] > 0;
			} else if (line == Line::ShipWeapons || line == Line::ShipArmour) {
				building = false;
			} else {
				building = done[KindIndex(Kind::Castle)] > 0
				           && done[KindIndex(Kind::MageTower)] + done[KindIndex(Kind::Church)] > 0;
			}
			const bool secondLevel = world.level[l] >= 1;
			world.canResearch[l] = building && !pending && world.level[l] < LineLevels(line)
			                       && (!secondLevel || done[KindIndex(Kind::Keep)] > 0);
		}
		world.workers.clear();
		for (const Peasant &p : peasants) {
			if (p.task != Away) {
				Worker w;
				w.slot = p.slot;
				w.at = {11, 11};
				w.idle = p.task == Idle;
				world.workers.push_back(w);
			}
		}
	}

	bool Pay(int gold, int wood)
	{
		if (world.gold < gold || world.wood < wood) {
			return false;
		}
		world.gold -= gold;
		world.wood -= wood;
		return true;
	}

	/// A worker for a building: an idle one, else one that gathers (the engine's fallback).
	int Builder() const
	{
		for (size_t i = 0; i < peasants.size(); ++i) {
			if (peasants[i].task == Idle) {
				return static_cast<int>(i);
			}
		}
		for (size_t i = 0; i < peasants.size(); ++i) {
			if (peasants[i].task == Gold || peasants[i].task == Wood) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int OnTask(Task task) const
	{
		return static_cast<int>(
			std::count_if(peasants.begin(), peasants.end(), [task](const Peasant &p) { return p.task == task; }));
	}

	void Step(const Profile &profile, State &state, const Decision &decision)
	{
		const long now = world.second;
		for (int k = 0; k < KindCount; ++k) {
			if (decision.release[k]) {
				Want w;
				w.kind = static_cast<Kind>(k);
				wants.push_back(w);
			}
		}
		for (const Kind upgrade : {Kind::Keep, Kind::Castle}) {
			if (upgrade == Kind::Keep ? decision.keep : decision.castle) {
				Want w;
				w.kind = upgrade;
				wants.push_back(w);
			}
		}
		for (const std::pair<Line, int> &r : decision.research) {
			research.push_back(r);
		}
		for (const Command &command : decision.commands) {
			for (Fighter &fighter : world.fighters) {
				if (fighter.slot == command.slot) {
					fighter.at = command.to;
					fighter.idle = true;
				}
			}
			for (Peasant &p : peasants) {
				if (p.slot == command.slot && p.task != Build) {
					p.task = Away; // a scout: back and idle a minute later
					p.trip = 0;
					p.until = now + 60;
				}
			}
		}

		// The engine's own farm when the food is used up (no farm asked, none held).
		const bool farmAsked =
			std::any_of(wants.begin(), wants.end(), [](const Want &w) { return w.kind == Kind::Farm; });
		if (!farmAsked && !decision.holdFarms && Supply() <= Units() + trainingFood && Allowed(done, Kind::Farm)
		    && world.gold >= PriceOf(Kind::Farm).gold && world.wood >= PriceOf(Kind::Farm).wood && Builder() >= 0
		    && state.rate.Take(now, profile.ordersPerMinute)) {
			Want w;
			w.kind = Kind::Farm;
			wants.insert(wants.begin(), w);
		}

		// Production in the order asked.
		bool foodBlocked = false;
		for (Want &w : wants) {
			if (w.started) {
				continue;
			}
			const Price p = PriceOf(w.kind);
			if (w.kind == Kind::Keep || w.kind == Kind::Castle) {
				if (hallBusyUntil < now && Allowed(done, w.kind) && Pay(p.gold, p.wood)) {
					spentGold[KindIndex(w.kind)] += p.gold;
					w.started = true;
					w.doneAt = now + p.seconds;
					hallBusyUntil = w.doneAt;
				}
				continue;
			}
			if (IsBuilding(w.kind)) {
				const int b = Builder();
				// no hall: no place for anything else (the bot's places are round its hall; usC8: the
				// farm asked before the hall stood found none)
				const bool place = w.kind == Kind::Hall || done[KindIndex(Kind::Hall)] > 0;
				if (!place && b >= 0 && now >= noPlaceUntil[KindIndex(w.kind)]) {
					noPlaceUntil[KindIndex(w.kind)] = now + 15; // the engine's next try 450 cycles later
				}
				if (b >= 0 && place && now >= noPlaceUntil[KindIndex(w.kind)] && Allowed(done, w.kind) && Pay(p.gold, p.wood)) {
					spentGold[KindIndex(w.kind)] += p.gold;
					w.started = true;
					w.doneAt = now + p.seconds;
					w.peasant = peasants[b].slot;
					peasants[b].task = Build;
					peasants[b].trip = 0;
					++begun[KindIndex(w.kind)];
					begunAt.emplace_back(now, w.kind);
				}
				continue;
			}
			// a unit: its trainer free, food for it
			long *trainer = nullptr;
			if (w.kind == Kind::Worker) {
				trainer = &hallBusyUntil;
			} else if (w.kind == Kind::Caster) {
				trainer = &mageBusyUntil;
			} else {
				for (int i = 0; i < std::min(8, done[KindIndex(Kind::Barracks)]); ++i) {
					if (barracksBusyUntil[i] < now) {
						trainer = &barracksBusyUntil[i];
						break;
					}
				}
			}
			if (trainer != nullptr && *trainer < now && Allowed(done, w.kind) && Supply() - Units() - trainingFood < 1) {
				foodBlocked = true;
			}
			if (trainer == nullptr || *trainer >= now || !Allowed(done, w.kind) || Supply() - Units() - trainingFood < 1
			    || !Pay(p.gold, p.wood)) {
				continue;
			}
			spentGold[KindIndex(w.kind)] += p.gold;
			w.started = true;
			w.doneAt = now + p.seconds;
			*trainer = w.doneAt;
			++trainingFood;
		}
		// Research: paid when the gold is there (the request stays until then), done at once.
		for (auto it = research.begin(); it != research.end();) {
			if (Pay(800, 0)) {
				spentGold[KindCount] += 800;
				world.level[static_cast<int>(it->first)] = it->second;
				it = research.erase(it);
			} else {
				++it;
			}
		}

		if (foodBlocked) {
			++blockedFood;
			// the first farm's block (the hall gives one food) is no matter of the bot's farms
			blockedFoodEarly += now < 360 && done[KindIndex(Kind::Farm)] > 0 ? 1 : 0;
		}
		foodRichRun = foodBlocked && world.gold >= 1500 && done[KindIndex(Kind::Farm)] > 0 ? foodRichRun + 1 : 0;
		longestFoodBlockRich = std::max(longestFoodBlockRich, foodRichRun);
		woodNoGoldRun = world.wood >= 1000 && world.gold < 200 ? woodNoGoldRun + 1 : 0;
		longestWoodNoGold = std::max(longestWoodNoGold, woodNoGoldRun);

		// Ends.
		if (hallSiteUntil >= 0 && now >= hallSiteUntil) {
			done[KindIndex(Kind::Hall)] = 1;
			hallSiteUntil = -1;
			peasants[0].task = Idle;
		}
		for (auto it = wants.begin(); it != wants.end();) {
			if (!it->started || it->doneAt > now) {
				++it;
				continue;
			}
			const Kind kind = it->kind;
			++done[KindIndex(kind)];
			if (kind == Kind::Worker) {
				Peasant p;
				p.slot = nextSlot++;
				peasants.push_back(p);
				--trainingFood;
			} else if (IsFighter(kind)) {
				Fighter f;
				f.slot = nextSlot++;
				f.at = {11, 11};
				f.idle = true;
				f.kind = kind;
				world.fighters.push_back(f);
				--trainingFood;
			}
			for (Peasant &p : peasants) {
				if (p.slot == it->peasant) {
					p.task = Idle;
				}
			}
			it = wants.erase(it);
		}
		for (Peasant &p : peasants) {
			if (p.task == Away && p.until <= now) {
				p.task = Idle;
			}
		}

		// Gathering: the engine's assignment of idle workers, then one move a second.
		const int wanted = decision.lumberWorkers;
		for (Peasant &p : peasants) {
			if (p.task != Idle || !state.rate.CanTake(now, profile.ordersPerMinute)) {
				continue;
			}
			p.task = OnTask(Wood) < wanted ? Wood : Gold;
			p.trip = 0;
			state.rate.Take(now, profile.ordersPerMinute);
		}
		const auto move = [&](Task from, Task to) {
			for (Peasant &p : peasants) {
				// carries nothing: early in its trip
				if (p.task == from && p.trip < 5 && state.rate.CanTake(now, profile.ordersPerMinute)) {
					p.task = to;
					p.trip = 0;
					state.rate.Take(now, profile.ordersPerMinute);
					return;
				}
			}
		};
		if (OnTask(Wood) > wanted) {
			move(Wood, Gold);
		} else if (OnTask(Wood) < wanted && OnTask(Gold) > 1) {
			move(Gold, Wood);
		}
		bool goldCame = false;
		for (Peasant &p : peasants) {
			if (p.task == Gold && ++p.trip >= 18) {
				p.trip = 0;
				world.gold += 100;
				++goldTrips;
				goldCame = true;
			} else if (p.task == Wood && ++p.trip >= 30) {
				p.trip = 0;
				world.wood += 100;
				++woodTrips;
			}
		}

		// What the test looks at.
		idleRun.resize(peasants.size(), 0);
		for (size_t i = 0; i < peasants.size(); ++i) {
			if (peasants[i].task == Idle) {
				++idleWorkerSeconds;
				longestIdle = std::max(longestIdle, ++idleRun[i]);
			} else {
				idleRun[i] = 0;
			}
		}
		// starving: no worker on gold while short of gold and of workers, with workers cutting wood or
		// standing idle (the round's deadlock; a lone worker building the first farm is not)
		const int cap = done[KindIndex(Kind::Keep)] > 0 ? profile.workersAfterKeep : profile.workers;
		const bool fewWorkers = static_cast<int>(peasants.size()) < cap;
		if (goldCame || world.gold >= 900 || !fewWorkers || OnTask(Gold) > 0 || OnTask(Wood) + OnTask(Idle) == 0) {
			noIncomeRun = 0;
		} else {
			longestNoIncome = std::max(longestNoIncome, ++noIncomeRun);
		}
		// gold lying unspent: the hall idle with gold for a worker while workers are short; every
		// barracks idle with gold for a fighter while the army is below its size
		if (fewWorkers && hallBusyUntil < now && world.gold >= 1000 && Supply() - Units() - trainingFood >= 1) {
			longestHallIdle = std::max(longestHallIdle, ++hallIdleRun);
		} else {
			hallIdleRun = 0;
		}
		int army = 0;
		for (const Kind kind : FighterKindList) {
			army += done[KindIndex(kind)];
		}
		bool barracksIdle = done[KindIndex(Kind::Barracks)] > 0;
		for (int i = 0; i < std::min(8, done[KindIndex(Kind::Barracks)]); ++i) {
			barracksIdle = barracksIdle && barracksBusyUntil[i] < now;
		}
		if (barracksIdle && army + trainingFood < profile.armyMax && world.gold >= 1500 + 900) {
			longestBarracksIdle = std::max(longestBarracksIdle, ++barracksIdleRun);
		} else {
			barracksIdleRun = 0;
		}

		world.alarm = false;
		++world.second;
		Sync();
	}
};

struct EcoResult {
	int workersAt[16] = {};          ///< workers at the start of each minute 0..15
	int armyAt[16] = {};             ///< fighters standing at the start of each minute 0..15
	int peakGold = 0;
	int goldAtEnd = 0;
	long firstWave = -1;
	size_t firstWaveSize = 0;
	int waves = 0;
	int maxWindow = 0;
	EcoGame game{0, 0};
};

/// 15 game minutes of a profile from one worker and a hall (with hallSite > 0: the hall a site its
/// only worker builds until that second, as on the classic maps).
EcoResult PlayFromOneWorker(const Profile &profile, int gold, int wood, long hallSite = 0)
{
	EcoResult result;
	result.game = EcoGame(gold, wood);
	EcoGame &game = result.game;
	if (hallSite > 0) {
		game.HallSite(hallSite);
	} else if (hallSite < 0) {
		game.NoHall(); // one peasant, no hall: the bot places its own
	}
	State state;
	SeededDice dice;
	std::vector<Decision> decisions;
	for (int s = 0; s < 900; ++s) {
		if (s % 60 == 0) {
			result.workersAt[s / 60] = static_cast<int>(game.peasants.size());
			result.armyAt[s / 60] = static_cast<int>(game.world.fighters.size());
		}
		const int waves = state.waves;
		decisions.push_back(Decide(profile, game.world, state, Roll, &dice));
		if (state.waves > waves && result.firstWave < 0) {
			result.firstWave = game.world.second;
			result.firstWaveSize = state.wave.size();
		}
		game.Step(profile, state, decisions.back());
		result.peakGold = std::max(result.peakGold, game.world.gold);
	}
	result.workersAt[15] = static_cast<int>(game.peasants.size());
	result.armyAt[15] = static_cast<int>(game.world.fighters.size());
	result.goldAtEnd = game.world.gold;
	result.waves = state.waves;
	result.maxWindow = MaxWindow(decisions);
	return result;
}

/// The second the n-th (from 1) building of a kind was begun, -1 never.
long BegunAt(const EcoGame &game, Kind kind, int n = 1)
{
	for (const std::pair<long, Kind> &b : game.begunAt) {
		if (b.second == kind && --n == 0) {
			return b.first;
		}
	}
	return -1;
}

std::string EcoLine(const Profile &profile, const EcoResult &r)
{
	std::string text = profile.id + ": workers/min";
	for (const int w : r.workersAt) {
		text += " " + std::to_string(w);
	}
	text += " | army/min";
	for (const int a : r.armyAt) {
		text += " " + std::to_string(a);
	}
	text += " | built";
	for (const std::pair<long, Kind> &b : r.game.begunAt) {
		text += " " + std::string(KindName(b.second)) + "@" + std::to_string(b.first);
	}
	text += " | wave " + std::to_string(r.firstWave) + " x" + std::to_string(r.firstWaveSize) + ", waves "
	        + std::to_string(r.waves) + " | gold end " + std::to_string(r.goldAtEnd) + " peak "
	        + std::to_string(r.peakGold) + " wood end " + std::to_string(r.game.world.wood) + " | idle "
	        + std::to_string(r.game.idleWorkerSeconds) + " s (longest " + std::to_string(r.game.longestIdle)
	        + ") | no gold " + std::to_string(r.game.longestNoIncome) + " s | hall idle rich "
	        + std::to_string(r.game.longestHallIdle) + " s | barracks idle rich " + std::to_string(r.game.longestBarracksIdle)
	        + " s | food blocked " + std::to_string(r.game.blockedFood) + " s (first 6 min " + std::to_string(r.game.blockedFoodEarly) + ", rich " + std::to_string(r.game.longestFoodBlockRich) + ") | wood no gold " + std::to_string(r.game.longestWoodNoGold) + " s | trips gold "
	        + std::to_string(r.game.goldTrips) + " wood " + std::to_string(r.game.woodTrips) + " | window "
	        + std::to_string(r.maxWindow);
	text += " | gold to";
	for (int k = 0; k <= KindCount; ++k) {
		if (r.game.spentGold[k] > 0) {
			text += " " + std::string(k < KindCount ? KindName(static_cast<Kind>(k)) : "research") + "="
			        + std::to_string(r.game.spentGold[k]);
		}
	}
	return text;
}
} // namespace

TEST_CASE("wc2bot: lumber - never every worker; one worker mines gold, half while short of the next worker")
{
	const Profile captain = Named("captain");
	SeededDice dice;
	World world = StartWorld();
	for (int k = 0; k < KindCount; ++k) {
		world.goldCost[k] = PriceOf(static_cast<Kind>(k)).gold;
		world.woodCost[k] = PriceOf(static_cast<Kind>(k)).wood;
	}
	// round 3.4.65: two workers, the profile's 3 on lumber, gold 0 - before, both cut wood for ever
	world.wood = 2400;
	world.gold = 0;
	world.have[KindIndex(Kind::Worker)] = 2;
	State a = Idle(captain);
	CHECK(Decide(captain, world, a, Roll, &dice).lumberWorkers == 0);
	world.have[KindIndex(Kind::Worker)] = 1;
	CHECK(Decide(captain, world, a, Roll, &dice).lumberWorkers == 0);
	// six workers, gold for the next one there: the share (35 % of 6 = 2), never all
	world.have[KindIndex(Kind::Worker)] = 6;
	world.gold = 900;
	world.wood = 600;
	CHECK(Decide(captain, world, a, Roll, &dice).lumberWorkers == 2);
	// gold short of the next worker: half of them mine gold (3 of 6), the share stays 2
	world.gold = 100;
	CHECK(Decide(captain, world, a, Roll, &dice).lumberWorkers == 2);
	world.wood = 2000; // and wood piling up: a quarter (one) fewer on lumber
	CHECK(Decide(captain, world, a, Roll, &dice).lumberWorkers == 1);
	// gold lying unspent while wood is short: a quarter more to lumber (35 % of 12 = 4, + 3)
	world.have[KindIndex(Kind::Worker)] = 12;
	world.gold = 5000;
	world.wood = 100;
	CHECK(Decide(captain, world, a, Roll, &dice).lumberWorkers == 7);
	// the Recruit's share (35 %) at its twelve workers
	const Profile recruit = Recruit();
	State b = Idle(recruit);
	world.gold = 1000;
	world.wood = 600;
	CHECK(Decide(recruit, world, b, Roll, &dice).lumberWorkers == 4);
}

TEST_CASE("wc2bot: below its workers the bot keeps the next worker's and its farm's gold; a farm first when food is out")
{
	const Profile captain = Named("captain");
	SeededDice dice;
	World world = StartWorld();
	for (int k = 0; k < KindCount; ++k) {
		world.goldCost[k] = PriceOf(static_cast<Kind>(k)).gold;
		world.woodCost[k] = PriceOf(static_cast<Kind>(k)).wood;
	}
	world.wood = 2000;
	world.have[KindIndex(Kind::Barracks)] = 1;
	world.have[KindIndex(Kind::Worker)] = 2;
	world.queued[KindIndex(Kind::Worker)] = 1; // one in training: no new worker this second
	world.supply = 9;
	world.demand = 3;
	for (int l = 0; l < LineCount; ++l) {
		world.canResearch[l] = false;
	}
	State state = Idle(captain);
	state.planned[KindIndex(Kind::Barracks)] = 1;
	// footman 600 + worker 400 kept: 999 gold buys nothing, 1000 the footman
	world.gold = 999;
	Decision d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Soldier)] == 0);
	world.second = 1;
	world.gold = 1000;
	d = Decide(captain, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Soldier)] == 1);
	// research waits the same way (800 + 400)
	State r = Idle(captain);
	r.research = 0;
	r.planned[KindIndex(Kind::Barracks)] = 1;
	world.can[KindIndex(Kind::Soldier)] = false;
	world.canResearch[static_cast<int>(Line::Weapons)] = true;
	world.researchGold[static_cast<int>(Line::Weapons)] = 800;
	world.second = 300;
	world.gold = 1199;
	CHECK(Decide(captain, world, r, Roll, &dice).research.empty());
	world.second = 301;
	world.gold = 1200;
	CHECK(Decide(captain, world, r, Roll, &dice).research.size() == 1);
	// food out, no farm asked: a farm first, and its price is kept from the rest too
	State f = Idle(captain);
	f.planned[KindIndex(Kind::Barracks)] = 1;
	world.can[KindIndex(Kind::Soldier)] = true;
	world.canResearch[static_cast<int>(Line::Weapons)] = false;
	world.supply = 3;
	world.queued[KindIndex(Kind::Worker)] = 1;
	world.gold = 1400; // worker 400 + farm 500 kept: no footman (600)
	world.second = 400;
	d = Decide(captain, world, f, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Farm)] == 1);
	CHECK(d.release[KindIndex(Kind::Soldier)] == 0);
	// at the workers' number nothing is kept
	world.have[KindIndex(Kind::Worker)] = captain.workers;
	world.supply = 30;
	world.demand = 21;
	world.gold = 600;
	world.second = 401;
	State g = Idle(captain);
	g.planned[KindIndex(Kind::Barracks)] = 1;
	CHECK(Decide(captain, world, g, Roll, &dice).release[KindIndex(Kind::Soldier)] == 1);
}

TEST_CASE("wc2bot: from one worker the opening starts at once - a worker, the farm, the barracks only once the farm stands")
{
	for (const char *id : {"recruit", "captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		EcoGame game(3800, 1200);
		State state;
		SeededDice dice;
		std::vector<Decision> decisions;
		for (int s = 0; s < 240; ++s) {
			decisions.push_back(Decide(profile, game.world, state, Roll, &dice));
			game.Step(profile, state, decisions.back());
		}
		CHECK(decisions[0].release[KindIndex(Kind::Worker)] == 1); // the worker asked at once
		REQUIRE_FALSE(game.begunAt.empty());
		CHECK(game.begunAt[0].second == Kind::Farm);
		CHECK(game.begunAt[0].first <= 2);                        // the farm at once (the round: ~195 s)
		const long barracks = BegunAt(game, Kind::Barracks);
		REQUIRE(barracks >= 0);
		CHECK(barracks > game.begunAt[0].first);
		CHECK(barracks <= 60);                                    // the round: ~110 s, before the farm
		int early = 0;
		for (size_t s = 0; s < 2; ++s) {
			early += decisions[s].release[KindIndex(Kind::Barracks)];
		}
		CHECK(early == 0);                                        // not beside the farm still to come
		CHECK(game.peasants.size() >= 4);                         // workers from the first farm on
	}
}

TEST_CASE("wc2bot: from one worker a farm that finds no place at first is still the first building")
{
	// round 3.4.65: the opening's farm found no place for a while and the barracks asked a second
	// later took the only worker first. Now the barracks is asked once the farm stands - or after
	// the farm has found no place for 20 s (a building is better than none).
	for (const char *id : {"recruit", "captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		for (const long noPlace : {12L, 60L}) {
			CAPTURE(noPlace);
			EcoGame game(3800, 1200);
			game.noPlaceUntil[KindIndex(Kind::Farm)] = noPlace;
			State state;
			SeededDice dice;
			for (int s = 0; s < 240; ++s) {
				const Decision d = Decide(profile, game.world, state, Roll, &dice);
				game.Step(profile, state, d);
			}
			const long farm = BegunAt(game, Kind::Farm);
			const long barracks = BegunAt(game, Kind::Barracks);
			REQUIRE(farm >= 0);
			REQUIRE(barracks >= 0);
			if (noPlace < 20) {
				CHECK(farm == noPlace);
				CHECK(barracks > farm);
			} else {
				CHECK(barracks >= 20); // after the farm's 20 s without a place, not at once
				CHECK(barracks <= 25);
			}
		}
	}
}

TEST_CASE("wc2bot: a hall site at the start - nothing asked until it stands, the farm and the worker within a second of it")
{
	// check 0.2.1 (Unyielding, Units: Map Default): the start's hall is a site with the only worker
	// inside for ~103 s; the bot asked the farm from second 2 (no worker, no place) and a second hall.
	for (const char *id : {"recruit", "captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		const long site = 100;
		EcoGame game(3800, 1200);
		game.HallSite(site);
		State state;
		SeededDice dice;
		long farmAsked = -1;
		long workerAsked = -1;
		int askedDuringSite = 0;
		bool heldDuringSite = true;
		for (int s = 0; s < 240; ++s) {
			const Decision d = Decide(profile, game.world, state, Roll, &dice);
			if (s <= site) {
				for (int k = 0; k < KindCount; ++k) {
					askedDuringSite += d.release[k];
				}
				askedDuringSite += d.keep + d.castle;
				heldDuringSite = heldDuringSite && d.holdFarms;
			}
			if (farmAsked < 0 && d.release[KindIndex(Kind::Farm)]) {
				farmAsked = s;
			}
			if (workerAsked < 0 && d.release[KindIndex(Kind::Worker)]) {
				workerAsked = s;
			}
			game.Step(profile, state, d);
		}
		CHECK(askedDuringSite == 0);          // no farm, no second hall, no worker while the hall is a site
		CHECK(heldDuringSite);                // nor the engine's own farm
		CHECK(farmAsked == site + 1);         // the second after the hall stands
		CHECK(workerAsked == site + 1);
		const long farm = BegunAt(game, Kind::Farm);
		CHECK(farm == site + 1);              // begun at once by the worker out of the hall
		CHECK(BegunAt(game, Kind::Hall) < 0); // never a second hall
		CHECK(game.done[KindIndex(Kind::Hall)] == 1);
		const long barracks = BegunAt(game, Kind::Barracks);
		REQUIRE(barracks >= 0);
		CHECK(barracks > farm);
		CHECK(state.unplacedSince[KindIndex(Kind::Farm)] < 0);
	}
	// a hall standing beside the site (a hall lost and one rebuilt): the economy is not held
	{
		const Profile captain = Named("captain");
		EcoGame game(3800, 1200);
		game.world.hallSites = 1;
		State state;
		SeededDice dice;
		const Decision d = Decide(captain, game.world, state, Roll, &dice);
		CHECK(d.release[KindIndex(Kind::Worker)] == 1);
	}
}

TEST_CASE("wc2bot: the main mine is low only from a real read of its gold, never from a missing read")
{
	const Profile captain = Named("captain");
	SeededDice dice;
	State state = Idle(captain);
	state.planned[KindIndex(Kind::Hall)] = 1;
	World world = QuietWorld();
	world.can[KindIndex(Kind::Hall)] = true;
	world.mineGold = 10000;
	world.second = 290;
	CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Hall)] == 0);
	REQUIRE(state.mineFirst == 10000);
	// not read for a while (BOTS_FIX_3465: counted as empty, the Captain's hall at ~5 min)
	for (long s = 291; s < 320; ++s) {
		world.second = s;
		world.mineGold = -1;
		CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Hall)] == 0);
	}
	CHECK(state.expansions == 0);
	CHECK(state.mineFirst == 10000);
	// read again, well filled: still nothing
	world.second = 320;
	world.mineGold = 9000;
	CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Hall)] == 0);
	// the mine gone (a real read: no gold mine near home any more): empty, the expansion
	world.second = 321;
	world.mineGold = 0;
	CHECK(Decide(captain, world, state, Roll, &dice).release[KindIndex(Kind::Hall)] == 1);
	CHECK(state.expansions == 1);
}

namespace
{
struct FakePlaces {
	int firstFit = 0;       ///< the smallest radius with a place (-1: none anywhere; 0: only without a limit)
	std::vector<int> tried;
};
bool FakeFits(int radius, void *context)
{
	FakePlaces &places = *static_cast<FakePlaces *>(context);
	places.tried.push_back(radius);
	if (places.firstFit < 0) {
		return false;
	}
	return radius == 0 ? true : (places.firstFit > 0 && radius >= places.firstFit);
}
} // namespace

TEST_CASE("wc2bot: a bot's building goes near its hall - the smallest radius that fits, growing only when none does")
{
	REQUIRE(std::size(NearHallRadii) >= 2);
	CHECK(NearHallRadii[0] > 0);
	CHECK(NearHallRadii[0] <= 8);                          // the first try is beside the hall
	CHECK(NearHallRadii[std::size(NearHallRadii) - 1] == 0); // the last has no limit
	for (size_t i = 1; i + 1 < std::size(NearHallRadii); ++i) {
		CHECK(NearHallRadii[i] > NearHallRadii[i - 1]);
	}
	{
		FakePlaces places{1, {}};                           // room right beside the hall
		int used = -1;
		CHECK(PlaceNearHall(FakeFits, &places, &used));
		CHECK(used == NearHallRadii[0]);
		CHECK(places.tried.size() == 1);                   // nothing further looked at
	}
	{
		FakePlaces places{NearHallRadii[1], {}};            // the first ring full
		int used = -1;
		CHECK(PlaceNearHall(FakeFits, &places, &used));
		CHECK(used == NearHallRadii[1]);
		CHECK(places.tried.size() == 2);
	}
	{
		FakePlaces places{0, {}};                           // room only far away
		int used = -1;
		CHECK(PlaceNearHall(FakeFits, &places, &used));
		CHECK(used == 0);
		CHECK(places.tried.size() == std::size(NearHallRadii));
	}
	{
		FakePlaces places{-1, {}};                          // no room round the hall at all
		CHECK_FALSE(PlaceNearHall(FakeFits, &places));
		CHECK(places.tried.size() == std::size(NearHallRadii));
	}
}

TEST_CASE("wc2bot: 15 game minutes from one worker - workers, buildings in order, first wave on time, nothing stuck")
{
	for (const char *id : {"recruit", "captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		const EcoResult r = PlayFromOneWorker(profile, 3800, 1200);
		const EcoGame &game = r.game;
		MESSAGE(EcoLine(profile, r));

		// workers: the second soon after the farm, then on to the profile's number
		CHECK(r.workersAt[2] >= 3);
		CHECK(r.workersAt[15] >= std::min(profile.workers, 12));
		const int cap = game.done[KindIndex(Kind::Keep)] > 0 ? profile.workersAfterKeep : profile.workers;
		CHECK(r.workersAt[15] <= cap);
		if (profile.id != "recruit") {
			CHECK(r.workersAt[10] >= profile.workers * 3 / 4);
		}

		// buildings: the opening's first ones in its order (hall given, farm, barracks, farm)
		const long farm1 = BegunAt(game, Kind::Farm, 1);
		const long barracks = BegunAt(game, Kind::Barracks, 1);
		const long farm2 = BegunAt(game, Kind::Farm, 2);
		REQUIRE(farm1 >= 0);
		REQUIRE(barracks >= 0);
		REQUIRE(farm2 >= 0);
		CHECK(farm1 < barracks);
		CHECK(barracks <= farm2);
		// the timed steps of the first 14 minutes: begun, not before their second
		for (const OpeningStep &step : profile.opening) {
			if (IsHallUpgrade(step.what) || step.atSecond <= 0 || step.atSecond >= 840) {
				continue;
			}
			CAPTURE(KindName(step.what));
			const long at = BegunAt(game, step.what, 1);
			CHECK(at >= step.atSecond);
			// not held up for ever by the gold kept back (+200: the fighters of the wave due come first, BOTS_FIX3)
			CHECK(at <= std::max<long>(step.atSecond, 300) + 200);
		}
		if (profile.id != "recruit") {
			CHECK(game.done[KindIndex(Kind::Keep)] == 1); // the keep stands by 15 minutes
		}

		// the first wave at the design's second (Warlord: the enemy start 40 tiles off is near)
		const long due = profile.id == "warlord" ? profile.nearFirstSecond : profile.firstWaveSecond;
		REQUIRE(r.firstWave >= 0);
		CHECK(r.firstWave >= due);
		CHECK(r.firstWave <= due + (profile.id == "warlord" ? 150 : 60)); // from one worker the Warlord's 5 min near rush is late
		CHECK(r.firstWaveSize >= static_cast<size_t>(profile.firstWaveMin));
		CHECK(r.waves >= 2);

		// nothing stuck: gold comes in and is spent while workers or fighters are wanted, no worker
		// stands about (a few seconds at the order cap)
		CHECK(game.longestNoIncome <= 30);
		CHECK(game.longestIdle <= 20);
		CHECK(game.idleWorkerSeconds * 100 <= 900L * r.workersAt[15]); // under 1 %
		CHECK(game.longestHallIdle <= 2 * 15 * profile.workerGapBuilds);
		CHECK(game.longestBarracksIdle <= 60);
		CHECK(game.woodTrips > 0);
		CHECK(r.maxWindow <= profile.ordersPerMinute);
	}
}

TEST_CASE("wc2bot: from one worker with little gold no bot is stuck")
{
	for (const char *id : {"recruit", "captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		const EcoResult r = PlayFromOneWorker(profile, 500, 300);
		MESSAGE(EcoLine(profile, r));
		CHECK(r.game.longestNoIncome <= 30);
		CHECK(r.game.longestIdle <= 50); // the Recruit's 30 orders a minute taken by its first wave's turns (fix 4)
		CHECK(r.game.longestBarracksIdle <= 60);
		CHECK(r.workersAt[15] >= std::min(profile.workers, 12));
		CHECK(BegunAt(r.game, Kind::Barracks) >= 0);
		CHECK(r.waves >= 1);
	}
}

TEST_CASE("wc2bot: from a hall site (classic map) first waves by the design's minutes, the harder level ends with the larger army")
{
	// round 3.4.66 (tablet, classic maps, Units: Map Default; work/c1102): the Warlord had 1-2 fighters
	// after 9-11 minutes, the Captain 3-6, the Recruit 8 - its gold went to the spare barracks, farms
	// and research first. The start: the hall a site its only worker builds for ~108 s.
	EcoResult r[3];
	const char *ids[3] = {"recruit", "captain", "warlord"};
	for (int i = 0; i < 3; ++i) {
		const Profile profile = Named(ids[i]);
		r[i] = PlayFromOneWorker(profile, 3800, 1200, 108);
		MESSAGE(EcoLine(profile, r[i]));
	}
	// the toy has no fights: nobody dies, so this is the plan's army, not a game's. Fix 4 (round
	// 3.4.67: waves 2-4 min late) traded some of the Warlord's later army for its first wave on time.
	CHECK(r[2].armyAt[8] + 2 >= r[1].armyAt[8]);
	CHECK(r[2].armyAt[10] + 2 >= r[1].armyAt[10]); // before fix 3: 4 against 5
	CHECK(r[2].armyAt[10] >= 5);                   // before fix 3: 4
	CHECK(r[2].armyAt[15] >= r[1].armyAt[15]);
	CHECK(r[1].armyAt[15] >= r[0].armyAt[15]);
	CHECK(r[1].armyAt[12] >= r[0].armyAt[12]);
	// pressure by the design's minutes: the Captain's first wave at ~7 min (from a 108 s hall site),
	// the Warlord's not later, the Recruit's ~10 min
	CHECK(r[1].firstWave <= 460);                  // fix 3: 501
	CHECK(r[2].firstWave <= r[1].firstWave + 15);  // fix 3: 475
	CHECK(r[1].firstWave <= r[0].firstWave);	for (int i = 0; i < 3; ++i) {
		CAPTURE(ids[i]);
		CHECK(r[i].game.longestBarracksIdle <= 60);
		if (i > 0) {
			CHECK(r[i].game.blockedFood <= (i == 2 ? 120 : 180)); // farms ahead of need (before: Captain 213 s, Warlord 284 s)
		}
	}
}
TEST_CASE("wc2bot: no fighter goes out alone - the scout is a fast unit only; a unit hit far from the base calls no defence")
{
	// round 3.4.66: the Captain's and the Warlord's only footman walked to the person's start as the
	// scout and died; the defence then sent the home units there one by one (radius 40 / 64).
	SeededDice dice;
	for (const char *id : {"captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		State state = Idle(profile);
		state.nextScoutAt = 420;
		World world = QuietWorld();
		world.home = {10, 10};
		world.enemyStarts = {{60, 10}};
		world.fighters = {Own(20, {11, 11}), Own(21, {11, 12}, Kind::Shooter)};
		world.second = 420;
		Decision d = Decide(profile, world, state, Roll, &dice);
		CHECK(d.commands.empty());              // footman and archer stay home
		CHECK(state.nextScoutAt > 420);         // looked again soon
		world.fighters.push_back(Own(22, {12, 12}, Kind::Cavalry));
		world.second = state.nextScoutAt;
		d = Decide(profile, world, state, Roll, &dice);
		REQUIRE(d.commands.size() == 1);
		CHECK(d.commands[0].slot == 22);        // the knight scouts

		// the knight hit at the enemy start, 50 tiles out: nobody is sent, the alarm is dropped
		state = Idle(profile);
		world.fighters = {Own(20, {11, 11}), Own(21, {11, 12}, Kind::Shooter), Own(22, {58, 10}, Kind::Cavalry)};
		world.alarm = true;
		world.alarmAt = {58, 10};
		world.alarmTo = {59, 10};
		for (long s = 500; s < 520; ++s) {
			world.second = s;
			d = Decide(profile, world, state, Roll, &dice);
			CHECK(d.commands.empty());
			world.alarm = false;
		}
		CHECK(state.alarmDue < 0);
		// a farm hit beside the hall: the home units answer after the delay
		world.alarm = true;
		world.alarmAt = {14, 12};
		world.alarmTo = {16, 12};
		int sent = 0;
		for (long s = 520; s < 540; ++s) {
			world.second = s;
			sent += static_cast<int>(Decide(profile, world, state, Roll, &dice).commands.size());
			world.alarm = false;
		}
		CHECK(sent >= 2);
		// a far hall's place is the base too
		state = Idle(profile);
		world.farHalls = {{50, 40}};
		world.alarm = true;
		world.alarmAt = {52, 41};
		world.alarmTo = {53, 41};
		world.fighters = {Own(20, {48, 40}), Own(21, {49, 40})};
		sent = 0;
		for (long s = 600; s < 620; ++s) {
			world.second = s;
			sent += static_cast<int>(Decide(profile, world, state, Roll, &dice).commands.size());
			world.alarm = false;
		}
		CHECK(sent >= 2);
	}
}

TEST_CASE("wc2bot: a spare barracks only while every barracks trains and the gold is still there; the army first when the wave is due")
{
	SeededDice dice;
	const Profile warlord = Named("warlord");
	State state = Idle(warlord);
	World world = QuietWorld();
	world.can[KindIndex(Kind::Barracks)] = true;
	world.can[KindIndex(Kind::Soldier)] = true;
	world.have[KindIndex(Kind::Barracks)] = 2;
	world.have[KindIndex(Kind::Blacksmith)] = 1;
	world.have[KindIndex(Kind::LumberMill)] = 1;
	state.planned[KindIndex(Kind::Barracks)] = 2;
	state.planned[KindIndex(Kind::Blacksmith)] = 1;
	state.planned[KindIndex(Kind::LumberMill)] = 1;
	world.goldCost[KindIndex(Kind::Barracks)] = 700;
	world.goldCost[KindIndex(Kind::Soldier)] = 600;
	world.supply = 40;
	world.demand = 10;
	world.gold = 1400;
	world.second = 300;
	// one barracks free: the footman, no barracks (before: the barracks at spend_gold 1000)
	Decision d = Decide(warlord, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Soldier)] == 1);
	CHECK(d.release[KindIndex(Kind::Barracks)] == 0);
	// both barracks train, 1400 gold: still the footman's gold first -> no barracks below 1000 + 600
	world.queued[KindIndex(Kind::Soldier)] = 1;
	world.second = 301;
	d = Decide(warlord, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Barracks)] == 0);
	world.gold = 1700;
	world.second = 302;
	d = Decide(warlord, world, state, Roll, &dice);
	CHECK(d.release[KindIndex(Kind::Soldier)] == 1);
	CHECK(d.release[KindIndex(Kind::Barracks)] == 1);

	// the wave due and the army short: a fighter is not held back by the next worker's gold
	const Profile captain = Named("captain");
	State s2 = Idle(captain);
	s2.nextWaveAt = 420;
	World w2 = QuietWorld();
	w2.can[KindIndex(Kind::Soldier)] = true;
	w2.can[KindIndex(Kind::Worker)] = true;
	w2.have[KindIndex(Kind::Barracks)] = 1;
	w2.have[KindIndex(Kind::Worker)] = 10;
	w2.goldCost[KindIndex(Kind::Soldier)] = 600;
	w2.goldCost[KindIndex(Kind::Worker)] = 400;
	w2.queued[KindIndex(Kind::Worker)] = 1;
	w2.supply = 30;
	w2.demand = 12;
	w2.gold = 700;
	w2.second = 290; // over two minutes before the wave: the worker's gold kept back
	CHECK(Decide(captain, w2, s2, Roll, &dice).release[KindIndex(Kind::Soldier)] == 0);
	w2.second = 330; // within the lead (120 s): the fighter
	CHECK(Decide(captain, w2, s2, Roll, &dice).release[KindIndex(Kind::Soldier)] == 1);
	// with the wave's least size at home it is the worker's gold again
	for (int i = 0; i < captain.firstWaveMax; ++i) {
		w2.fighters.push_back(Own(30 + i, {11, 11}));
	}
	w2.second = 371;
	CHECK(Decide(captain, w2, s2, Roll, &dice).release[KindIndex(Kind::Soldier)] == 0);
}
TEST_CASE("wc2bot: an expansion hall goes to a gold mine no hall serves, the nearest to home first")
{
	// Unyielding stone fortresses, check 3.4.66: start 87,84, the main hall at 78,83 (8 tiles off its
	// mine at 89,84, past the engine finder's 5-tile look); the Captain's second hall went to 69,83.
	const Point home{87, 84};
	std::vector<Mine> mines = {
		{{89, 84}, 25000, true},  // 0: the main mine, served by the main hall
		{{93, 33}, 25000, true},  // 1: free, 51 tiles
		{{37, 82}, 25000, true},  // 2: free, 50 tiles
		{{64, 7}, 0, true},       // 3: no gold left
		{{0, 51}, 25000, false},  // 4: not on the worker's ground
		{{3, 12}, 25000, true},   // 5: beside the enemy's hall
	};
	const std::vector<Point> halls = {{78, 83}, {8, 11}};
	CHECK(ExpansionMines(mines, halls, home) == std::vector<int>{2, 1});

	// the main hall on the far side of a mine 12 tiles off still serves it; 13 do not
	CHECK(ExpansionMines({{{90, 83}, 100, true}}, {{78, 83}}, home).empty());
	CHECK(ExpansionMines({{{91, 83}, 100, true}}, {{78, 83}}, home) == std::vector<int>{0});
	// a hall being built serves its mine as well (the halls list holds it), so a second one waits
	CHECK(ExpansionMines({{{37, 82}, 100, true}}, {{78, 83}, {41, 82}}, home).empty());
	// none left: nothing (the engine then finds no place; the hall never goes beside home)
	CHECK(ExpansionMines({}, halls, home).empty());
}

TEST_CASE("wc2bot: the debug log line holds the second, resources, food, units, what is asked and the wave")
{
	World world;
	world.second = 754;
	world.gold = 1400;
	world.wood = 725;
	world.demand = 25;
	world.supply = 29;
	world.have[KindIndex(Kind::Worker)] = 20;
	world.have[KindIndex(Kind::Hall)] = 2;
	world.queued[KindIndex(Kind::Farm)] = 1;
	world.fighters.resize(5);
	State state;
	state.waves = 1;
	state.waveSize = 5;
	state.nextWaveAt = 900;
	const std::string home = LogLine(world, state);
	MESSAGE(home);
	CHECK(home.find("s754 g1400 w725 food25/29 |") == 0);
	CHECK(home.find(" worker=20") != std::string::npos);
	CHECK(home.find(std::string(" ") + KindName(Kind::Hall) + "=2") != std::string::npos);
	CHECK(home.find(" fighters=5 | asked " + std::string(KindName(Kind::Farm)) + "=1") != std::string::npos);
	CHECK(home.find("| wave home out=0/0 size=5 next=900 waves=1") != std::string::npos);

	state.wave = {11, 12, 13};
	state.launchSize = 4;
	state.target = {10, 12};
	CHECK(LogLine(world, state).find("| wave march out=3/4") != std::string::npos);
	state.gatherUntil = 760;
	CHECK(LogLine(world, state).find("| wave gather out=3/4") != std::string::npos);
	CHECK(LogLine(world, state).find(" to=10,12") != std::string::npos);
}

namespace
{
/// The longest stretch (s) with no building begun from `from` to `to` (the end of the window counts).
long LongestBuildGap(const EcoGame &game, long from, long to)
{
	long last = from;
	long gap = 0;
	for (const std::pair<long, Kind> &b : game.begunAt) {
		if (b.first < from || b.first > to) {
			continue;
		}
		gap = std::max(gap, b.first - last);
		last = b.first;
	}
	return std::max(gap, to - last);
}
} // namespace

TEST_CASE("wc2bot: from a hall site the farm leads the food, no gold lies at a food block, the opening never stalls")
{
	// round 3.4.67 (work/c1102/BOTS_CHECK_3467.md): food 5/5 at ~3.6-4.4 min in every game; the
	// Captain at 13/13 with 1800-2600 gold for 1.8 min; the Warlord without gold while 1000-1200 wood
	// lay, nothing new built for 3.1 min.
	for (const char *id : {"recruit", "captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		const EcoResult r = PlayFromOneWorker(profile, 3800, 1200, 108);
		MESSAGE(EcoLine(profile, r));
		// workers are never held for the farm (fix 4b, usC8): a short block may remain (live before: ~50 s)
		CHECK(r.game.blockedFoodEarly <= 40);
		CHECK(r.game.longestFoodBlockRich <= 20);
		CHECK(r.game.longestWoodNoGold <= 30);
		const long barracks = BegunAt(r.game, Kind::Barracks);
		REQUIRE(barracks >= 0);
		CHECK(LongestBuildGap(r.game, barracks, 600) <= 120);
	}
}
TEST_CASE("wc2bot: a wave spread over two roads keeps its goal; a wave turning to a new place turns as a whole")
{
	// round 3.4.67: the Captain's wave of 5 on Unyielding walked two roads round the water, its
	// middle fell on the water, nothing lay "on the same land" from there and the wave went home.
	const Profile captain = Named("captain");
	SeededDice dice;
	std::vector<uint16_t> lake(64 * 64, 1);
	for (int y = 25; y <= 38; ++y) {
		for (int x = 25; x <= 38; ++x) {
			lake[static_cast<size_t>(y) * 64 + x] = 0;
		}
	}
	{
		State state = Idle(captain);
		World world = SeaWorld(lake, 64, 64);
		world.home = {10, 10};
		world.enemyStarts = {{55, 55}};
		world.fighters = {Own(20, {24, 40}, Kind::Soldier, false), Own(21, {23, 41}, Kind::Soldier, false),
		                  Own(22, {40, 24}, Kind::Soldier, false), Own(23, {41, 23}, Kind::Soldier, false)};
		state.wave = {20, 21, 22, 23};
		state.launchSize = 4;
		state.waves = 1;
		state.target = {55, 55};
		state.idleAt = {55, 55};
		REQUIRE_FALSE(SameLand(world, {32, 32}, world.enemyStarts[0])); // the plain middle is water
		for (long s = 601; s < 605; ++s) {
			world.second = s;
			Decide(captain, world, state, Roll, &dice);
			CHECK(state.wave.size() == 4);          // before: ended, back home
			CHECK(state.target == Point{55, 55});
		}
	}
	{
		// one unit reached the first start (empty), three still walk to it: all turn to the next
		State state = Idle(captain);
		World world = SeaWorld(lake, 64, 64);
		world.home = {10, 10};
		world.enemyStarts = {{20, 50}, {55, 55}};
		world.fighters = {Own(20, {20, 50}, Kind::Soldier, true), Own(21, {15, 40}, Kind::Soldier, false),
		                  Own(22, {14, 40}, Kind::Soldier, false), Own(23, {15, 39}, Kind::Soldier, false)};
		state.wave = {20, 21, 22, 23};
		state.launchSize = 4;
		state.waves = 1;
		state.target = {20, 50};
		state.idleAt = {20, 50};
		world.second = 601;
		const Decision d = Decide(captain, world, state, Roll, &dice);
		CHECK(state.target == Point{55, 55});
		int sent = 0;
		for (const Command &c : d.commands) {
			sent += c.to == Point{55, 55} ? 1 : 0;
		}
		CHECK(sent == 4);                           // before: only the idle one
	}
}
TEST_CASE("wc2bot: the one-peasant classic start - the hall, a worker and a farm first, workers on time")
{
	// usC8 (fix 4 live): from one peasant and no hall the Captain asked the farm before its hall
	// stood (no place), built the barracks and the blacksmith with its only peasant, sat at 1/1 food
	// and had 1 peasant for ~4.5 min (6 at 9 min; fix 3: 20).
	for (const char *id : {"recruit", "captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		for (const bool site : {false, true}) {
			CAPTURE(site);
			const EcoResult r = site ? PlayFromOneWorker(profile, 3800, 1200, 108) : PlayFromOneWorker(profile, 5000, 2000, -1);
			MESSAGE(EcoLine(profile, r));
			const EcoGame &game = r.game;
			const long hall = site ? 108 : BegunAt(game, Kind::Hall);
			REQUIRE(hall >= 0);
			REQUIRE(hall <= (site ? 108 : 15));          // the bot's own hall at once
			const long farm = BegunAt(game, Kind::Farm);
			const long barracks = BegunAt(game, Kind::Barracks);
			REQUIRE(farm >= 0);
			CHECK(farm < barracks);                      // the farm before the barracks
			CHECK(farm < BegunAt(game, Kind::Blacksmith));
			CHECK(farm <= (site ? 110 : hall + 95));     // as the hall stands
			// a hall stands ~1.8 min after the start and the first farm ~35 s later: 5 workers at 4 min
			// (3 min cannot be: worker 2 needs that farm), 12 by 7 min where the profile's worker gap
			// allows (the Recruit trains one every second worker time: 9)
			CHECK(r.workersAt[4] >= 5);
			CHECK(r.workersAt[6] >= (profile.workerGapBuilds > 1 ? 7 : 10));
			CHECK(r.workersAt[7] >= (profile.workerGapBuilds > 1 ? 9 : 12));
		}
	}
}
TEST_CASE("wc2bot: from one peasant and no hall nothing but the hall is asked until it stands")
{
	// usC8: the farm asked at second 1 beside the hall still to be built found no place; once the hall
	// stood the barracks took the only peasant (the farm's next try ~15 s later)
	for (const char *id : {"recruit", "captain", "warlord"}) {
		CAPTURE(id);
		const Profile profile = Named(id);
		EcoGame game(5000, 2000);
		game.NoHall();
		game.noPlaceUntil[KindIndex(Kind::Hall)] = 10; // the peasant walks to the hall's place (~10 s, usC8)
		State state;
		SeededDice dice;
		int before = 0;
		for (int s = 0; s < 200 && game.done[KindIndex(Kind::Hall)] == 0; ++s) {
			const Decision d = Decide(profile, game.world, state, Roll, &dice);
			for (int k = 0; k < KindCount; ++k) {
				before += k == KindIndex(Kind::Hall) ? 0 : d.release[k];
			}
			game.Step(profile, state, d);
			// nor the engine's own farm (the toy's: food used up and not held)
			before += static_cast<int>(std::count_if(game.wants.begin(), game.wants.end(),
			                                         [](const EcoGame::Want &w) { return w.kind == Kind::Farm; }));
		}
		CHECK(game.done[KindIndex(Kind::Hall)] == 1);
		CAPTURE(before);
		CHECK(before == 0); // fix 4: the farm at second 1
		CHECK(BegunAt(game, Kind::Farm) < 0); // nothing begun but the hall
	}
}