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
/**@name unit_cull_extent.h - The map-pixel box a unit can draw into (viewport culling). */
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

#ifndef __UNIT_CULL_EXTENT_H__
#define __UNIT_CULL_EXTENT_H__

#include <algorithm>
#include <cmath>

// CULL-HD-EXTENT (SCALE_ZOOM diff 07): CUnit::IsVisibleInViewport culled by the Lua `Image size`
// cell only, but the HD frame's canvas (216 native px a tile, centred on the footprint centre +
// Offset) overhangs it by up to ~58 map px, and the selection rectangle and the health bar (15 + 3
// classic px above the rectangle, or 12 + 3 below) sit outside it too. A unit then popped in up to
// 27 screen px late at a view edge at k = 3. Extent() is the union of everything the unit draws,
// in map pixels relative to the unit's footprint top-left (tilePos * tile + IX / IY). Pure: no
// engine types, host-testable.
namespace unitcull
{

constexpr int HdNativeTilePx = 216; // as video.h; repeated so the header stands alone

struct Input {
	int tileW = 1, tileH = 1;       // footprint, tiles
	int tilePxX = 32, tilePxY = 32; // PixelTileSize
	int width = 0, height = 0;      // Lua Image size (classic cell / logical box)
	int offX = 0, offY = 0;         // type Offset
	int spriteW = 0, spriteH = 0;   // classic Sprite size, 0 without one (as DrawUnitSelection)
	int boxW = 0, boxH = 0;         // selection rectangle
	int boxOffX = 0, boxOffY = 0;
	int canvasW = 0, canvasH = 0;   // HD frame canvas, native px (after a rule-canvas stretch); 0 = none
};

struct Box {
	int x0, y0, x1, y1; // [x0, x1] x [y0, y1], map px
};

/// Slack for the screen-side rounding (each term is lround'ed at draw) and the selection circle's
/// +2 screen px radius: 2 map px all round.
constexpr int Slack = 2;

inline Box Extent(const Input &in)
{
	const int footW = in.tileW * in.tilePxX;
	const int footH = in.tileH * in.tilePxY;
	// 1. classic cell, as the cull always had it
	Box b;
	b.x0 = -(in.width - footW) / 2 + in.offX;
	b.y0 = -(in.height - footH) / 2 + in.offY;
	b.x1 = b.x0 + in.width;
	b.y1 = b.y0 + in.height;
	// 2. HD canvas: centred on footprint centre + Offset at tile / 216 map px a native px
	if (in.canvasW > 0 && in.canvasH > 0) {
		const double cx = footW / 2.0 + in.offX;
		const double cy = footH / 2.0 + in.offY;
		const double hw = in.canvasW * (double) in.tilePxX / HdNativeTilePx / 2.0;
		const double hh = in.canvasH * (double) in.tilePxY / HdNativeTilePx / 2.0;
		b.x0 = std::min(b.x0, (int) std::floor(cx - hw));
		b.y0 = std::min(b.y0, (int) std::floor(cy - hh));
		b.x1 = std::max(b.x1, (int) std::ceil(cx + hw));
		b.y1 = std::max(b.y1, (int) std::ceil(cy + hh));
	}
	// 3. selection rectangle (DrawUnitSelection) and the health bar on it (DrawHealthBarOnBox),
	// both health-bar places, so the box does not depend on the preference
	if (in.boxW > 0 && in.boxH > 0) {
		const int bx0 = footW / 2 - in.boxW / 2 - (in.width - in.spriteW) / 2 + in.boxOffX;
		const int by0 = footH / 2 - in.boxH / 2 - (in.height - in.spriteH) / 2 + in.boxOffY;
		const int classicPx = (int) std::ceil(in.tilePxY / 32.0);
		b.x0 = std::min(b.x0, bx0);
		b.x1 = std::max(b.x1, bx0 + in.boxW);
		b.y0 = std::min(b.y0, by0 - 15 * classicPx);
		b.y1 = std::max(b.y1, by0 + in.boxH + 15 * classicPx);
	}
	b.x0 -= Slack;
	b.y0 -= Slack;
	b.x1 += Slack;
	b.y1 += Slack;
	return b;
}

/// The unit at map pixel `pos` (footprint top-left) with extent `e` touches the view
/// [vx0, vx1] x [vy0, vy1] (map px). Same comparison as the old cull, on the wider box.
inline bool Touches(const Box &e, int posX, int posY, int vx0, int vy0, int vx1, int vy1)
{
	return !(posX + e.x1 < vx0 || posX + e.x0 > vx1 || posY + e.y1 < vy0 || posY + e.y0 > vy1);
}

} // namespace unitcull

#endif // !__UNIT_CULL_EXTENT_H__
