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

/**
**  The Remastered ships its entire user-visible text in `Data/Strings/<lang>.json` -- 1599 keys in
**  enUS: 431 `unit_*` (names, build captions, tooltips, advice, requirements, flavour, bonus),
**  109 `upgrade_*`, 52 `spell_*`, 63 `tips_*`, 43 `rank_txt_*`, 26 `clan_*`, 22 `message_*`,
**  72 `option_*`, the campaign text and the classic `stat_txt_*` set.
**
**  Until now WargusHD extracted only the campaign keys into a generated Lua table, so every other
**  caption, tooltip and message in the game was Wargus's own wording rather than the original's
**  (audit AUDIT_ORIGINAL_VS_OURS_2026-08-21, items TX1..TX9). This module makes the WHOLE file
**  available at runtime, read from the player's own installation through the normal library search
**  path (so it resolves out of the folder chosen at the first-run gate).
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
**  TX8 convenience: the original's message text for `key`, or `fallback` when the player's string
**  file has no such key (or is absent). Lets a call site read as one line.
*/
inline const char *WC2Msg(const char *key, const char *fallback)
{
	const std::string &s = GetWC2String(key);
	return s.empty() ? fallback : s.c_str();
}

#endif
