//     _________ __                 __
//    /   _____//  |_____________ _/  |______     ____  __ __  ______
//    \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//    /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ \
//   /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//           \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/// @file hd_atlas.h - Shared data model for HD sprite atlases (.hdatlas).
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

// This header is shared by the host importer tool and the engine consumer. It
// must stay free of engine/SDL/Qt dependencies: only <cstdint> and the C++
// standard library are permitted so it compiles in any context.

#pragma once

#include <cstdint>

namespace hdatlas
{

/// One sprite frame within a split page. Fixed layout, little-endian on target.
struct HdFrameRecord {
	uint32_t nameHash;       // FNV-1a (32-bit) of the TexturePacker key, e.g. "arrow_0"
	uint16_t page;           // which split PNG page (0-based)
	uint16_t sx, sy, sw, sh; // src rect on that page (page-local, post-split)
	int16_t  offx, offy;     // spriteSourceSize.x / .y (trim offset into the untrimmed canvas)
	uint16_t cw, ch;         // sourceSize.w / .h (untrimmed canvas dimensions)
	uint8_t  rotated;        // TexturePacker 'rotated'; MUST be 0 for P0 (assert on import)
	uint8_t  _pad;
};

// Binary file .hdatlas layout (all little-endian):
//   char     magic[4]      = {'H','D','A','1'}
//   uint32_t cap           (the GPU page-size cap used at import)
//   uint32_t pageCount
//   uint32_t frameCount
//   for each page: uint16_t nameLen; char name[nameLen]  (PNG filename, no path, e.g. "foo.p0.png")
//   for each frame: HdFrameRecord (packed, in the order below)
//
// Record ordering is deterministic: frames are written sorted by their name
// using std::string operator< so the output is byte-reproducible regardless of
// the JSON object's map/iteration order.

/// Four-byte magic identifying an .hdatlas stream.
inline constexpr char kMagic[4] = { 'H', 'D', 'A', '1' };

/// 32-bit FNV-1a hash of a NUL-terminated string. Offset basis 2166136261,
/// prime 16777619. Shared by tool, engine and tests so keys hash identically.
inline uint32_t fnv1a(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; ++s) {
		h ^= static_cast<uint8_t>(*s);
		h *= 16777619u;
	}
	return h;
}

/// Classic view: expand a 256-entry palette of 6-bit R,G,B triplets (the layout of the game's
/// .ppl files) into R,G,B,A bytes. Each value is multiplied by 4; alpha is 255. With keyIndex0
/// index 0 becomes fully transparent (sprites, masks); without it index 0 is a normal colour (tiles).
inline void expandPalette6(const uint8_t *ppl, bool keyIndex0, uint8_t out[256][4])
{
	for (int i = 0; i < 256; ++i) {
		for (int c = 0; c < 3; ++c) {
			out[i][c] = static_cast<uint8_t>((ppl[i * 3 + c] & 0x3F) * 4);
		}
		out[i][3] = 255;
	}
	if (keyIndex0) {
		out[0][0] = out[0][1] = out[0][2] = out[0][3] = 0;
	}
}

/// Classic view: the one-pixel checkerboard of explored-but-unseen ground. True for the pixels of
/// a fog cell that stay black: those whose column + row (counted inside the cell) is even. The
/// classic fog masks end in the same checkerboard, so a dithered mask and its neighbours join.
inline bool classicFogDither(int x, int y)
{
	return ((x + y) & 1) == 0;
}

} // namespace hdatlas
