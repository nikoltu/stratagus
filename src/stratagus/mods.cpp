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
/**@name mods.cpp - The enabled mods and where their files are found. */
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

#include "stratagus.h"

#include "mods.h"

#include "iolib.h"
#include "json.h"
#include "script.h"

#include <algorithm>
#include <fstream>
#include <iterator>

/// The enabled mods of this start, in load order
static std::vector<std::string> EnabledMods;
/// The mods directory when it is not the default one
static fs::path RootOverride;
/// Per enabled mod that gives unit voices, in load order: its id and the game file names it
/// replaces (each one present in its sounds/units/ and not empty)
static std::vector<std::pair<std::string, std::vector<std::string>>> UnitSounds;

/// A definition file larger than this is not read
constexpr long ModsJsonLimit = 1024 * 1024;

bool ModsActive()
{
	return !EnabledMods.empty();
}

bool ModsIsPath(std::string_view file)
{
	return file.substr(0, 5) == "mods/";
}

fs::path ModsRoot()
{
	if (!RootOverride.empty()) {
		return RootOverride;
	}
	return (fs::path(StratagusLibPath) / ".." / "mods").lexically_normal();
}

void ModsSetRoot(const fs::path &root)
{
	RootOverride = root;
}

/// A mod id: lower-case letters, digits and the hyphen, 3 to 40 characters
static bool IsModId(std::string_view id)
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

static bool IsEnabled(const std::string &id)
{
	return std::find(EnabledMods.begin(), EnabledMods.end(), id) != EnabledMods.end();
}

/// A game sound file's own name as a mod's unit voice: letters, digits, '_' and '-', ".wav"
static bool IsUnitSoundName(std::string_view name)
{
	if (name.size() < 5 || name.size() > 64) {
		return false;
	}
	const std::string_view stem = name.substr(0, name.size() - 4);
	const std::string_view ext = name.substr(name.size() - 4);
	if (ext != ".wav") {
		return false;
	}
	for (const char ch : stem) {
		if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
		      || ch == '_' || ch == '-')) {
			return false;
		}
	}
	return true;
}

/// Read what an enabled mod declares as unit voices: defs/voices.json "units", a list of the
/// game's file names, each found as sounds/units/<name> in the mod
static void ReadUnitSounds(const std::string &id)
{
	content::JsonValue defs;
	if (!ModsReadJson("mods/" + id + "/defs/voices.json", defs)) {
		return;
	}
	const content::JsonValue *units = defs.find("units");
	if (units == nullptr || !units->isArray()) {
		return;
	}
	std::vector<std::string> names;
	for (const content::JsonValue &item : units->items()) {
		if (item.isString() && IsUnitSoundName(item.string)
		    && std::find(names.begin(), names.end(), item.string) == names.end()
		    && ModsFileSize("mods/" + id + "/sounds/units/" + item.string) > 0) {
			names.push_back(item.string);
		}
	}
	if (!names.empty()) {
		UnitSounds.emplace_back(id, std::move(names));
	}
}

void ModsSetEnabled(const std::vector<std::string> &ids)
{
	EnabledMods.clear();
	UnitSounds.clear();
	const fs::path root = ModsRoot();
	for (const std::string &id : ids) {
		std::error_code ec;
		if (IsModId(id) && fs::is_directory(root / id, ec) && !IsEnabled(id)) {
			EnabledMods.push_back(id);
		}
	}
	for (const std::string &id : EnabledMods) {
		ReadUnitSounds(id);
	}
}

const std::vector<std::string> &ModsEnabled()
{
	return EnabledMods;
}

fs::path ModsLocate(std::string_view file)
{
	if (!ModsIsPath(file)) {
		return {};
	}
	const std::string_view inside = file.substr(5);
	const size_t slash = inside.find('/');
	if (slash == std::string_view::npos || slash + 1 >= inside.size()) {
		return {};
	}
	const std::string id(inside.substr(0, slash));
	if (!IsEnabled(id)) {
		return {};
	}
	// The rest names a file below the mod's directory and cannot lead out of it.
	const std::string_view rest = inside.substr(slash + 1);
	if (rest.find_first_of("\\:") != std::string_view::npos) {
		return {};
	}
	fs::path result = ModsRoot() / id;
	size_t from = 0;
	while (from <= rest.size()) {
		size_t to = rest.find('/', from);
		if (to == std::string_view::npos) {
			to = rest.size();
		}
		const std::string_view part = rest.substr(from, to - from);
		if (part.empty() || part == "." || part == "..") {
			return {};
		}
		result /= fs::path(std::string(part));
		from = to + 1;
	}
	return result;
}

long ModsFileSize(std::string_view file)
{
	const fs::path path = ModsLocate(file);
	std::error_code ec;
	if (path.empty() || !fs::is_regular_file(path, ec)) {
		return -1;
	}
	const auto size = fs::file_size(path, ec);
	return ec ? -1 : static_cast<long>(size);
}

bool ModsReadJson(std::string_view file, content::JsonValue &out)
{
	const long size = ModsFileSize(file);
	if (size <= 0 || size > ModsJsonLimit) {
		return false;
	}
	std::ifstream in(ModsLocate(file), std::ios::binary);
	if (!in) {
		return false;
	}
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::string err;
	if (!content::parseJson(text, out, err)) {
		ErrorPrint("Mod file '%s': %s\n", std::string(file).c_str(), err.c_str());
		return false;
	}
	return true;
}

std::string ModsUnitSound(std::string_view file)
{
	if (UnitSounds.empty() || ModsIsPath(file)) {
		return {};
	}
	const size_t slash = file.find_last_of("/\\");
	const std::string_view name = slash == std::string_view::npos ? file : file.substr(slash + 1);
	for (const auto &[id, names] : UnitSounds) {
		if (std::find(names.begin(), names.end(), name) != names.end()) {
			return "mods/" + id + "/sounds/units/" + std::string(name);
		}
	}
	return {};
}

/// Push a JSON value as the Lua value of the same shape (an array counts from 1)
static void PushJson(lua_State *l, const content::JsonValue &value)
{
	switch (value.type) {
		case content::JsonType::Bool:
			lua_pushboolean(l, value.boolean);
			break;
		case content::JsonType::Number:
			lua_pushnumber(l, value.number);
			break;
		case content::JsonType::String:
			lua_pushlstring(l, value.string.data(), value.string.size());
			break;
		case content::JsonType::Array: {
			lua_newtable(l);
			int n = 0;
			for (const content::JsonValue &item : value.items()) {
				PushJson(l, item);
				lua_rawseti(l, -2, ++n);
			}
			break;
		}
		case content::JsonType::Object:
			lua_newtable(l);
			if (value.object) {
				for (const auto &[key, member] : *value.object) {
					lua_pushlstring(l, key.data(), key.size());
					PushJson(l, member);
					lua_rawset(l, -3);
				}
			}
			break;
		default:
			lua_pushnil(l);
			break;
	}
}

/**
**  Set the enabled mods of this start.
**
**  @param l  Lua state: one table of mod ids, in load order.
**
**  @return   The number of mods that are enabled.
*/
static int CclModsEnable(lua_State *l)
{
	LuaCheckArgs(l, 1);
	if (!lua_istable(l, 1)) {
		LuaError(l, "incorrect argument");
	}
	std::vector<std::string> ids;
	const int count = static_cast<int>(lua_rawlen(l, 1));
	for (int i = 1; i <= count; ++i) {
		lua_rawgeti(l, 1, i);
		if (lua_isstring(l, -1)) {
			ids.emplace_back(lua_tostring(l, -1));
		}
		lua_pop(l, 1);
	}
	ModsSetEnabled(ids);
	lua_pushnumber(l, static_cast<lua_Number>(EnabledMods.size()));
	return 1;
}

/**
**  Size of a mod's file.
**
**  @param l  Lua state: the file as "mods/<id>/...".
**
**  @return   Its size in bytes, -1 if there is no such file.
*/
static int CclModsFileSize(lua_State *l)
{
	LuaCheckArgs(l, 1);
	lua_pushnumber(l, static_cast<lua_Number>(ModsFileSize(LuaToString(l, 1))));
	return 1;
}

/**
**  Read a mod's JSON file.
**
**  @param l  Lua state: the file as "mods/<id>/...".
**
**  @return   Its content as a table, nil if it cannot be read.
*/
static int CclModsReadJson(lua_State *l)
{
	LuaCheckArgs(l, 1);
	content::JsonValue root;
	if (!ModsReadJson(LuaToString(l, 1), root)) {
		lua_pushnil(l);
		return 1;
	}
	PushJson(l, root);
	return 1;
}

void ModsCclRegister()
{
	lua_register(Lua, "ModsEnable", CclModsEnable);
	lua_register(Lua, "ModsFileSize", CclModsFileSize);
	lua_register(Lua, "ModsReadJson", CclModsReadJson);
}
