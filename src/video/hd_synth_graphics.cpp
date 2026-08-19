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
/**@name hd_synth_graphics.cpp - Engine-synthesised HD command-icon graphics (IC1). */
//
//      (c) Copyright 2026 by the WargusHD project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.

//@{

// IC1 (owner-approved option B): build the command-icon strip AND the command-cell frame strip
// in-engine directly from the native Remastered folder, so nothing derived has to be shipped and
// everything is reachable "just by selecting the Remastered folder". This ports the offline bake
// (.wargushd/tools/build_hd_icons.py) into the engine and, unlike the baked static strip, keeps the
// frame SEPARATE so it can be drawn per-state (hover/active/highlight) at runtime.
//
// Ground truth (VERIFIED):
//   ICON  = Data/Art/hd/HUD/Portrait-face.{png,json}, frame "forest_<N>" (N == classic slot 0..195).
//   FRAME = Data/skins/Modern_HUD_Widgets.{png,json}, sprite "portrait_button[_hovered|_active|
//           _highlight|_highlight_hovered|_highlight_active]" (80x80). Oracle skin modern_portrait_button:
//           9-slice [25,25,25,25], scale 5. Cell 150x124, icon inset 18 (-> 114x88), per 2026-07 bake.

#include "stratagus.h"

#include "hd_synth_graphics.h"

#include "iolib.h"
#include "json.h"
#include "video.h"

#include <SDL.h>
#include <SDL_image.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace
{

namespace fs = std::filesystem;

/// A decoded TexturePacker atlas: one RGBA32 page surface + name->rect frame table.
struct Atlas {
	SDL_Surface *surface = nullptr;
	std::map<std::string, SDL_Rect> frames;
	~Atlas()
	{
		if (surface) {
			SDL_FreeSurface(surface);
		}
	}
};

/// Allocate a transparent RGBA32 surface.
SDL_Surface *NewRGBA(int w, int h)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
	if (s) {
		SDL_FillRect(s, nullptr, 0);
	}
	return s;
}

/// Copy (alpha-preserving, non-blended) a source sub-rect into a dest sub-rect, scaling as needed.
/// Patches never overlap, so a straight copy carries the source alpha into the transparent dest.
void CopyScaled(SDL_Surface *src, SDL_Rect srcR, SDL_Surface *dst, SDL_Rect dstR)
{
	if (srcR.w <= 0 || srcR.h <= 0 || dstR.w <= 0 || dstR.h <= 0) {
		return;
	}
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_BlitScaled(src, &srcR, dst, &dstR);
}

/// Read the alpha of one pixel of an RGBA32 surface (surface must be locked by the caller).
Uint8 PixelAlpha(SDL_Surface *s, int x, int y)
{
	const Uint32 *row = reinterpret_cast<const Uint32 *>(static_cast<Uint8 *>(s->pixels) + y * s->pitch);
	Uint8 r, g, b, a;
	SDL_GetRGBA(row[x], s->format, &r, &g, &b, &a);
	return a;
}

/// Load a Remastered TexturePacker atlas (mirrors CAtlasGraphic::LoadTexturePacker, but keeps the
/// page as a CPU RGBA32 surface for compositing rather than a GPU texture).
bool LoadAtlas(const std::string &jsonPath, Atlas &out)
{
	const fs::path resolved = LibraryFileName(jsonPath);
	if (resolved.empty()) {
		return false;
	}
	// Use CFile seek-to-end for sizing (fs::file_size fails on Android scoped storage paths).
	std::string text;
	{
		auto fp = std::make_unique<CFile>();
		if (fp->open(resolved.string().c_str(), CL_OPEN_READ) == -1) {
			return false;
		}
		fp->seek(0, SEEK_END);
		long sz = fp->tell();
		if (sz <= 0 || sz > 64 * 1024 * 1024) {
			fp->close();
			return false;
		}
		fp->seek(0, SEEK_SET);
		text.resize((size_t) sz);
		const bool ok = (size_t) fp->read(&text[0], (size_t) sz) == (size_t) sz;
		fp->close();
		if (!ok) {
			return false;
		}
	}
	content::JsonValue root;
	std::string jerr;
	if (!content::parseJson(text, root, jerr)) {
		return false;
	}
	const content::JsonValue *framesNode = root.find("frames");
	if (!framesNode || !framesNode->isObject() || !framesNode->object) {
		return false;
	}
	// meta.image is the packer's absolute dev path; the real page is the sibling <name>.png.
	fs::path pagePath = resolved;
	pagePath.replace_extension(".png");
	{
		auto pf = std::make_unique<CFile>();
		if (pf->open(pagePath.string().c_str(), CL_OPEN_READ) == -1) {
			return false;
		}
		SDL_Surface *surface = IMG_Load_RW(CFile::to_SDL_RWops(std::move(pf)), 1);
		if (!surface) {
			return false;
		}
		if (!surface->format || surface->format->BytesPerPixel != 4
			|| surface->format->format != SDL_PIXELFORMAT_RGBA32) {
			SDL_Surface *conv = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
			SDL_FreeSurface(surface);
			if (!conv) {
				return false;
			}
			surface = conv;
		}
		out.surface = surface;
	}

	auto reqInt = [](const content::JsonValue *o, const char *k, int &v) -> bool {
		const content::JsonValue *n = o ? o->find(k) : nullptr;
		if (!n) {
			return false;
		}
		v = (int) n->asInt(0);
		return true;
	};
	for (const auto &kv : *framesNode->object) {
		const content::JsonValue *frameRect = kv.second.find("frame");
		const content::JsonValue *rot = kv.second.find("rotated");
		if (rot && rot->asBool(false)) {
			continue; // rotated frames unsupported
		}
		SDL_Rect r{};
		if (reqInt(frameRect, "x", r.x) && reqInt(frameRect, "y", r.y)
			&& reqInt(frameRect, "w", r.w) && reqInt(frameRect, "h", r.h)) {
			out.frames[kv.first] = r;
		}
	}
	return out.surface != nullptr && !out.frames.empty();
}

/// Crop an atlas frame into its own tight RGBA32 surface.
SDL_Surface *CropFrame(const Atlas &atlas, const std::string &name)
{
	auto it = atlas.frames.find(name);
	if (it == atlas.frames.end()) {
		return nullptr;
	}
	const SDL_Rect &r = it->second;
	SDL_Surface *dst = NewRGBA(r.w, r.h);
	if (!dst) {
		return nullptr;
	}
	SDL_Rect src = r;
	SDL_Rect d = {0, 0, r.w, r.h};
	SDL_SetSurfaceBlendMode(atlas.surface, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(atlas.surface, &src, dst, &d);
	return dst;
}

/// Opaque bounding box of a surface (alpha > 12); returns the full rect if fully transparent.
SDL_Rect OpaqueBBox(SDL_Surface *s)
{
	SDL_LockSurface(s);
	int minx = s->w, miny = s->h, maxx = -1, maxy = -1;
	for (int y = 0; y < s->h; ++y) {
		for (int x = 0; x < s->w; ++x) {
			if (PixelAlpha(s, x, y) > 12) {
				if (x < minx) minx = x;
				if (y < miny) miny = y;
				if (x > maxx) maxx = x;
				if (y > maxy) maxy = y;
			}
		}
	}
	SDL_UnlockSurface(s);
	if (maxx < 0) {
		return SDL_Rect{0, 0, s->w, s->h};
	}
	return SDL_Rect{minx, miny, maxx - minx + 1, maxy - miny + 1};
}

/// Source corner size of a raised ring: walk in from the left along the vertical centre until the
/// ring turns transparent (its hollow middle). Mirrors build_hd_icons.py load_frame().
int MeasureRim(SDL_Surface *ring)
{
	SDL_LockSurface(ring);
	const int cy = ring->h / 2;
	int rim = 0;
	while (rim < ring->w && PixelAlpha(ring, rim, cy) > 40) {
		++rim;
	}
	SDL_UnlockSurface(ring);
	if (rim < 4) {
		rim = 4;
	}
	if (rim > ring->w / 2) {
		rim = ring->w / 2;
	}
	if (rim > ring->h / 2) {
		rim = ring->h / 2;
	}
	return rim;
}

/// 9-slice `ring` (corner = `rim`) into a `outW x outH` cell of `dst` at (ox,oy), output corner `bo`.
void NineSlice(SDL_Surface *ring, int rim, SDL_Surface *dst, int ox, int oy, int outW, int outH, int bo)
{
	const int sw = ring->w, sh = ring->h;
	auto blit = [&](int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1) {
		CopyScaled(ring, SDL_Rect{sx0, sy0, sx1 - sx0, sy1 - sy0}, dst,
		           SDL_Rect{ox + dx0, oy + dy0, dx1 - dx0, dy1 - dy0});
	};
	// corners (1:1-ish; scaled rim->bo)
	blit(0, 0, rim, rim, 0, 0, bo, bo);
	blit(sw - rim, 0, sw, rim, outW - bo, 0, outW, bo);
	blit(0, sh - rim, rim, sh, 0, outH - bo, bo, outH);
	blit(sw - rim, sh - rim, sw, sh, outW - bo, outH - bo, outW, outH);
	// edges
	blit(rim, 0, sw - rim, rim, bo, 0, outW - bo, bo);                   // top
	blit(rim, sh - rim, sw - rim, sh, bo, outH - bo, outW - bo, outH);   // bottom
	blit(0, rim, rim, sh - rim, 0, bo, bo, outH - bo);                   // left
	blit(sw - rim, rim, sw, sh - rim, outW - bo, bo, outW, outH - bo);   // right
	// centre
	blit(rim, rim, sw - rim, sh - rim, bo, bo, outW - bo, outH - bo);
}

} // namespace

SDL_Surface *BuildHDIconStrip()
{
	Atlas atlas;
	if (!LoadAtlas("Data/Art/hd/HUD/Portrait-face.json", atlas)) {
		return nullptr;
	}
	SDL_Surface *strip = NewRGBA(hdicon::CELL_W, hdicon::CELL_H * hdicon::SLOTS);
	if (!strip) {
		return nullptr;
	}
	const int innerW = hdicon::CELL_W - 2 * hdicon::ICON_INSET;
	const int innerH = hdicon::CELL_H - 2 * hdicon::ICON_INSET;
	int placed = 0;
	for (int n = 0; n < hdicon::SLOTS; ++n) {
		const std::string key = "forest_" + std::to_string(n);
		auto it = atlas.frames.find(key);
		if (it == atlas.frames.end()) {
			continue; // missing slot -> leave transparent (matches bake's missing handling)
		}
		CopyScaled(atlas.surface, it->second, strip,
		           SDL_Rect{hdicon::ICON_INSET, n * hdicon::CELL_H + hdicon::ICON_INSET, innerW, innerH});
		++placed;
	}
	if (!placed) {
		SDL_FreeSurface(strip);
		return nullptr;
	}
	return strip;
}

SDL_Surface *BuildHDCommandFrameStrip()
{
	Atlas atlas;
	if (!LoadAtlas("Data/skins/Modern_HUD_Widgets.json", atlas)) {
		return nullptr;
	}
	// State order must match hd_synth_graphics.h / DrawUnitIcon's index math.
	static const char *const kStates[hdicon::FRAME_STATES] = {
	    "portrait_button",
	    "portrait_button_hovered",
	    "portrait_button_active",
	    "portrait_button_highlight",
	    "portrait_button_highlight_hovered",
	    "portrait_button_highlight_active",
	};
	SDL_Surface *strip = NewRGBA(hdicon::CELL_W, hdicon::CELL_H * hdicon::FRAME_STATES);
	if (!strip) {
		return nullptr;
	}
	int built = 0;
	for (int i = 0; i < hdicon::FRAME_STATES; ++i) {
		std::string name = kStates[i];
		if (atlas.frames.find(name) == atlas.frames.end()) {
			name = kStates[0]; // fall back to the default frame if a state is absent
		}
		SDL_Surface *raw = CropFrame(atlas, name);
		if (!raw) {
			continue;
		}
		// Trim the transparent outer margin, then measure the raised rim, then 9-slice to the cell.
		const SDL_Rect bb = OpaqueBBox(raw);
		SDL_Surface *ring = NewRGBA(bb.w, bb.h);
		if (ring) {
			SDL_Rect src = bb, d = {0, 0, bb.w, bb.h};
			SDL_SetSurfaceBlendMode(raw, SDL_BLENDMODE_NONE);
			SDL_BlitSurface(raw, &src, ring, &d);
			const int rim = MeasureRim(ring);
			NineSlice(ring, rim, strip, 0, i * hdicon::CELL_H, hdicon::CELL_W, hdicon::CELL_H,
			          hdicon::ICON_INSET);
			SDL_FreeSurface(ring);
			++built;
		}
		SDL_FreeSurface(raw);
	}
	if (!built) {
		SDL_FreeSurface(strip);
		return nullptr;
	}
	return strip;
}

SDL_Surface *BuildHDSidebarPanel(const char *race, const char *frameName)
{
	const std::string atlasPath = std::string("Data/Art/hd/HUD/BG_") + race + ".json";
	Atlas atlas;
	if (!LoadAtlas(atlasPath, atlas)) {
		return nullptr;
	}
	return CropFrame(atlas, frameName);
}

SDL_Surface *BuildHDHudF5Strip(int cellW, int cellH)
{
	Atlas atlas;
	if (!LoadAtlas("Data/skins/Modern_HUD_Widgets.json", atlas)) {
		return nullptr;
	}
	static const char *const kStates[3] = {"f5", "f5_hovered", "f5_active"};
	SDL_Surface *strip = NewRGBA(cellW, cellH * 3);
	if (!strip) {
		return nullptr;
	}
	int built = 0;
	for (int i = 0; i < 3; ++i) {
		auto it = atlas.frames.find(kStates[i]);
		if (it == atlas.frames.end()) {
			continue;
		}
		CopyScaled(atlas.surface, it->second, strip, SDL_Rect{0, i * cellH, cellW, cellH});
		++built;
	}
	if (!built) {
		SDL_FreeSurface(strip);
		return nullptr;
	}
	return strip;
}

SDL_Surface *BuildHDHudSmallButton(const char *race, bool pressed, int cellW, int cellH)
{
	Atlas atlas;
	if (!LoadAtlas("Data/skins/Modern_HUD_Widgets.json", atlas)) {
		return nullptr;
	}
	std::string key = std::string("hud_") + race + "_small";
	if (pressed) {
		key += "_active";
	}
	// frame names use lowercase race
	for (auto &c : key) {
		if (c >= 'A' && c <= 'Z') {
			c = c - 'A' + 'a';
		}
	}
	auto it = atlas.frames.find(key);
	if (it == atlas.frames.end()) {
		return nullptr;
	}
	SDL_Surface *out = NewRGBA(cellW, cellH);
	if (!out) {
		return nullptr;
	}
	CopyScaled(atlas.surface, it->second, out, SDL_Rect{0, 0, cellW, cellH});
	return out;
}

//@}
