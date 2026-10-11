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
//           9-slice [25,25,25,25], scale 5, padding [4,4]. The sprites are kept whole; the nine-slice
//           is done at draw time at the HUD's scale (ui/icons.cpp DrawHDCommandFrame).
//   MASK  = Data/Art/hd/HUD/Portrait-mask.{png,json}, frame "forest_<N>_team": grey, 0 = not team.

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

} // namespace

static bool IconStripHasTeamKey = false;

bool HDIconStripHasTeamKey()
{
	return IconStripHasTeamKey;
}

SDL_Surface *BuildHDIconStrip(const char *set)
{
	// `set` is the frame-name prefix of the tileset's portrait set: forest / ice / swamp / xswamp.
	const std::string setName = (set && *set) ? set : "forest";
	IconStripHasTeamKey = false;
	Atlas atlas;
	if (!LoadAtlas("Data/Art/hd/HUD/Portrait-face.json", atlas)) {
		return nullptr;
	}
	SDL_Surface *strip = NewRGBA(hdicon::CELL_W, hdicon::CELL_H * hdicon::SLOTS);
	if (!strip) {
		return nullptr;
	}
	// M666-9: the game's own team mask. A frame of it is grey where the picture takes the player's
	// colour (0 elsewhere); the original's portrait is colour * grey / 255 there (fit on captures
	// hi10 and e01 of work/agent-orig-scenes, work/agent-fix-hudrest/tools/m9_teamcolour.py:
	// residual 2..3 levels).
	Atlas mask;
	const bool haveMask = LoadAtlas("Data/Art/hd/HUD/Portrait-mask.json", mask);
	SDL_Surface *cell = haveMask ? NewRGBA(hdicon::CELL_W, hdicon::CELL_H) : nullptr;
	int placed = 0;
	for (int n = 0; n < hdicon::SLOTS; ++n) {
		std::string key = setName + "_" + std::to_string(n);
		auto it = atlas.frames.find(key);
		if (it == atlas.frames.end()) {
			key = "forest_" + std::to_string(n);       // a slot the set lacks: the forest picture
			it = atlas.frames.find(key);
		}
		if (it == atlas.frames.end()) {
			continue; // missing slot -> leave transparent (matches bake's missing handling)
		}
		// M666-2: the picture fills its cell. It was seated 18 px in (114 x 88) and the whole cell,
		// margin included, was then stretched over the button, which made the picture 0.83..0.88 of
		// the original's size. The frame is a separate graphic and is drawn around the picture.
		const SDL_Rect dst{0, n * hdicon::CELL_H, hdicon::CELL_W, hdicon::CELL_H};
		CopyScaled(atlas.surface, it->second, strip, dst);
		++placed;
		if (!cell) {
			continue;
		}
		// The key: (v, 0, v) with v = the mask's grey. A picture pixel that happens to have that
		// form outside the team region gets g = 1, so the key is exact. Mask and picture frames
		// have one size and are scaled by one routine to one size, so they sample the same pixels.
		const auto mt = mask.frames.find(key + "_team");
		const bool teamed = mt != mask.frames.end() && mt->second.w == it->second.w
		                    && mt->second.h == it->second.h;
		if (teamed) {
			CopyScaled(mask.surface, mt->second, cell, SDL_Rect{0, 0, hdicon::CELL_W, hdicon::CELL_H});
		}
		SDL_LockSurface(strip);
		SDL_LockSurface(cell);
		for (int y = 0; y < hdicon::CELL_H; ++y) {
			Uint8 *p = static_cast<Uint8 *>(strip->pixels) + (dst.y + y) * strip->pitch;
			const Uint8 *m = static_cast<const Uint8 *>(cell->pixels) + y * cell->pitch;
			for (int x = 0; x < hdicon::CELL_W; ++x, p += 4, m += 4) {
				// RGBA32: the bytes are R, G, B, A
				if (teamed && m[0] > 0) {
					p[0] = m[0];
					p[1] = 0;
					p[2] = m[0];
				} else if (p[1] == 0 && p[0] == p[2] && p[0] > 0) {
					p[1] = 1;
				}
			}
		}
		SDL_UnlockSurface(cell);
		SDL_UnlockSurface(strip);
	}
	if (cell) {
		SDL_FreeSurface(cell);
	}
	if (!placed) {
		SDL_FreeSurface(strip);
		return nullptr;
	}
	IconStripHasTeamKey = haveMask;
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
	// M666-2: the sprites go in whole, at their own size. They were trimmed to their opaque box
	// first, which is a different box per state -- 70 x 74 for the plain frame (its drop shadow
	// counts), 80 x 80 for the hovered and pressed ones (their glow fills the rim) -- and then
	// nine-sliced to an 18-px ring in a 150 x 124 cell: a ring twice the original's 9 px at k = 3.
	SDL_Surface *strip = NewRGBA(hdicon::FRAME_PX, hdicon::FRAME_PX * hdicon::FRAME_STATES);
	if (!strip) {
		return nullptr;
	}
	int built = 0;
	for (int i = 0; i < hdicon::FRAME_STATES; ++i) {
		auto it = atlas.frames.find(kStates[i]);
		if (it == atlas.frames.end()) {
			it = atlas.frames.find(kStates[0]); // fall back to the default frame if a state is absent
		}
		if (it == atlas.frames.end()) {
			continue;
		}
		CopyScaled(atlas.surface, it->second, strip,
		           SDL_Rect{0, i * hdicon::FRAME_PX, hdicon::FRAME_PX, hdicon::FRAME_PX});
		++built;
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

/**
**  The F5 menu button's three states. Each is the atlas' empty button frame (f5, f5_hovered,
**  f5_active; 200x100) with the race's inset -- the "F5" mark and the race's figure
**  (f5_inset_<race>; 180x80) -- laid over its middle. The frame alone is what was shown before:
**  an empty framed slot at the head of the side bar.
*/
SDL_Surface *BuildHDHudF5Strip(const char *race, int cellW, int cellH)
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
		const auto inset = atlas.frames.find(std::string("f5_inset_") + race);
		if (inset != atlas.frames.end() && it->second.w > 0 && it->second.h > 0) {
			// centred in the frame, at the frame's own scale, blended over it
			SDL_Rect src = inset->second;
			const int w = src.w * cellW / it->second.w;
			const int h = src.h * cellH / it->second.h;
			SDL_Rect dst{(cellW - w) / 2, i * cellH + (cellH - h) / 2, w, h};
			SDL_SetSurfaceBlendMode(atlas.surface, SDL_BLENDMODE_BLEND);
			SDL_BlitScaled(atlas.surface, &src, strip, &dst);
			SDL_SetSurfaceBlendMode(atlas.surface, SDL_BLENDMODE_NONE);
		}
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

SDL_Surface *BuildHDHudCompleteBar(int w, int h)
{
	Atlas atlas;
	if (!LoadAtlas("Data/skins/Modern_HUD_Widgets.json", atlas)) {
		return nullptr;
	}
	auto it = atlas.frames.find("complete_bar");
	if (it == atlas.frames.end() || w <= 0 || h <= 0) {
		return nullptr;
	}
	SDL_Surface *out = NewRGBA(w, h);
	if (!out) {
		return nullptr;
	}
	CopyScaled(atlas.surface, it->second, out, SDL_Rect{0, 0, w, h});
	return out;
}

//@}
