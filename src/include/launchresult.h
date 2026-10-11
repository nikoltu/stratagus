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

#include <string>

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

#endif
