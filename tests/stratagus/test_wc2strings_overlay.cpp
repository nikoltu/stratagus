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
/**@name test_wc2strings_overlay.cpp - A language mod laid over the game's string table. */
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
#include "wc2strings.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{

/// The game's table for one test; table and overlay are emptied again when the test ends.
struct OverlayScope
{
	OverlayScope()
	{
		REQUIRE(LoadWC2StringsFromText(
			"{\"unit_1\":\"Grunt\",\"human_1_name\":\"Hillsbrad\",\"victory\":\"Victory!\"}"));
	}
	~OverlayScope()
	{
		ClearWC2StringsOverlay();
		LoadWC2StringsFromText("{}");
	}
};

} // namespace

TEST_CASE("wc2strings overlay: a key the mod gives is its text, any other stays the game's")
{
	OverlayScope scope;
	CHECK(UseWC2StringsOverlayText("{\"victory\":\"Pergal\xc4\x97!\",\"unit_1\":\"Grantas\"}") == 2);
	CHECK(GetWC2String("victory") == "Pergal\xc4\x97!");
	CHECK(GetWC2String("unit_1") == "Grantas");
	CHECK(GetWC2String("human_1_name") == "Hillsbrad");
	CHECK(GetWC2String("absent").empty());
	// The name index is the game's own: a translated name finds nothing, the English one its id.
	CHECK(GetWC2Id("unit", "Grunt") == 1);
	CHECK(GetWC2Id("unit", "Grantas") == -1);
}

TEST_CASE("wc2strings overlay: clearing gives every text back")
{
	OverlayScope scope;
	REQUIRE(UseWC2StringsOverlayText("{\"victory\":\"Pergal\xc4\x97!\"}") == 1);
	ClearWC2StringsOverlay();
	CHECK(GetWC2String("victory") == "Victory!");
}

TEST_CASE("wc2strings overlay: a broken or empty table changes nothing")
{
	OverlayScope scope;
	CHECK(UseWC2StringsOverlayText("[1,2]") == -1);
	CHECK(UseWC2StringsOverlayText("{not json") == -1);
	CHECK(GetWC2String("victory") == "Victory!");
	CHECK(UseWC2StringsOverlayText("{\"victory\":\"\",\"n\":5}") == 0);
	CHECK(GetWC2String("victory") == "Victory!");
}

TEST_CASE("wc2strings: WC2Or gives the original's text, else the English fallback")
{
	OverlayScope scope;
	CHECK(WC2Or("victory", "Won") == "Victory!");
	CHECK(WC2Or("absent", "Unable to create unit.") == "Unable to create unit.");
	REQUIRE(UseWC2StringsOverlayText("{\"victory\":\"PergalÄ!\"}") == 1);
	CHECK(WC2Or("victory", "Won") == "PergalÄ!");
}

TEST_CASE("wc2strings extra: a mod's text for a keyless literal is what _() gives")
{
	OverlayScope scope;
	CHECK(GetExtraString("Select Target") == nullptr);
	CHECK(std::string(_("Select Target")) == "Select Target");
	CHECK(UseExtraStringsText("{\"format\":1,\"strings\":{\"Select Target\":\"Pasirink taikinÄ¯\","
	                          "\"~!Yes\":\"~!Taip\",\"Watch\":\"\"}}") == 2);
	CHECK(std::string(_("Select Target")) == "Pasirink taikinÄ¯");
	CHECK(std::string(_("~!Yes")) == "~!Taip");
	// an empty text is not taken; a literal the mod does not name stays English
	CHECK(std::string(_("Watch")) == "Watch");
	CHECK(std::string(_("Game Paused")) == "Game Paused");
	// the fallback of WC2Or goes through _() as well
	CHECK(WC2Or("absent", "Select Target") == "Pasirink taikinÄ¯");
	ClearExtraStrings();
	CHECK(std::string(_("Select Target")) == "Select Target");
}

TEST_CASE("wc2strings extra: a broken table leaves the English")
{
	OverlayScope scope;
	CHECK(UseExtraStringsText("{not json") == -1);
	CHECK(UseExtraStringsText("{\"format\":1}") == -1);
	CHECK(UseExtraStringsText("{\"format\":1,\"strings\":[1]}") == -1);
	CHECK(std::string(_("Select Target")) == "Select Target");
	CHECK(UseExtraStringsText("{\"format\":1,\"strings\":{\"Select Target\":\"X\"}}") == 1);
	CHECK(UseExtraStringsText("[]") == -1);
	CHECK(GetExtraString("Select Target") == nullptr);
}

TEST_CASE("wc2strings language: the chosen language's text, enUS for a key it lacks")
{
	OverlayScope scope;
	CHECK(UseWC2LanguageText("deDE", "{\"victory\":\"Sieg!\",\"unit_1\":\"Grunzer\",\"empty\":\"\"}") == 2);
	CHECK(GetWC2Language() == "deDE");
	CHECK(GetWC2String("victory") == "Sieg!");
	CHECK(GetWC2String("unit_1") == "Grunzer");
	// a key the language lacks keeps the English
	CHECK(GetWC2String("human_1_name") == "Hillsbrad");
	CHECK(GetWC2String("absent").empty());
	// the name index stays English: the English name finds its id, the translated one nothing
	CHECK(GetWC2Id("unit", "Grunt") == 1);
	CHECK(GetWC2Id("unit", "Grunzer") == -1);
	ClearWC2Language();
	CHECK(GetWC2Language() == "enUS");
	CHECK(GetWC2String("victory") == "Victory!");
}

TEST_CASE("wc2strings language: a mod's overlay stands above the language table")
{
	OverlayScope scope;
	REQUIRE(UseWC2LanguageText("deDE", "{\"victory\":\"Sieg!\",\"unit_1\":\"Grunzer\"}") == 2);
	REQUIRE(UseWC2StringsOverlayText("{\"victory\":\"Pergal\xc4\x97!\"}") == 1);
	CHECK(GetWC2String("victory") == "Pergal\xc4\x97!");
	CHECK(GetWC2String("unit_1") == "Grunzer");
	CHECK(GetWC2String("human_1_name") == "Hillsbrad");
	ClearWC2Language();
	CHECK(GetWC2String("unit_1") == "Grunt");
	CHECK(GetWC2String("victory") == "Pergal\xc4\x97!");
}

TEST_CASE("wc2strings language: a broken table, enUS or a bad code leaves the English")
{
	OverlayScope scope;
	CHECK(UseWC2LanguageText("deDE", "{not json") == -1);
	CHECK(GetWC2Language() == "enUS");
	CHECK(GetWC2String("victory") == "Victory!");
	REQUIRE(UseWC2LanguageText("deDE", "{\"victory\":\"Sieg!\"}") == 1);
	CHECK(UseWC2Language("enUS") == 0);
	CHECK(GetWC2String("victory") == "Victory!");
	REQUIRE(UseWC2LanguageText("deDE", "{\"victory\":\"Sieg!\"}") == 1);
	CHECK(UseWC2Language("../x") == -1);
	CHECK(GetWC2String("victory") == "Victory!");
	CHECK(IsWC2LanguageCode("zhTW"));
	CHECK(IsWC2LanguageCode("ltLT"));
	CHECK_FALSE(IsWC2LanguageCode("de"));
	CHECK_FALSE(IsWC2LanguageCode("DEde"));
	CHECK_FALSE(IsWC2LanguageCode("de/E"));
	// a language the game does not ship (a mod's) has no file: the English stays
	CHECK(UseWC2Language("xxXX") == -1);
	CHECK(GetWC2String("victory") == "Victory!");
}

TEST_CASE("wc2strings language: the player's own deDE.json, when WC2_STRINGS_DIR names its folder")
{
	// Optional: nothing is read when unset (the game's files are never in the repository).
	const char *dir = std::getenv("WC2_STRINGS_DIR");
	if (dir == nullptr || *dir == '\0') {
		return;
	}
	OverlayScope scope;
	auto readAll = [](const std::string &path) {
		std::string text;
		if (FILE *f = std::fopen(path.c_str(), "rb")) {
			char buf[8192];
			size_t n;
			while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
				text.append(buf, n);
			}
			std::fclose(f);
		}
		return text;
	};
	REQUIRE(LoadWC2StringsFromText(readAll(std::string(dir) + "/enUS.json")));
	const std::string english = GetWC2String("unit_1");
	REQUIRE(!english.empty());
	const int n = UseWC2LanguageText("deDE", readAll(std::string(dir) + "/deDE.json"));
	MESSAGE("deDE texts: " << n << ", unit_1 = " << GetWC2String("unit_1"));
	CHECK(n > 1500);
	CHECK(!GetWC2String("unit_1").empty());
	CHECK(GetWC2Id("unit", english) >= 0);
	ClearWC2Language();
}
