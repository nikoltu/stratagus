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
/**@name minimap.cpp - The minimap. */
//
//      (c) Copyright 1998-2011 by Lutz Sammer and Jimmy Salmon and Pali Rohár
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

#include "stratagus.h"

#include "minimap.h"

#include "editor.h"
#include "iolib.h"
#include "map.h"
#include "player.h"
#include "settings.h"
#include "unit.h"
#include "unit_manager.h"
#include "ui.h"
#include "unittype.h"
#include "video.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

/*----------------------------------------------------------------------------
--  Defines
----------------------------------------------------------------------------*/

/// integer scale factor
static constexpr int MINIMAP_FAC           {16 * 3};
/// unit attacked are shown red for at least this amount of cycles
static constexpr int ATTACK_RED_DURATION   {1 * CYCLES_PER_SECOND};
/// unit attacked are shown blinking for this amount of cycles
static constexpr int ATTACK_BLINK_DURATION {7 * CYCLES_PER_SECOND};

static constexpr int SCALE_PRECISION       {100};

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

SDL_Surface        *MinimapSurface{nullptr};        /// generated minimap
static SDL_Texture *MinimapTexture{nullptr};        /// GPU-HUD: cached upload of MinimapSurface
static int          MinimapTextureW{0};
static int          MinimapTextureH{0};
static SDL_Surface *MinimapTerrainSurface{nullptr}; /// generated minimap terrain
static SDL_Surface *MinimapFogSurface{nullptr};     /// generated minimap fog of war
// Classic view: the minimap is a picture of logical pixels (128 x 128), every one drawn as
// step x step screen pixels (hdatlas::classicStep). The surfaces, the tables, the scale and the
// offsets are in surface pixels; W / H / X / Y stay the place on the screen. 1 outside the
// classic view: surface and screen pixels are the same.
static int          MinimapStep{1};
static int          SurfaceW{0};
static int          SurfaceH{0};

static std::vector<int> Minimap2MapX;      /// fast conversion table
static std::vector<int> Minimap2MapY;      /// fast conversion table
static int Map2MinimapX[MaxMapWidth];      /// fast conversion table
static int Map2MinimapY[MaxMapHeight];     /// fast conversion table

#define MAX_MINIMAP_EVENTS 8

struct MinimapEvent {
	PixelPos pos;
	int Size;
	Uint32 Color;
} MinimapEvents[MAX_MINIMAP_EVENTS];
int NumMinimapEvents;


/*----------------------------------------------------------------------------
-- Functions
----------------------------------------------------------------------------*/


/**
**  Create a mini-map from the tiles of the map.
**
**  @todo Scaling and scrolling the minmap is currently not supported.
*/
void CMinimap::Create()
{
	MinimapStep = ClassicHudActive() ? hdatlas::classicStep(Video.Height) : 1;
	if (W % MinimapStep != 0 || H % MinimapStep != 0) {
		MinimapStep = 1;
	}
	SurfaceW = W / MinimapStep;
	SurfaceH = H / MinimapStep;

	// Scale to biggest value.
	const int n = std::max({Map.Info.MapWidth, Map.Info.MapHeight, 32});

	// One scale for both axes: the picture keeps the map's shape and is centred in the box.
	MinimapScaleX = (std::min(SurfaceW, SurfaceH) * MINIMAP_FAC + n - 1) / n;
	MinimapScaleY = MinimapScaleX;

	XOffset = (SurfaceW - (Map.Info.MapWidth * MinimapScaleX) / MINIMAP_FAC + 1) / 2;
	YOffset = (SurfaceH - (Map.Info.MapHeight * MinimapScaleY) / MINIMAP_FAC + 1) / 2;

	DebugPrint("MinimapScale %d %d (%d %d), X off %d, Y off %d\n",
	           MinimapScaleX / MINIMAP_FAC,
	           MinimapScaleY / MINIMAP_FAC,
	           MinimapScaleX,
	           MinimapScaleY,
	           XOffset,
	           YOffset);

	//
	// Calculate minimap fast lookup tables.
	//
	Minimap2MapX.resize(SurfaceW * SurfaceH);
	Minimap2MapY.resize(SurfaceW * SurfaceH);
	for (int i = XOffset; i < SurfaceW - XOffset; ++i) {
		Minimap2MapX[i] = ((i - XOffset) * MINIMAP_FAC) / MinimapScaleX;
	}
	for (int i = YOffset; i < SurfaceH - YOffset; ++i) {
		Minimap2MapY[i] = (((i - YOffset) * MINIMAP_FAC) / MinimapScaleY) * Map.Info.MapWidth;
	}
	for (int i = 0; i < Map.Info.MapWidth; ++i) {
		Map2MinimapX[i] = (i * MinimapScaleX) / MINIMAP_FAC;
	}
	for (int i = 0; i < Map.Info.MapHeight; ++i) {
		Map2MinimapY[i] = (i * MinimapScaleY) / MINIMAP_FAC;
	}

	// Palette updated from UpdateMinimapTerrain()
	SDL_PixelFormat *f    = Map.TileGraphic->getSurface()->format;
	MinimapTerrainSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, SurfaceW, SurfaceH, f->BitsPerPixel, f->Rmask, f->Gmask, f->Bmask, f->Amask);
	MinimapSurface 		  = SDL_CreateRGBSurface(SDL_SWSURFACE, SurfaceW, SurfaceH, 32, RMASK, GMASK, BMASK, 0);
	MinimapFogSurface 	  = SDL_CreateRGBSurface(SDL_SWSURFACE, SurfaceW, SurfaceH, 32, RMASK, GMASK, BMASK, AMASK);

    SDL_SetSurfaceBlendMode(MinimapFogSurface, SDL_BLENDMODE_BLEND);

	const uint32_t fogColorSolid = FogOfWar->GetFogColorSDL() | (uint32_t(0xFF) << ASHIFT);
	SDL_FillRect(MinimapFogSurface, nullptr, fogColorSolid);

	UpdateTerrain();

	NumMinimapEvents = 0;
}

/// The tileset sheet has no such tile (a sheet with fewer tiles than the map uses): said once,
/// the minimap pixel is left black.
static void ReportTilePastSheet(int tile)
{
	static bool reported = false;
	if (!reported) {
		reported = true;
		ErrorPrint("Minimap: tile %d is past the end of the tileset sheet, drawn black\n", tile);
	}
}

/**
**  Calculate the tile graphic pixel
*/
static inline Uint8 *GetTileGraphicPixel(int xofs, int yofs, int mx, int my, int scalex, int scaley, int bpp)
{
	Uint8 *pixels = (Uint8 *) Map.TileGraphic->getSurface()->pixels;
	int x = (xofs + 7 + ((mx * SCALE_PRECISION) % scalex) / SCALE_PRECISION * 8);
	int y = (yofs + 6 + ((my * SCALE_PRECISION) % scaley) / SCALE_PRECISION * 8);
	return &pixels[x * bpp + y * Map.TileGraphic->getSurface()->pitch];
}

// NG11: native HD minimap terrain colours. On the HD path the classic tileset sheet is a placeholder,
// so the per-tile pixel sampling above cannot produce terrain colours. Instead derive them from the
// Remastered files the folder already ships: <era>_mapColors.bin (per-megatile 4x4 grid of palette
// indices, base index 16) + the era .ppl palette (256 x 6-bit RGB). Both read natively via
// LibraryFileName; cached per era so a tileset change (or map reload) re-derives.
namespace
{
namespace fs = std::filesystem;

struct HDMinimapColors {
	std::string era;
	bool ok = false;
	uint8_t pal[256][3] = {};
	uint8_t classicPal[256][4] = {}; // classic view: the same palette x 4
	int base = 16;
	std::vector<std::array<uint8_t, 16>> tiles; // indexed by (megatile - base)
};
static HDMinimapColors g_hdmm;

// Same tileset->era mapping as EnsureHDTileAtlas (map_draw.cpp): summer->forest, winter->iceland,
// NG-TERR1: classic "wasteland"->Remastered Swamp, classic "swamp"->Remastered XSwamp (the two were
// reversed, giving the wrong minimap palette + out-of-range colours). Returns "" if unknown.
static std::string MinimapEra()
{
	const std::string &img = Map.Tileset.ImageFile;
	if (img.find("summer") != std::string::npos) return "forest";
	if (img.find("winter") != std::string::npos) return "iceland";
	if (img.find("wasteland") != std::string::npos) return "swamp";
	if (img.find("swamp") != std::string::npos) return "xswamp";
	return "";
}

static std::string EraCap(const std::string &era)
{
	if (era == "forest") return "Forest";
	if (era == "iceland") return "Iceland";
	if (era == "swamp") return "Swamp";
	if (era == "xswamp") return "XSwamp";
	return "";
}

static bool ReadNativeFile(const std::string &logicalPath, std::string &out)
{
	const fs::path resolved = LibraryFileName(logicalPath);
	if (resolved.empty()) {
		return false;
	}
	std::error_code ec;
	const uintmax_t sz = fs::file_size(resolved, ec);
	if (ec || sz == 0 || sz > (uintmax_t) 8 * 1024 * 1024) {
		return false;
	}
	auto fp = std::make_unique<CFile>();
	if (fp->open(resolved.string().c_str(), CL_OPEN_READ) == -1) {
		return false;
	}
	out.resize((size_t) sz);
	const bool okc = (size_t) fp->read(&out[0], (size_t) sz) == (size_t) sz;
	fp->close();
	return okc;
}

static bool EnsureHDMinimapColors()
{
	const std::string era = MinimapEra();
	if (era.empty()) {
		return false;
	}
	if (g_hdmm.ok && g_hdmm.era == era) {
		return true;
	}
	g_hdmm.ok = false;
	g_hdmm.tiles.clear();

	// Palette: Data/Art/bgs/<Cap>/<era>.ppl  (256 x 3 bytes, 6-bit -> 8-bit)
	std::string ppl;
	if (!ReadNativeFile("Data/Art/bgs/" + EraCap(era) + "/" + era + ".ppl", ppl) || ppl.size() < 768) {
		return false;
	}
	for (int i = 0; i < 256; ++i) {
		for (int c = 0; c < 3; ++c) {
			const uint8_t v6 = (uint8_t) ppl[i * 3 + c] & 0x3F;
			g_hdmm.pal[i][c] = (uint8_t) ((v6 * 255 + 31) / 63); // 6-bit -> 8-bit
		}
	}
	hdatlas::expandPalette6((const uint8_t *) ppl.data(), false, g_hdmm.classicPal);
	// Colours: Data/Art/hd/classic/<era>_mapColors.bin  (N*16 bytes, megatile base 16)
	std::string bin;
	if (!ReadNativeFile("Data/Art/hd/classic/" + era + "_mapColors.bin", bin) || bin.size() < 16) {
		return false;
	}
	const size_t n = bin.size() / 16;
	g_hdmm.tiles.resize(n);
	for (size_t t = 0; t < n; ++t) {
		for (int k = 0; k < 16; ++k) {
			g_hdmm.tiles[t][k] = (uint8_t) bin[t * 16 + k];
		}
	}
	g_hdmm.era = era;
	g_hdmm.ok = true;
	return true;
}

// Average the megatile's 4x4 palette-resolved colours into one representative RGB.
static bool HDTileRGB(int megatile, uint8_t &r, uint8_t &g, uint8_t &b)
{
	const int idx = megatile - g_hdmm.base;
	if (idx < 0 || idx >= (int) g_hdmm.tiles.size()) {
		return false;
	}
	const std::array<uint8_t, 16> &cell = g_hdmm.tiles[idx];
	int R = 0, G = 0, B = 0;
	for (int k = 0; k < 16; ++k) {
		const uint8_t p = cell[k];
		R += g_hdmm.pal[p][0];
		G += g_hdmm.pal[p][1];
		B += g_hdmm.pal[p][2];
	}
	r = (uint8_t) (R / 16);
	g = (uint8_t) (G / 16);
	b = (uint8_t) (B / 16);
	return true;
}

// Classic view: the terrain layer was last filled by the classic rule.
static bool g_minimapTerrainClassic = false;

// Classic view: one sample of the megatile's 4x4 record, not the mean. Needs a true-colour layer.
static bool ClassicMinimapReady()
{
	return IsClassicView() && MinimapTerrainSurface
	       && MinimapTerrainSurface->format->BytesPerPixel > 1 && EnsureHDMinimapColors();
}

static Uint32 ClassicTilePixel(int megatile, int slotX, int slotY)
{
	const int idx = hdatlas::classicMinimapRecord(megatile, g_hdmm.base, (int) g_hdmm.tiles.size());
	if (idx < 0 || slotX < 0 || slotX > 3 || slotY < 0 || slotY > 3) {
		// A tile the colour file has no record for: black, said once.
		static bool reported = false;
		if (!reported) {
			reported = true;
			ErrorPrint("Minimap: tile %d has no record in the classic colours (%d records), drawn black\n",
			           megatile, (int) g_hdmm.tiles.size());
		}
		return 0;
	}
	const uint8_t *c = g_hdmm.classicPal[g_hdmm.tiles[idx][slotY * 4 + slotX]];
	return SDL_MapRGB(MinimapTerrainSurface->format, c[0], c[1], c[2]);
}

// Write one mapped pixel into a surface (respects BytesPerPixel; Android minimap is 32bpp).
static inline void PutMinimapPixel(SDL_Surface *s, int x, int y, Uint32 px)
{
	Uint8 *d = &((Uint8 *) s->pixels)[x * s->format->BytesPerPixel + y * s->pitch];
	switch (s->format->BytesPerPixel) {
		case 4: *(Uint32 *) d = px; break;
		case 3: d[0] = px & 0xFF; d[1] = (px >> 8) & 0xFF; d[2] = (px >> 16) & 0xFF; break;
		case 2: *(Uint16 *) d = (Uint16) px; break;
		default: d[0] = (Uint8) px; break;
	}
}

} // namespace

/**
**  Update a mini-map from the tiles of the map.
*/
void CMinimap::UpdateTerrain()
{
	int scalex = MinimapScaleX * SCALE_PRECISION / MINIMAP_FAC;
	if (!scalex) {
		scalex = 1;
	}
	int scaley = MinimapScaleY * SCALE_PRECISION / MINIMAP_FAC;
	if (!scaley) {
		scaley = 1;
	}
	g_minimapTerrainClassic = IsClassicView();
	if (g_minimapTerrainClassic && ClassicMinimapReady()) {
		const int tiles = std::max({Map.Info.MapWidth, Map.Info.MapHeight, 32});
		const int pixels = std::min(SurfaceW, SurfaceH);
		SDL_LockSurface(MinimapTerrainSurface);
		for (int my = YOffset; my < SurfaceH - YOffset; ++my) {
			const int slotY = hdatlas::classicMinimapSlot(my - YOffset, tiles, pixels);
			for (int mx = XOffset; mx < SurfaceW - XOffset; ++mx) {
				const int tile = Map.Fields[Minimap2MapX[mx] + Minimap2MapY[my]].getGraphicTile();
				const int slotX = hdatlas::classicMinimapSlot(mx - XOffset, tiles, pixels);
				PutMinimapPixel(MinimapTerrainSurface, mx, my, ClassicTilePixel(tile, slotX, slotY));
			}
		}
		SDL_UnlockSurface(MinimapTerrainSurface);
		return;
	}
	const int bpp = Map.TileGraphic->getSurface()->format->BytesPerPixel;

	if (bpp == 1) {
		SDL_SetPaletteColors(MinimapTerrainSurface->format->palette,
		                     Map.TileGraphic->getSurface()->format->palette->colors,
		                     0,
		                     256);
	}

	const int tilepitch = Map.TileGraphic->getSurface()->w / PixelTileSize.x;
	const int tilerows = PixelTileSize.y > 0 ? Map.TileGraphic->getSurface()->h / PixelTileSize.y : 0;

	// WargusHD: when terrain is drawn from the native Remastered HD atlas, the classic tileset sheet
	// is a 1-frame placeholder (missing classic PNG). Sampling per-tile pixels from it (tile indices
	// run into the hundreds) reads far past the 64x64 placeholder -> SIGSEGV, and tilepitch could be
	// 0. Detect the placeholder by its frame count and bail to a blank minimap-terrain layer instead
	// of crashing. Real HD minimap colours are a later step (Remastered *_mapColors.bin).
	if (tilepitch <= 0 || Map.TileGraphic->NumFrames <= 1) {
		// NG11: HD path -- classic sheet is a placeholder. Fill terrain colours from the native
		// Remastered <era>_mapColors.bin + .ppl palette instead of leaving the minimap black.
		if (EnsureHDMinimapColors()) {
			SDL_LockSurface(MinimapTerrainSurface);
			for (int my = YOffset; my < SurfaceH - YOffset; ++my) {
				for (int mx = XOffset; mx < SurfaceW - XOffset; ++mx) {
					const int tile = Map.Fields[Minimap2MapX[mx] + Minimap2MapY[my]].getGraphicTile();
					uint8_t r, g, b;
					const Uint32 px = HDTileRGB(tile, r, g, b)
					                      ? SDL_MapRGB(MinimapTerrainSurface->format, r, g, b)
					                      : 0;
					PutMinimapPixel(MinimapTerrainSurface, mx, my, px);
				}
			}
			SDL_UnlockSurface(MinimapTerrainSurface);
			return;
		}
		SDL_FillRect(MinimapTerrainSurface, nullptr, 0);
		return;
	}

	Assert(SDL_MUSTLOCK(MinimapTerrainSurface) == 0);
	Assert(SDL_MUSTLOCK(Map.TileGraphic->getSurface()) == 0);

	//
	//  Pixel 7,6 7,14, 15,6 15,14 are taken for the minimap picture.
	//
	for (int my = YOffset; my < SurfaceH - YOffset; ++my) {
		for (int mx = XOffset; mx < SurfaceW - XOffset; ++mx) {
			const int tile = Map.Fields[Minimap2MapX[mx] + Minimap2MapY[my]].getGraphicTile();
			if (!hdatlas::tileSheetHolds(tile, tilepitch, tilerows)) {
				ReportTilePastSheet(tile);
				PutMinimapPixel(MinimapTerrainSurface, mx, my, 0);
				continue;
			}
			const int xofs = PixelTileSize.x * (tile % tilepitch);
			const int yofs = PixelTileSize.y * (tile / tilepitch);

			if (bpp == 1) {
				((Uint8 *)MinimapTerrainSurface->pixels)[mx + my * MinimapTerrainSurface->pitch] =
					*GetTileGraphicPixel(xofs, yofs, mx, my, scalex, scaley, bpp);
			} else if (bpp == 3) {
				Uint8 *d = &((Uint8 *)MinimapTerrainSurface->pixels)[mx * bpp + my * MinimapTerrainSurface->pitch];
				Uint8 *s = GetTileGraphicPixel(xofs, yofs, mx, my, scalex, scaley, bpp);
				*d++ = *s++;
				*d++ = *s++;
				*d++ = *s++;
			} else {
				*(Uint32 *)&((Uint8 *)MinimapTerrainSurface->pixels)[mx * bpp + my * MinimapTerrainSurface->pitch] =
					*(Uint32 *)GetTileGraphicPixel(xofs, yofs, mx, my, scalex, scaley, bpp);
			}

		}
	}
}


/**
** Set fog of war opacity (alpha chanel values) for different levels of visibility
**
** @param explored  alpha channel value for explored tiles
** @param revealed  alpha channel value for revealed tiles (when the map revealed)
** @param unseen    alpha channel value for unseen tiles
**
*/
void CMinimap::SetFogOpacityLevels(const uint8_t explored, const uint8_t revealed, const uint8_t unseen)
{
    this->Settings.FogExploredOpacity = explored;
    this->Settings.FogRevealedOpacity = revealed;
    this->Settings.FogUnseenOpacity   = unseen;
}

/**
**  Update a single minimap tile after a change
**
**  @param pos  The map position to update in the minimap
*/
void CMinimap::UpdateXY(const Vec2i &pos)
{
	if (!MinimapTerrainSurface) {
		return;
	}

	int scalex = MinimapScaleX * SCALE_PRECISION / MINIMAP_FAC;
	if (scalex == 0) {
		scalex = 1;
	}
	int scaley = MinimapScaleY * SCALE_PRECISION / MINIMAP_FAC;
	if (scaley == 0) {
		scaley = 1;
	}

	if (ClassicMinimapReady()) {
		const int tiles = std::max({Map.Info.MapWidth, Map.Info.MapHeight, 32});
		const int pixels = std::min(SurfaceW, SurfaceH);
		const int cty = pos.y * Map.Info.MapWidth;
		SDL_LockSurface(MinimapTerrainSurface);
		for (int my = YOffset; my < SurfaceH - YOffset; ++my) {
			const int y = Minimap2MapY[my];
			if (y < cty) continue;
			if (y > cty) break;
			const int slotY = hdatlas::classicMinimapSlot(my - YOffset, tiles, pixels);
			for (int mx = XOffset; mx < SurfaceW - XOffset; ++mx) {
				const int x = Minimap2MapX[mx];
				if (x < pos.x) continue;
				if (x > pos.x) break;
				int tile = Map.Fields[x + y].playerInfo.SeenTile;
				if (!tile) {
					tile = Map.Fields[x + y].getGraphicTile();
				}
				const int slotX = hdatlas::classicMinimapSlot(mx - XOffset, tiles, pixels);
				PutMinimapPixel(MinimapTerrainSurface, mx, my, ClassicTilePixel(tile, slotX, slotY));
			}
		}
		SDL_UnlockSurface(MinimapTerrainSurface);
		return;
	}

	// NG11: on the HD path the classic sheet is a placeholder; sampling it per-tile would read OOB.
	// Refresh this tile's minimap pixels from the native mapColors instead.
	if (Map.TileGraphic->NumFrames <= 1) {
		if (!EnsureHDMinimapColors()) {
			return;
		}
		const int hty = pos.y * Map.Info.MapWidth;
		const int htx = pos.x;
		SDL_LockSurface(MinimapTerrainSurface);
		for (int my = YOffset; my < SurfaceH - YOffset; ++my) {
			const int y = Minimap2MapY[my];
			if (y < hty) continue;
			if (y > hty) break;
			for (int mx = XOffset; mx < SurfaceW - XOffset; ++mx) {
				const int x = Minimap2MapX[mx];
				if (x < htx) continue;
				if (x > htx) break;
				int tile = Map.Fields[x + y].playerInfo.SeenTile;
				if (!tile) {
					tile = Map.Fields[x + y].getGraphicTile();
				}
				uint8_t r, g, b;
				const Uint32 px = HDTileRGB(tile, r, g, b)
				                      ? SDL_MapRGB(MinimapTerrainSurface->format, r, g, b)
				                      : 0;
				PutMinimapPixel(MinimapTerrainSurface, mx, my, px);
			}
		}
		SDL_UnlockSurface(MinimapTerrainSurface);
		return;
	}

	const int tilepitch = Map.TileGraphic->getSurface()->w / PixelTileSize.x;
	const int tilerows = PixelTileSize.y > 0 ? Map.TileGraphic->getSurface()->h / PixelTileSize.y : 0;
	const int bpp = Map.TileGraphic->getSurface()->format->BytesPerPixel;

	//
	//  Pixel 7,6 7,14, 15,6 15,14 are taken for the minimap picture.
	//

	const int ty = pos.y * Map.Info.MapWidth;
	const int tx = pos.x;
	for (int my = YOffset; my < SurfaceH - YOffset; ++my) {
		const int y = Minimap2MapY[my];
		if (y < ty) {
			continue;
		}
		if (y > ty) {
			break;
		}

		for (int mx = XOffset; mx < SurfaceW - XOffset; ++mx) {
			const int x = Minimap2MapX[mx];

			if (x < tx) {
				continue;
			}
			if (x > tx) {
				break;
			}

			int tile = Map.Fields[x + y].playerInfo.SeenTile;
			if (!tile) {
				tile = Map.Fields[x + y].getGraphicTile();
			}
			if (!hdatlas::tileSheetHolds(tile, tilepitch, tilerows)) {
				ReportTilePastSheet(tile);
				PutMinimapPixel(MinimapTerrainSurface, mx, my, 0);
				continue;
			}

			const int xofs = PixelTileSize.x * (tile % tilepitch);
			const int yofs = PixelTileSize.y * (tile / tilepitch);

			const int index = mx * bpp + my * MinimapTerrainSurface->pitch;
			Uint8 *s = GetTileGraphicPixel(xofs, yofs, mx, my, scalex, scaley, bpp);
			if (bpp == 1) {
				((Uint8 *)MinimapTerrainSurface->pixels)[index] = *s;
			} else if (bpp == 3) {
				Uint8 *d = &((Uint8 *)MinimapTerrainSurface->pixels)[index];

				*d++ = *s++;
				*d++ = *s++;
				*d++ = *s++;
			} else {
				*(Uint32 *)&((Uint8 *)MinimapTerrainSurface->pixels)[index] = *(Uint32 *)s;
			}
		}
	}
}

/**
**  Draw a unit on the minimap.
*/
static void DrawUnitOn(const CUnit &unit, int red_phase, int scaleX, int scaleY)
{
	const CUnitType *type;

	if (Editor.Running || ReplayRevealMap || unit.IsVisible(*ThisPlayer) || unit.Player->IsRevealed()) {
		type = unit.Type;
	} else {
		type = unit.Seen.Type;
		// This will happen for radar if the unit has not been seen and we
		// have it on radar.
		if (!type) {
			type = unit.Type;
		}
	}

	Uint32 color;
	if (unit.Player->Index == PlayerNumNeutral) {
		color = Video.MapRGB(TheScreen->format, type->NeutralMinimapColorRGB);
	} else if (unit.Player == ThisPlayer && !Editor.Running) {
		if (unit.Attacked && unit.Attacked + ATTACK_BLINK_DURATION > GameCycle &&
			(red_phase || unit.Attacked + ATTACK_RED_DURATION > GameCycle)) {
			color = ColorRed;
		} else if (UI.Minimap.ShowSelected && unit.Selected) {
			color = ColorWhite;
		} else {
			color = ColorGreen;
		}
	} else {
		color = PlayerColorsRGB[GameSettings.Presets[unit.Player->Index].PlayerColor][0];
	}

	int mx = 1 + UI.Minimap.XOffset + Map2MinimapX[unit.tilePos.x];
	int my = 1 + UI.Minimap.YOffset + Map2MinimapY[unit.tilePos.y];
	// The loops below draw w + 1 by h0 + 1 pixels from the pixel of the unit's own tile.
	// Remastered view: the footprint, a one-tile unit never under 2 of the original's 128 minimap
	// pixels (live run 71, MINIMAP-MARK-SIZE; hdatlas::remasteredMinimapMarkPixels).
	const bool classic = ClassicHudActive();
	const int surfacePx = std::min(SurfaceW, SurfaceH);
	int w = (classic ? hdatlas::classicMinimapMarkPixels(type->TileWidth, scaleX, MINIMAP_FAC)
					 : hdatlas::remasteredMinimapMarkPixels(type->TileWidth, scaleX, MINIMAP_FAC, surfacePx)) - 1;
	if (mx + w >= SurfaceW) { // clip right side
		w = SurfaceW - mx;
	}
	int h0 = (classic ? hdatlas::classicMinimapMarkPixels(type->TileHeight, scaleY, MINIMAP_FAC)
					  : hdatlas::remasteredMinimapMarkPixels(type->TileHeight, scaleY, MINIMAP_FAC, surfacePx)) - 1;
	if (my + h0 >= SurfaceH) { // clip bottom side
		h0 = SurfaceH - my;
	}
	int bpp = 0;
	SDL_Color c;
	bpp = MinimapSurface->format->BytesPerPixel;
	SDL_GetRGB(color, TheScreen->format, &c.r, &c.g, &c.b);
	while (w-- >= 0) {
		int h = h0;
		while (h-- >= 0) {
			const unsigned int index = (mx + w) * bpp + (my + h) * MinimapSurface->pitch;
			if (bpp == 2) {
				*(Uint16 *)&((Uint8 *)MinimapSurface->pixels)[index] = color;
			} else {
				*(Uint32 *)&((Uint8 *)MinimapSurface->pixels)[index] = color;
			}
		}
	}
}

/**
**  Update the minimap with the current game information
*/
void CMinimap::Update()
{
	static int red_phase;

	int red_phase_changed = red_phase != (int)((FrameCounter / CYCLES_PER_SECOND) & 1);
	if (red_phase_changed) {
		red_phase = !red_phase;
	}

	// Classic view switched: the terrain layer is redrawn whole, once.
	if (MinimapTerrainSurface && g_minimapTerrainClassic != IsClassicView()) {
		UpdateTerrain();
	}

	// Clear Minimap background if not transparent
	if (!Transparent) {
		SDL_FillRect(MinimapSurface, nullptr, SDL_MapRGB(MinimapSurface->format, 0, 0, 0));
	}

	//
	// Draw the terrain
	//
	if (WithTerrain) {
		SDL_BlitSurface(MinimapTerrainSurface, nullptr, MinimapSurface, nullptr);
	}
	const uint32_t fogColorSDL = FogOfWar->GetFogColorSDL();
	if (!ReplayRevealMap) {
		uint32_t *const minimapFog = static_cast<uint32_t *>(MinimapFogSurface->pixels);
		// Classic view (S1_MINIMAP_NOTE.md section 2, cuts o11_95 / o11_24): explored-but-unseen
		// ground is not darkened but checkered - opaque black on the logical minimap pixels whose
		// x + y is odd, the ground colour untouched on the others. Only with the classic HUD, where
		// one surface pixel is one logical (128-box) pixel. Never-seen ground and visible ground
		// keep the alphas below in both views; Remastered unchanged.
		const bool classicChecker = ClassicHudActive() && IsClassicView();
		const uint32_t classicBlack = SDL_MapRGB(MinimapFogSurface->format, 0, 0, 0) & ~AMASK;
		size_t index = 0;
		for (uint16_t my = 0; my < SurfaceH; ++my) {
			for (uint16_t mx = 0; mx < SurfaceW; ++mx) {

				const Vec2i tilePos(Minimap2MapX[mx], Minimap2MapY[my] / Map.Info.MapWidth);
				const uint8_t vis = FogOfWar->GetVisibilityForTile(tilePos);

				if (classicChecker && vis == 1) {
					const uint32_t a = ((mx + my) & 1) ? 0xFFu : 0x00u;
					minimapFog[index++] = classicBlack | (a << ASHIFT);
					continue;
				}

				const uint32_t fogAlpha = vis == 0 ? (GameSettings.RevealMap != MapRevealModes::cHidden ? Settings.FogRevealedOpacity : Settings.FogUnseenOpacity)
											   	   : vis == 1 ? Settings.FogExploredOpacity
										   		   			  : Settings.FogVisibleOpacity;

				minimapFog[index++] = fogColorSDL | (fogAlpha << ASHIFT);
			}
		}
		/// Alpha blending the fog of war texture to minimap
		/// TODO: switch to hardware rendering
		const SDL_Rect fogRect {0, 0, SurfaceW, SurfaceH};
		BlitSurfaceAlphaBlending_32bpp(MinimapFogSurface, &fogRect, MinimapSurface, &fogRect);
	}
	//
	// Draw units on map
	//
	for (const CUnit *unit : UnitManager->GetUnits()) {
		if (unit->IsVisibleOnMinimap() && !unit->Removed && !unit->Type->BoolFlag[REVEALER_INDEX].value) {
			DrawUnitOn(*unit, red_phase, MinimapScaleX, MinimapScaleY);
		}
	}
}

/**
**  Draw the minimap events
*/
static void DrawEvents()
{
	const unsigned char alpha = 192;

	for (int i = 0; i < NumMinimapEvents; ++i) {
		Video.DrawTransCircleClip(MinimapEvents[i].Color,
								  MinimapEvents[i].pos.x, MinimapEvents[i].pos.y,
								  MinimapEvents[i].Size, alpha);
		MinimapEvents[i].Size -= 1;
		if (MinimapEvents[i].Size < 2) {
			MinimapEvents[i] = MinimapEvents[--NumMinimapEvents];
			--i;
		}
	}
}

/**
**  Draw the minimap on the screen
*/
void CMinimap::Draw() const
{
	// GPU-HUD: upload the (small, opaque) minimap surface to a cached texture and RenderCopy it to
	// the backbuffer. MinimapSurface has no alpha channel, so force BLENDMODE_NONE to match the
	// opaque CPU blit. DrawEvents() and the viewport-area contour stay on the CPU overlay, which
	// composites on top of this. Falls back to the CPU blit when the flag is off (menus/editor).
	if (GpuHudDrawActive && TheRenderer && MinimapSurface) {
		if (!MinimapTexture || MinimapTextureW != MinimapSurface->w
			|| MinimapTextureH != MinimapSurface->h) {
			if (MinimapTexture) {
				SDL_DestroyTexture(MinimapTexture);
			}
			const Uint32 fmt = SDL_MasksToPixelFormatEnum(32,
				MinimapSurface->format->Rmask, MinimapSurface->format->Gmask,
				MinimapSurface->format->Bmask, MinimapSurface->format->Amask);
			MinimapTexture = SDL_CreateTexture(TheRenderer, fmt,
				SDL_TEXTUREACCESS_STREAMING, MinimapSurface->w, MinimapSurface->h);
			MinimapTextureW = MinimapSurface->w;
			MinimapTextureH = MinimapSurface->h;
			if (MinimapTexture) {
				SDL_SetTextureBlendMode(MinimapTexture, SDL_BLENDMODE_NONE);
			}
		}
		if (MinimapTexture) {
			SDL_UpdateTexture(MinimapTexture, nullptr,
				MinimapSurface->pixels, MinimapSurface->pitch);
			SDL_Rect dst = {X, Y, MinimapSurface->w * MinimapStep, MinimapSurface->h * MinimapStep};
			SDL_RenderCopy(TheRenderer, MinimapTexture, nullptr, &dst);
			DrawEvents();
			return;
		}
	}

	SDL_Rect drect = {Sint16(X), Sint16(Y), 0, 0};
	SDL_BlitSurface(MinimapSurface, nullptr, TheScreen, &drect);

	DrawEvents();
}

/**
**  Convert screen position to tile map coordinate.
**
**  @param screenPos  Screen pixel coordinate.
**
**  @return   Tile coordinate.
*/
Vec2i CMinimap::ScreenToTilePos(const PixelPos &screenPos) const
{
	Vec2i tilePos(((((screenPos.x - X) / MinimapStep - XOffset) * MINIMAP_FAC) / MinimapScaleX),
				  ((((screenPos.y - Y) / MinimapStep - YOffset) * MINIMAP_FAC) / MinimapScaleY));

	Map.Clamp(tilePos);
	return tilePos;
}

/**
**  Convert tile map coordinate to screen position.
**
**  @param tilePos  Tile coordinate.
**
**  @return   Screen pixel coordinate.
*/
PixelPos CMinimap::TilePosToScreenPos(const Vec2i &tilePos) const
{
	const PixelPos screenPos(X + MinimapStep * (XOffset + (tilePos.x * MinimapScaleX) / MINIMAP_FAC),
							 Y + MinimapStep * (YOffset + (tilePos.y * MinimapScaleY) / MINIMAP_FAC));
	return screenPos;
}

/**
**  Destroy mini-map.
*/
void CMinimap::Destroy()
{
	if (MinimapTerrainSurface) {
		VideoPaletteListRemove(MinimapTerrainSurface);
		SDL_FreeSurface(MinimapTerrainSurface);
		MinimapTerrainSurface = nullptr;
	}
	if (MinimapTexture) {
		SDL_DestroyTexture(MinimapTexture);
		MinimapTexture = nullptr;
		MinimapTextureW = 0;
		MinimapTextureH = 0;
	}
	if (MinimapSurface) {
		VideoPaletteListRemove(MinimapSurface);
		SDL_FreeSurface(MinimapSurface);
		MinimapSurface = nullptr;
	}
	if (MinimapFogSurface && MinimapFogSurface->format != nullptr) {
		SDL_FreeSurface(MinimapFogSurface);
		MinimapFogSurface = nullptr;
	}
	Minimap2MapX.clear();
	Minimap2MapY.clear();
}

/**
**  Draw viewport area contour.
*/
void CMinimap::DrawViewportArea(const CViewport &viewport, int alpha) const
{
	// M666-7. Size, measured on the original as outer px, k = HUD step, view in tiles =
	// view px / tile px:
	//   k 3, view 25.33 x 19.25, 96-tile map  (4 px a tile): 102 x 78  (c_k, c25, b02, b03, b04)
	//   k 3, view 25.33 x 19.25, 128-tile map (3 px a tile):  75 x 57  (e01, e02)
	//   k 2, view 20.81 x 16.11, 32-tile map  (8 px a tile): 160 x 128 (o20, o41, o45)
	// One rule gives all six numbers: whole tiles of the view (rounded down), times the minimap's
	// base units per tile, rounded up to a whole unit, times k. It is a fit to three scenes, not
	// a decoded rule.
	// Line: k px, drawn inwards.
	// Place: the view's own top-left, pixel-true. The original's place rule is not in evidence.
	const int k = std::max(1, Video.HudCanvas.Step);
	const int n = std::max(Map.Info.MapWidth, Map.Info.MapHeight);
	const double unitsPerTile = (n > 0) ? (double) std::min(W, H) / k / n : 0.0;
	const PixelSize viewPx = viewport.GetRenderPixelSize();
	const int tilesX = viewPx.x / PixelTileSize.x;
	const int tilesY = viewPx.y / PixelTileSize.y;
	const int w = k * (int) std::ceil(tilesX * unitsPerTile - 1e-6);
	const int h = k * (int) std::ceil(tilesY * unitsPerTile - 1e-6);
	const PixelPos tilePos = TilePosToScreenPos(viewport.MapPos);
	const int x = tilePos.x + MinimapStep * ((viewport.Offset.x * MinimapScaleX) / (MINIMAP_FAC * PixelTileSize.x));
	const int y = tilePos.y + MinimapStep * ((viewport.Offset.y * MinimapScaleY) / (MINIMAP_FAC * PixelTileSize.y));

	if (ClassicHudActive()) {
		// Classic view: the size by the whole tiles of the logical view
		// (hdatlas::classicMinimapViewPixels: 104 x 92 on a 32-tile map, 25 x 23 on a 96-tile one
		// at 776 x 582), a line of one classic pixel = step.
		// Colour 252, 248, 240 (o11_13 and the other captures of the classic minimap).
		const Uint32 colour = Video.MapRGB(TheScreen->format, 252, 248, 240);
		const int line = hdatlas::classicStep(Video.Height);
		const hdatlas::ClassicRect lv = hdatlas::classicView(hdatlas::classicScreen(Video.Width, Video.Height));
		const int cw = MinimapStep * hdatlas::classicMinimapViewPixels(
						   std::min(lv.w, Map.Info.MapWidth * hdatlas::ClassicTilePx), n);
		const int ch = MinimapStep * hdatlas::classicMinimapViewPixels(
						   std::min(lv.h, Map.Info.MapHeight * hdatlas::ClassicTilePx), n);
		for (int i = 0; i < line && cw - 2 * i > 0 && ch - 2 * i > 0; ++i) {
			Video.DrawTransRectangle(colour, x + i, y + i, cw - 2 * i, ch - 2 * i, alpha);
		}
		return;
	}

	// Draw cursor as rectangle (Note: unclipped, as it is always visible)
	for (int i = 0; i < k && w - 2 * i > 0 && h - 2 * i > 0; ++i) {
		Video.DrawTransRectangle(UI.ViewportCursorColor, x + i, y + i, w - 2 * i, h - 2 * i, alpha);
	}
}

/**
**  Add a minimap event
**
**  @param pos  Map tile position
*/
void CMinimap::AddEvent(const Vec2i &pos, Uint32 color)
{
	if (NumMinimapEvents == MAX_MINIMAP_EVENTS) {
		return;
	}
	MinimapEvents[NumMinimapEvents].pos = TilePosToScreenPos(pos);
	MinimapEvents[NumMinimapEvents].Size = (W < H) ? W / 3 : H / 3;
	MinimapEvents[NumMinimapEvents].Color = color;
	++NumMinimapEvents;
}

bool CMinimap::Contains(const PixelPos &screenPos) const
{
	return this->X <= screenPos.x && screenPos.x < this->X + this->W
		   && this->Y <= screenPos.y && screenPos.y < this->Y + this->H;
}

//@}
