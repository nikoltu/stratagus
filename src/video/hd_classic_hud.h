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
/**@name hd_classic_hud.h - Classic view: the places of the HUD widgets in the logical screen. */
//
//      (c) Copyright 2026 by the WargusHD project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.

#ifndef __HD_CLASSIC_HUD_H__
#define __HD_CLASSIC_HUD_H__

#include <string>
#include <vector>

class CContentType;
class CFont;
class CUnit;

//@{

/// Classic view: where the widgets of the classic HUD are in the logical screen of the original
/// (hdatlas::classicScreen). Every number is a logical pixel read from captures of the original
/// at a 1366 x 768 client (logical 1024 x 768, one screen pixel each, box at client x 171), where
/// the widget's art was found pixel for pixel: o11_13 (nothing selected), o11_16 (Peasant),
/// o11_84 (Town Hall). The parts of the left column keep these places at every logical size
/// (they hang on the screen's top-left); the resource row hangs on its middle. Art: Data/skins/
/// Classic_HUD_Widgets and Data/Art/classic/HUD/Portrait-face / Portrait-mask of the game folder.
/// Pure geometry: no engine type, usable from the host tests.
namespace classichud
{

struct Rect {
	int x;
	int y;
	int w;
	int h;
};

// Header row. Sprite `f5` (40 x 20; 108 of its 108 drawn pixels equal) with the picture
// `f5_inset_<race>` (180 x 80) inside it at one fifth of its size (skin `classic_hud_f5`,
// "padding" [2, 2]; 477 of 576 pixels equal with the middle pixel of every 5 x 5 block, the rest
// is under the "F5" mark). Sprite `hud_<race>_small` (60 x 20) for Menu (hdatlas::ClassicMenuX)
// and for Pause (1038 of 1200 equal, the rest is under its caption).
constexpr Rect F5Face = {4, 2, 40, 20};
constexpr Rect F5Inset = {6, 4, 36, 16};
constexpr Rect PauseFace = {108, 2, 60, 20};

// The F5 picture at a step above 1 is its 36 x 16 logical pixels repeated, not the art scaled
// (o21_54 at step 3, o21_50 at step 2: every block of the slot one colour): a logical pixel
// shows the middle pixel of its 5 x 5 block of the art.
constexpr int F5InsetArtW = 180;
constexpr int F5InsetArtH = 80;
constexpr int F5InsetBlock = 5;
/// The art pixel of `f5_inset_<race>` that the logical pixel (lx, ly) of the picture shows.
constexpr Rect f5InsetSample(int lx, int ly)
{
	return {lx * F5InsetBlock + F5InsetBlock / 2, ly * F5InsetBlock + F5InsetBlock / 2, 1, 1};
}
// The "F5" mark on the slot is text of the small face (ink x 8.67..15, rows 6.33..11 at step 3:
// 14 px; 9 px at step 2; rows 6..9 at step 1). It is not drawn: the project's rule for this
// button (no "F5" mark in either view).

// Resource row: sprites `icons_<N>` (14 x 14), 196 of 196 pixels equal each. Gold, lumber, oil,
// food in this order; `icons_3` is in no capture. The row hangs on the middle of the logical
// screen, pitch 75: gold at 250 of 640, 430 of 1000, 442 of 1024, 463 of 1067, 290 of 720.
constexpr int ResourceIconCount = 4;
constexpr int ResourceIconSize = 14;
constexpr int ResourceIconY = 0;
constexpr int ResourceIconPitch = 75;
constexpr int ResourceIconLeftOfMiddle = 70;
/// The x of resource icon n (0..3) in a logical screen `logicalW` wide.
constexpr int resourceIconX(int logicalW, int n)
{
	return logicalW / 2 - ResourceIconLeftOfMiddle + ResourceIconPitch * n;
}
constexpr int ResourceIconSprite[ResourceIconCount] = {0, 1, 2, 4};

// A picture of Portrait-face is 46 x 38; its cell is the sprite `portrait_button` (16 x 16,
// skin `classic_portrait_button`: "9slice" 7, "padding" 4) nine-sliced on a rect 4 px larger on
// every side (736 of 736 ring pixels equal for each of the seven cells of o11_16).
constexpr int PictureW = 46;
constexpr int PictureH = 38;
constexpr int CellPadding = 4;
constexpr int CellW = PictureW + 2 * CellPadding;   // 54
constexpr int CellH = PictureH + 2 * CellPadding;   // 46
constexpr int CellSprite = 16;
constexpr int CellSlice = 7;

// Info panel of one selected unit: the same sprite nine-sliced on the whole panel and on the
// portrait's cell (3318 of 3318 ring pixels equal), the hit point bar under the cell: sprite
// `hp_bar` (6 x 6) nine-sliced by 2 with the 1 x 1 sprite `bar_green` inside (486 of 486).
constexpr Rect InfoPanel = {5, 165, 166, 169};
constexpr Rect PortraitCell = {5, 165, CellW, CellH};
constexpr Rect HitPointBar = {5, 209, 54, 9};
constexpr Rect HitPointFill = {7, 211, 50, 5};
constexpr int HitPointSprite = 6;
constexpr int HitPointSlice = 2;

// Command card: 3 x 3 cells, pitch 56 x 47.
constexpr int CommandColumns = 3;
constexpr int CommandRows = 3;
constexpr int CommandX = 5;
constexpr int CommandY = 336;
constexpr int CommandPitchX = 56;
constexpr int CommandPitchY = 47;

/// The cell of command button `index` (0..8, row by row).
constexpr Rect commandCell(int index)
{
	return {CommandX + CommandPitchX * (index % CommandColumns),
	        CommandY + CommandPitchY * (index / CommandColumns), CellW, CellH};
}

/// The picture inside a cell.
constexpr Rect cellPicture(const Rect &cell)
{
	return {cell.x + CellPadding, cell.y + CellPadding, cell.w - 2 * CellPadding,
	        cell.h - 2 * CellPadding};
}

constexpr bool contains(const Rect &r, int bx, int by)
{
	return bx >= r.x && bx < r.x + r.w && by >= r.y && by < r.y + r.h;
}

/// The command button whose cell holds the logical pixel, or -1 (the 2 / 1 px between cells too).
constexpr int commandCellAt(int bx, int by)
{
	for (int i = 0; i < CommandColumns * CommandRows; ++i) {
		if (contains(commandCell(i), bx, by)) {
			return i;
		}
	}
	return -1;
}

// Hotkey letter of a command button: the text face, capital 8 (24 px at step 3, 16 at 2, 8 at 1:
// o21_54, o21_55, o21_50, o11_16), white with the dark shadow, no plate; rows cell + 8..15. The
// ink's middle is 10.8..12.3 right of the cell's left edge for the fourteen letters read
// (M S A R H B V F L T K ...; the ink's left edge runs 7..10): taken as centred on 11.5. A button
// that may not be used has its letter at half brightness with the cell (o11_84 "K"). Cancel
// (o21_55, cell 9) has none.
constexpr double HotkeyMidDX = 11.5;
constexpr double HotkeyMidDY = 12.0;
constexpr double hotkeyMidX(const Rect &cell)
{
	return cell.x + HotkeyMidDX;
}
constexpr double hotkeyMidY(const Rect &cell)
{
	return cell.y + HotkeyMidDY;
}
/// The letter shown for a button's key: the key's capital; nothing for no key and for a key
/// that is not a visible ASCII character (Escape).
inline std::string hotkeyLetter(int key)
{
	if (key <= ' ' || key > '~') {
		return {};
	}
	return std::string(1, key >= 'a' && key <= 'z' ? (char) (key - 'a' + 'A') : (char) key);
}

/// Width of the hit point bar's filled part and its colour: the rule of the Remastered panel
/// (green from 75 %, yellow from 50 %, red under it; the captures show a full green bar only).
/// @return the width in logical pixels, 0 for a dead or empty unit; colour 0 green, 1 yellow, 2 red.
constexpr int hitPointFill(int value, int max, int &colour)
{
	colour = 0;
	if (max <= 0 || value <= 0) {
		return 0;
	}
	const int f = value >= max ? 100 : (100 * value) / max;
	colour = f >= 75 ? 0 : f >= 50 ? 1 : 2;
	return (f * HitPointFill.w) / 100;
}

/// One piece of a nine-sliced sprite: a part of the sprite and the rect it fills.
struct SlicePiece {
	Rect src;   // in sprite pixels
	Rect dst;   // in the pixels of the target rect's space
};

/// Nine-slice a sprite of spriteW x spriteH with the border `slice` onto `dst`: the corners keep
/// their size, the edges and the middle are stretched. A piece without area is left out.
/// @return the number of pieces written (at most 9)
inline int nineSlice(int spriteW, int spriteH, int slice, const Rect &dst, SlicePiece out[9])
{
	const int sx[4] = {0, slice, spriteW - slice, spriteW};
	const int sy[4] = {0, slice, spriteH - slice, spriteH};
	const int dx[4] = {dst.x, dst.x + slice, dst.x + dst.w - slice, dst.x + dst.w};
	const int dy[4] = {dst.y, dst.y + slice, dst.y + dst.h - slice, dst.y + dst.h};
	int n = 0;
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) {
			const Rect s = {sx[c], sy[r], sx[c + 1] - sx[c], sy[r + 1] - sy[r]};
			const Rect d = {dx[c], dy[r], dx[c + 1] - dx[c], dy[r + 1] - dy[r]};
			if (s.w > 0 && s.h > 0 && d.w > 0 && d.h > 0) {
				out[n++] = {s, d};
			}
		}
	}
	return n;
}

// Text. The original draws the classic view's text with its modern face, white with a dark shadow
// (captures o11_13, o11_16, o11_55, o11_84 at step 1): capitals and digits 8 px high, the hit
// point figure 5. The text is the one part that is not repeated pixels: it is drawn smooth at the
// screen's resolution, a capital 8 x s screen px (16 at s 2 whatever the logical height). Here
// it is drawn by the HUD's own faces, scaled to that height; the glyphs are not the original's.
constexpr int TextCap = 8;
constexpr int TextCapSmall = 5;
// Resource numbers: left, 18 px right of the icon (a leading "5" / "2"; a leading "1" starts at
// 20), ink rows 4..11.
constexpr int ResourceTextDX = 18;
constexpr double ResourceTextMidY = 8.0;
// Menu / Pause captions: centred in the face, ink rows 8..15.
constexpr double CaptionMidY = 12.0;
// Tooltip of the command under the pointer (o11_55 "Move" at logical 1024 x 768: ink x 204..232,
// rows 712..719). Seen at that size only; taken as hanging on the map view's bottom-left.
constexpr int TooltipX = 204;
constexpr double TooltipMidYFromBottom = 52.0;
constexpr double tooltipMidY(int logicalH)
{
	return logicalH - TooltipMidYFromBottom;
}
// Info panel, as measured (o11_16, o11_84): name centred about x 114 (rows 186..193), "Level N"
// under it (201..208), the hit point figure centred on x 31 (rows 220..224), stat lines with the
// colon ending at x 99 and the value from x 105, first line rows 233..240, pitch 15.
constexpr double PanelNameMidY = 190.0;
constexpr double PanelLevelMidY = 205.0;
constexpr double PanelStatMidY = 237.0;
constexpr int PanelStatPitch = 15;
constexpr int PanelColonX = 100;
constexpr double PanelNameX = 115.5;

// The Remastered panel is laid out in the HUD's art space, 4.5 units a classic pixel, and its
// lines are the classic view's lines: name at capital middle 852.75 (189.5), "Level" 920.25
// (204.5), stat row n 1064.25 + 67.5 n (236.5 + 15 n), colon at x 450 (100), name centred on
// x 520 (115.6). So a place of that panel is a place of the classic one: units / 4.5, and half
// a pixel down for a row's middle (190 / 205 / 237 measured).
constexpr double PanelUnitsPerPixel = 4.5;
constexpr double panelX(double hudUnits)
{
	return hudUnits / PanelUnitsPerPixel;
}
constexpr double panelMidY(double hudUnits)
{
	return hudUnits / PanelUnitsPerPixel + 0.5;
}

/// The middle row of a text of the small face (the hit point figure): no half pixel. Measured
/// middle 222.83 at step 3, 223.0 at 2, 222.5 at 1 for 1003.5 units = 223.0.
constexpr double panelSmallMidY(double hudUnits)
{
	return hudUnits / PanelUnitsPerPixel;
}

// A stat line's value starts at one column whatever the label: a "0" / "4" has its ink from
// 104.67 at step 3, 104.5 at 2, 105 at 1, a "1" 2 further right (o21_54, o21_53, o21_50, o11_16,
// o11_84); the colon's ink is 98.33..99.67.
constexpr double PanelValueX = 104.67;
/// A stat line of the panel "Label~|:<gap>value" in two parts: "Label~|:" (drawn to the colon's
/// column) and the value without the gap codes ("~_") and blanks before it. False when the text
/// is not such a line or has no value.
inline bool splitStatLine(const std::string &text, std::string &label, std::string &value)
{
	const std::string::size_type tab = text.find("~|:");
	if (tab == std::string::npos) {
		return false;
	}
	std::string::size_type p = tab + 3;
	while (p < text.size()) {
		if (text[p] == ' ') {
			++p;
		} else if (text.compare(p, 2, "~_") == 0) {
			p += 2;
		} else {
			break;
		}
	}
	if (p >= text.size()) {
		return false;
	}
	label = text.substr(0, tab + 3);
	value = text.substr(p);
	return true;
}

/// Height of a capital on the screen for a text whose capital is `cap` logical pixels, at the
/// step s (hdatlas::classicStep): 8 px at s 1, 16 at s 2, 24 at s 3.
constexpr double capOnScreen(int cap, int step)
{
	return cap * step;
}

/// Of `count` faces with the capital heights `faceCap` (screen px, as loaded) the one nearest to
/// the wanted height, and the factor it is drawn at to have exactly that height. A face without
/// a height (<= 0) is passed over.
/// @return the face's index, -1 when there is none
inline int pickFace(double wantedCap, const double *faceCap, int count, double &scale)
{
	int best = -1;
	double bestDiff = 0.0;
	for (int i = 0; i < count; ++i) {
		if (faceCap[i] <= 0.0) {
			continue;
		}
		const double diff = faceCap[i] > wantedCap ? faceCap[i] - wantedCap : wantedCap - faceCap[i];
		if (best < 0 || diff < bestDiff) {
			best = i;
			bestDiff = diff;
		}
	}
	scale = best < 0 ? 1.0 : wantedCap / faceCap[best];
	return best;
}

enum class TextAlign {
	Left,     // the text starts at the anchor
	Centre,   // the text's middle is on the anchor
	Tab       // the part before the tab ("Label" of "Label: value") ends at the anchor
};

/// The x a text is drawn from. `width`: of the whole text; `tabWidth`: of the part before the
/// tab (Tab only). All in the same unit as the anchor.
constexpr double textLeft(TextAlign align, double anchor, double width, double tabWidth)
{
	return align == TextAlign::Centre ? anchor - width / 2.0
	       : align == TextAlign::Tab ? anchor - tabWidth : anchor;
}

} // namespace classichud

/// The classic HUD is drawn and the logical pixel of the screen point is in the cell of a command button:
/// its number (0..8), else -1. (src/ui/ui.cpp)
extern int ClassicCommandCellAt(int screenX, int screenY);
/// Same for the Pause button's face. (src/ui/ui.cpp)
extern bool ClassicPauseButtonContains(int screenX, int screenY);
/// What the command card shows in cell `index`: false when there is no button there.
/// `allowed`: the selected units may use it (a button they may not use is drawn at half
/// brightness and does nothing). `marked`: the button of the order the units carry out (the
/// highlighted cell). (src/ui/botpanel.cpp)
extern bool ClassicCommandButtonShown(int index, bool &allowed, bool &marked);

/// One text of the Remastered info panel: what it prints, where and with which face. The text
/// keeps its codes ("~|" tab, "~<" "~>").
struct ClassicPanelText {
	std::string text;
	int x = 0;                  // the panel's own place (screen px of the Remastered layout)
	int y = 0;
	bool centred = false;
	const CFont *font = nullptr;
};
/// The text a content of the info panel prints for the unit; false when it is not a text.
/// (src/ui/contenttype.cpp)
extern bool ClassicContentText(const CContentType &content, const CUnit &unit, CFont *defaultFont,
                               ClassicPanelText &out);
/// The texts the Remastered info panel shows for the unit: the same panels, conditions, strings
/// and numbers. (src/ui/mainscr.cpp)
extern void ClassicInfoPanelTexts(const CUnit &unit, std::vector<ClassicPanelText> &out);

//@}

#endif // !__HD_CLASSIC_HUD_H__
