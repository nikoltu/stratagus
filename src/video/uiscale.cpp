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
/**@name uiscale.cpp - Aspect-preserving uniform fit of a reference UI. */
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

#include "uiscale.h"

#include <cstdint>

namespace
{

// round(a / b) for a >= 0, b > 0 (half up).
int divRoundPos(std::int64_t a, std::int64_t b)
{
	return static_cast<int>((a + b / 2) / b);
}

std::int64_t gcd64(std::int64_t a, std::int64_t b)
{
	if (a < 0) {
		a = -a;
	}
	if (b < 0) {
		b = -b;
	}
	while (b != 0) {
		std::int64_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}

} // namespace

UniformScale ComputeUniformScale(int refW, int refH, const SDL_Rect &target)
{
	if (refW <= 0) {
		refW = 1;
	}
	if (refH <= 0) {
		refH = 1;
	}
	const int tw = target.w < 0 ? 0 : target.w;
	const int th = target.h < 0 ? 0 : target.h;

	// The binding axis is the one giving the smaller ratio: compare tw/refW with
	// th/refH via cross-multiplication (64-bit, no division, no rounding).
	int num;
	int den;
	if (static_cast<std::int64_t>(tw) * refH <= static_cast<std::int64_t>(th) * refW) {
		num = tw;   // width binds: tw/refW is the smaller (or equal) ratio
		den = refW;
	} else {
		num = th;   // height binds
		den = refH;
	}

	// Reduce to lowest terms; keep den > 0.
	std::int64_t g = gcd64(num, den);
	if (g == 0) {
		g = 1;
	}
	num = static_cast<int>(num / g);
	den = static_cast<int>(den / g);
	if (den == 0) {
		den = 1;
	}

	UniformScale result;
	result.num = num;
	result.den = den;

	int fitW = divRoundPos(static_cast<std::int64_t>(refW) * num, den);
	int fitH = divRoundPos(static_cast<std::int64_t>(refH) * num, den);
	// Rounding must never push the fit past the target on either axis.
	if (fitW > tw) {
		fitW = tw;
	}
	if (fitH > th) {
		fitH = th;
	}

	result.fit.w = fitW;
	result.fit.h = fitH;
	result.fit.x = target.x + (tw - fitW) / 2;
	result.fit.y = target.y + (th - fitH) / 2;
	return result;
}

//@}
