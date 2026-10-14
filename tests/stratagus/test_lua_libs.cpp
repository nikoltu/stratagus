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
