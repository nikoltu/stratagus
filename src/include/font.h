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
/**@name include/font.h - The font headerfile. */
//
//      (c) Copyright 1998-2005 by Lutz Sammer and Jimmy Salmon
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

#ifndef __FONT_H__
#define __FONT_H__

//@{

/*----------------------------------------------------------------------------
--  Documentation
----------------------------------------------------------------------------*/

/**
**  @class CFont font.h
**
**  \#include "font.h"
**
**  Defines the fonts used in the Stratagus engine. We support
**  proportional multicolor fonts of 9 colors.
**  (Currently the fonts aren't packed)
**
**  CFont::CharWidth[]
**
**    The width of each font glyph in pixels. The index 0 is the
**    width of the SPACE (' ', 0x20).
**
**  CFont::G
**
**    Contains the graphics of the font, Only 9 colors are supported.
*/

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "color.h"

#include <SDL.h>
#include <array>
#include <guisan/font.hpp>
#include <string>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/
class CGraphic;
class CFontColor;

/**
**  FONT-CJK: the characters of a sheet beyond its code page. A sheet keeps its code page in the
**  slots 32..255 (ASCII, Latin-1 ...) and may carry more glyphs after them: slot 256 + i holds
**  the character on line i of the map file `<sheet stem>.map` (one decimal codepoint a line).
**  This maps codepoint -> sheet index (256 + i). Empty for every sheet without a map.
*/
using FontGlyphMap = std::unordered_map<int, int>;

/// FONT-CJK: the first sheet index the map file fills.
constexpr int FontGlyphMapFirstSlot = 256;

/// FONT-CJK: parse a map file's text (one decimal codepoint a line; line i -> slot 256 + i).
/// A line that is not a codepoint above 0x7F keeps its slot but maps nothing.
/// @return the number of characters mapped.
extern int ParseFontGlyphMap(std::string_view text, FontGlyphMap &out);

/// FONT-CJK: decode one UTF-8 character at `pos` and move `pos` past it. A broken or cut-off
/// sequence gives -1 and moves past its first byte only, so the text after it is still read.
extern int FontDecodeUTF8(std::string_view s, size_t &pos);

/// FONT-CJK: true for a character of the scripts written without spaces (kana, CJK ideographs,
/// CJK punctuation, full-width forms): a line may break before and after it.
extern bool FontIsCJKBreakChar(int codepoint);
/// FONT-CJK (kinsoku): a closing mark that must not begin a line (。、」） ? ! ...).
extern bool FontNoLineStart(int codepoint);
/// FONT-CJK (kinsoku): an opening mark that must not end a line (「（ ...).
extern bool FontNoLineEnd(int codepoint);

/**
**  FONT-CJK: where the first line of `s` ends when it is `maxWidth` px wide at most (0 = no
**  limit), measured with `width`. Breaks at the first newline, else at the last space or the
**  last break between CJK characters that fits. A first word that does not fit stands on its
**  own line.
**  @param next  out: where the next line starts (past the newline / space; at the CJK break)
**  @return the byte length of the line
*/
extern size_t FontLineBreak(std::string_view s, unsigned int maxWidth,
                            const std::function<int(std::string_view)> &width, size_t &next);

/// FONT-WIDTH: pixel width of `number` as drawn (thousands commas, '-'), each character's own
/// width from `charWidth` (index = character - 32) + `letterSpacing`; a character the table does
/// not have is measured as the space. Used by CFont::Width(int).
extern int FontNumberWidth(int number, const std::vector<char> &charWidth, int letterSpacing);

/// Font definition
class CFont : public gcn::Font
{
private:
	explicit CFont(std::string ident) : Ident(std::move(ident)) {}

public:
	~CFont() override = default;

	static CFont *New(const std::string &ident, std::shared_ptr<CGraphic> g);
	static CFont *Get(std::string_view ident);

	int Height() const;
	int Width(std::string_view text) const;
	int Width(const int number) const;
	bool IsLoaded() const;

	int getHeight() const override { return Height(); }
	int getWidth(const std::string &text) const override { return Width(text); }
	void drawString(gcn::Graphics *graphics, const std::string &text, int x, int y, bool enabled) override;
	int getStringIndexAt(const std::string &text, int x) const override;

	// Set to false to reverse color when drawing string
	void setIsNormal(bool value) { is_normal = value; }

	void Load();

	/// NG84: mark this font's sheet as authored for a logical screen height (3840x2160 HUD space).
	/// Load() then scales the glyph sheet by Video.Height / AuthoredHeight BEFORE the glyph widths
	/// are measured, so the text keeps its size relative to the scaled HUD on any screen. 0 = off,
	/// which is what the classic fixed-pixel fonts keep.
	void SetAuthoredHeight(int height) { AuthoredHeight = height; AuthoredWidth = 0; }

	/// NG84: the authored SIZE of the layout this sheet belongs to (the HUD's 3840x2160 space). The
	/// scale then matches what the layout itself uses -- ComputeUniformScale's aspect-preserving
	/// min(W/aw, H/ah) -- instead of height alone. On a screen whose fit is width-limited the two
	/// differ (here 0.7708 vs 0.8083) and text drawn at the height factor is ~5% too big for its box.
	void SetAuthoredSize(int width, int height) { AuthoredWidth = width; AuthoredHeight = height; }

	/// WORLD-ZOOM-RULE: a face of the in-game HUD. Its sheet is baked for the HUD's art space
	/// `height` units high (2160) and is scaled by the HUD's own step scale,
	/// CVideo::HudCanvas.Scale * 2160 / height -- not by the 16:9 fit above, which stays the rule of
	/// the menus and in-game windows (measured: c_k_2960x1848.png, digit "0" of the resource row
	/// 25 px high = 38.25 units).
	void SetAuthoredHud(int height) { AuthoredWidth = 0; AuthoredHeight = height; AuthoredHud = true; }

	/// NG84: declare the SPACE advance in the same authored units as the sheet. The engine's default
	/// is `G->Width / 2` -- half a whole cell -- which for a proportional HD face is roughly twice
	/// what the original client shows between words. Scaled by the same factor as the sheet.
	void SetSpaceAdvance(int authoredPx) { SpaceAdvance = authoredPx; }

	/// NG101: declare the line advance in the same authored units as the sheet. It belongs on the
	/// font rather than on a label: the original's leading is a property of the typeface
	/// (measured pitch / ink 1.90 on the menu face, 1.92 on the small face). Scaled by the same
	/// factor as the sheet; 0 keeps the cell height, which is what every classic fixed-pixel
	/// font uses.
	void SetLineHeight(int authoredPx) { LineHeightAuthored = authoredPx; }
	int LineHeight() const;

	/// HUD-TEXT-SHADOW: the drop shadow of the original's HUD text, `units` of the classic 640x480
	/// HUD down and right of every glyph (k px at the HUD step k), black, opaque. 0 = none.
	/// Data/skins/skins.json, skin `hud_widget_text`: "drop_text_color": [0, 0, 0],
	/// "drop_text_offset": [1, 1] (measured: c_k_2960x1848.png at k = 3, o41_o1_peon.png at k = 2).
	void SetHudShadow(int units) { ShadowUnits = units; }

	/// FONT-LT: the code page this face's sheet is laid out in (see FontCodePageHdSheet);
	/// 0 = the global FontCodePage (SetFontCodePage), which every face used before.
	void SetCodePage(int page) { CodePage = page; }
	/// The code page in force for this face.
	int GetCodePage() const;
	/// The shadow's offset in pixels on this screen; 0 when the face has none.
	int ShadowOffset() const;

	/// FONT-CJK: the characters drawn from the sheet slots after 255 (see FontGlyphMap). Read at
	/// Load() from `<sheet stem>.map` beside the sheet, when there is one.
	const FontGlyphMap &GetGlyphMap() const { return GlyphMap; }

	/// FONT-CJK: line `line` (1-based) of `text` broken at `maxWidth` px in this face, as
	/// GetLineFont does (spaces, newlines, and between CJK characters). For the Lua wrappers.
	std::string WrapLine(int line, const std::string &text, int maxWidth) const;

	std::shared_ptr<CGraphic> GetGraphic() const;

	template<bool CLIP>
	unsigned int DrawChar(CGraphic &g, int utf8, int x, int y, const CFontColor &fc) const;

	void DynamicLoad() const;

private:
	void MeasureWidths();

private:
	std::string Ident;    /// Ident of the font.
	std::vector<char> CharWidth; /// Real font width (starting with ' ')
	std::shared_ptr<CGraphic> G; /// Graphic object used to draw
	bool is_normal = true;
	int AuthoredHeight = 0;      /// NG84: logical screen height this sheet was baked for (0 = unscaled)
	int AuthoredWidth = 0;       /// NG84: authored layout width (0 = scale by height alone)
	bool AuthoredHud = false;    /// WORLD-ZOOM-RULE: scale with the HUD step instead of the screen fit
	int SpaceAdvance = 0;        /// NG84: declared SPACE advance in authored units (0 = engine default)
	int LineHeightAuthored = 0;  /// NG101: declared LINE advance in authored units (0 = use the cell)
	int ShadowUnits = 0;         /// HUD-TEXT-SHADOW: drop shadow offset in HUD units (0 = none)
	int CodePage = 0;            /// FONT-LT: code page of the sheet (0 = the global FontCodePage)
	int LetterSpacing = 1;       /// NG84: px added after each glyph. 1 = the classic bitmap-font rule;
	                             /// 0 once a real advance table is loaded, because a true advance
	                             /// already contains the glyph's right side bearing.

	FontGlyphMap GlyphMap;       /// FONT-CJK: codepoint -> sheet index for the slots after 255

private:
	void LoadAdvanceTable();
	void LoadGlyphMap();
	double AuthoredScale() const;
};

#define MaxFontColors 9

/// Font color definition
class CFontColor
{
public:
	explicit CFontColor(std::string ident);

	static CFontColor *New(const std::string &ident);
	static CFontColor *Get(std::string_view ident);

	std::string Ident;
	std::array<SDL_Color, MaxFontColors> Colors{};
};

/*----------------------------------------------------------------------------
--  Definitions
----------------------------------------------------------------------------*/

/**
**  FIXME: should be moved to lua
*/
#define FontRed "red"
#define FontGreen "green"
#define FontYellow "yellow"
#define FontWhite "white"
#define FontGrey "grey"

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

/**
**  Font selector for the font functions.
**  FIXME: should be moved to lua
*/
extern CFont &GetSmallFont();  /// Small font used in stats
extern CFont &GetGameFont();   /// Normal font used in game
extern bool IsGameFontReady(); /// true when GameFont is provided

extern int FontCodePage;

/**
**  FONT-LT: the layout of the sheets the launcher bakes from BLQ55.ttf (FontBaker, layout 3):
**  characters 32..255 as Latin-1, and in the slots 0x80..0x9D, which Latin-1 leaves to control
**  codes, the Lithuanian letters (A, C, E, E-dot, I, S, U-ogonek, U-macron, Z with their marks,
**  capital then small) and the typographic marks en dash, em dash, the four quotes and the
**  ellipsis, then (layout 3) the Polish L-stroke, capital then small, and the euro sign.
**  A face whose sheet has this layout is told so with CFont::SetCodePage.
*/
constexpr int FontCodePageHdSheet = 88591;

/**
**  The sheet indices `utf8` is drawn with in code page `page`: one index per character, or
**  the indices of its ASCII stand-in where the page has no slot for it (unknown: '?').
*/
extern std::vector<int> FontCodepageIndices(std::string_view utf8, int page,
                                            const FontGlyphMap *map = nullptr);

/// HUD-FOOD-RED: the colour of the glyphs of a baked HD face drawn from now on (white = as
/// baked). Those faces are white in palette indices no font colour reaches, so a coloured figure
/// cannot be had by a CFontColor; the caller sets the tint, draws, and sets white again.
extern void SetGlyphTint(unsigned char r, unsigned char g, unsigned char b);

/// Set the default text colors for normal and reverse text
extern void SetDefaultTextColors(const std::string &normal, const std::string &reverse);
/// Get the default text colors for normal and reverse text
extern std::pair<std::string, std::string> GetDefaultTextColors();
///  Return the 'line' line of the string 's'.
extern std::string_view GetLineFont(unsigned int line, std::string_view s, unsigned int maxlen, const CFont *font);

/// Get the hot key from a string
extern int GetHotKey(const std::string &text);

/// Load and initialize the fonts
extern void LoadFonts();

/// Cleanup the font module
extern void CleanFonts();

class CLabel
{
public:
	CLabel(const CFont &f, std::string_view nc, std::string_view rc): font(&f)
	{
		normal = CFontColor::Get(nc);
		reverse = CFontColor::Get(rc);
	}
	explicit CLabel(const CFont &f);

	int Height() const { return font->Height(); }

	void SetFont(const CFont &f) { font = &f; }

	void SetNormalColor(std::string_view nc) { normal = CFontColor::Get(nc); }

	/// Draw text/number unclipped
	int Draw(int x, int y, std::string_view text) const;
	int Draw(int x, int y, int number) const;
	/// Draw text/number clipped
	int DrawClip(int x, int y, std::string_view text, bool is_normal = true) const;
	int DrawClip(int x, int y, int number) const;
	/// Draw reverse text/number unclipped
	int DrawReverse(int x, int y, std::string_view text) const;
	int DrawReverse(int x, int y, int number) const ;
	/// Draw reverse text/number clipped
	int DrawReverseClip(int x, int y, std::string_view text) const;
	int DrawReverseClip(int x, int y, int number) const;

	int DrawCentered(int x, int y, std::string_view text) const;
	int DrawReverseCentered(int x, int y, std::string_view text) const;
private:
	template <const bool CLIP>
	int DoDrawText(int x, int y, std::string_view text, const CFontColor *fc) const;
private:
	const CFontColor *normal;
	const CFontColor *reverse;
	const CFont *font;
};

//@}

#endif // !__FONT_H__
