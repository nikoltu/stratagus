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
/// @file globalsave.h - Warcraft II: Remastered GlobalSave.json (campaign progress).
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#ifndef __GLOBALSAVE_H__
#define __GLOBALSAVE_H__

#include <string>

/**
**  Campaign progress in the ORIGINAL Remastered's own store.
**
**  Ground truth (Warcraft II.exe + the shipped file on this machine):
**    - the executable's string table holds, contiguously and in this order,
**      "GlobalSave.json", "todOrc", "todHuman", "bdpOrc", "bdpHuman", "missionsCompleted"
**      (rva 0x4214b4..0x4214ec), and "missionsCompleted" is referenced from the
**      mission-select builder region at 0x51c111;
**    - the real file, written by the original client, is
**      `<Saved Games>/Warcraft2Remastered/GlobalSave.json` and contains exactly:
**        {"missionsCompleted":{"todOrc":1,"todHuman":0,"bdpOrc":0,"bdpHuman":1}}
**
**  Campaign progress lives outside the save game (CF8), so deleting a save does not delete
**  progress. The original's file is written, in the original's schema, under the user
**  directory.
**
**  Keys are the original's: "todOrc", "todHuman", "bdpOrc", "bdpHuman".
*/
/// Monotonic: a lower value never overwrites a higher one, so a restarted or replayed
/// mission cannot take progress away.
extern void SetGlobalCampaignProgress(const std::string &campaignKey, int missionsCompleted);

#endif
