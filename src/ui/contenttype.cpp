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
/**@name contenttype.cpp - . */
//
//      (c) Copyright 1999-2015 by Joris Dauphin and Andrettin
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

#include <algorithm>
#include <string>
#include <type_traits>
#include "stratagus.h"

#include "ui/contenttype.h"

#include "actions.h"
#include "action/action_built.h"
#include "font.h"
#include "translate.h"
#include "unit.h"
#include "ui.h"
#include "video.h"
#include "../video/hd_classic_hud.h"

#include <variant>

using UStrInt = std::variant<int, const char *>;

extern UStrInt GetComponent(const CUnit &unit, int index, EnumVariable e, int t);

/**
**  Draw text with variable.
**
**  @param unit         unit with variable to show.
**  @param defaultfont  default font if no specific font in extra data.
*/
// UPGRADE-LOOK: the original prints the bonus an upgrade gives - "Damage: 2-9+2", "Armor: 2+2" - with
// the "+n" in yellow (255, 204, 0) (captures f3_footman_w1, f5_footman_w1a1). The
// card's texts mark that part as reverse ("~<...~>"); the baked HD face takes no font colour, so
// the marked part is tinted here. Returns the drawn width, as CLabel::Draw does.
static int DrawWithBonusTint(CLabel &label, int x, int y, std::string_view text)
{
	int w = 0;
	while (!text.empty()) {
		const auto open = text.find("~<");
		if (open == std::string_view::npos) {
			w += label.Draw(x + w, y, text);
			break;
		}
		if (open > 0) {
			w += label.Draw(x + w, y, text.substr(0, open));
		}
		text.remove_prefix(open + 2);
		const auto close = text.find("~>");
		const std::string_view marked = text.substr(0, close);
		SetGlyphTint(255, 204, 0);
		w += label.Draw(x + w, y, marked);
		SetGlyphTint(255, 255, 255);
		if (close == std::string_view::npos) {
			break;
		}
		text.remove_prefix(close + 2);
	}
	return w;
}

void CContentTypeText::Draw(const CUnit &unit, CFont *defaultfont) const /* override */
{
	int x = this->Pos.x;
	int y = this->Pos.y;
	Assert(this->Font || defaultfont);
	CFont &font = this->Font ? *this->Font : *defaultfont;
	Assert(this->Index == -1 || ((unsigned int) this->Index < UnitTypeVar.GetNumberVariable()));

	CLabel label(font);

	if (this->Text) {
		std::string text = EvalString(*this->Text);
		std::string::size_type pos;
		if ((pos = text.find("~|")) != std::string::npos) {
			const int offsetx = font.Width(std::string_view(text).substr(0, pos));
			x += DrawWithBonusTint(label, x - offsetx, y, text) - offsetx;
		} else if (this->Centered) {
			x += (label.DrawCentered(x, y, text) * 2);
		} else {
			x += label.Draw(x, y, text);
		}
	}

	if (this->ShowName) {
		label.DrawCentered(x, y, unit.Type->Name);
		return;
	}

	if (this->Index != -1) {
		if (!this->Stat) {
			// CASTER-PANEL: a value with no caption before it and Centered set is centred on its
			// position (the number inside the "Magic:" bar).
			const bool centred = this->Centered && !this->Text;
			std::visit([&](const auto &value) {
				if (centred) {
					if constexpr (std::is_same_v<std::decay_t<decltype(value)>, int>) {
						label.DrawCentered(x, y, std::to_string(value));
					} else {
						label.DrawCentered(x, y, std::string_view(value));
					}
				} else {
					label.Draw(x, y, value);
				}
			}, GetComponent(unit, this->Index, this->Component, 0));
		} else {
			int value = unit.Type->MapDefaultStat.Variables[this->Index].Value;
			int diff = unit.Stats->Variables[this->Index].Value - value;

			if (!diff) {
				label.Draw(x, y, value);
			} else {
				char buf[64];
				snprintf(buf, sizeof(buf), diff > 0 ? "%d~<+%d~>" : "%d~<-%d~>", value, diff);
				DrawWithBonusTint(label, x, y, buf);
			}
		}
	}
}

namespace
{
std::string tr(const char *s)
{
	return Translate(s);
}

auto tr(int n)
{
	return n;
}

auto printfArg(const std::string& s)
{
	return s.c_str();
}

auto printfArg(int n)
{
	return n;
}

} // namespace

/**
**  Draw formatted text with variable value.
**
**  @param unit         unit with variable to show.
**  @param defaultfont  default font if no specific font in extra data.
**
**  @note text is limited to 256 chars. (enough?)
**  @note text must have exactly 1 %d.
**  @bug if text format is incorrect.
*/
void CContentTypeFormattedText::Draw(const CUnit &unit, CFont *defaultfont) const /* override */
{
	char buf[256]{};

	Assert(this->Font || defaultfont);
	CFont &font = this->Font ? *this->Font : *defaultfont;
	CLabel label(font);

	Assert((unsigned int) this->Index < UnitTypeVar.GetNumberVariable());
	const auto usi1 = GetComponent(unit, this->Index, this->Component, 0);
	std::visit([&](auto v) { snprintf(buf, sizeof(buf), this->Format.c_str(), printfArg(tr(v))); },
	           usi1);

	char *pos;
	if ((pos = strstr(buf, "~|")) != nullptr) {
		const int offsetx = font.Width(std::string_view(buf, pos - buf));
		label.Draw(this->Pos.x - offsetx, this->Pos.y, buf);
	} else if (this->Centered) {
		label.DrawCentered(this->Pos.x, this->Pos.y, buf);
	} else {
		label.Draw(this->Pos.x, this->Pos.y, buf);
	}
}

/**
**  Draw formatted text with variable value.
**
**  @param unit         unit with variable to show.
**  @param defaultfont  default font if no specific font in extra data.
**
**  @note text is limited to 256 chars. (enough?)
**  @note text must have exactly 2 %d.
**  @bug if text format is incorrect.
*/
void CContentTypeFormattedText2::Draw(const CUnit &unit, CFont *defaultfont) const /* override */
{
	char buf[256]{};

	Assert(this->Font || defaultfont);
	CFont &font = this->Font ? *this->Font : *defaultfont;
	CLabel label(font);

	const auto usi1 = GetComponent(unit, this->Index1, this->Component1, 0);
	const auto usi2 = GetComponent(unit, this->Index2, this->Component2, 0);
	std::visit(
		[&](auto v1, auto v2) {
			snprintf(buf, sizeof(buf), this->Format.c_str(), printfArg(tr(v1)), printfArg(tr(v2)));
		},
		usi1,
		usi2);
	char *pos;
	if ((pos = strstr(buf, "~|")) != nullptr) {
		const int offsetx = font.Width(std::string_view(buf, pos - buf));
		label.Draw(this->Pos.x - offsetx, this->Pos.y, buf);
	} else if (this->Centered) {
		label.DrawCentered(this->Pos.x, this->Pos.y, buf);
	} else {
		label.Draw(this->Pos.x, this->Pos.y, buf);
	}
}


/**
**  Classic view: the text a content of the info panel prints for the unit, as its Draw builds it
**  (the caption, then the value), with the place and the face it would be drawn with.
*/
bool ClassicContentText(const CContentType &content, const CUnit &unit, CFont *defaultFont,
                        ClassicPanelText &out)
{
	char buf[256]{};
	out.x = content.Pos.x;
	out.y = content.Pos.y;
	if (const auto *text = dynamic_cast<const CContentTypeText *>(&content)) {
		out.font = text->Font ? text->Font : defaultFont;
		out.centred = text->Centered;
		out.text = text->Text ? EvalString(*text->Text) : std::string();
		if (text->ShowName) {
			out.text += unit.Type->Name;
		} else if (text->Index != -1 && text->Stat) {
			const int value = unit.Type->MapDefaultStat.Variables[text->Index].Value;
			const int diff = unit.Stats->Variables[text->Index].Value - value;
			if (!diff) {
				out.text += std::to_string(value);
			} else {
				snprintf(buf, sizeof(buf), diff > 0 ? "%d~<+%d~>" : "%d~<-%d~>", value, diff);
				out.text += buf;
			}
		} else if (text->Index != -1) {
			std::visit([&](const auto &value) {
				if constexpr (std::is_same_v<std::decay_t<decltype(value)>, int>) {
					out.text += std::to_string(value);
				} else {
					out.text += value;
				}
			}, GetComponent(unit, text->Index, text->Component, 0));
		}
		return out.font != nullptr && !out.text.empty();
	}
	if (const auto *text = dynamic_cast<const CContentTypeFormattedText *>(&content)) {
		out.font = text->Font ? text->Font : defaultFont;
		out.centred = text->Centered;
		std::visit([&](auto v) { snprintf(buf, sizeof(buf), text->Format.c_str(), printfArg(tr(v))); },
		           GetComponent(unit, text->Index, text->Component, 0));
		out.text = buf;
		return out.font != nullptr && !out.text.empty();
	}
	if (const auto *text = dynamic_cast<const CContentTypeFormattedText2 *>(&content)) {
		out.font = text->Font ? text->Font : defaultFont;
		out.centred = text->Centered;
		std::visit(
			[&](auto v1, auto v2) {
				snprintf(buf, sizeof(buf), text->Format.c_str(), printfArg(tr(v1)), printfArg(tr(v2)));
			},
			GetComponent(unit, text->Index1, text->Component1, 0),
			GetComponent(unit, text->Index2, text->Component2, 0));
		out.text = buf;
		return out.font != nullptr && !out.text.empty();
	}
	return false;
}

/**
**  Get unit from a unit depending of the relation.
**
**  @param unit  unit reference.
**  @param e     relation with unit.
**
**  @return      The desired unit.
*/
static const CUnit *GetUnitRef(const CUnit &unit, EnumUnit e)
{
	switch (e) {
		case EnumUnit::UnitRefItSelf:
			return &unit;
		case EnumUnit::UnitRefInside:
			return unit.InsideUnits.empty() ? nullptr: unit.InsideUnits[0];
		case EnumUnit::UnitRefContainer:
			return unit.Container;
		case EnumUnit::UnitRefWorker:
			if (unit.CurrentAction() == UnitAction::Built) {
				COrder_Built &order = *static_cast<COrder_Built *>(unit.CurrentOrder());

				return order.GetWorkerPtr();
			} else {
				return nullptr;
			}
		case EnumUnit::UnitRefGoal:
			return unit.Goal;
		default:
			Assert(0);
	}
	return nullptr;
}

/**
**  Draw icon for unit.
**
**  @param unit         unit with icon to show.
**  @param defaultfont  unused.
*/
void CContentTypeIcon::Draw(const CUnit &unit, CFont *) const /* override */
{
	const CUnit *unitToDraw = GetUnitRef(unit, this->UnitRef);

	if (unitToDraw && unitToDraw->Type->Icon.Icon) {
		if (this->ButtonIcon) {
			unitToDraw->Type->Icon.Icon->DrawUnitIcon(*UI.SingleSelectedButton->Style, 0, this->Pos, "",
				unitToDraw->RescuedFrom ? unitToDraw->RescuedFrom->Index : unitToDraw->Player->Index);
		} else if (this->SingleSelectionIcon) {
			unitToDraw->Type->Icon.Icon->DrawSingleSelectionIcon(*UI.SingleSelectedButton->Style, 0, this->Pos, "", *unitToDraw);
		} else if (this->GroupSelectionIcon) {
			unitToDraw->Type->Icon.Icon->DrawGroupSelectionIcon(*UI.SingleSelectedButton->Style, 0, this->Pos, "", *unitToDraw);
		} else if (this->TransportIcon) {
			unitToDraw->Type->Icon.Icon->DrawContainedIcon(*UI.SingleSelectedButton->Style, 0, this->Pos, "", *unitToDraw);
		}
	}
}

/**
**  Draw the graphic
*/
void CContentTypeGraphic::Draw(const CUnit &, CFont *) const /* override */
{
	auto g = CGraphic::Get(this->graphic);
	if (g) {
		if (this->frame) {
			g->DrawFrameClip(this->frame, this->Pos.x, this->Pos.y);
		} else {
			g->DrawClip(this->Pos.x, this->Pos.y);
		}
	}
}

/**
**  Draw life bar of a unit using selected variable.
**  Placed under icons on top-panel.
**
**  @param unit         Pointer to unit.
**  @param defaultfont  FIXME: docu
**
**  @todo Color and percent value Parametrisation.
*/
void CContentTypeLifeBar::Draw(const CUnit &unit, CFont *) const /* override */
{
	int f;
	if (this->Index != -1) {
		Assert((unsigned int) this->Index < UnitTypeVar.GetNumberVariable());
		if (!unit.Variable[this->Index].Max) {
			return;
		}
		f = (100 * unit.Variable[this->Index].Value) / unit.Variable[this->Index].Max;
	} else {
		f = (100 * EvalNumber(*this->ValueFunc)) / this->ValueMax;
		f = f > 100 ? 100 : f;
	}
	if (f < 0) {
		return;
	}
	int i = 0;

	// get to right color
	while (static_cast<unsigned int>(f) < this->values[i]) {
		i++;
	}
	const Uint32 color = IndexToColor(this->colors[i]);

	if (this->hasSeat) {
		// M666-9: the original's bar sits on a black seat and its unfilled part is grey (skin
		// `hud_hp_bar`; captures e01, hi10, i02). The margins and the grey come from Lua.
		Video.FillRectangleClip(ColorBlack, this->Pos.x - 1 - this->seat[0], this->Pos.y - 1 - this->seat[1],
		                        this->Width + this->seat[0] + this->seat[2],
		                        this->Height + this->seat[1] + this->seat[3]);
		if (this->hasTrack) {
			Video.FillRectangleClip(Video.MapRGB(TheScreen->format, this->track), this->Pos.x - 1,
			                        this->Pos.y - 1, this->Width, this->Height);
		}
	} else if (this->borderColor) {
		// Border. We have a simple heuristic to determine how big it is...
		// TODO: make configurable?
		auto thickness = this->Height <= 6 ? 2 : 3;
		Video.FillRectangleClip(*this->borderColor,
		                        this->Pos.x - 2,
		                        this->Pos.y - 2,
		                        this->Width + thickness,
		                        this->Height + thickness);
	}

	Video.FillRectangleClip(color, this->Pos.x - 1, this->Pos.y - 1,
							(f * this->Width) / 100, this->Height);
}

/**
**  Draw life bar of a unit using selected variable.
**  Placed under icons on top-panel.
**
**  @param unit         Pointer to unit.
**  @param defaultfont  FIXME: docu
**
**  @todo Color and percent value Parametrisation.
*/
void CContentTypeCompleteBar::Draw(const CUnit &unit, CFont *) const /* override */
{
	Assert((unsigned int) this->varIndex < UnitTypeVar.GetNumberVariable());
	if (!unit.Variable[this->varIndex].Max) {
		return;
	}
	int x = this->Pos.x;
	int y = this->Pos.y;
	int w = this->width;
	int h = this->height;
	Assert(w > 0);
	Assert(h > 4);
	if (this->frame > 0) {
		// The frame from its outer edge inwards, as measured at six pixels: three light steps,
		// a dark one, then black; scaled to the thickness asked for.
		static const int steps[6][3] = {{252, 248, 240}, {243, 239, 231}, {159, 157, 152},
		                                {75, 74, 71}, {8, 8, 8}, {8, 8, 8}};
		const int t = this->frame;
		for (int i = 0; i < t && 2 * i < w && 2 * i < h; ++i) {
			const int *c = steps[std::min(5, i * 6 / t)];
			Video.DrawRectangleClip(Video.MapRGB(TheScreen->format, c[0], c[1], c[2]), x + i, y + i, w - 2 * i, h - 2 * i);
		}
		const int tw = w - 2 * t;
		const int th = h - 2 * t;
		if (tw > 0 && th > 0) {
			const CVariable &v = unit.Variable[this->varIndex];
			const int fw = std::max(0, std::min(tw, (int) ((long long) v.Value * tw / v.Max)));
			Video.FillRectangleClip(Video.MapRGB(TheScreen->format, trackRGB[0], trackRGB[1], trackRGB[2]), x + t, y + t, tw, th);
			if (fw > 0) {
				Video.FillRectangleClip(Video.MapRGB(TheScreen->format, fillRGB[0], fillRGB[1], fillRGB[2]), x + t, y + t, fw, th);
			}
		}
		return;
	}
	const Uint32 color = (colorIndex != -1) ? IndexToColor(colorIndex) : UI.CompletedBarColor;
	const int f = (100 * unit.Variable[this->varIndex].Value) / unit.Variable[this->varIndex].Max;

	if (!this->hasBorder) {
		Video.FillRectangleClip(color, x, y, f * w / 100, h);
		if (UI.CompletedBarShadow) {
			// Shadow
			Video.DrawVLine(ColorGray, x + f * w / 100, y, h);
			Video.DrawHLine(ColorGray, x, y + h, f * w / 100);

			// |~  Light
			Video.DrawVLine(ColorWhite, x, y, h);
			Video.DrawHLine(ColorWhite, x, y, f * w / 100);
		}
	} else {
		Video.DrawRectangleClip(ColorWhite,  x,     y,     w + 4, h);
		Video.DrawRectangleClip(ColorBlack, x + 1, y + 1, w + 2, h - 2);
		Video.FillRectangleClip(color, x + 2, y + 2, f * w / 100, h - 4);
	}
}

void CContentTypeText::Parse(lua_State *l) /* override */
{
	Assert(lua_istable(l, -1) || lua_isstring(l, -1));

	if (lua_isstring(l, -1)) {
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
			} else if (key == "Component") {
				this->Component = Str2EnumVariable(l, LuaToString(l, -1));
			} else if (key == "Stat") {
				this->Stat = LuaToBoolean(l, -1);
			} else if (key == "ShowName") {
				this->ShowName = LuaToBoolean(l, -1);
			} else {
				LuaError(l, "'%s' invalid for method 'Text' in DefinePanelContents", key.data());
			}
		}
	}
}

void CContentTypeFormattedText::Parse(lua_State *l) /* override */
{
	Assert(lua_istable(l, -1));

	for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
		const std::string_view key = LuaToString(l, -2);
		if (key == "Format") {
			this->Format = LuaToString(l, -1);
		} else if (key == "Font") {
			this->Font = CFont::Get(LuaToString(l, -1));
		} else if (key == "Variable") {
			const std::string_view name = LuaToString(l, -1);
			this->Index = UnitTypeVar.VariableNameLookup[name];
			if (this->Index == -1) {
				LuaError(l, "unknown variable '%s'", name.data());
			}
		} else if (key == "Component") {
			this->Component = Str2EnumVariable(l, LuaToString(l, -1));
		} else if (key == "Centered") {
			this->Centered = LuaToBoolean(l, -1);
		} else {
			LuaError(l, "'%s' invalid for method 'FormattedText' in DefinePanelContents", key.data());
		}
	}
}

void CContentTypeFormattedText2::Parse(lua_State *l) /* override */
{
	Assert(lua_istable(l, -1));
	for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
		const std::string_view key = LuaToString(l, -2);
		if (key == "Format") {
			this->Format = LuaToString(l, -1);
		} else if (key == "Font") {
			this->Font = CFont::Get(LuaToString(l, -1));
		} else if (key == "Variable") {
			const std::string_view name = LuaToString(l, -1);
			this->Index1 = UnitTypeVar.VariableNameLookup[name];
			this->Index2 = this->Index1;
			if (this->Index1 == -1) {
				LuaError(l, "unknown variable '%s'", name.data());
			}
		} else if (key == "Component") {
			this->Component1 = Str2EnumVariable(l, LuaToString(l, -1));
			this->Component2 = Str2EnumVariable(l, LuaToString(l, -1));
		} else if (key == "Variable1") {
			const std::string_view name = LuaToString(l, -1);
			this->Index1 = UnitTypeVar.VariableNameLookup[name];
			if (this->Index1 == -1) {
				LuaError(l, "unknown variable '%s'", name.data());
			}
		} else if (key == "Component1") {
			this->Component1 = Str2EnumVariable(l, LuaToString(l, -1));
		} else if (key == "Variable2") {
			const std::string_view name = LuaToString(l, -1);
			this->Index2 = UnitTypeVar.VariableNameLookup[name];
			if (this->Index2 == -1) {
				LuaError(l, "unknown variable '%s'", name.data());
			}
		} else if (key == "Component2") {
			this->Component2 = Str2EnumVariable(l, LuaToString(l, -1));
		} else if (key == "Centered") {
			this->Centered = LuaToBoolean(l, -1);
		} else {
			LuaError(l, "'%s' invalid for method 'FormattedText2' in DefinePanelContents", key.data());
		}
	}
}

/**
**  Return enum from string about variable component.
**
**  @param l Lua State.
**  @param s string to convert.
**
**  @return  Corresponding value.
**  @note    Stop on error.
*/
static EnumUnit Str2EnumUnit(lua_State *l, std::string_view s)
{
	static const struct mapping
	{
		const char *s;
		EnumUnit value;
	} list[] = {{"ItSelf", EnumUnit::UnitRefItSelf},
	            {"Inside", EnumUnit::UnitRefInside},
	            {"Container", EnumUnit::UnitRefContainer},
	            {"Worker", EnumUnit::UnitRefWorker},
	            {"Goal", EnumUnit::UnitRefGoal}}; // List of possible values.

	if (const auto it = ranges::find(list, s, &mapping::s); it != std::end(list)) {
		return it->value;
	}
	LuaError(l, "'%s' is a invalid Unit reference", s.data());
	ExitFatal(-1);
}

void CContentTypeIcon::Parse(lua_State *l) /* override */
{
	SingleSelectionIcon = GroupSelectionIcon = TransportIcon = false;
	ButtonIcon = true;
	for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
		const std::string_view key = LuaToString(l, -2);
		if (key == "Unit") {
			this->UnitRef = Str2EnumUnit(l, LuaToString(l, -1));
		} else if (key == "SingleSelection") {
			bool flag = LuaToBoolean(l, -1);
			if (!ButtonIcon && flag) {
				LuaError(l, "Only one of SingleSelection, GroupSelection, TransportSelection can be chosen");
			}
			ButtonIcon = !flag;
			SingleSelectionIcon = flag;
		} else if (key == "GroupSelection") {
			bool flag = LuaToBoolean(l, -1);
			if (!ButtonIcon && flag) {
				LuaError(l, "Only one of SingleSelection, GroupSelection, TransportSelection can be chosen");
			}
			ButtonIcon = !flag;
			GroupSelectionIcon = flag;
		} else if (key == "TransportSelection") {
			bool flag = LuaToBoolean(l, -1);
			if (!ButtonIcon && flag) {
				LuaError(l, "Only one of SingleSelection, GroupSelection, TransportSelection can be chosen");
			}
			ButtonIcon = !flag;
			TransportIcon = flag;
		} else {
			LuaError(l, "'%s' invalid for method 'Icon' in DefinePanelContents", key.data());
		}
	}
}

void CContentTypeGraphic::Parse(lua_State *l) /* override */
{
	if (lua_isstring(l, -1)) {
		this->graphic = LuaToString(l, -1);
		this->frame = 0;
	} else if (lua_istable(l, -1)) {
		for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
			const std::string_view key = LuaToString(l, -2);
			if (key == "Graphic") {
				this->graphic = LuaToString(l, -1);
			} else if (key == "Frame") {
				this->frame = LuaToNumber(l, -1);
			}
		}
	}
}

void CContentTypeLifeBar::Parse(lua_State *l) /* override */
{
	for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
		const std::string_view key = LuaToString(l, -2);
		if (key == "Variable") {
			if (lua_isstring(l, -1)) {
				const std::string_view name = LuaToString(l, -1);
				this->Index = UnitTypeVar.VariableNameLookup[name];
				if (this->Index == -1) {
					LuaError(l, "unknown variable '%s'", name.data());
				}
			} else {
				if (!lua_istable(l, -1)) {
					LuaError(l, "incorrect argument, need list of size 2 with {function, max} or a string with the name of a unit variable");
				}
				for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
					const std::string_view key = LuaToString(l, -2);
					if (key == "Max") {
						this->ValueMax = LuaToNumber(l, -1);
					} else if (key == "Value") {
						this->ValueFunc = CclParseNumberDesc(l);
						lua_pushnil(l); // ParseStringDesc eat token
					} else {
						lua_pop(l, 1);
						LuaError(l, "unknow value '%s'", key.data());
					}
				}
				if (this->ValueMax == -1) {
					this->ValueMax = 100;
				}
				if (this->ValueFunc == nullptr) {
					LuaError(l, "didn't set a value function");
				}
			}
		} else if (key == "Height") {
			this->Height = LuaToNumber(l, -1);
		} else if (key == "Width") {
			this->Width = LuaToNumber(l, -1);
		} else if (key == "Colors") {
			if (!lua_istable(l, -1)) {
				LuaError(l, "incorrect argument, need list");
			}
			const int color_len = lua_rawlen(l, -1);
			if (color_len == 0) {
				LuaError(l, "need at least one {percentage, color} pair, got 0");
			}
			this->colors.resize(color_len);
			this->values.resize(color_len);
			int i = 0;
			for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
				if (!lua_istable(l, -1) || lua_rawlen(l, -1) != 2) {
					LuaError(l, "incorrect argument, need list of size 2 with {percentage, color}");
				}
				this->values[i] = LuaToNumber(l, -1, 1);
				const std::string_view colorName = LuaToString(l, -1, 2);
				const int color = GetColorIndexByName(colorName);
				if (color == -1) {
					LuaError(l, "incorrect color: '%s' ", colorName.data());
				}
				this->colors[i] = color;
				i++;
			}
			if (this->values.back() != 0) {
				LuaError(l, "the last {percentage, color} pair must be for 0%%");
			}
		} else if (key == "Border") {
			if (lua_isboolean(l, -1)) {
				if (LuaToBoolean(l, -1)) {
					this->borderColor = 0;
				} else {
					this->borderColor = std::nullopt;
				}
			} else {
				this->borderColor = LuaToUnsignedNumber(l, -1);
			}
		} else if (key == "Seat") {
			// M666-9: {left, top, right, bottom} px of black around the bar
			if (!lua_istable(l, -1) || lua_rawlen(l, -1) != 4) {
				LuaError(l, "incorrect argument, need list of size 4 with {left, top, right, bottom}");
			}
			for (int i = 0; i < 4; ++i) {
				this->seat[i] = LuaToNumber(l, -1, i + 1);
			}
			this->hasSeat = true;
		} else if (key == "Track") {
			// M666-9: {r, g, b} of the unfilled part
			if (!lua_istable(l, -1) || lua_rawlen(l, -1) != 3) {
				LuaError(l, "incorrect argument, need list of size 3 with {r, g, b}");
			}
			this->track = CColor(LuaToNumber(l, -1, 1), LuaToNumber(l, -1, 2), LuaToNumber(l, -1, 3));
			this->hasTrack = true;
		} else {
			LuaError(l, "'%s' invalid for method 'LifeBar' in DefinePanelContents", key.data());
		}
	}
	// Default value and checking errors.
	if (this->Height <= 0) {
		this->Height = 5; // Default value.
	}
	if (this->Width <= 0) {
		this->Width = 50; // Default value.
	}
	if (this->Index == -1 && this->ValueFunc == nullptr) {
		LuaError(l, "variable undefined for LifeBar");
	}
	if (this->colors.empty() || this->values.empty()) {
		this->values = {75, 50, 25, 0};
		this->colors = {static_cast<unsigned>(GetColorIndexByName("dark-green")),
		                static_cast<unsigned>(GetColorIndexByName("yellow")),
		                static_cast<unsigned>(GetColorIndexByName("orange")),
		                static_cast<unsigned>(GetColorIndexByName("red"))};
	}
}

void CContentTypeCompleteBar::Parse(lua_State *l) /* override */
{
	for (lua_pushnil(l); lua_next(l, -2); lua_pop(l, 1)) {
		const std::string_view key = LuaToString(l, -2);

		if (key == "Variable") {
			const std::string_view name = LuaToString(l, -1);
			this->varIndex = UnitTypeVar.VariableNameLookup[name];
			if (this->varIndex == -1) {
				LuaError(l, "unknown variable '%s'", name.data());
			}
		} else if (key == "Height") {
			this->height = LuaToNumber(l, -1);
		} else if (key == "Width") {
			this->width = LuaToNumber(l, -1);
		} else if (key == "Border") {
			this->hasBorder = LuaToBoolean(l, -1);
		} else if (key == "Color") {
			const std::string_view colorName = LuaToString(l, -1);
			this->colorIndex = GetColorIndexByName(colorName);
			if (colorIndex == -1) {
				LuaError(l, "incorrect color: '%s' ", colorName.data());
			}
		} else if (key == "Frame") {
			this->frame = LuaToNumber(l, -1);
		} else if (key == "FillRGB" || key == "TrackRGB") {
			int *rgb = (key == "FillRGB") ? this->fillRGB : this->trackRGB;
			if (!lua_istable(l, -1) || lua_rawlen(l, -1) != 3) {
				LuaError(l, "CompleteBar: %s needs {r, g, b}", key.data());
			}
			for (int i = 0; i < 3; ++i) {
				rgb[i] = LuaToNumber(l, -1, i + 1);
			}
		} else {
			LuaError(l, "'%s' invalid for method 'CompleteBar' in DefinePanelContents", key.data());
		}
	}
	// Default value and checking errors.
	if (this->height <= 0) {
		this->height = 5; // Default value.
	}
	if (this->width <= 0) {
		this->width = 50; // Default value.
	}
	if (this->varIndex == -1) {
		LuaError(l, "variable undefined for CompleteBar");
	}
}

//@}
