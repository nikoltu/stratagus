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
/**@name viewport.h - The Viewport header file. */
//
//      (c) Copyright 2012 by Joris Dauphin
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

#ifndef VIEWPORT_H
#define VIEWPORT_H

//@{
#include "fow.h"
#include "vec2i.h"
class CUnit;
class CMapField;

/**
**  A map viewport.
**
**  A part of the map displayed on screen.
**
**  CViewport::TopLeftPos
**  CViewport::BottomRightPos
**
**    upper left corner of this viewport is located at pixel
**    coordinates (TopLeftPosTopLeftPos) with respect to upper left corner of
**    stratagus's window, similarly lower right corner of this
**    viewport is (BottomRightPos) pixels away from the UL corner of
**    stratagus's window.
**
**  CViewport::MapX CViewport::MapY
**  CViewport::MapWidth CViewport::MapHeight
**
**    Tile coordinates of UL corner of this viewport with respect to
**    UL corner of the whole map.
**
**  CViewport::Unit
**
**    Viewport is bound to a unit. If the unit moves the viewport
**    changes the position together with the unit.
*/

using fieldHighlightChecker = bool(*)(const CMapField&); // type alias

class CViewport
{
public:
	CViewport() = default;
	~CViewport();

	/// Check if pos pixels are within map area
	bool IsInsideMapArea(const PixelPos &screenPixelPos) const;

	/// Convert screen coordinates into map pixel coordinates
	PixelPos ScreenToMapPixelPos(const PixelPos &screenPixelPos) const;
	// Convert map pixel coordinates into screen coordinates
	PixelPos MapToScreenPixelPos(const PixelPos &mapPixelPos) const;

	/// convert screen coordinate into tilepos
	Vec2i ScreenToTilePos(const PixelPos &screenPixelPos) const;
	/// convert tilepos coordinates into screen (take the top left of the tile)
	PixelPos TilePosToScreen_TopLeft(const Vec2i &tilePos) const;
	/// convert tilepos coordinates into screen (take the center of the tile)
	PixelPos TilePosToScreen_Center(const Vec2i &tilePos) const;

	SDL_Surface* GetFogSurface() {
		return this->FogSurface;
	}

	/// Set the current map view to x,y(upper,left corner)
	void Set(const Vec2i &tilePos, const PixelDiff &offset);
	/// Center map on point in viewport
	void Center(const PixelPos &mapPixelPos);

	void SetClipping() const;

	/// Draw the full Viewport.
	void Draw(const fieldHighlightChecker highlightChecker = nullptr);
	void DrawBorder() const;
	/// Check if any part of an area is visible in viewport
	bool AnyMapAreaVisibleInViewport(const Vec2i &boxmin, const Vec2i &boxmax) const;
	bool IsAreaVisibleInViewport(const PixelPos &areaScreenPos, const PixelSize &areaSize) const;

	bool Contains(const PixelPos &screenPos) const;

	void Restrict(int &screenPosX, int &screenPosY) const;
	void Clean();

	static bool isGridEnabled()
	{
		return CViewport::ShowGrid;
	}

	static void EnableGrid(bool value)
	{
		CViewport::ShowGrid = value;
	}

	static bool isPassabilityHighlighted()
	{
		return CViewport::ShowAStarPassability;
	}

	static void HighlightPassability(bool value)
	{
		CViewport::ShowAStarPassability = value;
	}

	PixelSize GetPixelSize() const;
	/// On-screen size of the viewport divided by the zoom factor. This is the
	/// size, in native pixels, the world is rendered into before it is scaled up
	/// to the on-screen rectangle. Equal to GetPixelSize() when Zoom == 1.
	PixelSize GetRenderPixelSize() const;
	const PixelPos &GetTopLeftPos() const { return TopLeftPos;}
	const PixelPos &GetBottomRightPos() const { return BottomRightPos;}
private:
	/// Set the current map view to x,y(upper,left corner)
	void Set(const PixelPos &mapPixelPos);
	/// Adjust the offscreen map-render surface to the given render size
	void AdjustMapRenderSurface(const PixelSize &renderSize);
	/// Free the offscreen map-render surface
	void CleanMapRenderSurface();
	/// Draw the map grid for debug purposes
	void DrawMapGridInViewport() const;
	/** Draw the map background.
	 * The template parameter graphicalTileIsLogicalTile selects the specialization.
	 * Drawing maps where graphical and logical tile sizes differ implies some extra
	 * work, so the caller should pass this as a compile-time flag to select the
	 * specialized variant of the method.
	 */
	template<bool graphicalTileIsLogicalTile>
	void DrawMapBackgroundInViewport(const fieldHighlightChecker highlightChecker = nullptr,
									 float drawZoom = 1.0f) const;
	/// Crisp-zoom (A2) world render: draws terrain crisp at full on-screen size plus scaled
	/// units/missiles/particles/fog into the offscreen, then copies it 1:1 to the screen.
	/// Only invoked when EnableCrispZoom is on AND the viewport is zoomed.
	void DrawCrispWorld(const fieldHighlightChecker highlightChecker,
						bool drawBuildCursor, const Vec2i &buildCursorTile);
	/// Draw the map fog of war
	void DrawMapFogOfWar();
	/// Adjust fog of war surface to viewport
	void AdjustFogSurface();
	/// Clean fog of war texture
	void CleanFog();

public:
	//private:
	PixelPos TopLeftPos;      /// Screen pixel top-left corner
	PixelPos BottomRightPos;  /// Screen pixel bottom-right corner

public:
	Vec2i MapPos;             /// Map tile left-upper corner
	PixelDiff Offset;         /// Offset within MapX, MapY
	int MapWidth = 0;             /// Width in map tiles
	int MapHeight = 0;            /// Height in map tiles

	/// Magnification of the map inside this viewport. 1.0 renders the world
	/// directly to the screen (default, unchanged behaviour). Values > 1 render
	/// the world into a smaller offscreen surface that is then upscaled into the
	/// on-screen rectangle, so the play area appears larger while the surrounding
	/// HUD, menus, fonts and cursors stay at native resolution.
	float Zoom = 1.0f;

	CUnit *Unit = nullptr;        /// Bound to this unit

	/// GPU-pipeline: the zoom offscreen surface (or null when Zoom==1). Used to gate the GPU
	/// world-draw path — tiles/units draw on the GPU only when the render target is the real
	/// screen, not this offscreen (zoom/crisp paths keep the CPU blit).
	SDL_Surface *GetMapRenderSurface() const { return MapRenderSurface; }
private:
	SDL_Surface *FogSurface { nullptr }; /// Texture for fog of war. Viewport sized.
	/// GPU-pipeline (Phase 2): streaming GPU texture holding the per-frame upload of FogSurface.
	/// Used only on the on-screen GPU render path (fog composited on the backbuffer over the world,
	/// under the CPU HUD overlay). Recreated when the fog surface size changes; null on the CPU path.
	SDL_Texture *FogTexture { nullptr };
	int FogTextureWidth { 0 };
	int FogTextureHeight { 0 };
	SDL_Surface *MapRenderSurface { nullptr }; /// Offscreen map render target when Zoom != 1.

	static bool ShowGrid;
	static bool ShowAStarPassability;
};

//@}

#endif // VIEWPORT_H
