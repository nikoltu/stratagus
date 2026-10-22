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
/**@name launchresult.h - Result record of a game started by a launch request. */
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

#ifndef __LAUNCHRESULT_H__
#define __LAUNCHRESULT_H__

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
**  A game started through a launch request reports its outcome in one file,
**  <user directory>/<game>/launch/result.json, which the program that made the
**  request reads after the engine has gone.
**
**  Scripts cannot write files themselves, so the start script builds the text
**  and hands it over here. The path is fixed; the caller chooses nothing but
**  the content. The file is replaced as a whole (temporary file, then rename),
**  so a reader never sees half a record.
*/
extern bool WriteLaunchResult(const std::string &text);

/**
**  While a network game waits for players who still load, their names are left for the
**  host in <the same directory>/loading.txt, one per line; the host is then told through
**  its message channel. Written the same way as the result.
*/
extern bool WriteLaunchLoading(const std::string &text);

/// Length of the game that ended last, in game cycles. The cycle counter itself
/// is cleared when a game ends, before a script regains control.
extern unsigned long LastGameCycle;

/**
**  The launcher's map editor asks the engine for two things it cannot work out itself
**  (scripts/launch.lua, modes "export" and "validate"):
**
**  export.json    the tables the engine plays by: every tile of each tileset with its
**                 flags and terrains, every unit type with its footprint, domain and
**                 the map flags it is kept off;
**  validate.json  a map as the engine loaded it: the edges where two tiles do not meet
**                 on the same terrain, and the units the loader had to put elsewhere.
**
**  Both are written like the result (whole file, fixed name, same directory). The
**  scripts compose the outer record; the parts are made here.
*/
extern bool WriteLaunchExport(const std::string &text);
extern bool WriteLaunchValidate(const std::string &text);

/// The checksum of the loaded game scripts, eight hex digits: the export's content stamp.
extern std::string EditorContentStamp();
/// The tileset loaded last (LoadTileModels) as a JSON object.
extern std::string EditorTilesetJson();
/// Every unit type as a JSON array.
extern std::string EditorUnitTypesJson();

/// The loader's notes on units are taken from here until EditorCheckLoadedMap reads them.
extern void EditorCheckBegin();
/// The map now loaded, checked: a JSON object.
extern std::string EditorCheckLoadedMap();

/// The HD art preload at a new game's load (unit atlases, terrain and water; CreateGame).
/// On unless a launch mode that never draws (export / validate) turns it off for its run;
/// a loaded save is not gated.
extern void SetHDArtPreload(bool on);
extern bool HDArtPreloadOn();

/// A unit the loader could not place where the map asks (CreateUnit), while a check is on.
extern void EditorNoteUnitMoved(const std::string &ident, int player, int askedX, int askedY,
                                int atX, int atY, bool lost);

/// An edge between two tiles whose corners name different terrains: between (x, y) and
/// (x + 1, y), or with `down` between (x, y) and (x, y + 1).
struct EditorSeam
{
	int x = 0;
	int y = 0;
	bool down = false;
};

/// A unit that is not where the map put it.
struct EditorMovedUnit
{
	std::string ident;
	int player = 0;
	int askedX = 0;
	int askedY = 0;
	int atX = 0;
	int atY = 0;
	bool lost = false;
};

/// The edges whose corners differ. `quads` holds one quad a tile (CTileset::getQuadFromTile,
/// corners ab / cd as bytes 3 2 / 1 0), row by row; a tile whose `mixes` entry is false (a
/// wall, a decorative or non-mixing field) is left out, as the engine editor leaves it out.
extern std::vector<EditorSeam> FindTileSeams(int width, int height,
                                             const std::vector<uint32_t> &quads,
                                             const std::vector<bool> &mixes);

/// The check's JSON object; each list holds at most `limit` entries, the counts all.
extern std::string EditorCheckJson(const std::string &tileset, int width, int height,
                                   const std::vector<EditorSeam> &seams,
                                   const std::vector<EditorMovedUnit> &moved, size_t limit);

/// Text as a JSON string, quotes included.
extern std::string EditorJsonString(const std::string &text);

#endif
