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
/**@name nineslice.cpp - Nine-slice (scalable bordered) rectangle geometry. */
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

#include "nineslice.h"

namespace
{

// Clamp a value to the range [lo, hi].
int clampInt(int v, int lo, int hi)
{
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

// Split a total span into a leading and trailing inset that never overlap.
// Normally the insets keep their native size; when they would exceed the span
// they are shrunk proportionally so lead + trail == span exactly (integer-exact:
// trail is computed as the remainder so no rounding gap can open up).
void splitInsets(int span, int lead, int trail, int &outLead, int &outTrail)
{
	lead = lead < 0 ? 0 : lead;
	trail = trail < 0 ? 0 : trail;
	if (lead + trail <= span) {
		outLead = lead;
		outTrail = trail;
		return;
	}
	const int combined = lead + trail;
	if (combined == 0) {
		outLead = 0;
		outTrail = 0;
		return;
	}
	// Proportional shrink; the trailing part takes the exact remainder.
	outLead = (span * lead) / combined;
	outTrail = span - outLead;
}

} // namespace

void ComputeNineSlice(int srcW, int srcH,
					  int left, int top, int right, int bottom,
					  const SDL_Rect &dest,
					  NineSlicePatch patches[9])
{
	srcW = srcW < 0 ? 0 : srcW;
	srcH = srcH < 0 ? 0 : srcH;
	const int dw = dest.w < 0 ? 0 : dest.w;
	const int dh = dest.h < 0 ? 0 : dest.h;

	// Source insets can never overlap inside the source image either.
	int sLeft = clampInt(left, 0, srcW);
	int sRight = clampInt(right, 0, srcW);
	if (sLeft + sRight > srcW) {
		splitInsets(srcW, sLeft, sRight, sLeft, sRight);
	}
	int sTop = clampInt(top, 0, srcH);
	int sBottom = clampInt(bottom, 0, srcH);
	if (sTop + sBottom > srcH) {
		splitInsets(srcH, sTop, sBottom, sTop, sBottom);
	}

	// Destination inset columns/rows: native size when there is room, otherwise
	// shrunk to fit so the patches tile the destination exactly.
	int dLeft;
	int dRight;
	splitInsets(dw, sLeft, sRight, dLeft, dRight);
	int dTop;
	int dBottom;
	splitInsets(dh, sTop, sBottom, dTop, dBottom);

	// Column edges (x) and row edges (y), source and destination.
	const int sx[4] = { 0, sLeft, srcW - sRight, srcW };
	const int sy[4] = { 0, sTop, srcH - sBottom, srcH };
	const int dx[4] = { dest.x, dest.x + dLeft, dest.x + dw - dRight, dest.x + dw };
	const int dy[4] = { dest.y, dest.y + dTop, dest.y + dh - dBottom, dest.y + dh };

	int n = 0;
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			NineSlicePatch &p = patches[n++];
			p.src.x = sx[col];
			p.src.y = sy[row];
			p.src.w = sx[col + 1] - sx[col];
			p.src.h = sy[row + 1] - sy[row];
			p.dst.x = dx[col];
			p.dst.y = dy[row];
			p.dst.w = dx[col + 1] - dx[col];
			p.dst.h = dy[row + 1] - dy[row];
		}
	}
}

void ComputeNineSliceScaled(int srcW, int srcH,
							int left, int top, int right, int bottom, double scale,
							const SDL_Rect &dest,
							NineSlicePatch patches[9])
{
	srcW = srcW < 0 ? 0 : srcW;
	srcH = srcH < 0 ? 0 : srcH;
	const int dw = dest.w < 0 ? 0 : dest.w;
	const int dh = dest.h < 0 ? 0 : dest.h;
	scale = scale > 0 ? scale : 0;

	int sLeft = clampInt(left, 0, srcW);
	int sRight = clampInt(right, 0, srcW);
	if (sLeft + sRight > srcW) {
		splitInsets(srcW, sLeft, sRight, sLeft, sRight);
	}
	int sTop = clampInt(top, 0, srcH);
	int sBottom = clampInt(bottom, 0, srcH);
	if (sTop + sBottom > srcH) {
		splitInsets(srcH, sTop, sBottom, sTop, sBottom);
	}

	// The border at the drawing's scale, rounded; shrunk to fit as in ComputeNineSlice.
	const auto scaled = [scale](int v) { return static_cast<int>(v * scale + 0.5); };
	int dLeft;
	int dRight;
	splitInsets(dw, scaled(sLeft), scaled(sRight), dLeft, dRight);
	int dTop;
	int dBottom;
	splitInsets(dh, scaled(sTop), scaled(sBottom), dTop, dBottom);

	const int sx[4] = { 0, sLeft, srcW - sRight, srcW };
	const int sy[4] = { 0, sTop, srcH - sBottom, srcH };
	const int dx[4] = { dest.x, dest.x + dLeft, dest.x + dw - dRight, dest.x + dw };
	const int dy[4] = { dest.y, dest.y + dTop, dest.y + dh - dBottom, dest.y + dh };

	int n = 0;
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			NineSlicePatch &p = patches[n++];
			p.src.x = sx[col];
			p.src.y = sy[row];
			p.src.w = sx[col + 1] - sx[col];
			p.src.h = sy[row + 1] - sy[row];
			p.dst.x = dx[col];
			p.dst.y = dy[row];
			p.dst.w = dx[col + 1] - dx[col];
			p.dst.h = dy[row + 1] - dy[row];
		}
	}
}

//@}
