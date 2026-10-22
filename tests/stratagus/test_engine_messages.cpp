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
/**@name test_engine_messages.cpp - U-16: the engine's in-game lines come from the game's string table. */
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

#include "translate.h"
#include "unittype.h"
#include "upgrade_structs.h"
#include "wc2strings.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// Defined in src/stratagus/player.cpp.
extern const char *WC2CostRefusalKey(int cost);
extern const char *WC2CostRefusalEnglish(int cost);
// Defined in src/action/action_upgradeto.cpp.
extern bool UpgradeToCompletePrintsLine(const CUnitType &newtype);

namespace
{

/// Every string-table key the engine's refusal and limit lines use (player.cpp CheckLimits /
/// CheckCosts, action_spellcast.cpp).
const char *const MessageKeys[] = {
	"message_cannot_create_units", // unit / building / total / per-type limit
	"stat_txt_435",                // mana
	"stat_txt_438",                // food
	"stat_txt_439",                // gold
	"stat_txt_440",                // lumber
	"stat_txt_441",                // oil
};

/// A table holding the keys with a marker text each; emptied again when the test ends.
struct TableScope
{
	explicit TableScope(const std::string &json) { REQUIRE(LoadWC2StringsFromText(json)); }
	~TableScope() { LoadWC2StringsFromText("{}"); }
};

std::string MarkerTable()
{
	std::string json = "{";
	bool first = true;
	for (const char *key : MessageKeys) {
		json += first ? "" : ",";
		json += "\"" + std::string(key) + "\":\"T:" + key + "\"";
		first = false;
	}
	return json + "}";
}

} // namespace

TEST_CASE("engine messages: the refusal per cost is the original's key, none for unused costs")
{
	CHECK(std::string(WC2CostRefusalKey(GoldCost)) == "stat_txt_439");
	CHECK(std::string(WC2CostRefusalKey(WoodCost)) == "stat_txt_440");
	CHECK(std::string(WC2CostRefusalKey(OilCost)) == "stat_txt_441");
	CHECK(WC2CostRefusalKey(TimeCost) == nullptr);
	CHECK(WC2CostRefusalKey(Cost4) == nullptr);
	CHECK(WC2CostRefusalKey(Cost5) == nullptr);
	CHECK(WC2CostRefusalKey(Cost6) == nullptr);
	CHECK(WC2CostRefusalEnglish(Cost4) == nullptr);
}

TEST_CASE("engine messages: every key resolves through the string lookup")
{
	TableScope scope(MarkerTable());
	for (const char *key : MessageKeys) {
		CAPTURE(key);
		CHECK(GetWC2String(key) == std::string("T:") + key);
		CHECK(WC2Or(key, "fallback") == std::string("T:") + key);
		CHECK(std::string(WC2Msg(key, "fallback")) == std::string("T:") + key);
	}
	for (int cost : {GoldCost, WoodCost, OilCost}) {
		const char *key = WC2CostRefusalKey(cost);
		CHECK(WC2Or(key, WC2CostRefusalEnglish(cost)) == std::string("T:") + key);
	}
}

TEST_CASE("engine messages: without the string file the line is the original's English wording")
{
	TableScope scope("{\"unrelated\":\"x\"}");
	CHECK(WC2Or(WC2CostRefusalKey(GoldCost), WC2CostRefusalEnglish(GoldCost))
	      == "Not enough gold...mine more gold.");
	CHECK(WC2Or(WC2CostRefusalKey(WoodCost), WC2CostRefusalEnglish(WoodCost))
	      == "Not enough lumber...chop more trees.");
	CHECK(WC2Or(WC2CostRefusalKey(OilCost), WC2CostRefusalEnglish(OilCost))
	      == "Not enough oil...drill for oil.");
}

TEST_CASE("engine messages: the player's own enUS.json has every key (WARGUSHD_WC2_STRINGS)")
{
	// Optional: the path of the player's Data/Strings/enUS.json. Nothing is read when unset.
	const char *path = std::getenv("WARGUSHD_WC2_STRINGS");
	if (path == nullptr || *path == '\0') {
		return;
	}
	std::ifstream in(path, std::ios::binary);
	REQUIRE(in.good());
	std::stringstream text;
	text << in.rdbuf();
	std::string json = text.str();
	if (json.compare(0, 3, "\xEF\xBB\xBF") == 0) {
		json.erase(0, 3); // UTF-8 byte order mark
	}
	TableScope scope(json);
	for (const char *key : MessageKeys) {
		CAPTURE(key);
		CHECK_FALSE(GetWC2String(key).empty());
	}
	for (int cost : {GoldCost, WoodCost, OilCost}) {
		CHECK(GetWC2String(WC2CostRefusalKey(cost)) == WC2CostRefusalEnglish(cost));
	}
}

TEST_CASE("engine messages: no line when a hall upgrade completes (live run 53, 2 of 2)")
{
	// Town Hall -> Keep and Great Hall -> Stronghold: no message line in 39 and 38 grabs.
	CUnitType hall;
	hall.CanStore[GoldCost] = 1;
	hall.CanStore[WoodCost] = 1;
	hall.CanStore[OilCost] = 1;
	CHECK_FALSE(UpgradeToCompletePrintsLine(hall));
	// a tower upgrade is not measured: its line stays
	CUnitType tower;
	CHECK(UpgradeToCompletePrintsLine(tower));
}
