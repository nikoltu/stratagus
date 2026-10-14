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
/**@name test_mods.cpp - The test file for mods.cpp. */
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

#include "iolib.h"
#include "json.h"
#include "mods.h"
#include "script.h"

#include <fstream>
#include <string>

namespace
{

/// A mods directory with one mod "abc" (a definition file, a voice file, an empty file) and a
/// second mod "other"; removed again when the test ends.
struct ModsFixture
{
	fs::path root;

	ModsFixture()
	{
		root = fs::temp_directory_path() / "stratagus_test_mods";
		fs::remove_all(root);
		fs::create_directories(root / "abc" / "defs");
		fs::create_directories(root / "abc" / "sounds");
		fs::create_directories(root / "other");
		Write(root / "abc" / "defs" / "voices.json",
		      R"({"briefing": {"orc": {"1": ["sounds/a.mp3", "sounds/b.mp3"]}}})");
		Write(root / "abc" / "defs" / "broken.json", "{\"briefing\": ");
		Write(root / "abc" / "sounds" / "a.mp3", "12345");
		Write(root / "abc" / "sounds" / "b.mp3", "");
		Write(root / "other" / "x.txt", "x");
		Write(root / "outside.txt", "outside");
		ModsSetRoot(root);
	}

	~ModsFixture()
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

TEST_CASE("mods: with no mod enabled a mods/ name is an ordinary name")
{
	ModsFixture fixture;

	CHECK_FALSE(ModsActive());
	CHECK(ModsLocate("mods/abc/sounds/a.mp3").empty());
	CHECK(ModsFileSize("mods/abc/sounds/a.mp3") == -1);
	// the lookup falls through all of its usual places and hands the name back
	CHECK(LibraryFileName("mods/abc/sounds/a.mp3") == "mods/abc/sounds/a.mp3");
	CHECK_FALSE(CanAccessFile("mods/abc/sounds/a.mp3"));
}

TEST_CASE("mods: an enabled mod's files are found under mods/<id>/ only")
{
	ModsFixture fixture;

	ModsSetEnabled({"abc", "abc", "missing", "Bad_Id", ".."});
	REQUIRE(ModsEnabled().size() == 1);
	CHECK(ModsEnabled()[0] == "abc");
	CHECK(ModsActive());

	CHECK(ModsLocate("mods/abc/sounds/a.mp3") == fixture.root / "abc" / "sounds" / "a.mp3");
	CHECK(fs::path(LibraryFileName("mods/abc/sounds/a.mp3")) == fixture.root / "abc" / "sounds" / "a.mp3");
	CHECK(CanAccessFile("mods/abc/sounds/a.mp3"));
	CHECK_FALSE(CanAccessFile("mods/abc/sounds/none.mp3"));
	CHECK(ModsFileSize("mods/abc/sounds/a.mp3") == 5);
	CHECK(ModsFileSize("mods/abc/sounds/b.mp3") == 0);
	CHECK(ModsFileSize("mods/abc/sounds/none.mp3") == -1);
	CHECK(ModsFileSize("mods/abc/sounds") == -1);

	// a mod that is installed but not enabled, and every way out of the mod's directory
	CHECK(ModsLocate("mods/other/x.txt").empty());
	CHECK_FALSE(CanAccessFile("mods/other/x.txt"));
	CHECK(ModsLocate("mods/abc/../outside.txt").empty());
	CHECK(ModsLocate("mods/abc/../other/x.txt").empty());
	CHECK(ModsLocate("mods/abc/./sounds/a.mp3").empty());
	CHECK(ModsLocate("mods/abc//sounds/a.mp3").empty());
	CHECK(ModsLocate("mods/abc/sounds\\a.mp3").empty());
	CHECK(ModsLocate("mods/abc/c:/x").empty());
	CHECK(ModsLocate("mods/abc/").empty());
	CHECK(ModsLocate("mods/abc").empty());
	CHECK(ModsLocate("sounds/a.mp3").empty());
}

TEST_CASE("mods: a definition file is read as JSON")
{
	ModsFixture fixture;
	ModsSetEnabled({"abc"});

	content::JsonValue root;
	REQUIRE(ModsReadJson("mods/abc/defs/voices.json", root));
	const content::JsonValue *briefing = root.find("briefing");
	REQUIRE(briefing != nullptr);
	const content::JsonValue *orc = briefing->find("orc");
	REQUIRE(orc != nullptr);
	const content::JsonValue *first = orc->find("1");
	REQUIRE(first != nullptr);
	REQUIRE(first->items().size() == 2);
	CHECK(first->items()[0].asString() == "sounds/a.mp3");

	content::JsonValue none;
	CHECK_FALSE(ModsReadJson("mods/abc/defs/broken.json", none));
	CHECK_FALSE(ModsReadJson("mods/abc/defs/absent.json", none));
	CHECK_FALSE(ModsReadJson("mods/abc/sounds/b.mp3", none));
}

TEST_CASE("mods: disabling gives the ordinary lookup back")
{
	ModsFixture fixture;
	ModsSetEnabled({"abc"});
	CHECK(CanAccessFile("mods/abc/sounds/a.mp3"));

	ModsSetEnabled({});
	CHECK_FALSE(ModsActive());
	CHECK_FALSE(CanAccessFile("mods/abc/sounds/a.mp3"));
	CHECK(LibraryFileName("mods/abc/sounds/a.mp3") == "mods/abc/sounds/a.mp3");
}

TEST_CASE("mods: an enabled mod adds nothing a saved game would write")
{
	ModsFixture fixture;

	InitLua();
	REQUIRE(Lua);
	ModsCclRegister();
	const std::string before = SaveGlobal(Lua);
	REQUIRE(luaL_dostring(Lua, R"(ModsEnable({"abc"}))") == 0);
	REQUIRE(ModsEnabled().size() == 1);
	const std::string text = SaveGlobal(Lua);
	CHECK(text.find("mods/") == std::string::npos);
	CHECK(text.find("abc") == std::string::npos);
	CHECK(text == before);
	lua_close(Lua);
	Lua = nullptr;
}
