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
/**@name touch_drag.h - The pointer events of a moving single finger. */
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

#ifndef __TOUCH_DRAG_H__
#define __TOUCH_DRAG_H__

//@{

/// IG-014: a single finger that moves while down. Once it has drifted past the drag threshold
/// from the point where it landed, the touch is a drag: the left press is given AT THE LANDING
/// POINT (so a selection box begins under the finger, as a mouse press would), and only then
/// does the pointer follow to the finger's present point. Before that, the pointer follows the
/// finger, except for the second touch of a pair, whose pointer stays on the first tap's point
/// until the finger drifts.
namespace touchdrag
{

enum class Kind { Motion, LeftDown };

struct Event
{
	Kind kind;
	int x;
	int y;
};

struct Step
{
	int count = 0;          /// number of events in `events`, in the order they are sent
	Event events[3] = {};
	bool startsDrag = false; /// this motion turned the touch into a drag
};

inline int absDiff(int a, int b) { return a > b ? a - b : b - a; }

/// The pointer events for one finger motion to (px, py).
/// (downX, downY): where the finger landed; dragging: the left button is already held for a
/// drag; pair: the touch began as the second of a pair; dragPx: the drag threshold.
inline Step onMotion(int downX, int downY, int px, int py, bool dragging, bool pair, int dragPx)
{
	Step s;
	const bool drift = absDiff(px, downX) > dragPx || absDiff(py, downY) > dragPx;
	if (pair && !dragging && !drift) {
		return s;   // still a possible second tap: the pointer stays where it is
	}
	if (!dragging && drift) {
		s.startsDrag = true;
		s.events[s.count++] = Event{Kind::Motion, downX, downY};
		s.events[s.count++] = Event{Kind::LeftDown, downX, downY};
	}
	s.events[s.count++] = Event{Kind::Motion, px, py};
	return s;
}

} // namespace touchdrag

//@}

#endif // !__TOUCH_DRAG_H__
