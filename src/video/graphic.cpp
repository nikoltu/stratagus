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
/**@name graphic.cpp - The general graphic functions. */
//
//      (c) Copyright 1999-2011 by Lutz Sammer, Nehal Mistry, Jimmy Salmon and
//                                 Pali Rohár
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

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/
#include "video.h"
#include "json.h"

#include "hd_synth_graphics.h"
#include "intern_video.h"
#include "iolib.h"
#include "map.h"
#include "network.h"
#include "nineslice.h"
#include "player.h"
#include "stratagus.h"
#include "debug_bridge.h"
#include <cstring>
#include "ui.h"

#include <SDL_image.h>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

static int HashCount;
static std::map<fs::path, std::shared_ptr<CGraphic>> GraphicHash;

// See declarations in video.h. Both default to the "classic" values so that, with the
// crisp-zoom feature off, every render path below behaves exactly as before.
bool EnableCrispZoom = false;
float CrispZoomDrawScale = 1.0f;

// GPU-pipeline (Phase 1). See video.h. Off by default => every draw path stays on the CPU
// compositor until the on-screen world render explicitly turns it on for tiles/units.
bool GpuWorldDrawActive = false;

// GPU-pipeline (HUD sprites). See video.h. Off by default; set only around the in-game HUD draw.
bool GpuHudDrawActive = false;
bool OverlayDirty = false;
bool OverlayDirtyPrevFrame = true;

// GPU-pipeline (Phase 1). See video.h. 1.0 => the classic native (Zoom==1) draw; set to the real
// viewport zoom around the in-game world GPU render so tile/unit destination rects scale up.
float GpuWorldDrawZoom = 1.0f;

// GPU-pipeline (Phase 5). See video.h. 1.0 => world renders straight to the backbuffer (no change).
// Overwritten once at video init from <internal>/world_scale.txt on Android; clamped to [0.25, 1.0].
float WorldRenderScale = 1.0f;

// Classic view. See video.h. Presentation only: never part of a saved game or a network message.
static bool ClassicView = false;

void SetClassicView(bool on)
{
	if (ClassicView == on) {
		return;
	}
	ClassicView = on;
	ErrorPrint("ClassicView: %s\n", on ? "on" : "off");
	if (on && Map.HDTileAtlasTried && !Map.ClassicTileAtlas) {
		ErrorPrint("ClassicView: no classic terrain for this map, the terrain stays Remastered\n");
	}
	// The picture's layout follows the view: the 4:3 box, or the Remastered rects back.
	ApplyClassicViewLayout();
}

bool IsClassicView()
{
	return ClassicView;
}

// Upload an RGBA (paletteless) surface to a nearest-filtered, alpha-blended GPU texture. Returns
// nullptr for paletted surfaces (kept on the CPU path for colour-cycling / palette remap) or on
// failure. Nearest scaling matches the pixel-art look and the Zoom==1 world (Phase 1 only draws
// through the GPU at native scale).
static SDL_Texture *UploadSurfaceToTexture(SDL_Surface *surface)
{
	if (!surface || !TheRenderer) {
		return nullptr;
	}
	if (surface->format->palette != nullptr) {
		// 8-bit paletted: keep on CPU so palette colour-cycling and player-colour remap work.
		return nullptr;
	}
	SDL_Texture *tex = SDL_CreateTextureFromSurface(TheRenderer, surface);
	if (!tex) {
		return nullptr;
	}
	SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	return tex;
}

// Upload a grey-on-black player-colour MASK as a texture whose black background is made
// transparent. The HD <file>_team.png masks store the team region as a grey silhouette on an
// OPAQUE black rectangle; RenderCopy'd straight over the base sprite that black paints a box
// around the unit. The CPU GetColorVariant() path avoids this by compositing only where the
// mask's red channel is > 0; mirror that here by deriving per-pixel alpha from the mask
// luminance so pure black becomes fully transparent, with a short ramp to keep silhouette
// edges anti-aliased. Bright team pixels keep their (opaque) alpha so the tint stays solid.
static SDL_Texture *UploadMaskToTexture(SDL_Surface *surface)
{
	if (!surface || !TheRenderer) {
		return nullptr;
	}
	if (surface->format->palette != nullptr) {
		// Paletted masks stay on the CPU colour-variant path.
		return nullptr;
	}
	SDL_Surface *conv = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
	if (!conv) {
		return nullptr;
	}
	SDL_LockSurface(conv);
	const int w = conv->w;
	const int h = conv->h;
	const int stride = conv->pitch / 4;
	Uint32 *pix = (Uint32 *)conv->pixels;
	for (int yy = 0; yy < h; ++yy) {
		for (int xx = 0; xx < w; ++xx) {
			Uint32 &p = pix[xx + yy * stride];
			Uint8 r, g, b, a;
			SDL_GetRGBA(p, conv->format, &r, &g, &b, &a);
			if (a == 0) {
				continue;
			}
			int lum = r;
			if (g > lum) lum = g;
			if (b > lum) lum = b;
			if (lum < 32) {
				p = SDL_MapRGBA(conv->format, r, g, b, (Uint8)(a * lum / 32));
			}
		}
	}
	SDL_UnlockSurface(conv);
	SDL_Texture *tex = SDL_CreateTextureFromSurface(TheRenderer, conv);
	SDL_FreeSurface(conv);
	if (!tex) {
		return nullptr;
	}
	SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	return tex;
}

// Shared crisp-zoom helper: blit srcRect from src, scaled up by CrispZoomDrawScale, to
// destination top-left (x,y). Clipping is delegated to dst->clip_rect (the crisp render path
// constrains it to the painted viewport region). Only ever reached while CrispZoomDrawScale
// != 1.0f, i.e. exclusively on the crisp-zoom path.
static void CrispBlitScaled(SDL_Surface *src, SDL_Rect srcRect, int x, int y, SDL_Surface *dst)
{
	SDL_Rect drect;
	drect.x = x;
	drect.y = y;
	drect.w = static_cast<int>(std::lround(srcRect.w * CrispZoomDrawScale));
	drect.h = static_cast<int>(std::lround(srcRect.h * CrispZoomDrawScale));
	MarkOverlayDirty(dst);
	SDL_BlitScaled(src, &srcRect, dst, &drect);
}

static void WarnInvalidGraphicFrame(const CGraphic &graphic, unsigned frame, bool flipped)
{
	static std::set<std::tuple<fs::path, unsigned, bool>> warnedFrames;
	if (!warnedFrames.insert({graphic.File, frame, flipped}).second) {
		return;
	}
	const size_t available = flipped ? graphic.frameFlip_map.size() : graphic.frame_map.size();
	ErrorPrint("Warning: graphic '%s' requested %sframe %u but only %zu frames are available; "
	           "skipping draw\n",
	           graphic.File.string().c_str(),
	           flipped ? "flipped " : "",
	           frame,
	           available);
}

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

// GPU-pipeline (HUD): RenderCopy a source sub-rect of tex to dst on the backbuffer at 1:1 screen
// pixels, mirroring the global CPU clip rect onto the renderer so edge clipping matches the overlay
// path. alpha < 255 applies a texture alpha mod for the trans variants (reset after). No zoom scale.
static void HudRenderCopy(SDL_Texture *tex, const SDL_Rect &src, const SDL_Rect &dst,
						  int alpha = 255)
{
	SDL_Rect clip = {ClipX1, ClipY1, ClipX2 - ClipX1 + 1, ClipY2 - ClipY1 + 1};
	SDL_RenderSetClipRect(TheRenderer, &clip);
	if (alpha != 255) {
		SDL_SetTextureAlphaMod(tex, (Uint8)alpha);
		SDL_RenderCopy(TheRenderer, tex, &src, &dst);
		SDL_SetTextureAlphaMod(tex, 255);
	} else {
		SDL_RenderCopy(TheRenderer, tex, &src, &dst);
	}
	SDL_RenderSetClipRect(TheRenderer, nullptr);
}

/**
**  Video draw the graphic clipped.
**
**  @param x   X position on the target surface
**  @param y   Y position on the target surface
**  @param surface target surface
*/
void CGraphic::DrawClip(int x, int y,
						SDL_Surface *surface /*= TheScreen*/) const
{
	int oldx = x;
	int oldy = y;
	int w = Width;
	int h = Height;
	CLIP_RECTANGLE(x, y, w, h);
	DrawSub(x - oldx, y - oldy, w, h, x, y, surface);
}

/**
**  Video draw part of graphic.
**
**  @param gx  X offset into object
**  @param gy  Y offset into object
**  @param w   width to display
**  @param h   height to display
**  @param x   X position on the target surface
**  @param y   Y position on the target surface
**	@param surface target surface
*/
void CGraphic::DrawSub(int gx, int gy, int w, int h, int x, int y,
					   SDL_Surface *surface /*= TheScreen*/) const
{
	Assert(surface);

	// GPU-pipeline (HUD): route RGBA sub-blits to the backbuffer, same as DrawSubClip.
	if (GpuHudDrawActive && mTexture && surface == TheScreen) {
		SDL_Rect src = {gx, gy, w, h};
		SDL_Rect dst = {x, y, w, h};
		HudRenderCopy(mTexture, src, dst);
		return;
	}

	SDL_Rect srect = {Sint16(gx), Sint16(gy), Uint16(w), Uint16(h)};
	// Crisp-zoom: scale the sprite up to on-screen size. (x,y) already carries the
	// on-screen ·Zoom position from the Zoom-aware viewport transform, so only the size is
	// scaled here. Any alpha mod set by the DrawSubTrans wrappers is honoured by
	// SDL_BlitScaled. Guard is a no-op (scale == 1) unless the crisp path is active.
	if (CrispZoomDrawScale != 1.0f) {
		CrispBlitScaled(mSurface, srect, x, y, surface);
		return;
	}

	SDL_Rect drect = {Sint16(x), Sint16(y), 0, 0};

	MarkOverlayDirty(surface);
	SDL_BlitSurface(mSurface, &srect, surface, &drect);
}

/**
**  Video draw part of graphic by custom pixel modifier.
**  32bpp only (yet?)
**
**  @param gx  X offset into object
**  @param gy  Y offset into object
**  @param w   width to display
**  @param h   height to display
**  @param x   X position on the target surface
**  @param y   Y position on the target surface
**  @param modifier method used to draw pixels instead of SDL_BlitSurface
**  @param param    parameter for modifier
**  @param surface  target surface
*/
void CGraphic::DrawSubCustomMod(int gx, int gy, int w, int h, int x, int y,
							    pixelModifier modifier, const uint32_t param,
								SDL_Surface *surface /*= TheScreen*/) const
{
	Assert(surface);
	Assert(surface->format->BitsPerPixel == 32);

	MarkOverlayDirty(surface);

	size_t srcOffset = mSurface->w * gy + gx;
	size_t dstOffset = surface->w * y + x;

	uint32_t *const src = reinterpret_cast<uint32_t *>(mSurface->pixels);
	uint32_t *const dst = reinterpret_cast<uint32_t *>(surface->pixels);

	for (uint16_t posY = 0; posY < h; posY++) {
		for (uint16_t posX = 0; posX < w; posX++) {
			const uint32_t srcColor = src[srcOffset + posX];
			const uint32_t dstColor = dst[dstOffset + posX];
			const uint32_t resColor =  modifier(srcColor, dstColor, param);

			dst[dstOffset + posX] = resColor;
		}
		dstOffset += surface->w;
		srcOffset += mSurface->w;
	}
}


/**
**  Video draw part of graphic clipped.
**
**  @param gx  X offset into object
**  @param gy  Y offset into object
**  @param w   width to display
**  @param h   height to display
**  @param x   X position on the target surface
**  @param y   Y position on the target surface
**  @param surface target surface
*/
void CGraphic::DrawSubClip(int gx, int gy, int w, int h, int x, int y,
						   SDL_Surface *surface /*= TheScreen*/) const
{
	Assert(surface);

	// GPU-pipeline (HUD): route RGBA sub-blits to the backbuffer. Renderer clip handles the edge
	// clipping the CLIP_RECTANGLE below would do on the CPU. Paletted/off-screen fall through.
	if (GpuHudDrawActive && mTexture && surface == TheScreen) {
		SDL_Rect src = {gx, gy, w, h};
		SDL_Rect dst = {x, y, w, h};
		HudRenderCopy(mTexture, src, dst);
		return;
	}

	// Crisp-zoom: bypass the native CLIP_RECTANGLE (clipping is delegated to the target
	// surface clip_rect) and blit the frame scaled up to on-screen size at the already
	// ·Zoom-positioned (x,y). No-op unless the crisp path is active.
	if (CrispZoomDrawScale != 1.0f) {
		SDL_Rect srect = {Sint16(gx), Sint16(gy), Uint16(w), Uint16(h)};
		CrispBlitScaled(mSurface, srect, x, y, surface);
		return;
	}

	int oldx = x;
	int oldy = y;
	CLIP_RECTANGLE(x, y, w, h);
	gx += x - oldx;
	gy += y - oldy;

	SDL_Rect srect = {Sint16(gx), Sint16(gy), Uint16(w), Uint16(h)};
	SDL_Rect drect = {Sint16(x), Sint16(y), 0, 0};
	MarkOverlayDirty(surface);
	SDL_BlitSurface(mSurface, &srect, surface, &drect);
}

/**
**  Video draw part of graphic with alpha.
**
**  @param gx     X offset into object
**  @param gy     Y offset into object
**  @param w      width to display
**  @param h      height to display
**  @param x      X position on the target surface
**  @param y      Y position on the target surface
**  @param alpha  Alpha
**  @param surface target surface
*/
void CGraphic::DrawSubTrans(int gx, int gy, int w, int h, int x, int y,
							unsigned char alpha,
							SDL_Surface *surface /*= TheScreen*/) const
{
	Assert(surface);

	Uint8 oldalpha = 0xff;
	SDL_GetSurfaceAlphaMod(mSurface, &oldalpha);
	SDL_SetSurfaceAlphaMod(mSurface, alpha);
	DrawSub(gx, gy, w, h, x, y, surface);
	SDL_SetSurfaceAlphaMod(mSurface, oldalpha);
}

/**
**  Video draw part of graphic with alpha and clipped.
**
**  @param gx     X offset into object
**  @param gy     Y offset into object
**  @param w      width to display
**  @param h      height to display
**  @param x      X position on the target surface
**  @param y      Y position on the target surface
**  @param alpha  Alpha
**  @param surface target surface
*/
void CGraphic::DrawSubClipTrans(int gx, int gy, int w, int h, int x, int y,
								unsigned char alpha,
								SDL_Surface *surface /*= TheScreen*/) const
{
	// GPU-pipeline (HUD): RGBA sub-blit with alpha straight to the backbuffer.
	if (GpuHudDrawActive && mTexture && surface == TheScreen) {
		SDL_Rect src = {gx, gy, w, h};
		SDL_Rect dst = {x, y, w, h};
		HudRenderCopy(mTexture, src, dst, alpha);
		return;
	}
	int oldx = x;
	int oldy = y;
	CLIP_RECTANGLE(x, y, w, h);
	DrawSubTrans(gx + x - oldx, gy + y - oldy, w, h, x, y, alpha, surface);
}


/**
**  Video draw clipped part of graphic by custom pixel modifier.
**  32bpp only (yet?)
**
**  @param gx  X offset into object
**  @param gy  Y offset into object
**  @param w   width to display
**  @param h   height to display
**  @param x   X position on the target surface
**  @param y   Y position on the target surface
**  @param modifier method used to draw pixels instead of SDL_BlitSurface
**  @param param    parameter for modifier
**  @param surface  target surface
*/
void CGraphic::DrawSubClipCustomMod(int gx, int gy, int w, int h, int x, int y,
								    pixelModifier modifier,
									const uint32_t param,
								    SDL_Surface *surface /*= TheScreen*/) const
{
	int oldx = x;
	int oldy = y;
	CLIP_RECTANGLE(x, y, w, h);
	DrawSubCustomMod(gx + x - oldx, gy + y - oldy, w, h, x, y, modifier, param, surface);
}

/**
**  Draw graphic object unclipped.
**
**  @param frame   number of frame (object index)
**  @param x       x coordinate on the target surface
**  @param y       y coordinate on the target surface
**  @param surface target surface
*/
void CGraphic::DrawFrame(unsigned frame, int x, int y,
						 SDL_Surface *surface /*= TheScreen*/) const
{
	if (frame >= frame_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, false);
		return;
	}
	DrawSub(frame_map[frame].x, frame_map[frame].y,
			Width, Height, x, y, surface);
}

/**
**  Draw graphic object clipped.
**
**  @param frame   number of frame (object index)
**  @param x       x coordinate on the target surface
**  @param y       y coordinate on the target surface
**  @param surface target surface
*/
void CGraphic::DrawFrameClip(unsigned frame, int x, int y,
							 SDL_Surface *surface /*= TheScreen*/) const
{
	if (frame >= frame_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, false);
		return;
	}
	// GPU-pipeline (Phase 1): during the on-screen world render, route tiles/shadows straight to
	// the backbuffer texture. Requires an RGBA GPU copy and the default screen target; otherwise
	// (paletted graphic, or drawing into an offscreen surface such as the zoomed MapRenderSurface)
	// fall through to the CPU compositor exactly as before.
	if (GpuWorldDrawActive && mTexture && surface == TheScreen) {
		DrawFrameClipTex(frame, x, y);
		return;
	}
	// GPU-pipeline (HUD): when the whole sheet was too big for one texture (mTexture == null, e.g.
	// icons_hd), draw via a small cached per-frame texture instead of the CPU blit. mTexture!=null
	// RGBA graphics are handled by DrawSubClip's own HUD gate below; paletted fall through to CPU.
	if (GpuHudDrawActive && !mTexture && surface == TheScreen) {
		if (SDL_Texture *ft = GetHudFrameTexture(frame)) {
			SDL_Rect src = {0, 0, Width, Height};
			SDL_Rect dst = {x, y, Width, Height};
			HudRenderCopy(ft, src, dst);
			return;
		}
	}
	DrawSubClip(frame_map[frame].x, frame_map[frame].y,
				Width, Height, x, y, surface);
}

/**
**  GPU-pipeline (Phase 1): RenderCopy a single frame to the backbuffer. The viewport clip
**  rectangle is expected to be active on the renderer (set by CViewport::Draw), so partial
**  clipping at viewport edges is handled by SDL_RenderSetClipRect. Vertical-pixel-size scaling
**  is applied by the renderer's logical size, same as the overlay copy.
*/
void CGraphic::DrawFrameClipTex(unsigned frame, int x, int y) const
{
	if (!mTexture || frame >= frame_map.size()) {
		return;
	}
	// SRC is always the native sprite frame; only the DST size scales by the world zoom so the GPU
	// upscales the HD art in place (crisp at integer zoom, no CPU offscreen). At GpuWorldDrawZoom==1
	// this is byte-identical to the native path.
	const int dw = (GpuWorldDrawZoom == 1.0f) ? Width : (int)std::lround(Width * GpuWorldDrawZoom);
	const int dh = (GpuWorldDrawZoom == 1.0f) ? Height : (int)std::lround(Height * GpuWorldDrawZoom);
	SDL_Rect src = {frame_map[frame].x, frame_map[frame].y, Width, Height};
	SDL_Rect dst = {x, y, dw, dh};
	SDL_RenderCopy(TheRenderer, mTexture, &src, &dst);
}

/**
**  GPU-pipeline (Phase 1): as DrawFrameClipTex but mirrored horizontally, drawn from the SAME
**  base texture via SDL_RenderCopyEx (no separate flip sheet needed). frame indexes frame_map
**  (the un-flipped layout); RenderCopyEx flips the pixels of that source rect.
*/
void CGraphic::DrawFrameClipTexX(unsigned frame, int x, int y) const
{
	if (!mTexture || frame >= frame_map.size()) {
		return;
	}
	const int dw = (GpuWorldDrawZoom == 1.0f) ? Width : (int)std::lround(Width * GpuWorldDrawZoom);
	const int dh = (GpuWorldDrawZoom == 1.0f) ? Height : (int)std::lround(Height * GpuWorldDrawZoom);
	SDL_Rect src = {frame_map[frame].x, frame_map[frame].y, Width, Height};
	SDL_Rect dst = {x, y, dw, dh};
	SDL_RenderCopyEx(TheRenderer, mTexture, &src, &dst, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
}

/*----------------------------------------------------------------------------
--  CAtlasGraphic (World HD P0): atlas-backed frame draw
----------------------------------------------------------------------------*/

// The importer writes HdFrameRecord as a fixed 24-byte, naturally-aligned struct with no interior
// padding, so records can be fread straight into the vector on the little-endian engine targets.
static_assert(sizeof(hdatlas::HdFrameRecord) == 24, "HdFrameRecord must stay byte-packed at 24");

CAtlasGraphic::~CAtlasGraphic()
{
	for (SDL_Texture *tex : pages) {
		if (tex) {
			SDL_DestroyTexture(tex);
		}
	}
	pages.clear();
	frames.clear();
	byHash.clear();
	classicGroups.clear(); // the cut groups own their textures
	if (classicPage) {
		SDL_FreeSurface(classicPage);
		classicPage = nullptr;
	}
}

// Load <path>.hdatlas + its page PNGs. See video/hd_atlas.h for the on-disk layout. Any failure
// (missing/short file, bad magic, page decode fail, or a rotated record) leaves the object empty
// and returns false, releasing anything already created. Little-endian target assumed (as elsewhere
// in the engine's binary I/O).
bool CAtlasGraphic::Load(const std::string &path)
{
	if (!pages.empty()) {
		return true; // already loaded; Load is single-shot
	}

	const fs::path resolved = LibraryFileName(path);
	if (resolved.empty()) {
		return false;
	}
	// Fetch the file size up front so the header counts can be bounds-checked before any
	// allocation is sized from them: a corrupt uint32 count (e.g. 0xFFFFFFFF) must not be
	// able to make frames.resize()/pageNames.reserve() throw std::length_error/bad_alloc,
	// which would propagate out of this bool-returning function and terminate the process.
	std::error_code ec;
	const uintmax_t fileSize = fs::file_size(resolved, ec);
	if (ec) {
		return false;
	}
	auto fp = std::make_unique<CFile>();
	if (fp->open(resolved.string().c_str(), CL_OPEN_READ) == -1) {
		return false;
	}
	// Read helper: returns true only on an exact-length read (guards against truncation).
	auto readExact = [&fp](void *buf, size_t len) -> bool {
		return len == 0 || (size_t) fp->read(buf, len) == len;
	};

	char magic[4];
	uint32_t pageCount = 0;
	uint32_t frameCount = 0;
	if (!readExact(magic, 4) || memcmp(magic, hdatlas::kMagic, 4) != 0
		|| !readExact(&cap, 4) || !readExact(&pageCount, 4) || !readExact(&frameCount, 4)) {
		fp->close();
		cap = 0;
		return false;
	}

	// Reject header counts that cannot possibly fit in the file before allocating anything
	// sized from them (64-bit math avoids the uint32 overflow that a naive size check could
	// hit). Each page needs at least a 2-byte nameLen; each frame is a fixed HdFrameRecord.
	if ((uint64_t) frameCount * sizeof(hdatlas::HdFrameRecord) > (uint64_t) fileSize
		|| (uint64_t) pageCount * 2 > (uint64_t) fileSize) {
		fp->close();
		cap = 0;
		return false;
	}
	// An atlas with frames must reference at least one page (see Fix 4 below); a corrupt
	// file could otherwise claim frames but no pages, which would leave Load() returning
	// true while IsLoaded() (!pages.empty()) stays false and every draw silently no-ops.
	if (frameCount > 0 && pageCount == 0) {
		fp->close();
		cap = 0;
		return false;
	}

	// Page-name table: uint16 nameLen; char name[nameLen] (PNG filename, no path).
	std::vector<std::string> pageNames;
	pageNames.reserve(pageCount);
	for (uint32_t i = 0; i < pageCount; ++i) {
		uint16_t nameLen = 0;
		if (!readExact(&nameLen, 2)) {
			fp->close();
			cap = 0;
			return false;
		}
		std::string name(nameLen, '\0');
		if (nameLen && !readExact(&name[0], nameLen)) {
			fp->close();
			cap = 0;
			return false;
		}
		pageNames.push_back(std::move(name));
	}

	// Packed frame records (deterministic, name-sorted order — see hd_atlas.h).
	frames.resize(frameCount);
	if (frameCount && !readExact(frames.data(), (size_t) frameCount * sizeof(hdatlas::HdFrameRecord))) {
		frames.clear();
		cap = 0;
		return false;
	}
	fp->close();

	// P0 forbids rotated frames (the importer asserts rotated==0); reject defensively here too.
	// Also reject any record referencing a page beyond the header's pageCount: a frame
	// pointing at a missing page must fail at load, not silently no-op at draw time.
	for (const hdatlas::HdFrameRecord &rec : frames) {
		if (rec.rotated != 0 || rec.page >= pageCount) {
			frames.clear();
			cap = 0;
			return false;
		}
	}

	// Decode each page PNG (relative to the .hdatlas directory) and upload it as an RGBA texture,
	// reusing the same SDL_image decode route as CGraphic::Load and the nearest+blend upload helper.
	const fs::path dir = resolved.parent_path();
	pages.reserve(pageCount);
	for (uint32_t i = 0; i < pageCount; ++i) {
		NetworkLoadingPing(); // a page takes seconds to decode: a loading device is not a dead one
		const fs::path pagePath = dir / pageNames[i];
		auto pf = std::make_unique<CFile>();
		if (pf->open(pagePath.string().c_str(), CL_OPEN_READ) == -1) {
			goto pageFail;
		}
		{
			SDL_Surface *surface = IMG_Load_RW(CFile::to_SDL_RWops(std::move(pf)), 1);
			if (!surface) {
				goto pageFail;
			}
			SDL_Texture *tex = UploadSurfaceToTexture(surface);
			SDL_FreeSurface(surface);
			if (!tex) {
				// Paletted (helper returns nullptr) or GPU upload failure: pages must be RGBA.
				goto pageFail;
			}
			pages.push_back(tex);
		}
		continue;
	pageFail:
		for (SDL_Texture *tex : pages) {
			if (tex) {
				SDL_DestroyTexture(tex);
			}
		}
		pages.clear();
		frames.clear();
		cap = 0;
		return false;
	}

	// Name -> frame-index map for FrameByName lookups.
	byHash.reserve(frames.size());
	for (uint32_t i = 0; i < frames.size(); ++i) {
		byHash.emplace(frames[i].nameHash, i);
	}
	return true;
}

// Classic view: cut the given frames out of an 8-bit indexed page into small RGBA pages, coloured by
// `rgba` (one R,G,B,A entry per index), and upload those instead of the whole page (a classic page is
// 38.8 MB as RGBA; its tiles are 1.5 MB). Each record is re-pointed at its new place. A frame that
// cannot be placed gets a page index no draw accepts.
// `cuts` (empty, or one entry per frame) marks the fog mask frames: CutKeyed = index 0 is nothing,
// CutDither = keep only the checkerboard pixels, CutSolid = a full cell of the masks' colour.
enum { CutKeyed = 1, CutDither = 2, CutSolid = 4 };
static bool UploadPalettedFrames(SDL_Surface *indexed, const uint8_t rgba[256][4], int maxDim,
                                 std::vector<hdatlas::HdFrameRecord> &frames,
                                 std::vector<SDL_Texture *> &pages,
                                 const std::vector<uint8_t> &cuts)
{
	const int side = std::min(maxDim, 1024);
	struct Slot { int page; int x; int y; };
	std::vector<Slot> slots(frames.size(), Slot{-1, 0, 0});
	std::vector<int> pageHeights;
	int shelfX = 0, shelfY = 0, shelfH = 0;
	for (size_t i = 0; i < frames.size(); ++i) {
		const hdatlas::HdFrameRecord &rec = frames[i];
		const int fw = rec.sw, fh = rec.sh;
		if (fw == 0 || fh == 0 || fw > side || fh > side
			|| rec.sx + fw > indexed->w || rec.sy + fh > indexed->h) {
			continue;
		}
		if (shelfX + fw > side) { // wrap to the next shelf row
			shelfX = 0;
			shelfY += shelfH;
			shelfH = 0;
		}
		if (shelfY + fh > side) { // page full -> start a fresh page
			pageHeights.push_back(shelfY + shelfH);
			shelfX = shelfY = shelfH = 0;
		}
		slots[i] = Slot{(int) pageHeights.size(), shelfX, shelfY};
		shelfX += fw;
		if (fh > shelfH) {
			shelfH = fh;
		}
	}
	if (shelfY + shelfH > 0) {
		pageHeights.push_back(shelfY + shelfH);
	}
	if (pageHeights.empty()) {
		return false;
	}

	SDL_LockSurface(indexed);
	const Uint8 *srcBase = static_cast<const Uint8 *>(indexed->pixels);
	// The colour of the solid cells is the masks' own: the first mask pixel that is not index 0.
	const uint8_t *ink = nullptr;
	for (size_t i = 0; !ink && i < cuts.size(); ++i) {
		if (cuts[i] != CutKeyed || slots[i].page < 0) {
			continue;
		}
		for (int y = 0; !ink && y < frames[i].sh; ++y) {
			const Uint8 *src = srcBase + (frames[i].sy + y) * indexed->pitch + frames[i].sx;
			for (int x = 0; x < frames[i].sw; ++x) {
				if (src[x]) {
					ink = rgba[src[x]];
					break;
				}
			}
		}
	}
	bool ok = true;
	for (size_t p = 0; ok && p < pageHeights.size(); ++p) {
		SDL_Surface *page =
		    SDL_CreateRGBSurfaceWithFormat(0, side, pageHeights[p], 32, SDL_PIXELFORMAT_RGBA32);
		if (!page) {
			ok = false;
			break;
		}
		SDL_FillRect(page, nullptr, SDL_MapRGBA(page->format, 0, 0, 0, 0));
		Uint8 *dstBase = static_cast<Uint8 *>(page->pixels);
		for (size_t i = 0; i < frames.size(); ++i) {
			if (slots[i].page != (int) p) {
				continue;
			}
			const hdatlas::HdFrameRecord &rec = frames[i];
			const int cut = i < cuts.size() ? cuts[i] : 0;
			for (int y = 0; y < rec.sh; ++y) {
				const Uint8 *src = srcBase + (rec.sy + y) * indexed->pitch + rec.sx;
				Uint8 *dst = dstBase + (slots[i].y + y) * page->pitch + slots[i].x * 4;
				if (cut) {
					// Fog mask cell: the page was cleared to nothing, write only the kept pixels.
					for (int x = 0; x < rec.sw; ++x, dst += 4) {
						if ((cut & CutDither) && !hdatlas::classicFogDither(x, y)) {
							continue;
						}
						if (cut & CutSolid) {
							if (ink) {
								memcpy(dst, ink, 4);
							}
						} else if (src[x]) {
							memcpy(dst, rgba[src[x]], 4);
						}
					}
					continue;
				}
				for (int x = 0; x < rec.sw; ++x, dst += 4) {
					memcpy(dst, rgba[src[x]], 4);
				}
			}
		}
		NetworkLoadingPing(); // every cut page
		SDL_Texture *tex = UploadSurfaceToTexture(page);
		SDL_FreeSurface(page);
		if (!tex) {
			ok = false;
			break;
		}
		pages.push_back(tex);
	}
	SDL_UnlockSurface(indexed);
	if (!ok) {
		return false;
	}
	for (size_t i = 0; i < frames.size(); ++i) {
		hdatlas::HdFrameRecord &rec = frames[i];
		if (slots[i].page < 0) {
			rec.page = 0xFFFF;
			continue;
		}
		rec.page = (uint16_t) slots[i].page;
		rec.sx = (uint16_t) slots[i].x;
		rec.sy = (uint16_t) slots[i].y;
	}
	return true;
}

// An atlas page as decoded brought to RGBA32, the form the upload takes; a team mask as white with
// its luminance for alpha. Takes `surface` over (it is freed or returned); null on a failure.
// An 8-bit team mask is written in one pass by a table of its palette (index 0 is the colour key
// of the two-pass way below, so it gives alpha 0 whatever its colour); `maskInOnePass` false keeps
// the two passes for the test that holds the two ways equal.
SDL_Surface *AtlasPageToRGBA(SDL_Surface *surface, bool teamMask, bool fogMask, bool maskInOnePass)
{
	if (!surface) {
		return nullptr;
	}
	if (maskInOnePass && teamMask && !fogMask && surface->format && surface->format->BytesPerPixel == 1
		&& surface->format->palette) {
		SDL_Surface *conv =
		    SDL_CreateRGBSurfaceWithFormat(0, surface->w, surface->h, 32, SDL_PIXELFORMAT_RGBA32);
		if (!conv) {
			SDL_FreeSurface(surface);
			return nullptr;
		}
		const SDL_Palette *pal = surface->format->palette;
		Uint32 white[256];
		for (int i = 0; i < 256; ++i) {
			const Uint8 alpha = (i == 0 || i >= pal->ncolors)
			                        ? 0 : hdatlas::maskAlpha(pal->colors[i].r, pal->colors[i].g, pal->colors[i].b);
			white[i] = SDL_MapRGBA(conv->format, 255, 255, 255, alpha);
		}
		SDL_LockSurface(surface);
		SDL_LockSurface(conv);
		for (int y = 0; y < surface->h; ++y) {
			const Uint8 *src = static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch;
			Uint32 *dst = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(conv->pixels) + y * conv->pitch);
			for (int x = 0; x < surface->w; ++x) {
				dst[x] = white[src[x]];
			}
		}
		SDL_UnlockSurface(conv);
		SDL_UnlockSurface(surface);
		SDL_FreeSurface(surface);
		return conv;
	}
	if (!surface->format || surface->format->BytesPerPixel != 4) {
		const bool paletted = surface->format && surface->format->BytesPerPixel == 1;
		if (paletted && !fogMask) {
			SDL_SetColorKey(surface, SDL_TRUE, 0);
		}
		SDL_Surface *conv =
		    SDL_CreateRGBSurfaceWithFormat(0, surface->w, surface->h, 32, SDL_PIXELFORMAT_RGBA32);
		if (!conv) {
			SDL_FreeSurface(surface);
			return nullptr;
		}
		SDL_FillRect(conv, nullptr, SDL_MapRGBA(conv->format, 0, 0, 0, 0));
		SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE); // straight copy; colour-key still skips keyed pixels
		SDL_BlitSurface(surface, nullptr, conv, nullptr);
		SDL_FreeSurface(surface);
		surface = conv;
	}
	if (teamMask) {
		// A luminance mask converts to RGBA as (lum,lum,lum,255) -> opaque. Remap so the luminance
		// becomes ALPHA and RGB is white: the _team overlay's colour-mod then tints only the bright
		// (team-colourable) pixels and leaves the rest transparent.
		SDL_LockSurface(surface);
		const SDL_PixelFormat *f = surface->format;
		Uint8 *base = static_cast<Uint8 *>(surface->pixels);
		if (f->format == SDL_PIXELFORMAT_RGBA32) {
			// STUTTER-LOAD-SPEED: the bytes stand as R, G, B, A; the same sum without two SDL
			// calls per pixel (a mask page has up to 60 million of them).
			for (int y = 0; y < surface->h; ++y) {
				Uint8 *px = base + y * surface->pitch;
				for (int x = 0; x < surface->w; ++x, px += 4) {
					const Uint8 lum = hdatlas::maskAlpha(px[0], px[1], px[2]);
					px[0] = 255;
					px[1] = 255;
					px[2] = 255;
					px[3] = lum;
				}
			}
		} else {
			for (int y = 0; y < surface->h; ++y) {
				Uint32 *row = reinterpret_cast<Uint32 *>(base + y * surface->pitch);
				for (int x = 0; x < surface->w; ++x) {
					Uint8 r, g, b, a;
					SDL_GetRGBA(row[x], f, &r, &g, &b, &a);
					const Uint8 lum = hdatlas::maskAlpha(r, g, b);
					row[x] = SDL_MapRGBA(f, 255, 255, 255, lum);
				}
			}
		}
		SDL_UnlockSurface(surface);
	} else if (fogMask) {
		// bgs_Fow.png: palette is all-black, tRNS chunk carries per-index alpha (0-254).
		// After palette→RGBA32 conversion above, pixels are already (0,0,0,alpha) —
		// exactly the fog mask we need. No luminance conversion required.
	}
	return surface;
}

// The part of an atlas load that needs no renderer: the page PNG read, decoded and brought to the
// form the upload takes (RGBA32; a team mask as white with its luminance for alpha; with
// indexedForPalette the 8-bit page as it is). Touches nothing but its own file and surface, so it
// may run on a worker thread. The surface is null on any failure.
AtlasPageDecode DecodeAtlasPage(const fs::path &pngPath, bool teamMask, bool fogMask, bool indexedForPalette)
{
	AtlasPageDecode out;
	const Uint32 ticksStart = SDL_GetTicks();
	auto pf = std::make_unique<CFile>();
	if (pf->open(pngPath.string().c_str(), CL_OPEN_READ) == -1) {
		return out;
	}
	SDL_Surface *surface = IMG_Load_RW(CFile::to_SDL_RWops(std::move(pf)), 1);
	if (!surface) {
		return out;
	}
	const Uint32 ticksDecoded = SDL_GetTicks();
	// The classic_* terrain atlases are 8-bit PALETTED PNGs and the team masks are 8-bit LUMINANCE;
	// UploadSurfaceToTexture only accepts RGBA. Convert to RGBA32 first.
	// NG6: those paletted PNGs carry NO tRNS chunk and use the classic convention that palette
	// index 0 is the transparent key (magenta 255,0,255). A plain SDL_ConvertSurfaceFormat keeps
	// index-0 pixels OPAQUE, so tree/scenery/fog frames upload a red/pink halo. Colour-key index 0
	// and colour-key-blit onto a transparent RGBA32 target so keyed pixels become alpha 0.
	if (indexedForPalette) {
		// Classic view: the page stays indexed; it is coloured by `palette` at the cut below.
		if (!surface->format || surface->format->BytesPerPixel != 1) {
			SDL_FreeSurface(surface);
			return out;
		}
	} else {
		surface = AtlasPageToRGBA(surface, teamMask, fogMask);
		if (!surface) {
			return out;
		}
	}
	out.surface = surface;
	out.decodeMs = (unsigned) (ticksDecoded - ticksStart);
	out.convertMs = (unsigned) (SDL_GetTicks() - ticksDecoded);
	return out;
}

// WargusHD: load a native Remastered TexturePacker atlas (<name>.json + its meta.image PNG page)
// directly, with NO offline .hdatlas conversion step. Builds the same frames/pages/byHash model as
// Load() so every draw method + FrameByName work unchanged. Frame keys keep their full TexturePacker
// name (e.g. "Forest/forest_0"). Single page (meta.image). Returns false on any failure.
bool CAtlasGraphic::LoadTexturePacker(const std::string &jsonPath, bool teamMask, bool fogMask,
                                      const unsigned char *palette, bool nearest,
                                      const char *framePrefix, const char *maskPrefix,
                                      const char *groupMark, const AtlasPageDecode *decoded)
{
	// A page decoded ahead is this function's to free, on every way out.
	struct GivenPage : AtlasPageDecode {
		~GivenPage() { if (surface) { SDL_FreeSurface(surface); } }
	} given;
	if (decoded) {
		static_cast<AtlasPageDecode &>(given) = *decoded;
	}
	if (!pages.empty()) {
		return true; // already loaded; single-shot
	}
	// Load timing (log only): where the time of one atlas goes.
	const fs::path resolved = LibraryFileName(jsonPath);
	if (resolved.empty()) {
		return false;
	}
	std::error_code ec;
	const uintmax_t sz = fs::file_size(resolved, ec);
	if (ec || sz == 0 || sz > (uintmax_t) 64 * 1024 * 1024) {
		return false; // absent or implausibly large
	}
	std::string text;
	{
		auto fp = std::make_unique<CFile>();
		if (fp->open(resolved.string().c_str(), CL_OPEN_READ) == -1) {
			return false;
		}
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
	const content::JsonValue *meta = root.find("meta");
	if (!framesNode || !framesNode->isObject() || !framesNode->object || !meta) {
		return false;
	}
	(void) meta; // meta.image holds the packing machine's absolute dev path (e.g. "C:/Projects/..."),
	// which is useless on-device -- derive the page PNG from the .json filename instead (sibling
	// <name>.png), which is how the Remastered atlases are actually laid out.
	// D0: the GPU upload is deferred. An atlas page can exceed GL_MAX_TEXTURE_SIZE (baseline phones cap at
	// 4096, but native unit atlases reach ~8000px) -> a single-texture upload would fail and drop the
	// whole atlas to the classic path. Keep the CPU surface and, after frames are parsed, either upload
	// it whole (fits) or re-pack it frame-by-frame into <=cap sub-textures (see below).
	AtlasPageDecode page;
	if (given.surface) {
		page = given; // decoded ahead (HD preload); its times are the worker's
		given.surface = nullptr;
	} else {
		fs::path pagePath = resolved;
		pagePath.replace_extension(".png");
		page = DecodeAtlasPage(pagePath, teamMask, fogMask, palette != nullptr);
		if (!page.surface) {
			return false;
		}
	}
	SDL_Surface *pageSurface = page.surface; // kept alive past frame parsing for possible re-pack
	const Uint32 ticksConverted = SDL_GetTicks();
	const int pageW = pageSurface->w;
	const int pageH = pageSurface->h;

	auto reqInt = [](const content::JsonValue *o, const char *k, int &out) -> bool {
		const content::JsonValue *v = o ? o->find(k) : nullptr;
		if (!v) {
			return false;
		}
		out = (int) v->asInt(0);
		return true;
	};
	const size_t framePrefixLen = framePrefix ? strlen(framePrefix) : 0;
	// Classic view: fog mask frames are kept beside the prefixed ones; `cuts` says how each is cut.
	const size_t maskPrefixLen = (palette && maskPrefix) ? strlen(maskPrefix) : 0;
	std::vector<uint8_t> cuts;
	int firstMask = -1;
	for (const auto &kv : *framesNode->object) {
		const std::string &name = kv.first;
		const bool isMask = maskPrefixLen && name.compare(0, maskPrefixLen, maskPrefix) == 0;
		std::string groupName;
		int groupFrame = -1;
		const bool isGroup = palette && groupMark && !isMask
		                     && hdatlas::classicSplitGroupFrame(name, groupMark, groupName, groupFrame);
		if (!isMask && !isGroup && framePrefixLen && name.compare(0, framePrefixLen, framePrefix) != 0) {
			continue;
		}
		const content::JsonValue &f = kv.second;
		const content::JsonValue *frameRect = f.find("frame");
		int fx = 0, fy = 0, fw = 0, fh = 0;
		if (!reqInt(frameRect, "x", fx) || !reqInt(frameRect, "y", fy)
			|| !reqInt(frameRect, "w", fw) || !reqInt(frameRect, "h", fh)) {
			continue;
		}
		const content::JsonValue *rot = f.find("rotated");
		if (rot && rot->asBool(false)) {
			continue; // rotated frames unsupported (parity with P0)
		}
		if (isGroup) {
			// A sprite cell: listed under its group, cut only when the group is first drawn. The
			// cell is taken whole; a trimmed cell is not a classic sprite cell and is left out.
			const content::JsonValue *trim = f.find("trimmed");
			if ((trim && trim->asBool(false)) || fx < 0 || fy < 0 || fw <= 0 || fh <= 0
				|| fx + fw > pageSurface->w || fy + fh > pageSurface->h) {
				continue;
			}
			std::string lower = groupName;
			for (char &c : lower) { c = (char) std::tolower((unsigned char) c); }
			auto git = classicGroupByName.find(lower);
			if (git == classicGroupByName.end()) {
				git = classicGroupByName.emplace(lower, (int) classicGroups.size()).first;
				classicGroups.emplace_back();
				classicGroups.back().name = groupName;
			}
			std::vector<hdatlas::HdFrameRecord> &cells = classicGroups[git->second].cells;
			if ((size_t) groupFrame >= cells.size()) {
				cells.resize((size_t) groupFrame + 1, hdatlas::HdFrameRecord{});
			}
			hdatlas::HdFrameRecord &cell = cells[groupFrame];
			cell.sx = (uint16_t) fx; cell.sy = (uint16_t) fy; cell.sw = (uint16_t) fw; cell.sh = (uint16_t) fh;
			cell.cw = (uint16_t) fw; cell.ch = (uint16_t) fh;
			continue;
		}
		int offx = 0, offy = 0;
		if (const content::JsonValue *sss = f.find("spriteSourceSize")) {
			reqInt(sss, "x", offx);
			reqInt(sss, "y", offy);
		}
		int cw = fw, ch = fh;
		if (const content::JsonValue *ss = f.find("sourceSize")) {
			reqInt(ss, "w", cw);
			reqInt(ss, "h", ch);
		}
		hdatlas::HdFrameRecord rec{};
		rec.nameHash = hdatlas::fnv1a(name.c_str());
		rec.page = 0;
		rec.sx = (uint16_t) fx; rec.sy = (uint16_t) fy; rec.sw = (uint16_t) fw; rec.sh = (uint16_t) fh;
		rec.offx = (int16_t) offx; rec.offy = (int16_t) offy;
		rec.cw = (uint16_t) cw; rec.ch = (uint16_t) ch;
		rec.rotated = 0;
		byHash.emplace(rec.nameHash, (uint32_t) frames.size());
		frames.push_back(rec);
		if (maskPrefixLen) {
			cuts.push_back(isMask ? CutKeyed : 0);
			if (isMask) {
				if (firstMask < 0) {
					firstMask = (int) frames.size() - 1;
				}
				rec.nameHash = hdatlas::fnv1a((name + "_dither").c_str());
				byHash.emplace(rec.nameHash, (uint32_t) frames.size());
				frames.push_back(rec);
				cuts.push_back(CutKeyed | CutDither);
			}
		}
	}
	if (firstMask >= 0) {
		// Two full cells of the size of a mask: all fog, and all explored-but-unseen ground.
		hdatlas::HdFrameRecord rec = frames[firstMask];
		const std::string solid = std::string(maskPrefix) + "solid";
		rec.nameHash = hdatlas::fnv1a(solid.c_str());
		byHash.emplace(rec.nameHash, (uint32_t) frames.size());
		frames.push_back(rec);
		cuts.push_back(CutSolid);
		rec.nameHash = hdatlas::fnv1a((solid + "_dither").c_str());
		byHash.emplace(rec.nameHash, (uint32_t) frames.size());
		frames.push_back(rec);
		cuts.push_back(CutSolid | CutDither);
	}
	if (frames.empty() || !pageSurface) {
		if (pageSurface) { SDL_FreeSurface(pageSurface); }
		for (SDL_Texture *tex : pages) {
			if (tex) { SDL_DestroyTexture(tex); }
		}
		pages.clear();
		return false;
	}

	// D0: upload the page. If it fits within GL_MAX_TEXTURE_SIZE, one texture (unchanged behaviour on
	// desktop/emulator where the cap is large). Otherwise re-pack the frames -- each <= ~900px, so none
	// straddles -- into maxDim x maxDim shelf pages so every frame lives wholly inside one sub-texture.
	// Page count is bounded (~atlas_area / maxDim^2); no per-frame textures, no eviction needed.
	int maxDim = 4096;
	{
		SDL_RendererInfo info;
		if (TheRenderer && SDL_GetRendererInfo(TheRenderer, &info) == 0
			&& info.max_texture_width > 0 && info.max_texture_height > 0) {
			maxDim = std::min(info.max_texture_width, info.max_texture_height);
		}
	}
	NetworkLoadingPing(); // between the decode and the upload
	bool uploadOk = true;
	if (palette) {
		// Tiles: index 0 is a normal colour. Fog masks (index 0 = nothing) are told apart by `cuts`.
		uint8_t rgba[256][4];
		hdatlas::expandPalette6(palette, false, rgba);
		uploadOk = UploadPalettedFrames(pageSurface, rgba, maxDim, frames, pages, cuts);
	} else if (pageSurface->w <= maxDim && pageSurface->h <= maxDim) {
		SDL_Texture *tex = UploadSurfaceToTexture(pageSurface);
		if (tex) {
			pages.push_back(tex);
		} else {
			uploadOk = false;
		}
	} else {
		SDL_Surface *page = SDL_CreateRGBSurfaceWithFormat(0, maxDim, maxDim, 32, SDL_PIXELFORMAT_RGBA32);
		if (!page) {
			uploadOk = false;
		} else {
			int shelfX = 0, shelfY = 0, shelfH = 0, pageIdx = 0;
			auto flushPage = [&]() -> bool {
				NetworkLoadingPing(); // every re-packed page
				SDL_Texture *tex = UploadSurfaceToTexture(page);
				if (!tex) {
					return false;
				}
				pages.push_back(tex);
				return true;
			};
			SDL_SetSurfaceBlendMode(pageSurface, SDL_BLENDMODE_NONE);
			for (size_t i = 0; uploadOk && i < frames.size(); ++i) {
				hdatlas::HdFrameRecord &rec = frames[i];
				const int fw = rec.sw, fh = rec.sh;
				if (fw > maxDim || fh > maxDim) {
					continue; // a single frame larger than the cap (never happens for Remastered atlases)
				}
				if (shelfX + fw > maxDim) { // wrap to the next shelf row
					shelfX = 0;
					shelfY += shelfH;
					shelfH = 0;
				}
				if (shelfY + fh > maxDim) { // page full -> upload it, start a fresh page
					if (!flushPage()) {
						uploadOk = false;
						break;
					}
					SDL_FillRect(page, nullptr, SDL_MapRGBA(page->format, 0, 0, 0, 0));
					++pageIdx;
					shelfX = shelfY = shelfH = 0;
				}
				SDL_Rect s = {rec.sx, rec.sy, rec.sw, rec.sh};
				SDL_Rect d = {shelfX, shelfY, rec.sw, rec.sh};
				SDL_BlitSurface(pageSurface, &s, page, &d);
				rec.page = (uint16_t) pageIdx;
				rec.sx = (uint16_t) shelfX;
				rec.sy = (uint16_t) shelfY;
				shelfX += fw;
				if (fh > shelfH) {
					shelfH = fh;
				}
			}
			if (uploadOk && !flushPage()) { // upload the last (partial) page
				uploadOk = false;
			}
			SDL_FreeSurface(page);
		}
	}
	if (uploadOk && !classicGroups.empty()) {
		// Classic view: the indexed page and the palette stay for ClassicGroup().
		static unsigned serial = 0;
		classicPage = pageSurface;
		classicPalette.assign(palette, palette + 768);
		classicSerial = ++serial;
	} else {
		SDL_FreeSurface(pageSurface);
		classicGroups.clear();
		classicGroupByName.clear();
	}
	if (!uploadOk) {
		for (SDL_Texture *tex : pages) {
			if (tex) { SDL_DestroyTexture(tex); }
		}
		pages.clear();
		return false;
	}
	if (fogMask && !nearest) {
		// EM1: the 216x216 bgs_Fow masks are drawn scaled DOWN to the ~66px tile screen rect; the
		// default nearest filtering quantizes their smooth alpha gradients into hard tile-edge
		// staircases ("two masks" artifact). Filter the fog pages bilinearly so the authored
		// feathered edges survive the downscale. Fog-only: sprite/terrain pages stay nearest.
		for (SDL_Texture *tex : pages) {
			if (tex) { SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear); }
		}
	}
	// "upload" holds the frame table and the upload (with the re-pack, when the page is cut).
	ErrorPrint("ATLAS load %s %dx%d read+decode %u ms convert %u ms upload %u ms pages %d in-game %d\n",
	           resolved.stem().string().c_str(), pageW, pageH,
	           page.decodeMs, page.convertMs,
	           (unsigned) (SDL_GetTicks() - ticksConverted), (int) pages.size(),
	           (GameRunning && GameCycle > 0) ? 1 : 0);
	return true;
}

int CAtlasGraphic::ClassicGroupByName(const std::string &group) const
{
	std::string lower = group;
	for (char &c : lower) { c = (char) std::tolower((unsigned char) c); }
	const auto it = classicGroupByName.find(lower);
	return it != classicGroupByName.end() ? it->second : -1;
}

const CAtlasGraphic *CAtlasGraphic::ClassicGroup(int group, int rampStart)
{
	if (!classicPage || group < 0 || group >= (int) classicGroups.size()
		|| rampStart < 0 || rampStart + hdatlas::ClassicTeamCount > 256) {
		return nullptr;
	}
	ClassicGroupData &data = classicGroups[group];
	if (data.hasTeam < 0) {
		// Team-colour pixels in any cell? Without them one cut serves every player colour.
		data.hasTeam = 0;
		SDL_LockSurface(classicPage);
		const Uint8 *base = static_cast<const Uint8 *>(classicPage->pixels);
		for (size_t i = 0; !data.hasTeam && i < data.cells.size(); ++i) {
			const hdatlas::HdFrameRecord &cell = data.cells[i];
			for (int y = 0; !data.hasTeam && y < cell.sh; ++y) {
				const Uint8 *row = base + (cell.sy + y) * classicPage->pitch + cell.sx;
				for (int x = 0; x < cell.sw; ++x) {
					if (row[x] >= hdatlas::ClassicTeamFirst
						&& row[x] < hdatlas::ClassicTeamFirst + hdatlas::ClassicTeamCount) {
						data.hasTeam = 1;
						break;
					}
				}
			}
		}
		SDL_UnlockSurface(classicPage);
	}
	const int key = data.hasTeam ? rampStart : hdatlas::ClassicTeamFirst;
	const auto it = data.byRamp.find(key);
	if (it != data.byRamp.end()) {
		return it->second.get();
	}
	std::unique_ptr<CAtlasGraphic> &slot = data.byRamp[key]; // stays null when the cut fails
	int maxDim = 4096;
	{
		SDL_RendererInfo info;
		if (TheRenderer && SDL_GetRendererInfo(TheRenderer, &info) == 0
			&& info.max_texture_width > 0 && info.max_texture_height > 0) {
			maxDim = std::min(info.max_texture_width, info.max_texture_height);
		}
	}
	uint8_t rgba[256][4];
	hdatlas::expandPalette6(classicPalette.data(), true, rgba);
	hdatlas::classicApplyRamp(rgba, key);
	auto cut = std::make_unique<CAtlasGraphic>();
	cut->frames = data.cells;
	if (!UploadPalettedFrames(classicPage, rgba, maxDim, cut->frames, cut->pages, {})) {
		for (SDL_Texture *tex : cut->pages) {
			if (tex) { SDL_DestroyTexture(tex); }
		}
		cut->pages.clear();
		return nullptr;
	}
	slot = std::move(cut);
	return slot.get();
}

// FNV-1a of the key -> frame index (or -1). Hash matches the importer and hd_atlas.h.
int CAtlasGraphic::FrameByName(const char *name) const
{
	auto it = byHash.find(hdatlas::fnv1a(name));
	return it != byHash.end() ? (int) it->second : -1;
}

// Draw one trimmed atlas frame with its untrimmed canvas centred on (anchorX, anchorY). Mirrors
// CGraphic::DrawFrameClipTex for the zoom + RenderCopy: only the destination size scales by the
// world zoom, so the GPU upscales the HD art in place (byte-identical to native at zoom==1).
void CAtlasGraphic::DrawAtlasFrameClip(int frame, int anchorX, int anchorY) const
{
	if (pages.empty() || frame < 0 || frame >= (int) frames.size()) {
		return;
	}
	const hdatlas::HdFrameRecord &rec = frames[frame];
	Assert(rec.rotated == 0);
	if (rec.page >= pages.size() || !pages[rec.page]) {
		return;
	}
	const int dw = (GpuWorldDrawZoom == 1.0f) ? rec.sw : (int) std::lround(rec.sw * GpuWorldDrawZoom);
	const int dh = (GpuWorldDrawZoom == 1.0f) ? rec.sh : (int) std::lround(rec.sh * GpuWorldDrawZoom);
	SDL_Rect src = {rec.sx, rec.sy, rec.sw, rec.sh};
	SDL_Rect dst = {anchorX - rec.cw / 2 + rec.offx, anchorY - rec.ch / 2 + rec.offy, dw, dh};
	SDL_RenderCopy(TheRenderer, pages[rec.page], &src, &dst);
}

// Draw one frame with its RAW untrimmed HD canvas (cw x ch) mapped onto the caller's on-screen rect
// (dstX, dstY, dstW, dstH). The stored frame is trimmed (offx/offy into that canvas, sw x sh actual
// pixels), so scale the trim offset and size by the canvas->footprint ratio to place the sub-frame
// proportionally. Buildings need this instead of DrawAtlasFrameClip: their HD sourceSize (cw) is the
// full-resolution art (e.g. ~986px), far larger than the classic tile footprint (e.g. 128px), so a
// native sw x sh blit would draw the building ~6x oversized. An (r,g,b) != 255 tints the page via
// SDL_SetTextureColorMod (reset to 255 after) for the player-colour `_team` mask; `flip` mirrors.
void CAtlasGraphic::DrawAtlasFrameClipScaled(int frame, int dstX, int dstY, int dstW, int dstH,
                                             bool flip, uint8_t r, uint8_t g, uint8_t b) const
{
	if (pages.empty() || frame < 0 || frame >= (int) frames.size()) {
		return;
	}
	const hdatlas::HdFrameRecord &rec = frames[frame];
	Assert(rec.rotated == 0);
	if (rec.page >= pages.size() || !pages[rec.page]) {
		return;
	}
	const double sxScale = (rec.cw != 0) ? (double) dstW / rec.cw : 1.0;
	const double syScale = (rec.ch != 0) ? (double) dstH / rec.ch : 1.0;
	// NG7: the trimmed content lives at [offx, offx+sw] inside the cw-wide canvas. When mirroring the
	// frame the content must be re-anchored to [cw-offx-sw, cw-offx]; otherwise the flipped sprite (and
	// its independently-trimmed `_team` mask) drift sideways off the unit centre. Mirror the x offset.
	const int offxEff = flip ? (rec.cw - rec.offx - rec.sw) : rec.offx;
	SDL_Rect src = {rec.sx, rec.sy, rec.sw, rec.sh};
	SDL_Rect dst = {dstX + (int) std::lround(offxEff * sxScale),
	                dstY + (int) std::lround(rec.offy * syScale),
	                (int) std::lround(rec.sw * sxScale),
	                (int) std::lround(rec.sh * syScale)};
	const bool tint = (r != 255 || g != 255 || b != 255);
	if (tint) {
		SDL_SetTextureColorMod(pages[rec.page], r, g, b);
	}
	if (flip) {
		SDL_RenderCopyEx(TheRenderer, pages[rec.page], &src, &dst, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
	} else {
		SDL_RenderCopy(TheRenderer, pages[rec.page], &src, &dst);
	}
	if (tint) {
		SDL_SetTextureColorMod(pages[rec.page], 255, 255, 255);
	}
}

// SPRITE-SCALE-TRUE (SPEC_SPRITE_SCALE_TRUE.md section 4): the frame's own canvas, centred on
// (cx, cy), at `s` screen pixels per native pixel. Trim offset, mirroring and tint are handled as in
// DrawAtlasFrameClipScaled; the difference is that no caller-side rectangle stands in for the canvas.
void CAtlasGraphic::DrawAtlasFrameCentred(int frame, float cx, float cy, float s, bool flip,
                                          uint8_t r, uint8_t g, uint8_t b) const
{
	if (pages.empty() || frame < 0 || frame >= (int) frames.size()) {
		return;
	}
	const hdatlas::HdFrameRecord &rec = frames[frame];
	Assert(rec.rotated == 0);
	if (rec.page >= pages.size() || !pages[rec.page]) {
		return;
	}
	// The trimmed content lives at [offx, offx + sw] inside the cw-wide canvas; mirrored, it lives
	// at [cw - offx - sw, cw - offx] (NG7).
	const int offxEff = flip ? (rec.cw - rec.offx - rec.sw) : rec.offx;
	const double halfW = rec.cw / 2.0;
	const double halfH = rec.ch / 2.0;
	const int x0 = (int) std::lround(cx + (offxEff - halfW) * s);
	const int x1 = (int) std::lround(cx + (offxEff + rec.sw - halfW) * s);
	const int y0 = (int) std::lround(cy + (rec.offy - halfH) * s);
	const int y1 = (int) std::lround(cy + (rec.offy + rec.sh - halfH) * s);
	if (x1 <= x0 || y1 <= y0) {
		return;
	}
	SDL_Rect src = {rec.sx, rec.sy, rec.sw, rec.sh};
	SDL_Rect dst = {x0, y0, x1 - x0, y1 - y0};
	const bool tint = (r != 255 || g != 255 || b != 255);
	if (tint) {
		SDL_SetTextureColorMod(pages[rec.page], r, g, b);
	}
	if (flip) {
		SDL_RenderCopyEx(TheRenderer, pages[rec.page], &src, &dst, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
	} else {
		SDL_RenderCopy(TheRenderer, pages[rec.page], &src, &dst);
	}
	if (tint) {
		SDL_SetTextureColorMod(pages[rec.page], 255, 255, 255);
	}
}

// World HD (P1): blit the whole frame's src rect scaled into the caller-supplied dst rect. Used by
// the map terrain draw, which computes the exact on-screen tile rect itself; no canvas-centre/zoom
// bookkeeping here (terrain frames are untrimmed 216px, so full frame == canvas).
void CAtlasGraphic::DrawAtlasFrameFull(int frame, const SDL_Rect &dst) const
{
	if (pages.empty() || frame < 0 || frame >= (int) frames.size()) {
		return;
	}
	const hdatlas::HdFrameRecord &rec = frames[frame];
	Assert(rec.rotated == 0);
	if (rec.page >= pages.size() || !pages[rec.page]) {
		return;
	}
	SDL_Rect src = {rec.sx, rec.sy, rec.sw, rec.sh};
	SDL_RenderCopy(TheRenderer, pages[rec.page], &src, &dst);
}

void CAtlasGraphic::DrawAtlasFrameFullAlpha(int frame, const SDL_Rect &dst, uint8_t alpha) const
{
	if (pages.empty() || frame < 0 || frame >= (int) frames.size()) {
		return;
	}
	const hdatlas::HdFrameRecord &rec = frames[frame];
	Assert(rec.rotated == 0);
	if (rec.page >= pages.size() || !pages[rec.page]) {
		return;
	}
	SDL_SetTextureAlphaMod(pages[rec.page], alpha);
	SDL_Rect src = {rec.sx, rec.sy, rec.sw, rec.sh};
	SDL_RenderCopy(TheRenderer, pages[rec.page], &src, &dst);
	SDL_SetTextureAlphaMod(pages[rec.page], 255);
}

void CAtlasGraphic::DrawAtlasFramePart(int frame, int srcW, int srcH, const SDL_Rect &dst) const
{
	if (pages.empty() || frame < 0 || frame >= (int) frames.size() || srcW <= 0 || srcH <= 0) {
		return;
	}
	const hdatlas::HdFrameRecord &rec = frames[frame];
	Assert(rec.rotated == 0);
	if (rec.page >= pages.size() || !pages[rec.page]) {
		return;
	}
	SDL_Rect src = {rec.sx, rec.sy, std::min<int>(srcW, rec.sw), std::min<int>(srcH, rec.sh)};
	SDL_RenderCopy(TheRenderer, pages[rec.page], &src, &dst);
}

/**
**  Lazily build and cache a small per-frame texture from mSurface, for RGBA HUD graphics whose
**  whole sheet was too large for one GPU texture (mTexture == null, e.g. the tall icons_hd sheet).
**  Returns null for the normal case (mTexture set), for paletted sheets, or on failure -> CPU blit.
*/
SDL_Texture *CGraphic::GetHudFrameTexture(unsigned frame) const
{
	if (mTexture || !mSurface || !TheRenderer || frame >= frame_map.size()) {
		return nullptr;
	}
	if (mSurface->format->palette) {
		return nullptr; // paletted: keep the CPU path (colour cycling / player-colour remap)
	}
	auto it = mHudFrameTex.find(frame);
	if (it != mHudFrameTex.end()) {
		return it->second;
	}
	SDL_Texture *tex = nullptr;
	SDL_Surface *fs = SDL_CreateRGBSurfaceWithFormat(0, Width, Height, 32, SDL_PIXELFORMAT_RGBA32);
	if (fs) {
		SDL_Rect srect = {frame_map[frame].x, frame_map[frame].y, Width, Height};
		SDL_FillRect(fs, nullptr, 0);
		const SDL_BlendMode prev = SDL_BLENDMODE_NONE;
		SDL_GetSurfaceBlendMode(mSurface, const_cast<SDL_BlendMode *>(&prev));
		SDL_SetSurfaceBlendMode(mSurface, SDL_BLENDMODE_NONE); // copy pixels incl. alpha
		SDL_BlitSurface(mSurface, &srect, fs, nullptr);
		SDL_SetSurfaceBlendMode(mSurface, prev);
		tex = SDL_CreateTextureFromSurface(TheRenderer, fs);
		if (tex) {
			SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
		}
		SDL_FreeSurface(fs);
	}
	mHudFrameTex[frame] = tex; // cache (even null) to avoid rebuild attempts each frame
	return tex;
}

/**
**  Like GetHudFrameTexture, but for an oversized RGBA player-colour sheet (ui/icons_hd.png) whose
**  team region is a baked MAGENTA mask and which ships no grey _team.png. Swap the magenta for the
**  player's colour the same way HD unit sprites tint their grey mask (ramp[0] scaled by the mask
**  intensity), so the icon matches the model's team colour. Cached per (frame, colorIndex).
*/
SDL_Texture *CPlayerColorGraphic::GetHudFrameColorTexture(unsigned frame, int colorIndex) const
{
	if (mTexture || !mSurface || !TheRenderer || frame >= frame_map.size()) {
		return nullptr;
	}
	if (mSurface->format->palette) {
		return nullptr; // paletted: classic palette remap handles it
	}
	if (colorIndex < 0 || (size_t)colorIndex >= PlayerColorsRGB.size()
	    || PlayerColorsRGB[colorIndex].empty()) {
		return GetHudFrameTexture(frame); // no valid ramp -> draw the untinted base frame
	}
	const uint32_t key = ((uint32_t)frame << 8) | (uint32_t)(colorIndex & 0xff);
	auto it = mHudFrameColorTex.find(key);
	if (it != mHudFrameColorTex.end()) {
		return it->second;
	}
	const CColor c0 = PlayerColorsRGB[colorIndex][0]; // brightest ramp shade; models tint by this too
	SDL_Texture *tex = nullptr;
	SDL_Surface *fs = SDL_CreateRGBSurfaceWithFormat(0, Width, Height, 32, SDL_PIXELFORMAT_RGBA32);
	if (fs) {
		SDL_Rect srect = {frame_map[frame].x, frame_map[frame].y, Width, Height};
		SDL_FillRect(fs, nullptr, 0);
		SDL_BlendMode prev = SDL_BLENDMODE_NONE;
		SDL_GetSurfaceBlendMode(mSurface, &prev);
		SDL_SetSurfaceBlendMode(mSurface, SDL_BLENDMODE_NONE); // copy pixels incl. alpha
		SDL_BlitSurface(mSurface, &srect, fs, nullptr);
		SDL_SetSurfaceBlendMode(mSurface, prev);
		SDL_LockSurface(fs);
		Uint8 *base = (Uint8 *)fs->pixels;
		const int n = Width * Height;
		for (int i = 0; i < n; ++i) {
			Uint8 *p = base + i * 4; // RGBA32: p[0]=R p[1]=G p[2]=B p[3]=A
			const int r = p[0], g = p[1], b = p[2], a = p[3];
			// M666-9: a strip built with the game's team mask carries the exact key (v, 0, v)
			// (BuildHDIconStrip). The guess -- high R & B, low G, roughly balanced R~B -- stays
			// for a strip read from a file; on the game's art it finds about half of the team
			// region (1470 of 3120 px on forest_0) and none of it on a red picture (forest_40).
			const bool team = HDIconStripHasTeamKey()
			                  ? (g == 0 && r == b && r > 0)
			                  : (r > 48 && b > 48 && g < r - 24 && g < b - 24 && std::abs(r - b) < 120);
			if (a > 24 && team) {
				const int inten = (r + b) / 2; // mask "grey" -> shade the player colour
				p[0] = (Uint8)(c0.R * inten / 255);
				p[1] = (Uint8)(c0.G * inten / 255);
				p[2] = (Uint8)(c0.B * inten / 255);
				// keep alpha
			}
		}
		SDL_UnlockSurface(fs);
		tex = SDL_CreateTextureFromSurface(TheRenderer, fs);
		if (tex) {
			SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
		}
		SDL_FreeSurface(fs);
	}
	mHudFrameColorTex[key] = tex; // cache (even null) to avoid per-frame rebuilds
	return tex;
}

/**
**  GPU-pipeline: RenderCopy one frame into an explicit destination rect. The map-tile path
**  computes dst from consecutive rounded tile-grid edges so neighbours abut with no gap at any
**  zoom. Paletted tiles (no mTexture) fall back to a native-size CPU blit at the rect origin,
**  the same fallback DrawFrameClip takes, so colour-cycling terrain keeps working.
*/
void CGraphic::DrawFrameClipTexDst(unsigned frame, const SDL_Rect &dst) const
{
	if (frame >= frame_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, false);
		return;
	}
	if (!mTexture) {
		DrawSubClip(frame_map[frame].x, frame_map[frame].y,
					Width, Height, dst.x, dst.y, TheScreen);
		return;
	}
	SDL_Rect src = {frame_map[frame].x, frame_map[frame].y, Width, Height};
	SDL_Rect d = dst;
	SDL_RenderCopy(TheRenderer, mTexture, &src, &d);
}

/**
**  GPU-pipeline: RenderCopy the whole image to the backbuffer at (x,y). srcRect == nullptr means
**  the entire texture. Used for the static opaque HUD fillers (full-image blits). The texture keeps
**  SDL_BLENDMODE_BLEND from upload so transparent cut-outs let the world/backbuffer show through.
*/
void CGraphic::DrawClipTex(int x, int y) const
{
	if (!mTexture) {
		return;
	}
	SDL_Rect dst = {x, y, Width, Height};
	SDL_RenderCopy(TheRenderer, mTexture, nullptr, &dst);
}

/**
**  Crisp-zoom: draw a frame scaled to an explicit destination size via SDL_BlitScaled.
**  Used by the terrain pass of the crisp render path to magnify a (possibly higher-res)
**  tile to its on-screen size. Clipping is delegated to surface->clip_rect, which the
**  crisp path constrains to the painted viewport region. This routine is never called
**  when EnableCrispZoom is off.
*/
void CGraphic::DrawFrameClipScaled(unsigned frame, int x, int y, int w, int h,
								   SDL_Surface *surface /*= TheScreen*/) const
{
	if (frame >= frame_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, false);
		return;
	}
	// M666-3: in the HUD phase go to the backbuffer like the unscaled draws do (DrawSub,
	// DrawFrameClip): the sheet's texture, or the per-frame one of a sheet too big for a texture.
	if (GpuHudDrawActive && surface == TheScreen) {
		SDL_Rect dst = {x, y, w, h};
		if (mTexture) {
			SDL_Rect src = {frame_map[frame].x, frame_map[frame].y, Width, Height};
			HudRenderCopy(mTexture, src, dst);
			return;
		}
		if (SDL_Texture *ft = GetHudFrameTexture(frame)) {
			SDL_Rect src = {0, 0, Width, Height};
			HudRenderCopy(ft, src, dst);
			return;
		}
	}
	SDL_Rect srect = {Sint16(frame_map[frame].x), Sint16(frame_map[frame].y),
					  Uint16(Width), Uint16(Height)};
	SDL_Rect drect = {Sint16(x), Sint16(y), Uint16(w), Uint16(h)};
	SDL_BlitScaled(mSurface, &srect, surface, &drect);
}

void CGraphic::DrawNineSlice(const SDL_Rect &dest,
							 int left, int top, int right, int bottom,
							 unsigned frame /*= 0*/,
							 SDL_Surface *surface /*= TheScreen*/) const
{
	if (frame >= frame_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, false);
		return;
	}
	const int ox = frame_map[frame].x;
	const int oy = frame_map[frame].y;

	NineSlicePatch patches[9];
	ComputeNineSlice(Width, Height, left, top, right, bottom, dest, patches);

	// Same GPU/CPU split as DrawSub: on the HUD GPU path route each patch through
	// the backbuffer, otherwise scale-blit it on the CPU. Corners come out 1:1
	// (equal src/dst size), edges and centre are stretched by the blit.
	const bool gpu = GpuHudDrawActive && mTexture && surface == TheScreen;
	for (const NineSlicePatch &p : patches) {
		if (p.src.w <= 0 || p.src.h <= 0 || p.dst.w <= 0 || p.dst.h <= 0) {
			continue;
		}
		SDL_Rect srect = {ox + p.src.x, oy + p.src.y, p.src.w, p.src.h};
		SDL_Rect drect = {p.dst.x, p.dst.y, p.dst.w, p.dst.h};
		if (gpu) {
			HudRenderCopy(mTexture, srect, drect);
		} else {
			SDL_BlitScaled(mSurface, &srect, surface, &drect);
		}
	}
}

void CGraphic::DrawNineSliceScaled(const SDL_Rect &dest, int srcBorder, int dstBorder,
								   unsigned frame /*= 0*/) const
{
	if (frame >= frame_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, false);
		return;
	}
	if (dest.w <= 0 || dest.h <= 0) {
		return;
	}
	const int ox = frame_map[frame].x;
	const int oy = frame_map[frame].y;
	// Neither border may pass the middle of its rectangle.
	const int sb = std::max(0, std::min(srcBorder, std::min(Width, Height) / 2));
	const int db = std::max(0, std::min(dstBorder, std::min(dest.w, dest.h) / 2));
	const int sx[4] = {0, sb, Width - sb, Width};
	const int sy[4] = {0, sb, Height - sb, Height};
	const int dx[4] = {dest.x, dest.x + db, dest.x + dest.w - db, dest.x + dest.w};
	const int dy[4] = {dest.y, dest.y + db, dest.y + dest.h - db, dest.y + dest.h};

	// Same GPU/CPU split as DrawNineSlice.
	const bool gpu = GpuHudDrawActive && mTexture;
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			SDL_Rect srect = {ox + sx[col], oy + sy[row], sx[col + 1] - sx[col], sy[row + 1] - sy[row]};
			SDL_Rect drect = {dx[col], dy[row], dx[col + 1] - dx[col], dy[row + 1] - dy[row]};
			if (srect.w <= 0 || srect.h <= 0 || drect.w <= 0 || drect.h <= 0) {
				continue;
			}
			if (gpu) {
				HudRenderCopy(mTexture, srect, drect);
			} else {
				SDL_BlitScaled(mSurface, &srect, TheScreen, &drect);
			}
		}
	}
}

void CGraphic::DrawFrameTrans(unsigned frame, int x, int y, int alpha,
							  SDL_Surface *surface /*= TheScreen*/) const
{
	if (frame >= frame_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, false);
		return;
	}
	DrawSubTrans(frame_map[frame].x, frame_map[frame].y,
				 Width, Height, x, y, alpha, surface);
}

void CGraphic::DrawFrameClipTrans(unsigned frame, int x, int y, int alpha,
								  SDL_Surface *surface /* = TheScreen*/) const
{
	if (frame >= frame_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, false);
		return;
	}
	DrawSubClipTrans(frame_map[frame].x, frame_map[frame].y,
					 Width, Height, x, y, alpha, surface);
}

void CGraphic::DrawFrameClipCustomMod(unsigned frame, int x, int y,
									  pixelModifier modifier,
									  const uint32_t param,
									  SDL_Surface *surface /* = TheScreen*/) const
{
	if (frame >= frame_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, false);
		return;
	}
	DrawSubClipCustomMod(frame_map[frame].x, frame_map[frame].y,
						 Width, Height, x, y, modifier, param, surface);
}

/**
**  Draw graphic object clipped and with player colors.
**
**  @param player  player number
**  @param frame   number of frame (object index)
**  @param x       x coordinate on the target surface
**  @param y       y coordinate on the target surface
**	@param surface target surface
*/
/**
**  Load the sibling <file>_team.png grey player-colour mask, if present, and (when the sprite
**  is flipped) a horizontally-mirrored copy aligned with SurfaceFlip. Absence leaves TeamMask
**  null so the classic 8-bit palette remap path is used instead.
*/
void CPlayerColorGraphic::LoadTeamMask(const std::string &baseFile)
{
	if (NoClassicFile) {
		return; // CLASSIC-SHEET-LOOKUPS: the HD atlas has its own team masks
	}
	std::string maskFile = baseFile;
	const auto dot = maskFile.rfind(".png");
	if (dot == std::string::npos) {
		return;
	}
	maskFile.insert(dot, "_team");
	const fs::path name = LibraryFileName(maskFile);
	if (name.empty()) {
		return;
	}
	auto fp = std::make_unique<CFile>();
	if (fp->open(name.string().c_str(), CL_OPEN_READ) == -1) {
		return;
	}
	TeamMask = IMG_Load_RW(CFile::to_SDL_RWops(std::move(fp)), 1);
	if (TeamMask && SurfaceFlip) {
		TeamMaskFlip = SDL_ConvertSurface(TeamMask, TeamMask->format, 0);
		SDL_LockSurface(TeamMask);
		SDL_LockSurface(TeamMaskFlip);
		const int w = TeamMask->w;
		const int h = TeamMask->h;
		const int sp = TeamMask->pitch / 4;
		const int dp = TeamMaskFlip->pitch / 4;
		for (int yy = 0; yy < h; ++yy) {
			for (int xx = 0; xx < w; ++xx) {
				((Uint32 *)TeamMaskFlip->pixels)[xx + yy * dp] =
					((Uint32 *)TeamMask->pixels)[(w - 1 - xx) + yy * sp];
			}
		}
		SDL_UnlockSurface(TeamMaskFlip);
		SDL_UnlockSurface(TeamMask);
	}

	// GPU-pipeline (Phase 1): upload the grey mask once. At draw time SetTextureColorMod tints it
	// to the player's colour and it is RenderCopy'd (BLEND) over the base frame. The flipped case
	// reuses this same texture via RenderCopyEx horizontal flip, so no TeamMaskFlip texture is
	// needed. Skipped for paletted masks (kept on the CPU path).
	if (TeamMask && !mTextureTeam) {
		mTextureTeam = UploadMaskToTexture(TeamMask);
	}
}

/**
**  Build (and cache) a whole-sheet copy of the base surface with the player's colour composited
**  into the team region, keyed by colour index. The grey mask value indexes the player's colour
**  ramp, reproducing the shaded team colour that the palette remap gave classic sprites.
*/
SDL_Surface *CPlayerColorGraphic::GetColorVariant(int colorIndex, bool flip)
{
	std::map<int, SDL_Surface *> &cache = flip ? ColorVariantsFlip : ColorVariants;
	auto it = cache.find(colorIndex);
	if (it != cache.end()) {
		return it->second;
	}
	SDL_Surface *base = flip ? SurfaceFlip : mSurface;
	SDL_Surface *mask = flip ? TeamMaskFlip : TeamMask;
	if (!base || !mask) {
		return base;
	}
	SDL_Surface *v = SDL_ConvertSurface(base, base->format, 0);
	if (colorIndex >= 0 && colorIndex < (int)PlayerColorsRGB.size() && !PlayerColorsRGB[colorIndex].empty()) {
		const std::vector<CColor> &ramp = PlayerColorsRGB[colorIndex];
		SDL_LockSurface(v);
		SDL_LockSurface(mask);
		const int w = v->w;
		const int h = v->h;
		const int vp = v->pitch / 4;
		const int mp = mask->pitch / 4;
		for (int yy = 0; yy < h; ++yy) {
			for (int xx = 0; xx < w; ++xx) {
				Uint8 mr, mg, mb, ma;
				SDL_GetRGBA(((Uint32 *)mask->pixels)[xx + yy * mp], mask->format, &mr, &mg, &mb, &ma);
				if (ma > 0 && mr > 0) {
					// Team-colour composite: out.rgb = pure player colour * (maskGray/255),
					// keeping the base alpha. The mask grey (mr) carries the shading, so the
					// canonical/brightest ramp entry scaled by it reproduces a vivid, correctly
					// shaded player colour. (The old quantized ramp index inverted the brightness
					// -> bright mask pixels hit the darkest shade -> a muddy grey "highlight".)
					const CColor &c = ramp[0];
					Uint8 br, bg, bb, ba;
					SDL_GetRGBA(((Uint32 *)v->pixels)[xx + yy * vp], v->format, &br, &bg, &bb, &ba);
					const Uint8 rr = (Uint8)(c.R * mr / 255);
					const Uint8 gg = (Uint8)(c.G * mr / 255);
					const Uint8 bl = (Uint8)(c.B * mr / 255);
					((Uint32 *)v->pixels)[xx + yy * vp] = SDL_MapRGBA(v->format, rr, gg, bl, ba);
				}
			}
		}
		SDL_UnlockSurface(mask);
		SDL_UnlockSurface(v);
	}
	SDL_SetSurfaceBlendMode(v, SDL_BLENDMODE_BLEND);
	cache[colorIndex] = v;
	return v;
}

/**
**  GPU-pipeline (Phase 1): draw a player-coloured frame with NO CPU baking. RenderCopy the base
**  frame, then (if a team-mask texture exists) tint the grey mask to the player's colour via
**  SetTextureColorMod and RenderCopy the same frame of the mask on top (BLEND). This reproduces
**  the baked look of GetColorVariant() at zero per-player CPU cost. flip => mirror both copies.
*/
void CPlayerColorGraphic::DrawPlayerColorFrameClipTex(int colorIndex, unsigned frame,
													  int x, int y, bool flip /*= false*/)
{
	if (!mTexture || frame >= frame_map.size()) {
		return;
	}
	// Base frame.
	if (flip) {
		DrawFrameClipTexX(frame, x, y);
	} else {
		DrawFrameClipTex(frame, x, y);
	}
	if (!mTextureTeam) {
		return;
	}
	// Team-colour pass: tint the grey mask to the player's canonical colour (ramp[0], matching
	// GetColorVariant) and blend it over the base frame. mr in the mask carries the shading.
	Uint8 r = 255, g = 255, b = 255;
	if (colorIndex >= 0 && colorIndex < (int)PlayerColorsRGB.size()
		&& !PlayerColorsRGB[colorIndex].empty()) {
		const CColor &c = PlayerColorsRGB[colorIndex][0];
		r = c.R;
		g = c.G;
		b = c.B;
	}
	// Mask must scale exactly like the base frame (drawn just above) so the tint overlays it 1:1.
	const int dw = (GpuWorldDrawZoom == 1.0f) ? Width : (int)std::lround(Width * GpuWorldDrawZoom);
	const int dh = (GpuWorldDrawZoom == 1.0f) ? Height : (int)std::lround(Height * GpuWorldDrawZoom);
	SDL_Rect src = {frame_map[frame].x, frame_map[frame].y, Width, Height};
	SDL_Rect dst = {x, y, dw, dh};
	SDL_SetTextureColorMod(mTextureTeam, r, g, b);
	if (flip) {
		SDL_RenderCopyEx(TheRenderer, mTextureTeam, &src, &dst, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
	} else {
		SDL_RenderCopy(TheRenderer, mTextureTeam, &src, &dst);
	}
	// Reset so a later plain RenderCopy of this mask texture is not left tinted.
	SDL_SetTextureColorMod(mTextureTeam, 255, 255, 255);
}

void CPlayerColorGraphic::DrawPlayerColorFrameClip(int colorIndex, unsigned frame,
												   int x, int y,
												   SDL_Surface *surface /*= TheScreen*/)
{
	// GPU-pipeline (Phase 1): HD (RGBA) sprites take the GPU path; if a team mask texture exists it
	// is tinted+blended for the player colour, otherwise just the base frame is drawn (which
	// matches the CPU fallback below, where GraphicPlayerPixels is a no-op on paletteless sprites).
	// Classic 8-bit paletted sprites (mTexture == nullptr) keep the CPU palette-remap path so
	// player colours still render; off-screen targets also fall back.
	// GpuHudDrawActive shares the GPU path so player-coloured HUD icons stay correctly tinted (the
	// CPU swap path below would otherwise reach DrawSubClip with the un-baked base texture). Zoom is
	// 1.0 outside the world draw, so the HUD icon is native-sized.
	if ((GpuWorldDrawActive || GpuHudDrawActive) && mTexture && surface == TheScreen) {
		DrawPlayerColorFrameClipTex(colorIndex, frame, x, y, false);
		return;
	}
	// HD player-colour icons (ui/icons_hd.png): oversized RGBA sheet -> mTexture is null so the GPU
	// gate above never fires, its team mask is baked as magenta and it has no _team.png so the CPU
	// GetColorVariant path below also cannot colour it. Draw a per-player recoloured per-frame
	// texture so the icon's team colour matches the unit model (else it renders raw magenta).
	if (GpuHudDrawActive && !mTexture && surface == TheScreen && !TeamMask
	    && mSurface && mSurface->format->palette == nullptr) {
		if (SDL_Texture *ft = GetHudFrameColorTexture(frame, colorIndex)) {
			SDL_Rect src = {0, 0, Width, Height};
			SDL_Rect dst = {x, y, Width, Height};
			HudRenderCopy(ft, src, dst);
			return;
		}
	}
	if (mSurface && mSurface->format->palette == nullptr && TeamMask) {
		SDL_Surface *saved = mSurface;
		mSurface = GetColorVariant(colorIndex, false);
		DrawFrameClip(frame, x, y, surface);
		mSurface = saved;
	} else {
		GraphicPlayerPixels(colorIndex, *this);
		DrawFrameClip(frame, x, y, surface);
	}
}

/**
**  Crisp-zoom player-colour variant of DrawFrameClipScaled. Only used on the crisp path.
*/
void CPlayerColorGraphic::DrawPlayerColorFrameClipScaled(int colorIndex, unsigned frame,
														 int x, int y, int w, int h,
														 SDL_Surface *surface /*= TheScreen*/)
{
	// M666-9: the same per-player frame texture DrawPlayerColorFrameClip uses for the oversized
	// icon sheet. Without it the scaled draw fell to the plain blit below and the picture kept the
	// art's own purple (portrait cloth (80, 24, 88) beside blue units, capture c666_c).
	if (GpuHudDrawActive && !mTexture && surface == TheScreen && !TeamMask
	    && mSurface && mSurface->format->palette == nullptr) {
		if (SDL_Texture *ft = GetHudFrameColorTexture(frame, colorIndex)) {
			SDL_Rect src = {0, 0, Width, Height};
			SDL_Rect dst = {x, y, w, h};
			HudRenderCopy(ft, src, dst);
			return;
		}
	}
	if (mSurface && mSurface->format->palette == nullptr && TeamMask) {
		SDL_Surface *saved = mSurface;
		mSurface = GetColorVariant(colorIndex, false);
		DrawFrameClipScaled(frame, x, y, w, h, surface);
		mSurface = saved;
	} else {
		GraphicPlayerPixels(colorIndex, *this);
		DrawFrameClipScaled(frame, x, y, w, h, surface);
	}
}

/**
**  Draw graphic object unclipped and flipped in X direction.
**
**  @param frame   number of frame (object index)
**  @param x       x coordinate on the target surface
**  @param y       y coordinate on the target surface
**	@param surface target surface
*/
void CGraphic::DrawFrameX(unsigned frame, int x, int y,
						  SDL_Surface *surface /*= TheScreen*/) const
{
	SDL_Rect srect = {frameFlip_map[frame].x, frameFlip_map[frame].y, Uint16(Width), Uint16(Height)};
	SDL_Rect drect = {Sint16(x), Sint16(y), 0, 0};

	MarkOverlayDirty(surface);
	SDL_BlitSurface(SurfaceFlip, &srect, surface, &drect);
}

/**
**  Draw graphic object clipped and flipped in X direction.
**
**  @param frame   number of frame (object index)
**  @param x       x coordinate on the target surface
**  @param y       y coordinate on the target surface
**	@param surface target surface
*/
void CGraphic::DrawFrameClipX(unsigned frame, int x, int y,
							  SDL_Surface *surface /*= TheScreen*/) const
{
	// GPU-pipeline (Phase 1): mirror the base frame via RenderCopyEx (no SurfaceFlip needed).
	// frameFlip_map[f] points at "base frame f, mirrored", so the base rect frame_map[f] flipped
	// is pixel-identical. See DrawFrameClip for the guard rationale.
	if (GpuWorldDrawActive && mTexture && surface == TheScreen && frame < frame_map.size()) {
		DrawFrameClipTexX(frame, x, y);
		return;
	}
	// GPU-pipeline (HUD): mirrored RGBA HUD frame via RenderCopyEx from the base texture, 1:1 pixels.
	if (GpuHudDrawActive && mTexture && surface == TheScreen && frame < frame_map.size()) {
		SDL_Rect src = {frame_map[frame].x, frame_map[frame].y, Width, Height};
		SDL_Rect dst = {x, y, Width, Height};
		SDL_Rect clip = {ClipX1, ClipY1, ClipX2 - ClipX1 + 1, ClipY2 - ClipY1 + 1};
		SDL_RenderSetClipRect(TheRenderer, &clip);
		SDL_RenderCopyEx(TheRenderer, mTexture, &src, &dst, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
		SDL_RenderSetClipRect(TheRenderer, nullptr);
		return;
	}
	if (frame >= frameFlip_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, true);
		return;
	}
	SDL_Rect srect = {frameFlip_map[frame].x, frameFlip_map[frame].y, Uint16(Width), Uint16(Height)};

	// Crisp-zoom: scale the (flipped) frame up to on-screen size at the ·Zoom position.
	if (CrispZoomDrawScale != 1.0f) {
		CrispBlitScaled(SurfaceFlip, srect, x, y, surface);
		return;
	}

	const int oldx = x;
	const int oldy = y;
	CLIP_RECTANGLE(x, y, srect.w, srect.h);
	srect.x += x - oldx;
	srect.y += y - oldy;

	SDL_Rect drect = {Sint16(x), Sint16(y), 0, 0};

	MarkOverlayDirty(surface);
	SDL_BlitSurface(SurfaceFlip, &srect, surface, &drect);
}

void CGraphic::DrawFrameTransX(unsigned frame, int x, int y, int alpha,
							   SDL_Surface *surface /*= TheScreen*/) const
{
	if (frame >= frameFlip_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, true);
		return;
	}
	SDL_Rect srect = {frameFlip_map[frame].x, frameFlip_map[frame].y, Uint16(Width), Uint16(Height)};
	SDL_Rect drect = {Sint16(x), Sint16(y), 0, 0};
	Uint8 oldalpha = 0xff;
	SDL_GetSurfaceAlphaMod(SurfaceFlip, &oldalpha);

	SDL_SetSurfaceAlphaMod(SurfaceFlip, alpha);
	MarkOverlayDirty(surface);
	SDL_BlitSurface(SurfaceFlip, &srect, surface, &drect);
	SDL_SetSurfaceAlphaMod(SurfaceFlip, oldalpha);
}

void CGraphic::DrawFrameClipTransX(unsigned frame, int x, int y, int alpha,
								   SDL_Surface *surface /*= TheScreen*/) const
{
	if (frame >= frameFlip_map.size()) {
		WarnInvalidGraphicFrame(*this, frame, true);
		return;
	}
	SDL_Rect srect = {frameFlip_map[frame].x, frameFlip_map[frame].y, Uint16(Width), Uint16(Height)};

	// Crisp-zoom: scale the (flipped, alpha) frame up to on-screen size at the ·Zoom position.
	if (CrispZoomDrawScale != 1.0f) {
		Uint8 saved = 0xff;
		SDL_GetSurfaceAlphaMod(SurfaceFlip, &saved);
		SDL_SetSurfaceAlphaMod(SurfaceFlip, alpha);
		CrispBlitScaled(SurfaceFlip, srect, x, y, surface);
		SDL_SetSurfaceAlphaMod(SurfaceFlip, saved);
		return;
	}

	int oldx = x;
	int oldy = y;
	CLIP_RECTANGLE(x, y, srect.w, srect.h);
	srect.x += x - oldx;
	srect.y += y - oldy;

	SDL_Rect drect = {Sint16(x), Sint16(y), 0, 0};
	Uint8 oldalpha = 0xff;
	SDL_GetSurfaceAlphaMod(SurfaceFlip, &oldalpha);

	SDL_SetSurfaceAlphaMod(SurfaceFlip, alpha);
	MarkOverlayDirty(surface);
	SDL_BlitSurface(SurfaceFlip, &srect, surface, &drect);
	SDL_SetSurfaceAlphaMod(SurfaceFlip, oldalpha);
}

/**
**  Draw graphic object clipped, flipped, and with player colors.
**
**  @param player  player number
**  @param frame   number of frame (object index)
**  @param x       x coordinate on the target surface
**  @param y       y coordinate on the target surface
**  @param surface target surface
*/
void CPlayerColorGraphic::DrawPlayerColorFrameClipX(int colorIndex, unsigned frame,
													int x, int y,
													SDL_Surface *surface /*= TheScreen*/)
{
	// GPU-pipeline (Phase 1): mirrored HD player-colour draw via RenderCopyEx from the base (+
	// optional mask) texture (frame indexes the un-flipped layout; see DrawFrameClipX). CPU
	// fallback for paletted sprites / off-screen targets.
	if ((GpuWorldDrawActive || GpuHudDrawActive) && mTexture && surface == TheScreen
		&& frame < frame_map.size()) {
		DrawPlayerColorFrameClipTex(colorIndex, frame, x, y, true);
		return;
	}
	if (SurfaceFlip && SurfaceFlip->format->palette == nullptr && TeamMaskFlip) {
		SDL_Surface *saved = SurfaceFlip;
		SurfaceFlip = GetColorVariant(colorIndex, true);
		DrawFrameClipX(frame, x, y, surface);
		SurfaceFlip = saved;
	} else {
		GraphicPlayerPixels(colorIndex, *this);
		DrawFrameClipX(frame, x, y, surface);
	}
}

/*----------------------------------------------------------------------------
--  Global functions
----------------------------------------------------------------------------*/

/**
**  Make a new graphic object.
**
**  @param filename  Filename
**  @param w     Width of a frame (optional)
**  @param h     Height of a frame (optional)
**
**  @return      New graphic object
*/
std::shared_ptr<CGraphic> CGraphic::New(const std::string &filename, const int w, const int h)
{
	if (filename.empty()) {
		return std::make_shared<CGraphic>();
	}

	const fs::path file = LibraryFileName(filename);
	auto &cache = GraphicHash[file];
	auto res = cache;
	if (res == nullptr) {
		res = std::make_shared<CGraphic>();
		// FIXME: use a constructor for this
		res->File = file;
		res->HashFile = res->File.string();
		res->Width = w;
		res->Height = h;
		cache = res;
	} else {
		DebugPrint("f:%s,w%d,W%d,H%d,h%d\n", filename.c_str(), w, res->Width, res->Height, h);
		Assert((w == 0 || res->Width == w) && (res->Height == h || h == 0));
	}

	return res;
}

/**
**  Make a new player color graphic object.
**
**  @param filename  Filename
**  @param w     Width of a frame (optional)
**  @param h     Height of a frame (optional)
**
**  @return      New graphic object
*/
std::shared_ptr<CPlayerColorGraphic> CPlayerColorGraphic::New(const std::string &filename, int w, int h)
{
	if (filename.empty()) {
		return std::make_unique<CPlayerColorGraphic>();
	}

	const fs::path file = LibraryFileName(filename);
	auto &cache = GraphicHash[file];
	auto res = std::dynamic_pointer_cast<CPlayerColorGraphic>(cache);
	if (res == nullptr) {
		res = std::make_shared<CPlayerColorGraphic>();
		// FIXME: use a constructor for this
		res->File = file;
		res->HashFile = res->File.string();
		res->Width = w;
		res->Height = h;
		cache = res;
	} else {
		Assert((w == 0 || res->Width == w) && (res->Height == h || h == 0));
	}

	return res;
}

/**
**  Make a new graphic object.  Don't reuse a graphic from the hash table.
**
**  @param file  Filename
**  @param w     Width of a frame (optional)
**  @param h     Height of a frame (optional)
**
**  @return      New graphic object
*/
std::shared_ptr<CGraphic> CGraphic::ForceNew(const std::string &file, const int w, const int h)
{
	auto g = std::make_shared<CGraphic>();
	g->File = file;
	g->HashFile = file + std::to_string(HashCount++);
	g->Width = w;
	g->Height = h;
	GraphicHash[g->HashFile] = g;

	return g;
}

/**
**  Clone a graphic
**
**  @param grayscale  Make grayscale texture
*/
std::shared_ptr<CPlayerColorGraphic> CPlayerColorGraphic::Clone(bool grayscale) const
{
	auto g = CPlayerColorGraphic::ForceNew(this->File.string(), this->Width, this->Height);

	if (this->IsLoaded()) {
		g->Load(grayscale);
	}
	return g;
}

/**
**  Get a graphic object.
**
**  @param filename  Filename
**
**  @return      Graphic object
*/
std::shared_ptr<CGraphic> CGraphic::Get(const std::string &filename)
{
	if (filename.empty()) {
		return nullptr;
	}

	const fs::path file = LibraryFileName(filename);
	auto &cache = GraphicHash[file];

	return cache;
}

/**
**  Get a player color graphic object.
**
**  @param filename  Filename
**
**  @return      Graphic object
*/
std::shared_ptr<CPlayerColorGraphic> CPlayerColorGraphic::Get(const std::string &filename)
{
	if (filename.empty()) {
		return nullptr;
	}

	const fs::path file = LibraryFileName(filename);
	auto cache = GraphicHash[file];

	return std::dynamic_pointer_cast<CPlayerColorGraphic>(cache);
}

/**
**  Make a new player color graphic object.  Don't reuse a graphic from the
**  hash table.
**
**  @param file  Filename
**  @param w     Width of a frame (optional)
**  @param h     Height of a frame (optional)
**
**  @return      New graphic object
*/
std::shared_ptr<CPlayerColorGraphic> CPlayerColorGraphic::ForceNew(const std::string &file, int w, int h)
{
	auto g = std::make_shared<CPlayerColorGraphic>();
	g->File = file;
	g->HashFile = file + std::to_string(HashCount++);
	g->Width = w;
	g->Height = h;
	GraphicHash[g->HashFile] = g;

	return g;
}

void CGraphic::GenFramesMap()
{
	Assert(NumFrames != 0);
	Assert(mSurface != nullptr);
	Assert(Width != 0);
	Assert(Height != 0);

	frame_map.resize(NumFrames);

	for (int frame = 0; frame < NumFrames; ++frame) {
		frame_map[frame].x = (frame % (mSurface->w / Width)) * Width;
		frame_map[frame].y = (frame / (mSurface->w / Width)) * Height;
	}
}

static void ApplyGrayScale(SDL_Surface *Surface, int Width, int Height)
{
	SDL_LockSurface(Surface);
	const SDL_PixelFormat *f = Surface->format;
	const int bpp = Surface->format->BytesPerPixel;
	const double redGray = 0.21;
	const double greenGray = 0.72;
	const double blueGray = 0.07;

	switch (bpp) {
		case 1: {
			SDL_Color colors[256];
			SDL_Palette &pal = *Surface->format->palette;
			for (int i = 0; i < 256; ++i) {
				const int gray = redGray * pal.colors[i].r + greenGray * pal.colors[i].g + blueGray * pal.colors[i].b;
				colors[i].r = colors[i].g = colors[i].b = gray;
			}
			SDL_SetPaletteColors(&pal, &colors[0], 0, 256);
			break;
		}
		case 4: {
			// 32-bit RGBA (HD PNG skins). Decode each pixel via the surface format so the
			// luminance weights hit the real R/G/B channels and the ORIGINAL alpha is preserved
			// (transparent stays transparent). Iterate the whole surface, not just one frame
			// cell, so every frame of a multi-frame sheet is desaturated. The previous version
			// read *(p+1..3) as neighbouring PIXELS (not channels) and shifted nonsensically,
			// which corrupted every HD graphic loaded grayscale (disabled buttons, gray icons).
			Uint8 *base = static_cast<Uint8 *>(Surface->pixels);
			for (int i = 0; i < Surface->h; ++i) {
				Uint32 *row = reinterpret_cast<Uint32 *>(base + i * Surface->pitch);
				for (int j = 0; j < Surface->w; ++j) {
					Uint8 r, g, b, a;
					SDL_GetRGBA(row[j], f, &r, &g, &b, &a);
					const Uint8 gray = static_cast<Uint8>(redGray * r + greenGray * g + blueGray * b);
					row[j] = SDL_MapRGBA(f, gray, gray, gray, a);
				}
			}
			break;
		}
	}
	SDL_UnlockSurface(Surface);
}

/**
**  Build a placeholder surface so a single missing/broken graphic degrades
**  gracefully instead of taking down the whole program. It must be:
**   - fully TRANSPARENT, so a missing in-game graphic (a construction overlay,
**     an effect) draws nothing rather than leaving a coloured stain on the map;
**   - sized to the graphic's own frame cell (w x h) when known, so the frame-grid
**     maths stays valid (NumFrames >= 1) and drawing does not index out of bounds.
*/
static SDL_Surface *MakePlaceholderSurface(int w, int h)
{
	if (w < 1) { w = 16; }
	if (h < 1) { h = 16; }
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
	if (s) {
		SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
		SDL_FillRect(s, nullptr, SDL_MapRGBA(s->format, 0, 0, 0, 0)); // fully transparent
	}
	return s;
}

/**
**  Load a graphic
**
**  @param grayscale  Make a grayscale surface
*/
void CGraphic::Load(bool grayscale)
{
	// NG68: beat the debug watchdog's heartbeat. The blocking phase of a mission load is the
	// per-sprite graphics load, and it runs with the loading screen INACTIVE, so neither the
	// ShowLoadProgress hook nor the UpdateLoadingScreen one covers it -- the watchdog kept
	// reporting "main loop STALLED 3..11s at 'UpdateDisplay:begin'" on every mission load, which
	// is a false alarm pointing at the wrong place. This is the choke point that actually spends
	// the seconds (one call per image file, not a per-frame path), and a genuine hang while
	// loading a sprite now trips the watchdog labelled 'loading:graphics'.
	DBG_CHECKPOINT("loading:graphics");
	if (mSurface) {
		return;
	}
	NetworkLoadingPing(); // LAN-MESH S8: the long part of a load; nothing happens outside one

	// IC1: a few HD command graphics are synthesised in-engine from the native Remastered atlases
	// (Portrait-face icons, portrait_button frame states) rather than shipping a derived PNG, so
	// they are reachable from the Remastered folder alone. Try the builder by basename first; if the
	// native atlas is absent the builder returns nullptr and we fall back to the on-disk file below.
	// ICON-TEAM-KEY: true for the icon strip built with the team key (see the upload below).
	bool teamKeyedStrip = false;
	{
		const std::string base = File.filename().string();
		if (base == "icons_hd.png") {
			// The classic tileset directory in the path names the portrait set; the atlas keys are
			// the terrain atlas names (classic wasteland == swamp_, classic swamp == xswamp_).
			const std::string path = File.generic_string();
			const char *set = path.find("/winter/") != std::string::npos ? "ice"
			                  : path.find("/wasteland/") != std::string::npos ? "swamp"
			                  : path.find("/swamp/") != std::string::npos ? "xswamp" : "forest";
			mSurface = BuildHDIconStrip(set);
			teamKeyedStrip = mSurface && HDIconStripHasTeamKey();
		} else if (base == "hd_cmd_frame.png") {
			mSurface = BuildHDCommandFrameStrip();
		} else if (base == "hud_TopLeft.png" || base == "hud_TileableLeft.png") {
			const std::string path = File.string();
			const char *race = (path.find("/orc/") != std::string::npos
			                    || path.find("\\orc\\") != std::string::npos) ? "Orc" : "Human";
			const char *frame = (base == "hud_TopLeft.png") ? "TopLeft" : "TileableLeft";
			mSurface = BuildHDSidebarPanel(race, frame);
		} else if (base == "f5_menu.png") {
			const std::string path = File.string();
			const char *race = (path.find("/orc/") != std::string::npos
			                    || path.find("\\orc\\") != std::string::npos) ? "orc" : "human";
			mSurface = BuildHDHudF5Strip(race, Width ? Width : 200, Height ? Height : 100);
		} else if (base == "hud_btn_small.png" || base == "hud_btn_small_pressed.png") {
			const std::string path = File.string();
			const char *race = (path.find("/orc/") != std::string::npos
			                    || path.find("\\orc\\") != std::string::npos) ? "Orc" : "Human";
			const bool pressed = (base == "hud_btn_small_pressed.png");
			mSurface = BuildHDHudSmallButton(race, pressed, Width ? Width : 400, Height ? Height : 100);
		} else if (base == "hud_complete_bar.png") {
			mSurface = BuildHDHudCompleteBar(Width ? Width : 790, Height ? Height : 100);
		}
	}

	if (!mSurface && NoClassicFile) {
		mSurface = MakePlaceholderSurface(Width, Height); // CLASSIC-SHEET-LOOKUPS: the HD atlas draws it
	}
	fs::path name;
	if (!mSurface) {
		name = LibraryFileName(File.string());
	}
	// A missing or unreadable graphic must NOT kill the program: a single absent
	// skin/menu asset (e.g. a race that ships fewer button variants) would otherwise
	// take down the whole app via ExitFatal -> _Exit (silent on Android). Log loudly
	// and substitute a visible placeholder so the owning menu/HUD still opens.
	if (mSurface) {
		// already synthesised above -> skip the on-disk decode
	} else if (name.empty()) {
		ErrorPrint("Can't load the graphic '%s': file not found; using placeholder\n",
		           File.u8string().c_str());
		mSurface = MakePlaceholderSurface(Width, Height);
	} else {
		auto fp = std::make_unique<CFile>();
		if (fp->open(name.string().c_str(), CL_OPEN_READ) == -1) {
			ErrorPrint("Can't open the graphic '%s': %s; using placeholder\n",
			           File.u8string().c_str(), strerror(errno));
			mSurface = MakePlaceholderSurface(Width, Height);
		} else {
			mSurface = IMG_Load_RW(CFile::to_SDL_RWops(std::move(fp)), 1);
			if (mSurface == nullptr) {
				ErrorPrint("Couldn't decode the graphic '%s': %s; using placeholder\n",
				           name.u8string().c_str(), IMG_GetError());
				mSurface = MakePlaceholderSurface(Width, Height);
			}
		}
	}
	if (mSurface == nullptr) {
		// Only reached if even a 16x16 surface could not be allocated (true OOM).
		ErrorPrint("Can't allocate a placeholder surface for '%s'\n", File.u8string().c_str());
		ExitFatal(-1);
	}

	OriginWidth = mSurface->w;
	OriginHeight = mSurface->h;

	if (mSurface->format->BytesPerPixel == 1) {
		VideoPaletteListAdd(mSurface);
	}

	if (!Width) {
		Width = GetGraphicWidth();
	}
	if (!Height) {
		Height = GetGraphicHeight();
	}

	Assert(Width <= GetGraphicWidth() && Height <= GetGraphicHeight());

	if (GetGraphicWidth() % Width != 0 || GetGraphicHeight() % Height != 0) {
		// A wrong-sized asset must not be fatal (mirrors the missing-graphic placeholder policy):
		// when the image does not tile evenly into the declared frame, fall back to treating the
		// whole image as a single frame so it still loads instead of aborting startup.
		ErrorPrint("Invalid graphic (width, height) '%s'\n"
		           "Expected: (%d,%d)  Found: (%d,%d); using the whole image as one frame\n",
		           File.u8string().c_str(),
		           Width,
		           Height,
		           GetGraphicWidth(),
		           GetGraphicHeight());
		Width = GetGraphicWidth();
		Height = GetGraphicHeight();
	}

	NumFrames = GetGraphicWidth() / Width * GetGraphicHeight() / Height;

	// Remember the grayscale request so SetOriginalSize() (which frees and reloads mSurface from
	// file during a Resize cycle) re-applies it. Without this the graphic reverts to full colour on
	// the second Resize, silently undoing a grayscale (disabled/locked) load.
	Grayscale = grayscale;
	if (grayscale) {
		ApplyGrayScale(mSurface, Width, Height);
	}

	GenFramesMap();

	// GPU-pipeline (Phase 1): upload an RGBA copy once so tiles/units can be RenderCopy'd off the
	// CPU compositor. mSurface is intentionally NOT freed here: CPU-read consumers (screenshots,
	// the debug colour bridge) and the non-GPU / zoomed CPU draw paths still need it. Paletted
	// graphics return nullptr and stay fully on the CPU path.
	// ICON-TEAM-KEY: the icon strip that carries the team key (v, 0, v) gets no sheet texture.
	// Every HUD draw of it then takes a per-frame texture (GetHudFrameTexture), and a draw in a
	// player's colour the recoloured one (GetHudFrameColorTexture). With a sheet texture the
	// `!mTexture` gates of DrawPlayerColorFrameClip[Scaled] would be shut and the sheet would go
	// to the screen as stored, the key itself showing in the team region.
	if (teamKeyedStrip) {
		static bool logged = false;
		if (!logged) {
			logged = true;
			LogPrint("ICONS team key strip %dx%d: no sheet texture, per-frame textures\n",
			         mSurface->w, mSurface->h);
		}
	} else if (!mTexture) {
		mTexture = UploadSurfaceToTexture(mSurface);
	}
}

/**
**  Free a SDL surface
**
**  @param surface  SDL surface to free
*/
static void FreeSurface(SDL_Surface **surface)
{
	if (!*surface) {
		return;
	}
	VideoPaletteListRemove(*surface);

	unsigned char *pixels = nullptr;

	if ((*surface)->flags & SDL_PREALLOC) {
		pixels = (unsigned char *)(*surface)->pixels;
	}

	SDL_FreeSurface(*surface);
	delete[] pixels;
	*surface = nullptr;
}

/**
**  Free a graphic
*/
CGraphic::~CGraphic()
{
	// GPU-pipeline (Phase 1): release this graphic's GPU copy. (The CPlayerColorGraphic team-mask
	// texture is released in ~CPlayerColorGraphic; shared_ptr's type-erased deleter runs it.)
	if (mTexture) {
		SDL_DestroyTexture(mTexture);
		mTexture = nullptr;
	}
	FreeSurface(&mSurface);
	FreeSurface(&SurfaceFlip);
}

CPlayerColorGraphic::~CPlayerColorGraphic()
{
	// GPU-pipeline (Phase 1): release the team-mask GPU copy. The pre-existing TeamMask/
	// ColorVariants CPU surfaces are left as-is (their lifetime already spans the program).
	if (mTextureTeam) {
		SDL_DestroyTexture(mTextureTeam);
		mTextureTeam = nullptr;
	}
	for (auto &kv : mHudFrameColorTex) {
		if (kv.second) {
			SDL_DestroyTexture(kv.second);
		}
	}
	mHudFrameColorTex.clear();
}

/**
**  Flip graphic and store in graphic->SurfaceFlip
*/
void CGraphic::Flip()
{
	if (SurfaceFlip) {
		return;
	}

	SDL_Surface *s = SurfaceFlip = SDL_ConvertSurface(mSurface, mSurface->format, 0);
	Uint32 ckey;
	if (!SDL_GetColorKey(mSurface, &ckey)) {
		// Colour-keyed (classic paletted) source: keep the key + opaque blit.
		SDL_SetColorKey(SurfaceFlip, SDL_TRUE, ckey);
		SDL_SetSurfaceBlendMode(SurfaceFlip, SDL_BLENDMODE_NONE);
	} else {
		// No colour key: an RGBA (HD) source relies on per-pixel alpha. Mirror the source's
		// blend mode so the flipped frame's transparent margin stays transparent; a forced
		// NONE here blits that margin as opaque black (a black box behind left-facing units).
		SDL_BlendMode bm = SDL_BLENDMODE_BLEND;
		SDL_GetSurfaceBlendMode(mSurface, &bm);
		SDL_SetSurfaceBlendMode(SurfaceFlip, bm);
	}
	if (SurfaceFlip->format->BytesPerPixel == 1) {
		VideoPaletteListAdd(SurfaceFlip);
	}
	SDL_LockSurface(mSurface);
	SDL_LockSurface(s);
	switch (s->format->BytesPerPixel) {
		case 1:
			for (int i = 0; i < s->h; ++i) {
				for (int j = 0; j < s->w; ++j) {
					((char *)s->pixels)[j + i * s->pitch] =
						((char *)mSurface->pixels)[s->w - j - 1 + i * mSurface->pitch];
				}
			}
			break;
		case 3:
			for (int i = 0; i < s->h; ++i) {
				for (int j = 0; j < s->w; ++j) {
					memcpy(&((char *)s->pixels)[j * 3 + i * s->pitch],
						   &((char *)mSurface->pixels)[(s->w - j - 1) * 3 + i * mSurface->pitch], 3);
				}
			}
			break;
		case 4: {
			for (int i = 0; i < s->h; ++i) {
				for (int j = 0; j < s->w; ++j) {
					memcpy(&((char *)s->pixels)[j * 4 + i * s->pitch],
						   &((char *)mSurface->pixels)[(s->w - j - 1) * 4 + i * mSurface->pitch], 4);
				}
			}
		}
		break;
	}
	SDL_UnlockSurface(mSurface);
	SDL_UnlockSurface(s);

	frameFlip_map.resize(NumFrames);
	for (int frame = 0; frame < NumFrames; ++frame) {
		frameFlip_map[frame].x = ((NumFrames - frame - 1) % (SurfaceFlip->w / Width)) * Width;
		frameFlip_map[frame].y = (frame / (SurfaceFlip->w / Width)) * Height;
	}
}

/**
**  Resize a graphic
**
**  @param w  New width of graphic.
**  @param h  New height of graphic.
*/
void CGraphic::Resize(int w, int h)
{
	Assert(mSurface); // can't resize before it's been loaded

	if (GetGraphicWidth() == w && GetGraphicHeight() == h) {
		return;
	}

	// Resizing the same image multiple times looks horrible
	// If the image has already been resized then get a clean copy first
	if (Resized) {
		this->SetOriginalSize();
		if (GetGraphicWidth() == w && GetGraphicHeight() == h) {
			return;
		}
	}

	const int originWidth = GetGraphicWidth();
	const int originHeight = GetGraphicHeight();

	Resized = true;
	Uint32 ckey;
	bool useckey = !SDL_GetColorKey(mSurface, &ckey);

	int bpp = mSurface->format->BytesPerPixel;
	if (bpp == 1) {
		SDL_Color pal[256];

		SDL_LockSurface(mSurface);

		unsigned char *pixels = (unsigned char *)mSurface->pixels;
		unsigned char *data = new unsigned char[w * h];
		int x = 0;

		for (int i = 0; i < h; ++i) {
			for (int j = 0; j < w; ++j) {
				data[x] = pixels[(i * GetGraphicHeight() / h) * mSurface->pitch + j * GetGraphicWidth() / w];
				++x;
			}
		}

		SDL_UnlockSurface(mSurface);
		VideoPaletteListRemove(mSurface);

		memcpy(pal, mSurface->format->palette->colors, sizeof(SDL_Color) * 256);
		SDL_FreeSurface(mSurface);

		mSurface = SDL_CreateRGBSurfaceFrom(data, w, h, 8, w, 0, 0, 0, 0);
		if (mSurface->format->BytesPerPixel == 1) {
			VideoPaletteListAdd(mSurface);
		}
		SDL_SetPaletteColors(mSurface->format->palette, pal, 0, 256);
	} else {
		SDL_LockSurface(mSurface);

		unsigned char *pixels = (unsigned char *)mSurface->pixels;
		unsigned char *data = new unsigned char[w * h * bpp];
		int x = 0;

		for (int i = 0; i < h; ++i) {
			float fy = (float)i * GetGraphicHeight() / h;
			int iy = (int)fy;
			fy -= iy;
			for (int j = 0; j < w; ++j) {
				float fx = (float)j * GetGraphicWidth() / w;
				int ix = (int)fx;
				fx -= ix;
				float fz = (fx + fy) / 2;

				unsigned char *p1 = &pixels[iy * mSurface->pitch + ix * bpp];
				unsigned char *p2 = (iy != mSurface->h - 1) ?
									&pixels[(iy + 1) * mSurface->pitch + ix * bpp] :
									p1;
				unsigned char *p3 = (ix != mSurface->w - 1) ?
									&pixels[iy * mSurface->pitch + (ix + 1) * bpp] :
									p1;
				unsigned char *p4 = (iy != mSurface->h - 1 && ix != mSurface->w - 1) ?
									&pixels[(iy + 1) * mSurface->pitch + (ix + 1) * bpp] :
									p1;

				data[x * bpp + 0] = static_cast<unsigned char>(
										(p1[0] * (1 - fy) + p2[0] * fy +
										 p1[0] * (1 - fx) + p3[0] * fx +
										 p1[0] * (1 - fz) + p4[0] * fz) / 3.0 + .5);
				data[x * bpp + 1] = static_cast<unsigned char>(
										(p1[1] * (1 - fy) + p2[1] * fy +
										 p1[1] * (1 - fx) + p3[1] * fx +
										 p1[1] * (1 - fz) + p4[1] * fz) / 3.0 + .5);
				data[x * bpp + 2] = static_cast<unsigned char>(
										(p1[2] * (1 - fy) + p2[2] * fy +
										 p1[2] * (1 - fx) + p3[2] * fx +
										 p1[2] * (1 - fz) + p4[2] * fz) / 3.0 + .5);
				if (bpp == 4) {
					data[x * bpp + 3] = static_cast<unsigned char>(
											(p1[3] * (1 - fy) + p2[3] * fy +
											 p1[3] * (1 - fx) + p3[3] * fx +
											 p1[3] * (1 - fz) + p4[3] * fz) / 3.0 + .5);
				}
				++x;
			}
		}

		int Rmask = mSurface->format->Rmask;
		int Gmask = mSurface->format->Gmask;
		int Bmask = mSurface->format->Bmask;
		int Amask = mSurface->format->Amask;

		SDL_UnlockSurface(mSurface);
		VideoPaletteListRemove(mSurface);
		SDL_FreeSurface(mSurface);

		mSurface =
			SDL_CreateRGBSurfaceFrom(data, w, h, 8 * bpp, w * bpp, Rmask, Gmask, Bmask, Amask);
	}
	if (useckey) {
		SDL_SetColorKey(mSurface, SDL_TRUE, ckey);
	}

	Height = h / (originHeight / Height);
	Width = w / (originWidth / Width);
	Assert(GetGraphicWidth() / Width * GetGraphicHeight() / Height == NumFrames);

	GenFramesMap();
}

void CGraphic::ResizeKeepRatio(int width, int height)
{
	if (this->OriginWidth * height < width * this->OriginHeight) {
		this->Resize(this->OriginWidth * height / this->OriginHeight, height);
	} else {
		this->Resize(width, this->OriginHeight * width / this->OriginWidth);
	}
}

/**
**  Sets the original size for a graphic
**
*/
void CGraphic::SetOriginalSize()
{
	Assert(mSurface); // can't resize before it's been loaded

	if (!Resized) {
		return;
	}
	if (mSurface) {
		FreeSurface(&mSurface);
		mSurface = nullptr;
	}
	frame_map.clear();
	if (SurfaceFlip) {
		FreeSurface(&SurfaceFlip);
		SurfaceFlip = nullptr;
	}
	frameFlip_map.clear();

	this->Width = this->Height = 0;
	this->mSurface = nullptr;
	this->Load(this->Grayscale); // preserve a grayscale (disabled/locked) load across resize cycles

	Resized = false;
}

/**
**  Add additional frames to the end of this graphic set
**
**  @param frames  Vector of frame-sized SDL surfaces with frames to add
**
*/
void CGraphic::AppendFrames(const sequence_of_images &frames)
{
	Assert(!Resized); /// We can't add frames into a resized graphic set

	uint16_t currFrame = this->NumFrames;
	ExpandFor(frames.size());

	for (auto &frame : frames) {
		SDL_Rect dstRect { frame_map[currFrame].x, frame_map[currFrame].y, Width, Height };
		SDL_BlitSurface(frame.get(), nullptr, mSurface, &dstRect);
		currFrame++;
	}
}

/**
**  Expand graphic set for certain number of frames
**
**  @param numOfFramesToAdd  Number of frames to add into the graphic set
**
*/
void CGraphic::ExpandFor(const uint16_t numOfFramesToAdd)
{
	Assert(!Resized); /// We can't add frames into a resized graphic set

	if (numOfFramesToAdd == 0) {
		return;
	}
	const uint16_t cols = GetFrameCountPerRow();
	const int newGraphicHeight = GetGraphicHeight() + Height * ((numOfFramesToAdd - 1) / cols + 1);

	const SDL_PixelFormat *pf = mSurface->format;
	const uint8_t bpp = mSurface->format->BytesPerPixel;
	SDL_Surface *newSurface = SDL_CreateRGBSurface(SDL_SWSURFACE,
	                                               GetGraphicWidth(),
	                                               newGraphicHeight,
	                                               8 * bpp,
	                                               pf->Rmask,
	                                               pf->Gmask,
	                                               pf->Bmask,
	                                               pf->Amask);
	uint32_t ckey;
	const bool useckey = !SDL_GetColorKey(mSurface, &ckey);
	if (useckey) {
		SDL_SetColorKey(newSurface, SDL_TRUE, ckey);
	}

	SDL_FillRect(newSurface, nullptr, useckey ? ckey : 0);

	/// Copy pixels
	const uint8_t *src = static_cast<uint8_t*>(mSurface->pixels);
		  uint8_t *dst = static_cast<uint8_t*>(newSurface->pixels);
	const size_t dataSize = mSurface->pitch * mSurface->h;
	std::copy(src, &src[dataSize], dst);


	if (bpp == 1) {
		VideoPaletteListRemove(mSurface);

		const SDL_Palette *palette = mSurface->format->palette;
		SDL_SetPaletteColors(newSurface->format->palette, palette->colors, 0, palette->ncolors);

		VideoPaletteListAdd(newSurface);
	}

	SDL_FreeSurface(mSurface);
	mSurface = newSurface;
	NumFrames = GetGraphicWidth() / Width * GetGraphicHeight() / Height;

	GenFramesMap();
}

/**
**  Check if a pixel is transparent
**
**  @param x  X coordinate
**  @param y  Y coordinate
**
**  @return   True if the pixel is transparent, False otherwise
*/
bool CGraphic::TransparentPixel(int x, int y)
{
	int bpp = mSurface->format->BytesPerPixel;
	Uint32 colorkey;
	bool has_colorkey = !SDL_GetColorKey(mSurface, &colorkey);
	if ((bpp == 1 && !has_colorkey) || bpp == 3) {
		return false;
	}

	bool ret = false;
	SDL_LockSurface(mSurface);
	unsigned char *p = (unsigned char *)mSurface->pixels + y * mSurface->pitch + x * bpp;
	if (bpp == 1) {
		if (*p == colorkey) {
			ret = true;
		}
	} else {
		bool ckey = has_colorkey;
		if (ckey && *p == colorkey) {
			ret = true;
		} else if (p[mSurface->format->Ashift >> 3] == 255) {
			ret = true;
		}
	}
	SDL_UnlockSurface(mSurface);

	return ret;
}

/**
** Change a palette color.
*/
void CGraphic::SetPaletteColor(int idx, int r, int g, int b) {
	if (!mSurface) {
		return;
	}
	SDL_Color color;
	color.r = r;
	color.g = g;
	color.b = b;
	SDL_SetPaletteColors(mSurface->format->palette, &color, idx, 1);
}

void CGraphic::OverlayGraphic(CGraphic *other, bool mask)
{
	this->Load();
	other->Load();
	if (!mSurface) {
		PrintOnStdOut(
			Format("ERROR: Graphic %s not loaded in call to OverlayGraphic", this->File.c_str()));
		return;
	}
	if (!other->mSurface) {
		PrintOnStdOut(
			Format("ERROR: Graphic %s not loaded in call to OverlayGraphic", other->File.c_str()));
		return;
	}
	if (mSurface->w != other->mSurface->w || mSurface->h != other->mSurface->h) {
		PrintOnStdOut(Format("ERROR: Graphic %s has different size than %s OverlayGraphic",
		                     File.c_str(),
		                     other->File.c_str()));
		return;
	}

	int bpp = mSurface->format->BytesPerPixel;
	unsigned int srcColorKey;
	unsigned int dstColorKey;

	if (!((bpp == 1 && SDL_GetColorKey(other->mSurface, &srcColorKey) == 0 && SDL_GetColorKey(mSurface, &dstColorKey) == 0) || bpp == 4)) {
		PrintOnStdOut(Format("ERROR: OverlayGraphic only supported for 8-bit graphics with "
		                     "transparency or RGBA graphics (%s)",
		                     File.c_str()));
		return;
	}
	if ((bpp != other->mSurface->format->BytesPerPixel)) {
		PrintOnStdOut(Format(
			"ERROR: OverlayGraphic only supported graphics with same depth (%s depth != %s depth)",
			File.c_str(),
			other->File.c_str()));
		return;
	}

	SDL_LockSurface(mSurface);
	SDL_LockSurface(other->mSurface);

	switch (bpp) {
		case 1: {
			uint8_t *src_base = (uint8_t *)other->mSurface->pixels;
			uint8_t *dst_base = (uint8_t *)mSurface->pixels;

			for (int x = 0; x < mSurface->w; x++) {
				for (int y = 0; y < mSurface->h; y++) {
					uint8_t* src = src_base + x + y * other->mSurface->pitch;
					uint8_t* dst = dst_base + x + y * mSurface->pitch;
					if (*src != srcColorKey) {
						if (!mask) {
							*dst = *src;
						} else {
							*dst = dstColorKey;
						}
					}
				}
			}
			break;
		}
		case 4: {
			uint32_t *src_base = (uint32_t *)other->mSurface->pixels;
			uint32_t *dst_base = (uint32_t *)mSurface->pixels;

			for (int x = 0; x < mSurface->w; x++) {
				for (int y = 0; y < mSurface->h; y++) {
					uint32_t* src = src_base + x + y * other->mSurface->pitch;
					uint32_t* dst = dst_base + x + y * mSurface->pitch;
					double alphaSrc = (*src & other->mSurface->format->Amask) / 255.0;
					if (mask) {
						alphaSrc = 1 - alphaSrc;
					}
					double alphaDst = (*dst & mSurface->format->Amask) / 255.0;
					uint8_t rSrc = *src & other->mSurface->format->Rmask;
					uint8_t rDst = *dst & mSurface->format->Rmask;
					uint8_t gSrc = *src & other->mSurface->format->Gmask;
					uint8_t gDst = *dst & mSurface->format->Gmask;
					uint8_t bSrc = *src & other->mSurface->format->Bmask;
					uint8_t bDst = *dst & mSurface->format->Bmask;

					double aOut = std::min(1.0, alphaSrc + alphaDst * (1 - alphaSrc));
					uint8_t rOut = static_cast<uint8_t>((rSrc * alphaSrc + rDst * alphaDst * (1 - alphaSrc)) / aOut);
					uint8_t gOut = static_cast<uint8_t>((gSrc * alphaSrc + gDst * alphaDst * (1 - alphaSrc)) / aOut);
					uint8_t bOut = static_cast<uint8_t>((bSrc * alphaSrc + bDst * alphaDst * (1 - alphaSrc)) / aOut);

					*dst = (static_cast<uint8_t>(aOut * 255) << mSurface->format->Ashift) |
						(rOut << mSurface->format->Rshift) |
						(gOut << mSurface->format->Gshift) |
						(bOut << mSurface->format->Bshift);
				}
			}
			break;
		}
		default:
			break;
	}

	SDL_UnlockSurface(mSurface);
	SDL_UnlockSurface(other->mSurface);
}

static inline void dither(SDL_Surface *Surface) {
	for (int x = 0; x < Surface->w; x++) {
		for (int y = 0; y < Surface->h; y++) {
			Uint8* pixel = ((Uint8*)(Surface->pixels)) + x + y * Surface->pitch;
			if (*pixel != 255) {
				*pixel = (y % 2 == x % 2) ? 255 : 0;
			}
		}
	}
}

static void applyAlphaGrayscaleToSurface(SDL_Surface **src, int alpha)
{
	SDL_Surface *alphaSurface = SDL_CreateRGBSurface(0, (*src)->w, (*src)->h, 32, RMASK, GMASK, BMASK, AMASK);
	if (!alphaSurface) {
		ErrorPrint("Cannot create surface: %s\n", SDL_GetError());
		ExitFatal(-1);
	}
	SDL_BlitSurface(*src, nullptr, alphaSurface, nullptr);
	SDL_SetSurfaceAlphaMod(alphaSurface, alpha);
	SDL_SetSurfaceColorMod(alphaSurface, 0, 0, 0);
	SDL_FreeSurface(*src);
	*src = alphaSurface;
}

static void shrinkSurfaceFramesInY(SDL_Surface **src, int shrink, const std::vector<CGraphic::frame_pos_t> &frameMap, int frameW, int frameH)
{
	shrink = std::abs(shrink);
	SDL_Surface *alphaSurface = SDL_CreateRGBSurface(0, (*src)->w, (*src)->h, 32, RMASK, GMASK, BMASK, AMASK);
	if (!alphaSurface) {
		ErrorPrint("Cannot create surface: %s\n", SDL_GetError());
		ExitFatal(-1);
	}
	for (const auto& frame : frameMap) {
		const SDL_Rect srcRect = { frame.x, frame.y, frameW, frameH };
		SDL_Rect dstRect = { frame.x, frame.y + shrink / 2, frameW, frameH - (shrink - shrink / 2) };
		SDL_BlitScaled(*src, &srcRect, alphaSurface, &dstRect);
	}
	SDL_FreeSurface(*src);
	*src = alphaSurface;
}

static void shearSurface(SDL_Surface *surface, int xOffset, int yOffset, const std::vector<CGraphic::frame_pos_t> &frameMap, int frameW, int frameH)
{
	if (yOffset || xOffset) {
		SDL_LockSurface(surface);
		uint32_t* pixels = (uint32_t *)surface->pixels;
		int pitch = surface->pitch / sizeof(uint32_t);
		for (const auto& frame : frameMap) {
			for (int x = xOffset > 0 ? 0 : frameW - 1; xOffset > 0 ? x < frameW : x >= 0; xOffset > 0 ? x++ : x--) {
				for (int y = yOffset > 0 ? 0 : frameH - 1; yOffset > 0 ? y < frameH : y >= 0; yOffset > 0 ? y++ : y--) {
					int xNew = x + xOffset * y / frameH;
					int yNew = y + yOffset * x / frameW;
					if (xNew < 0 || yNew < 0 || xNew >= frameW || yNew >= frameH) {
						pixels[x + frame.x + (y + frame.y) * pitch] = 0;
					} else {
						pixels[x + frame.x + (y + frame.y) * pitch] = pixels[xNew + frame.x + (yNew + frame.y) * pitch];
					}
				}
			}
		}
		SDL_UnlockSurface(surface);
	}
}

/**
**  Make shadow sprite
**
**  @todo FIXME: 32bpp
*/
void CGraphic::MakeShadow(PixelPos offset)
{
	VideoPaletteListRemove(mSurface);
	applyAlphaGrayscaleToSurface(&mSurface, 80);
	if (SurfaceFlip) {
		VideoPaletteListRemove(SurfaceFlip);
		applyAlphaGrayscaleToSurface(&SurfaceFlip, 80);
	}

	const int xOffset = offset.x;
	// BEGIN HACK: XXX: FIXME: positive yOffset is used for fliers for now, these should not get shearing.
	// We need to find a better way to communicate that. The rest of the code already supports shearing
	// in both directions for y.
	const int yOffset = std::min(0, offset.y);
	// END HACK

	// Apply shearing effect. same angle for Surface and SurfaceFlip!
	// The sun shines from the same angle on to both normal and flipped sprites :)

	// 1. Shrink each frame in y-direction based on the x offset
	shrinkSurfaceFramesInY(&mSurface, xOffset, frame_map, Width, Height);
	if (SurfaceFlip) {
		shrinkSurfaceFramesInY(&SurfaceFlip, xOffset, frameFlip_map, Width, Height);
	}

	// 2. Apply shearing
	shearSurface(mSurface, xOffset, yOffset, frame_map, Width, Height);
	if (SurfaceFlip) {
		shearSurface(SurfaceFlip, xOffset, yOffset, frameFlip_map, Width, Height);
	}
}

void FreeGraphics()
{
	GraphicHash.clear();
}

// Debug bridge: expose the (file-scope) loaded-graphic registry read-only, so the
// `assets` command can enumerate loaded sprites without touching GraphicHash directly.
void DebugBridge_CollectGraphics(std::vector<DebugGraphicInfo> &out)
{
	out.reserve(GraphicHash.size());
	for (const auto &kv : GraphicHash) {
		const std::shared_ptr<CGraphic> &g = kv.second;
		if (!g) {
			continue;
		}
		DebugGraphicInfo info;
		info.file = g->File.string();
		info.width = g->Width;
		info.height = g->Height;
		info.numFrames = g->NumFrames;
		out.push_back(std::move(info));
	}
}

CFiller::bits_map::~bits_map()
{
	if (bstore) {
		free(bstore);
		bstore = nullptr;
	}
	Width = 0;
	Height = 0;
}

void CFiller::bits_map::Init(CGraphic *g)
{
	SDL_Surface *s = g->getSurface();
	int bpp = s->format->BytesPerPixel;
	unsigned int ckey;

	if (bstore) {
		free(bstore);
		bstore = nullptr;
		Width = 0;
		Height = 0;
	}

	if ((bpp == 1 && SDL_GetColorKey(s, &ckey) != 0) || bpp == 3) {
		return;
	}

	Width = g->Width;
	Height = g->Height;

	size_t line = (Width + (sizeof(int) * 8) - 1) / (sizeof(int) * 8);
	size_t size = line * Height;

	bstore = (unsigned int *)calloc(size, sizeof(unsigned int));

	SDL_LockSurface(s);

	switch (s->format->BytesPerPixel) {
		case 1: {
			unsigned char *ptr = (unsigned char *)s->pixels;

			for (int i = 0; i < Height; ++i) {
				int l = i * Width;
				int lm = i * line;
				int k = 0;
				int p = 0;
				for (int j = 0; j < Width; ++j) {
					bstore[lm + k] |= ((ptr[j + l] != ckey) ? (1 << p) : 0);
					if (++p > 31) {
						p = 0;
						k++;
					}
				}
			}
			break;
		}
		case 2:
		case 3:
			break;
		case 4:
		{
			if (!SDL_GetColorKey(s, &ckey)) {
				unsigned int *ptr = (unsigned int *)s->pixels;

				for (int i = 0; i < Height; ++i) {
					int l = i * Width;
					int lm = i * line;
					int k = 0;
					int p = 0;
					for (int j = 0; j < Width; ++j) {
						bstore[lm + k] |= ((ptr[j + l] != ckey) ? (1 << p) : 0);
						if (++p > 31) {
							p = 0;
							k++;
						}
					}
				}
			} else {
				unsigned int *ptr = (unsigned int *)s->pixels;

				for (int i = 0; i < Height; ++i) {
					int l = i * Width;
					int lm = i * line;
					int k = 0;
					int p = 0;
					for (int j = 0; j < Width; ++j) {
						bstore[lm + k] |= ((ptr[j + l] & AMASK) ? (1 << p) : 0);
						if (++p > 31) {
							p = 0;
							k++;
						}
					}
				}
			}
			break;
		}
		default:
			break;
	}

	SDL_UnlockSurface(s);
}

void CFiller::Load()
{
	if (G) {
		G->Load();
		map.Init(G.get());
	}
}

//@}
