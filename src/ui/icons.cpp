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
/**@name icons.cpp - The icons. */
//
//      (c) Copyright 1998-2012 by Lutz Sammer and Jimmy Salmon
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

#include "icons.h"

#include "hd_synth_graphics.h"
#include "menus.h"
#include "player.h"
#include "translate.h"
#include "ui.h"
#include "unit.h"
#include "video.h"

#include <cmath>
#include <map>
#include <memory>

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

using IconMap = std::map<std::string, std::unique_ptr<CIcon>, std::less<>>;
static IconMap Icons;   /// Map of ident to icon.


/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

/**
**  CIcon constructor
*/
CIcon::CIcon(const std::string &ident) : Ident(ident)
{
}

/**
**  Create a new icon
**
**  @param ident  Icon identifier
**
**  @return       New icon
*/
/* static */ CIcon *CIcon::New(const std::string &ident)
{
	auto &icon = Icons[ident];

	if (icon == nullptr) {
		icon = std::make_unique<CIcon>(ident);
	}
	return icon.get();
}

/**
**  Get an icon
**
**  @param ident  Icon identifier
**
**  @return       The icon
*/
/* static */ CIcon *CIcon::Get(std::string_view ident)
{
	IconMap::iterator it = Icons.find(ident);
	if (it == Icons.end()) {
		ErrorPrint("icon not found: '%s'\n", ident.data());
		ExitFatal(-1);
	}
	return it->second.get();
}

void CIcon::Load()
{
	Assert(G);
	G->Load();
	if (Preference.GrayscaleIcons) {
		GScale = G->Clone(true);
	}
	if (Frame >= G->NumFrames) {
		DebugPrint("Invalid icon frame: %s - %d\n", Ident.c_str(), Frame);
		Frame = 0;
	}
	for (auto g : this->SingleSelectionG) {
		g->Load();
		if (Frame >= G->NumFrames) {
			DebugPrint("Invalid icon frame for single selection graphic: %s - %d\n", Ident.c_str(), Frame);
		}
	}
	for (auto g : this->GroupSelectionG) {
		g->Load();
		if (Frame >= G->NumFrames) {
			DebugPrint("Invalid icon frame for group selection graphic: %s - %d\n", Ident.c_str(), Frame);
		}
	}
	for (auto g : this->ContainedG) {
		g->Load();
		if (Frame >= G->NumFrames) {
			DebugPrint("Invalid icon frame for transport selection graphic: %s - %d\n", Ident.c_str(), Frame);
		}
	}
}

/**
**  Draw icon at pos.
**
**  @param player  Player pointer used for icon colors
**  @param pos     display pixel position
*/
void CIcon::DrawIcon(const PixelPos &pos, const int player) const
{
	if (player != -1 ) {
		this->G->DrawPlayerColorFrameClip(player, this->Frame, pos.x, pos.y);
	} else {
		this->G->DrawFrameClip(this->Frame, pos.x, pos.y);
	}
}

/**
**  Draw grayscale icon at pos.
**
**  @param pos     display pixel position
*/
void CIcon::DrawGrayscaleIcon(const PixelPos &pos, const int player) const
{
	if (this->GScale) {
		if (player != -1) {
			this->GScale->DrawPlayerColorFrameClip(player, this->Frame, pos.x, pos.y);
		} else {
			this->GScale->DrawFrameClip(this->Frame, pos.x, pos.y);
		}
	}
}

/**
**  Draw cooldown spell effect on icon at pos.
**
**  @param pos       display pixel position
**  @param percent   cooldown percent
*/
void CIcon::DrawCooldownSpellIcon(const PixelPos &pos, const int percent) const
{
	// TO-DO: implement more effect types (clock-like)
	if (this->GScale) {
		this->GScale->DrawFrameClip(this->Frame, pos.x, pos.y);
		const int height = (G->Height * (100 - percent)) / 100;
		this->G->DrawSubClip(G->frame_map[Frame].x, G->frame_map[Frame].y + G->Height - height,
							 G->Width, height, pos.x, pos.y + G->Height - height);
	} else {
		DebugPrint("Enable grayscale icon drawing in your game to achieve special effects for cooldown spell icons");
		this->DrawIcon(pos);
	}
}

static void ApplyPaletteSwaps(const std::vector<PaletteSwap> &swaps, const CUnit &unit, CGraphic *graphic, bool def = false)
{
	for (PaletteSwap swap : swaps) {
		unsigned int varIdx = swap.GetUnitVariableIndex();
		if (unit.Variable[varIdx].Enable) {
			int value = def ? unit.Variable[varIdx].Max : unit.Variable[varIdx].Value;
			const SDL_Color *colors = swap.GetColorsForPercentAndAlternative(value, unit.Variable[varIdx].Max, UnitNumber(unit));
			SDL_SetPaletteColors(graphic->getSurface()->format->palette,
			                     colors,
			                     swap.GetColorIndexStart(),
			                     swap.GetColorCount());
		}
	}
}

static void DrawByHealthIcon(const CIcon *icon,
                             const std::vector<std::shared_ptr<CPlayerColorGraphic>> &graphics,
                             const ButtonStyle &style,
                             unsigned flags,
                             const PixelPos &pos,
                             const std::string &text,
                             const CUnit &unit,
                             const std::vector<PaletteSwap> &swaps)
{
	int playerColor = unit.RescuedFrom
				? GameSettings.Presets[unit.RescuedFrom->Index].PlayerColor
				: GameSettings.Presets[unit.Player->Index].PlayerColor;
	int sz = graphics.size();
	if (!sz) {
		ApplyPaletteSwaps(swaps, unit, icon->G.get());
		icon->DrawUnitIcon(style, flags, pos, text, playerColor);
		// the normal icons are used outside this code path as well, so undo the swap immediately
		ApplyPaletteSwaps(swaps, unit, icon->G.get(), true);
	} else {
		// TODO: we could have this more configurable?
		int graphicIdx = 0;
		if (sz > 1 && unit.Variable[HP_INDEX].Max) {
			graphicIdx = ((sz - 1) * unit.Variable[HP_INDEX].Value) / unit.Variable[HP_INDEX].Max;
		}
		ButtonStyle s(style);
		auto g = graphics[graphicIdx];
		ApplyPaletteSwaps(swaps, unit, g.get());
		s.Default.Sprite = s.Hover.Sprite = s.Clicked.Sprite = g;
		s.Default.Frame = s.Hover.Frame = s.Clicked.Frame = icon->Frame;
		DrawUIButton(&s, flags, pos.x, pos.y, text, /*scaleSpriteToStyle=*/true, playerColor);
	}
}

/**
**  IG-T8 "Portrait Icon Style" (option_portrait_style_title: Scaled / Cropped). MEASURED in the
**  running original client on the selected unit's info-panel icon, at TWO different UI scales so the
**  rule could be told apart from the scale:
**
**    UI Scale Automatic:  Cropped 50 px wide, Scaled 36  -> 0.720
**    UI Scale Classic:    Cropped 80 x 65.5,  Scaled 57 x 47.5 -> 0.7125 wide, 0.725 tall
**
**  In both modes the icon keeps the SAME top-left origin, the SAME aspect, and shows the SAME region
**  of the art (compared at 4x zoom): "Scaled" is the whole icon -- frame, art and the bar under it --
**  drawn uniformly smaller. So the port reproduces the measured observable: one uniform factor.
**
**  LABEL: STRONGLY SUPPORTED, not VERIFIED. Two UI scales agree to within 1%, but WHY the original
**  lands on ~0.72 is not established (it is not fit-vs-fill of this art in this cell -- the art's own
**  aspect is 207:171 = 1.2105 in Art/hd/HUD/Portrait-face.json, the same 46:38 as the classic sheet,
**  and both modes render at that aspect). Confirmation still owed: a third measurement at UI Scale
**  "Fixed" with the slider moved.
**  SCOPE, disclosed: applied to the info-panel selected-unit icon only. Whether the original's
**  setting also resizes the command-panel cells is NOT established -- the captures were taken with an
**  empty command panel -- so those are left alone rather than guessed at.
*/
bool PortraitIconScaled = false;
static constexpr double PORTRAIT_SCALED_FACTOR = 0.72;

/// NG64: the factor currently in force (1 when Cropped), so Lua can size the portrait's health bar
/// from the same constant instead of carrying its own copy.
double GetPortraitIconScaleFactor()
{
	return PortraitIconScaled ? PORTRAIT_SCALED_FACTOR : 1.0;
}

void CIcon::DrawSingleSelectionIcon(const ButtonStyle &style, unsigned flags,
						 const PixelPos &pos, const std::string &text, const CUnit &unit) const
{
	if (PortraitIconScaled && style.Width > 0 && style.Height > 0) {
		ButtonStyle small(style);
		small.Width = static_cast<int>(style.Width * PORTRAIT_SCALED_FACTOR + 0.5);
		small.Height = static_cast<int>(style.Height * PORTRAIT_SCALED_FACTOR + 0.5);
		DrawByHealthIcon(this, this->SingleSelectionG, small, flags, pos, text, unit,
		                 this->PaletteSwaps);
		return;
	}
	DrawByHealthIcon(this, this->SingleSelectionG, style, flags, pos, text, unit, this->PaletteSwaps);
}

void CIcon::DrawGroupSelectionIcon(const ButtonStyle &style, unsigned flags,
						 const PixelPos &pos, const std::string &text, const CUnit &unit) const
{
	DrawByHealthIcon(this, this->GroupSelectionG, style, flags, pos, text, unit, this->PaletteSwaps);
}

void CIcon::DrawContainedIcon(const ButtonStyle &style, unsigned flags,
						 const PixelPos &pos, const std::string &text, const CUnit &unit) const
{
	DrawByHealthIcon(this, this->ContainedG, style, flags, pos, text, unit, this->PaletteSwaps);
}

void CIcon::ClearExtraGraphics()
{
	this->SingleSelectionG.clear();
	this->GroupSelectionG.clear();
	this->ContainedG.clear();
}

void CIcon::AddSingleSelectionGraphic(std::shared_ptr<CPlayerColorGraphic> g)
{
	this->SingleSelectionG.push_back(g);
}

void CIcon::AddGroupSelectionGraphic(std::shared_ptr<CPlayerColorGraphic> g)
{
	this->GroupSelectionG.push_back(g);
}

void CIcon::AddContainedGraphic(std::shared_ptr<CPlayerColorGraphic> g)
{
	this->ContainedG.push_back(g);
}

void CIcon::SetPaletteSwaps(std::vector<PaletteSwap> &newSwaps)
{
	this->PaletteSwaps.clear();
	for (auto s : newSwaps) {
		this->PaletteSwaps.push_back(s);
	}
}

/**
**  IC1: draw the native Remastered command-cell frame (portrait_button, state-dependent) behind an
**  icon. The frame strip `ui/hd_cmd_frame.png` is synthesised in-engine from the native
**  Modern_HUD_Widgets atlas (see hd_synth_graphics.cpp). If that atlas is absent the strip degrades
**  to a placeholder (NumFrames != FRAME_STATES); we then draw nothing so classic installs are
**  untouched. Frame order: 0 default,1 hovered,2 active,3 highlight,4 highlight_hovered,5 highlight_active.
*/
/// PANEL-A: the cell is 44 base units high (132 px at k = 3, captures e01, u03) and its picture
/// 46 units wide (138 px). A cell of the 4-column selection grid is 38 units wide and as high
/// as the others (114 x 132 px, capture u11): its unit is read from the height.
static constexpr int HD_CELL_UNITS_H = 44;
static constexpr int HD_PICTURE_UNITS_W = 46;

/// px of one base unit (of the 640x480 layout) in a cell `w` x `h` px: the cell is 52 x 44
/// units, or narrower and 44 units high.
static double HDCellUnit(int w, int h)
{
	const double uw = w / (double) hdicon::CELL_UNITS_W;
	const double uh = h / (double) HD_CELL_UNITS_H;
	return (h > 0 && uh > uw) ? uh : uw;
}

/// @return true when the frame was drawn (the picture then goes inside it, see DrawUnitIcon)
static bool DrawHDCommandFrame(unsigned flags, const PixelPos &pos, int w, int h)
{
	static int state = 0;   // 0 = untried, 1 = active, -1 = disabled
	static std::shared_ptr<CGraphic> frameG;
	if (state == 0) {
		frameG = CGraphic::New("ui/hd_cmd_frame.png", hdicon::FRAME_PX, hdicon::FRAME_PX);
		frameG->Load();
		state = (frameG && frameG->NumFrames >= hdicon::FRAME_STATES) ? 1 : -1;
	}
	if (state != 1) {
		return false;
	}
	int idx = (flags & IconSelected) ? 3 : 0;   // highlight vs default base
	if (flags & IconClicked) {
		idx += 2;                                // ..._active
	} else if (flags & IconActive) {
		idx += 1;                                // ..._hovered
	}
	if (w <= 0 || h <= 0) {
		w = hdicon::CELL_W;
		h = hdicon::CELL_H;
	}
	// M666-2: the skin's own rule -- the 80-px sprite nine-sliced at 25 px, 5 sprite px per base
	// unit -- in a widget that is the visible cell plus the sprite's empty rim (1 unit a side).
	// At k = 3: widget 162 x 138 at (-3, -3), corners 15 px, ring 9.6 px against the 9 px read in
	// captures e01 and hi10 (outline 1, bevel 7, inner line 1).
	const double u = HDCellUnit(w, h);
	const int m = (int) std::lround(u * hdicon::FRAME_MARGIN_U);
	const SDL_Rect dst = {pos.x - m, pos.y - m, w + 2 * m, h + 2 * m};
	frameG->DrawNineSliceScaled(dst, hdicon::FRAME_SLICE,
	                            (int) std::lround(u * hdicon::FRAME_SLICE / hdicon::FRAME_SCALE),
	                            (unsigned) idx);
	return true;
}

/**
**  Draw unit icon 'icon' with border on x,y
**
**  @param style   Button style
**  @param flags   State of icon (clicked, mouse over...)
**  @param pos     display pixel position
**  @param text    Optional text to display
*/
void CIcon::DrawUnitIcon(const ButtonStyle &style, unsigned flags,
						 const PixelPos &pos, const std::string &text, int player) const
{
	const bool framed = DrawHDCommandFrame(flags, pos, style.Width, style.Height);

	ButtonStyle s(style);
	// M666-2: inside the frame the picture is the widget less its padding (4 units), i.e. the
	// visible cell less 3 units a side: 46 x 38 units, 138 x 114 px at (9, 9) for k = 3 (captures
	// e01, hi10: picture x 27..164, y 507..620 in a cell at 18, 498).
	PixelPos picPos(pos);
	int pictureW = 0;   // > 0: the picture's own width, wider than the room inside the frame
	if (framed && style.Width > 0 && style.Height > 0) {
		const double u = HDCellUnit(style.Width, style.Height);
		const int inset = (int) std::lround(u * (hdicon::PICTURE_PAD_U - hdicon::FRAME_MARGIN_U));
		picPos.x += inset;
		picPos.y += inset;
		s.Width = style.Width - 2 * inset;
		s.Height = style.Height - 2 * inset;
		// A narrow cell is clearly under the 52 units: one unit of margin, so that a cell of
		// the usual shape at another size (the Scaled portrait) never counts by rounding.
		const int fullW = (int) std::lround(u * HD_PICTURE_UNITS_W);
		if (style.Width < u * (hdicon::CELL_UNITS_W - 1) && fullW > s.Width) {
			pictureW = fullW;
		}
	}

	s.Default.Sprite = s.Hover.Sprite = s.Clicked.Sprite = this->G;
	s.Default.Frame = s.Hover.Frame = s.Clicked.Frame = this->Frame;
	if (!(flags & IconSelected) && (flags & IconAutoCast)) {
		s.Default.BorderColorRGB = UI.ButtonPanel.AutoCastBorderColorRGB;
		s.Default.BorderColor = 0;
		s.Default.BorderSize = 2;
	}
	if (Preference.IconsShift && Preference.IconFrameG && Preference.PressedIconFrameG) {
		int shift = 0;
		if (!(flags & IconClicked)) {
			int xoffset = (s.Width - Preference.IconFrameG->Width) / 2;
			int yoffset = (s.Height - Preference.IconFrameG->Height) / 2;
			Preference.IconFrameG->DrawClip(pos.x + xoffset, pos.y + yoffset);
		} else { // Shift the icon a bit to make it look like it's been pressed.
			shift = 1;
			int xoffset = (s.Width - Preference.PressedIconFrameG->Width) / 2 + shift;
			int yoffset = (s.Height - Preference.PressedIconFrameG->Height) / 2 + shift;
			Preference.PressedIconFrameG->DrawClip(pos.x + xoffset, pos.y + yoffset);
		}
		DrawUIButton(&s, flags, pos.x + shift, pos.y + shift, text, /*scaleSpriteToStyle=*/true, player);
		if (flags & IconSelected) {
			Video.DrawRectangle(ColorGreen, pos.x + shift, pos.y + shift, s.Width, s.Height);
		}
	} else if (Preference.IconsShift) {
		// Left and top edge of Icon
		Video.DrawHLine(ColorWhite, pos.x - 1, pos.y - 1, 49);
		Video.DrawVLine(ColorWhite, pos.x - 1, pos.y, 40);
		Video.DrawVLine(ColorWhite, pos.x, pos.y + 38, 2);
		Video.DrawHLine(ColorWhite, pos.x + 46, pos.y, 2);

		// Bottom and Right edge of Icon
		Video.DrawHLine(ColorGray, pos.x + 1, pos.y + 38, 47);
		Video.DrawHLine(ColorGray, pos.x + 1, pos.y + 39, 47);
		Video.DrawVLine(ColorGray, pos.x + 46, pos.y + 1, 37);
		Video.DrawVLine(ColorGray, pos.x + 47, pos.y + 1, 37);

		Video.DrawRectangle(ColorBlack, pos.x - 3, pos.y - 3, 52, 44);
		Video.DrawRectangle(ColorBlack, pos.x - 4, pos.y - 4, 54, 46);

		if (flags & IconActive) { // Code to make a border appear around the icon when the mouse hovers over it.
			Video.DrawRectangle(ColorGray, pos.x - 4, pos.y - 4, 54, 46);
			DrawUIButton(&s, flags, pos.x, pos.y, text, /*scaleSpriteToStyle=*/true, player);
		}

		if (flags & IconClicked) { // Shift the icon a bit to make it look like it's been pressed.
			DrawUIButton(&s, flags, pos.x + 1, pos.y + 1, text, /*scaleSpriteToStyle=*/true, player);
			if (flags & IconSelected) {
				Video.DrawRectangle(ColorGreen, pos.x + 1, pos.y + 1, 46, 38);
			}
			Video.DrawRectangle(ColorGray, pos.x, pos.y, 48, 40);
			Video.DrawVLine(ColorDarkGray, pos.x - 1, pos.y - 1, 40);
			Video.DrawHLine(ColorDarkGray, pos.x - 1, pos.y - 1, 49);
			Video.DrawHLine(ColorDarkGray, pos.x - 1, pos.y + 39, 2);

			Video.DrawRectangle(ColorGray, pos.x - 4, pos.y - 4, 54, 46);
		} else {
			DrawUIButton(&s, flags, pos.x, pos.y, text, /*scaleSpriteToStyle=*/true, player);
			if (flags & IconSelected) {
				Video.DrawRectangle(ColorGreen, pos.x, pos.y, 46, 38);
			}
		}
	} else if (pictureW > 0) {
		// PANEL-A: a narrow cell shows the middle of the picture at the picture's own size, it is
		// not squeezed: the 96 px inside a 4-column cell are columns 21..116 of the 138-px
		// picture (measured: capture u11 against u07 and u09).
		ButtonStyle pic(s);
		pic.Width = pictureW;
		pic.Default.BorderSize = pic.Hover.BorderSize = pic.Clicked.BorderSize = 0;
		SDL_Rect room = {picPos.x, picPos.y, s.Width, s.Height};
		PushClipping();
		SetClipping(room.x, room.y, room.x + room.w - 1, room.y + room.h - 1);
		SDL_SetClipRect(TheScreen, &room);
		DrawUIButton(&pic, flags, picPos.x - (pictureW - s.Width) / 2, picPos.y, text,
		             /*scaleSpriteToStyle=*/true, player);
		SDL_SetClipRect(TheScreen, nullptr);
		PopClipping();
		// the style's own border goes round the part that shows
		s.Default.Sprite = s.Hover.Sprite = s.Clicked.Sprite = nullptr;
		DrawUIButton(&s, flags, picPos.x, picPos.y, "", /*scaleSpriteToStyle=*/true, player);
	} else {
		DrawUIButton(&s, flags, picPos.x, picPos.y, text, /*scaleSpriteToStyle=*/true, player);
	}
}

/**
**  Load the Icon
*/
bool IconConfig::LoadNoLog()
{
	if (Name.empty()) {
		return false;
	}

	Icon = CIcon::Get(Name);
	return Icon != nullptr;
}

/**
**  Load the Icon
*/
bool IconConfig::Load()
{
	if (this->Name.empty()) {
		return false;
	}
	if (LoadNoLog() == true) {
		ShowLoadProgress(_("Icon %s"), this->Name.c_str());
		return true;
	} else {
		ErrorPrint("Can't find icon '%s'\n", this->Name.c_str());
		return false;
	}
}

/**
**  Load the graphics for the icons.
*/
void LoadIcons()
{
	for (auto &[key, icon] : Icons) {
		ShowLoadProgress(_("Icons %s"), icon->G->File.c_str());
		icon->Load();
	}
}

/**
**  Clean up memory used by the icons.
*/
void CleanIcons()
{
	Icons.clear();
}

//@}
