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
/**@name uianchor.h - Anchor + uniform-scale placement of UI elements. */
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

#ifndef __UIANCHOR_H__
#define __UIANCHOR_H__

//@{

#include <SDL.h>

/// Horizontal anchor edge: which part of the element is pinned to the matching
/// part of the container, so the corresponding margin is preserved on rescale.
enum class AnchorX { Left, Center, Right };

/// Vertical anchor edge; see AnchorX.
enum class AnchorY { Top, Center, Bottom };

/**
**  Place a UI element authored in a reference resolution into an arbitrary
**  container, scaled uniformly and anchored to an edge/corner/centre.
**
**  The element is scaled by scaleNum/scaleDen (its size becomes
**  round(w*scale) x round(h*scale)). Its position keeps the authored margin from
**  the anchored side, itself scaled: a Right-anchored element keeps its authored
**  right margin (scaled) measured from the container's right edge; a Center
**  anchored element keeps the offset of its centre from the design centre
**  (scaled) measured from the container's centre; Left keeps the left margin.
**  This lets a fixed 640x480-style layout follow the screen edges instead of
**  stretching uniformly from the origin.
**
**  With scale 1:1 and a container whose origin and size equal the reference,
**  the result equals the input element exactly (identity), for every anchor.
**
**  @param element   Element rectangle in reference/design coordinates.
**  @param refW      Reference resolution width the layout was authored at (> 0).
**  @param refH      Reference resolution height (> 0).
**  @param container Actual container (e.g. screen) rectangle to place into.
**  @param ax        Horizontal anchor.
**  @param ay        Vertical anchor.
**  @param scaleNum  Scale numerator (>= 0).
**  @param scaleDen  Scale denominator (> 0; treated as 1 if <= 0).
**  @return The on-screen rectangle for the element.
*/
SDL_Rect AnchorScaleRect(const SDL_Rect &element,
						 int refW, int refH,
						 const SDL_Rect &container,
						 AnchorX ax, AnchorY ay,
						 int scaleNum, int scaleDen);

//@}

#endif // !__UIANCHOR_H__
