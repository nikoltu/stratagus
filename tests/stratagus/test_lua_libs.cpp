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
/**@name test_lua_libs.cpp - The test file for the Lua library set. */
//
//      (c) Copyright 2026 by the Stratagus Team
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
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//      02111-1307, USA.
//

#include <doctest.h>

#include "stratagus.h"

#include "script.h"

#include <string>

namespace
{
struct LuaState
{
	LuaState()
	{
		InitLua();
		REQUIRE(Lua);
#ifdef DEBUG
		// A debug library keeps everything open for the script debugger; the rule is tested anyway.
		RestrictLuaLibraries(Lua);
#endif
	}
	LuaState(const LuaState &) = delete;
	~LuaState()
	{
		if (Lua) {
			lua_close(Lua);
			Lua = nullptr;
		}
	}
};

[[nodiscard]] std::string Eval(const std::string &expression)
{
	REQUIRE(CclCommand("__probe__ = tostring(" + expression + ")", false) == 0);
	lua_getglobal(Lua, "__probe__");
	const char *text = lua_tostring(Lua, -1);
	std::string res = text ? text : "";
	lua_pop(Lua, 1);
	return res;
}
} // namespace

TEST_CASE("lua libs: traceback survives without the debug global")
{
	LuaState state;

	REQUIRE(CclCommand("debug = nil", false) == 0);
	// CclCommand returns the status of the load only; the report of the run is in __last_error__.
	CclCommand("error('probe')", false);
	lua_getglobal(Lua, "__last_error__");
	const char *text = lua_tostring(Lua, -1);
	const std::string message = text ? text : "";
	lua_pop(Lua, 1);
	CHECK(message.find("probe") != std::string::npos);
	CHECK(message.find("stack traceback") != std::string::npos);
}

TEST_CASE("lua libs: what a script can reach")
{
	LuaState state;

	// The four time functions stay (a wall-clock deadline of the lobby scripts uses os.time).
	CHECK(Eval("type(os.time)") == "function");
	CHECK(Eval("type(os.clock)") == "function");
	CHECK(Eval("type(os.date)") == "function");
	CHECK(Eval("type(os.difftime)") == "function");
	CHECK(Eval("type(os.time())") == "number");
	// Process, file system and environment are closed.
	CHECK(Eval("os.execute") == "nil");
	CHECK(Eval("os.remove") == "nil");
	CHECK(Eval("os.rename") == "nil");
	CHECK(Eval("os.getenv") == "nil");
	CHECK(Eval("os.exit") == "nil");
	CHECK(Eval("os.tmpname") == "nil");
	CHECK(Eval("debug") == "nil");
	CHECK(Eval("dofile") == "nil");
	CHECK(Eval("loadfile") == "nil");
	CHECK(Eval("loadstring") == "nil");
	CHECK(Eval("load") == "nil");
	CHECK(Eval("string.dump") == "nil");
	CHECK(Eval("('x').dump") == "nil");
#ifndef DEBUG
	CHECK(Eval("io") == "nil");
	CHECK(Eval("package") == "nil");
	CHECK(Eval("require") == "nil");
#endif
	// The rest of the libraries is untouched.
	CHECK(Eval("type(string.format)") == "function");
	CHECK(Eval("type(table.insert)") == "function");
	CHECK(Eval("type(math.floor)") == "function");
	CHECK(Eval("type(pcall)") == "function");
}

TEST_CASE("lua libs: text is compiled by the C API only")
{
	LuaState state;

	// A script that tries gets a Lua error, not a result.
	CclCommand("__compiled__ = 'no'; __compiled__ = loadstring('return 7')()", false);
	CHECK(Eval("__compiled__") == "no");
	CclCommand("__compiled__ = load(function() return nil end) and 'yes' or 'no'", false);
	CHECK(Eval("__compiled__") == "no");
	// The engine's command line and the debug bridge compile through the C API.
	REQUIRE(CclCommand("__compiled__ = 3 + 4", false) == 0);
	CHECK(Eval("__compiled__") == "7");
	REQUIRE(luaL_loadstring(Lua, "return 6 * 7") == 0);
	REQUIRE(lua_pcall(Lua, 0, 1, 0) == 0);
	CHECK(lua_tointeger(Lua, -1) == 42);
	lua_pop(Lua, 1);
}

// A copy of RM_Upper from the device scripts (scripts/guichan.lua, M3): it is run here so that the
// byte ranges of its pattern and its arithmetic are proven in the engine's own Lua.
static const char *RMUpperSource = R"lua(
local function rm_upper2(c1, c2)
  local cp = (c1:byte() - 192) * 64 + (c2:byte() - 128)
  local up = cp
  if cp >= 0xE0 and cp <= 0xFE and cp ~= 0xF7 then up = cp - 0x20
  elseif cp == 0xFF then up = 0x178
  elseif ((cp >= 0x100 and cp <= 0x137) or (cp >= 0x14A and cp <= 0x177)) and cp ~= 0x131 then
    if cp % 2 == 1 then up = cp - 1 end
  elseif (cp >= 0x139 and cp <= 0x148) or (cp >= 0x179 and cp <= 0x17E) then
    if cp % 2 == 0 then up = cp - 1 end
  end
  if up == cp then return c1 .. c2 end
  return string.char(192 + math.floor(up / 64), 128 + up % 64)
end
function RM_Upper(s)
  s = string.upper(s)
  return (string.gsub(s, "([\195-\197])([\128-\191])", rm_upper2))
end
function RM_TitleCase(s)
  return (string.gsub(s, "([%a\194-\244][\128-\191]*)([%w\128-\244]*)",
    function(a, b) return RM_Upper(a) .. b end))
end
)lua";

TEST_CASE("lua libs: RM_Upper capitalises the Lithuanian letters")
{
	LuaState state;

	REQUIRE(CclCommand(RMUpperSource, false) == 0);
	// "Stromgardo žlugimas" -> "STROMGARDO ŽLUGIMAS"
	CHECK(Eval("RM_Upper('II. Stromgardo \xc5\xbelugimas')") == "II. STROMGARDO \xc5\xbdLUGIMAS");
	// ą č ę ė į š ų ū ž -> Ą Č Ę Ė Į Š Ų Ū Ž
	CHECK(Eval("RM_Upper('\xc4\x85\xc4\x8d\xc4\x99\xc4\x97\xc4\xaf\xc5\xa1\xc5\xb3\xc5\xab\xc5\xbe')")
	      == "\xc4\x84\xc4\x8c\xc4\x98\xc4\x96\xc4\xae\xc5\xa0\xc5\xb2\xc5\xaa\xc5\xbd");
	// capitals stay; Latin-1 é ü ÿ; ı and ĸ have no capital of their own
	CHECK(Eval("RM_Upper('\xc4\x84\xc5\xbd')") == "\xc4\x84\xc5\xbd");
	CHECK(Eval("RM_Upper('\xc3\xa9\xc3\xbc\xc3\xbf')") == "\xc3\x89\xc3\x9c\xc5\xb8");
	CHECK(Eval("RM_Upper('\xc4\xb1\xc4\xb8')") == "\xc4\xb1\xc4\xb8");
}

TEST_CASE("lua libs: RM_TitleCase raises the first letter of every word, outside ASCII too")
{
	LuaState state;

	REQUIRE(CclCommand(RMUpperSource, false) == 0);
	CHECK(Eval("RM_TitleCase('garden of war')") == "Garden Of War");
	CHECK(Eval("RM_TitleCase('2players map')") == "2Players Map");
	// "žalgiris ąžuolas" -> "Žalgiris Ąžuolas", not "žAlgiris ąžUolas"
	CHECK(Eval("RM_TitleCase('\xc5\xbe" "algiris \xc4\x85\xc5\xbeuolas')") == "\xc5\xbd" "algiris \xc4\x84\xc5\xbeuolas");
	// a letter outside ASCII inside a word does not begin a new word: "cafénoir ölund"
	CHECK(Eval("RM_TitleCase('caf\xc3\xa9noir \xc3\xb6lund')") == "Caf\xc3\xa9noir \xc3\x96lund");
}
