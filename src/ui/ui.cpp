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
/**@name ui.cpp - The user interface globals. */
//
//      (c) Copyright 1999-2011 by Lutz Sammer, Andreas Arens,
//                                 Jimmy Salmon and Pali Rohár
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

#include "ui.h"

#include "action/action_research.h"
#include "action/action_train.h"
#include "action/action_upgradeto.h"
#include "cursor.h"
#include "font.h"
#include "interface.h"
#include "iolib.h"
#include "debug_bridge.h"
#include "loadingscreen.h"
#include "map.h"
#include "menus.h"
#include "minimap.h"
#include "player.h"
#include "network.h"
#include "title.h"
#include "ui/contenttype.h"
#include "ui/popup.h"
#include "unit.h"
#include "video.h"
#include "../video/hd_classic_hud.h"
#include "icons.h"
#include "settings.h"
#include "unittype.h"
#include "upgrade.h"

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <map>
#include <set>
#include <memory>

/*----------------------------------------------------------------------------
-- Variables
----------------------------------------------------------------------------*/

bool RightButtonAttacks;                   /// right button attacks

static ViewportModeType NewViewportMode = VIEWPORT_SINGLE;

bool FancyBuildings;                       /// Mirror buildings 1 yes, 0 now.

/**
**  The user interface configuration
*/
CUserInterface UI;

/*----------------------------------------------------------------------------
-- Functions
----------------------------------------------------------------------------*/

/**
**  Show load progress.
**
**  @param fmt  printf format string.
*/
void ShowLoadProgress(const char *fmt, ...)
{
	va_list va;
	char temp[4096];

	// NG68: beat the debug watchdog's heartbeat here, before the loading-screen early return below.
	// A mission load blocks the main loop for many seconds without crossing a main-loop checkpoint.
	// This call keeps the heartbeat moving for as long as loading is making progress; a real hang
	// inside the load still trips the watchdog, labelled 'loading'.
	DBG_CHECKPOINT("loading");
	NetworkLoadingPing(); // LAN-MESH S8: a device which loads is not a dead one

	// The Remastered map-load screen owns the framebuffer during a blocking load; suppress the
	// legacy progress text so it does not overpaint the loading art.
	if (IsLoadingScreenActive()) {
		return;
	}

	static unsigned int lastProgressUpdate = SDL_GetTicks();
	if (SDL_GetTicks() < lastProgressUpdate + 500) {
		// Only show progress updates every half second, otherwise we're waiting for the screen too much
		return;
	}
	lastProgressUpdate = SDL_GetTicks();

	va_start(va, fmt);
	vsnprintf(temp, sizeof(temp) - 1, fmt, va);
	temp[sizeof(temp) - 1] = '\0';
	va_end(va);

	if (Video.Depth && IsGameFontReady() && GetGameFont().IsLoaded()) {
		// Remove non printable chars
		for (unsigned char *s = (unsigned char *)temp; *s; ++s) {
			if (*s < 32) {
				*s = ' ';
			}
		}
		Video.FillRectangle(ColorBlack, 5, Video.Height - 18, Video.Width - 10, 18);
		CLabel(GetGameFont()).DrawCentered(Video.Width / 2, Video.Height - 16, temp);
		InvalidateArea(5, Video.Height - 18, Video.Width - 10, 18);
		RealizeVideoMemory();
	} else {
		DebugPrint("!!!!%s\n", temp);
	}
}

/**
**  Check if the game is running in Demo mode.
**
**  @return  True if the game is in demo mode, false otherwise
*/
bool IsDemoMode()
{
	return GameRunning && ThisPlayer->Type == PlayerTypes::PlayerNobody;
}

CUserInterface::CUserInterface()
{
	Point.Name = "cursor-point";
	Glass.Name = "cursor-glass";
	Cross.Name = "cursor-cross";
	YellowHair.Name = "cursor-yellow-hair";
	GreenHair.Name = "cursor-green-hair";
	RedHair.Name = "cursor-red-hair";
	Scroll.Name = "cursor-scroll";

	ArrowE.Name = "cursor-arrow-e";
	ArrowNE.Name = "cursor-arrow-ne";
	ArrowN.Name = "cursor-arrow-n";
	ArrowNW.Name = "cursor-arrow-nw";
	ArrowW.Name = "cursor-arrow-w";
	ArrowSW.Name = "cursor-arrow-sw";
	ArrowS.Name = "cursor-arrow-s";
	ArrowSE.Name = "cursor-arrow-se";

	LifeBarColorNames = {"dark-green", "yellow", "orange", "red"};
}

/**
**  Get popup class pointer by string identifier.
**
**  @param ident  Popup identifier.
**
**  @return       popup class pointer.
*/
CPopup &PopupByIdent(std::string_view ident)
{
	if (CPopup *popup = FindPopupByIdent(ident)) {
		return *popup;
	}
	ErrorPrint("Unknown popup '%s'\n", ident.data());
	ExitFatal(1);
}

/**
**  Find a popup by string identifier.
**
**  The scripts define the button popups only while "Show button popups" is on, but the buttons
**  keep their popup ident, so a missing popup is not an error when drawing.
**
**  @param ident  Popup identifier.
**
**  @return       popup class pointer, or nullptr when no popup has that ident.
*/
CPopup *FindPopupByIdent(std::string_view ident)
{
	const auto it = ranges::find(UI.ButtonPopups, ident, &CPopup::Ident);
	return it != UI.ButtonPopups.end() ? it->get() : nullptr;
}

// Magnification applied to the in-game map viewports. Only the play area is scaled. Used both when
// the map area is sized (InitUserInterface, ClipViewport) and when the viewport zoom is set
// (FinishViewportModeConfiguration).
// WORLD-ZOOM-RULE (R3): the original draws a map tile at 32 * k px, k = CVideo::HudCanvas.Step
// (measured: g_2960x1848.png). The zoom is that size over the logical tile: 1.5 for the 64-px
// tile at k = 3, 1.0 at k = 2, 2.0 at k = 4; with a 32-px tile it is k itself.
// It is a function, not a constant: the step exists only after CVideo::ComputeHudCanvas and the tile
// size only after the scripts have set it; both are final before InitUserInterface runs.
// Classic view: a tile is 32 classic pixels and a classic pixel is a whole number of screen pixels
// (hdatlas::classicStep), so the zoom is that step times 32 over the logical tile
// (ClassicLayoutScale is 0 outside the classic layout).
static float ClassicLayoutScale = 0.0f;

static float MapViewportZoom()
{
	if (ClassicLayoutScale > 0.0f && PixelTileSize.x > 0) {
		return 32.0f * ClassicLayoutScale / PixelTileSize.x;
	}
#ifdef __ANDROID__
	if (PixelTileSize.x > 0 && Video.HudCanvas.Step > 0) {
		return 32.0f * Video.HudCanvas.Step / PixelTileSize.x;
	}
#endif
	return 1.0f;
}

// Classic view layout. The scripts lay out the Remastered picture; the classic rects are put over
// theirs while the classic view is on, and theirs are kept here to be put back unchanged.
struct LayoutRects {
	CMapArea MapArea;
	int MinimapX = 0;
	int MinimapY = 0;
	int MinimapW = 0;
	int MinimapH = 0;
};
static LayoutRects RemasteredLayout;       /// the scripts' rects, as InitUserInterface found them
static LayoutRects ClassicLayout;          /// the classic rects as they were put
static bool RemasteredLayoutKept = false;
static bool ClassicLayoutOn = false;

static LayoutRects PresentLayoutRects()
{
	LayoutRects r;
	r.MapArea = UI.MapArea;
	r.MinimapX = UI.Minimap.X;
	r.MinimapY = UI.Minimap.Y;
	r.MinimapW = UI.Minimap.W;
	r.MinimapH = UI.Minimap.H;
	return r;
}

static void PutLayoutRects(const LayoutRects &r)
{
	UI.MapArea = r.MapArea;
	UI.Minimap.X = r.MinimapX;
	UI.Minimap.Y = r.MinimapY;
	UI.Minimap.W = r.MinimapW;
	UI.Minimap.H = r.MinimapH;
}

static bool SameMapRect(const CMapArea &a, const CMapArea &b)
{
	return a.X == b.X && a.Y == b.Y && a.EndX == b.EndX && a.EndY == b.EndY;
}

static hdatlas::ClassicScreen ClassicBoxOnScreen()
{
	return hdatlas::classicScreen(Video.Width, Video.Height);
}

/// Put the classic rects: the map view from logical 176,16 to 16 short of the logical screen's
/// right and bottom edges; minimap 128 x 128 at logical 24,26; all times the step. The map view
/// has no HUD over it in the box, so it has no scroll padding (o11_13: the view lies flush on the
/// map's left and bottom edges).
static void PutClassicLayoutRects()
{
	const hdatlas::ClassicScreen box = ClassicBoxOnScreen();
	const hdatlas::ClassicRect lv = hdatlas::classicView(box);
	const hdatlas::ClassicRect view = hdatlas::classicToScreen(box, lv.x, lv.y, lv.w, lv.h);
	const hdatlas::ClassicRect mini = hdatlas::classicToScreen(
		box, hdatlas::ClassicMinimapX, hdatlas::ClassicMinimapY, hdatlas::ClassicMinimapSize,
		hdatlas::ClassicMinimapSize);

	LayoutRects r;
	r.MapArea.X = view.x;
	r.MapArea.Y = view.y;
	r.MapArea.EndX = view.x + view.w - 1;
	r.MapArea.EndY = view.y + view.h - 1;
	r.MinimapX = mini.x;
	r.MinimapY = mini.y;
	r.MinimapW = mini.w;
	r.MinimapH = mini.h;
	PutLayoutRects(r);
	ClassicLayout = r;
	ClassicLayoutOn = true;
	ClassicLayoutScale = (float) box.s;
}

static void PutRemasteredLayoutRects()
{
	PutLayoutRects(RemasteredLayout);
	ClassicLayoutOn = false;
	ClassicLayoutScale = 0.0f;
}

/// Align UI map area if at least one of the map dimensions fits completely into it.
static void AlignMapAreaToMap()
{
	if (Map.Info.MapWidth) {
		/// Align UI map area if at least one of the map dimensions fits completely into it.
		// WargusHD: the viewport renders each tile at PixelTileSize * MapViewportZoom(), so the
		// ON-SCREEN map differs from the raw tile maths by that factor. Using the
		// unscaled size made the engine think a small map was half its rendered width and shift
		// UI.MapArea.X right -- overwriting the Lua-set flush X (= sidebar width) and leaving a black
		// gap between the HUD and the map. Fold the zoom in so the centring matches what is drawn.
		const uint16_t mapWidthInPixels = (uint16_t)(Map.Info.MapWidth * PixelTileSize.x * MapViewportZoom());
		const uint16_t mapHeightInPixels = (uint16_t)(Map.Info.MapHeight * PixelTileSize.y * MapViewportZoom());
		const uint16_t uiMapAreaWidth = UI.MapArea.EndX - UI.MapArea.X;
		const uint16_t uiMapAreaHeight = UI.MapArea.EndY - UI.MapArea.Y;
		uint16_t xAlign = 0;
		uint16_t yAlign = 0;

		// NG4: never re-centre horizontally. Shifting UI.MapArea.X right for a narrow map overwrote the
		// Lua-set flush edge (= sidebar width) and left a black gap between the HUD and the map. Keep X
		// flush against the HUD; a map narrower than the area just letterboxes on the FAR right (EndX
		// clamp below), never between the HUD and the map. Vertical centring is unaffected (no HUD there).
		if (mapHeightInPixels < uiMapAreaHeight) {
			yAlign = (uiMapAreaHeight - mapHeightInPixels) / 2;
			UI.MapArea.Y += yAlign;
		}

		UI.MapArea.EndX = std::min<int>(UI.MapArea.EndX - xAlign, UI.MapArea.X + (int)(Map.Info.MapWidth * PixelTileSize.x * MapViewportZoom()) - 1);
		UI.MapArea.EndY = std::min<int>(UI.MapArea.EndY - yAlign, UI.MapArea.Y + (int)(Map.Info.MapHeight * PixelTileSize.y * MapViewportZoom()) - 1);
	}
}

/**
**  Initialize the user interface.
*/
void InitUserInterface()
{
	UI.Offset640X = (Video.Width - 640) / 2;
	UI.Offset480Y = (Video.Height - 480) / 2;

	UI.LifeBarColorsInt.clear();
	for(const std::string& name : UI.LifeBarColorNames) {
		UI.LifeBarColorsInt.push_back(IndexToColor(GetColorIndexByName(name)));
	}

	// Keep the scripts' rects. If the classic rects of an earlier game are still in place (the
	// scripts did not lay the picture out again), the kept ones stay as they are.
	if (!(ClassicLayoutOn && SameMapRect(UI.MapArea, ClassicLayout.MapArea))) {
		RemasteredLayout = PresentLayoutRects();
		RemasteredLayoutKept = true;
	}
	if (IsClassicView()) {
		PutClassicLayoutRects();
	} else if (ClassicLayoutOn) {
		PutRemasteredLayoutRects();
	}

	//
	// Calculations
	//
	AlignMapAreaToMap();
	if (ClassicLayoutOn) {
		ClassicLayout.MapArea = UI.MapArea;
	}

	UI.SelectedViewport = UI.Viewports;

	SetViewportMode(VIEWPORT_SINGLE);

	UI.CompletedBarColor = Video.MapRGB(TheScreen->format, UI.CompletedBarColorRGB);
	UI.ViewportCursorColor = ColorWhite;
}

bool ClassicHudActive()
{
	return ClassicLayoutOn;
}

/**
**  Classic view switched: put the rects of the present view and rebuild what depends on them.
**
**  The view keeps its top row and its left column: the map pixel at the new view's top-left
**  corner is the one at the old view's top-left corner, as far as the map's edges allow. The
**  original keeps the top row (run 56: 7 of 7 toggles, within 0.6 tile) but puts the classic view
**  at the map's right edge (6 of 6) - a defect, not copied (CLASSIC_CALIB56.md).
*/
void ApplyClassicViewLayout()
{
	if (IsClassicView() == ClassicLayoutOn) {
		return;
	}
	// Outside a game there is nothing to rebuild: InitUserInterface puts the rects at the start
	// of the next one.
	if (!RemasteredLayoutKept || !Map.Info.MapWidth || UI.NumViewports == 0 || !MinimapSurface) {
		return;
	}
	if (BigMapMode) {
		// The whole-screen map mode keeps its own copy of the map area; left as it is.
		ErrorPrint("ClassicView: the layout is not switched in the big map mode\n");
		return;
	}
	// Nothing that points into the old layout crosses the switch: a building being placed is
	// let go, an open pie menu is closed, and the pointer is on nothing until it moves again.
	if (CursorBuilding != nullptr) {
		CancelBuildingMode();
	}
	if (CursorState == CursorStates::PieMenu) {
		CursorState = CursorStates::Point;
	}
	ButtonAreaUnderCursor.reset();
	ButtonUnderCursor = -1;
	CursorOn = ECursorOn::Unknown;

	const CViewport &old = UI.Viewports[0];
	const PixelPos oldMapPixel = Map.TilePosToMapPixelPos_TopLeft(old.MapPos) + old.Offset;

	if (IsClassicView()) {
		PutClassicLayoutRects();
	} else {
		PutRemasteredLayoutRects();
	}
	AlignMapAreaToMap();
	if (ClassicLayoutOn) {
		ClassicLayout.MapArea = UI.MapArea;
	}
	SetViewportMode(UI.ViewportMode);

	UI.Viewports[0].Set(Vec2i(0, 0), PixelDiff(oldMapPixel.x, oldMapPixel.y));

	// The minimap's surfaces and tables are made for its size.
	UI.Minimap.Destroy();
	UI.Minimap.Create();
}

static std::string ClassicRaceName()
{
	std::string race;
	if (ThisPlayer && ThisPlayer->Race >= 0 && ThisPlayer->Race < (int) PlayerRaces.Count) {
		race = PlayerRaces.Name[ThisPlayer->Race];
	}
	return race;
}

/// A HUD atlas of the game folder, loaded once; a failed load is kept and reported once.
static CAtlasGraphic *ClassicHudAtlas(const std::string &jsonPath)
{
	static std::map<std::string, std::unique_ptr<CAtlasGraphic>> atlases;
	auto it = atlases.find(jsonPath);
	if (it == atlases.end()) {
		auto atlas = std::make_unique<CAtlasGraphic>();
		if (!atlas->LoadTexturePacker(jsonPath)) {
			ErrorPrint("Classic HUD: '%s' not readable, that part is not drawn\n", jsonPath.c_str());
			atlas.reset();
		}
		it = atlases.emplace(jsonPath, std::move(atlas)).first;
	}
	return it->second.get();
}

bool ClassicBoxContains(const PixelPos &screenPos)
{
	int bx;
	int by;
	return hdatlas::classicFromScreen(ClassicBoxOnScreen(), screenPos.x, screenPos.y, bx, by);
}

bool ClassicMenuButtonContains(const PixelPos &screenPos)
{
	int bx;
	int by;
	if (!hdatlas::classicFromScreen(ClassicBoxOnScreen(), screenPos.x, screenPos.y, bx, by)) {
		return false;
	}
	return bx >= hdatlas::ClassicMenuX && bx < hdatlas::ClassicMenuX + hdatlas::ClassicMenuW
		   && by >= hdatlas::ClassicMenuY && by < hdatlas::ClassicMenuY + hdatlas::ClassicMenuH;
}

int ClassicCommandCellAt(int screenX, int screenY)
{
	int bx;
	int by;
	if (!ClassicLayoutOn
		|| !hdatlas::classicFromScreen(ClassicBoxOnScreen(), screenX, screenY, bx, by)) {
		return -1;
	}
	return classichud::commandCellAt(bx, by);
}

bool ClassicPauseButtonContains(int screenX, int screenY)
{
	int bx;
	int by;
	return ClassicLayoutOn
		   && hdatlas::classicFromScreen(ClassicBoxOnScreen(), screenX, screenY, bx, by)
		   && classichud::contains(classichud::PauseFace, bx, by);
}

bool ClassicF5SlotContains(const PixelPos &screenPos)
{
	int bx;
	int by;
	return ClassicLayoutOn
		   && hdatlas::classicFromScreen(ClassicBoxOnScreen(), screenPos.x, screenPos.y, bx, by)
		   && classichud::contains(classichud::F5Face, bx, by);
}

/// The art the classic HUD cannot be drawn without: the frames of both races and the widget
/// atlas (header buttons, bars, cells). Portrait-face is not among them: without it the cells
/// show the Remastered icons (DrawClassicPicture). Read once each (ClassicHudAtlas keeps the
/// result, a failure included).
static bool ClassicHudArtReadable(std::string &missing)
{
	static const char *const needed[] = {
		"Data/Art/classic/HUD/BG_Human.json",
		"Data/Art/classic/HUD/BG_Orc.json",
		"Data/skins/Classic_HUD_Widgets.json",
	};
	for (const char *path : needed) {
		if (ClassicHudAtlas(path) == nullptr) {
			missing = path;
			return false;
		}
	}
	return true;
}

bool RequestClassicView(bool on)
{
	if (on == IsClassicView()) {
		return on;
	}
	// The big map mode keeps its own copy of the map area (ApplyClassicViewLayout): no switch
	// either way while it is on, so the flag and the layout never part.
	if (BigMapMode) {
		ErrorPrint("ClassicView: not switched in the big map mode\n");
		return IsClassicView();
	}
	if (on) {
		if (!TheRenderer) {
			ErrorPrint("ClassicView: no renderer yet, the view stays Remastered\n");
			return false;
		}
		std::string missing;
		if (!ClassicHudArtReadable(missing)) {
			static bool told = false;
			if (!told) {
				told = true;
				ErrorPrint("ClassicView: '%s' is not in the game folder, the view stays Remastered\n",
				           missing.c_str());
			}
			return false;
		}
	}
	SetClassicView(on);
	return IsClassicView();
}

void UiToggleClassicView()
{
	RequestClassicView(!IsClassicView());
}

static SDL_Rect ClassicScreenRect(const hdatlas::ClassicScreen &box, const classichud::Rect &r)
{
	const hdatlas::ClassicRect s = hdatlas::classicToScreen(box, r.x, r.y, r.w, r.h);
	return {s.x, s.y, s.w, s.h};
}

/// Draw the part src (frame pixels) of an untrimmed frame into dst. The atlas draws a frame's
/// top-left part only, so that part is laid with its right and bottom edge on dst's and the
/// rest of it is clipped away.
static void DrawClassicFramePart(const CAtlasGraphic &atlas, int frame, const classichud::Rect &src,
                                 const SDL_Rect &dst)
{
	if (frame < 0 || src.w <= 0 || src.h <= 0 || dst.w <= 0 || dst.h <= 0) {
		return;
	}
	if (src.x == 0 && src.y == 0) {
		atlas.DrawAtlasFramePart(frame, src.w, src.h, dst);
		return;
	}
	const int w = (int) std::lround((src.x + src.w) * (double) dst.w / src.w);
	const int h = (int) std::lround((src.y + src.h) * (double) dst.h / src.h);
	const SDL_Rect whole = {dst.x + dst.w - w, dst.y + dst.h - h, w, h};
	SDL_RenderSetClipRect(TheRenderer, &dst);
	atlas.DrawAtlasFramePart(frame, src.x + src.w, src.y + src.h, whole);
	SDL_RenderSetClipRect(TheRenderer, nullptr);
}

/// A sprite of the widget atlas nine-sliced onto a logical rect (classichud::nineSlice).
static void DrawClassicNineSlice(const CAtlasGraphic &atlas, const hdatlas::ClassicScreen &box,
                                 const char *sprite, int spriteSize, int slice,
                                 const classichud::Rect &dst)
{
	const int frame = atlas.FrameByName(sprite);
	if (frame < 0) {
		return;
	}
	classichud::SlicePiece pieces[9];
	const int count = classichud::nineSlice(spriteSize, spriteSize, slice, dst, pieces);
	for (int i = 0; i < count; ++i) {
		DrawClassicFramePart(atlas, frame, pieces[i].src, ClassicScreenRect(box, pieces[i].dst));
	}
}

/// The picture of an icon from Portrait-face: frame "<set>_<N>" (the forest picture where the
/// set lacks the slot), N = the icon's frame, the set by the tileset directory of the icon's
/// graphic (the rule of the Remastered icons, CGraphic::Load). Where Portrait-mask has
/// "<set>_<N>_team" the picture takes the player's colour there: colour x the mask's grey / 255
/// (o11_16, o11_84: greys 255 / 215 / 175 / 135 give 164 / 138 / 112 / 87 of red 164).
/// `srcX`, `srcW`: the columns of the picture that dst shows (the narrow cell of a large group
/// shows a part of it at the picture's own size).
static void DrawClassicPicture(const hdatlas::ClassicScreen &box, const CIcon &icon, int playerColor,
                               const classichud::Rect &dst, int srcX = 0,
                               int srcW = classichud::PictureW)
{
	if (!icon.G) {
		return;
	}
	const CAtlasGraphic *faces = ClassicHudAtlas("Data/Art/classic/HUD/Portrait-face.json");
	const std::string path = icon.G->File.generic_string();
	const char *set = path.find("/winter/") != std::string::npos ? "ice"
	                  : path.find("/wasteland/") != std::string::npos ? "swamp"
	                  : path.find("/swamp/") != std::string::npos ? "xswamp" : "forest";
	std::string name = std::string(set) + "_" + std::to_string(icon.Frame);
	int frame = faces != nullptr ? faces->FrameByName(name.c_str()) : -1;
	if (faces != nullptr && frame < 0) {
		name = "forest_" + std::to_string(icon.Frame);
		frame = faces->FrameByName(name.c_str());
	}
	const SDL_Rect shown = ClassicScreenRect(box, dst);
	// The whole picture's rect; a window of it is cut out by the clip rect (the pattern of
	// DrawClassicFramePart).
	const bool window = srcX != 0 || srcW != classichud::PictureW;
	const SDL_Rect r = window ? ClassicScreenRect(box, {dst.x - srcX, dst.y, classichud::PictureW, dst.h})
	                          : shown;
	// Black under the picture: where a picture has no pixel the original shows black
	// (o11_16, "Repair": its 38 empty pixels).
	Uint8 oldR;
	Uint8 oldG;
	Uint8 oldB;
	Uint8 oldA;
	SDL_GetRenderDrawColor(TheRenderer, &oldR, &oldG, &oldB, &oldA);
	SDL_SetRenderDrawColor(TheRenderer, 0, 0, 0, 255);
	SDL_RenderFillRect(TheRenderer, &shown);
	SDL_SetRenderDrawColor(TheRenderer, oldR, oldG, oldB, oldA);
	if (window) {
		SDL_RenderSetClipRect(TheRenderer, &shown);
	}
	if (faces == nullptr || frame < 0) {
		// No classic picture for this icon in the game folder: the Remastered icon in the
		// picture's rect (no team colour on it), logged once per picture (spec section 5, rules).
		static std::set<std::string> told;
		if (told.insert(name).second) {
			ErrorPrint("Classic HUD: no classic picture '%s', the Remastered icon '%s' is shown\n",
			           name.c_str(), icon.GetIdent().c_str());
		}
		icon.G->DrawFrameClipTexDst(icon.Frame, r);
	} else {
		faces->DrawAtlasFramePart(frame, classichud::PictureW, classichud::PictureH, r);
		if (playerColor >= 0 && playerColor < (int) PlayerColorsRGB.size()
			&& !PlayerColorsRGB[playerColor].empty()) {
			if (const CAtlasGraphic *masks = ClassicHudAtlas("Data/Art/classic/HUD/Portrait-mask.json")) {
				const int mask = masks->FrameByName((name + "_team").c_str());
				if (mask >= 0) {
					const CColor &c = PlayerColorsRGB[playerColor][0];
					masks->DrawAtlasFrameClipScaled(mask, r.x, r.y, r.w, r.h, false, c.R, c.G, c.B);
				}
			}
		}
	}
	if (window) {
		SDL_RenderSetClipRect(TheRenderer, nullptr);
	}
}

static int ClassicPlayerColor(const CUnit &unit)
{
	const CPlayer *player = unit.RescuedFrom ? unit.RescuedFrom : unit.Player;
	return player ? GameSettings.Presets[player->Index].PlayerColor : -1;
}

/// A header button (user button `index`) is held down: the pointer is on it with the left
/// button down, the engine's own state of a pressed button.
static bool ClassicUserButtonHeld(int index)
{
	return ButtonAreaUnderCursor == ButtonArea::User && ButtonUnderCursor == index
	       && (MouseButtons & LeftButton) != 0;
}

/// An in-game window is up: the scripts' mark of it, the dim caption of the Pause button
/// (user button 1, set with the "Resume" caption when the first window comes up).
static bool ClassicWindowUp()
{
	return UI.UserButtons.size() > 1 && UI.UserButtons[1].Dim;
}

/// The one selected unit is shown with what it carries: a transport with units aboard, of the
/// player or an ally (the units DrawUnitInfo shows the passengers of). Its panel has no text.
static bool ClassicTransportShown(const CUnit &unit)
{
	return unit.Type->CanTransport() && unit.BoardCount > 0 && ThisPlayer != nullptr
	       && (unit.Player == ThisPlayer || ThisPlayer->IsAllied(*unit.Player));
}

/// Passenger n of a transport in the order of its cells (DrawUnitInfo_transporter), or nullptr.
static const CUnit *ClassicPassenger(const CUnit &transport, int n)
{
	for (const CUnit *inside : transport.InsideUnits) {
		if (inside != nullptr && inside->Boarded && n-- == 0) {
			return inside;
		}
	}
	return nullptr;
}

/// The variable that holds the progress of what the unit makes, for a unit the Remastered panel
/// shows its bar for (not of an opponent, not neutral); -1 for any other unit.
static int ClassicProgressVariable(const CUnit &unit)
{
	if (ThisPlayer == nullptr || ThisPlayer->IsEnemy(unit)
		|| unit.Player->Type == PlayerTypes::PlayerNeutral) {
		return -1;
	}
	switch (unit.CurrentAction()) {
		case UnitAction::Built: return BUILD_INDEX;
		case UnitAction::Train: return TRAINING_INDEX;
		case UnitAction::Research: return RESEARCH_INDEX;
		case UnitAction::UpgradeTo: return UPGRADINGTO_INDEX;
		default: return -1;
	}
}

/// The picture of what the unit trains, researches or upgrades to (the icons of
/// DrawUnitInfo_single_selection), or nullptr.
static const CIcon *ClassicProgressIcon(const CUnit &unit)
{
	switch (unit.CurrentAction()) {
		case UnitAction::Train:
			return static_cast<const COrder_Train *>(unit.CurrentOrder())->GetUnitType().Icon.Icon;
		case UnitAction::UpgradeTo:
			return static_cast<const COrder_UpgradeTo *>(unit.CurrentOrder())->GetUnitType().Icon.Icon;
		case UnitAction::Research:
			return static_cast<const COrder_Research *>(unit.CurrentOrder())->GetUpgrade().Icon;
		default:
			return nullptr;
	}
}

/// The "Magic:" row of the one selected unit is shown: a caster the stat rows are shown for.
/// A caster is a unit with the Mana variable enabled, the rule of the Remastered panel's row
/// (Mana = "only"); every unit type has the variable's default maximum, casters or not.
static bool ClassicMagicShown(const CUnit &unit)
{
	return unit.Variable[MANA_INDEX].Enable && unit.Variable[MANA_INDEX].Max > 0
	       && ThisPlayer != nullptr && !ThisPlayer->IsEnemy(unit)
	       && unit.Player->Type != PlayerTypes::PlayerNeutral && ClassicProgressVariable(unit) < 0
	       && !ClassicTransportShown(unit);
}

/// A unit in a cell of the info panel (a unit of a group, a passenger): the cell, its picture
/// and the hit point bar under it. `wide`: the cell of the portrait's size.
static void DrawClassicUnitCell(const CAtlasGraphic &widgets, const hdatlas::ClassicScreen &box,
                                const CUnit &unit, const classichud::Rect &cell, bool wide, bool pointerOn)
{
	DrawClassicNineSlice(widgets, box, pointerOn ? "portrait_button_hovered" : "portrait_button",
	                     classichud::CellSprite, classichud::CellSlice, cell);
	if (unit.Type != nullptr && unit.Type->Icon.Icon) {
		if (wide) {
			DrawClassicPicture(box, *unit.Type->Icon.Icon, ClassicPlayerColor(unit),
			                   classichud::cellPicture(cell));
		} else {
			DrawClassicPicture(box, *unit.Type->Icon.Icon, ClassicPlayerColor(unit),
			                   classichud::cellPicture(cell), classichud::GroupSmallPictureSrcX,
			                   classichud::GroupSmallPictureW);
		}
	}
	DrawClassicNineSlice(widgets, box, "hp_bar", classichud::HitPointSprite, classichud::HitPointSlice,
	                     classichud::groupBar(cell));
	classichud::Rect fill = classichud::groupFill(cell);
	int colour = 0;
	fill.w = classichud::groupHitPointFill(unit.Variable[HP_INDEX].Value, unit.Variable[HP_INDEX].Max,
	                                       fill.w, colour);
	if (fill.w > 0) {
		const char *sprite = colour == 0 ? "bar_green" : colour == 1 ? "bar_yellow" : "bar_red";
		widgets.DrawAtlasFramePart(widgets.FrameByName(sprite), 1, 1, ClassicScreenRect(box, fill));
	}
}

int ClassicGroupCellAt(int screenX, int screenY)
{
	int bx;
	int by;
	if (!ClassicLayoutOn
		|| !hdatlas::classicFromScreen(ClassicBoxOnScreen(), screenX, screenY, bx, by)) {
		return -1;
	}
	const std::vector<CUnit *> &cells = GetSelectionCells();
	const int cell = classichud::groupCellAt(bx, by, (int) cells.size() <= classichud::GroupWideMax);
	return cell >= 0 && cell < (int) cells.size() && cells[cell] != nullptr ? cell : -1;
}

int ClassicPassengerCellAt(int screenX, int screenY)
{
	int bx;
	int by;
	if (!ClassicLayoutOn || Selected.size() != 1 || Selected[0] == nullptr || Selected[0]->Type == nullptr
		|| !ClassicTransportShown(*Selected[0])
		|| !hdatlas::classicFromScreen(ClassicBoxOnScreen(), screenX, screenY, bx, by)) {
		return -1;
	}
	const int n = classichud::passengerCellAt(bx, by);
	return n >= 0 && ClassicPassenger(*Selected[0], n) != nullptr ? n : -1;
}

/// The widgets of the classic HUD that are art: header buttons, resource icons, the panel of the
/// selected unit or units (frame, portraits, hit point bars, what a unit makes or carries) and
/// the command card.
static void DrawClassicHudWidgets(const hdatlas::ClassicScreen &box, const std::string &race)
{
	const CAtlasGraphic *widgets = ClassicHudAtlas("Data/skins/Classic_HUD_Widgets.json");
	if (widgets == nullptr) {
		return;
	}
	// Header: the F5 slot (its picture, then its frame) and the Pause button's face.
	// At a step above 1 the picture is its logical pixels repeated: every one the middle pixel
	// of its block of the art (classichud::f5InsetSample), not the art scaled.
	const int inset = widgets->FrameByName(("f5_inset_" + race).c_str());
	if (box.s <= 1) {
		widgets->DrawAtlasFramePart(inset, classichud::F5InsetArtW, classichud::F5InsetArtH,
		                            ClassicScreenRect(box, classichud::F5Inset));
	} else {
		for (int ly = 0; ly < classichud::F5Inset.h; ++ly) {
			for (int lx = 0; lx < classichud::F5Inset.w; ++lx) {
				const classichud::Rect pixel = {classichud::F5Inset.x + lx, classichud::F5Inset.y + ly, 1, 1};
				DrawClassicFramePart(*widgets, inset, classichud::f5InsetSample(lx, ly),
				                     ClassicScreenRect(box, pixel));
			}
		}
	}
	// The slot's frame: `f5_active` while it is held down (the engine's pressed state of the
	// menu-button area, which the slot uses: mouse.cpp), else `f5`.
	const bool f5Held = GameMenuButtonClicked && ButtonAreaUnderCursor == ButtonArea::Menu
	                    && (ButtonUnderCursor == ButtonUnderMenu || ButtonUnderCursor == ButtonUnderNetworkMenu);
	widgets->DrawAtlasFramePart(widgets->FrameByName(classichud::faceName("f5", f5Held).c_str()),
	                            classichud::F5Face.w, classichud::F5Face.h,
	                            ClassicScreenRect(box, classichud::F5Face));
	widgets->DrawAtlasFramePart(
		widgets->FrameByName(classichud::faceName("hud_" + race + "_small", ClassicUserButtonHeld(1)).c_str()),
		classichud::PauseFace.w, classichud::PauseFace.h, ClassicScreenRect(box, classichud::PauseFace));
	// Resource row: the icons, about the middle of the logical screen.
	for (int i = 0; i < classichud::ResourceIconCount; ++i) {
		const std::string name = "icons_" + std::to_string(classichud::ResourceIconSprite[i]);
		const classichud::Rect r = {classichud::resourceIconX(box.lw, i), classichud::ResourceIconY,
		                            classichud::ResourceIconSize, classichud::ResourceIconSize};
		widgets->DrawAtlasFramePart(widgets->FrameByName(name.c_str()), r.w, r.h,
		                            ClassicScreenRect(box, r));
	}
	// One selected unit: panel, portrait, hit points.
	if (Selected.size() == 1 && Selected[0] != nullptr && Selected[0]->Type != nullptr) {
		UpdateUnitVariables(*Selected[0]);
		const CUnit &unit = *Selected[0];
		DrawClassicNineSlice(*widgets, box, "portrait_button", classichud::CellSprite,
		                     classichud::CellSlice, classichud::InfoPanel);
		DrawClassicNineSlice(*widgets, box, "portrait_button", classichud::CellSprite,
		                     classichud::CellSlice, classichud::PortraitCell);
		if (unit.Type->Icon.Icon) {
			DrawClassicPicture(box, *unit.Type->Icon.Icon, ClassicPlayerColor(unit),
			                   classichud::cellPicture(classichud::PortraitCell));
		}
		DrawClassicNineSlice(*widgets, box, "hp_bar", classichud::HitPointSprite,
		                     classichud::HitPointSlice, classichud::HitPointBar);
		int colour = 0;
		const int fill = classichud::hitPointFill(unit.Variable[HP_INDEX].Value,
		                                          unit.Variable[HP_INDEX].Max, colour);
		if (fill > 0) {
			const classichud::Rect r = {classichud::HitPointFill.x, classichud::HitPointFill.y, fill,
			                            classichud::HitPointFill.h};
			const char *sprite = colour == 0 ? "bar_green" : colour == 1 ? "bar_yellow" : "bar_red";
			widgets->DrawAtlasFramePart(widgets->FrameByName(sprite), 1, 1, ClassicScreenRect(box, r));
		}
		// A building under construction, a unit that trains, researches or upgrades: the bar and
		// its done part, for the units the Remastered panel shows its bar for.
		const int progress = ClassicProgressVariable(unit);
		if (progress >= 0) {
			widgets->DrawAtlasFramePart(widgets->FrameByName("complete_bar"), classichud::CompleteBar.w,
			                            classichud::CompleteBar.h,
			                            ClassicScreenRect(box, classichud::CompleteBar));
			const int done = classichud::completeFill(unit.Variable[progress].Value,
			                                          unit.Variable[progress].Max);
			if (done > 0) {
				const classichud::Rect r = {classichud::CompleteFill.x, classichud::CompleteFill.y, done,
				                            classichud::CompleteFill.h};
				widgets->DrawAtlasFramePart(widgets->FrameByName("bar_green"), 1, 1,
				                            ClassicScreenRect(box, r));
			}
			// What is made, in its cell beside the label (o30_C01_*, o30_C02_*, o30_C03_*).
			if (const CIcon *icon = ClassicProgressIcon(unit)) {
				DrawClassicNineSlice(*widgets, box, "portrait_button", classichud::CellSprite,
				                     classichud::CellSlice, classichud::TrainingCell);
				DrawClassicPicture(box, *icon, ClassicPlayerColor(unit),
				                   classichud::cellPicture(classichud::TrainingCell));
			}
		}
		// A caster: the bar of the "Magic:" row (o30_C14_mage_selected).
		if (ClassicMagicShown(unit)) {
			const classichud::Rect bar = classichud::magicBar(classichud::MagicRow);
			DrawClassicNineSlice(*widgets, box, "magic_bar", classichud::MagicSprite,
			                     classichud::MagicSlice, bar);
			classichud::Rect r = classichud::magicFillArea(bar);
			r.w = classichud::magicFill(unit.Variable[MANA_INDEX].Value, unit.Variable[MANA_INDEX].Max);
			if (r.w > 0) {
				widgets->DrawAtlasFramePart(widgets->FrameByName("bar_magic"), 1, 1,
				                            ClassicScreenRect(box, r));
			}
		}
		// A transport: its passengers in the cells under the portrait's row (o30_C13_*).
		if (ClassicTransportShown(unit)) {
			for (int n = 0; n < classichud::PassengerMax; ++n) {
				const CUnit *inside = ClassicPassenger(unit, n);
				if (inside == nullptr) {
					break;
				}
				DrawClassicUnitCell(*widgets, box, *inside, classichud::passengerCell(n), true,
				                    ButtonAreaUnderCursor == ButtonArea::Transporting
				                    && ButtonUnderCursor == n);
			}
		}
	} else if (Selected.size() > 1) {
		// Several units: the cells of the selection grid (GetSelectionCells: a unit that died
		// leaves its cell empty), 3 columns up to 9 cells, 4 narrow ones above (o30_C06_*).
		const std::vector<CUnit *> &cells = GetSelectionCells();
		const bool wide = (int) cells.size() <= classichud::GroupWideMax;
		DrawClassicNineSlice(*widgets, box, "portrait_button", classichud::CellSprite,
		                     classichud::CellSlice, classichud::InfoPanel);
		for (int i = 0; i < (int) cells.size() && i < classichud::groupCellCount(wide); ++i) {
			if (cells[i] != nullptr && cells[i]->Type != nullptr) {
				DrawClassicUnitCell(*widgets, box, *cells[i], classichud::groupCell(i, wide), wide,
				                    ButtonAreaUnderCursor == ButtonArea::Selected
				                    && ButtonUnderCursor == i);
			}
		}
	}
	// Command card.
	if (!Selected.empty() && Selected[0] != nullptr) {
		const int playerColor = ClassicPlayerColor(*Selected[0]);
		for (int i = 0; i < classichud::CommandColumns * classichud::CommandRows; ++i) {
			bool allowed = false;
			bool marked = false;
			if (!ClassicCommandButtonShown(i, allowed, marked)) {
				continue;
			}
			const classichud::Rect cell = classichud::commandCell(i);
			// Held down: the pointer on the cell with the left button down (a button that may
			// not be used never has the pointer, HandleMouseOn).
			const bool held = ButtonAreaUnderCursor == ButtonArea::Button && ButtonUnderCursor == i
			                  && (MouseButtons & LeftButton) != 0;
			DrawClassicNineSlice(*widgets, box, classichud::faceName("portrait_button", held).c_str(),
			                     classichud::CellSprite, classichud::CellSlice, cell);
			DrawClassicPicture(box, *CurrentButtons[i].Icon.Icon, playerColor,
			                   classichud::cellPicture(cell));
			if (marked) {
				DrawClassicNineSlice(*widgets, box,
				                     classichud::faceName("portrait_button_highlight", held).c_str(),
				                     classichud::CellSprite, classichud::CellSlice, cell);
			}
			if (!allowed) {
				// The whole cell at half brightness (o11_84, "Keep": frame 204 -> 102, 128 -> 64,
				// picture 1461 of 1476 pixels = half of the art): black at 128 of 255 over it.
				const SDL_Rect r = ClassicScreenRect(box, cell);
				Uint8 oldR;
				Uint8 oldG;
				Uint8 oldB;
				Uint8 oldA;
				SDL_BlendMode oldMode = SDL_BLENDMODE_NONE;
				SDL_GetRenderDrawColor(TheRenderer, &oldR, &oldG, &oldB, &oldA);
				SDL_GetRenderDrawBlendMode(TheRenderer, &oldMode);
				SDL_SetRenderDrawBlendMode(TheRenderer, SDL_BLENDMODE_BLEND);
				SDL_SetRenderDrawColor(TheRenderer, 0, 0, 0, 128);
				SDL_RenderFillRect(TheRenderer, &r);
				SDL_SetRenderDrawBlendMode(TheRenderer, oldMode);
				SDL_SetRenderDrawColor(TheRenderer, oldR, oldG, oldB, oldA);
			}
		}
	}
}

/// The HUD faces the classic text is drawn with and the height of a capital of each on this
/// screen: `remaster_hud` 38.25 units of the HUD's art space (digit "0" of the resource row,
/// c_k_2960x1848.png), `remaster_hud_small` 27 (the hit point figure, ink rows 990..1017).
/// The faces exist at these sizes only; the nearest is drawn scaled (classichud::pickFace).
static const CFont *ClassicTextFace(int capBox, const hdatlas::ClassicScreen &box, double &scale)
{
	static const char *const names[2] = {"remaster_hud", "remaster_hud_small"};
	static const double capUnits[2] = {38.25, 27.0};
	const CFont *faces[2] = {nullptr, nullptr};
	double caps[2] = {0.0, 0.0};
	for (int i = 0; i < 2; ++i) {
		faces[i] = CFont::Get(names[i]);
		if (faces[i] != nullptr && faces[i]->IsLoaded()) {
			caps[i] = capUnits[i] * Video.HudCanvas.Scale;
		}
	}
	const int face = classichud::pickFace(classichud::capOnScreen(capBox, box.s), caps, 2, scale);
	return face < 0 ? nullptr : faces[face];
}

/// Draw a text of the classic HUD: a capital capBox logical pixels high (times the step on the
/// screen), its middle row at the logical row midY, placed on the logical column anchorX by
/// `align`. White with the face's own shadow; a tint colours the glyphs. The face is drawn
/// through the renderer's scale, smooth, so it needs the GPU glyph path of the game's HUD pass.
/// @return the width drawn, in logical pixels
static double DrawClassicText(const hdatlas::ClassicScreen &box, int capBox, double anchorX, double midY,
                              classichud::TextAlign align, std::string_view text,
                              const SDL_Color *tint = nullptr)
{
	double scale = 1.0;
	const CFont *font = ClassicTextFace(capBox, box, scale);
	if (font == nullptr || text.empty() || !GpuFontToBackbuffer || scale <= 0.0) {
		return 0.0;
	}
	const double k = box.s;
	const double width = font->Width(text) * scale;
	const auto tab = text.find("~|");
	const double tabWidth = tab == std::string_view::npos ? 0.0 : font->Width(text.substr(0, tab)) * scale;
	const double left = classichud::textLeft(align, box.box.x + anchorX * k, width, tabWidth);
	const double top = box.box.y + midY * k - font->Height() * scale / 2.0;

	float oldX = 1.0f;
	float oldY = 1.0f;
	SDL_RenderGetScale(TheRenderer, &oldX, &oldY);
	SDL_RenderSetScale(TheRenderer, (float) (oldX * scale), (float) (oldY * scale));
	if (tint != nullptr) {
		SetGlyphTint(tint->r, tint->g, tint->b);
	}
	CLabel(*font).Draw((int) std::lround(left / scale), (int) std::lround(top / scale), text);
	if (tint != nullptr) {
		SetGlyphTint(255, 255, 255);
	}
	SDL_RenderSetScale(TheRenderer, oldX, oldY);
	return k > 0.0 ? width / k : 0.0;
}

/// The tooltip of a command button: the box, then the lines the Remastered tooltip has for the
/// button (GetRemasteredTooltipTexts; the costs DrawPopup reads): title, the cost row, the
/// description, and for a button that may not be used the yellow line of what it needs.
static void DrawClassicTooltip(const hdatlas::ClassicScreen &box, const ButtonAction &button, bool allowed)
{
	using classichud::TextAlign;
	std::string title;
	std::string body;
	std::string requirement;
	GetRemasteredTooltipTexts(button, allowed, title, body, requirement);
	if (title.empty()) {
		title = button.Hint;
	}
	// Gold, lumber, oil: the resources with a cost, packed from the left.
	static const int resources[classichud::TooltipCostMax] = {GoldCost, WoodCost, OilCost};
	const int *costs = nullptr;
	switch (button.Action) {
		case ButtonCmd::Research:
			costs = AllUpgrades[button.Value]->Costs;
			break;
		case ButtonCmd::Build:
		case ButtonCmd::Train:
		case ButtonCmd::UpgradeTo:
			costs = ThisPlayer != nullptr ? getUnitTypes()[button.Value]->Stats[ThisPlayer->Index].Costs : nullptr;
			break;
		default:
			break;
	}
	int cost[classichud::TooltipCostMax];
	int costCount = 0;
	for (int i = 0; costs != nullptr && i < classichud::TooltipCostMax; ++i) {
		if (costs[resources[i]] > 0) {
			cost[costCount++] = i;
		}
	}
	// The description and the requirement, wrapped to the box's text columns.
	double scale = 1.0;
	const CFont *font = ClassicTextFace(classichud::TextCap, box, scale);
	if (font == nullptr || scale <= 0.0) {
		return;
	}
	const unsigned int wrap = (unsigned int) (classichud::TooltipTextW * box.s / scale);
	std::string_view line[classichud::TooltipLineMax];
	int lines = 0;
	line[lines++] = title;
	const int costLine = costCount > 0 ? lines++ : -1;
	std::string_view part;
	for (unsigned int n = 1; lines < classichud::TooltipLineMax
							 && !(part = GetLineFont(n, body, wrap, font)).empty(); ++n) {
		line[lines++] = part;
	}
	const int requirementLine = lines;
	for (unsigned int n = 1; lines < classichud::TooltipLineMax
							 && !(part = GetLineFont(n, requirement, wrap, font)).empty(); ++n) {
		line[lines++] = part;
	}

	// The box (run 59): black at a third inside the line, the line a translucent cream-white
	// over the bare map, and a dark ring one pixel outside it. All three are blended, and no
	// pixel is in two of their rects.
	const classichud::TooltipBoxBlend parts = classichud::tooltipBoxBlend(box.lh, lines);
	Uint8 oldR;
	Uint8 oldG;
	Uint8 oldB;
	Uint8 oldA;
	SDL_BlendMode oldMode = SDL_BLENDMODE_NONE;
	SDL_GetRenderDrawColor(TheRenderer, &oldR, &oldG, &oldB, &oldA);
	SDL_GetRenderDrawBlendMode(TheRenderer, &oldMode);
	SDL_SetRenderDrawBlendMode(TheRenderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(TheRenderer, 0, 0, 0, (Uint8) classichud::TooltipBoxAlpha);
	for (const classichud::Rect &r : parts.inner) {
		const SDL_Rect dst = ClassicScreenRect(box, r);
		SDL_RenderFillRect(TheRenderer, &dst);
	}
	SDL_SetRenderDrawColor(TheRenderer, 0, 0, 0, (Uint8) classichud::TooltipBoxRingAlpha);
	for (const classichud::Rect &r : parts.ring) {
		const SDL_Rect dst = ClassicScreenRect(box, r);
		SDL_RenderFillRect(TheRenderer, &dst);
	}
	SDL_SetRenderDrawColor(TheRenderer, (Uint8) classichud::TooltipBoxLineRGB[0],
	                       (Uint8) classichud::TooltipBoxLineRGB[1],
	                       (Uint8) classichud::TooltipBoxLineRGB[2],
	                       (Uint8) classichud::TooltipBoxLineAlpha);
	for (const classichud::Rect &r : parts.line) {
		const SDL_Rect dst = ClassicScreenRect(box, r);
		SDL_RenderFillRect(TheRenderer, &dst);
	}
	SDL_SetRenderDrawBlendMode(TheRenderer, oldMode);
	SDL_SetRenderDrawColor(TheRenderer, oldR, oldG, oldB, oldA);

	const SDL_Color yellow = {(Uint8) classichud::TooltipYellowRGB[0], (Uint8) classichud::TooltipYellowRGB[1],
	                          (Uint8) classichud::TooltipYellowRGB[2], 255};
	const CAtlasGraphic *widgets = ClassicHudAtlas("Data/skins/Classic_HUD_Widgets.json");
	for (int n = 0; n < lines; ++n) {
		const bool needs = n >= requirementLine;
		const double midY = classichud::tooltipLineMidY(box.lh, lines, n, needs);
		if (n != costLine) {
			DrawClassicText(box, classichud::TextCap, classichud::TooltipX, midY, TextAlign::Left, line[n],
			                needs ? &yellow : nullptr);
			continue;
		}
		for (int k = 0; k < costCount; ++k) {
			const classichud::Rect icon = classichud::tooltipCostIcon(k, midY);
			if (widgets != nullptr) {
				const std::string name = "icons_" + std::to_string(classichud::ResourceIconSprite[cost[k]]);
				widgets->DrawAtlasFramePart(widgets->FrameByName(name.c_str()), icon.w, icon.h,
				                            ClassicScreenRect(box, icon));
			}
			DrawClassicText(box, classichud::TextCap, icon.x + classichud::TooltipCostTextDX, midY,
			                TextAlign::Left, std::to_string(costs[resources[cost[k]]]));
		}
	}
}

/// The texts of the classic HUD, at the places measured in the original (hd_classic_hud.h).
static void DrawClassicHudTexts(const hdatlas::ClassicScreen &box)
{
	using classichud::TextAlign;
	// Resource numbers, as the Remastered row prints them: plain digits; food "used/limit" with
	// the used figure red (204, 0, 0) when it is over the limit (DrawResources; o11_16 "2/1").
	if (ThisPlayer != nullptr) {
		static const int costs[3] = {GoldCost, WoodCost, OilCost};
		for (int i = 0; i < 3; ++i) {
			DrawClassicText(box, classichud::TextCap,
			                classichud::resourceIconX(box.lw, i) + classichud::ResourceTextDX,
			                classichud::ResourceTextMidY, TextAlign::Left,
			                std::to_string(ThisPlayer->Resources[costs[i]]));
		}
		const double x = classichud::resourceIconX(box.lw, 3) + classichud::ResourceTextDX;
		const std::string used = std::to_string(ThisPlayer->Demand);
		const std::string limit = "/" + std::to_string(ThisPlayer->Supply);
		const SDL_Color red = {204, 0, 0, 255};
		const double w = DrawClassicText(box, classichud::TextCap, x, classichud::ResourceTextMidY,
		                                 TextAlign::Left, used,
		                                 ThisPlayer->Supply < ThisPlayer->Demand ? &red : nullptr);
		DrawClassicText(box, classichud::TextCap, x + w, classichud::ResourceTextMidY, TextAlign::Left,
		                limit);
	}
	// Menu and Pause: the captions of the scripts' buttons (user buttons 0 and 1, the ones the
	// Remastered header draws), centred in the faces.
	if (UI.UserButtons.size() > 0) {
		DrawClassicText(box, classichud::TextCap, hdatlas::ClassicMenuX + hdatlas::ClassicMenuW / 2.0,
		                classichud::CaptionMidY, TextAlign::Centre, UI.UserButtons[0].Button.Text);
	}
	if (UI.UserButtons.size() > 1) {
		// With a window up the scripts give it the "Resume" caption and mark it dim (o11_28);
		// the level is the Remastered header's, 127 of 255 (DrawUserDefinedButtons).
		const SDL_Color dim = {127, 127, 127, 255};
		DrawClassicText(box, classichud::TextCap,
		                classichud::PauseFace.x + classichud::PauseFace.w / 2.0,
		                classichud::CaptionMidY, TextAlign::Centre, UI.UserButtons[1].Button.Text,
		                UI.UserButtons[1].Dim ? &dim : nullptr);
	}
	// One selected unit: the texts of the Remastered info panel at their classic places
	// (classichud::panelX / panelMidY). A building under construction has its name, its hit point
	// figure and the words on the bar, which that panel gives by its own conditions (o21_52); a
	// unit that trains, researches or upgrades likewise, with the label "Training:" /
	// "Upgrading:" on stat row 1 (o30_C01_*, o30_C02_*, o30_C03_*). A transport with units aboard
	// has no text (o30_C13_*).
	if (Selected.size() == 1 && Selected[0] != nullptr && Selected[0]->Type != nullptr
		&& Video.HudCanvas.Scale > 0.0) {
		CUnit &unit = *Selected[0];
		if (!ClassicTransportShown(unit)) {
			UpdateUnitVariables(unit);
			const bool magic = ClassicMagicShown(unit);
			const classichud::Rect magicBar = classichud::magicBar(classichud::MagicRow);
			std::vector<ClassicPanelText> texts;
			ClassicInfoPanelTexts(unit, texts);
			const CFont *small = CFont::Get("remaster_hud_small");
			const CFont *main = CFont::Get("remaster_hud");
			for (const ClassicPanelText &t : texts) {
				const double unitsX = (t.x - Video.HudCanvas.X) / Video.HudCanvas.Scale;
				const double unitsY = (t.y + t.font->Height() / 2.0 - Video.HudCanvas.Y) / Video.HudCanvas.Scale;
				const bool tab = t.text.find("~|") != std::string::npos;
				const bool smallFace = t.font == small && small != main;
				const double x = classichud::panelX(unitsX);
				const double midY = smallFace ? classichud::panelSmallMidY(unitsY)
				                              : classichud::panelMidY(unitsY);
				// A stat line: the label to the colon's column, the value from its own column.
				std::string label;
				std::string value;
				if (!smallFace && x > classichud::PanelColonX - 8.0 && x <= classichud::PanelColonX
					&& classichud::splitStatLine(t.text, label, value)) {
					DrawClassicText(box, classichud::TextCap, x, midY, TextAlign::Tab, label);
					DrawClassicText(box, classichud::TextCap, classichud::PanelValueX, midY, TextAlign::Left,
					                value);
					continue;
				}
				// The mana figure: centred on the bar of the "Magic:" row.
				if (magic && !smallFace && t.centred && classichud::contains(magicBar, (int) x, (int) midY)) {
					DrawClassicText(box, classichud::TextCap, classichud::MagicTextMidX,
					                classichud::PanelStatMidY + classichud::PanelStatPitch * classichud::MagicRow,
					                TextAlign::Centre, t.text);
					continue;
				}
				DrawClassicText(box, smallFace ? classichud::TextCapSmall : classichud::TextCap, x, midY,
				                tab ? TextAlign::Tab : t.centred ? TextAlign::Centre : TextAlign::Left,
				                t.text);
			}
		}
	}
	// Hotkey letters of the command card: the key the Remastered card prints, under the same
	// preference (CButtonPanel::ShowCommandKey); at half brightness on a button that may not be
	// used.
	if (UI.ButtonPanel.ShowCommandKey && !Selected.empty() && Selected[0] != nullptr) {
		const SDL_Color dim = {128, 128, 128, 255};
		for (int i = 0; i < classichud::CommandColumns * classichud::CommandRows; ++i) {
			bool allowed = false;
			bool marked = false;
			if (!ClassicCommandButtonShown(i, allowed, marked)) {
				continue;
			}
			const std::string letter = classichud::hotkeyLetter(CurrentButtons[i].Key);
			if (letter.empty()) {
				continue;
			}
			const classichud::Rect cell = classichud::commandCell(i);
			DrawClassicText(box, classichud::TextCap, classichud::hotkeyMidX(cell),
			                classichud::hotkeyMidY(cell), TextAlign::Centre, letter,
			                allowed ? nullptr : &dim);
		}
	}
	// The command under the pointer: the texts of the Remastered tooltip in the classic box. A
	// button that may not be used has no pointer state (HandleMouseOn); its cell under the
	// pointer shows the tooltip all the same (o30_C05_hover_dim_keep).
	int tip = ButtonAreaUnderCursor == ButtonArea::Button ? ButtonUnderCursor : -1;
	if (tip < 0 && !ButtonAreaUnderCursor.has_value() && CursorOn == ECursorOn::Unknown) {
		tip = ClassicCommandCellAt(CursorScreenPos.x, CursorScreenPos.y);
	}
	bool allowed = false;
	bool marked = false;
	if (tip >= 0 && tip < (int) CurrentButtons.size() && ClassicCommandButtonShown(tip, allowed, marked)
		&& !CurrentButtons[tip].Hint.empty()) {
		DrawClassicTooltip(box, CurrentButtons[tip], allowed);
	}
}

void DrawClassicMessage(std::string_view text)
{
	if (!ClassicLayoutOn || !TheRenderer || text.empty()) {
		return;
	}
	const hdatlas::ClassicScreen box = ClassicBoxOnScreen();
	const SDL_Color yellow = {(Uint8) classichud::TooltipYellowRGB[0], (Uint8) classichud::TooltipYellowRGB[1],
	                          (Uint8) classichud::TooltipYellowRGB[2], 255};
	DrawClassicText(box, classichud::TextCap, classichud::MessageX, classichud::messageMidY(box.lh),
	                classichud::TextAlign::Left, text, &yellow);
}

bool ClassicMessageRect(std::string_view text, int &x, int &y, int &w, int &h)
{
	if (!ClassicLayoutOn || text.empty()) {
		return false;
	}
	const hdatlas::ClassicScreen box = ClassicBoxOnScreen();
	double scale = 1.0;
	const CFont *font = ClassicTextFace(classichud::TextCap, box, scale);
	if (font == nullptr || scale <= 0.0) {
		return false;
	}
	// As DrawClassicText places a left-aligned text.
	x = (int) std::lround(box.box.x + classichud::MessageX * (double) box.s);
	y = (int) std::lround(box.box.y + classichud::messageMidY(box.lh) * box.s - font->Height() * scale / 2.0);
	w = (int) std::lround(font->Width(text) * scale);
	h = (int) std::lround(font->Height() * scale);
	return w > 0 && h > 0;
}

void DrawClassicObjective(int n, std::string_view text)
{
	if (!ClassicLayoutOn || !TheRenderer || text.empty()) {
		return;
	}
	DrawClassicText(ClassicBoxOnScreen(), classichud::TextCap, classichud::ObjectivesX,
	                classichud::objectiveMidY(n), classichud::TextAlign::Left, text);
}

/**
**  Draw the classic HUD frame of the player's race into the box, the header buttons' faces and
**  the widgets that are art (DrawClassicHudWidgets), then the texts (DrawClassicHudTexts).
**
**  Art: Data/Art/classic/HUD/BG_<Race> (frame), Data/skins/Classic_HUD_Widgets and
**  Data/Art/classic/HUD/Portrait-face / Portrait-mask, read from the game folder, every pixel of
**  the art drawn as step x step screen pixels (nearest sampling). The frame is laid out for the
**  logical screen: the left column to its bottom, the strips on its right and bottom edges.
*/
void DrawClassicHud()
{
	if (!TheRenderer) {
		return;
	}
	std::string race = ClassicRaceName();
	if (race.empty()) {
		return;
	}
	const hdatlas::ClassicScreen box = ClassicBoxOnScreen();

	std::string raceCap = race;
	raceCap[0] = (char) toupper((unsigned char) raceCap[0]);
	if (const CAtlasGraphic *frame = ClassicHudAtlas("Data/Art/classic/HUD/BG_" + raceCap + ".json")) {
		hdatlas::ClassicHudPiece pieces[hdatlas::ClassicHudPieceMax];
		const int count = hdatlas::classicHudFramePieces(box.lw, box.lh, pieces, !Selected.empty());
		for (int i = 0; i < count; ++i) {
			const hdatlas::ClassicHudPiece &p = pieces[i];
			const hdatlas::ClassicRect r = hdatlas::classicToScreen(box, p.x, p.y, p.w, p.h);
			const SDL_Rect dst = {r.x, r.y, r.w, r.h};
			frame->DrawAtlasFramePart(frame->FrameByName(p.frame), p.w, p.h, dst);
		}
	}
	if (const CAtlasGraphic *widgets = ClassicHudAtlas("Data/skins/Classic_HUD_Widgets.json")) {
		// The Menu button's face: the held one while it is held down and while its window is up
		// (o11_28: the face with the light frame under the Game Menu).
		const std::string face =
			classichud::faceName("hud_" + race + "_small", ClassicUserButtonHeld(0) || ClassicWindowUp());
		const hdatlas::ClassicRect r = hdatlas::classicToScreen(
			box, hdatlas::ClassicMenuX, hdatlas::ClassicMenuY, hdatlas::ClassicMenuW, hdatlas::ClassicMenuH);
		const SDL_Rect dst = {r.x, r.y, r.w, r.h};
		widgets->DrawAtlasFramePart(widgets->FrameByName(face.c_str()), hdatlas::ClassicMenuW,
		                            hdatlas::ClassicMenuH, dst);
	}
	DrawClassicHudWidgets(box, race);
	DrawClassicHudTexts(box);
}

/**
**  Load Cursor.
*/
void CursorConfig::Load()
{
	Assert(!Name.empty());
	Cursor = CursorByIdent(Name);
	if (Cursor == nullptr) {
		return ;
	}
	Assert(Name == Cursor->Ident);
}

/**
**  Load the user interface graphics.
*/
void CUserInterface::Load()
{
	//  Load graphics
	for (auto &filler : Fillers) {
		filler.Load();
	}

	for (int i = 0; i <= FreeWorkersCount; ++i) {
		if (Resources[i].G) {
			Resources[i].G->Load();
		}
	}

	if (InfoPanel.G) {
		InfoPanel.G->Load();
	}
	if (ButtonPanel.G) {
		ButtonPanel.G->Load();
	}
	if (PieMenu.G) {
		PieMenu.G->Load();
	}

	if (Preference.IconFrameG) {
		Preference.IconFrameG->Load();
	}
	if (Preference.PressedIconFrameG) {
		Preference.PressedIconFrameG->Load();
	}
	// TOOLTIP-FRAME-FAINT: the popup frames (CclDefinePopup only names them). Loaded here the
	// renderer exists, so the frame gets its GPU texture and is drawn in the HUD pass, under the
	// tooltip's own icons and text.
	for (auto &popup : ButtonPopups) {
		if (popup->BackgroundImage) {
			popup->BackgroundImage->Load();
		}
	}

	//  Resolve cursors
	Point.Load();
	Glass.Load();
	Cross.Load();
	YellowHair.Load();
	GreenHair.Load();
	RedHair.Load();
	Scroll.Load();

	ArrowE.Load();
	ArrowNE.Load();
	ArrowN.Load();
	ArrowNW.Load();
	ArrowW.Load();
	ArrowSW.Load();
	ArrowS.Load();
	ArrowSE.Load();
}


bool CMapArea::Contains(const PixelPos &screenPos) const
{
	return this->X <= screenPos.x && screenPos.x <= this->EndX
		   && this->Y <= screenPos.y && screenPos.y <= this->EndY;
}

/**
**  Save the viewports.
**
**  @param file  Save file handle
**  @param ui    User interface to save
*/
static void SaveViewports(CFile &file, const CUserInterface &ui)
{
	// FIXME: don't save the number
	file.printf("DefineViewports(\"mode\", %d", ui.ViewportMode);
	for (size_t i = 0; i < ui.NumViewports; ++i) {
		const CViewport &vp = ui.Viewports[i];
		file.printf(",\n  \"viewport\", {%d, %d, %d}", vp.MapPos.x, vp.MapPos.y,
					vp.Unit ? UnitNumber(*vp.Unit) : -1);
	}
	file.printf(")\n\n");
}

/**
**  Save the user interface module.
**
**  @param file  Save file handle
*/
void SaveUserInterface(CFile &file)
{
	SaveViewports(file, UI);
	// The stored map positions. Guarded: a build that does not know the call reads the file all the same.
	file.printf("if (SetSavedMapPosition ~= nil) then\n");
	for (unsigned i = 0; i < SavedMapPositionCount; ++i) {
		const Vec2i pos = GetSavedMapPosition(i);
		file.printf("  SetSavedMapPosition(%u, %d, %d)\n", i, pos.x, pos.y);
	}
	file.printf("end\n\n");
}

/**
**  Clean up a user interface.
*/
CUserInterface::~CUserInterface()
{
}

/**
**  Clean up the user interface module.
*/
void CleanUserInterface()
{
	UI.Fillers.clear();

	// Resource Icons
	for (int i = 0; i <= FreeWorkersCount; ++i) {
		UI.Resources[i].G = nullptr;
	}

	// Info Panel
	UI.InfoPanel.G = nullptr;
	UI.InfoPanelContents.clear();

	Preference.IconFrameG = nullptr;
	Preference.PressedIconFrameG = nullptr;

	// Button Popups
	UI.ButtonPopups.clear();

	delete UI.SingleSelectedButton;
	UI.SelectedButtons.clear();
	UI.SelectedButtonsWide.clear();
	delete UI.SingleTrainingButton;
	UI.SingleTrainingText.clear();
	UI.TrainingButtons.clear();
	UI.TrainingText.clear();
	delete UI.UpgradingButton;
	delete UI.ResearchingButton;
	UI.TransportingButtons.clear();
	UI.UserButtons.clear();

	// Button Panel
	UI.ButtonPanel.G = nullptr;

	// Pie Menu
	UI.PieMenu.G = nullptr;

	// Backgrounds
	UI.VictoryBackgroundG = nullptr;
	UI.DefeatBackgroundG = nullptr;

	// Title Screens
	TitleScreens.clear();
}

void FreeButtonStyles()
{
	ButtonStyleHash.clear();
}

/**
**  Takes coordinates of a pixel in stratagus's window and computes
**  the map viewport which contains this pixel.
**
**  @param screenPos  pixel coordinate with origin at UL corner of screen
**
**  @return viewport pointer or nullptr if this pixel is not inside
**  any of the viewports.
**
**  @note This functions only works with rectangular viewports, when
**  we support shaped map window, this must be rewritten.
*/
CViewport *GetViewport(const PixelPos &screenPos)
{
	for (CViewport *vp = UI.Viewports; vp < UI.Viewports + UI.NumViewports; ++vp) {
		if (vp->Contains(screenPos)) {
			return vp;
		}
	}
	return nullptr;
}

/**
**  Takes an array of new Viewports which are supposed to have their
**  pixel geometry (CViewport::[XY] and CViewport::End[XY]) already
**  computed. Using this information as well as old viewport's
**  parameters fills in new viewports' CViewport::Map* parameters.
**  Then it replaces the old viewports with the new ones and finishes
**  the set-up of the new mode.
**
**  @param new_vps  The array of the new viewports
**  @param num_vps  The number of elements in the new_vps[] array.
*/

static void FinishViewportModeConfiguration(CViewport new_vps[], unsigned int num_vps)
{
	//  Compute location of the viewport using oldviewport
	for (unsigned int i = 0; i < num_vps; ++i) {
		new_vps[i].MapPos.x = 0;
		new_vps[i].MapPos.y = 0;
		const CViewport *vp = GetViewport(new_vps[i].GetTopLeftPos());
		if (vp) {
			const PixelDiff relDiff = new_vps[i].GetTopLeftPos() - vp->GetTopLeftPos();

			new_vps[i].Offset = relDiff + Map.TilePosToMapPixelPos_TopLeft(vp->MapPos) + vp->Offset;
		} else {
			// No viewport has a screen area yet (nothing has been played in this process), so
			// there is no old view to carry over by position. Keep the map position the viewport
			// already holds: a saved game sets it before the user interface is initialised.
			if (i < UI.NumViewports) {
				new_vps[i].MapPos = UI.Viewports[i].MapPos;
			}
			new_vps[i].Offset.x = 0;
			new_vps[i].Offset.y = 0;
		}
	}

	// Affect the old viewport.
	for (unsigned int i = 0; i < num_vps; ++i) {
		CViewport &vp = UI.Viewports[i];

		vp.TopLeftPos = new_vps[i].TopLeftPos;
		vp.BottomRightPos = new_vps[i].BottomRightPos;
		// Set the zoom before Set() so the tiles-in-view and scroll limits are
		// derived from the zoomed-down render size.
		vp.Zoom = MapViewportZoom();
		vp.Set(new_vps[i].MapPos, new_vps[i].Offset);
	}
	UI.NumViewports = num_vps;

	//
	//  Update the viewport pointers
	//
	UI.MouseViewport = GetViewport(CursorScreenPos);
	UI.SelectedViewport = std::min(UI.Viewports + UI.NumViewports - 1, UI.SelectedViewport);
}

#ifdef __ANDROID__
/**
**  Apply a new map magnification to every in-game viewport and re-derive the
**  dependent geometry (tiles-in-view, scroll clamp, fog and minimap box) so the
**  change takes effect on the next frame. Only the map render scale is touched;
**  the surrounding HUD, menus, fonts and cursors keep their native size because
**  nothing here alters Video.Width/Height or any HUD metric. Used by the
**  two-finger pinch-zoom gesture on touch devices.
**
**  @param zoom  Desired magnification, clamped to [1.0, 4.0] widened to hold the rule's own
**               default (0.5 on a screen under 960 rows, 2.5 at 2400 rows).
*/
void SetMapViewportsZoom(float zoom)
{
	const float lo = std::min(1.0f, MapViewportZoom());
	const float hi = std::max(4.0f, MapViewportZoom());
	if (zoom < lo) {
		zoom = lo;
	} else if (zoom > hi) {
		zoom = hi;
	}
	for (unsigned int i = 0; i < UI.NumViewports; ++i) {
		CViewport &vp = UI.Viewports[i];
		// Set the zoom first so Set() re-derives the tiles-in-view and scroll
		// limits from the new render size, then re-anchor on the current corner.
		vp.Zoom = zoom;
		vp.Set(vp.MapPos, vp.Offset);
	}
}
#endif

/**
**  Takes a viewport which is supposed to have its CViewport::[XY]
**  correctly filled-in and computes CViewport::End[XY] attributes
**  according to clipping information passed in other two arguments.
**
**  @param vp     The viewport.
**  @param ClipX  Maximum x-coordinate of the viewport's right side
**                as dictated by current UI's geometry and ViewportMode.
**  @param ClipY  Maximum y-coordinate of the viewport's bottom side
**                as dictated by current UI's geometry and ViewportMode.
**
**  @note It is supposed that values passed in Clip[XY] will
**  never be greater than UI::MapArea::End[XY].
**  However, they can be smaller according to the place
**  the viewport vp takes in context of current ViewportMode.
*/
static void ClipViewport(CViewport &vp, int ClipX, int ClipY)
{
	// begin with maximum possible viewport size
	// (the map is drawn at PixelTileSize * MapViewportZoom(), as UI.MapArea is sized above)
	vp.BottomRightPos.x = vp.TopLeftPos.x + (int)(Map.Info.MapWidth * PixelTileSize.x * MapViewportZoom()) - 1;
	vp.BottomRightPos.y = vp.TopLeftPos.y + (int)(Map.Info.MapHeight * PixelTileSize.y * MapViewportZoom()) - 1;

	// first clip it to MapArea size if necessary
	vp.BottomRightPos.x = std::min<int>(vp.BottomRightPos.x, ClipX);
	vp.BottomRightPos.y = std::min<int>(vp.BottomRightPos.y, ClipY);

	Assert(vp.BottomRightPos.x <= UI.MapArea.EndX);
	Assert(vp.BottomRightPos.y <= UI.MapArea.EndY);
}

/**
**  Compute viewport parameters for single viewport mode.
**
**  The parameters include viewport's width and height expressed
**  in pixels, its position with respect to Stratagus's window
**  origin, and the corresponding map parameters expressed in map
**  tiles with origin at map origin (map tile (0,0)).
*/
static void SetViewportModeSingle()
{
	CViewport new_vps[MAX_NUM_VIEWPORTS];

	DebugPrint("Single viewport set\n");

	new_vps[0].TopLeftPos.x = UI.MapArea.X;
	new_vps[0].TopLeftPos.y = UI.MapArea.Y;
	ClipViewport(new_vps[0], UI.MapArea.EndX, UI.MapArea.EndY);

	FinishViewportModeConfiguration(new_vps, 1);
}

/**
**  Compute viewport parameters for horizontally split viewport mode.
**  This mode splits the UI::MapArea with a horizontal line to
**  2 (approximately) equal parts.
**
**  The parameters include viewport's width and height expressed
**  in pixels, its position with respect to Stratagus's window
**  origin, and the corresponding map parameters expressed in map
**  tiles with origin at map origin (map tile (0,0)).
*/
static void SetViewportModeSplitHoriz()
{
	CViewport new_vps[MAX_NUM_VIEWPORTS];

	DebugPrint("Two horizontal viewports set\n");

	new_vps[0].TopLeftPos.x = UI.MapArea.X;
	new_vps[0].TopLeftPos.y = UI.MapArea.Y;
	ClipViewport(new_vps[0], UI.MapArea.EndX,
				 UI.MapArea.Y + (UI.MapArea.EndY - UI.MapArea.Y + 1) / 2);

	new_vps[1].TopLeftPos.x = UI.MapArea.X;
	new_vps[1].TopLeftPos.y = new_vps[0].BottomRightPos.y + 1;
	ClipViewport(new_vps[1], UI.MapArea.EndX, UI.MapArea.EndY);

	FinishViewportModeConfiguration(new_vps, 2);
}

/**
**  Compute viewport parameters for horizontal 3-way split viewport mode.
**  This mode splits the UI::MapArea with a horizontal line to
**  2 (approximately) equal parts, then splits the bottom part vertically
**  to another 2 parts.
**
**  The parameters include viewport's width and height expressed
**  in pixels, its position with respect to Stratagus's window
**  origin, and the corresponding map parameters expressed in map
**  tiles with origin at map origin (map tile (0,0)).
*/
static void SetViewportModeSplitHoriz3()
{
	CViewport new_vps[MAX_NUM_VIEWPORTS];

	DebugPrint("Horizontal 3-way viewport division set\n");

	new_vps[0].TopLeftPos.x = UI.MapArea.X;
	new_vps[0].TopLeftPos.y = UI.MapArea.Y;
	ClipViewport(new_vps[0], UI.MapArea.EndX,
				 UI.MapArea.Y + (UI.MapArea.EndY - UI.MapArea.Y + 1) / 2);

	new_vps[1].TopLeftPos.x = UI.MapArea.X;
	new_vps[1].TopLeftPos.y = new_vps[0].BottomRightPos.y + 1;
	ClipViewport(new_vps[1],
				 UI.MapArea.X + (UI.MapArea.EndX - UI.MapArea.X + 1) / 2,
				 UI.MapArea.EndY);

	new_vps[2].TopLeftPos.x = new_vps[1].BottomRightPos.x + 1;
	new_vps[2].TopLeftPos.y = new_vps[0].BottomRightPos.y + 1;
	ClipViewport(new_vps[2], UI.MapArea.EndX, UI.MapArea.EndY);

	FinishViewportModeConfiguration(new_vps, 3);
}

/**
**  Compute viewport parameters for vertically split viewport mode.
**  This mode splits the UI::MapArea with a vertical line to
**  2 (approximately) equal parts.
**
**  The parameters  include viewport's width and height expressed
**  in pixels, its position with respect to Stratagus's window
**  origin, and the corresponding map parameters expressed in map
**  tiles with origin at map origin (map tile (0,0)).
*/
static void SetViewportModeSplitVert()
{
	CViewport new_vps[MAX_NUM_VIEWPORTS];

	DebugPrint("Two vertical viewports set\n");

	new_vps[0].TopLeftPos.x = UI.MapArea.X;
	new_vps[0].TopLeftPos.y = UI.MapArea.Y;
	ClipViewport(new_vps[0],
				 UI.MapArea.X + (UI.MapArea.EndX - UI.MapArea.X + 1) / 2,
				 UI.MapArea.EndY);

	new_vps[1].TopLeftPos.x = new_vps[0].BottomRightPos.x + 1;
	new_vps[1].TopLeftPos.y = UI.MapArea.Y;
	ClipViewport(new_vps[1], UI.MapArea.EndX, UI.MapArea.EndY);

	FinishViewportModeConfiguration(new_vps, 2);
}

/**
**  Compute viewport parameters for 4-way split viewport mode.
**  This mode splits the UI::MapArea vertically *and* horizontally
**  to 4 (approximately) equal parts.
**
**  The parameters  include viewport's width and height expressed
**  in pixels, its position with respect to Stratagus's window
**  origin, and the corresponding map parameters expressed in map
**  tiles with origin at map origin (map tile (0,0)).
*/
static void SetViewportModeQuad()
{
	CViewport new_vps[MAX_NUM_VIEWPORTS];

	DebugPrint("Four viewports set\n");

	new_vps[0].TopLeftPos.x = UI.MapArea.X;
	new_vps[0].TopLeftPos.y = UI.MapArea.Y;
	ClipViewport(new_vps[0],
				 UI.MapArea.X + (UI.MapArea.EndX - UI.MapArea.X + 1) / 2,
				 UI.MapArea.Y + (UI.MapArea.EndY - UI.MapArea.Y + 1) / 2);

	new_vps[1].TopLeftPos.x = new_vps[0].BottomRightPos.x + 1;
	new_vps[1].TopLeftPos.y = UI.MapArea.Y;
	ClipViewport(new_vps[1],
				 UI.MapArea.EndX,
				 UI.MapArea.Y + (UI.MapArea.EndY - UI.MapArea.Y + 1) / 2);

	new_vps[2].TopLeftPos.x = UI.MapArea.X;
	new_vps[2].TopLeftPos.y = new_vps[0].BottomRightPos.y + 1;
	ClipViewport(new_vps[2],
				 UI.MapArea.X + (UI.MapArea.EndX - UI.MapArea.X + 1) / 2,
				 UI.MapArea.EndY);

	new_vps[3].TopLeftPos.x = new_vps[1].TopLeftPos.x;
	new_vps[3].TopLeftPos.y = new_vps[2].TopLeftPos.y;
	ClipViewport(new_vps[3], UI.MapArea.EndX, UI.MapArea.EndY);

	FinishViewportModeConfiguration(new_vps, 4);
}

/**
**  Sets up (calls geometry setup routines for) a new viewport mode.
**
**  @param new_mode  New mode's number.
*/
void SetViewportMode(ViewportModeType new_mode)
{
	switch (UI.ViewportMode = new_mode) {
		case VIEWPORT_SINGLE:
			SetViewportModeSingle();
			break;
		case VIEWPORT_SPLIT_HORIZ:
			SetViewportModeSplitHoriz();
			break;
		case VIEWPORT_SPLIT_HORIZ3:
			SetViewportModeSplitHoriz3();
			break;
		case VIEWPORT_SPLIT_VERT:
			SetViewportModeSplitVert();
			break;
		case VIEWPORT_QUAD:
			SetViewportModeQuad();
			break;
		default:
			DebugPrint("trying to set an unknown mode!!\n");
			break;
	}
}

/**
**  Sets up a new viewport mode.
**
**  @param new_mode  New mode's number.
*/
void SetNewViewportMode(ViewportModeType new_mode)
{
	NewViewportMode = new_mode;
	if (NewViewportMode >= NUM_VIEWPORT_MODES) {
		NewViewportMode = VIEWPORT_SINGLE;
	}
	if (NewViewportMode < 0) {
		NewViewportMode = (ViewportModeType)(NUM_VIEWPORT_MODES - 1);
	}
}

/**
**  Cycles through predefined viewport modes (geometry configurations)
**  in order defined by the ViewportMode enumerated type.
**
**  @param step   The size of step used for cycling. Values that
**               make sense are mostly 1 (next viewport mode) and
**               -1 (previous viewport mode).
*/
void CycleViewportMode(int step)
{
	NewViewportMode = (ViewportModeType)(UI.ViewportMode + step);
	if (NewViewportMode >= NUM_VIEWPORT_MODES) {
		NewViewportMode = VIEWPORT_SINGLE;
	}
	if (NewViewportMode < 0) {
		NewViewportMode = (ViewportModeType)(NUM_VIEWPORT_MODES - 1);
	}
}

void CheckViewportMode()
{
	if (NewViewportMode != UI.ViewportMode) {
		SetViewportMode(NewViewportMode);
	}
}

/**
**  Check if mouse scrolling is enabled
*/
bool GetMouseScroll()
{
	return UI.MouseScroll;
}

/**
**  Enable/disable scrolling with the mouse
**
**  @param enabled  True to enable mouse scrolling, false to disable
*/
void SetMouseScroll(bool enabled)
{
	UI.MouseScroll = enabled;
}

/**
**  Check if keyboard scrolling is enabled
*/
bool GetKeyScroll()
{
	return UI.KeyScroll;
}

/**
**  Enable/disable scrolling with the keyboard
**
**  @param enabled  True to enable keyboard scrolling, false to disable
*/
void SetKeyScroll(bool enabled)
{
	UI.KeyScroll = enabled;
}

/**
**  Check if mouse grabbing is enabled
*/
bool GetGrabMouse()
{
	return SdlGetGrabMouse();
}

/**
**  Enable/disable grabbing the mouse
**
**  @param enabled  True to enable mouse grabbing, false to disable
*/
void SetGrabMouse(bool enabled)
{
	ToggleGrabMouse(enabled ? 1 : -1);
}

/**
**  Check if scrolling stops when leaving the window
*/
bool GetLeaveStops()
{
	return LeaveStops;
}

/**
**  Enable/disable leaving the window stops scrolling
**
**  @param enabled  True to stop scrolling, false to disable
*/
void SetLeaveStops(bool enabled)
{
	LeaveStops = enabled;
}

//@}
