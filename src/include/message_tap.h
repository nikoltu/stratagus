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
/**@name message_tap.h - The tap on the message line. */
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

#ifndef __MESSAGE_TAP_H__
#define __MESSAGE_TAP_H__

//@{

/// The message line on a touch screen (owner decision 71): the original's Space centres the view
/// on the last events in turn; on a tablet a tap on the message line centres the view on the event
/// of the message shown there. The tap takes the line only where its text is drawn, widened by
/// half the text's height on every side so a finger finds it, and only when that message has a
/// position; any other tap is the map's.
namespace messagetap
{

/// True when the screen point (tx, ty) falls on the drawn text [x, x + w) x [y, y + h), widened
/// by h / 2 on every side, and the message has a position. An empty text is never hit.
constexpr bool hits(int tx, int ty, int x, int y, int w, int h, bool hasPos)
{
	if (!hasPos || w <= 0 || h <= 0) {
		return false;
	}
	const int pad = h / 2;
	return tx >= x - pad && tx < x + w + pad && ty >= y - pad && ty < y + h + pad;
}

} // namespace messagetap

//@}

#endif // !__MESSAGE_TAP_H__
