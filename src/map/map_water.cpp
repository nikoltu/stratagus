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
/**@name map_water.cpp - The HD animated water layer (drawing only). */
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

// Owner decision 87. Spec: .wargushd/work/WATER_ANIM.md (design work/CALIB_RUN55.md "Item 5 design";
// look: "Look recalibration (owner feedback)"). The rates and the look statistics are measured
// (water_anim.h); the combine is ours (the original's is UNKNOWN):
//   target O = open-water weight of each visible water megatile x twinkle layer (a hash per texel,
//              nearest filter, each copy mirrored by its place) x crest map (green; 0 between the
//              crests, section 12) x gate layer
//              (red phase map, ranked even), each map tiled at its own period;
//   target S = shore weight x shore layer (blue phase map ranked even, 4.8 s loop);
//   both added over the drawn terrain in the tileset's water tint, before units and fog.
// Nothing is ever shifted: each step only the small CPU layers are rebuilt from fixed phase maps
// (a pixel lights when its phase meets the step's offset), so pixels light and fade in place. An
// unlit twinkle texel is exactly 0 and the products keep it 0, so open water outside the twinkling
// spots is the same, byte for byte, from step to step (WATER_ANIM.md section 11).
// The targets are rebuilt only when the step, the view or the visible tiles change; every other
// frame they are only added again.

#include "stratagus.h"

#include "iolib.h"
#include "unit.h"
#include "video.h"
#include "water_anim.h"
#include "../video/hd_classic_hud.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace
{

struct WaterState {
	std::shared_ptr<CAtlasGraphic> openMasks;   // bgs_water_<X>, grey = open-water weight
	std::shared_ptr<CAtlasGraphic> shoreMasks;  // the same frames, grey = shore weight
	std::vector<int> frameForTile;              // megatile -> frame of both atlases, or -1
	std::vector<uint8_t> phase;                 // waterMasks.png blue ranked even, LayerSize^2 (fixed)
	std::vector<uint8_t> gatePhase;             // waterMasks.png red ranked even, GateSize^2 (fixed)
	std::vector<uint8_t> twinkle;               // TwinkleHash per texel, TwinkleSize^2 (fixed)
	std::vector<uint8_t> layer;                 // scratch, LayerSize^2
	std::vector<uint32_t> texels;               // scratch, LayerSize^2
	SDL_Texture *pulseTex = nullptr;            // open twinkle layer of the step (streaming, nearest)
	SDL_Texture *crestTex = nullptr;            // crest map (static)
	SDL_Texture *gateTex = nullptr;             // gate layer of the step (streaming)
	SDL_Texture *shoreTex = nullptr;            // shore layer of the step (streaming)
	water_anim::Tint tint;
	uint32_t gain = 256;
	uint32_t layerStep = 0;
	bool layerValid = false;
	SDL_Texture *openTarget = nullptr;
	SDL_Texture *shoreTarget = nullptr;
	int targetW = 0;
	int targetH = 0;
	uint64_t builtKey = 0;
	bool built = false;
	bool shown = false;                         // the layer was added in the last map draw
	// PERF-A3a held water (HDWaterSetProbe, HDWaterAddHeldGreys; WATER_ANIM.md "Frame-time check").
	HDWaterProbe probe = HDWaterProbe::None;    // what the layer draws in the running map draw
	int draws = 0;                              // layer calls since HDWaterNewFrame
	bool onBackbuffer = false;                  // the last call drew straight into the backbuffer
	bool heldValid = false;                     // the view below was kept by a probe
	std::vector<HDWaterTile> heldTiles;
	SDL_Rect heldArea = {0, 0, 0, 0};
	float heldOriginX = 0.f;
	float heldOriginY = 0.f;
	float heldTilePx = 0.f;
};

WaterState Water;

void DestroyTexture(SDL_Texture *&tex)
{
	if (tex) {
		SDL_DestroyTexture(tex);
		tex = nullptr;
	}
}

/// A copy of an RGBA32 page whose grey is one mask weight (opaque).
SDL_Surface *WeightPage(const SDL_Surface *page, bool shore)
{
	SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, page->w, page->h, 32, SDL_PIXELFORMAT_RGBA32);
	if (!out) {
		return nullptr;
	}
	for (int y = 0; y < page->h; ++y) {
		const Uint8 *src = static_cast<const Uint8 *>(page->pixels) + y * page->pitch;
		Uint8 *dst = static_cast<Uint8 *>(out->pixels) + y * out->pitch;
		for (int x = 0; x < page->w; ++x, src += 4, dst += 4) {
			const Uint8 w = shore ? water_anim::ShoreWeight(src[2], src[3])
			                      : water_anim::OpenWeight(src[2], src[3]);
			dst[0] = dst[1] = dst[2] = w;
			dst[3] = 255;
		}
	}
	return out;
}

/// One channel of the RGBA32 noise page averaged down to size x size (the page is NoiseSize square).
std::vector<uint8_t> NoiseChannel(const SDL_Surface *noise, int channel, int size, bool crest)
{
	std::vector<uint8_t> out(size_t(size) * size, 0);
	const int fx = std::max(1, noise->w / size);
	const int fy = std::max(1, noise->h / size);
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			int sum = 0;
			int n = 0;
			for (int j = 0; j < fy; ++j) {
				const int sy = std::min(noise->h - 1, y * fy + j);
				const Uint8 *row = static_cast<const Uint8 *>(noise->pixels) + sy * noise->pitch;
				for (int i = 0; i < fx; ++i) {
					const int sx = std::min(noise->w - 1, x * fx + i);
					const Uint8 v = row[sx * 4 + channel];
					sum += crest ? water_anim::CrestTexel(v) : v;
					++n;
				}
			}
			out[size_t(y) * size + x] = static_cast<uint8_t>((sum + n / 2) / n);
		}
	}
	return out;
}

/// A grey texture for the multiply pass (RGBA32, MOD; linear unless `nearest`).
SDL_Texture *GreyTexture(int size, bool streaming, bool nearest = false)
{
	SDL_Texture *tex = SDL_CreateTexture(TheRenderer, SDL_PIXELFORMAT_RGBA32,
	                                     streaming ? SDL_TEXTUREACCESS_STREAMING : SDL_TEXTUREACCESS_STATIC,
	                                     size, size);
	if (tex) {
		SDL_SetTextureScaleMode(tex, nearest ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_MOD);
	}
	return tex;
}

/// Upload size x size grey values (opaque).
void UploadGrey(SDL_Texture *tex, const uint8_t *grey, int size)
{
	const size_t n = size_t(size) * size;
	Water.texels.resize(std::max(Water.texels.size(), n));
	for (size_t i = 0; i < n; ++i) {
		const uint32_t g = grey[i];
		Uint8 *px = reinterpret_cast<Uint8 *>(&Water.texels[i]);
		px[0] = px[1] = px[2] = static_cast<Uint8>(g);
		px[3] = 255;
	}
	SDL_UpdateTexture(tex, nullptr, Water.texels.data(), size * 4);
}

/// The step's layers (pulse, gate, shore) into their textures; once per step.
void UpdateLayers(uint32_t step)
{
	if (Water.layerValid && Water.layerStep == step) {
		return;
	}
	const size_t n = size_t(water_anim::LayerSize) * water_anim::LayerSize;
	const size_t ng = size_t(water_anim::GateSize) * water_anim::GateSize;
	const size_t nt = size_t(water_anim::TwinkleSize) * water_anim::TwinkleSize;
	Water.layer.resize(std::max(n, nt));
	water_anim::BuildOpenLayer(Water.twinkle.data(), nt, step, Water.layer.data());
	UploadGrey(Water.pulseTex, Water.layer.data(), water_anim::TwinkleSize);
	water_anim::BuildShoreLayer(Water.phase.data(), n, step, Water.gain, Water.layer.data());
	UploadGrey(Water.shoreTex, Water.layer.data(), water_anim::LayerSize);
	water_anim::BuildGateLayer(Water.gatePhase.data(), ng, step, Water.layer.data());
	UploadGrey(Water.gateTex, Water.layer.data(), water_anim::GateSize);
	Water.layerStep = step;
	Water.layerValid = true;
}

uint64_t Mix(uint64_t h, int64_t v)
{
	h ^= static_cast<uint64_t>(v) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
	return h;
}

/// Draw `tex` tiled over the whole current target (tw x th), map pixel 0,0 of its grid at (ox, oy),
/// one square `period` target pixels wide. Never moved: the grid is fixed to the map. `mirror`:
/// each copy is mirrored by its place on the map grid (water_anim::TwinkleFlip), so the copies do
/// not repeat every square; the rects are the same, so the copies still meet without a gap.
void TileMap(SDL_Texture *tex, int tw, int th, float ox, float oy, float period, bool mirror)
{
	if (period < 1.0f) {
		return;
	}
	float x0 = std::fmod(ox, period);
	float y0 = std::fmod(oy, period);
	if (x0 > 0.0f) {
		x0 -= period;
	}
	if (y0 > 0.0f) {
		y0 -= period;
	}
	for (float y = y0; y < th; y += period) {
		const int ya = static_cast<int>(std::lround(y));
		const int yb = static_cast<int>(std::lround(y + period));
		for (float x = x0; x < tw; x += period) {
			const int xa = static_cast<int>(std::lround(x));
			const int xb = static_cast<int>(std::lround(x + period));
			const SDL_Rect dst = {xa, ya, xb - xa, yb - ya};
			if (!mirror) {
				SDL_RenderCopy(TheRenderer, tex, nullptr, &dst);
				continue;
			}
			const int f = water_anim::TwinkleFlip(std::lround((x - ox) / period), std::lround((y - oy) / period));
			const int flip = ((f & 1) ? SDL_FLIP_HORIZONTAL : 0) | ((f & 2) ? SDL_FLIP_VERTICAL : 0);
			SDL_RenderCopyEx(TheRenderer, tex, nullptr, &dst, 0.0, nullptr, static_cast<SDL_RendererFlip>(flip));
		}
	}
}

struct TiledMap {
	SDL_Texture *tex;
	float period;
	bool mirror;
};

/// Fill one target: the masks of the visible water tiles, times each tiled map.
void BuildTarget(SDL_Texture *target, const CAtlasGraphic &masks, std::initializer_list<TiledMap> maps,
                 const std::vector<HDWaterTile> &tiles, const SDL_Rect &area, float s, float ox, float oy)
{
	SDL_SetRenderTarget(TheRenderer, target);
	SDL_SetRenderDrawColor(TheRenderer, 0, 0, 0, 0);
	SDL_RenderClear(TheRenderer);
	for (const HDWaterTile &t : tiles) {
		const int x0 = static_cast<int>(std::lround((t.dst.x - area.x) * s));
		const int y0 = static_cast<int>(std::lround((t.dst.y - area.y) * s));
		const int x1 = static_cast<int>(std::lround((t.dst.x + t.dst.w - area.x) * s));
		const int y1 = static_cast<int>(std::lround((t.dst.y + t.dst.h - area.y) * s));
		const SDL_Rect dst = {x0, y0, x1 - x0, y1 - y0};
		masks.DrawAtlasFrameFull(t.frame, dst);
	}
	for (const TiledMap &m : maps) {
		TileMap(m.tex, Water.targetW, Water.targetH, ox, oy, m.period, m.mirror);
	}
}

bool BuildLayerTargets(const std::vector<HDWaterTile> &tiles, const SDL_Rect &area, float originX,
                       float originY, float tilePx, bool clipOn, const SDL_Rect &clip);

} // namespace

void ReleaseHDWater()
{
	Water.openMasks = nullptr;
	Water.shoreMasks = nullptr;
	Water.frameForTile.clear();
	Water.phase.clear();
	Water.gatePhase.clear();
	Water.twinkle.clear();
	Water.layer.clear();
	Water.texels.clear();
	Water.layer.shrink_to_fit();
	Water.texels.shrink_to_fit();
	DestroyTexture(Water.pulseTex);
	DestroyTexture(Water.crestTex);
	DestroyTexture(Water.gateTex);
	DestroyTexture(Water.shoreTex);
	DestroyTexture(Water.openTarget);
	DestroyTexture(Water.shoreTarget);
	Water.layerValid = false;
	Water.targetW = Water.targetH = 0;
	Water.built = false;
	Water.shown = false;
	Water.heldValid = false;
	Water.heldTiles.clear();
	Water.heldTiles.shrink_to_fit();
	Water.draws = 0;
}

void HDWaterNewFrame()
{
	Water.shown = false;
	Water.draws = 0;
}

void HDWaterSetProbe(HDWaterProbe probe)
{
	Water.probe = probe;
}

bool HDWaterHeldCapable()
{
	return Water.draws == 1 && Water.onBackbuffer && HDWaterLoaded();
}

bool HDWaterShown()
{
	return Water.shown;
}

bool HDWaterLoaded()
{
	return Water.openMasks && Water.shoreMasks && Water.pulseTex && Water.crestTex && Water.gateTex
		&& Water.shoreTex;
}

int HDWaterFrame(unsigned megatile)
{
	return megatile < Water.frameForTile.size() ? Water.frameForTile[megatile] : -1;
}

void LoadHDWater(const char *bgsName, const char *framePrefix, int megatileCount)
{
	ReleaseHDWater();
	if (!TheRenderer || !bgsName || !framePrefix) {
		return;
	}
	const Uint32 start = SDL_GetTicks();
	const std::string json = std::string("Data/Art/hd/bgs/bgs_water_") + bgsName + ".json";
	const fs::path jsonFile = LibraryFileName(json);
	const fs::path noiseFile = LibraryFileName("Data/Art/hd/bgs/waterMasks.png");
	std::error_code ec;
	if (jsonFile.empty() || noiseFile.empty() || !fs::exists(jsonFile, ec) || !fs::exists(noiseFile, ec)) {
		return; // a file is missing: the water stays static
	}
	fs::path pagePath = jsonFile;
	pagePath.replace_extension(".png");
	AtlasPageDecode page = DecodeAtlasPage(pagePath, false, false, false);
	AtlasPageDecode noise = DecodeAtlasPage(noiseFile, false, false, false);
	const auto freeBoth = [&]() {
		if (page.surface) { SDL_FreeSurface(page.surface); page.surface = nullptr; }
		if (noise.surface) { SDL_FreeSurface(noise.surface); noise.surface = nullptr; }
	};
	if (!page.surface || !noise.surface || page.surface->format->format != SDL_PIXELFORMAT_RGBA32
		|| noise.surface->format->format != SDL_PIXELFORMAT_RGBA32) {
		freeBoth();
		ErrorPrint("Animated water: %s or waterMasks.png not readable, the water stays static\n", json.c_str());
		return;
	}
	// The two weights are cut from the one page; LoadTexturePacker takes each copy over. fogMask =
	// true only asks for the linear filter of the pages (the page is decoded here already).
	AtlasPageDecode openPage;
	openPage.surface = WeightPage(page.surface, false);
	AtlasPageDecode shorePage;
	shorePage.surface = WeightPage(page.surface, true);
	auto openMasks = std::make_shared<CAtlasGraphic>();
	auto shoreMasks = std::make_shared<CAtlasGraphic>();
	// LoadTexturePacker frees a page it is given on every way out, so each made page is given.
	const bool openOk = openPage.surface
		&& openMasks->LoadTexturePacker(json, false, true, nullptr, false, nullptr, nullptr, nullptr, &openPage);
	const bool shoreOk = shorePage.surface
		&& shoreMasks->LoadTexturePacker(json, false, true, nullptr, false, nullptr, nullptr, nullptr, &shorePage);
	const bool masksOk = openOk && shoreOk && openMasks->NumFrames() == shoreMasks->NumFrames();
	if (!masksOk) {
		freeBoth();
		ErrorPrint("Animated water: %s did not load, the water stays static\n", json.c_str());
		return;
	}
	// waterMasks.png as fixed per-pixel maps (water_anim.h "Look"): blue = shore phase, green =
	// crests, red = gate phase; both phases ranked to an even spread (water_anim::Equalize), so the
	// share lit is the same on every step. The twinkle map is our own hash per texel. Never
	// shifted; only the small layers are rebuilt per step.
	Water.phase = NoiseChannel(noise.surface, 2, water_anim::LayerSize, false);
	water_anim::Equalize(Water.phase.data(), Water.phase.size());
	const std::vector<uint8_t> crest = NoiseChannel(noise.surface, 1, water_anim::LayerSize, true);
	Water.gatePhase = NoiseChannel(noise.surface, 0, water_anim::GateSize, false);
	water_anim::Equalize(Water.gatePhase.data(), Water.gatePhase.size());
	freeBoth();
	Water.twinkle.resize(size_t(water_anim::TwinkleSize) * water_anim::TwinkleSize);
	for (int y = 0; y < water_anim::TwinkleSize; ++y) {
		for (int x = 0; x < water_anim::TwinkleSize; ++x) {
			Water.twinkle[size_t(y) * water_anim::TwinkleSize + x] = water_anim::TwinkleHash(x, y);
		}
	}
	Water.pulseTex = GreyTexture(water_anim::TwinkleSize, true, true);
	Water.crestTex = GreyTexture(water_anim::LayerSize, false);
	Water.gateTex = GreyTexture(water_anim::GateSize, true);
	Water.shoreTex = GreyTexture(water_anim::LayerSize, true);
	if (!Water.pulseTex || !Water.crestTex || !Water.gateTex || !Water.shoreTex) {
		ReleaseHDWater();
		return;
	}
	UploadGrey(Water.crestTex, crest.data(), water_anim::LayerSize);
	Water.tint = water_anim::TintFor(bgsName);
	Water.gain = water_anim::TintGain(Water.tint);
	Water.layerValid = false;
	Water.openMasks = std::move(openMasks);
	Water.shoreMasks = std::move(shoreMasks);
	// The same name rule as the terrain frames (map_draw.cpp EnsureHDTileAtlas): megatile M has the
	// water mask "<prefix>_<M-16>_water" (RAW: forest_190_water .. forest_317_water).
	Water.frameForTile.assign(std::max(megatileCount, 16), -1);
	int found = 0;
	std::string name;
	for (int m = 16; m < (int) Water.frameForTile.size(); ++m) {
		name = framePrefix;
		name += '_';
		name += std::to_string(m - 16);
		name += "_water";
		Water.frameForTile[m] = Water.openMasks->FrameByName(name.c_str());
		found += Water.frameForTile[m] >= 0;
	}
	ErrorPrint("Animated water: %s, %d of %d masks used, %u ms\n", json.c_str(), found,
	           Water.openMasks->NumFrames(), (unsigned) (SDL_GetTicks() - start));
}

void DrawHDWaterLayer(const std::vector<HDWaterTile> &tiles, float originX, float originY, float tilePx)
{
	if (!Preference.AnimatedWater || tiles.empty() || !TheRenderer || !HDWaterLoaded()) {
		return;
	}
	water_anim::Game().Advance(SDL_GetTicks());

	// The area the layer covers: the renderer clip (the viewport), else the tiles' bounds.
	SDL_Rect clip = {0, 0, 0, 0};
	const bool clipOn = SDL_RenderIsClipEnabled(TheRenderer) == SDL_TRUE;
	SDL_RenderGetClipRect(TheRenderer, &clip);
	SDL_Rect area = clip;
	if (!clipOn || area.w <= 0 || area.h <= 0) {
		int x0 = tiles[0].dst.x, y0 = tiles[0].dst.y;
		int x1 = x0 + tiles[0].dst.w, y1 = y0 + tiles[0].dst.h;
		for (const HDWaterTile &t : tiles) {
			x0 = std::min(x0, t.dst.x);
			y0 = std::min(y0, t.dst.y);
			x1 = std::max(x1, t.dst.x + t.dst.w);
			y1 = std::max(y1, t.dst.y + t.dst.h);
		}
		area = {x0, y0, x1 - x0, y1 - y0};
	}
	if (area.w <= 0 || area.h <= 0) {
		return;
	}
	++Water.draws;
	Water.onBackbuffer = SDL_GetRenderTarget(TheRenderer) == nullptr;
	if (Water.probe != HDWaterProbe::None) {
		// PERF-A3a held water: the view is kept for HDWaterAddHeldGreys; Black / White fill the
		// layer's area so that sdl.cpp can measure how much of it shows through what is drawn over it.
		Water.heldTiles = tiles;
		Water.heldArea = area;
		Water.heldOriginX = originX;
		Water.heldOriginY = originY;
		Water.heldTilePx = tilePx;
		Water.heldValid = true;
		if (Water.probe == HDWaterProbe::Black || Water.probe == HDWaterProbe::White) {
			const Uint8 v = Water.probe == HDWaterProbe::White ? 255 : 0;
			Uint8 cr, cg, cb, ca;
			SDL_BlendMode bm;
			SDL_GetRenderDrawColor(TheRenderer, &cr, &cg, &cb, &ca);
			SDL_GetRenderDrawBlendMode(TheRenderer, &bm);
			SDL_SetRenderDrawBlendMode(TheRenderer, SDL_BLENDMODE_NONE);
			SDL_SetRenderDrawColor(TheRenderer, v, v, v, 255);
			SDL_RenderFillRect(TheRenderer, &area);
			SDL_SetRenderDrawBlendMode(TheRenderer, bm);
			SDL_SetRenderDrawColor(TheRenderer, cr, cg, cb, ca);
		}
		Water.shown = true;
		return;
	}
	Water.heldValid = false;
	if (!BuildLayerTargets(tiles, area, originX, originY, tilePx, clipOn, clip)) {
		return;
	}
	const SDL_Rect used = {0, 0, Water.targetW, Water.targetH};
	// The layers hold luminance levels; added in the tileset's water tint (water_anim::TintFor).
	for (SDL_Texture *t : {Water.openTarget, Water.shoreTarget}) {
		SDL_SetTextureAlphaMod(t, 255);
		SDL_SetTextureColorMod(t, Water.tint.r, Water.tint.g, Water.tint.b);
		SDL_RenderCopy(TheRenderer, t, &used, &area);
	}
	Water.shown = true;
}

bool HDWaterAddHeldGreys(float sx, float sy)
{
	if (!Water.heldValid || Water.heldTiles.empty() || !TheRenderer || !HDWaterLoaded()) {
		return false;
	}
	water_anim::Game().Advance(SDL_GetTicks());
	const SDL_Rect clip = {0, 0, 0, 0};
	if (!BuildLayerTargets(Water.heldTiles, Water.heldArea, Water.heldOriginX, Water.heldOriginY,
	                       Water.heldTilePx, false, clip)) {
		return false;
	}
	// The current target is a texture the size of the backbuffer in pixels (scale 1): the kept
	// area, in the renderer's logical units, is scaled to it.
	const SDL_Rect &a = Water.heldArea;
	const int x0 = static_cast<int>(std::lround(a.x * sx));
	const int y0 = static_cast<int>(std::lround(a.y * sy));
	const SDL_Rect dst = {x0, y0, static_cast<int>(std::lround((a.x + a.w) * sx)) - x0,
	                      static_cast<int>(std::lround((a.y + a.h) * sy)) - y0};
	const SDL_Rect used = {0, 0, Water.targetW, Water.targetH};
	// Greys are linear: the grey of the tinted layer is the layer times the grey of the tint.
	const Uint8 g = static_cast<Uint8>(classichud::greyOf(Water.tint.r, Water.tint.g, Water.tint.b));
	for (SDL_Texture *t : {Water.openTarget, Water.shoreTarget}) {
		SDL_SetTextureAlphaMod(t, 255);
		SDL_SetTextureColorMod(t, g, g, g);
		SDL_RenderCopy(TheRenderer, t, &used, &dst);
	}
	return true;
}

namespace
{

/// The two layer targets for `area`, (re)built when the step, the view or the tiles changed. The
/// render target, its clip (clipOn / clip) and the draw colour are put back.
bool BuildLayerTargets(const std::vector<HDWaterTile> &tiles, const SDL_Rect &area, float originX,
                       float originY, float tilePx, bool clipOn, const SDL_Rect &clip)
{
	water_anim::Clock &clock = water_anim::Game();
	const float s = water_anim::TargetScale;
	const int tw = std::max(1, static_cast<int>(std::ceil(area.w * s)));
	const int th = std::max(1, static_cast<int>(std::ceil(area.h * s)));
	if (!Water.openTarget || Water.targetW != tw || Water.targetH != th) {
		DestroyTexture(Water.openTarget);
		DestroyTexture(Water.shoreTarget);
		Water.targetW = Water.targetH = 0;
		Water.built = false;
		if (SDL_RenderTargetSupported(TheRenderer) != SDL_TRUE) {
			return false;
		}
		Water.openTarget = SDL_CreateTexture(TheRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
		Water.shoreTarget = SDL_CreateTexture(TheRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
		if (!Water.openTarget || !Water.shoreTarget) {
			DestroyTexture(Water.openTarget);
			DestroyTexture(Water.shoreTarget);
			return false;
		}
		for (SDL_Texture *t : {Water.openTarget, Water.shoreTarget}) {
			SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
			SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
		}
		Water.targetW = tw;
		Water.targetH = th;
	}

	// Rebuild only when the step, the view or the visible water tiles changed.
	uint64_t key = 1469598103934665603ull;
	key = Mix(key, clock.Step());
	key = Mix(key, area.x); key = Mix(key, area.y); key = Mix(key, area.w); key = Mix(key, area.h);
	key = Mix(key, std::lround(originX * 16)); key = Mix(key, std::lround(originY * 16));
	key = Mix(key, std::lround(tilePx * 16));
	for (const HDWaterTile &t : tiles) {
		key = Mix(key, t.frame);
		key = Mix(key, (int64_t(t.dst.x) << 32) ^ (int64_t(t.dst.y) << 16) ^ t.dst.w);
	}
	if (!Water.built || key != Water.builtKey) {
		UpdateLayers(clock.Step());
		SDL_Texture *prevTarget = SDL_GetRenderTarget(TheRenderer);
		Uint8 cr, cg, cb, ca;
		SDL_GetRenderDrawColor(TheRenderer, &cr, &cg, &cb, &ca);
		const float ox = (originX - area.x) * s;
		const float oy = (originY - area.y) * s;
		// One noise square spans NoiseSize native HD pixels; a megatile is HdNativeTilePx of them.
		const float period = tilePx * s * water_anim::NoiseSize / HdNativeTilePx;
		BuildTarget(Water.openTarget, *Water.openMasks,
		            {{Water.pulseTex, period, true}, {Water.crestTex, period * water_anim::CrestPeriod, false},
		             {Water.gateTex, period * water_anim::GatePeriod, false}},
		            tiles, area, s, ox, oy);
		BuildTarget(Water.shoreTarget, *Water.shoreMasks, {{Water.shoreTex, period, false}}, tiles, area, s, ox,
		            oy);
		// Back to the target the world draws into. A texture target does not keep its clip over a
		// switch, so the clip is set again.
		SDL_SetRenderTarget(TheRenderer, prevTarget);
		SDL_RenderSetClipRect(TheRenderer, clipOn ? &clip : nullptr);
		SDL_SetRenderDrawColor(TheRenderer, cr, cg, cb, ca);
		Water.builtKey = key;
		Water.built = true;
	}
	return true;
}

} // namespace
