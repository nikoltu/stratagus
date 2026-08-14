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
/**@name uianchor.cpp - Anchor + uniform-scale placement of UI elements. */
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

//@{

#include "uianchor.h"

#include <cstdint>

namespace
{

// Integer round-half-away-from-zero division for a positive divisor.
int divRound(std::int64_t num, std::int64_t den)
{
	if (den <= 0) {
		den = 1;
	}
	if (num >= 0) {
		return static_cast<int>((num + den / 2) / den);
	}
	return static_cast<int>(-((-num + den / 2) / den));
}

// round(v * scaleNum / scaleDen), all in 64-bit to avoid overflow; the float
// path is avoided entirely so there is no float->int cast to go out of range.
int scaleRound(int v, int scaleNum, int scaleDen)
{
	return divRound(static_cast<std::int64_t>(v) * scaleNum, scaleDen);
}

} // namespace

SDL_Rect AnchorScaleRect(const SDL_Rect &element,
						 int refW, int refH,
						 const SDL_Rect &container,
						 AnchorX ax, AnchorY ay,
						 int scaleNum, int scaleDen)
{
	if (scaleNum < 0) {
		scaleNum = 0;
	}
	if (scaleDen <= 0) {
		scaleDen = 1;
	}
	if (refW <= 0) {
		refW = 1;
	}
	if (refH <= 0) {
		refH = 1;
	}

	SDL_Rect out;
	out.w = scaleRound(element.w, scaleNum, scaleDen);
	out.h = scaleRound(element.h, scaleNum, scaleDen);

	// Horizontal placement.
	switch (ax) {
		case AnchorX::Left:
			out.x = container.x + scaleRound(element.x, scaleNum, scaleDen);
			break;
		case AnchorX::Right: {
			const int rightMargin = refW - element.x - element.w;
			out.x = container.x + container.w
			        - scaleRound(rightMargin, scaleNum, scaleDen) - out.w;
			break;
		}
		case AnchorX::Center: {
			// Doubled coordinates keep the centre exact at 1:1 for any parity.
			const int designOff2 = (2 * element.x + element.w) - refW;
			const std::int64_t center2 =
				static_cast<std::int64_t>(2) * container.x + container.w
				+ scaleRound(designOff2, scaleNum, scaleDen);
			out.x = divRound(center2 - out.w, 2);
			break;
		}
	}

	// Vertical placement (mirror of horizontal).
	switch (ay) {
		case AnchorY::Top:
			out.y = container.y + scaleRound(element.y, scaleNum, scaleDen);
			break;
		case AnchorY::Bottom: {
			const int bottomMargin = refH - element.y - element.h;
			out.y = container.y + container.h
			        - scaleRound(bottomMargin, scaleNum, scaleDen) - out.h;
			break;
		}
		case AnchorY::Center: {
			const int designOff2 = (2 * element.y + element.h) - refH;
			const std::int64_t center2 =
				static_cast<std::int64_t>(2) * container.y + container.h
				+ scaleRound(designOff2, scaleNum, scaleDen);
			out.y = divRound(center2 - out.h, 2);
			break;
		}
	}

	return out;
}

//@}
