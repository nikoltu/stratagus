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
/**@name mods.h - The enabled mods and where their files are found. */
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

#ifndef MODS_H
#define MODS_H

#include "filesystem.h"

#include <string>
#include <string_view>
#include <vector>

namespace content
{
class JsonValue;
}

// A mod is a directory <mods root>/<id>/. Its files are reachable under the virtual prefix
// "mods/<id>/" and under no other name, and only while the mod is enabled for this start. With
// no mod enabled nothing here is consulted: the file lookup behaves as it always did.

/// True while at least one mod is enabled
extern bool ModsActive();
/// True for a file name under the virtual prefix "mods/"
extern bool ModsIsPath(std::string_view file);
/// The directory the mods are installed in (beside the data directory)
extern fs::path ModsRoot();
/// Replace the mods directory; an empty path gives the default back
extern void ModsSetRoot(const fs::path &root);
/// Replace the set of enabled mods; an id that is not a mod directory is left out
extern void ModsSetEnabled(const std::vector<std::string> &ids);
/// The ids of the enabled mods, in the order given
extern const std::vector<std::string> &ModsEnabled();
/// The file a "mods/<id>/..." name stands for; an empty path if it is not inside an enabled mod
extern fs::path ModsLocate(std::string_view file);
/// Size in bytes of a mod's file, -1 if there is none
extern long ModsFileSize(std::string_view file);
/// Parse a mod's JSON file; false if it is absent, too large or malformed
extern bool ModsReadJson(std::string_view file, content::JsonValue &out);

/// Register the script functions
extern void ModsCclRegister();

#endif // MODS_H
