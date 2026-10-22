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
//
//      (c) Copyright 2012 by Joris Dauphin
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

#ifndef GAME_H
#define GAME_H

#include "filesystem.h"

#include <string>

class CFile;

extern void LoadGame(const fs::path &filename); /// Load saved game
extern void PlaceLoadedUnits(); /// Place the units a saved game read (part of LoadGame)
extern int SaveGame(const std::string &filename); /// Save game
extern bool DeleteSaveGame(const std::string &filename); /// Delete save game (guarded)
/// The shape of the saved game file this build writes (the header's Format; a file without it is 0).
/// 3: a unit may carry "remembered-attacker" (a file of 2 or older reads as none remembered).
constexpr int SavedGameFormat = 3;
/// A file of this format can be read: any up to this build's own.
inline bool SavedGameFormatReadable(int format) { return format <= SavedGameFormat; }
/// An automatic save is written now: when a game starts (new or loaded), and every 60 s of wall time.
constexpr unsigned long AutosavePeriodMs = 60000; /// wall time between two timed automatic saves
extern bool AutosaveDue(unsigned long nowMs, unsigned long &nextMs, int minutes, bool network, bool replay,
                        bool gameStart);
/// A save is written beside its target and moved over it when complete: the name it is written
/// under (the file layer adds ".gz" itself), and the move. False: nothing was moved, the
/// unfinished file is removed and the target is as it was.
extern fs::path SaveGameTemporary(const fs::path &target);
extern bool CommitSaveGame(const fs::path &target, bool complete);
/// Compress and write a built save text under its temporary name, then move it over the target.
/// False: nothing was moved and the target is as it was.
extern bool WriteSaveText(const fs::path &target, const std::string &text);
/// The same on the save writer thread. False: a write is still running, nothing was started
/// (one in flight at a time; the caller keeps the text or tries again).
extern bool WriteSaveTextInBackground(const fs::path &target, std::string text);
extern bool SaveGameWriting();               /// a background save write is still running
extern void WaitForSaveGame();               /// wait until no save write is running
/// The automatic save: the text is built here on the game thread, the compression and the file
/// write run on the save writer thread. 0 started, 1 skipped (the last one still writes), -1 failed.
extern int SaveGameInBackground(const std::string &filename);
extern bool SaveGameLoading;                 /// Save game is in progress of loading

/// Create the player slots from the map's types and the presets; preset teams set alliances
/// and shared vision (part of CreateGame).
extern void CreatePresetPlayers();

extern void InitModules();              /// Initialize all modules
extern void LuaRegisterModules();       /// Register lua script of each modules
extern void LoadModules();              /// Load all modules
extern void CleanModules();             /// Cleanup all modules

extern void SaveGameSettings(CFile &file);             /// Save game settings

extern std::string GameName;                /// Name of the game
extern std::string FullGameName;            /// Full Name of the game

extern bool UseHPForXp;                     /// true if gain XP by dealing damage, false if by killing.

#endif // GAME_H

