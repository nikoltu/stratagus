//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ \
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/// @file wc2strings.cpp - Warcraft II: Remastered text (Data/Strings/<lang>.json).
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#include "stratagus.h"
#include "wc2strings.h"

#include "iolib.h"
#include "json.h"
#include "mods.h"
#include "script.h"
#include "translate.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

static std::map<std::string, std::string> WC2Strings;
/// family ("unit"/"spell"/"upgrade"/...) -> lowercased display name -> numeric id
static std::map<std::string, std::map<std::string, int>> WC2IdByName;
/// family -> object ident -> numeric id, filled by the scripts (SetWC2IdForIdent)
static std::map<std::string, std::map<std::string, int>> WC2IdByIdent;
static const std::string EmptyString;
static std::string LoadedLanguage;
/// The texts an enabled language mod gives (M2): asked before WC2Strings. The base table and the
/// name index stay as the game's own file made them, so a translated name never changes an id.
static std::map<std::string, std::string> WC2Overlay;
/// The mod's texts for keyless English literals (M3), asked by Translate before the catalog.
static std::map<std::string, std::string> ExtraStrings;
/// LANGUAGE-ENGINE: the game's own table of the language the launcher chose (Data/Strings/<lang>
/// .json of the player's installation), asked after the mod overlay and before the enUS base. The
/// base and its name index stay enUS, so a translated name never changes an id; a key this table
/// lacks (or leaves empty) keeps the English text.
static std::map<std::string, std::string> WC2Language;
static std::string WC2LanguageCode;

static std::string Normalise(const std::string &s)
{
	size_t b = s.find_first_not_of(" \t\r\n");
	size_t e = s.find_last_not_of(" \t\r\n");
	if (b == std::string::npos) {
		return std::string();
	}
	std::string out = s.substr(b, e - b + 1);
	std::transform(out.begin(), out.end(), out.begin(),
				   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

/// "unit_12" -> family "unit", id 12. Keys with a suffix ("unit_12_tooltip") are not index entries.
static bool SplitNumberedKey(const std::string &key, std::string &family, int &id)
{
	const size_t us = key.rfind('_');
	if (us == std::string::npos || us + 1 >= key.size()) {
		return false;
	}
	const std::string num = key.substr(us + 1);
	if (num.find_first_not_of("0123456789") != std::string::npos) {
		return false;
	}
	family = key.substr(0, us);
	id = std::stoi(num);
	return true;
}

/// Replace the table and the name index by the content of `text`. `err` is set when the text is
/// not a JSON object; a well-formed object without a single string gives false with no error.
static bool ParseWC2Strings(const std::string &text, std::string &err)
{
	content::JsonValue root;
	if (!content::parseJson(text, root, err) || !root.isObject()) {
		if (err.empty()) {
			err = "not an object";
		}
		return false;
	}

	WC2Strings.clear();
	WC2IdByName.clear();
	LoadedLanguage.clear();
	for (const auto &[key, value] : *root.object) {
		if (!value.isString()) {
			continue;   // the file is flat; anything else is not usable text
		}
		WC2Strings[key] = value.string;

		// Index the plain "<family>_<id>" entries by their display name so a Wargus unit/spell/
		// upgrade can find its Remastered id from the name it already carries.
		std::string family;
		int id = 0;
		if (SplitNumberedKey(key, family, id)) {
			const std::string norm = Normalise(value.string);
			if (!norm.empty()) {
				auto &byName = WC2IdByName[family];
				// First key wins: the families repeat a name for the human/orc pair (e.g. two
				// "Oil Tanker" entries). The keys arrive in text order ("unit_16" before
				// "unit_2"), so an object that has to reach its own record of such a pair is
				// registered by its ident instead (SetWC2IdForIdent).
				byName.emplace(norm, id);
			}
		}
	}
	return !WC2Strings.empty();
}

bool LoadWC2Strings(const std::string &language)
{
	const std::string lang = language.empty() ? std::string("enUS") : language;
	if (lang == LoadedLanguage && !WC2Strings.empty()) {
		return true;
	}

	// Resolved through the normal library search path, which includes the folder the player picked
	// at the first-run gate (see LibraryFileNameImpl / WC2SourcePath).
	const fs::path path = LibraryFileName("Data/Strings/" + lang + ".json");
	std::string text;
	{
		CFile f;
		if (f.open(path.string().c_str(), CL_OPEN_READ) == -1) {
			ErrorPrint("Remastered strings not found: '%s'\n", path.u8string().c_str());
			return false;
		}
		char buf[8192];
		int n;
		while ((n = f.read(buf, sizeof(buf))) > 0) {
			text.append(buf, n);
		}
		f.close();
	}

	std::string err;
	if (!ParseWC2Strings(text, err)) {
		if (!err.empty()) {
			ErrorPrint("Remastered strings '%s' are malformed: %s\n", path.u8string().c_str(), err.c_str());
		}
		return false;
	}
	LoadedLanguage = lang;
	return true;
}

bool LoadWC2StringsFromText(const std::string &jsonText)
{
	std::string err;
	return ParseWC2Strings(jsonText, err);
}

int UseWC2StringsOverlay(const content::JsonValue &root)
{
	WC2Overlay.clear();
	if (!root.isObject()) {
		return -1;
	}
	for (const auto &[key, value] : *root.object) {
		if (value.isString() && !value.string.empty()) {
			WC2Overlay[key] = value.string;
		}
	}
	return static_cast<int>(WC2Overlay.size());
}

int UseWC2StringsOverlayText(const std::string &jsonText)
{
	content::JsonValue root;
	std::string err;
	if (!content::parseJson(jsonText, root, err)) {
		WC2Overlay.clear();
		return -1;
	}
	return UseWC2StringsOverlay(root);
}

void ClearWC2StringsOverlay()
{
	WC2Overlay.clear();
}

bool IsWC2LanguageCode(const std::string &code)
{
	// The game's codes are four letters, two small and two capital ("deDE", "zhTW"): the only
	// shape that may become a file name.
	return code.size() == 4 && std::islower(static_cast<unsigned char>(code[0]))
		   && std::islower(static_cast<unsigned char>(code[1]))
		   && std::isupper(static_cast<unsigned char>(code[2]))
		   && std::isupper(static_cast<unsigned char>(code[3]));
}

int UseWC2LanguageText(const std::string &language, const std::string &jsonText)
{
	WC2Language.clear();
	WC2LanguageCode.clear();
	content::JsonValue root;
	std::string err;
	if (!content::parseJson(jsonText, root, err) || !root.isObject()) {
		return -1;
	}
	for (const auto &[key, value] : *root.object) {
		if (value.isString() && !value.string.empty()) {
			WC2Language[key] = value.string;
		}
	}
	if (!WC2Language.empty()) {
		WC2LanguageCode = language;
	}
	return static_cast<int>(WC2Language.size());
}

int UseWC2Language(const std::string &language)
{
	if (language.empty() || language == "enUS") {
		ClearWC2Language();
		return 0;   // the base table is the game's English
	}
	if (!IsWC2LanguageCode(language)) {
		ClearWC2Language();
		return -1;
	}
	if (language == WC2LanguageCode && !WC2Language.empty()) {
		return static_cast<int>(WC2Language.size());
	}
	ClearWC2Language();
	// Only the player's own file, read at run time; a language the game does not ship (a mod's,
	// e.g. ltLT) has no file here and leaves the English base to the mod's overlay.
	const fs::path path = LibraryFileName("Data/Strings/" + language + ".json");
	std::string text;
	{
		CFile f;
		if (f.open(path.string().c_str(), CL_OPEN_READ) == -1) {
			DebugPrint("No game string table for '%s'\n", language.c_str());
			return -1;
		}
		char buf[8192];
		int n;
		while ((n = f.read(buf, sizeof(buf))) > 0) {
			text.append(buf, n);
		}
		f.close();
	}
	const int count = UseWC2LanguageText(language, text);
	if (count < 0) {
		ErrorPrint("Remastered strings '%s' are malformed\n", path.u8string().c_str());
	}
	return count;
}

void ClearWC2Language()
{
	WC2Language.clear();
	WC2LanguageCode.clear();
}

const std::string &GetWC2Language()
{
	static const std::string English("enUS");
	return WC2LanguageCode.empty() ? English : WC2LanguageCode;
}

int UseExtraStrings(const content::JsonValue &root)
{
	ExtraStrings.clear();
	if (!root.isObject()) {
		return -1;
	}
	auto table = root.object->find("strings");
	if (table == root.object->end() || !table->second.isObject()) {
		return -1;
	}
	for (const auto &[english, value] : *table->second.object) {
		if (!english.empty() && value.isString() && !value.string.empty()) {
			ExtraStrings[english] = value.string;
		}
	}
	return static_cast<int>(ExtraStrings.size());
}

int UseExtraStringsText(const std::string &jsonText)
{
	content::JsonValue root;
	std::string err;
	if (!content::parseJson(jsonText, root, err)) {
		ExtraStrings.clear();
		return -1;
	}
	return UseExtraStrings(root);
}

void ClearExtraStrings()
{
	ExtraStrings.clear();
}

const std::string *GetExtraString(const std::string &english)
{
	if (ExtraStrings.empty()) {
		return nullptr;
	}
	auto it = ExtraStrings.find(english);
	return it == ExtraStrings.end() ? nullptr : &it->second;
}

std::string WC2Or(const std::string &key, const std::string &fallback)
{
	const std::string &s = GetWC2String(key);
	if (!s.empty()) {
		return s;
	}
	return Translate(fallback);
}

const std::string &GetWC2String(const std::string &key)
{
	if (!WC2Overlay.empty()) {
		auto own = WC2Overlay.find(key);
		if (own != WC2Overlay.end()) {
			return own->second;
		}
	}
	if (!WC2Language.empty()) {
		auto chosen = WC2Language.find(key);
		if (chosen != WC2Language.end()) {
			return chosen->second;
		}
	}
	if (WC2Strings.empty()) {
		LoadWC2Strings("enUS");
	}
	auto it = WC2Strings.find(key);
	return it == WC2Strings.end() ? EmptyString : it->second;
}

int GetWC2StringCount()
{
	return static_cast<int>(WC2Strings.size());
}

int GetWC2Id(const std::string &family, const std::string &displayName)
{
	if (WC2Strings.empty()) {
		LoadWC2Strings("enUS");
	}
	auto fam = WC2IdByName.find(family);
	if (fam == WC2IdByName.end()) {
		return -1;
	}
	auto it = fam->second.find(Normalise(displayName));
	return it == fam->second.end() ? -1 : it->second;
}

void SetWC2IdForIdent(const std::string &family, const std::string &ident, int id)
{
	if (family.empty() || ident.empty()) {
		return;
	}
	if (id < 0) {
		auto fam = WC2IdByIdent.find(family);
		if (fam != WC2IdByIdent.end()) {
			fam->second.erase(ident);
		}
		return;
	}
	WC2IdByIdent[family][ident] = id;
}

int GetWC2IdForIdent(const std::string &family, const std::string &ident)
{
	auto fam = WC2IdByIdent.find(family);
	if (fam == WC2IdByIdent.end()) {
		return -1;
	}
	auto it = fam->second.find(ident);
	return it == fam->second.end() ? -1 : it->second;
}

int ResolveWC2Id(const std::string &family, const std::string &ident,
				 const std::string &displayName)
{
	const int id = GetWC2IdForIdent(family, ident);
	return id >= 0 ? id : GetWC2Id(family, displayName);
}

/*----------------------------------------------------------------------------
--  Credits (Data/Strings/credits.json)
----------------------------------------------------------------------------*/

namespace
{

/// A JSON value that keeps the members of an object in the order of the text (content::JsonValue
/// keeps them in a map, which sorts them; the credits need the file's order).
struct OrderedJson
{
	enum Type { Null, Scalar, Array, Object } type = Null;
	std::string scalar;   // a string as text; numbers and literals as written
	std::vector<OrderedJson> items;
	std::vector<std::pair<std::string, OrderedJson>> members;

	const OrderedJson *find(const std::string &key) const
	{
		for (const auto &[k, v] : members) {
			if (k == key) {
				return &v;
			}
		}
		return nullptr;
	}
};

class OrderedJsonParser
{
public:
	explicit OrderedJsonParser(const std::string &text) : s(text)
	{
		if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) {
			pos = 3;
		}
	}

	bool parse(OrderedJson &out)
	{
		if (!value(out, 0)) {
			return false;
		}
		skip();
		return pos == s.size();
	}

private:
	const std::string &s;
	size_t pos = 0;

	void skip()
	{
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) {
			++pos;
		}
	}

	static void putUtf8(std::string &out, unsigned long c)
	{
		if (c < 0x80) {
			out += static_cast<char>(c);
		} else if (c < 0x800) {
			out += static_cast<char>(0xC0 | (c >> 6));
			out += static_cast<char>(0x80 | (c & 0x3F));
		} else if (c < 0x10000) {
			out += static_cast<char>(0xE0 | (c >> 12));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		} else {
			out += static_cast<char>(0xF0 | (c >> 18));
			out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
	}

	bool hex4(unsigned long &c)
	{
		if (pos + 4 > s.size()) {
			return false;
		}
		c = 0;
		for (int i = 0; i < 4; ++i) {
			const char h = s[pos++];
			c <<= 4;
			if (h >= '0' && h <= '9') {
				c |= h - '0';
			} else if (h >= 'a' && h <= 'f') {
				c |= h - 'a' + 10;
			} else if (h >= 'A' && h <= 'F') {
				c |= h - 'A' + 10;
			} else {
				return false;
			}
		}
		return true;
	}

	bool string(std::string &out)
	{
		if (pos >= s.size() || s[pos] != '"') {
			return false;
		}
		++pos;
		while (pos < s.size()) {
			const char c = s[pos++];
			if (c == '"') {
				return true;
			}
			if (c != '\\') {
				out += c;
				continue;
			}
			if (pos >= s.size()) {
				return false;
			}
			const char e = s[pos++];
			switch (e) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					unsigned long c1;
					if (!hex4(c1)) {
						return false;
					}
					// A surrogate pair is one character.
					if (c1 >= 0xD800 && c1 <= 0xDBFF && pos + 1 < s.size() && s[pos] == '\\'
						&& s[pos + 1] == 'u') {
						pos += 2;
						unsigned long c2;
						if (!hex4(c2)) {
							return false;
						}
						c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
					}
					putUtf8(out, c1);
					break;
				}
				default: return false;
			}
		}
		return false;
	}

	bool value(OrderedJson &out, int depth)
	{
		if (depth > 64) {
			return false;
		}
		skip();
		if (pos >= s.size()) {
			return false;
		}
		const char c = s[pos];
		if (c == '{') {
			out.type = OrderedJson::Object;
			++pos;
			skip();
			if (pos < s.size() && s[pos] == '}') {
				++pos;
				return true;
			}
			for (;;) {
				skip();
				std::string key;
				if (!string(key)) {
					return false;
				}
				skip();
				if (pos >= s.size() || s[pos] != ':') {
					return false;
				}
				++pos;
				out.members.emplace_back(std::move(key), OrderedJson());
				if (!value(out.members.back().second, depth + 1)) {
					return false;
				}
				skip();
				if (pos < s.size() && s[pos] == ',') {
					++pos;
				} else if (pos < s.size() && s[pos] == '}') {
					++pos;
					return true;
				} else {
					return false;
				}
			}
		}
		if (c == '[') {
			out.type = OrderedJson::Array;
			++pos;
			skip();
			if (pos < s.size() && s[pos] == ']') {
				++pos;
				return true;
			}
			for (;;) {
				out.items.emplace_back();
				if (!value(out.items.back(), depth + 1)) {
					return false;
				}
				skip();
				if (pos < s.size() && s[pos] == ',') {
					++pos;
				} else if (pos < s.size() && s[pos] == ']') {
					++pos;
					return true;
				} else {
					return false;
				}
			}
		}
		if (c == '"') {
			out.type = OrderedJson::Scalar;
			return string(out.scalar);
		}
		// number, true, false, null: kept as written
		static const std::string ends = "{}[],: \t\r\n\"";
		const size_t start = pos;
		while (pos < s.size() && ends.find(s[pos]) == std::string::npos) {
			++pos;
		}
		if (pos == start) {
			return false;
		}
		out.scalar = s.substr(start, pos - start);
		out.type = out.scalar == "null" ? OrderedJson::Null : OrderedJson::Scalar;
		return true;
	}
};

/// The items of `v` when it is an array; a single object counts as a list of one.
std::vector<const OrderedJson *> ListOf(const OrderedJson *v)
{
	std::vector<const OrderedJson *> out;
	if (v == nullptr) {
		return out;
	}
	if (v->type == OrderedJson::Array) {
		for (const auto &item : v->items) {
			out.push_back(&item);
		}
	} else if (v->type == OrderedJson::Object) {
		out.push_back(v);
	}
	return out;
}

/// The attribute `name` of an element (its "$" object), or "" when it has none.
std::string Attribute(const OrderedJson &element, const char *name)
{
	const OrderedJson *attrs = element.find("$");
	const OrderedJson *v = attrs ? attrs->find(name) : nullptr;
	return (v != nullptr && v->type == OrderedJson::Scalar) ? v->scalar : std::string();
}

void AddLine(std::vector<WC2CreditLine> &out, const char *kind, const std::string &text)
{
	if (!text.empty()) {
		out.push_back({kind, text});
	}
}

void AddGap(std::vector<WC2CreditLine> &out)
{
	out.push_back({"gap", std::string()});
}

} // namespace

bool ParseWC2Credits(const std::string &jsonText, std::vector<WC2CreditLine> &out)
{
	out.clear();
	OrderedJson root;
	if (!OrderedJsonParser(jsonText).parse(root) || root.type != OrderedJson::Object) {
		return false;
	}
	const OrderedJson *xml = root.find("xml");
	for (const OrderedJson *credits : ListOf(xml ? xml->find("credits") : nullptr)) {
		for (const OrderedJson *group : ListOf(credits->find("group"))) {
			const std::string title = Attribute(*group, "name");
			if (!title.empty()) {
				AddLine(out, "group", title);
				AddGap(out);
			}
			for (const auto &[key, value] : group->members) {
				if (key != "role" && key != "thirdParty") {
					continue;
				}
				for (const OrderedJson *item : ListOf(&value)) {
					const size_t before = out.size();
					AddLine(out, "entry", Attribute(*item, "name"));
					for (const auto &[k, v] : item->members) {
						if (k == "creditee") {
							for (const OrderedJson *who : ListOf(&v)) {
								AddLine(out, "name", Attribute(*who, "name"));
							}
						} else if (k == "_" && v.type == OrderedJson::Scalar) {
							AddLine(out, "text", v.scalar);
						}
					}
					if (out.size() != before) {
						AddGap(out);
					}
				}
			}
		}
	}
	return !out.empty();
}

bool ReadWC2Credits(const std::string &path, std::vector<WC2CreditLine> &out)
{
	out.clear();
	std::string text;
	CFile f;
	if (f.open(path.c_str(), CL_OPEN_READ) == -1) {
		return false;
	}
	char buf[8192];
	int n;
	while ((n = f.read(buf, sizeof(buf))) > 0) {
		text.append(buf, n);
	}
	f.close();
	return ParseWC2Credits(text, out);
}

/**
**  RM_Credits(): the credits of the player's installation as a list of {kind = ..., text = ...}
**  (see WC2CreditLine), or nil when Data/Strings/credits.json is absent or unreadable.
*/
static int CclRMCredits(lua_State *l)
{
	LuaCheckArgs(l, 0);
	// Resolved like the strings file, out of the folder the player picked.
	const fs::path path = LibraryFileName("Data/Strings/credits.json");
	std::vector<WC2CreditLine> lines;
	if (!ReadWC2Credits(path.string(), lines)) {
		lua_pushnil(l);
		return 1;
	}
	lua_createtable(l, static_cast<int>(lines.size()), 0);
	int i = 0;
	for (const auto &line : lines) {
		lua_createtable(l, 0, 2);
		lua_pushstring(l, line.kind.c_str());
		lua_setfield(l, -2, "kind");
		lua_pushlstring(l, line.text.data(), line.text.size());
		lua_setfield(l, -2, "text");
		lua_rawseti(l, -2, ++i);
	}
	return 1;
}

/**
**  RM_UseStrings(file): lay an enabled mod's string table ("mods/<id>/strings/<lang>.json") over
**  the game's own; a key it lacks keeps the game's text. RM_UseStrings(nil) takes it away.
**  Only a file of an enabled mod can be named (mods.cpp). Returns the number of texts it gives,
**  -1 when the file cannot be read or is not a JSON object (the game's text stays).
*/
static int CclRMUseStrings(lua_State *l)
{
	LuaCheckArgs(l, 1);
	if (lua_isnil(l, 1)) {
		ClearWC2StringsOverlay();
		lua_pushnumber(l, 0);
		return 1;
	}
	const std::string file(LuaToString(l, 1));
	content::JsonValue root;
	int count = -1;
	if (ModsActive() && ModsIsPath(file) && ModsReadJson(file, root)) {
		count = UseWC2StringsOverlay(root);
	} else {
		ClearWC2StringsOverlay();
	}
	lua_pushnumber(l, count);
	return 1;
}

/**
**  RM_UseExtraStrings(file): an enabled mod's texts for the keyless English literals
**  ("mods/<id>/strings/extra_<lang>.json"); RM_UseExtraStrings(nil) takes them away. Only a file
**  of an enabled mod can be named. Returns the number of texts it gives, -1 when the file cannot
**  be read or is not such an object (the English stays).
*/
static int CclRMUseExtraStrings(lua_State *l)
{
	LuaCheckArgs(l, 1);
	if (lua_isnil(l, 1)) {
		ClearExtraStrings();
		lua_pushnumber(l, 0);
		return 1;
	}
	const std::string file(LuaToString(l, 1));
	content::JsonValue root;
	int count = -1;
	if (ModsActive() && ModsIsPath(file) && ModsReadJson(file, root)) {
		count = UseExtraStrings(root);
	} else {
		ClearExtraStrings();
	}
	lua_pushnumber(l, count);
	return 1;
}

/**
**  RM_UseLanguage(code): the game's own table of the language `code` ("deDE", ...) from the
**  player's Data/Strings/<code>.json, between a mod's overlay and the English. "enUS", "" or nil
**  gives the English alone (0). Returns the number of texts taken, -1 when the code is not a game
**  code or the file is absent / unreadable (the English stays).
*/
static int CclRMUseLanguage(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const std::string code = lua_isnil(l, 1) ? std::string() : std::string(LuaToString(l, 1));
	lua_pushnumber(l, UseWC2Language(code));
	return 1;
}

/// RM_Language(): the language whose game table is in force ("enUS" when only the English is).
static int CclRMLanguage(lua_State *l)
{
	LuaCheckArgs(l, 0);
	lua_pushstring(l, GetWC2Language().c_str());
	return 1;
}

void WC2StringsCclRegister()
{
	lua_register(Lua, "RM_UseLanguage", CclRMUseLanguage);
	lua_register(Lua, "RM_Language", CclRMLanguage);
	lua_register(Lua, "RM_UseStrings", CclRMUseStrings);
	lua_register(Lua, "RM_UseExtraStrings", CclRMUseExtraStrings);
	lua_register(Lua, "RM_Credits", CclRMCredits);
}
