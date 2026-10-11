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

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

static std::map<std::string, std::string> WC2Strings;
/// family ("unit"/"spell"/"upgrade"/...) -> lowercased display name -> numeric id
static std::map<std::string, std::map<std::string, int>> WC2IdByName;
static const std::string EmptyString;
static std::string LoadedLanguage;

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

	content::JsonValue root;
	std::string err;
	if (!content::parseJson(text, root, err) || !root.isObject()) {
		ErrorPrint("Remastered strings '%s' are malformed: %s\n", path.u8string().c_str(), err.c_str());
		return false;
	}

	WC2Strings.clear();
	WC2IdByName.clear();
	for (const auto &[key, value] : *root.object) {
		if (!value.isString()) {
			continue;   // the file is flat; anything else is not text we can use
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
				// "Oil Tanker" entries), and taking the lower id keeps the mapping stable.
				byName.emplace(norm, id);
			}
		}
	}
	LoadedLanguage = lang;
	return !WC2Strings.empty();
}

const std::string &GetWC2String(const std::string &key)
{
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
