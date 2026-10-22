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
/**@name loadgame.cpp - Load game. */
//
//      (c) Copyright 2001-2006 by Lutz Sammer, Andreas Arens
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

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "stratagus.h"

#include "actions.h"
#include "ai.h"
#include "commands.h"
#include "construct.h"
#include "depend.h"
#include "font.h"
#include "game.h"
#include "map.h"
#include "minimap.h"
#include "missile.h"
#include "particle.h"
#include "pathfinder.h"
#include "replay.h"
#include "script.h"
#include "sound.h"
#include "sound_server.h"
#include "spells.h"
#include "trigger.h"
#include "ui.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"
#include "upgrade.h"
#include "video.h"
#include "wc2bot.h"

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

bool SaveGameLoading;                 /// If a Saved Game is Loading

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/


/**
**  Cleanup modules.
**
**  Call each module to clean up.
*/
void CleanModules()
{
	EndReplayLog();
	CleanMessages();

	CleanIcons();
	CleanCursors();
	CleanUserInterface();
	CleanFonts();
	CleanTriggers();
	FreeAi();
	PlayerRaces.Clean();
	CleanConstructions();
	CleanDecorations();
	CleanMissiles();
	CleanUnits();
	CleanUnitTypes();
	CleanPlayers();
	CleanSelections();
	CleanGroups();
	CleanUpgrades();
	CleanDependencies();
	CleanButtons();
	CleanMissileTypes();
	Map.Clean(true);
	CParticleManager::exit();
	CleanReplayLog();
	CleanSpells();
	FreePathfinder();

	UnitTypeVar.Init(); // internal script. should be to a better place, don't find for restart.
}

/**
**  Initialize all modules.
**
**  Call each module to initialize.
*/
void InitModules()
{
	GameCycle = 0;
	FastForwardCycle = 0;
	SyncHash = 0;

	InitSyncRand();
	InitVideoCursors();
	InitUserInterface();
	InitPlayers();
	InitMissileTypes();
	InitMissiles();
	InitConstructions();

	// LUDO : 0 = don't reset player stats ( units level , upgrades, ... ) !
	InitUnitTypes(0);

	InitUnits();
	InitSpells();
	InitUpgrades();
	InitDependencies();

	InitButtons();
	InitTriggers();

	InitAiModule();

	Map.Init();
}

/**
**  Load all.
**
**  Call each module to load additional files (graphics,sounds).
*/
void LoadModules()
{
	LoadFonts();
	LoadIcons();
	LoadCursors(PlayerRaces.Name[ThisPlayer->Race]);
	UI.Load();
#ifndef DYNAMIC_LOAD
	LoadMissileSprites();
#endif
	LoadConstructions();
	LoadDecorations();
	LoadUnitTypes();

	InitPathfinder();

	LoadUnitSounds();
	MapUnitSounds();
	if (SoundEnabled()) {
		InitSoundClient();
	}

	SetPlayersPalette();
	UI.Minimap.Create();

	SetDefaultTextColors(UI.NormalFontColor, UI.ReverseFontColor);
}

/// Places every unit read from a saved game (game.h; host-tested in test_saveload.cpp).
void PlaceLoadedUnits()
{
	std::vector<CUnit *> offGrid;

	// FOW-SAVE (work/FOW_BUG.md, LOAD_CRASH_3473.md): units inside a container ("host-info" in the
	// file) are not marked here. Place() of the container marks its passengers through
	// MapMarkUnitSightRec, with the container's tiles, range and shape - exactly what leaving takes
	// away. A second mark here (3.4.73) counted every passenger twice: a stray +1 that never fogs.

	for (CUnit *unit : UnitManager->GetUnits()) {
		if (!unit->Removed) {
			const int step = unit->Type->GridStep;
			if (step > 1 && (unit->tilePos.x % step || unit->tilePos.y % step)
			    && !(unit->Type->CanTransport() && Map.Field(unit->tilePos)->CoastOnMap())) {
				offGrid.push_back(unit);
				continue;
			}
			unit->Removed = 1;
			unit->Place(unit->tilePos);
		}
	}
	// A game saved before such a type moved on a grid can hold it between grid tiles. It goes to the
	// nearest free grid tile, after everything else stands, and starts its order from there.
	for (CUnit *unit : offGrid) {
		Vec2i pos;
		FindNearestDrop(*unit->Type, unit->tilePos, pos, LookingW);
		unit->IX = 0;
		unit->IY = 0;
		unit->Moving = 0;
		if (unit->pathFinderData) {
			unit->pathFinderData->output.Length = 0;
			unit->pathFinderData->output.OverflowLength = 0;
		}
		unit->Removed = 1;
		unit->Place(pos);
	}
	if (!offGrid.empty()) {
		ErrorPrint("Saved game: %d unit(s) stood off their movement grid and were moved to the nearest free grid tile\n",
		           static_cast<int>(offGrid.size()));
	}
	// LOAD-CRASH-3473 (work/LOAD_CRASH_3473.md): every unit stands and every sight is marked; a
	// unit known under fog without a remembered look gets one, or the fog draw has no type.
	for (CUnit *unit : UnitManager->GetUnits()) {
		UnitFillMissingSeenAfterLoad(*unit);
	}
}

/**
**  Load a game to file.
**
**  @param filename  File name to be loaded.
*/
/// NG55: set when LoadGame gave up on a malformed save, so the caller can back out instead of
/// starting a map from half-loaded state.
bool LastLoadFailed = false;

void LoadGame(const fs::path &filename)
{
	WaitForSaveGame(); // a save still being written is complete before any file is read
	// log will be enabled if found in the save game
	CommandLogDisabled = true;
	SaveGameLoading = true;

	SetDefaultTextColors(FontYellow, FontWhite);
	LoadFonts();

	LuaGarbageCollect();
	InitUnitTypes(1);
	Wc2BotClear(); // the file brings back the computer players of mods it was saved with
	// NG55: a save file that fails to parse must not end the process (LuaLoadFile defaults to
	// exitOnError). Report the failure and let the caller return to the menu instead.
	if (LuaLoadFile(filename, "", /*exitOnError=*/false) != 0) {
		ErrorPrint("Can't load the saved game '%s' -- the file is not valid.\n",
		           filename.u8string().c_str());
		SaveGameLoading = false;
		LastLoadFailed = true;
		return;
	}
	// A computer player of a mod that is missing or has changed: the game is not started (the
	// reason stays readable through Wc2BotRefusal for the caller).
	if (!Wc2BotRefusal().empty()) {
		ErrorPrint("Can't load the saved game '%s' -- %s\n", filename.u8string().c_str(),
		           Wc2BotRefusal().c_str());
		SaveGameLoading = false;
		LastLoadFailed = true;
		return;
	}
	LastLoadFailed = false;
	LuaGarbageCollect();

	PlaceLoadedUnits();

	const unsigned long game_cycle = GameCycle;
	const unsigned syncrand = SyncRandSeed;
	const unsigned synchash = SyncHash;

	InitModules();
	LoadModules();

	GameCycle = game_cycle;
	SyncRandSeed = syncrand;
	SyncHash = synchash;
	SelectionChanged();
}

//@}
