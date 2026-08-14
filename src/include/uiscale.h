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
/**@name uiscale.h - Aspect-preserving uniform fit of a reference UI. */
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

#ifndef __UISCALE_H__
#define __UISCALE_H__

//@{

#include <SDL.h>

/// The uniform fit of a reference resolution into a target rectangle.
struct UniformScale {
	int num;      ///< Scale numerator; the applied factor is num/den.
	int den;      ///< Scale denominator (> 0).
	SDL_Rect fit; ///< Centred, aspect-preserved rectangle inside the target.
};

/**
**  Fit a reference resolution into a target rectangle, preserving aspect ratio.
**
**  Picks the largest uniform factor that keeps a refW x refH area inside the
**  target (letterbox/pillarbox), returns that factor as an exact integer
**  rational num/den == min(target.w/refW, target.h/refH), and the centred fit
**  rectangle it produces (refW*num/den x refH*num/den, centred in the target).
**
**  The rational is chosen so the fit rectangle never exceeds the target on
**  either axis, so that `num/den` can be fed unchanged to
**  AnchorScaleRect(scaleNum, scaleDen) and to a nine-slice destination and stay
**  pixel-consistent with `fit`. When target == reference the result is 1/1 with
**  fit == target (identity).
**
**  @param refW    Reference width the UI was authored at (> 0; clamped).
**  @param refH    Reference height (> 0; clamped).
**  @param target  Target rectangle to fit into.
**  @return The uniform scale rational and the centred fit rectangle.
*/
UniformScale ComputeUniformScale(int refW, int refH, const SDL_Rect &target);

//@}

#endif // !__UISCALE_H__
