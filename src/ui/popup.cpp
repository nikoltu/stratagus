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
/**@name popup.cpp - The popup globals. */
//
//      (c) Copyright 2012-2015 by cybermind and Joris Dauphin
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

#include "ui/popup.h"

#include "depend.h"
#include "font.h"
#include "player.h"
#include "spells.h"
#include "trigger.h"
#include "ui.h"
#include "unittype.h"
#include "upgrade.h"
#include "wc2strings.h"
#include "video.h"

/**
**  IG-T1: the Remastered's own tooltip text for whatever this button targets.
**
**  The original keys its tooltips off a numeric id (`unit_N_tooltip_advice`, `upgrade_N_tooltip`,
**  `spell_N_tooltip`). The id is the one the scripts registered for the target's ident; the
**  table repeats a display name for the human / orc pair and for the levels of an upgrade, so
**  only the ident selects the target's own record. A target without a registered id is looked up
**  by the display name it carries (TX1). -1 when the button targets nothing with a Remastered
**  counterpart; `family` is then left as it was set by the action.
*/
static int RemasteredButtonId(const ButtonAction &button, std::string &family)
{
	std::string ident;
	std::string name;
	switch (button.Action) {
		case ButtonCmd::Build:
		case ButtonCmd::Train:
		case ButtonCmd::UpgradeTo:
			family = "unit";
			if (button.Value >= 0 && button.Value < (int) getUnitTypes().size()) {
				ident = getUnitTypes()[button.Value]->Ident;
				name = getUnitTypes()[button.Value]->Name;
			}
			break;
		case ButtonCmd::Research:
			family = "upgrade";
			if (button.Value >= 0 && button.Value < (int) AllUpgrades.size() && AllUpgrades[button.Value]) {
				ident = AllUpgrades[button.Value]->Ident;
				name = AllUpgrades[button.Value]->Name;
			}
			break;
		case ButtonCmd::SpellCast:
			family = "spell";
			if (button.Value >= 0 && button.Value < (int) SpellTypeTable.size() && SpellTypeTable[button.Value]) {
				ident = SpellTypeTable[button.Value]->Ident;
				name = SpellTypeTable[button.Value]->Name;
			}
			break;
		default:
			return -1;
	}
	// A target without a name has nothing to look up by (the upgrades carry none).
	return name.empty() ? GetWC2IdForIdent(family, ident) : ResolveWC2Id(family, ident, name);
}

/// The Remastered's text `<family>_<id><suffix>` for whatever this button targets; empty when
/// there is none, in which case the caller keeps Wargus's own text.
static std::string RemasteredButtonText(const ButtonAction &button, const char *suffix)
{
	std::string family;
	const int id = RemasteredButtonId(button, family);
	if (id < 0) {
		return std::string();
	}
	return GetWC2String(family + "_" + std::to_string(id) + suffix);
}

/**
**  IG-T1: the plain COMMAND buttons have their own names in the original, in the classic
**  `stat_txt_*` block -- "Move", "Stop", "Attack", "Repair", "Patrol", "Stand Ground",
**  "Build Basic Structure", "Build Advanced Structure", "Cancel". Wargus spells the same commands
**  in capitals with a hotkey marker ("~!MOVE"); the original's tooltip shows the proper-case
**  name, and puts the hotkey letter in the icon corner instead.
*/
static std::string RemasteredCommandName(const ButtonAction &button)
{
	int id = 0;
	switch (button.Action) {
		case ButtonCmd::Move: id = 392; break;
		case ButtonCmd::Stop: id = 393; break;
		case ButtonCmd::Attack: id = 394; break;
		case ButtonCmd::Repair: id = 395; break;
		// HUD-020: 396 is the worker's harvest command (capture o46_o1_peon_hover_harvest).
		// 397 is the tanker's own harvest command (run 20, rows 83 / 199).
		case ButtonCmd::Harvest:
			id = button.UnitMask.find("oil-tanker") != std::string::npos ? 397 : 396;
			break;
		// HUD-020: 398 / 399 name the two build PAGES, not a building (capture o09_h_hover_build
		// shows 398 on the page button; o11_h_build_hover1 shows unit_58_build on the Farm button).
		// A Build button therefore falls through to unit_<id>_build in GetDraw().
		case ButtonCmd::Button:
			if (button.Value == 1) {
				id = 398;
			} else if (button.Value == 2) {
				id = 399;
			} else if (button.Value == 0) {
				id = 407; // the build pages' way back
			} else {
				return std::string();
			}
			break;
		// Run 20 (work/agent-orig-20/ORIG20.md): each of these has a one-line tooltip of its own.
		case ButtonCmd::Return: id = 401; break;
		case ButtonCmd::AttackGround: id = 402; break;
		case ButtonCmd::StandGround: id = 403; break;
		case ButtonCmd::Patrol: id = 404; break;
		case ButtonCmd::Unload: id = 405; break;
		case ButtonCmd::SpellCast:
			if (button.ValueStr != "spell-suicide-bomber") {
				return std::string();
			}
			id = 406;
			break;
		case ButtonCmd::Cancel: id = 407; break;
		case ButtonCmd::CancelBuild: id = 408; break;
		case ButtonCmd::CancelTrain: id = 409; break;
		case ButtonCmd::CancelUpgrade: id = 410; break;
		default: return std::string();
	}
	return GetWC2String("stat_txt_" + std::to_string(id));
}

static std::string GetDraw(EPopupButtonInfo infoType, const ButtonAction &button)
{
	switch (infoType) {
		default:
		case EPopupButtonInfo::Hint: {
			// IG-T1: prefer the original's own wording for the command and for anything it names.
			std::string s = RemasteredCommandName(button);
			if (s.empty()) {
				s = RemasteredButtonText(button, "_build");   // "Train Grunt", "Build Farm", ...
			}
			if (s.empty()) {
				s = RemasteredButtonText(button, "");         // the plain name
			}
			return s.empty() ? button.Hint : s;
		}
		case EPopupButtonInfo::Description: {
			// IG-T1: prefer the original's advice, then its plain tooltip, then Wargus's own text.
			std::string s = RemasteredButtonText(button, "_tooltip_advice");
			if (s.empty()) {
				s = RemasteredButtonText(button, "_tooltip");
			}
			if (!s.empty()) {
				const std::string req = RemasteredButtonText(button, "_requirement");
				if (!req.empty()) {
					s += "\n" + req;
				}
				return s;
			}
			return button.Description;
		}
		case EPopupButtonInfo::Dependencies: return PrintDependencies(*ThisPlayer, button);
	}
}

/*----------------------------------------------------------------------------
--  HUD-020: the original's tooltip panel
----------------------------------------------------------------------------*/

namespace
{
// Oracle table `tooltip_layout_contract` (EXE builder 0x516AF0, HUD call 0x518C48), in the
// original's logical UI units: 480 rows, so one unit is 4.5 units of the 2160-row HUD space.
// Each value was also read off the captures o08 / o11 / o26 / o48.
constexpr double TipUnit = 2160.0 / 480.0;
constexpr int TipX = 184;          // fixed_x
constexpr int TipInset = 20;       // horizontal_content_inset, vertical_content_inset
constexpr int TipCompactW = 200;   // compact_content_width
constexpr int TipExpandedW = 400;  // expanded_content_width
constexpr int TipLineH = 16;       // title_height, line_height
constexpr int TipCostH = 14;       // cost_row_height
constexpr int TipBottom = 24;      // bottom_anchor_offset
constexpr int TipBodyMax = 3;      // compact_to_expanded_rule: body lines that still fit compact
constexpr int TipBorder = 70 / 5;  // skins.json `hud_tooltip`: "9slice" 70 at "scale" 5
// Captures o11_h_build_hover1 / o48_o1_build_hover3: the cost columns are one third of the
// content width apart (133 px and 266 px at 2 px per unit).
constexpr int TipCostColumns = 3;
// Captures o11 / o26 / o48: the amount starts 34 px (2 px per unit) right of its icon's left edge.
// UNVERIFIED to +-1 unit: read from ink, not from the builder.
constexpr int TipAmountX = 17;
// Where the capitals of the tooltip's face sit in its glyph cell: the baked faces stand every
// glyph on one baseline placed so that a capital is centred in the cell's height (FontBaker), so
// the middle of the capitals is half the cell below the text position, whatever the face and its
// scale. The original centres the capitals in a row: capital top = row top + 4 units of 16,
// capital height 8 to 8.5 units (captures o08, o26).
int TipCapMid(const CFont &font)
{
	return font.Height() / 2;
}

int TipLineCount(const std::string &text, int width, const CFont &font)
{
	int n = 0;
	while (!GetLineFont(n + 1, text, width, &font).empty()) {
		++n;
	}
	return n;
}

// Body text. Oracle `tooltip_family_contracts`: unit_%d_tooltip_advice, upgrade_%d_tooltip,
// spell_%d_tooltip; a `stat` command has a title only. Without the game's string file the button
// keeps its own Description.
std::string TipBody(const ButtonAction &button)
{
	if (GetWC2StringCount() == 0) {
		return button.Description;
	}
	switch (button.Action) {
		case ButtonCmd::Research:
		case ButtonCmd::SpellCast:
			return RemasteredButtonText(button, "_tooltip");
		default:
			return RemasteredButtonText(button, "_tooltip_advice");
	}
}
} // namespace

int GetRemasteredTooltipTexts(const ButtonAction &button, bool allowed, std::string &title,
							  std::string &body, std::string &requirement)
{
	title = GetDraw(EPopupButtonInfo::Hint, button);
	body = TipBody(button);
	// The requirement line is absent on an available command (capture o11_h_build_hover1: the Farm
	// has unit_58_requirement and shows no such line). Shown on a greyed command: STRONGLY_SUPPORTED
	// by the builder's content order only -- no capture of a greyed button yet.
	requirement = allowed ? std::string() : RemasteredButtonText(button, "_requirement");
	std::string family;
	return RemasteredButtonId(button, family);
}

void DrawRemasteredTooltip(const CPopup &popup, const ButtonAction &button, const int *costs,
						   bool allowed)
{
	const CFont &font = popup.DefaultFont ? *popup.DefaultFont : GetSmallFont();
	const double scale = Video.HudCanvas.Scale * TipUnit;
	const auto px = [scale](int units) { return (int) (units * scale + 0.5); };

	std::string title;
	std::string body;
	std::string requirement;
	GetRemasteredTooltipTexts(button, allowed, title, body, requirement);

	// Cost row: gold, lumber, oil (Oracle: "three costs"); mana for a spell.
	int costIndex[TipCostColumns];
	int costCount = 0;
	if (button.Action == ButtonCmd::SpellCast) {
		if (costs[ManaResCost] > 0) {
			costIndex[costCount++] = ManaResCost;
		}
	} else {
		for (const int i : {GoldCost, WoodCost, OilCost}) {
			if (costs[i] > 0 && costCount < TipCostColumns) {
				costIndex[costCount++] = i;
			}
		}
	}

	int contentW = px(TipCompactW);
	if (TipLineCount(title, contentW, font) > 1 || TipLineCount(body, contentW, font) > TipBodyMax) {
		contentW = px(TipExpandedW);
	}
	const int bodyLines = TipLineCount(body, contentW, font);
	const int reqLines = TipLineCount(requirement, contentW, font);

	const int inset = px(TipInset);
	const int lineH = px(TipLineH);
	const int costH = costCount ? px(TipCostH) : 0;
	const int w = contentW + 2 * inset;
	const int h = 2 * inset + lineH + costH + lineH * (bodyLines + reqLines);
	const int x = Video.HudCanvas.X + px(TipX);
	const int y = Video.HudCanvas.Y + Video.HudCanvas.H - px(TipBottom) - h;

	if (popup.BackgroundImage) {
		const SDL_Rect dst = {x, y, w, h};
		popup.BackgroundImage->DrawNineSliceScaled(dst, popup.BackgroundBorder, px(TipBorder));
	} else {
		Video.FillTransRectangle(popup.BackgroundColor, x, y, w, h, popup.BackgroundColor >> ASHIFT);
		Video.DrawRectangle(popup.BorderColor, x, y, w, h);
	}

	// skins.json `hud_widget_text`: text 255,255,255.
	const CLabel label(font, "white", "white");
	const int capMid = TipCapMid(font);
	const int textDy = lineH / 2 - capMid;
	const int cx = x + inset;
	int cy = y + inset;

	label.Draw(cx, cy + textDy, GetLineFont(1, title, contentW, &font));
	cy += lineH;

	for (int k = 0; k < costCount; ++k) {
		const int i = costIndex[k];
		const int colX = cx + k * contentW / TipCostColumns;
		if (const CGraphic *g = UI.Resources[i].G.get()) {
			g->DrawFrameClip(UI.Resources[i].IconFrame, colX, cy + (costH - g->Height) / 2);
		}
		label.Draw(colX + px(TipAmountX), cy + costH / 2 - capMid, costs[i]);
	}
	cy += costH;

	for (int n = 1; n <= bodyLines; ++n, cy += lineH) {
		label.Draw(cx, cy + textDy, GetLineFont(n, body, contentW, &font));
	}
	for (int n = 1; n <= reqLines; ++n, cy += lineH) {
		label.Draw(cx, cy + textDy, GetLineFont(n, requirement, contentW, &font));
	}
}

int CPopupContentTypeButtonInfo::GetWidth(const ButtonAction &button, int *) const /* override */
{
	const CFont &font = this->Font ? *this->Font : GetSmallFont();
	const std::string draw = GetDraw(this->InfoType, button);

	int width = 0;
	std::string_view sub;
	if (draw.length()) {
		if (this->MaxWidth) {
			return std::min((unsigned int)font.getWidth(draw), this->MaxWidth);
		}
		int i = 1;
		while (!(sub = GetLineFont(i++, draw, 0, &font)).empty()) {
			width = std::max(width, font.Width(sub));
		}
	}
	return width;
}

int CPopupContentTypeButtonInfo::GetHeight(const ButtonAction &button, int *) const /* override */
{
	const CFont &font = this->Font ? *this->Font : GetSmallFont();
	const std::string draw = GetDraw(this->InfoType, button);

	int height = 0;
	if (draw.length()) {
		int i = 1;
		while ((GetLineFont(i++, draw, this->MaxWidth, &font)).length()) {
			height += font.Height() + 2;
		}
	}
	return height;
}

void CPopupContentTypeButtonInfo::Draw(int x,
                                       int y,
                                       const CPopup &popup,
                                       const unsigned int popupWidth,
                                       const ButtonAction &button,
                                       int *) const /* override */
{
	const CFont &font = this->Font ? *this->Font : GetSmallFont();
	CLabel label(font, this->TextColor, this->HighlightColor);
	const std::string draw = GetDraw(this->InfoType, button);
	if (draw.length()) {
		int i = 0;
		int y_off = y;
		unsigned int width = this->MaxWidth
							 ? std::min(this->MaxWidth, popupWidth - 2 * popup.MarginX)
							 : 0;
		std::string_view sub;
		while ((sub = GetLineFont(++i, draw, width, &font)).length()) {
			label.Draw(x, y_off, sub);
			y_off += font.Height() + 2;
		}
	}
}

static EPopupButtonInfo toEPopupButtonInfo(std::string_view s)
{
	if (s == "Hint") {
		return EPopupButtonInfo::Hint;
	} else if (s == "Description") {
		return EPopupButtonInfo::Description;
	} else if (s == "Dependencies") {
		return EPopupButtonInfo::Dependencies;
	} else {
		ErrorPrint("Invalid infotype '%s'\n", s.data());
		ExitFatal(-1);
	}
}

void CPopupContentTypeButtonInfo::Parse(lua_State *l) /* override */
{
	Assert(lua_istable(l, -1));

	for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
		const std::string_view key = LuaToString(l, -2);
		if (key == "InfoType") {
			this->InfoType = toEPopupButtonInfo(LuaToString(l, -1));
		} else if (key == "MaxWidth") {
			this->MaxWidth = LuaToNumber(l, -1);
		} else if (key == "Font") {
			this->Font = CFont::Get(LuaToString(l, -1));
		} else {
			LuaError(l, "'%s' invalid for method 'Name' in DefinePopups", key.data());
		}
	}
}

int CPopupContentTypeText::GetWidth(const ButtonAction &button, int *) const /* override */
{
	const CFont &font = this->Font ? *this->Font : GetSmallFont();

	if (this->MaxWidth) {
		return std::min((unsigned int)font.getWidth(this->Text), this->MaxWidth);
	}
	int width = 0;
	std::string_view sub;
	int i = 1;
	while (!(sub = GetLineFont(i++, this->Text, 0, &font)).empty()) {
		width = std::max(width, font.Width(sub));
	}
	return width;
}

int CPopupContentTypeText::GetHeight(const ButtonAction &button, int *) const /* override */
{
	CFont &font = this->Font ? *this->Font : GetSmallFont();
	int height = 0;
	int i = 1;
	while ((GetLineFont(i++, this->Text, this->MaxWidth, &font)).length()) {
		height += font.Height() + 2;
	}
	return height;
}

void CPopupContentTypeText::Draw(int x,
                                 int y,
                                 const CPopup &popup,
                                 const unsigned int popupWidth,
                                 const ButtonAction &button,
                                 int *) const /* override */
{
	const CFont &font = this->Font ? *this->Font : GetSmallFont();
	CLabel label(font, this->TextColor, this->HighlightColor);
	std::string_view sub;
	int i = 0;
	int y_off = y;
	unsigned int width = this->MaxWidth
						 ? std::min(this->MaxWidth, popupWidth - 2 * popup.MarginX)
						 : 0;
	while ((sub = GetLineFont(++i, this->Text, width, &font)).length()) {
		label.Draw(x, y_off, sub);
		y_off += font.Height() + 2;
	}
}

void CPopupContentTypeText::Parse(lua_State *l) /* override */
{
	Assert(lua_istable(l, -1));

	for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
		const std::string_view key = LuaToString(l, -2);
		if (key == "Text") {
			this->Text = LuaToString(l, -1);
		} else if (key == "MaxWidth") {
			this->MaxWidth = LuaToNumber(l, -1);
		} else if (key == "Font") {
			this->Font = CFont::Get(LuaToString(l, -1));
		} else {
			LuaError(l, "'%s' invalid for method 'Text' in DefinePopups", key.data());
		}
	}
}

int CPopupContentTypeCosts::GetWidth(const ButtonAction &button, int *Costs) const /* override */
{
	int popupWidth = 0;
	const CFont &font = this->Font ? *this->Font : GetSmallFont();

	for (unsigned int i = 1; i <= MaxCosts; ++i) {
		if (Costs[i]) {
			if (UI.Resources[i].IconWidth != -1)	{
				popupWidth += (UI.Resources[i].IconWidth + 5);
			} else {
				const CGraphic *G = UI.Resources[i].G.get();
				if (G) {
					popupWidth += (G->Width + 5);
				}
			}
			popupWidth += (font.Width(Costs[i]) + 5);
		}
	}
	if (Costs[ManaResCost]) {
		const SpellType &spell = *SpellTypeTable.at(button.Value);

		if (spell.ManaCost) {
			popupWidth = 10;
			if (UI.Resources[ManaResCost].IconWidth != -1) {
				popupWidth += (UI.Resources[ManaResCost].IconWidth + 5);
			} else {
				const CGraphic *G = UI.Resources[ManaResCost].G.get();
				if (G) {
					popupWidth += (G->Width + 5);
				}
			}
			popupWidth += font.Width(spell.ManaCost);
			popupWidth = std::max<int>(popupWidth, font.Width(spell.Name) + 10);
		} else {
			popupWidth = font.Width(button.Hint) + 10;
		}
		popupWidth = std::max<int>(popupWidth, 100);
	}
	return popupWidth;
}

int CPopupContentTypeCosts::GetHeight(const ButtonAction &button, int *Costs) const /* override */
{
	int popupHeight = 0;
	const CFont &font = this->Font ? *this->Font : GetSmallFont();

	for (unsigned int i = 1; i <= ManaResCost; ++i) {
		if (Costs[i] && UI.Resources[i].G) {
			popupHeight = std::max(UI.Resources[i].G->Height, popupHeight);
		}
	}
	return std::max(popupHeight, font.Height());
}

void CPopupContentTypeCosts::Draw(int x,
                                  int y,
                                  const CPopup &,
                                  const unsigned int,
                                  const ButtonAction &button,
                                  int *Costs) const /* override */
{
	const CFont &font = this->Font ? *this->Font : GetSmallFont();
	CLabel label(font, this->TextColor, this->HighlightColor);

	for (unsigned int i = 1; i <= MaxCosts; ++i) {
		if (Costs[i]) {
			int y_offset = 0;
			if (const CGraphic *G = UI.Resources[i].G.get()) {
				int x_offset = UI.Resources[i].IconWidth;
				G->DrawFrameClip(UI.Resources[i].IconFrame,	x , y);
				x += ((x_offset != -1 ? x_offset : G->Width) + 5);
				y_offset = G->Height;
				y_offset -= label.Height();
				y_offset /= 2;
			}
			x += label.Draw(x, y + y_offset, Costs[i]);
			x += 5;
		}
	}
	if (Costs[ManaResCost]) {
		const SpellType &spell = *SpellTypeTable[button.Value];
		if (spell.ManaCost) {
			int y_offset = 0;
			if (const CGraphic *G = UI.Resources[ManaResCost].G.get()) {
				int x_offset =  UI.Resources[ManaResCost].IconWidth;
				x += 5;
				G->DrawFrameClip(UI.Resources[ManaResCost].IconFrame, x, y);
				x += ((x_offset != -1 ? x_offset : G->Width) + 5);
				y_offset = G->Height;
				y_offset -= font.Height();
				y_offset /= 2;
			}
			label.Draw(x, y + y_offset, spell.ManaCost);
		}
	}
}

void CPopupContentTypeCosts::Parse(lua_State *l) /* override */
{
	Assert(lua_istable(l, -1) || lua_isnil(l, -1));

	if (!lua_isnil(l, -1)) {
		for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
			const std::string_view key = LuaToString(l, -2);
			if (key == "Font") {
				this->Font = CFont::Get(LuaToString(l, -1));
			} else if (key == "Centered") {
				this->Centered = LuaToBoolean(l, -1);
			} else {
				LuaError(l, "'%s' invalid for method 'Costs' in DefinePopups", key.data());
			}
		}
	}
}

int CPopupContentTypeLine::GetWidth(const ButtonAction &button, int *Costs) const /* override */
{
	return this->Width;
}

int CPopupContentTypeLine::GetHeight(const ButtonAction &button, int *Costs) const /* override */
{
	return this->Height;
}

void CPopupContentTypeLine::Draw(int x,
                                 int y,
                                 const CPopup &popup,
                                 const unsigned int popupWidth,
                                 const ButtonAction &button,
                                 int *Costs) const /* override */
{
	Video.FillRectangle(this->Color, x - popup.MarginX - this->MarginX + 1,
						y, this->Width && Width < popupWidth ? Width : popupWidth - 2, Height);
}

void CPopupContentTypeLine::Parse(lua_State *l) /* override */
{
	Assert(lua_istable(l, -1) || lua_isnil(l, -1));

	if (!lua_isnil(l, -1)) {
		for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
			const std::string_view key = LuaToString(l, -2);
			if (key == "Width") {
				this->Width = LuaToNumber(l, -1);
			} else if (key == "Height") {
				this->Height = LuaToNumber(l, -1);
			} else if (key == "Color") {
				this->Color = LuaToUnsignedNumber(l, -1);
			} else {
				LuaError(l, "'%s' invalid for method 'Costs' in DefinePopups", key.data());
			}
		}
	}
}

int CPopupContentTypeVariable::GetWidth(const ButtonAction &button, int *) const /* override */
{
	CFont &font = this->Font ? *this->Font : GetSmallFont();
	TriggerData.Type = getUnitTypes()[button.Value];
	std::string text = EvalString(*this->Text);
	TriggerData.Type = nullptr;
	return font.getWidth(text);
}

int CPopupContentTypeVariable::GetHeight(const ButtonAction &, int *) const /* override */
{
	CFont &font = this->Font ? *this->Font : GetSmallFont();
	return font.Height();
}

void CPopupContentTypeVariable::Draw(int x,
                                     int y,
                                     const CPopup &,
                                     const unsigned int,
                                     const ButtonAction &button,
                                     int *) const /* override */
{
	std::string text;										// Optional text to display.
	CFont &font = this->Font ? *this->Font : GetSmallFont(); // Font to use.

	Assert(this->Index == -1 || ((unsigned int) this->Index < UnitTypeVar.GetNumberVariable()));

	CLabel label(font, this->TextColor, this->HighlightColor);

	if (this->Text) {
		TriggerData.Type = getUnitTypes()[button.Value];
		text = EvalString(*this->Text);
		TriggerData.Type = nullptr;
		if (this->Centered) {
			x += (label.DrawCentered(x, y, text) * 2);
		} else {
			x += label.Draw(x, y, text);
		}
	}

	if (this->Index != -1) {
		CUnitType &type = *getUnitTypes()[button.Value];
		int value = type.DefaultStat.Variables[this->Index].Value;
		int diff = type.Stats[ThisPlayer->Index].Variables[this->Index].Value - value;

		if (!diff) {
			label.Draw(x, y, value);
		} else {
			char buf[64];
			snprintf(buf, sizeof(buf), diff > 0 ? "%d~<+%d~>" : "%d~<-%d~>", value, diff);
			label.Draw(x, y, buf);
		}
	}
}

void CPopupContentTypeVariable::Parse(lua_State *l) /* override */
{
	Assert(lua_istable(l, -1) || lua_isstring(l, -1) || lua_isfunction(l, -1));

	if (lua_isstring(l, -1) || lua_isfunction(l, -1)) {
		this->Text = CclParseStringDesc(l);
		lua_pushnil(l); // ParseStringDesc eat token
	} else {
		for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
			const std::string_view key = LuaToString(l, -2);
			if (key == "Text") {
				this->Text = CclParseStringDesc(l);
				lua_pushnil(l); // ParseStringDesc eat token
			} else if (key == "Font") {
				this->Font = CFont::Get(LuaToString(l, -1));
			} else if (key == "Centered") {
				this->Centered = LuaToBoolean(l, -1);
			} else if (key == "Variable") {
				const std::string_view name = LuaToString(l, -1);
				this->Index = UnitTypeVar.VariableNameLookup[name];
				if (this->Index == -1) {
					LuaError(l, "unknown variable '%s'", name.data());
				}
			} else {
				LuaError(l, "'%s' invalid for method 'Text' in DefinePopups", key.data());
			}
		}
	}
}

/**
**  Parse the popup conditions.
**
**  @param l   Lua State.
*/
static std::unique_ptr<PopupConditionPanel> ParsePopupConditions(lua_State *l)
{
	Assert(lua_istable(l, -1));

	std::unique_ptr<PopupConditionPanel> condition = std::make_unique<PopupConditionPanel>();
	for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
		const std::string_view key = LuaToString(l, -2);

		if (key == "HasHint") {
			condition->HasHint = LuaToBoolean(l, -1);
		} else if (key == "HasDescription") {
			condition->HasDescription = LuaToBoolean(l, -1);
		} else if (key == "HasDependencies") {
			condition->HasDependencies = LuaToBoolean(l, -1);
		} else if (key == "ButtonValue") {
			condition->ButtonValue = LuaToString(l, -1);
		} else if (key == "ButtonAction") {
			const std::string_view value = LuaToString(l, -1);
			if (value == "move") {
				condition->ButtonAction = ButtonCmd::Move;
			} else if (value == "stop") {
				condition->ButtonAction = ButtonCmd::Stop;
			} else if (value == "attack") {
				condition->ButtonAction = ButtonCmd::Attack;
			} else if (value == "repair") {
				condition->ButtonAction = ButtonCmd::Repair;
			} else if (value == "harvest") {
				condition->ButtonAction = ButtonCmd::Harvest;
			} else if (value == "button") {
				condition->ButtonAction = ButtonCmd::Button;
			} else if (value == "build") {
				condition->ButtonAction = ButtonCmd::Build;
			} else if (value == "train-unit") {
				condition->ButtonAction = ButtonCmd::Train;
			} else if (value == "patrol") {
				condition->ButtonAction = ButtonCmd::Patrol;
			} else if (value == "explore") {
				condition->ButtonAction = ButtonCmd::Explore;
			} else if (value == "stand-ground") {
				condition->ButtonAction = ButtonCmd::StandGround;
			} else if (value == "attack-ground") {
				condition->ButtonAction = ButtonCmd::AttackGround;
			} else if (value == "return-goods") {
				condition->ButtonAction = ButtonCmd::Return;
			} else if (value == "cast-spell") {
				condition->ButtonAction = ButtonCmd::SpellCast;
			} else if (value == "research") {
				condition->ButtonAction = ButtonCmd::Research;
			} else if (value == "upgrade-to") {
				condition->ButtonAction = ButtonCmd::UpgradeTo;
			} else if (value == "unload") {
				condition->ButtonAction = ButtonCmd::Unload;
			} else if (value == "cancel") {
				condition->ButtonAction = ButtonCmd::Cancel;
			} else if (value == "cancel-upgrade") {
				condition->ButtonAction = ButtonCmd::CancelUpgrade;
			} else if (value == "cancel-train-unit") {
				condition->ButtonAction = ButtonCmd::CancelTrain;
			} else if (value == "cancel-build") {
				condition->ButtonAction = ButtonCmd::CancelBuild;
			} else {
				LuaError(l, "Unsupported button action: %s", value.data());
			}
		} else {
			int index = UnitTypeVar.BoolFlagNameLookup[key];
			if (index != -1) {
				if (condition->BoolFlags.empty()) {
					size_t new_bool_size = UnitTypeVar.GetNumberBoolFlag();
					condition->BoolFlags.resize(new_bool_size);
				}
				condition->BoolFlags[index] = Ccl2Condition(l, LuaToString(l, -1));
				continue;
			}
			index = UnitTypeVar.VariableNameLookup[key];
			if (index != -1) {
				if (condition->Variables.empty()) {
					size_t new_variables_size = UnitTypeVar.GetNumberVariable();
					condition->Variables.resize(new_variables_size);
				}
				condition->Variables[index] = Ccl2Condition(l, LuaToString(l, -1));
				continue;
			}
			LuaError(l, "'%s' invalid for Condition in DefinePopups", key.data());
		}
	}
	return condition;
}

/* static */ std::unique_ptr<CPopupContentType> CPopupContentType::ParsePopupContent(lua_State *l)
{
	Assert(lua_istable(l, -1));

	bool wrap = true;
	int marginX = MARGIN_X;
	int marginY = MARGIN_Y;
	int minWidth = 0;
	int minHeight = 0;
	std::string textColor("white");
	std::string highColor("red");
	std::unique_ptr<CPopupContentType> content;
	std::unique_ptr<PopupConditionPanel> condition;

	for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
		std::string_view key = LuaToString(l, -2);

		if (key == "Wrap") {
			wrap = LuaToBoolean(l, -1);
		} else if (key == "TextColor") {
			textColor = LuaToString(l, -1);
		} else if (key == "HighlightColor") {
			highColor = LuaToString(l, -1);
		} else if (key == "Margin") {
			CclGetPos(l, &marginX, &marginY);
		} else if (key == "MinWidth") {
			minWidth = LuaToNumber(l, -1);
		} else if (key == "MinHeight") {
			minHeight = LuaToNumber(l, -1);
		} else if (key == "More") {
			Assert(lua_istable(l, -1));
			key = LuaToString(l, -1, 1); // Method name
			lua_rawgeti(l, -1, 2); // Method data
			if (key == "ButtonInfo") {
				content = std::make_unique<CPopupContentTypeButtonInfo>();
			} else if (key == "Text") {
				content = std::make_unique<CPopupContentTypeText>();
			} else if (key == "Costs") {
				content = std::make_unique<CPopupContentTypeCosts>();
			} else if (key == "Line") {
				content = std::make_unique<CPopupContentTypeLine>();
			} else if (key == "Variable") {
				content = std::make_unique<CPopupContentTypeVariable>();
			} else {
				LuaError(l, "Invalid drawing method '%s' in DefinePopups", key.data());
			}
			content->Parse(l);
			lua_pop(l, 1); // Pop Variable Method data
		} else if (key == "Condition") {
			condition = ParsePopupConditions(l);
		} else {
			LuaError(l, "'%s' invalid for Contents in DefinePopups", key.data());
		}
	}
	content->Wrap = wrap;
	content->MarginX = marginX;
	content->MarginY = marginY;
	content->minSize.x = minWidth;
	content->minSize.y = minHeight;
	content->Condition = std::move(condition);
	content->TextColor = textColor;
	content->HighlightColor = highColor;
	return content;
}

//@}
