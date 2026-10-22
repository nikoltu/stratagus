//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \   __\_  __ \__  \   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name test_lua_shadow.cpp - A scan of the device scripts for a call of a global function
**  whose name a local of the same scope has taken over. */
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

#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The fault this guards against (3.4.59 / 3.4.60): menus/save.lua declared `local RM_S = RM.FIT()`
// and then called `RM_S("save_game", ...)`, guichan.lua's global string helper; the call reached
// the number and the Save and Load windows did not open. The scan below flags every call of a
// global function of the device scripts made where a local of the same name is in scope.

namespace
{
struct LuaToken
{
	std::string text;
	bool name = false; // identifier or keyword
	int line = 0;
};

std::vector<LuaToken> LexLua(const std::string &src)
{
	std::vector<LuaToken> out;
	size_t i = 0;
	int line = 1;
	const size_t n = src.size();
	// the level of a long bracket opening at `p` ("[[", "[=[", ...), -1 when there is none
	auto longLevel = [&](size_t p) {
		if (p >= n || src[p] != '[') {
			return -1;
		}
		size_t q = p + 1;
		int level = 0;
		while (q < n && src[q] == '=') {
			++level;
			++q;
		}
		return (q < n && src[q] == '[') ? level : -1;
	};
	auto skipLong = [&](size_t p, int level) {
		const std::string close = "]" + std::string(level, '=') + "]";
		const size_t end = src.find(close, p + level + 2);
		const size_t stop = end == std::string::npos ? n : end + close.size();
		for (size_t k = p; k < stop; ++k) {
			line += src[k] == '\n';
		}
		return stop;
	};
	while (i < n) {
		const char c = src[i];
		if (c == '\n') {
			++line;
			++i;
		} else if (std::isspace(static_cast<unsigned char>(c))) {
			++i;
		} else if (c == '-' && i + 1 < n && src[i + 1] == '-') {
			const int level = longLevel(i + 2);
			if (level >= 0) {
				i = skipLong(i + 2, level);
			} else {
				while (i < n && src[i] != '\n') {
					++i;
				}
			}
		} else if (c == '"' || c == '\'') {
			out.push_back({"\"", false, line});
			++i;
			while (i < n && src[i] != c && src[i] != '\n') {
				i += src[i] == '\\' ? 2 : 1;
			}
			++i;
		} else if (c == '[' && longLevel(i) >= 0) {
			out.push_back({"\"", false, line});
			i = skipLong(i, longLevel(i));
		} else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
			size_t j = i;
			while (j < n && (std::isalnum(static_cast<unsigned char>(src[j])) || src[j] == '_')) {
				++j;
			}
			out.push_back({src.substr(i, j - i), true, line});
			i = j;
		} else if (std::isdigit(static_cast<unsigned char>(c))) {
			while (i < n && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '.')) {
				++i;
			}
			out.push_back({"0", false, line});
		} else {
			out.push_back({std::string(1, c), false, line});
			++i;
		}
	}
	return out;
}

/// Names defined as global functions: `function Name(` (no `local`, no table path).
void CollectGlobalFunctions(const std::vector<LuaToken> &t, std::set<std::string> &names)
{
	for (size_t k = 0; k + 2 < t.size(); ++k) {
		if (t[k].text == "function" && t[k + 1].name && t[k + 2].text == "("
		    && (k == 0 || t[k - 1].text != "local")) {
			names.insert(t[k + 1].text);
		}
	}
}

/// "file:line name" for each call of a name of `globals` made while a local of that name is in
/// scope. `local X = X` (an alias of the global) is not a shadow.
std::vector<std::string> FindShadowedCalls(const std::vector<LuaToken> &t,
                                           const std::set<std::string> &globals,
                                           const std::string &file)
{
	std::vector<std::string> found;
	std::vector<std::set<std::string>> scopes(1);
	auto isLocal = [&](const std::string &name) {
		for (const auto &s : scopes) {
			if (s.count(name)) {
				return true;
			}
		}
		return false;
	};
	auto pop = [&]() {
		if (scopes.size() > 1) {
			scopes.pop_back();
		}
	};
	for (size_t k = 0; k < t.size(); ++k) {
		const std::string &w = t[k].text;
		if (w == "function") {
			scopes.emplace_back();
			// the parameters are locals of the function
			size_t p = k + 1;
			while (p < t.size() && t[p].text != "(") {
				++p;
			}
			for (++p; p < t.size() && t[p].text != ")"; ++p) {
				if (t[p].name) {
					scopes.back().insert(t[p].text);
				}
			}
			k = p;
		} else if (w == "do" || w == "then" || w == "repeat") {
			scopes.emplace_back();
		} else if (w == "end" || w == "until") {
			pop();
		} else if (w == "elseif" || w == "else") {
			pop();
			if (w == "else") {
				scopes.emplace_back();
			}
		} else if (w == "local" && k + 1 < t.size()) {
			if (t[k + 1].text == "function") {
				if (k + 2 < t.size()) {
					scopes.back().insert(t[k + 2].text);
				}
				continue;
			}
			std::vector<std::string> names;
			size_t p = k + 1;
			while (p < t.size() && t[p].name) {
				names.push_back(t[p].text);
				if (p + 1 < t.size() && t[p + 1].text == ",") {
					p += 2;
				} else {
					++p;
					break;
				}
			}
			const bool alias = names.size() == 1 && p + 1 < t.size() && t[p].text == "="
			                   && t[p + 1].text == names[0]
			                   && (p + 2 >= t.size() || t[p + 2].text != "(");
			if (!alias) {
				for (const auto &name : names) {
					scopes.back().insert(name);
				}
			}
			k = p - 1;
		} else if (t[k].name && globals.count(w) && k + 1 < t.size()
		           && (t[k + 1].text == "(" || t[k + 1].text == "\"" || t[k + 1].text == "{")
		           && (k == 0
		               || (t[k - 1].text != "." && t[k - 1].text != ":" && t[k - 1].text != "function"))
		           && isLocal(w)) {
			found.push_back(file + ":" + std::to_string(t[k].line) + " " + w);
		}
	}
	return found;
}

std::string ReadAll(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}
} // namespace

TEST_CASE("lua shadow scan: the fault of 3.4.59 is found")
{
	const std::string guichan = "function RM_S(key, fallback) return fallback end\n";
	const std::string save = "function RunSaveMenu()\n"
	                         "  local RM_S = RM.FIT()\n"
	                         "  local function LEN(v) return math.floor(v * RM_S + 0.5) end\n"
	                         "  local menu = WarMenuRemaster(RM_S(\"save_game\", \"Save Game\"), 1, 1)\n"
	                         "end\n";
	std::set<std::string> globals;
	CollectGlobalFunctions(LexLua(guichan), globals);
	const auto found = FindShadowedCalls(LexLua(save), globals, "save.lua");
	REQUIRE(found.size() == 1);
	CHECK(found[0] == "save.lua:4 RM_S");
}

TEST_CASE("lua shadow scan: the fixed script, an alias, other scopes and strings pass")
{
	const std::set<std::string> globals{"RM_S"};
	const std::string fixed = "function RunSaveMenu()\n"
	                          "  local RM_SC = RM.FIT()\n"
	                          "  local x = RM_S(\"save_game\", \"Save Game\")\n"
	                          "end\n";
	CHECK(FindShadowedCalls(LexLua(fixed), globals, "a").empty());

	const std::string alias = "function RunGameMenu()\n"
	                          "  local rmstr = RM_S\n"
	                          "  local y = rmstr(\"k\", \"v\")\n"
	                          "end\n"
	                          "function Other()\n"
	                          "  local RM_S = RM_S\n"
	                          "  return RM_S(\"k\", \"v\")\n"
	                          "end\n";
	CHECK(FindShadowedCalls(LexLua(alias), globals, "b").empty());

	const std::string strings = "function F()\n"
	                            "  local RM_S = 1 -- RM_S(\"x\")\n"
	                            "  local s = \"RM_S(\" .. [[RM_S(]]\n"
	                            "  local t = M.RM_S(1)\n"
	                            "end\n";
	CHECK(FindShadowedCalls(LexLua(strings), globals, "c").empty());

	const std::string inner = "function F()\n"
	                          "  if a then\n"
	                          "    local RM_S = 1\n"
	                          "  else\n"
	                          "    RM_S(\"k\", \"v\")\n"
	                          "  end\n"
	                          "  for i = 1, 2 do local RM_S = i end\n"
	                          "  return RM_S(\"k\", \"v\")\n"
	                          "end\n";
	CHECK(FindShadowedCalls(LexLua(inner), globals, "d").empty());

	const std::string param = "function G(RM_S)\n"
	                          "  return RM_S(\"k\")\n"
	                          "end\n";
	CHECK(FindShadowedCalls(LexLua(param), globals, "e").size() == 1);
}

TEST_CASE("lua shadow scan: no device script calls a global function its own local hides")
{
	const std::filesystem::path root(WARGUSHD_DEVICE_SCRIPTS_DIR);
	if (!std::filesystem::is_directory(root)) {
		MESSAGE("device scripts not found at " << root.string() << "; scan skipped");
		return;
	}
	std::vector<std::filesystem::path> files;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
		if (entry.is_regular_file() && entry.path().extension() == ".lua") {
			files.push_back(entry.path());
		}
	}
	REQUIRE(files.size() > 10);

	std::set<std::string> globals;
	std::vector<std::vector<LuaToken>> tokens;
	for (const auto &file : files) {
		tokens.push_back(LexLua(ReadAll(file)));
		CollectGlobalFunctions(tokens.back(), globals);
	}
	CHECK(globals.count("RM_S") == 1);

	std::vector<std::string> found;
	for (size_t i = 0; i < files.size(); ++i) {
		const auto hits = FindShadowedCalls(tokens[i], globals,
		                                    files[i].lexically_relative(root).generic_string());
		found.insert(found.end(), hits.begin(), hits.end());
	}
	for (const auto &hit : found) {
		MESSAGE("shadowed global call: " << hit);
	}
	CHECK(found.empty());
}
