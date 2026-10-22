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
/**@name target_cursor.h - Which cursor the pointer shows over the map. */
//
//      (c) Copyright 2026 by The Stratagus Project
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

#ifndef __TARGET_CURSOR_H__
#define __TARGET_CURSOR_H__

//@{

/// The pointer over the map, as measured on the original (run 70, 49 cursor reads, Remastered
/// and classic alike):
/// - with no order waiting for a target, the pointer stays the plain one over every unit, gold
///   mine and tree - the original has no hover cursor (no magnifier);
/// - while an order waits for its target (A = attack), the crosshair is coloured by what is under
///   it: yellow over ground and trees, green over an own unit, red over an enemy unit.
/// Neutral units take the ground colour: live run 79 (CALIB_RUN79) read the same yellow cursor
/// (one handle, one bit hash) over two critters (sheep), the gold mine and the Circle of Power
/// as over bare ground, and green over an own Archer. Allied units take the own colour: not
/// read (no allied unit in the scene; UNMEASURED, the engine's old grouping kept).
namespace targetcursor
{

enum class Pick { Unchanged, Yellow, Green, Red };

/// selecting: an order waits for its target. unit: a visible, non-decoration unit is under the
/// pointer. ownOrAllied / neutral: that unit's side, as seen by this player.
inline Pick pick(bool selecting, bool unit, bool ownOrAllied, bool neutral)
{
	if (!selecting) {
		return Pick::Unchanged;
	}
	if (!unit || neutral) {
		return Pick::Yellow;
	}
	return ownOrAllied ? Pick::Green : Pick::Red;
}

} // namespace targetcursor

//@}

#endif // !__TARGET_CURSOR_H__
