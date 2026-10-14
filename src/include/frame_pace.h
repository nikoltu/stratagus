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
/**@name frame_pace.h - The rate of presented frames. */
//
//      (c) Copyright 2026 by The Stratagus Team
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

#ifndef __FRAME_PACE_H__
#define __FRAME_PACE_H__

//@{

namespace framepace
{

/**
**  Frames presented per second.
**
**  The display's own rate, but no more than the cap; and never less than the game's cycle rate
**  (a display slower than the game runs without vertical sync). The simulation is not on this
**  clock: its cycles come from elapsed time.
**
**  @param refresh          Refresh rate of the display.
**  @param cyclesPerSecond  Game cycles per second.
**  @param cap              Highest rate presented.
*/
constexpr int targetFps(int refresh, int cyclesPerSecond, int cap = 60)
{
	return refresh < cyclesPerSecond ? cyclesPerSecond : (refresh < cap ? refresh : cap);
}

} // namespace framepace

//@}

#endif // !__FRAME_PACE_H__
