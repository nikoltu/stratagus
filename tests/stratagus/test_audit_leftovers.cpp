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
/**@name test_audit_leftovers.cpp - engine audit defects (EA-205, 216, 403/404, 408, 419). */
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

#include "game.h"
#include "globalsave.h"
#include "iolib.h"
#include "parameters.h"
#include "pud.h"
#include "replay.h"
#include "script.h"
#include "wc2mission.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// ---- a minimal PUD in memory ------------------------------------------------------------------

void Chunk(std::string &pud, const char *name, const std::string &body)
{
	pud.append(name, 4);
	const uint32_t len = static_cast<uint32_t>(body.size());
	for (int i = 0; i < 4; ++i) {
		pud.push_back(static_cast<char>((len >> (8 * i)) & 0xFF));
	}
	pud += body;
}

struct PudParts
{
	std::string desc = std::string("Test map") + '\0';
	std::string ownr = std::string(8, '\x05') + std::string(8, '\x03'); // persons, then nobody
	std::string era = std::string(1, '\0') + '\0';
	std::string dim = std::string("\x02\x00\x02\x00", 4); // 2 x 2
	std::string side = std::string(16, '\0');
	std::string mtxm = std::string(2 * 2 * 2, '\0');
	std::string unit; // 8 bytes per unit
	bool truncateLast = false;

	std::string Build() const
	{
		std::string pud;
		Chunk(pud, "TYPE", std::string("WAR2 MAP") + '\0' + '\0');
		Chunk(pud, "DESC", desc);
		Chunk(pud, "OWNR", ownr);
		Chunk(pud, "ERA ", era);
		Chunk(pud, "DIM ", dim);
		Chunk(pud, "SIDE", side);
		Chunk(pud, "MTXM", mtxm);
		if (!unit.empty()) {
			Chunk(pud, "UNIT", unit);
		}
		if (truncateLast) {
			pud += "UNI"; // a header cut short at the end of the file
		}
		return pud;
	}
};

std::string UnitRecord(int x, int y, int type, int owner, int data)
{
	std::string r;
	r.push_back(static_cast<char>(x & 0xFF));
	r.push_back(static_cast<char>(x >> 8));
	r.push_back(static_cast<char>(y & 0xFF));
	r.push_back(static_cast<char>(y >> 8));
	r.push_back(static_cast<char>(type));
	r.push_back(static_cast<char>(owner));
	r.push_back(static_cast<char>(data & 0xFF));
	r.push_back(static_cast<char>(data >> 8));
	return r;
}

bool ParsePud(const std::string &bytes, wc2pud::PudData &pd)
{
	return pd.Parse(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
}

// ---- Lua round trip -------------------------------------------------------------------------------

std::string LuaRoundTrip(const std::string &text)
{
	lua_State *l = luaL_newstate();
	REQUIRE(l);
	const std::string code = "return \"" + EscapeLuaString(text) + "\"";
	const int status = luaL_loadbuffer(l, code.data(), code.size(), "probe") || lua_pcall(l, 0, 1, 0);
	std::string res;
	if (status == 0) {
		size_t len = 0;
		const char *s = lua_tolstring(l, -1, &len);
		res.assign(s, len);
	} else {
		res = "<error>";
	}
	lua_close(l);
	return res;
}

std::string ReadAll(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

} // namespace

TEST_CASE("audit EA-403/404: names with quotes, backslashes and line breaks survive a Lua literal")
{
	for (const std::string &text : {std::string("Thrall"), std::string("\"Thrall\""), std::string("Bob\\"),
									std::string("a\\\"b"), std::string("two\nlines\r]]"),
									std::string("\nleading"), std::string("nul\0" "7", 5)}) {
		CHECK(LuaRoundTrip(text) == text);
	}
}

TEST_CASE("audit EA-216: a well-formed PUD parses, a damaged one is refused")
{
	{
		PudParts p;
		p.unit = UnitRecord(1, 1, 0x5E, 0, 0) + UnitRecord(0, 0, 0, 0, 0);
		wc2pud::PudData pd;
		REQUIRE(ParsePud(p.Build(), pd));
		std::string setup;
		pd.BuildSetup(setup);
		CHECK(setup.find("SetStartView(0, 1, 1)") != std::string::npos);
		CHECK(setup.find("CreateUnit(\"unit-footman\", 0, {0, 0})") != std::string::npos);
	}
	SUBCASE("owner type out of the table")
	{
		PudParts p;
		p.ownr[3] = static_cast<char>(200);
		wc2pud::PudData pd;
		CHECK_FALSE(ParsePud(p.Build(), pd));
	}
	SUBCASE("side out of the table")
	{
		PudParts p;
		p.side[0] = 9;
		wc2pud::PudData pd;
		CHECK_FALSE(ParsePud(p.Build(), pd));
	}
	SUBCASE("tileset out of the table")
	{
		PudParts p;
		p.era[0] = 7;
		wc2pud::PudData pd;
		CHECK_FALSE(ParsePud(p.Build(), pd));
	}
	SUBCASE("a start location of owner 200")
	{
		PudParts p;
		p.unit = UnitRecord(1, 1, 0x5E, 200, 0);
		wc2pud::PudData pd;
		CHECK_FALSE(ParsePud(p.Build(), pd));
	}
	SUBCASE("tile words missing for the map size")
	{
		PudParts p;
		p.dim = std::string("\x40\x00\x40\x00", 4); // 64 x 64 with 2 x 2 tiles' worth of words
		wc2pud::PudData pd;
		CHECK_FALSE(ParsePud(p.Build(), pd));
	}
	SUBCASE("a header cut at the end of the file")
	{
		PudParts p;
		p.truncateLast = true;
		wc2pud::PudData pd;
		CHECK_FALSE(ParsePud(p.Build(), pd));
	}
	SUBCASE("too short to be a PUD")
	{
		wc2pud::PudData pd;
		CHECK_FALSE(ParsePud(std::string("TYPE"), pd));
		CHECK_FALSE(ParsePud(std::string(), pd));
	}
	SUBCASE("a unit type beyond the name table is skipped, not read out of bounds")
	{
		PudParts p;
		p.unit = UnitRecord(0, 0, 250, 0, 0);
		wc2pud::PudData pd;
		REQUIRE(ParsePud(p.Build(), pd));
		std::string setup;
		pd.BuildSetup(setup);
		CHECK(setup.find("CreateUnit") == std::string::npos);
	}
	SUBCASE("a quote in the description stays inside the Lua string")
	{
		PudParts p;
		p.desc = std::string("A \"quoted\" map\\") + '\0';
		wc2pud::PudData pd;
		REQUIRE(ParsePud(p.Build(), pd));
		std::string presentation;
		pd.BuildPresentation(presentation);
		CHECK(presentation.find("PresentMap(\"A \\\"quoted\\\" map\\\\\", ") != std::string::npos);
	}
	SUBCASE("a description longer than its buffer is cut, not overrun")
	{
		PudParts p;
		p.desc = std::string(200, 'x') + '\0';
		wc2pud::PudData pd;
		REQUIRE(ParsePud(p.Build(), pd));
		std::string presentation;
		pd.BuildPresentation(presentation);
		CHECK(presentation.find("PresentMap(\"" + std::string(31, 'x') + "\", ") != std::string::npos);
	}
}

TEST_CASE("audit EA-205: every unit-allow ident is a unit the scripts define")
{
	// The PUD converter's names are the script idents of the original's unit numbers 0x00-0x68.
	auto known = [](const char *ident) {
		for (int i = 0; i < 0x69; ++i) {
			if (std::strcmp(wc2pud::UnitScriptNames[i], ident) == 0) {
				return true;
			}
		}
		return false;
	};
	for (int bit = 0; bit < 32; ++bit) {
		for (bool orc : {false, true}) {
			const char *ident = MissionUnitAllowIdent(bit, orc);
			if (ident) {
				INFO("bit ", bit, orc ? " orc " : " human ", ident);
				CHECK(known(ident));
			}
		}
	}
	CHECK(std::strcmp(MissionUnitAllowIdent(10, false), "unit-human-submarine") == 0);
	CHECK(std::strcmp(MissionUnitAllowIdent(10, true), "unit-orc-submarine") == 0);
	CHECK(std::strcmp(MissionUnitAllowIdent(11, false), "unit-balloon") == 0);
	CHECK(std::strcmp(MissionUnitAllowIdent(23, false), "unit-inventor") == 0);
}

TEST_CASE("audit EA-408: a save without a replay object writes no replay block")
{
	CleanReplayLog(); // no replay object, as with the command log off
	const fs::path path = fs::temp_directory_path() / "wargushd_test_ea408.lua";
	{
		CFile file;
		REQUIRE(file.open(path.string().c_str(), CL_OPEN_WRITE) != -1);
		SaveReplayList(file); // dereferenced the missing object before
		file.close();
	}
	CHECK(ReadAll(path).find("ReplayLog") == std::string::npos);
	std::error_code ec;
	fs::remove(path, ec);
}

TEST_CASE("audit EA-419: GlobalSave.json is never rewritten from zeros")
{
	const fs::path dir = fs::temp_directory_path() / "wargushd_test_ea419";
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::create_directories(dir);
	const fs::path oldUser = Parameters::Instance.GetUserDirectory();
	const std::string oldGame = GameName;
	Parameters::Instance.SetUserDirectory(dir);
	GameName.clear();
	const fs::path file = dir / "GlobalSave.json";

	SUBCASE("a damaged file is left as it is")
	{
		{
			std::ofstream(file, std::ios::binary) << "{\"missionsCompleted\":{\"todOrc\":5,\"todHu";
		}
		const std::string before = ReadAll(file);
		SetGlobalCampaignProgress("bdpHuman", 3);
		CHECK(ReadAll(file) == before);
	}
	SUBCASE("a good file keeps the other campaigns and leaves no temporary file")
	{
		{
			std::ofstream(file, std::ios::binary)
				<< "{\"missionsCompleted\":{\"todOrc\":5,\"todHuman\":2,\"bdpOrc\":1,\"bdpHuman\":0}}";
		}
		SetGlobalCampaignProgress("bdpHuman", 3);
		const std::string after = ReadAll(file);
		CHECK(after.find("\"todOrc\": 5") != std::string::npos);
		CHECK(after.find("\"todHuman\": 2") != std::string::npos);
		CHECK(after.find("\"bdpOrc\": 1") != std::string::npos);
		CHECK(after.find("\"bdpHuman\": 3") != std::string::npos);
		CHECK_FALSE(fs::exists(dir / "GlobalSave.json.tmp"));
	}
	SUBCASE("no file yet: the first value is written")
	{
		SetGlobalCampaignProgress("todOrc", 1);
		CHECK(ReadAll(file).find("\"todOrc\": 1") != std::string::npos);
	}

	Parameters::Instance.SetUserDirectory(oldUser);
	GameName = oldGame;
	fs::remove_all(dir, ec);
}
