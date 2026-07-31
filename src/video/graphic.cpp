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

#include "intern_video.h"
#include "iolib.h"
#include "player.h"
#include "stratagus.h"
#include "ui.h"

#include <SDL_image.h>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>

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

// GPU-pipeline (Phase 1). See video.h. 1.0 => the classic native (Zoom==1) draw; set to the real
// viewport zoom around the in-game world GPU render so tile/unit destination rects scale up.
float GpuWorldDrawZoom = 1.0f;

// GPU-pipeline (Phase 5). See video.h. 1.0 => world renders straight to the backbuffer (no change).
// Overwritten once at video init from <internal>/world_scale.txt on Android; clamped to [0.25, 1.0].
float WorldRenderScale = 1.0f;

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
	SDL_Rect srect = {Sint16(frame_map[frame].x), Sint16(frame_map[frame].y),
					  Uint16(Width), Uint16(Height)};
	SDL_Rect drect = {Sint16(x), Sint16(y), Uint16(w), Uint16(h)};
	SDL_BlitScaled(mSurface, &srect, surface, &drect);
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
	if (GpuWorldDrawActive && mTexture && surface == TheScreen) {
		DrawPlayerColorFrameClipTex(colorIndex, frame, x, y, false);
		return;
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
	if (GpuWorldDrawActive && mTexture && surface == TheScreen
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
			Uint32 *p;
			for (int i = 0; i < Height; ++i) {
				for (int j = 0; j < Width; ++j) {
					p = static_cast<Uint32 *>(Surface->pixels) + (i * Surface->w + j);
					const Uint32 gray = ((Uint8)((*p) * redGray) >> f->Rshift) +
										((Uint8)(*(p + 1) * greenGray) >> f->Gshift) +
										((Uint8)(*(p + 2) * blueGray) >> f->Bshift) +
										((Uint8)(*(p + 3)) >> f->Ashift);
					*p = gray;
				}
			}
			break;
		}
	}
	SDL_UnlockSurface(Surface);
}

/**
**  Load a graphic
**
**  @param grayscale  Make a grayscale surface
*/
void CGraphic::Load(bool grayscale)
{
	if (mSurface) {
		return;
	}

	auto fp = std::make_unique<CFile>();
	const fs::path name = LibraryFileName(File.string());
	if (name.empty()) {
		perror("Cannot find file");
		ErrorPrint("Can't load the graphic '%s'\n", File.u8string().c_str());
		ExitFatal(-1);
	}
	if (fp->open(name.string().c_str(), CL_OPEN_READ) == -1) {
		perror("Can't open file");
		ErrorPrint("Can't load the graphic '%s'\n", File.u8string().c_str());
		ExitFatal(-1);
	}
	mSurface = IMG_Load_RW(CFile::to_SDL_RWops(std::move(fp)), 1);
	if (mSurface == nullptr) {
		ErrorPrint("Couldn't load file '%s': %s", name.u8string().c_str(), IMG_GetError());
		ErrorPrint("Can't load the graphic '%s'\n", File.u8string().c_str());
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
		ErrorPrint("Invalid graphic (width, height) '%s'\n"
		           "Expected: (%d,%d)  Found: (%d,%d)\n",
		           File.u8string().c_str(),
		           Width,
		           Height,
		           GetGraphicWidth(),
		           GetGraphicHeight());
		ExitFatal(-1);
	}

	NumFrames = GetGraphicWidth() / Width * GetGraphicHeight() / Height;

	if (grayscale) {
		ApplyGrayScale(mSurface, Width, Height);
	}

	GenFramesMap();

	// GPU-pipeline (Phase 1): upload an RGBA copy once so tiles/units can be RenderCopy'd off the
	// CPU compositor. mSurface is intentionally NOT freed here: CPU-read consumers (screenshots,
	// the debug colour bridge) and the non-GPU / zoomed CPU draw paths still need it. Paletted
	// graphics return nullptr and stay fully on the CPU path.
	if (!mTexture) {
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
	this->Load();

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
#include "debug_bridge.h"
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
