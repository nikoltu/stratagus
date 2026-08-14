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
/**@name nineslice.h - Nine-slice (scalable bordered) rectangle geometry. */
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

#ifndef __NINESLICE_H__
#define __NINESLICE_H__

//@{

#include <SDL.h>

/// One of the nine source->destination rectangle pairs of a sliced image.
struct NineSlicePatch {
	SDL_Rect src;
	SDL_Rect dst;
};

/**
**  Slice a bordered source image over an arbitrary destination rectangle.
**
**  Produces nine source/destination rectangle pairs, in reading order:
**  top-left, top, top-right, left, centre, right, bottom-left, bottom,
**  bottom-right. The four corners keep their native (source) size whenever the
**  destination is large enough; the four edges stretch along their long axis
**  and the centre stretches both ways. When the destination is smaller than the
**  combined corners, the corner columns/rows are shrunk proportionally so the
**  nine destination patches always tile the destination exactly -- no gaps, no
**  overlaps, nothing drawn outside it.
**
**  @param srcW    Source image width in pixels (>= 0).
**  @param srcH    Source image height in pixels (>= 0).
**  @param left    Left border inset in source pixels.
**  @param top     Top border inset in source pixels.
**  @param right   Right border inset in source pixels.
**  @param bottom  Bottom border inset in source pixels.
**  @param dest    Destination rectangle to fill.
**  @param patches Output array of nine patches (reading order, see above).
**                 Zero-area patches are emitted for degenerate slices and may
**                 be skipped by the caller.
*/
void ComputeNineSlice(int srcW, int srcH,
					  int left, int top, int right, int bottom,
					  const SDL_Rect &dest,
					  NineSlicePatch patches[9]);

//@}

#endif // !__NINESLICE_H__
