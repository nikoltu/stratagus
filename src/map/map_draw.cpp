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
/**@name map_draw.cpp - The map drawing. */
//
//      (c) Copyright 1999-2005 by Lutz Sammer and Jimmy Salmon
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

#include "stratagus.h"

#include "viewport.h"

#include "font.h"
#include "fow.h"
#include "iolib.h"
#include "map.h"
#include "missile.h"
#include "particle.h"
#include "pathfinder.h"
#include "player.h"
#include "tileset.h"
#include "unit.h"
#include "unittype.h"
#include "ui.h"
#include "video.h"
#include "editor.h"

#include <cmath>
#include <cstdlib>

bool CViewport::ShowGrid = false;
bool CViewport::ShowAStarPassability = false;

/**
**  Crisp-zoom: lazily load the higher-resolution (2x) tile graphic the first time the crisp
**  path needs it. The HD art is a sibling of the tileset image with an "_hd" suffix, at twice
**  the graphical tile size (e.g. 128px/tile, 2048x3072 for the classic 1024x1536/64px sheet,
**  same 16-tiles-per-row slot order so the frame index is identical). If the file is absent we
**  leave Map.HDTileGraphic null and the crisp terrain draw scales the plain 64px TileGraphic
**  instead, so nothing breaks when the art has not shipped yet. Attempted at most once per map.
*/
static void EnsureHDTileGraphic()
{
	if (Map.HDTileGraphicTried) {
		return;
	}
	Map.HDTileGraphicTried = true;

	std::string hdFile = Map.Tileset.ImageFile;
	const auto dot = hdFile.rfind(".png");
	if (dot == std::string::npos) {
		return;
	}
	hdFile.insert(dot, "_hd");

	// Same file-access guard used elsewhere before loading optional art.
	if (!CanAccessFile(LibraryFileName(hdFile).c_str())) {
		return;
	}

	const PixelSize gts = Map.Tileset.getPixelTileSize();
	// ForceNew (not New) so we never collide with a cached graphic loaded at a different
	// frame size, matching how Map.TileGraphic itself is created.
	Map.HDTileGraphic = CGraphic::ForceNew(hdFile, gts.x * 2, gts.y * 2);
	Map.HDTileGraphic->Load();
}

CViewport::~CViewport()
{
	this->Clean();
}

bool CViewport::Contains(const PixelPos &screenPos) const
{
	return this->GetTopLeftPos().x <= screenPos.x && screenPos.x <= this->GetBottomRightPos().x
		   && this->GetTopLeftPos().y <= screenPos.y && screenPos.y <= this->GetBottomRightPos().y;
}


void CViewport::Restrict(int &screenPosX, int &screenPosY) const
{
	clamp(&screenPosX, this->GetTopLeftPos().x, this->GetBottomRightPos().x - 1);
	clamp(&screenPosY, this->GetTopLeftPos().y, this->GetBottomRightPos().y - 1);
}

PixelSize CViewport::GetPixelSize() const
{
	return this->BottomRightPos - this->TopLeftPos;
}

PixelSize CViewport::GetRenderPixelSize() const
{
	const PixelSize size = this->GetPixelSize();
	if (this->Zoom == 1.0f) {
		return size;
	}
	return PixelSize(static_cast<int>(std::lround(size.x / this->Zoom)),
	                 static_cast<int>(std::lround(size.y / this->Zoom)));
}

void CViewport::SetClipping() const
{
	::SetClipping(this->TopLeftPos.x, this->TopLeftPos.y, this->BottomRightPos.x, this->BottomRightPos.y);
}

/**
**  Check if any part of an area is visible in a viewport.
**
**  @param boxmin  map tile position of area in map to be checked.
**  @param boxmax  map tile position of area in map to be checked.
**
**  @return    True if any part of area is visible, false otherwise
*/
bool CViewport::AnyMapAreaVisibleInViewport(const Vec2i &boxmin, const Vec2i &boxmax) const
{
	Assert(boxmin.x <= boxmax.x && boxmin.y <= boxmax.y);

	if (boxmax.x < this->MapPos.x
		|| boxmax.y < this->MapPos.y
		|| boxmin.x >= this->MapPos.x + this->MapWidth
		|| boxmin.y >= this->MapPos.y + this->MapHeight) {
		return false;
	}
	return true;
}

bool CViewport::IsAreaVisibleInViewport(const PixelPos &areaScreenPos, 
										const PixelSize &areaSize) const
{
	if (areaScreenPos.x >= this->GetBottomRightPos().x
		|| areaScreenPos.y >= this->GetBottomRightPos().y
		|| areaScreenPos.x + areaSize.x < this->GetTopLeftPos().x
		|| areaScreenPos.y + areaSize.y < this->GetTopLeftPos().y) {
		return false;
	}
	return true;
}

bool CViewport::IsInsideMapArea(const PixelPos &screenPixelPos) const
{
	const Vec2i tilePos = ScreenToTilePos(screenPixelPos);

	return Map.Info.IsPointOnMap(tilePos);
}

// Convert viewport coordinates into map pixel coordinates
PixelPos CViewport::ScreenToMapPixelPos(const PixelPos &screenPixelPos) const
{
	// The on-screen distance from the viewport corner is Zoom times the map
	// distance, so divide it out to recover map-pixel coordinates.
	PixelDiff relPos = screenPixelPos - this->TopLeftPos;
	if (this->Zoom != 1.0f) {
		relPos.x = static_cast<int>(std::lround(relPos.x / this->Zoom));
		relPos.y = static_cast<int>(std::lround(relPos.y / this->Zoom));
	}
	relPos += this->Offset;
	const PixelPos mapPixelPos = relPos + Map.TilePosToMapPixelPos_TopLeft(this->MapPos);

	return mapPixelPos;
}

// Convert map pixel coordinates into viewport coordinates
PixelPos CViewport::MapToScreenPixelPos(const PixelPos &mapPixelPos) const
{
	PixelDiff relPos = mapPixelPos - Map.TilePosToMapPixelPos_TopLeft(this->MapPos) - this->Offset;
	if (this->Zoom != 1.0f) {
		relPos.x = static_cast<int>(std::lround(relPos.x * this->Zoom));
		relPos.y = static_cast<int>(std::lround(relPos.y * this->Zoom));
	}
	return this->TopLeftPos + relPos;
}

/// convert screen coordinate into tilepos
Vec2i CViewport::ScreenToTilePos(const PixelPos &screenPixelPos) const
{
	const PixelPos mapPixelPos = ScreenToMapPixelPos(screenPixelPos);
	const Vec2i tilePos = Map.MapPixelPosToTilePos(mapPixelPos);

	return tilePos;
}

/// convert tilepos coordinates into screen (take the top left of the tile)
PixelPos CViewport::TilePosToScreen_TopLeft(const Vec2i &tilePos) const
{
	const PixelPos mapPos = Map.TilePosToMapPixelPos_TopLeft(tilePos);

	return MapToScreenPixelPos(mapPos);
}

/// convert tilepos coordinates into screen (take the center of the tile)
PixelPos CViewport::TilePosToScreen_Center(const Vec2i &tilePos) const
{
	const PixelPos topLeft = TilePosToScreen_TopLeft(tilePos);

	// Half a tile expressed in on-screen pixels grows with the zoom factor.
	PixelDiff halfTile = PixelTileSize / 2;
	if (this->Zoom != 1.0f) {
		halfTile.x = static_cast<int>(std::lround(halfTile.x * this->Zoom));
		halfTile.y = static_cast<int>(std::lround(halfTile.y * this->Zoom));
	}
	return topLeft + halfTile;
}

/**
**  Change viewpoint of map viewport v to tilePos.
**
**  @param tilePos  map tile position.
**  @param offset   offset in tile.
*/
void CViewport::Set(const PixelPos &mapPos)
{
	int x = mapPos.x;
	int y = mapPos.y;

	x = std::max(x, -UI.MapArea.ScrollPaddingLeft);
	y = std::max(y, -UI.MapArea.ScrollPaddingTop);

	// The amount of the world actually shown is the on-screen size divided by
	// the zoom factor, so all scroll limits and the tiles-in-view derivation are
	// computed from the render size rather than the raw on-screen size.
	const PixelSize pixelSize = this->GetRenderPixelSize();
	x = std::min(x, Map.Info.MapWidth * PixelTileSize.x - (pixelSize.x) - 1 + UI.MapArea.ScrollPaddingRight);
	y = std::min(y, Map.Info.MapHeight * PixelTileSize.y - (pixelSize.y) - 1 + UI.MapArea.ScrollPaddingBottom);

	this->MapPos.x = x / PixelTileSize.x;
	if (x < 0 && x % PixelTileSize.x) {
		this->MapPos.x--;
	}
	this->MapPos.y = y / PixelTileSize.y;
	if (y < 0 && y % PixelTileSize.y) {
		this->MapPos.y--;
	}
	this->Offset.x = x % PixelTileSize.x;
	if (this->Offset.x < 0) {
		this->Offset.x += PixelTileSize.x;
	}
	this->Offset.y = y % PixelTileSize.y;
	if (this->Offset.y < 0) {
		this->Offset.y += PixelTileSize.y;
	}
	this->MapWidth = (pixelSize.x + this->Offset.x - 1) / PixelTileSize.x + 1;
	this->MapHeight = (pixelSize.y + this->Offset.y - 1) / PixelTileSize.y + 1;
}

/**
**  Change viewpoint of map viewport v to tilePos.
**
**  @param tilePos  map tile position.
**  @param offset   offset in tile.
*/
void CViewport::Set(const Vec2i &tilePos, const PixelDiff &offset)
{
	const PixelPos mapPixelPos = Map.TilePosToMapPixelPos_TopLeft(tilePos) + offset;

	this->Set(mapPixelPos);
}

/**
**  Center map viewport v on map tile (pos).
**
**  @param mapPixelPos     map pixel position.
*/
void CViewport::Center(const PixelPos &mapPixelPos)
{
	this->Set(mapPixelPos - this->GetRenderPixelSize() / 2);
}

/**
**  Draw the map backgrounds.
**
** StephanR: variables explained below for screen:<PRE>
** *---------------------------------------*
** |                                       |
** |        *-----------------------*      |<-TheUi.MapY,dy (in pixels)
** |        |   |   |   |   |   |   |      |        |
** |        |   |   |   |   |   |   |      |        |
** |        |---+---+---+---+---+---|      |        |
** |        |   |   |   |   |   |   |      |        |MapHeight (in tiles)
** |        |   |   |   |   |   |   |      |        |
** |        |---+---+---+---+---+---|      |        |
** |        |   |   |   |   |   |   |      |        |
** |        |   |   |   |   |   |   |      |        |
** |        *-----------------------*      |<-ey,UI.MapEndY (in pixels)
** |                                       |
** |                                       |
** *---------------------------------------*
**          ^                       ^
**        dx|-----------------------|ex,UI.MapEndX (in pixels)
**            UI.MapX MapWidth (in tiles)
** (in pixels)
** </PRE>
*/
/// Draw the map grid for debug purposes
void CViewport::DrawMapGridInViewport() const
{
	int x0 = this->TopLeftPos.x - this->Offset.x;
	int y0 = this->TopLeftPos.y - this->Offset.y;

	for (int x = ((x0 % PixelTileSize.x != 0) ? x0 + PixelTileSize.x : x0) ; x <= this->BottomRightPos.x ; x += PixelTileSize.x) {
		Video.DrawLineClip(ColorDarkGray, {x, this->TopLeftPos.y - this->Offset.y}, {x, this->BottomRightPos.y});
	}
	for(int y = ((y0 % PixelTileSize.y != 0) ? y0 + PixelTileSize.y : y0) ; y <= this->BottomRightPos.y ; y += PixelTileSize.y) {
		Video.DrawLineClip(ColorDarkGray, {this->TopLeftPos.x - this->Offset.x, y}, {this->BottomRightPos.x, y});
	}
}

template<bool graphicalTileIsLogicalTile>
void CViewport::DrawMapBackgroundInViewport(const fieldHighlightChecker highlightChecker /* = nullptr */,
											float drawZoom /* = 1.0f */) const
{
	// GPU-pipeline (Phase 1, zoomed direct render): when this viewport is zoomed and we are drawing
	// straight to the backbuffer (not the CPU offscreen), the tile loop still steps by NATIVE tile
	// size (logical geometry) but each tile is placed & scaled to its zoomed on-screen rect below.
	// The loop's right/bottom bounds must then be the LOGICAL render extent, not the full on-screen
	// (zoomed) extent, otherwise the native-stepping loop would iterate ~Zoom× too many columns/rows
	// (the surplus fall off-screen and would only be discarded by the renderer clip). Capping to the
	// render size makes the native tile count exactly fill the zoomed viewport.
	const bool gpuZoomTiles = (TheScreen != this->MapRenderSurface) && (drawZoom == 1.0f)
	                          && (this->Zoom != 1.0f);
	int ex = this->BottomRightPos.x;
	int ey = this->BottomRightPos.y;
	if (gpuZoomTiles) {
		const PixelSize rs = this->GetRenderPixelSize();
		const PixelSize gts = Map.Tileset.getPixelTileSize();
		// +1 tile of slack so the partial tile straddling the right/bottom edge is always drawn
		// (its zoomed span then covers the last on-screen pixels); the overshoot is clipped by the
		// renderer clip rect set in CViewport::Draw.
		ex = this->TopLeftPos.x + rs.x + gts.x;
		ey = this->TopLeftPos.y + rs.y + gts.y;
	}
	int sy = this->MapPos.y;
	int dy = this->TopLeftPos.y - this->Offset.y;
	const int mapW = Map.Info.MapWidth;
	const int mapH = Map.Info.MapHeight;
	/// uninitialized when graphicalTileIsLogicalTile
	int logicalMapW;
	if constexpr(!graphicalTileIsLogicalTile) {
		logicalMapW = mapW << Map.Tileset.getLogicalToGraphicalTileSizeShift();
	}
	const int map_max = mapW * mapH;
	PixelSize graphicTileSize = Map.Tileset.getPixelTileSize();
	/// uninitialized when graphicalTileIsLogicalTile
	int graphicTileOffset;
	bool canShortcut;
	if constexpr(!graphicalTileIsLogicalTile) {
		graphicTileOffset = Map.Tileset.getLogicalToGraphicalTileSizeMultiplier();
		canShortcut = false;
	} else {
		graphicTileOffset = 1;
		// The shortcut skips painting terrain under non-visible tiles, relying on the fog pass
		// to cover them. When rendering into the zoomed offscreen surface (freshly cleared to
		// black each frame) there is no backing content, so a skipped tile the fog does not
		// fully cover leaks through as a black patch around units. Only take the shortcut when
		// drawing straight to the screen.
		canShortcut = (TheScreen != this->MapRenderSurface)
					  && (GameSettings.RevealMap == MapRevealModes::cHidden || FogOfWar->GetType() == FogOfWarTypes::cTiledLegacy)
					  && FogOfWar->GetType() != FogOfWarTypes::cEnhanced
					  && !ReplayRevealMap;
	}

	while (sy < 0) {
		if constexpr(graphicalTileIsLogicalTile) {
			++sy;
		} else {
			sy += graphicTileOffset;
		}
		dy += graphicTileSize.y;
	}
	if constexpr(!graphicalTileIsLogicalTile) {
		auto dv = std::div(sy * PixelTileSize.y, graphicTileSize.y);
		sy = dv.quot * graphicTileOffset;
		dy -= dv.rem;
	}
	sy *= mapW;

	// GPU-pipeline (Phase 1): route terrain tiles through the GPU texture path, but ONLY for the
	// native on-screen render (Zoom==1, drawing straight to the backbuffer rather than the zoomed
	// offscreen MapRenderSurface). Zoomed/crisp passes keep the CPU compositor. Each tile draw is
	// dispatched inside CGraphic::DrawFrameClip (paletted tiles still fall back to the CPU path so
	// palette colour-cycling animation keeps working).
	const bool prevGpuWorld = GpuWorldDrawActive;
	const float prevGpuZoom = GpuWorldDrawZoom;
	GpuWorldDrawActive = (TheScreen != this->MapRenderSurface) && (drawZoom == 1.0f);
	// Drive the destination-size scale of the GPU tile RenderCopy from this viewport's zoom (1.0 on
	// the classic native path => no change). Positions below are pre-scaled to the zoomed screen.
	GpuWorldDrawZoom = GpuWorldDrawActive ? this->Zoom : 1.0f;

	while (dy <= ey && sy < map_max) {
		int sx = this->MapPos.x + sy;
		int dx = this->TopLeftPos.x - this->Offset.x;
		if constexpr(!graphicalTileIsLogicalTile) {
			auto dv = std::div(sx * PixelTileSize.x, graphicTileSize.x);
			sx = dv.quot * graphicTileOffset;
			dx -= dv.rem;
		}
		while (dx <= ex && (sx - sy < mapW)) {
			if (sx - sy < 0 || (canShortcut && !FogOfWar->GetVisibilityForTile(Vec2i(sx % mapW, sx / mapW)))) {
				if constexpr(graphicalTileIsLogicalTile) {
					++sx;
				} else {
					sx += graphicTileOffset;
				}
				dx += graphicTileSize.x;
				continue;
			}
			const CMapField &mf = Map.Fields[sx];
			unsigned short int tile;
			if (ReplayRevealMap) {
				tile = mf.getGraphicTile();
			} else {
				tile = mf.playerInfo.SeenTile;
			}
			if (drawZoom != 1.0f) {
				// Crisp-zoom terrain: dx/dy are LOGICAL positions (this pass runs with
				// origin 0 and Zoom-1 stepping, exactly like the classic path), so multiply
				// by drawZoom to reach the on-screen position and scale each tile to its
				// on-screen size. Deriving width/height from consecutive rounded tile edges
				// makes adjacent tiles abut seamlessly (no 1px gaps from rounding position
				// and size independently). Prefer the HD sheet; fall back to the 64px tiles.
				CGraphic *const src = Map.HDTileGraphic ? Map.HDTileGraphic.get()
													    : Map.TileGraphic.get();
				const int sx0 = static_cast<int>(std::lround(dx * drawZoom));
				const int sy0 = static_cast<int>(std::lround(dy * drawZoom));
				const int sx1 = static_cast<int>(std::lround((dx + graphicTileSize.x) * drawZoom));
				const int sy1 = static_cast<int>(std::lround((dy + graphicTileSize.y) * drawZoom));
				src->DrawFrameClipScaled(tile, sx0, sy0, sx1 - sx0, sy1 - sy0);
			} else if (gpuZoomTiles) {
				// Zoomed GPU direct render: dx,dy are logical (native-stepped) screen positions.
				// Derive both this tile's zoomed top-left AND its size from consecutive rounded grid
				// edges (the next tile's edge minus this one). At integer zoom the edges land on exact
				// multiples so the size equals PixelTileSize*Zoom, byte-identical to the old fixed
				// DrawFrameClipTex dst; at fractional zoom (e.g. Zoom*WorldRenderScale = 1.6) each
				// tile's right/bottom edge is exactly the next tile's left/top edge, so neighbours
				// abut with no 1px gap (which the black render target would otherwise show as a grid).
				const float z = this->Zoom;
				const int zx = this->TopLeftPos.x
				               + static_cast<int>(std::lround((dx - this->TopLeftPos.x) * z));
				const int zy = this->TopLeftPos.y
				               + static_cast<int>(std::lround((dy - this->TopLeftPos.y) * z));
				const int zx1 = this->TopLeftPos.x
				                + static_cast<int>(std::lround((dx + graphicTileSize.x - this->TopLeftPos.x) * z));
				const int zy1 = this->TopLeftPos.y
				                + static_cast<int>(std::lround((dy + graphicTileSize.y - this->TopLeftPos.y) * z));
				const SDL_Rect dst = { zx, zy, zx1 - zx, zy1 - zy };
				Map.TileGraphic->DrawFrameClipTexDst(tile, dst);
			} else {
				Map.TileGraphic->DrawFrameClip(tile, dx, dy);
			}
#ifdef DEBUG
			// AStar passability overlay
			if (CViewport::isPassabilityHighlighted() && Editor.Running == EditorNotRunning) {
				for (int i = 0; i < graphicTileOffset; i++) {
					for (int j = 0; j < graphicTileOffset; j++) {
						if (Map.Fields[sx + j + (mapW * i)].isFlag(MapFieldUnpassable)) {
							Video.FillTransRectangleClip(ColorRed, dx + j * PixelTileSize.x, dy + i * PixelTileSize.y, PixelTileSize.x, PixelTileSize.y, 32);
						} else {
							Video.FillTransRectangleClip(ColorGreen, dx + j * PixelTileSize.x, dy + i * PixelTileSize.y, PixelTileSize.x, PixelTileSize.y, 32);
						}

						if (Map.Fields[sx + j + (mapW * i)].isFlag(MapFieldLandUnit | MapFieldBuilding | MapFieldSeaUnit)) {
							Video.FillTransRectangleClip(ColorOrange, dx + j * PixelTileSize.x, dy + i * PixelTileSize.y, PixelTileSize.x, PixelTileSize.y, 32);
						}
					}
				}
			}
			DrawLastAStar(*this);
#endif
			/// Highlight layer if needed (editor stuff)
			if (highlightChecker && highlightChecker(mf)) {
				Video.FillTransRectangleClip(ColorRed, dx, dy, PixelTileSize.x, PixelTileSize.y, 64);
			}
			if constexpr(graphicalTileIsLogicalTile) {
				++sx;
			} else {
				sx += graphicTileOffset;
			}
			dx += graphicTileSize.x;
		}
		if constexpr(graphicalTileIsLogicalTile) {
			sy += mapW;
		} else {
			sy += logicalMapW;
		}
		dy += graphicTileSize.y;
	}
	GpuWorldDrawActive = prevGpuWorld;
	GpuWorldDrawZoom = prevGpuZoom;
	if (CViewport::isGridEnabled()) {
		DrawMapGridInViewport();
	}
}

/**
**  Show unit's name under cursor or print the message if territory is invisible.
**
**  @param pos  Mouse position.
**  @param unit  Unit to show name.
**  @param hidden  If true, write "Unrevealed terrain"
**
*/
static void ShowUnitName(const CViewport &vp, PixelPos pos, CUnit *unit, bool hidden = false)
{
	CFont &font = GetSmallFont();
	int width;
	int height = font.Height() + 6;
	CLabel label(font, "white", "red");
	int x;
	int y = std::min<int>(GameCursor->G->Height + pos.y + 10, vp.BottomRightPos.y - 1 - height);
	const CPlayer *tplayer = ThisPlayer;

	if (unit && unit->IsAliveOnMap()) {
		int backgroundColor;
		if (unit->Player->Index == (*tplayer).Index) {
			backgroundColor = Video.MapRGB(TheScreen->format, 0, 0, 252);
		} else if (unit->Player->IsAllied(*tplayer)) {
			backgroundColor = Video.MapRGB(TheScreen->format, 0, 176, 0);
		} else if (unit->Player->IsEnemy(*tplayer)) {
			backgroundColor = Video.MapRGB(TheScreen->format, 252, 0, 0);
		} else {
			backgroundColor = Video.MapRGB(TheScreen->format, 176, 176, 176);
		}
		width = font.getWidth(unit->Type->Name) + 10;
		x = std::min<int>(GameCursor->G->Width + pos.x, vp.BottomRightPos.x - 1 - width);
		Video.FillTransRectangle(backgroundColor, x, y, width, height, 128);
		Video.DrawRectangle(ColorWhite, x, y, width, height);
		label.DrawCentered(x + width / 2, y + 3, unit->Type->Name);
	} else if (hidden) {
		const std::string str("Unrevealed terrain");
		width = font.getWidth(str) + 10;
		x = std::min<int>(GameCursor->G->Width + pos.x, vp.BottomRightPos.x - 1 - width);
		Video.FillTransRectangle(ColorBlue, x, y, width, height, 128);
		Video.DrawRectangle(ColorWhite, x, y, width, height);
		label.DrawCentered(x + width / 2, y + 3, str);
	}
}

/**
**  Draw a map viewport.
*/
void CViewport::Draw(const fieldHighlightChecker highlightChecker /* = nullptr */)
{
	// When zoomed, the world is rendered into a smaller offscreen surface and
	// then upscaled into the on-screen rectangle. The viewport is temporarily
	// repositioned to that surface's origin with Zoom reset to 1, so every draw
	// call below (tiles, units, missiles, fog) targets the offscreen at native
	// scale without any per-call change.
	const bool zoomed = (this->Zoom != 1.0f);
	// Building-placement preview must scale with the world; when zoomed we render it inside the
	// offscreen (below) so it upscales identically to the placed building. Capture the tile now,
	// while the viewport still holds its real Zoom/origin.
	const bool drawBuildCursor = (this == UI.MouseViewport && CursorBuilding
	                              && CursorOn == ECursorOn::Map && zoomed);
	Vec2i buildCursorTile(0, 0);
	if (drawBuildCursor) {
		buildCursorTile = this->ScreenToTilePos(CursorScreenPos);
	}

	// Crisp-at-zoom (A2) path. Only diverges from the classic pipeline when the runtime flag
	// is on AND the viewport is actually zoomed; otherwise everything below is byte-for-byte
	// the original code. The crisp world render is fully self-contained in DrawCrispWorld and
	// leaves the viewport/clipping state exactly as the classic zoomed path does, so the
	// shared overlay drawing further down is identical for both branches.
	const bool crisp = EnableCrispZoom && zoomed;
	if (crisp) {
		EnsureHDTileGraphic();
		this->DrawCrispWorld(highlightChecker, drawBuildCursor, buildCursorTile);
	} else {

	SDL_Surface *savedScreen = nullptr;
	PixelPos savedTopLeft;
	PixelPos savedBottomRight;
	float savedZoom = this->Zoom;
	PixelSize renderSize;
	SDL_Rect dstRect;

	// GPU-pipeline (Phase 1, zoomed direct render): for the normal in-game viewport we draw the
	// zoomed world (tiles + unit sprites/shadows) straight to the backbuffer at ZOOMED size via the
	// GPU, bypassing the CPU offscreen (MapRenderSurface) + SoftStretch entirely. That offscreen was
	// what turned black once TheScreen became the translucent ARGB overlay. The offscreen path is
	// kept ONLY as a fallback for the editor (heavy direct-pixel TheScreen consumer). At Zoom==1 the
	// world already renders straight to the backbuffer, so gpuDirect only matters when zoomed.
	const bool gpuDirect = zoomed && (Editor.Running == EditorNotRunning);
	const bool useOffscreen = zoomed && !gpuDirect;

	// GPU-pipeline (Phase 5): world internal-resolution downscale. Only on the zoomed GPU-direct
	// path and only when WorldRenderScale < 1.0. The world (tiles/units/missiles/particles/fog) is
	// rendered into a persistent GPU target texture (WorldRT) at a fraction of the viewport render
	// size, then upscaled (linear) onto the on-screen viewport rect. HUD/order-lines/cursor draw
	// afterwards on the backbuffer at native res. At 1.0 this whole block is skipped and the world
	// renders straight to the backbuffer exactly as before (byte-identical).
	static SDL_Texture *WorldRT = nullptr;
	static int WorldRTW = 0;
	static int WorldRTH = 0;
	bool rtScaled = gpuDirect && TheRenderer && WorldRenderScale < 1.0f;
	SDL_Rect vpClipScreen = { 0, 0, 0, 0 };
	SDL_Rect rtUsedRect = { 0, 0, 0, 0 };
	SDL_Texture *savedRenderTarget = nullptr;
	if (rtScaled) {
		vpClipScreen.x = this->TopLeftPos.x;
		vpClipScreen.y = this->TopLeftPos.y;
		vpClipScreen.w = this->BottomRightPos.x - this->TopLeftPos.x + 1;
		vpClipScreen.h = this->BottomRightPos.y - this->TopLeftPos.y + 1;
		const int tw = std::max(1, (int)std::lround(vpClipScreen.w * WorldRenderScale));
		const int th = std::max(1, (int)std::lround(vpClipScreen.h * WorldRenderScale));
		if (!WorldRT || WorldRTW != tw || WorldRTH != th) {
			if (WorldRT) {
				SDL_DestroyTexture(WorldRT);
				WorldRT = nullptr;
			}
			WorldRT = SDL_CreateTexture(TheRenderer, SDL_PIXELFORMAT_ARGB8888,
										SDL_TEXTUREACCESS_TARGET, tw, th);
			if (WorldRT) {
				WorldRTW = tw;
				WorldRTH = th;
				SDL_SetTextureScaleMode(WorldRT, SDL_ScaleModeLinear);
				SDL_SetTextureBlendMode(WorldRT, SDL_BLENDMODE_NONE);
			} else {
				WorldRTW = WorldRTH = 0;
			}
		}
		if (WorldRT) {
			rtUsedRect = { 0, 0, tw, th };
			// Reposition the viewport onto the RT origin at Zoom*scale so every world draw (which
			// derives its screen position from TopLeftPos + Zoom*offset, and its GPU sprite/tile
			// size from GpuWorldDrawZoom == this->Zoom) lands scaled into the RT with no per-call
			// change. The renderer clip set below then becomes the full RT.
			savedTopLeft = this->TopLeftPos;
			savedBottomRight = this->BottomRightPos;
			this->TopLeftPos = PixelPos(0, 0);
			this->BottomRightPos = PixelPos(tw - 1, th - 1);
			this->Zoom = savedZoom * WorldRenderScale;
			savedRenderTarget = SDL_GetRenderTarget(TheRenderer);
			SDL_SetRenderTarget(TheRenderer, WorldRT);
			SDL_SetRenderDrawColor(TheRenderer, 0, 0, 0, 255);
			SDL_RenderClear(TheRenderer);
		} else {
			rtScaled = false; // texture creation failed: fall back to the direct backbuffer render
		}
	}

	if (useOffscreen) {
		const PixelSize realSize = this->GetPixelSize();
		renderSize = this->GetRenderPixelSize();
		this->AdjustMapRenderSurface(renderSize);

		dstRect.x = this->TopLeftPos.x;
		dstRect.y = this->TopLeftPos.y;
		dstRect.w = realSize.x;
		dstRect.h = realSize.y;

		savedTopLeft = this->TopLeftPos;
		savedBottomRight = this->BottomRightPos;
		this->TopLeftPos = PixelPos(0, 0);
		// BottomRightPos is the inclusive last pixel everywhere else in the engine, so the
		// rendered region is exactly the renderSize sub-rect (no one-past-the-edge column/row).
		this->BottomRightPos = PixelPos(renderSize.x - 1, renderSize.y - 1);
		this->Zoom = 1.0f;

		savedScreen = TheScreen;
		TheScreen = this->MapRenderSurface;
		// Only the renderSize sub-rect is drawn and upscaled; clearing the whole
		// framebuffer-sized surface each frame is wasted bandwidth (~4x at Zoom 2).
		SDL_Rect clearRect = { 0, 0, renderSize.x, renderSize.y };
		SDL_FillRect(this->MapRenderSurface, &clearRect, 0);
	}

	PushClipping();
	this->SetClipping();

	// GPU-pipeline (Phase 1): constrain the GPU world draws (tiles + unit sprites/shadows) to this
	// viewport's on-screen rectangle. The CPU compositor uses PushClipping/CLIP_RECTANGLE above;
	// the GPU RenderCopy path is unaffected by that and needs the renderer's own clip. Only set it
	// for the native on-screen render (not the zoomed offscreen, where world draws stay on CPU).
	// MUST be reset before RealizeVideoMemory copies the full-screen overlay, or that copy (and the
	// benchmark bar) would be clipped to the last viewport. Applies whenever world draws hit the
	// backbuffer directly: the classic Zoom==1 render AND the new zoomed GPU-direct render.
	const bool worldToScreen = !useOffscreen;
	if (worldToScreen) {
		SDL_Rect vpClip = { this->TopLeftPos.x, this->TopLeftPos.y,
							this->BottomRightPos.x - this->TopLeftPos.x + 1,
							this->BottomRightPos.y - this->TopLeftPos.y + 1 };
		SDL_RenderSetClipRect(TheRenderer, &vpClip);
	}

	// Scale the world GPU draws (tiles + unit sprites/shadows) to the on-screen zoom. On the
	// classic Zoom==1 path this->Zoom is 1.0 so this is a no-op; on the GPU-direct zoomed path it
	// makes every RenderCopy destination and every native sprite-centring offset scale up. Reset to
	// 1.0 before the CPU-overlay draws (fog/HUD/cursor) below. DrawMapBackgroundInViewport manages
	// its own copy of this around the tile pass; setting it here covers the unit/shadow draws.
	const float savedGpuZoom = GpuWorldDrawZoom;
	if (worldToScreen) {
		GpuWorldDrawZoom = this->Zoom;
	}

	/* this may take while */
	if (Map.Tileset.getLogicalToGraphicalTileSizeShift() > 0) {
		this->DrawMapBackgroundInViewport<false>(highlightChecker);
	} else {
		this->DrawMapBackgroundInViewport<true>(highlightChecker);
	}

	Missile *clickMissile = nullptr;
	CurrentViewport = this;
	{
		// Now we need to sort units, missiles, particles by draw level and draw them
		const std::vector<CUnit *> unittable = FindAndSortUnits(*this);
		const std::vector<Missile *> missiletable = FindAndSortMissiles(*this);
		const std::vector<CParticle *> particletable = ParticleManager.prepareToDraw(*this);

		const size_t nunits = unittable.size();
		const size_t nmissiles = missiletable.size();
		const size_t nparticles = particletable.size();

		size_t i = 0;
		size_t j = 0;
		size_t k = 0;


		while ((i < nunits && j < nmissiles) || (i < nunits && k < nparticles)
			   || (j < nmissiles && k < nparticles)) {
			if (i == nunits) {
				if (missiletable[j]->Type->DrawLevel < particletable[k]->getDrawLevel()) {
					missiletable[j]->DrawMissile(*this);
					if (clickMissile == nullptr && missiletable[j]->Type->Ident == ClickMissile) {
						clickMissile = missiletable[j];
					}
					++j;
				} else {
					particletable[k]->draw();
					++k;
				}
			} else if (j == nmissiles) {
				if (unittable[i]->GetDrawLevel() < particletable[k]->getDrawLevel()) {
					unittable[i]->Draw(*this);
					++i;
				} else {
					particletable[k]->draw();
					++k;
				}
			} else if (k == nparticles) {
				if (unittable[i]->GetDrawLevel() < missiletable[j]->Type->DrawLevel) {
					unittable[i]->Draw(*this);
					++i;
				} else {
					missiletable[j]->DrawMissile(*this);
					if (clickMissile == nullptr && missiletable[j]->Type->Ident == ClickMissile) {
						clickMissile = missiletable[j];
					}
					++j;
				}
			} else {
				if (unittable[i]->GetDrawLevel() <= missiletable[j]->Type->DrawLevel) {
					if (unittable[i]->GetDrawLevel() < particletable[k]->getDrawLevel()) {
						unittable[i]->Draw(*this);
						++i;
					} else {
						particletable[k]->draw();
						++k;
					}
				} else {
					if (missiletable[j]->Type->DrawLevel < particletable[k]->getDrawLevel()) {
						missiletable[j]->DrawMissile(*this);
						if (clickMissile == nullptr && missiletable[j]->Type->Ident == ClickMissile) {
							clickMissile = missiletable[j];
						}
						++j;
					} else {
						particletable[k]->draw();
						++k;
					}
				}
			}
		}
		for (; i < nunits; ++i) {
			unittable[i]->Draw(*this);
		}
		for (; j < nmissiles; ++j) {
			missiletable[j]->DrawMissile(*this);
			if (clickMissile == nullptr && missiletable[j]->Type->Ident == ClickMissile) {
				clickMissile = missiletable[j];
			}
		}
		for (; k < nparticles; ++k) {
			particletable[k]->draw();
		}
		ParticleManager.endDraw();
	}

	// GPU-pipeline (Phase 1): world GPU draws for this viewport are done; drop the renderer clip so
	// the fog/HUD CPU overlay and the full-screen overlay copy in RealizeVideoMemory are not
	// clipped to this viewport. (Fog/missiles/particles above already drew on the CPU overlay.)
	if (worldToScreen) {
		SDL_RenderSetClipRect(TheRenderer, nullptr);
	}
	// World GPU draws are done; the fog/HUD/cursor CPU overlay below composites at native scale
	// (uploaded 1:1 over the whole screen), so drop the world zoom before it.
	GpuWorldDrawZoom = savedGpuZoom;

	/// Draw Fog of War
	this->DrawMapFogOfWar();

	if (rtScaled) {
		// World (tiles/units/missiles/particles/fog) is complete in WorldRT. Restore the real
		// viewport placement + CPU clip, drop the render target back to the backbuffer, and upscale
		// the RT into the on-screen viewport rect (linear). clickMissile/buildCursor/overlays below
		// then draw on the backbuffer at native res, identical to the direct GPU path.
		PopClipping();
		this->TopLeftPos = savedTopLeft;
		this->BottomRightPos = savedBottomRight;
		this->Zoom = savedZoom;
		SDL_SetRenderTarget(TheRenderer, savedRenderTarget);
		SDL_RenderCopy(TheRenderer, WorldRT, &rtUsedRect, &vpClipScreen);
		PushClipping();
		this->SetClipping();
	}

	// If there was a click missile, draw it again here above the fog
	if (clickMissile != nullptr) {
		Vec2i pos = Map.MapPixelPosToTilePos(clickMissile->position);
		Map.Clamp(pos);
		if (Map.Field(pos.x, pos.y)->playerInfo.TeamVisibilityState(*ThisPlayer) != 2) {
			// if this tile is not visible, we want to draw the click on top of
			// the fog again
			clickMissile->DrawMissile(*this);
		}
	}

	if (drawBuildCursor) {
		DrawBuildingCursorViewport(*this, buildCursorTile);
	}

	if (useOffscreen) {
		// Map content is complete in the offscreen surface: restore the real
		// screen and viewport placement, then upscale the offscreen into the
		// on-screen rectangle. Overlays below (order lines, unit-name popup,
		// border) are drawn afterwards so they stay at native resolution.
		PopClipping();
		TheScreen = savedScreen;
		this->TopLeftPos = savedTopLeft;
		this->BottomRightPos = savedBottomRight;
		this->Zoom = savedZoom;

		SDL_Rect srcRect;
		srcRect.x = 0;
		srcRect.y = 0;
		srcRect.w = renderSize.x;
		srcRect.h = renderSize.y;
		// Both surfaces share the same 32bpp no-alpha layout. Use the bilinear (Linear) upscale
		// instead of nearest: it removes the blocky stair-step and the sub-pixel-scroll shimmer
		// when the player pinch-zooms in. The HD art is 64px/tile, so this reads as smooth rather
		// than razor-crisp (true crispness needs higher-res source art), but it is a clear, cheap
		// win over nearest. Same-format keeps SDL's fast path.
		SDL_SoftStretchLinear(this->MapRenderSurface, &srcRect, TheScreen, &dstRect);

		PushClipping();
		this->SetClipping();
	}

	} // end of classic (non-crisp) world render branch

	//
	// Draw orders of selected units.
	// Drawn here so that they are shown even when the unit is out of the screen.
	// While a unit is selected its order path (the move/attack line to the target)
	// is drawn every frame, so a touch move order shows a clear, persistent line to
	// the destination for the whole move instead of a brief flash. The timed/Shift
	// path still applies to non-selected feedback.
	//
	if (Preference.ShowOrders) {
		// GPU-pipeline (Phase 4b): the persistent order line + its endpoint dots are map-area
		// overlays; issue them as GPU primitives (DrawLineClip / FillCircleClip) on the backbuffer
		// like the selection boxes/circles, instead of the CPU TheScreen overlay. The world render
		// above already restored GpuWorldDrawActive to false and dropped the renderer clip, so
		// re-establish the viewport clip + active scope for the ShowOrder loop, then restore. Only
		// when this frame's world actually landed on the backbuffer (Zoom==1 or the zoomed
		// GPU-direct render); the editor/crisp offscreen paths keep the order line on the CPU
		// overlay so it composites with their TheScreen world.
		const bool zoomed = (this->Zoom != 1.0f);
		const bool crisp = EnableCrispZoom && zoomed;
		const bool gpuDirect = zoomed && (Editor.Running == EditorNotRunning);
		const bool orderOnGpu = TheRenderer && !crisp && (!zoomed || gpuDirect);

		bool prevGpuWorld = GpuWorldDrawActive;
		float prevGpuZoom = GpuWorldDrawZoom;
		SDL_Rect prevClip;
		bool prevClipEnabled = false;
		if (orderOnGpu) {
			prevClipEnabled = (SDL_RenderIsClipEnabled(TheRenderer) == SDL_TRUE);
			SDL_RenderGetClipRect(TheRenderer, &prevClip);
			SDL_Rect vpClip = { this->TopLeftPos.x, this->TopLeftPos.y,
								this->BottomRightPos.x - this->TopLeftPos.x + 1,
								this->BottomRightPos.y - this->TopLeftPos.y + 1 };
			SDL_RenderSetClipRect(TheRenderer, &vpClip);
			GpuWorldDrawActive = true;
			GpuWorldDrawZoom = this->Zoom;
		}

		for (const CUnit *unit : Selected) {
			ShowOrder(*unit);
		}

		if (orderOnGpu) {
			GpuWorldDrawActive = prevGpuWorld;
			GpuWorldDrawZoom = prevGpuZoom;
			SDL_RenderSetClipRect(TheRenderer, prevClipEnabled ? &prevClip : nullptr);
		}
	}

	//
	// Draw unit's name popup
	//
	if (CursorOn == ECursorOn::Map && Preference.ShowNameDelay && (ShowNameDelay < GameCycle) && (GameCycle < ShowNameTime)) {
		const Vec2i tilePos = this->ScreenToTilePos(CursorScreenPos);
		const bool isMapFieldVisible = Map.Field(tilePos)->playerInfo.IsTeamVisible(*ThisPlayer);

		if (UI.MouseViewport->IsInsideMapArea(CursorScreenPos) && UnitUnderCursor
			&& ((isMapFieldVisible && !UnitUnderCursor->Type->BoolFlag[ISNOTSELECTABLE_INDEX].value) || ReplayRevealMap)) {
			ShowUnitName(*this, CursorScreenPos, UnitUnderCursor);
		} else if (!isMapFieldVisible) {
			ShowUnitName(*this, CursorScreenPos, nullptr, true);
		}
	}

	DrawBorder();
	PopClipping();
}

/**
**  Crisp-at-zoom (A2) world render.
**
**  Renders the whole world into the offscreen surface at FULL on-screen size so the final
**  copy back to the screen is 1:1 (no soft upscale):
**   - Terrain is drawn crisp: the tile loop runs with logical geometry (origin 0, Zoom 1,
**     the classic stepping) but each tile is placed & scaled to on-screen size from the HD
**     tile sheet (or the 64px sheet scaled up when HD art is absent).
**   - Units / missiles / particles are drawn with the REAL Zoom kept, so the Zoom-aware
**     viewport transforms place them at on-screen ·Zoom positions; their sprite blits are
**     scaled up to on-screen size by the CrispZoomDrawScale fast paths in CGraphic. This is
**     "soft but correct" for now (higher-res unit art is a later phase). Direct-pixel
**     decorations (health bars, selection boxes) are positioned correctly but not enlarged.
**   - Fog is composited by the crisp branch inside DrawMapFogOfWar.
**
**  On return the viewport/screen/clipping state matches the classic zoomed path exactly (one
**  clipping level left pushed for the shared overlays that Draw() renders afterwards).
*/
void CViewport::DrawCrispWorld(const fieldHighlightChecker highlightChecker,
							   bool drawBuildCursor, const Vec2i &buildCursorTile)
{
	const float z = this->Zoom;
	const PixelSize realSize = this->GetPixelSize();        // full on-screen viewport size
	const PixelSize renderSize = this->GetRenderPixelSize(); // logical size (== realSize / z)

	this->AdjustMapRenderSurface(renderSize);              // allocates the full-framebuffer offscreen

	const PixelPos savedTopLeft = this->TopLeftPos;
	const PixelPos savedBottomRight = this->BottomRightPos;
	const float savedZoom = this->Zoom;
	SDL_Surface *const savedScreen = TheScreen;

	// Redirect all world drawing into the offscreen. We paint it at FULL on-screen size.
	TheScreen = this->MapRenderSurface;

	// Clear only the realSize sub-rect we actually paint & copy back.
	SDL_Rect clearRect = { 0, 0, realSize.x, realSize.y };
	SDL_FillRect(this->MapRenderSurface, &clearRect, 0);

	// The scaled sprite/tile blits use SDL_BlitScaled, which clips to the destination
	// surface's clip_rect (NOT the engine CLIP_RECTANGLE). Constrain it to the painted
	// region so nothing spills outside this viewport (e.g. split-screen neighbours).
	SDL_Rect savedClip;
	SDL_GetClipRect(this->MapRenderSurface, &savedClip);
	SDL_Rect crispClip = { 0, 0, realSize.x, realSize.y };
	SDL_SetClipRect(this->MapRenderSurface, &crispClip);

	// ---- Phase A: crisp terrain (logical geometry + scaled draw) ----
	this->TopLeftPos = PixelPos(0, 0);
	this->BottomRightPos = PixelPos(renderSize.x - 1, renderSize.y - 1);
	this->Zoom = 1.0f;
	PushClipping();
	// Keep the engine clip permissive over the full painted region; the tiles clip via the
	// surface clip_rect set above.
	::SetClipping(0, 0, realSize.x - 1, realSize.y - 1);
	if (Map.Tileset.getLogicalToGraphicalTileSizeShift() > 0) {
		this->DrawMapBackgroundInViewport<false>(highlightChecker, z);
	} else {
		this->DrawMapBackgroundInViewport<true>(highlightChecker, z);
	}
	PopClipping();

	// ---- Phase B: units / missiles / particles at REAL zoom ----
	this->TopLeftPos = PixelPos(0, 0);
	this->BottomRightPos = PixelPos(realSize.x - 1, realSize.y - 1);
	this->Zoom = z; // Zoom-aware transforms now place sprites at on-screen ·z positions
	PushClipping();
	this->SetClipping();

	CurrentViewport = this;
	Missile *clickMissile = nullptr;

	// Turn on the per-blit up-scale so unit/missile/particle sprites reach on-screen size.
	CrispZoomDrawScale = z;
	{
		const std::vector<CUnit *> unittable = FindAndSortUnits(*this);
		const std::vector<Missile *> missiletable = FindAndSortMissiles(*this);
		const std::vector<CParticle *> particletable = ParticleManager.prepareToDraw(*this);

		const size_t nunits = unittable.size();
		const size_t nmissiles = missiletable.size();
		const size_t nparticles = particletable.size();

		size_t i = 0;
		size_t j = 0;
		size_t k = 0;

		while ((i < nunits && j < nmissiles) || (i < nunits && k < nparticles)
			   || (j < nmissiles && k < nparticles)) {
			if (i == nunits) {
				if (missiletable[j]->Type->DrawLevel < particletable[k]->getDrawLevel()) {
					missiletable[j]->DrawMissile(*this);
					if (clickMissile == nullptr && missiletable[j]->Type->Ident == ClickMissile) {
						clickMissile = missiletable[j];
					}
					++j;
				} else {
					particletable[k]->draw();
					++k;
				}
			} else if (j == nmissiles) {
				if (unittable[i]->GetDrawLevel() < particletable[k]->getDrawLevel()) {
					unittable[i]->Draw(*this);
					++i;
				} else {
					particletable[k]->draw();
					++k;
				}
			} else if (k == nparticles) {
				if (unittable[i]->GetDrawLevel() < missiletable[j]->Type->DrawLevel) {
					unittable[i]->Draw(*this);
					++i;
				} else {
					missiletable[j]->DrawMissile(*this);
					if (clickMissile == nullptr && missiletable[j]->Type->Ident == ClickMissile) {
						clickMissile = missiletable[j];
					}
					++j;
				}
			} else {
				if (unittable[i]->GetDrawLevel() <= missiletable[j]->Type->DrawLevel) {
					if (unittable[i]->GetDrawLevel() < particletable[k]->getDrawLevel()) {
						unittable[i]->Draw(*this);
						++i;
					} else {
						particletable[k]->draw();
						++k;
					}
				} else {
					if (missiletable[j]->Type->DrawLevel < particletable[k]->getDrawLevel()) {
						missiletable[j]->DrawMissile(*this);
						if (clickMissile == nullptr && missiletable[j]->Type->Ident == ClickMissile) {
							clickMissile = missiletable[j];
						}
						++j;
					} else {
						particletable[k]->draw();
						++k;
					}
				}
			}
		}
		for (; i < nunits; ++i) {
			unittable[i]->Draw(*this);
		}
		for (; j < nmissiles; ++j) {
			missiletable[j]->DrawMissile(*this);
			if (clickMissile == nullptr && missiletable[j]->Type->Ident == ClickMissile) {
				clickMissile = missiletable[j];
			}
		}
		for (; k < nparticles; ++k) {
			particletable[k]->draw();
		}
		ParticleManager.endDraw();
	}
	// Fog composites with real alpha over the terrain, not through the sprite fast path.
	CrispZoomDrawScale = 1.0f;

	/// Draw Fog of War (crisp branch inside handles the scaling)
	this->DrawMapFogOfWar();

	// Overlays that are part of the world (click missile above fog, building placement
	// preview) are scaled like the rest of the world content.
	CrispZoomDrawScale = z;
	if (clickMissile != nullptr) {
		Vec2i pos = Map.MapPixelPosToTilePos(clickMissile->position);
		Map.Clamp(pos);
		if (Map.Field(pos.x, pos.y)->playerInfo.TeamVisibilityState(*ThisPlayer) != 2) {
			clickMissile->DrawMissile(*this);
		}
	}
	if (drawBuildCursor) {
		DrawBuildingCursorViewport(*this, buildCursorTile);
	}
	CrispZoomDrawScale = 1.0f;

	PopClipping();

	// Restore the real screen/viewport and copy the finished offscreen 1:1 to the screen.
	TheScreen = savedScreen;
	this->TopLeftPos = savedTopLeft;
	this->BottomRightPos = savedBottomRight;
	this->Zoom = savedZoom;
	SDL_SetClipRect(this->MapRenderSurface, &savedClip);

	SDL_Rect srcRect = { 0, 0, realSize.x, realSize.y };
	SDL_Rect dstRect = { savedTopLeft.x, savedTopLeft.y, realSize.x, realSize.y };
	// Offscreen is already on-screen size: exact 1:1 copy, no resampling.
	SDL_BlitSurface(this->MapRenderSurface, &srcRect, TheScreen, &dstRect);

	// Leave one clipping level pushed for the shared overlays drawn by Draw().
	PushClipping();
	this->SetClipping();
}

/**
**  Draw border around the viewport
*/
void CViewport::DrawBorder() const
{
	// if we a single viewport, no need to denote the "selected" one
	if (UI.NumViewports == 1) {
		return;
	}

	Uint32 color = ColorBlack;
	if (this == UI.SelectedViewport) {
		color = ColorOrange;
	}

	const PixelSize pixelSize = this->GetPixelSize();
	Video.DrawRectangle(color, this->TopLeftPos.x, this->TopLeftPos.y, pixelSize.x + 1, pixelSize.y + 1);
}

/**
**  Allocate (or reallocate on size change) the offscreen surface the world is
**  rendered into when the viewport is zoomed. Sized to the largest render size
**  it has been asked for, so a shared surface also covers split-screen layouts.
*/
void CViewport::AdjustMapRenderSurface(const PixelSize &renderSize)
{
	// The direct-pixel line/rectangle primitives (health/mana bars, selection
	// corners, grid) index the target surface as x + y * Video.Width, so the
	// offscreen surface MUST share that exact stride. Allocate it at the full
	// framebuffer size and render only into the top-left renderSize sub-rectangle
	// (which is then upscaled); a narrower surface would make those primitives
	// write past the row and corrupt/segfault. renderSize never exceeds the
	// framebuffer, so the sub-rectangle always fits.
	(void)renderSize;
	if (this->MapRenderSurface
		&& this->MapRenderSurface->w == Video.Width
		&& this->MapRenderSurface->h == Video.Height) {
		return;
	}

	this->CleanMapRenderSurface();
	// Match TheScreen's pixel layout exactly (no alpha mask). A mismatched alpha mask forces
	// the per-frame upscale onto SDL's slow per-pixel converting blit; the same 32bpp no-alpha
	// layout lets it take the fast same-format stretch. Stride stays Video.Width so the
	// direct-pixel primitives that index x + y * Video.Width remain in bounds.
	this->MapRenderSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, Video.Width, Video.Height, 32,
	                                              RMASK, GMASK, BMASK, 0);
	SDL_SetSurfaceBlendMode(this->MapRenderSurface, SDL_BLENDMODE_NONE);
}

void CViewport::CleanMapRenderSurface()
{
	if (this->MapRenderSurface) {
		SDL_FreeSurface(this->MapRenderSurface);
		this->MapRenderSurface = nullptr;
	}
}

//@}
