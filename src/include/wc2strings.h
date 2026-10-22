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
/// @file wc2strings.h - Warcraft II: Remastered text (Data/Strings/<lang>.json).
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#ifndef __WC2STRINGS_H__
#define __WC2STRINGS_H__

#include <string>
#include <vector>

/**
**  The Remastered ships its entire user-visible text in `Data/Strings/<lang>.json` -- 1599 keys in
**  enUS: 431 `unit_*` (names, build captions, tooltips, advice, requirements, flavour, bonus),
**  109 `upgrade_*`, 52 `spell_*`, 63 `tips_*`, 43 `rank_txt_*`, 26 `clan_*`, 22 `message_*`,
**  72 `option_*`, the campaign text and the classic `stat_txt_*` set.
**
**  This module makes the whole file available at runtime (TX1..TX9), read from the player's own
**  installation through the normal library search path (so it resolves out of the folder chosen
**  at the first-run gate).
*/

/// Load Data/Strings/<language>.json. Safe to call repeatedly; a failed load leaves the table empty.
extern bool LoadWC2Strings(const std::string &language);

/// Value for `key`, or an empty string when the key (or the file) is absent.
extern const std::string &GetWC2String(const std::string &key);

/// How many keys are loaded (0 = the original's text is not available).
extern int GetWC2StringCount();

/**
**  Reverse lookup by DISPLAY NAME within a numbered family, e.g. GetWC2Id("unit", "Grunt") -> 1,
**  so `unit_1_tooltip`, `unit_1_tooltip_advice` and the rest can be reached from a Wargus unit
**  whose own Name matches. Returns -1 when the name is not in the family.
**  Matching is case-insensitive and ignores surrounding whitespace; nothing else is normalised,
**  because a name that does not match exactly is a mapping question, not something to guess at.
*/
extern int GetWC2Id(const std::string &family, const std::string &displayName);

/**
**  The id of a game object by its IDENT ("unit-orc-shipyard", "upgrade-sword1", "spell-healing")
**  within a numbered family. The families repeat a display name for the human / orc pair and for
**  the two levels of an upgrade, so a name cannot select one of two records; the ident can. The
**  scripts fill the registry (RM_SetId). It holds idents and numbers only and does not depend on
**  the loaded language.
**  A negative id removes the entry.
*/
extern void SetWC2IdForIdent(const std::string &family, const std::string &ident, int id);

/// The registered id of `ident` in `family`, or -1. Does not read the string file.
extern int GetWC2IdForIdent(const std::string &family, const std::string &ident);

/// The registered id of `ident` when there is one, otherwise GetWC2Id(family, displayName).
extern int ResolveWC2Id(const std::string &family, const std::string &ident,
						const std::string &displayName);

/**
**  Fill the table from the text of a flat JSON object instead of a file (LoadWC2Strings reads the
**  file and passes it here). A text that is not a JSON object leaves the table as it was and
**  gives false; an object without a single string empties the table and gives false.
*/
extern bool LoadWC2StringsFromText(const std::string &jsonText);

namespace content
{
class JsonValue;
}

/**
**  A language mod's texts laid over the table (M2_LANGUAGE_MOD_SPEC): GetWC2String gives the
**  overlay's text for a key it has, else the game's own. The name index (GetWC2Id) is not
**  touched. Returns the number of texts taken, -1 (overlay empty) when `root` is not an object.
*/
extern int UseWC2StringsOverlay(const content::JsonValue &root);
/// As UseWC2StringsOverlay, from JSON text; -1 when the text is not a JSON object.
extern int UseWC2StringsOverlayText(const std::string &jsonText);
/// Remove the overlay: every text is the game's own again.
extern void ClearWC2StringsOverlay();

/**
**  LANGUAGE-ENGINE: the game's own table of the language the launcher chose, read from the
**  player's Data/Strings/<language>.json. GetWC2String asks, in order: a language mod's overlay,
**  this table, the enUS base. The base and the name index (GetWC2Id) stay English; a key this
**  table lacks or leaves empty gives the English text. "enUS" or "" clears the table (0).
**  Returns the number of texts taken, -1 when the code is not of the game's shape (two small,
**  two capital letters), or the file is absent or not a JSON object (the table is then empty).
*/
extern int UseWC2Language(const std::string &language);
/// As UseWC2Language, from the file's text (host tests); -1 when not a JSON object.
extern int UseWC2LanguageText(const std::string &language, const std::string &jsonText);
/// Remove the language table: the English base alone (under any overlay).
extern void ClearWC2Language();
/// The language whose table is in force; "enUS" when none is.
extern const std::string &GetWC2Language();
/// True for a code of the game's shape ("deDE", "zhTW").
extern bool IsWC2LanguageCode(const std::string &code);

/**
**  A language mod's texts for the screen text that has no key in the game's table (M3): the
**  mod's strings/extra_<lang>.json, {"format": 1, "strings": {"<English literal>": "<text>"}},
**  keyed by the exact literal the scripts and the engine pass to _() (markup included). Translate
**  asks it before the gettext catalog. An empty text is not taken. Returns the number of texts
**  taken, -1 (table empty) when `root` is not such an object.
*/
extern int UseExtraStrings(const content::JsonValue &root);
/// As UseExtraStrings, from JSON text; -1 when the text is not a JSON object.
extern int UseExtraStringsText(const std::string &jsonText);
/// Remove the extra texts: _() answers from the catalog alone again.
extern void ClearExtraStrings();
/// The extra text for the English literal `english`, or nullptr when the mod gives none.
extern const std::string *GetExtraString(const std::string &english);

/**
**  The original's text for `key`, or the translation (_()) of `fallback` when the key is absent
**  or empty. The English fallback is the original's own wording where it has one.
*/
extern std::string WC2Or(const std::string &key, const std::string &fallback);

/**
**  The closing credits (Data/Strings/credits.json of the player's installation), in the order the
**  file lists them. The file is `xml.credits[].group[]`; a group has a name and `role` /
**  `thirdParty` lists; a role names its creditees (or carries them as one text `_`), a third
**  party has a name and sometimes a text `_`. Each line is one of:
**    "group"  a group's name (centred heading)
**    "entry"  a role's or a third party's name
**    "name"   one creditee of a role
**    "text"   the free text of a role or a third party (may hold line breaks)
**    "gap"    the empty line after a heading and after each role / third party
**  Members of an object are kept in the order of the text (a group may list its third parties
**  before its roles).
*/
struct WC2CreditLine
{
	std::string kind;
	std::string text;
};

/// Parse the text of a credits file. False (and `out` empty) when it is not one or lists nothing.
extern bool ParseWC2Credits(const std::string &jsonText, std::vector<WC2CreditLine> &out);

/// Read and parse the credits file at `path`. False when it cannot be read or parsed.
extern bool ReadWC2Credits(const std::string &path, std::vector<WC2CreditLine> &out);

/// Register RM_Credits() with Lua: a list of {kind = ..., text = ...} read from the player's
/// Data/Strings/credits.json, or nil when the file is absent or unreadable.
extern void WC2StringsCclRegister();

/**
**  TX8 convenience: the original's message text for `key`, or `fallback` when the player's string
**  file has no such key (or is absent). Lets a call site read as one line.
*/
inline const char *WC2Msg(const char *key, const char *fallback)
{
	const std::string &s = GetWC2String(key);
	return s.empty() ? fallback : s.c_str();
}

#endif
