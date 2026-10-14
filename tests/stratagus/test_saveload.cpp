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
/**@name test_saveload.cpp - the pure parts of saving and loading a game. */
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.
//

#include <doctest.h>

#include "stratagus.h"

#include "game.h"
#include "replay.h"
#include "unit.h"
#include "wc2mission.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace
{

fs::path Scratch(const char *name)
{
	std::error_code ec;
	const fs::path dir = fs::temp_directory_path(ec) / "stratagus_saveload_test" / name;
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
	return dir;
}

void Put(const fs::path &file, const std::string &text)
{
	std::ofstream out(file, std::ios::binary);
	out << text;
}

std::string Get(const fs::path &file)
{
	std::ifstream in(file, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("saveload: a save is written under a name no list shows")
{
	const fs::path target = fs::path("save") / "game.sav";
	CHECK(SaveGameTemporary(target).filename().string() == "game.sav.tmp");
	// With the file layer's ".gz" it ends in neither ".sav" nor ".sav.gz".
	const std::string packed = SaveGameTemporary(target).filename().string() + ".gz";
	CHECK(packed == "game.sav.tmp.gz");
	CHECK(packed.rfind(".sav.gz") != packed.size() - 7);
}

TEST_CASE("saveload: a complete save replaces the one that was there")
{
	const fs::path dir = Scratch("complete");
	const fs::path target = dir / "game.sav";
	Put(dir / "game.sav.gz", "old");
	Put(dir / "game.sav.tmp.gz", "new");
	CHECK(CommitSaveGame(target, true));
	CHECK(Get(dir / "game.sav.gz") == "new");
	CHECK_FALSE(fs::exists(dir / "game.sav.tmp.gz"));
	std::error_code ec;
	fs::remove_all(dir, ec);
}

TEST_CASE("saveload: a save that was not written whole leaves the one that was there")
{
	const fs::path dir = Scratch("cut");
	const fs::path target = dir / "game.sav";
	Put(dir / "game.sav.gz", "old");
	Put(dir / "game.sav.tmp.gz", "ha");
	CHECK_FALSE(CommitSaveGame(target, false));
	CHECK(Get(dir / "game.sav.gz") == "old");
	CHECK_FALSE(fs::exists(dir / "game.sav.tmp.gz"));
	std::error_code ec;
	fs::remove_all(dir, ec);
}

TEST_CASE("saveload: a save the file layer could not compress is moved under the plain name")
{
	const fs::path dir = Scratch("plain");
	const fs::path target = dir / "game.sav";
	Put(dir / "game.sav.tmp", "plain");
	CHECK(CommitSaveGame(target, true));
	CHECK(Get(dir / "game.sav") == "plain");
	CHECK_FALSE(fs::exists(dir / "game.sav.tmp"));
	// Nothing written at all: nothing to move.
	CHECK_FALSE(CommitSaveGame(target, true));
	std::error_code ec;
	fs::remove_all(dir, ec);
}

TEST_CASE("saveload: a file of a later format is refused, an older one and one without a number are read")
{
	CHECK(SavedGameFormat == 2);
	CHECK(SavedGameFormatReadable(0));
	CHECK(SavedGameFormatReadable(1));
	CHECK(SavedGameFormatReadable(2));
	CHECK_FALSE(SavedGameFormatReadable(3));
	CHECK_FALSE(SavedGameFormatReadable(99));
}

TEST_CASE("saveload: the unit's small counters go into the file only when set")
{
	CHECK(UnitSmallFieldsText(0, 0, false, -1).empty());
	CHECK(UnitSmallFieldsText(7, 3, true, 2)
	      == R"( "under-attack", 7, "just-moved", 3, "z-displaced", "colors", 2,)");
	CHECK(UnitSmallFieldsText(0, 0, true, -1) == R"( "z-displaced",)");
}

TEST_CASE("saveload: an autosave is due at every game's start and every 60 s of wall time after it")
{
	unsigned long next = 0;
	const unsigned long start = 5000; // the wall clock does not begin with the game

	// 0 s: the start writes.
	CHECK(AutosaveDue(start, next, 5, false, false, true));
	CHECK(next == start + 60000);
	CHECK_FALSE(AutosaveDue(start + 1, next, 5, false, false, false));
	CHECK_FALSE(AutosaveDue(start + 59900, next, 5, false, false, false));
	CHECK(AutosaveDue(start + 60000, next, 5, false, false, false));
	CHECK_FALSE(AutosaveDue(start + 60001, next, 5, false, false, false));
	// A late frame does not move the grid of the start.
	CHECK(AutosaveDue(start + 120040, next, 5, false, false, false));
	CHECK(next == start + 180000);
	CHECK_FALSE(AutosaveDue(start + 179999, next, 5, false, false, false));

	// A game comes up from a save at 130 s: it writes and the 60 s begin there.
	CHECK(AutosaveDue(start + 130000, next, 5, false, false, true));
	CHECK(next == start + 190000);
	CHECK_FALSE(AutosaveDue(start + 180000, next, 5, false, false, false));
	CHECK_FALSE(AutosaveDue(start + 189900, next, 5, false, false, false));
	CHECK(AutosaveDue(start + 190000, next, 5, false, false, false));

	// A stall of several periods: one write, the next a period later.
	CHECK(AutosaveDue(start + 500000, next, 5, false, false, false));
	CHECK(next == start + 560000);
	CHECK_FALSE(AutosaveDue(start + 500016, next, 5, false, false, false));

	// Switched off, a network game, a replay: never, and the clock is left alone.
	unsigned long untouched = 77;
	CHECK_FALSE(AutosaveDue(1000, untouched, 0, false, false, true));
	CHECK_FALSE(AutosaveDue(900000, untouched, 0, false, false, false));
	CHECK_FALSE(AutosaveDue(1000, untouched, 5, true, false, true));
	CHECK_FALSE(AutosaveDue(900000, untouched, 5, true, false, false));
	CHECK_FALSE(AutosaveDue(1000, untouched, 5, false, true, true));
	CHECK_FALSE(AutosaveDue(900000, untouched, 5, false, true, false));
	CHECK(untouched == 77);
}

TEST_CASE("saveload: the mission rule's counts go into the file as one guarded line")
{
	// Nothing counted, gate shut: the file carries no line.
	CHECK(MissionRuleStateLine({}, false).empty());
	CHECK(MissionRuleStateLine({12, 7, 301}, false)
	      == "if (MissionRuleState ~= nil) then MissionRuleState({gate = false, arrived = {12, 7, 301}}) end\n");
	CHECK(MissionRuleStateLine({}, true)
	      == "if (MissionRuleState ~= nil) then MissionRuleState({gate = true, arrived = {}}) end\n");
}

TEST_CASE("replay: an input line is a cheat word; only a debug build runs a console line")
{
	// What the recording game writes: a recognised cheat word (any build), or - in a debug build
	// only - a console line with a leading '-'.
	CHECK_FALSE(ReplayInputRunsLua("glittering prizes", false));
	CHECK_FALSE(ReplayInputRunsLua("it is a good day to die", false));
	CHECK_FALSE(ReplayInputRunsLua("glittering prizes", true));
	// A '-' line in a replay file never runs in a release build, whatever it says.
	CHECK_FALSE(ReplayInputRunsLua("-os.exit(0)", false));
	CHECK_FALSE(ReplayInputRunsLua("-", false));
	CHECK_FALSE(ReplayInputRunsLua("--", false));
	CHECK_FALSE(ReplayInputRunsLua("- SetPlayerData(0, 'Resources', 'gold', 1)", false));
	CHECK(ReplayInputRunsLua("-print(1)", true));
	// Only the first character counts; nothing is read from an empty line.
	CHECK_FALSE(ReplayInputRunsLua(" -print(1)", true));
	CHECK_FALSE(ReplayInputRunsLua("fow -off", true));
	CHECK_FALSE(ReplayInputRunsLua("", true));
	CHECK_FALSE(ReplayInputRunsLua("", false));
}
