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
#include <string_view>
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
/// (green from 75 %, yellow from 50 %, red under it). Seen: a full green bar (o11_16) and
/// 108 / 400 = 13 px of `bar_red` (o21_52); the two limits are not in a capture. The part that is
/// not filled is the sprite's own middle (52, 52, 52), not black.
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

// A building under construction (o21_52): the sprite `complete_bar` (158 x 20, its own size) in
// the panel's lower part, the 1 x 1 sprite `bar_green` stretched over the done part of its
// inside (skin `classic_hud_complete_bar`: "padding" 3), the words "% Complete" on the inside's
// middle (ink rows 315.5..325.5 with the descender). No level row and no stat line.
constexpr Rect CompleteBar = {9, 310, 158, 20};
constexpr Rect CompleteFill = {12, 313, 152, 14};
constexpr double CompleteTextMidX = 88.0;
constexpr double CompleteTextMidY = 320.0;
/// Width of the done part: the progress over the inside's width, rounded down (o21_52: 40 px;
/// a whole per cent first would give 39 or 41).
constexpr int completeFill(int value, int max)
{
	if (max <= 0 || value <= 0) {
		return 0;
	}
	return value >= max ? CompleteFill.w : (int) ((long long) value * CompleteFill.w / max);
}

// A unit that trains, researches or upgrades (o30_C01_*, o30_C02_*, o30_C03_*; one layout): the
// label "Training:" / "Upgrading:" on stat row 1 at the colon's column, the picture of what is
// made in a cell right of it, the bar of a building site under them. No cells of a queue.
constexpr Rect TrainingCell = {106, 238, CellW, CellH};
constexpr double TrainingLabelMidY = 252.0;

// A caster (o30_C14_mage_selected): stat row 5 is "Magic:" with a bar in the value's place: the
// sprite `magic_bar` (6 x 6, nine-sliced by 2) on 104, 306, 63 x 14, inside it the 1 x 1 sprite
// `bar_magic` over the part of 59 x 10 that the mana fills (29 px at 127 of 255), the number
// centred on the bar.
constexpr int MagicRow = 5;
constexpr int MagicSprite = 6;
constexpr int MagicSlice = 2;
constexpr double MagicTextMidX = 135.5;
constexpr Rect magicBar(int row)
{
	return {104, 231 + 15 * row, 63, 14};
}
constexpr Rect magicFillArea(const Rect &bar)
{
	return {bar.x + MagicSlice, bar.y + MagicSlice, bar.w - 2 * MagicSlice, bar.h - 2 * MagicSlice};
}
constexpr int magicFill(int value, int max)
{
	const int w = magicFillArea(magicBar(MagicRow)).w;
	if (max <= 0 || value <= 0) {
		return 0;
	}
	return value >= max ? w : (int) ((long long) value * w / max);
}

// Several units selected (o30_C06_*): the panel's frame stays; up to 9 units in cells of the
// portrait's size, pitch 56 x 55, row by row; 10 to 12 in cells 40 wide, pitch 42, four a row,
// whose picture is the columns 7..38 of the 46. Under every cell its hit point bar as under the
// single portrait. No text. Passengers of a transport (o30_C13_*) sit in the rows 1 and 2 of the
// wide cells, the ship's own portrait in cell 0.
constexpr int GroupPitchX = 56;
constexpr int GroupPitchY = 55;
constexpr int GroupWideColumns = 3;
constexpr int GroupWideMax = 9;
constexpr int GroupSmallW = 40;
constexpr int GroupSmallPitchX = 42;
constexpr int GroupSmallColumns = 4;
constexpr int GroupSmallMax = 12;
constexpr int GroupSmallPictureW = 32;
constexpr int GroupSmallPictureSrcX = 7;
constexpr int PassengerMax = 6;
constexpr int groupCellCount(bool wide)
{
	return wide ? GroupWideMax : GroupSmallMax;
}
/// Cell `index` of a group, row by row. `wide`: the cells of up to 9 units.
constexpr Rect groupCell(int index, bool wide)
{
	return wide ? Rect{InfoPanel.x + GroupPitchX * (index % GroupWideColumns),
	                   InfoPanel.y + GroupPitchY * (index / GroupWideColumns), CellW, CellH}
	            : Rect{InfoPanel.x + GroupSmallPitchX * (index % GroupSmallColumns),
	                   InfoPanel.y + GroupPitchY * (index / GroupSmallColumns), GroupSmallW, CellH};
}
/// The hit point bar under a cell and the part of it that is filled at full health.
constexpr Rect groupBar(const Rect &cell)
{
	return {cell.x, cell.y + cell.h - HitPointSlice, cell.w, HitPointBar.h};
}
constexpr Rect groupFill(const Rect &cell)
{
	return {cell.x + HitPointSlice, cell.y + cell.h, cell.w - 2 * HitPointSlice, HitPointFill.h};
}
/// Width of the filled part of a bar `width` wide and its colour (the limits of hitPointFill;
/// the same width as hitPointFill for the 50 of the single bar).
constexpr int groupHitPointFill(int value, int max, int width, int &colour)
{
	if (hitPointFill(value, max, colour) <= 0 && (max <= 0 || value <= 0)) {
		return 0;
	}
	return value >= max ? width : (int) ((long long) value * width / max);
}
/// The cell of a group that holds the logical pixel, or -1 (between cells and on the bars too).
constexpr int groupCellAt(int bx, int by, bool wide)
{
	for (int i = 0; i < groupCellCount(wide); ++i) {
		if (contains(groupCell(i, wide), bx, by)) {
			return i;
		}
	}
	return -1;
}
/// The cell of passenger n (0..5) of a transport.
constexpr Rect passengerCell(int n)
{
	return groupCell(GroupWideColumns + n, true);
}
/// The passenger whose cell holds the logical pixel, or -1.
constexpr int passengerCellAt(int bx, int by)
{
	const int cell = groupCellAt(bx, by, true);
	return cell >= GroupWideColumns ? cell - GroupWideColumns : -1;
}

// The message line (o30_C07_*): one line, yellow, no plate, first ink x 183, capitals
// logicalH - 50 .. - 42; a new message takes the line of the one before. The objectives of a
// mission (o30_C08_*, h40_h6_start): white, first ink x 185, capitals from row 28, 16 px a line.
constexpr int MessageX = 183;
constexpr double messageMidY(int logicalH)
{
	return logicalH - 46.0;
}
constexpr int ObjectivesX = 185;
constexpr int ObjectivesPitch = 16;
constexpr double objectiveMidY(int n)
{
	return 32.0 + ObjectivesPitch * n;
}

/// The sprite of a button's face by its state: `<sprite>_active` while the button is held down
/// (`hud_<race>_small`, `portrait_button`, `portrait_button_highlight`, `f5` have one each).
inline std::string faceName(const std::string &sprite, bool held)
{
	return held ? sprite + "_active" : sprite;
}

/// The grey a pixel has behind an in-game window: 0.2126 R + 0.7152 G + 0.0722 B, not darkened
/// (the classic frame of the orc side, o21_14 against o21_40 at the same pixels: 52, 32, 0 -> 34;
/// 80, 48, 8 -> 52; 60, 36, 4 -> 39; 72, 44, 4 -> 47; the Remastered view by the same rule).
constexpr int greyOf(int r, int g, int b)
{
	return (int) ((r * 13933u + g * 46871u + b * 4732u + 32768u) >> 16);
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
// The box under the hint (o11_55, logical 1024 x 768): black with a line of one pixel
// (67, 66, 60), a rectangle with its four corners cut out square: columns 195..411 in the rows
// 692..739, columns 188..419 in the rows 699..732. Seen with the one-word hint "Move" at that
// size only; taken as hanging on the map view's bottom-left like the hint, its width fixed.
constexpr int TooltipBoxX = 188;
constexpr int TooltipBoxW = 232;
constexpr int TooltipBoxH = 48;
constexpr int TooltipBoxTopFromBottom = 76;
constexpr int TooltipBoxCutLeft = 7;
constexpr int TooltipBoxCutRight = 8;
constexpr int TooltipBoxCutY = 7;
constexpr int TooltipBoxLineRGB[3] = {67, 66, 60};
constexpr int TooltipBoxFills = 2;
constexpr int TooltipBoxLines = 12;
struct TooltipBox {
	Rect fill[TooltipBoxFills];   // black
	Rect line[TooltipBoxLines];   // the line, clockwise from the top
};
// More lines (o30_C05_hover_*, logical 1024 x 768; the place at 640 x 480 and 720 x 540 from
// o30_C09_*): the box keeps its columns and its bottom row (logicalH - 29) and grows upward, 15 px
// a line. Top row 692 with one line, 662 with three, 646 with four (both captures of four lines:
// one pixel more than the pitch gives). The lines stand from the top: the first capital 20 under
// the top row (712; 682 / 697 / 712; 666 / 681 / 696 / 711), the yellow requirement line one
// pixel lower than the pitch (712 in o30_C05_hover_dim_keep).
constexpr int TooltipLinePitch = 15;
constexpr int TooltipLineMax = 8;
constexpr double TooltipFirstMidFromTop = 24.0;
constexpr int tooltipBoxHeight(int lines)
{
	return TooltipBoxH + TooltipLinePitch * ((lines < 1 ? 1 : lines) - 1) + (lines >= 4 ? 1 : 0);
}
/// The middle row of the capitals of line n (0 the title) of a box of `lines` lines.
/// `requirementLine`: n is the line that names what the command still needs.
constexpr double tooltipLineMidY(int logicalH, int lines, int n, bool requirementLine)
{
	return logicalH - TooltipBoxTopFromBottom + TooltipBoxH - tooltipBoxHeight(lines)
	       + TooltipFirstMidFromTop + TooltipLinePitch * n + (requirementLine ? 1 : 0);
}
// The cost row is the second line: the 14 x 14 sprites `icons_0` gold, `icons_1` lumber,
// `icons_2` oil at x 204 / 270 / 336, rows capital top - 3 .. + 10, the number's ink 18 px right
// of its icon; only the resources with a cost, packed from the left (Farm 500, 250; Keep 2000,
// 1000, 200).
constexpr int TooltipCostIconX = 204;
constexpr int TooltipCostPitch = 66;
constexpr int TooltipCostTextDX = 18;
constexpr int TooltipCostIconDY = -3;
constexpr int TooltipCostMax = 3;
/// The icon of cost k (0..2, as packed) of the cost row whose capitals' middle row is `midY`.
constexpr Rect tooltipCostIcon(int k, double midY)
{
	return {TooltipCostIconX + TooltipCostPitch * k, (int) midY - TextCap / 2 + TooltipCostIconDY,
	        ResourceIconSize, ResourceIconSize};
}
// The requirement line and the message line are yellow (255, 204, 0). The box is black at one
// third over the map: what is under it keeps two thirds (120, 56, 4 -> 80, 38, 3; 16, 32, 44 ->
// 11, 21, 30), alpha 85 of 255. The description wraps inside the columns 204..403 (seen: two
// lines, ink 205..403; the width itself is not measured).
constexpr int TooltipYellowRGB[3] = {255, 204, 0};
constexpr int TooltipBoxAlpha = 85;
constexpr int TooltipTextW = 200;
/// The box's parts in a logical screen `logicalH` high, for `lines` lines of text.
constexpr TooltipBox tooltipBox(int logicalH, int lines = 1)
{
	const int x0 = TooltipBoxX;
	const int x1 = x0 + TooltipBoxCutLeft;
	const int x2 = x0 + TooltipBoxW - 1 - TooltipBoxCutRight;
	const int x3 = x0 + TooltipBoxW - 1;
	const int y3 = logicalH - TooltipBoxTopFromBottom + TooltipBoxH - 1;
	const int y0 = y3 - tooltipBoxHeight(lines) + 1;
	const int y1 = y0 + TooltipBoxCutY;
	const int y2 = y3 - TooltipBoxCutY;
	return {{{x1, y0, x2 - x1 + 1, y3 - y0 + 1}, {x0, y1, x3 - x0 + 1, y2 - y1 + 1}},
	        {{x1, y0, x2 - x1 + 1, 1}, {x2, y0, 1, y1 - y0 + 1}, {x2, y1, x3 - x2 + 1, 1},
	         {x3, y1, 1, y2 - y1 + 1}, {x2, y2, x3 - x2 + 1, 1}, {x2, y2, 1, y3 - y2 + 1},
	         {x1, y3, x2 - x1 + 1, 1}, {x1, y2, 1, y3 - y2 + 1}, {x0, y2, x1 - x0 + 1, 1},
	         {x0, y1, 1, y2 - y1 + 1}, {x0, y1, x1 - x0 + 1, 1}, {x1, y0, 1, y1 - y0 + 1}}};
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

/// The classic HUD is drawn and the logical pixel of the screen point is in the cell of a unit of
/// the selected group that holds a unit: the cell's number (GetSelectionCells), else -1.
/// (src/ui/ui.cpp)
extern int ClassicGroupCellAt(int screenX, int screenY);
/// Same for the cell of a passenger of the selected transport: the passenger's number. (src/ui/ui.cpp)
extern int ClassicPassengerCellAt(int screenX, int screenY);
/// The message line of the classic view. (src/ui/ui.cpp)
extern void DrawClassicMessage(std::string_view text);
/// Line n (0 the first) of the objectives block of the classic view. (src/ui/ui.cpp)
extern void DrawClassicObjective(int n, std::string_view text);

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
