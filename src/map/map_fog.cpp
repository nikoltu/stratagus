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
/**@name map_fog.cpp - The map fog of war handling. */
//
//      (c) Copyright 1999-2021 by Lutz Sammer, Vladi Shabanski,
//		Russell Smith, Jimmy Salmon, Pali Rohár, Andrettin and Alyokhin
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
#include <cmath>
#include <queue>

#include "stratagus.h"

#include "map.h"

#include "actions.h"
#include "fov.h"
#include "minimap.h"
#include "player.h"
#include "tileset.h"
#include "ui.h"
#include "unit.h"
#include "unit_manager.h"
#include "video.h"
#include "../video/intern_video.h"

#ifdef USE_STACKTRACE
#include <stdexcept>
#include <stacktrace/call_stack.hpp>
#include <stacktrace/stack_exception.hpp>
#else
#include "st_backtrace.h"
#endif

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/
CFieldOfView FieldOfView;

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

class _filter_flags
{
public:
	_filter_flags(const CPlayer &p, int *fogmask) : player(&p), fogmask(fogmask)
	{
		Assert(fogmask != nullptr);
	}

	void operator()(const CUnit *const unit) const
	{
		if (!unit->IsVisibleAsGoal(*player)) {
			*fogmask &= ~unit->Type->FieldFlags;
		}
	}
private:
	const CPlayer *player;
	int *fogmask;
};

/**
**  Find out what the tile flags are a tile is covered by fog
**
**  @param player  player who is doing operation
**  @param index   map location
**  @param mask    input mask to filter
**
**  @return        Filtered mask after taking fog into account
*/
int MapFogFilterFlags(CPlayer &player, const unsigned int index, int mask)
{
	int fogMask = mask;

	_filter_flags filter(player, &fogMask);
	for (auto* unit : Map.Field(index)->UnitCache) {
		filter(unit);
	}
	return fogMask;
}

int MapFogFilterFlags(CPlayer &player, const Vec2i &pos, int mask)
{
	if (Map.Info.IsPointOnMap(pos)) {
		return MapFogFilterFlags(player, Map.getIndex(pos), mask);
	}
	return mask;
}

template<bool MARK>
class _TileSeen
{
public:
	_TileSeen(const CPlayer &p , int c) : player(&p), cloak(c)
	{}

	void operator()(CUnit *const unit) const
	{
		if (cloak != (int)unit->Type->BoolFlag[PERMANENTCLOAK_INDEX].value) {
			return ;
		}
		const uint8_t p = this->player->Index;
		if constexpr (MARK) {
			//  If the unit goes out of fog, this can happen for any player that
			//  this player shares vision with, and can't YET see the unit.
			//  It will be able to see the unit after the Unit->VisCount ++
			if (!unit->VisCount[p]) {
				UnitGoesOutOfFog(*unit, *this->player);
				for (const uint8_t pi : this->player->GetGaveVisionTo()) {
					if (!unit->IsVisible(Players[pi])) {
						UnitGoesOutOfFog(*unit, Players[pi]);
					}
				}
			}
			unit->VisCount[p/*player->Index*/]++;
		} else {
			/*
			 * HACK: UGLY !!!
			 * There is bug in Seen code connected with
			 * UnitAction::Die and Cloaked units.
			 */
			if (!unit->VisCount[p] && unit->CurrentAction() == UnitAction::Die) {
				return;
			}

			/// This could happen if shadow caster type of field of view is enabled,
			/// because of multiple calls for tiles in vertical/horizontal/diagonal lines
			if(!unit->VisCount[p]) {
				return;
			}

			unit->VisCount[p]--;
			//  If the unit goes under of fog, this can happen for any player that
			//  this player shares vision to. First of all, before unmarking,
			//  every player that this player shares vision to can see the unit.
			//  Now we have to check who can't see the unit anymore.
			if (!unit->VisCount[p]) {
				UnitGoesUnderFog(*unit, *this->player);
				for (const uint8_t pi : this->player->GetGaveVisionTo()) {
					if (!unit->IsVisible(Players[pi])) {
						UnitGoesUnderFog(*unit, Players[pi]);
					}
				}
			}
		}
	}
private:
	const CPlayer *player;
	int cloak;
};

/**
**  Mark all units on a tile as now visible.
**
**  @param player  The player this is for.
**  @param mf      field location to check
**  @param cloak   If we mark cloaked units too.
*/
static void UnitsOnTileMarkSeen(const CPlayer &player, CMapField &mf, int cloak)
{
	_TileSeen<true> seen(player, cloak);
	for (auto *unit : mf.UnitCache) {
		seen(unit);
	}
}

/**
**  This function unmarks units on x, y as seen. It uses a reference count.
**
**  @param player    The player to mark for.
**  @param mf        field to check if building is on, and mark as seen
**  @param cloak     If this is for cloaked units.
*/
static void UnitsOnTileUnmarkSeen(const CPlayer &player, CMapField &mf, int cloak)
{
	_TileSeen<false> seen(player, cloak);
	for (auto* unit : mf.UnitCache) {
		seen(unit);
	}
}

/**
**  Mark a tile's sight. (Explore and make visible.)
**
**  @param player  Player to mark sight.
**  @param index   tile to mark.
*/
void MapMarkTileSight(const CPlayer &player, const unsigned int index)
{
	CMapField &mf = *Map.Field(index);
	unsigned short *v = &(mf.playerInfo.Visible[player.Index]);

	if (*v == 0 || *v == 1) { // Unexplored or unseen
		// When there is no fog only unexplored tiles are marked.
		if (!Map.NoFogOfWar || *v == 0) {
			UnitsOnTileMarkSeen(player, mf, 0);
		}
		*v = 2;
		if (mf.playerInfo.IsTeamVisible(*ThisPlayer)) {
			Map.MarkSeenTile(mf);
		}
	} else {
		Assert(*v != 65535);
		++*v;
	}
#if 0
	if (EnableDebugPrint) {
		ErrorPrint("Mapsight: GameCycle: %lud, SyncHash before: %x", GameCycle, SyncHash);
	}
	// Calculate some hash.
	SyncHash = (SyncHash << 5) | (SyncHash >> 27);
	SyncHash ^= (*v << 16) | *v;

	if (EnableDebugPrint) {
		ErrorPrint(", after: %x (mapfield: %d, player: %d, sight: %d)\n", SyncHash, index, player.Index, *v);
		print_backtrace(8);
		fflush(stderr);
	}
#endif
}

void MapMarkTileSight(const CPlayer &player, const Vec2i &pos)
{
	Assert(Map.Info.IsPointOnMap(pos));
	MapMarkTileSight(player, Map.getIndex(pos));
}

/**
**  Unmark a tile's sight. (Explore and make visible.)
**
**  @param player  Player to mark sight.
**  @param index   tile to mark.
*/
void MapUnmarkTileSight(const CPlayer &player, const unsigned int index)
{
	CMapField &mf = *Map.Field(index);
	unsigned short *v = &mf.playerInfo.Visible[player.Index];
	switch (*v) {
		case 0:  // Unexplored
		case 1:
			// This happens when we unmark everything in CommandSharedVision
			break;
		case 2:
			// When there is NoFogOfWar units never get unmarked.
			if (!Map.NoFogOfWar) {
				UnitsOnTileUnmarkSeen(player, mf, 0);
			}
			// Check visible Tile, then deduct...
			/// TODO: change ThisPlayer to currently rendered player/players #RenderTargets
			if (mf.playerInfo.IsTeamVisible(*ThisPlayer)) {
				Map.MarkSeenTile(mf);
			}
		default:  // seen -> seen
			--*v;
			break;
	}
}

void MapUnmarkTileSight(const CPlayer &player, const Vec2i &pos)
{
	Assert(Map.Info.IsPointOnMap(pos));
	MapUnmarkTileSight(player, Map.getIndex(pos));
}

/**
**  Mark a tile for cloak detection.
**
**  @param player  Player to mark sight.
**  @param index   Tile to mark.
*/
void MapMarkTileDetectCloak(const CPlayer &player, const unsigned int index)
{
	CMapField &mf = *Map.Field(index);
	unsigned char *v = &mf.playerInfo.VisCloak[player.Index];
	if (*v == 0) {
		UnitsOnTileMarkSeen(player, mf, 1);
	}
	Assert(*v != 255);
	++*v;
}

void MapMarkTileDetectCloak(const CPlayer &player, const Vec2i &pos)
{
	MapMarkTileDetectCloak(player, Map.getIndex(pos));
}

/**
**  Unmark a tile for cloak detection.
**
**  @param player  Player to mark sight.
**  @param index   tile to mark.
*/
void MapUnmarkTileDetectCloak(const CPlayer &player, const unsigned int index)
{
	CMapField &mf = *Map.Field(index);
	unsigned char *v = &mf.playerInfo.VisCloak[player.Index];
	///Assert(*v != 0);
	/// This could happen if shadow caster type of field of view is enabled,
	/// because of multiple calls for tiles in vertical/horizontal/diagonal lines
	if(*v == 0) {
		return;
	}
	if (*v == 1) {
		UnitsOnTileUnmarkSeen(player, mf, 1);
	}
	--*v;
}

void MapUnmarkTileDetectCloak(const CPlayer &player, const Vec2i &pos)
{
	MapUnmarkTileDetectCloak(player, Map.getIndex(pos));
}

/**
**  Mark the sight of unit. (Explore and make visible.)
**
**  @param player  player to mark the sight for (not unit owner)
**  @param pos     location to mark
**  @param w       width to mark, in square
**  @param h       height to mark, in square
**  @param range   Radius to mark.
**  @param marker  Function to mark or unmark sight
*/
void MapSight(const CPlayer &player, const CUnit &unit, const Vec2i &pos, int w, int h, int range, MapMarkerFunc *marker)
{
	FieldOfView.Refresh(player, unit, pos, w, h, range, marker);
}

/**
**  Update fog of war.
*/
void UpdateFogOfWarChange()
{
	DebugPrint("::UpdateFogOfWarChange\n");
	//  Mark all explored fields as visible again.
	if (Map.NoFogOfWar) {
		const unsigned int w = Map.Info.MapHeight * Map.Info.MapWidth;
		for (unsigned int index = 0; index != w; ++index) {
			CMapField &mf = *Map.Field(index);
			if (mf.playerInfo.IsExplored(*ThisPlayer)) {
				Map.MarkSeenTile(mf);
			}
		}
	}
	//  Global seen recount.
	for (CUnit *unit : UnitManager->GetUnits()) {
		UnitCountSeen(*unit);
	}
}


/**
**  Draw the map fog of war.
*/
void CViewport::DrawMapFogOfWar()
{
	// flags must redraw or not
	if (ReplayRevealMap) {
		return;
	}

	if (!this->FogSurface || ( ((this->BottomRightPos.x - this->TopLeftPos.x)
									/ PixelTileSize.x + 2)
									* PixelTileSize.x != this->FogSurface->w
								|| ((this->BottomRightPos.y - this->TopLeftPos.y)
									/ PixelTileSize.y + 2)
									* PixelTileSize.y != this->FogSurface->h) ) {
		this->AdjustFogSurface();
	}

	FogOfWar->Draw(*this);

	const bool nonLegacy = FogOfWar->GetType() != FogOfWarTypes::cTiledLegacy;

	// On-screen destination = the whole viewport rectangle in window pixels.
	SDL_Rect screenRect;
	screenRect.x = this->TopLeftPos.x;
	screenRect.y = this->TopLeftPos.y;
	screenRect.w = this->BottomRightPos.x - this->TopLeftPos.x + 1;
	screenRect.h = this->BottomRightPos.y - this->TopLeftPos.y + 1;

	// Source sub-region of the fog surface: the logical (native == full / Zoom) map area starting
	// at the scroll Offset. This is exactly the region the CPU blit sampled below. At Zoom==1 it is
	// the full on-screen size; when zoomed it is the smaller native region that must be scaled up to
	// the zoomed world underneath.
	const PixelSize logical = this->GetRenderPixelSize(); // == full / Zoom
	SDL_Rect fogRect;
	fogRect.x = this->Offset.x;
	fogRect.y = this->Offset.y;
	fogRect.w = logical.x;
	fogRect.h = logical.y;

	// GPU-pipeline (Phase 2): when the world for this viewport was drawn straight to the GPU
	// backbuffer (the on-screen render), composite the fog on the GPU too. The editor/zoom offscreen
	// and the crisp path both redirect TheScreen to this->MapRenderSurface before drawing fog, so the
	// `TheScreen != MapRenderSurface` test selects exactly the backbuffer path and keeps the CPU blit
	// for those. Upload the small per-viewport fog surface (map-tile resolution, cheap) to a streaming
	// texture and RenderCopy it scaled over the viewport rect with alpha blending. This lands on the
	// backbuffer AFTER the tiles/units and BEFORE the CPU HUD overlay (composited in
	// RealizeVideoMemory), so fog covers the world but the HUD stays on top. The fog surface already
	// carries the fog tint in RGB and coverage in alpha, so plain BLEND is correct (no colour-mod
	// needed). Linear scaling smooths the low-res fog gradient at zoom.
	const bool gpuFog = nonLegacy && this->FogSurface && TheRenderer
	                    && TheScreen != this->MapRenderSurface;
	if (gpuFog) {
		if (!this->FogTexture || this->FogTextureWidth != this->FogSurface->w
			|| this->FogTextureHeight != this->FogSurface->h) {
			if (this->FogTexture) {
				SDL_DestroyTexture(this->FogTexture);
				this->FogTexture = nullptr;
			}
			this->FogTexture = SDL_CreateTexture(TheRenderer, SDL_PIXELFORMAT_ARGB8888,
												 SDL_TEXTUREACCESS_STREAMING,
												 this->FogSurface->w, this->FogSurface->h);
			if (this->FogTexture) {
				this->FogTextureWidth = this->FogSurface->w;
				this->FogTextureHeight = this->FogSurface->h;
				SDL_SetTextureBlendMode(this->FogTexture, SDL_BLENDMODE_BLEND);
				SDL_SetTextureScaleMode(this->FogTexture, SDL_ScaleModeLinear);
			}
		}
		if (this->FogTexture) {
			SDL_UpdateTexture(this->FogTexture, nullptr, this->FogSurface->pixels,
							  this->FogSurface->pitch);
			SDL_RenderCopy(TheRenderer, this->FogTexture, &fogRect, &screenRect);
			return;
		}
		// Texture creation failed: fall through to the CPU blit below.
	}

	/// CPU fallback: editor/zoom offscreen, crisp path, or GPU texture-creation failure.
	const bool isSoftwareRender {true};
	if (isSoftwareRender && nonLegacy) {
		// Zoomed viewport (crisp path OR the offscreen render): sample the logical (full / Zoom)
		// sub-region of the fog and scale-blend it up so it lines up with the zoomed world, using
		// SDL's own alpha blit (scale + blend). Byte-identical classic path when not zoomed (the
		// offscreen render resets Zoom to 1.0 before calling here, so that path is unaffected).
		if (this->Zoom != 1.0f) {
			SDL_SetSurfaceBlendMode(this->FogSurface, SDL_BLENDMODE_BLEND);
			SDL_BlitScaled(this->FogSurface, &fogRect, TheScreen, &screenRect);
			SDL_SetSurfaceBlendMode(this->FogSurface, SDL_BLENDMODE_NONE);
			return;
		}

		// Non-zoomed CPU path: byte-identical to the classic code — sample the full on-screen
		// viewport region (GetPixelSize omits the inclusive +1 pixel that screenRect carries).
		SDL_Rect nativeFogRect = { this->Offset.x, this->Offset.y, screenRect.w, screenRect.h };
		BlitSurfaceAlphaBlending_32bpp(this->FogSurface, &nativeFogRect, TheScreen, &screenRect);
	}
}


/**
**  Adjust fog of war surface to viewport
**
*/
void CViewport::AdjustFogSurface()
{
	this->CleanFog();

    const uint16_t surfaceWidth  = ((this->BottomRightPos.x - this->TopLeftPos.x)
									/ PixelTileSize.x + 2)
									* PixelTileSize.x;  /// +2 because of Offset.x
    const uint16_t surfaceHeight = ((this->BottomRightPos.y - this->TopLeftPos.y)
									/ PixelTileSize.y + 2)
									* PixelTileSize.y; /// +2 because of Offset.y

    this->FogSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, surfaceWidth,
                                                     	   surfaceHeight,
                                                     	   32, RMASK, GMASK, BMASK, AMASK);
    SDL_SetSurfaceBlendMode(this->FogSurface, SDL_BLENDMODE_NONE);

	const uint32_t fogColorSolid = FogOfWar->GetFogColorSDL() | (uint32_t(0xFF) << ASHIFT);
	SDL_FillRect(this->FogSurface, nullptr, fogColorSolid);
}

void CViewport::Clean()
{
	if (this->FogSurface) {
		CleanFog();
	}
	this->CleanMapRenderSurface();
}

void CViewport::CleanFog()
{
	SDL_FreeSurface(this->FogSurface);
	this->FogSurface = nullptr;
	// GPU-pipeline (Phase 2): release the streaming fog texture too. It is lazily recreated on the
	// next GPU fog draw (sized to the fresh FogSurface).
	if (this->FogTexture) {
		SDL_DestroyTexture(this->FogTexture);
		this->FogTexture = nullptr;
	}
	this->FogTextureWidth = 0;
	this->FogTextureHeight = 0;
}


//@}
