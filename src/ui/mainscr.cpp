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
/**@name mainscr.cpp - The main screen. */
//
//      (c) Copyright 1998-2015 by Lutz Sammer, Valery Shchedrin,
//      Jimmy Salmon and Andrettin
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

#include "action/action_built.h"
#include "action/action_research.h"
#include "action/action_train.h"
#include "action/action_upgradeto.h"
#include "editor.h"
#include "font.h"
#include "icons.h"
#include "interface.h"
#include "map.h"
#include "menus.h"
#include "network.h"
#include "player.h"
#include "script.h"
#include "settings.h"
#include "sound.h"
#include "spells.h"
#include "translate.h"
#include "trigger.h"
#include "ui/contenttype.h"
#include "uianchor.h"
#include "uiscale.h"
#include "ui.h"
#include "unit.h"
#include "unitsound.h"
#include "unittype.h"
#include "upgrade.h"
#include "video.h"

#ifdef DEBUG
#include "../ai/ai_local.h"
#endif

#include <sstream>
#include <variant>

/*----------------------------------------------------------------------------
--  UI BUTTONS
----------------------------------------------------------------------------*/

static void DrawMenuButtonArea_noNetwork()
{
	if (UI.MenuButton.X != -1) {
		DrawUIButton(UI.MenuButton.Style,
					 (ButtonAreaUnderCursor == ButtonArea::Menu
					  && ButtonUnderCursor == ButtonUnderMenu ? MI_FLAGS_ACTIVE : 0) |
					 (GameMenuButtonClicked ? MI_FLAGS_CLICKED : 0),
					 // (UI.MenuButton.Clicked ? MI_FLAGS_CLICKED : 0),
					 UI.MenuButton.X, UI.MenuButton.Y,
					 UI.MenuButton.Text);
	}
}

static void DrawMenuButtonArea_Network()
{
	if (UI.NetworkMenuButton.X != -1) {
		DrawUIButton(UI.NetworkMenuButton.Style,
					 (ButtonAreaUnderCursor == ButtonArea::Menu
					  && ButtonUnderCursor == ButtonUnderNetworkMenu ? MI_FLAGS_ACTIVE : 0) |
					 (GameMenuButtonClicked ? MI_FLAGS_CLICKED : 0),
					 // (UI.NetworkMenuButton.Clicked ? MI_FLAGS_CLICKED : 0),
					 UI.NetworkMenuButton.X, UI.NetworkMenuButton.Y,
					 UI.NetworkMenuButton.Text);
	}
	if (UI.NetworkDiplomacyButton.X != -1) {
		DrawUIButton(UI.NetworkDiplomacyButton.Style,
					 (ButtonAreaUnderCursor == ButtonArea::Menu
					  && ButtonUnderCursor == ButtonUnderNetworkDiplomacy ? MI_FLAGS_ACTIVE : 0) |
					 (GameDiplomacyButtonClicked ? MI_FLAGS_CLICKED : 0),
					 // (UI.NetworkDiplomacyButton.Clicked ? MI_FLAGS_CLICKED : 0),
					 UI.NetworkDiplomacyButton.X, UI.NetworkDiplomacyButton.Y,
					 UI.NetworkDiplomacyButton.Text);
	}
}

/**
**  Draw menu button area.
*/
void DrawMenuButtonArea()
{
	if (!IsNetworkGame()) {
		DrawMenuButtonArea_noNetwork();
	} else {
		DrawMenuButtonArea_Network();
	}
}

void DrawUserDefinedButtons()
{
	for (size_t i = 0; i < UI.UserButtons.size(); ++i) {
		const CUIUserButton &button = UI.UserButtons[i];

		if (button.Button.X != -1) {
			// 127 of 255: the brightest pixel of the dim caption in the original with a window up
			// (captures o34_h1_menu_open, o50_o1_menu_open; the caption beside it reads 255).
			if (button.Dim) {
				SetGlyphShade(127);
			}
			DrawUIButton(button.Button.Style,
						 (ButtonAreaUnderCursor == ButtonArea::User
						  && size_t(ButtonUnderCursor) == i ? MI_FLAGS_ACTIVE : 0) |
						 (button.Clicked ? MI_FLAGS_CLICKED : 0),
						 button.Button.X, button.Button.Y,
						 button.Button.Text);
			if (button.Dim) {
				SetGlyphShade(255);
			}
		}
	}
}

/*----------------------------------------------------------------------------
--  Icons
----------------------------------------------------------------------------*/

/**
**  Draw life bar of a unit at x,y.
**  Placed under icons on top-panel.
**
**  @param unit  Pointer to unit.
**  @param x     Screen X position of icon
**  @param y     Screen Y position of icon
*/
// NG26: cellW/cellH are the button style's on-screen size -- the rect the portrait was actually
// drawn into. The bar used to size and offset itself from Icon.Icon->G->Width/Height (the icon
// graphic's NATIVE size), so once the portrait is drawn scaled to its cell the bar no longer matched
// it: narrower than the portrait and vertically offset. Pass 0/0 to keep the old native behaviour.
static void UiDrawLifeBar(const CUnit &unit, int x, int y, int cellW = 0, int cellH = 0)
{
	// FIXME: add icon borders
	int hBar, hAll;
	if (Preference.IconsShift) {
		hBar = 6;
		hAll = 10;
		if (Preference.IconFrameG && Preference.PressedIconFrameG) {
			y -= hBar;
		}
	} else {
		hBar = 5;
		hAll = 7;
	}
	y += (cellH > 0) ? cellH : unit.Type->Icon.Icon->G->Height;
	y += UI.LifeBarYOffset;
	const int barW = (cellW > 0) ? cellW : unit.Type->Icon.Icon->G->Width;
	if (UI.LifeBarBorder) {
		Video.FillRectangleClip(ColorBlack, x - 4, y + 2, barW + 8, hAll);
	}

	if (unit.Variable[HP_INDEX].Value) {
		Uint32 color;
		int f = (100 * unit.Variable[HP_INDEX].Value) / unit.Variable[HP_INDEX].Max;

		// get to right color
		int i = 0;
		while (f < UI.LifeBarPercents[i]) {
			i++;
		}
		color = UI.LifeBarColorsInt[i];

		f = (f * (barW + 6)) / 100;
		Video.FillRectangleClip(color, x - 2 + UI.LifeBarPadding, y + 4 + UI.LifeBarPadding,
			std::max(f - 2 - UI.LifeBarPadding * 2, 0), hBar - UI.LifeBarPadding);
	}
}

/**
**  PANEL-A: the hit-point bar under a cell of the selection grid. It is the bar of the single
**  unit's panel (skin `hud_hp_bar`, scripts/ui.lua M666-9) under every cell: at k = 3 the fill is
**  150 x 15 px (108 x 15 in the 4-column grid), 3 px inside the cell's edges and 3 px under it,
**  on a black seat 6 px larger on every side, the unfilled part grey (work/agent-orig-scenes
**  captures u03 u07 u11 k02 k19). The sizes come from Lua (UI.SelectedBar*). Colours: green
**  from 75 %, yellow from 50 %, red under it -- the single panel's rule (scripts/ui.lua
**  HP_COLORS); in the grid green at 100 % and yellow at 61 % and 50 % are seen (k02, k19), a
**  red cell is not captured.
**  Drawn BEFORE the cell's picture: the frame covers the seat's top rows, as in the original.
**
**  @param unit   unit of the cell
**  @param x      screen x of the cell
**  @param y      screen y of the cell
**  @param cellW  width of the cell
**  @param cellH  height of the cell
*/
static void UiDrawCellLifeBar(const CUnit &unit, int x, int y, int cellW, int cellH)
{
	const int fx = x + UI.SelectedBarInset;
	const int fy = y + cellH + UI.SelectedBarGap;
	const int fw = cellW - 2 * UI.SelectedBarInset;
	const int fh = UI.SelectedBarHeight;
	const int seat = UI.SelectedBarSeat;
	if (fw <= 0 || fh <= 0) {
		return;
	}
	Video.FillRectangleClip(ColorBlack, fx - seat, fy - seat, fw + 2 * seat, fh + 2 * seat);
	Video.FillRectangleClip(Video.MapRGB(TheScreen->format, UI.SelectedBarTrack), fx, fy, fw, fh);
	const int max = unit.Variable[HP_INDEX].Max;
	const int value = unit.Variable[HP_INDEX].Value;
	if (max <= 0 || value <= 0) {
		return;
	}
	const int f = std::min(100, (100 * value) / max);
	const Uint32 color = f >= 75 ? ColorDarkGreen : f >= 50 ? ColorYellow : ColorRed;
	Video.FillRectangleClip(color, fx, fy, (f * fw) / 100, fh);
}

/*
**  PANEL-A: the cells of the selection grid.
**  In the original a selected unit that dies leaves its cell EMPTY and the others stay where
**  they are (work/agent-orig-scenes captures u09: nine dragged, a hole in row 2 column 1;
**  k19: two left in cells 2 and 3). `Selected` cannot carry that: UnSelectUnit moves the last
**  unit into the freed place. So the panel keeps its own list of cells. It stands as long as
**  the units in it are exactly the selected ones; LetUnitDie empties the cell of a unit that
**  dies; any other change of the selection (a unit added, taken out, gone into a building, a
**  new selection, a new game) makes the list anew from `Selected`. Only pointers are compared,
**  a unit in the list is never touched unless it is selected.
**  NOT established in the original: what the panel shows when one unit is left of a group
**  (here: the single unit's panel, as before), and whether a cell also stays for a unit that
**  leaves the selection alive (here: it does not).
*/
static std::vector<CUnit *> SelectionCells;

void SelectionCellUnitDied(const CUnit &unit)
{
	for (CUnit *&cell : SelectionCells) {
		if (cell == &unit) {
			cell = nullptr;
		}
	}
}

const std::vector<CUnit *> &GetSelectionCells()
{
	if (Selected.size() < 2) {
		SelectionCells.clear();
		return SelectionCells;
	}
	size_t live = 0;
	bool same = true;
	for (const CUnit *cell : SelectionCells) {
		if (cell == nullptr) {
			continue;
		}
		++live;
		if (ranges::find(Selected, cell) == Selected.end()) {
			same = false;
			break;
		}
	}
	if (!same || live != Selected.size()) {
		SelectionCells.assign(Selected.begin(), Selected.end());
	}
	return SelectionCells;
}

const std::vector<CUIButton> &GetSelectionCellButtons(size_t cells)
{
	// 3 columns while the cells fit them, the narrow grid above that: 9 cells are 3 columns
	// (u09), 11 are 4 (u11); 10 can only be the narrow grid, 3 x 3 has nine places.
	if (cells > UI.SelectedButtons.size() && !UI.SelectedButtonsWide.empty()) {
		return UI.SelectedButtonsWide;
	}
	return UI.SelectedButtons;
}

/**
**  Draw mana bar of a unit at x,y.
**  Placed under icons on top-panel.
**
**  @param unit  Pointer to unit.
**  @param x     Screen X position of icon
**  @param y     Screen Y position of icon
*/
static void UiDrawManaBar(const CUnit &unit, int x, int y)
{
	// FIXME: add icon borders
	y += unit.Type->Icon.Icon->G->Height;
	Video.FillRectangleClip(ColorBlack, x, y + 3, unit.Type->Icon.Icon->G->Width, 4);

	if (unit.Stats->Variables[MANA_INDEX].Max) {
		int f = (100 * unit.Variable[MANA_INDEX].Value) / unit.Variable[MANA_INDEX].Max;
		f = (f * (unit.Type->Icon.Icon->G->Width)) / 100;
		Video.FillRectangleClip(ColorBlue, x + 1, y + 3 + 1, f, 2);
	}
}

/**
**  Tell if we can show the content.
**  verify each sub condition for that.
**
**  @param condition   condition to verify.
**  @param unit        unit that certain condition can refer.
**
**  @return            false if we can't show the content, else true.
*/
static bool CanShowContent(const ConditionPanel *condition, const CUnit &unit)
{
	if (!condition) {
		return true;
	}
	if ((condition->ShowOnlySelected && !unit.Selected)
	    || (unit.Player->Type == PlayerTypes::PlayerNeutral && condition->HideNeutral)
	    || (ThisPlayer->IsEnemy(unit) && !condition->ShowOpponent)
	    || ((ThisPlayer->IsAllied(unit) || unit.Player == ThisPlayer) && condition->HideAllied)) {
		return false;
	}
	if (!unit.Type->CheckUserBoolFlags(condition->BoolFlags)) {
		return false;
	}
	if (!condition->Variables.empty()) {
		for (unsigned int i = 0; i < UnitTypeVar.GetNumberVariable(); ++i) {
			const auto is_variable_compatible = [&](auto v) {
				if constexpr (std::is_same_v<ECondition, decltype(v)>) {
					if (v != ECondition::Ignore) {
						if ((v == ECondition::ShouldBeTrue) ^ unit.Variable[i].Enable) {
							return false;
						}
					}
				} else {
					if (v < 0) {
						// only show for less than -v%
						if (unit.Variable[i].Enable) {
							const int f = (100 * unit.Variable[i].Value) / unit.Variable[i].Max;
							if (f >= -v) {
								return false;
							}
						} else if (v != -1) {
							// special case: the condition "<1" should apply
							// to units that do not have the variable at all
							return false;
						}
					} else if (v > 0) {
						// only show for more than v%
						if (unit.Variable[i].Enable) {
							const int f = (100 * unit.Variable[i].Value) / unit.Variable[i].Max;
							if (f <= v) {
								return false;
							}
						} else {
							return false;
						}
					}
				}
				return true;
			};
			if (!std::visit(is_variable_compatible, condition->Variables[i])) {
				return false;
			}
		}
	}
	return true;
}

using UStrInt = std::variant<int, const char *>;

/**
**  Return the value corresponding.
**
**  @param unit   Unit.
**  @param index  Index of the variable.
**  @param e      Component of the variable.
**  @param t      Which var use (0:unit, 1:Type, 2:Stats)
**
**  @return       Value corresponding
*/
UStrInt GetComponent(const CUnit &unit, int index, EnumVariable e, int t)
{
	const CVariable *var = nullptr;

	Assert((unsigned int) index < UnitTypeVar.GetNumberVariable());

	switch (t) {
		case 0: // Unit:
			var = &unit.Variable[index];
			break;
		case 1: // Type:
			var = &unit.Type->MapDefaultStat.Variables[index];
			break;
		case 2: // Stats:
			var = &unit.Stats->Variables[index];
			break;
		default:
			DebugPrint("Bad value for GetComponent: t = %d", t);
			var = &unit.Variable[index];
			break;
	}

	switch (e) {
		case EnumVariable::Value: return var->Value;
		case EnumVariable::Max: return var->Max;
		case EnumVariable::Increase: return var->Increase;
		case EnumVariable::Diff: return var->Max - var->Value;
		case EnumVariable::Percent:
			Assert(unit.Variable[index].Max != 0);
			return 100 * var->Value / var->Max;
		case EnumVariable::Name:
			if (index == GIVERESOURCE_INDEX) {
				return DefaultResourceNames[unit.Type->GivesResource].c_str();
			} else if (index == CARRYRESOURCE_INDEX) {
				return DefaultResourceNames[unit.CurrentResource].c_str();
			} else {
				return UnitTypeVar.VariableNameLookup[index].data();
			}
	}
	return 0;
}

UStrInt GetComponent(const CUnitType &type, int index, EnumVariable e, int t)
{
	const CVariable *var = nullptr;

	Assert((unsigned int) index < UnitTypeVar.GetNumberVariable());

	switch (t) {
		case 0: // Unit:
			var = &type.Stats[ThisPlayer->Index].Variables[index];
			break;
		case 1: // Type:
			var = &type.MapDefaultStat.Variables[index];
			break;
		case 2: // Stats:
			var = &type.Stats[ThisPlayer->Index].Variables[index];
			break;
		default:
			DebugPrint("Bad value for GetComponent: t = %d", t);
			var = &type.Stats[ThisPlayer->Index].Variables[index];
			break;
	}
	switch (e) {
		case EnumVariable::Value: return var->Value;
		case EnumVariable::Max: return var->Max;
		case EnumVariable::Increase: return var->Increase;
		case EnumVariable::Diff: return var->Max - var->Value;
		case EnumVariable::Percent:
			Assert(type.Stats[ThisPlayer->Index].Variables[index].Max != 0);
			return 100 * var->Value / var->Max;
		case EnumVariable::Name:
			if (index == GIVERESOURCE_INDEX) {
				return DefaultResourceNames[type.GivesResource].c_str();
			} else {
				return UnitTypeVar.VariableNameLookup[index].data();
			}
	}
	return 0;
}

static void DrawUnitInfo_Training(const CUnit &unit)
{
	if (unit.Orders.size() == 1 || unit.Orders[1]->Action != UnitAction::Train) {
		if (!UI.SingleTrainingText.empty()) {
			CLabel label(*UI.SingleTrainingFont);
			label.Draw(UI.SingleTrainingTextX, UI.SingleTrainingTextY, UI.SingleTrainingText);
		}
		if (UI.SingleTrainingButton) {
			const COrder_Train &order = *static_cast<COrder_Train *>(unit.CurrentOrder());
			CIcon &icon = *order.GetUnitType().Icon.Icon;
			const unsigned int flags = (ButtonAreaUnderCursor == ButtonArea::Training && ButtonUnderCursor == 0) ?
									   (IconActive | (MouseButtons & LeftButton)) : 0;
			const PixelPos pos(UI.SingleTrainingButton->X, UI.SingleTrainingButton->Y);
			icon.DrawUnitIcon(*UI.SingleTrainingButton->Style, flags, pos, "", unit.RescuedFrom
				? GameSettings.Presets[unit.RescuedFrom->Index].PlayerColor
				: GameSettings.Presets[unit.Player->Index].PlayerColor);
		}
	} else {
		if (!UI.TrainingText.empty()) {
			CLabel label(*UI.TrainingFont);
			label.Draw(UI.TrainingTextX, UI.TrainingTextY, UI.TrainingText);
		}
		if (!UI.TrainingButtons.empty()) {
			for (size_t i = 0; i < unit.Orders.size()
				 && i < UI.TrainingButtons.size(); ++i) {
				if (unit.Orders[i]->Action == UnitAction::Train) {
					const COrder_Train &order = *static_cast<COrder_Train *>(unit.Orders[i].get());
					CIcon &icon = *order.GetUnitType().Icon.Icon;
					const int flag = (ButtonAreaUnderCursor == ButtonArea::Training
									  && static_cast<size_t>(ButtonUnderCursor) == i) ?
									 (IconActive | (MouseButtons & LeftButton)) : 0;
					const PixelPos pos(UI.TrainingButtons[i].X, UI.TrainingButtons[i].Y);
					icon.DrawUnitIcon(*UI.TrainingButtons[i].Style, flag, pos, "", unit.RescuedFrom
						? GameSettings.Presets[unit.RescuedFrom->Index].PlayerColor
						: GameSettings.Presets[unit.Player->Index].PlayerColor);
				}
			}
		}
	}
}

#ifdef USE_MNG
static bool DrawTypePortrait(const CUnitType &type)
{
	if (UI.SingleSelectedButton && !type.Portrait.Mngs.empty()) {
#ifdef DYNAMIC_LOAD
		if (!type.Sprite) {
			LoadUnitTypeSprite(const_cast<CUnitType &>(type));
		}
#endif
		type.Portrait.Mngs[type.Portrait.CurrMng]->Draw(
			UI.SingleSelectedButton->X, UI.SingleSelectedButton->Y);
		if (type.Portrait.Mngs[type.Portrait.CurrMng]->getIteration()
		    == type.Portrait.NumIterations) {
			type.Portrait.Mngs[type.Portrait.CurrMng]->Reset();
			// FIXME: should be configurable
			if (type.Portrait.CurrMng == 0 && type.Portrait.Mngs.size() > 1) {
				// choose only from before the talking portraits (from all, if no talking portrait offset is defined)
				type.Portrait.CurrMng = MyRand() % ((type.Portrait.Talking ? type.Portrait.Talking : type.Portrait.Mngs.size()) - 1) + 1;
				type.Portrait.NumIterations = 1;
			} else {
				type.Portrait.CurrMng = 0;
				type.Portrait.NumIterations = MyRand() % 16 + 1;
			}
		}
		return true;
	}
	return false;
}
#endif

static void DrawUnitInfo_portrait(const CUnit &unit)
{
	const CUnitType &type = *unit.Type;
#ifdef USE_MNG
	if (DrawTypePortrait(type)) {
		return;
	}
#endif
	if (UI.SingleSelectedButton) {
		const PixelPos pos(UI.SingleSelectedButton->X, UI.SingleSelectedButton->Y);
		const int flag = (ButtonAreaUnderCursor == ButtonArea::Selected && ButtonUnderCursor == 0) ?
						 (IconActive | (MouseButtons & LeftButton)) : 0;

		type.Icon.Icon->DrawSingleSelectionIcon(*UI.SingleSelectedButton->Style, flag, pos, "", unit);
	}
}

static bool DrawUnitInfo_single_selection(const CUnit &unit)
{
	switch (unit.CurrentAction()) {
		case UnitAction::Train:
		{ //  Building training units.
			DrawUnitInfo_Training(unit);
			return true;
		}
		case UnitAction::UpgradeTo:
		{ //  Building upgrading to better type.
			if (UI.UpgradingButton) {
				const COrder_UpgradeTo &order = *static_cast<COrder_UpgradeTo *>(unit.CurrentOrder());
				CIcon &icon = *order.GetUnitType().Icon.Icon;
				unsigned int flag = (ButtonAreaUnderCursor == ButtonArea::Upgrading
									 && ButtonUnderCursor == 0) ?
									(IconActive | (MouseButtons & LeftButton)) : 0;
				const PixelPos pos(UI.UpgradingButton->X, UI.UpgradingButton->Y);
				icon.DrawUnitIcon(*UI.UpgradingButton->Style, flag, pos, "", unit.RescuedFrom
					? GameSettings.Presets[unit.RescuedFrom->Index].PlayerColor
					: GameSettings.Presets[unit.Player->Index].PlayerColor);
			}
			return true;
		}
		case UnitAction::Research:
		{ //  Building research new technology.
			if (UI.ResearchingButton) {
				COrder_Research &order = *static_cast<COrder_Research *>(unit.CurrentOrder());
				CIcon &icon = *order.GetUpgrade().Icon;
				int flag = (ButtonAreaUnderCursor == ButtonArea::Researching
							&& ButtonUnderCursor == 0) ?
						   (IconActive | (MouseButtons & LeftButton)) : 0;
				PixelPos pos(UI.ResearchingButton->X, UI.ResearchingButton->Y);
				icon.DrawUnitIcon(*UI.ResearchingButton->Style, flag, pos, "", unit.RescuedFrom
					? GameSettings.Presets[unit.RescuedFrom->Index].PlayerColor
					: GameSettings.Presets[unit.Player->Index].PlayerColor);
			}
			return true;
		}
		default:
			return false;
	}
}

static void DrawUnitInfo_transporter(const CUnit &unit)
{
	size_t j = 0;

	for (const CUnit *uins : unit.InsideUnits) {
		if (!uins->Boarded || j >= UI.TransportingButtons.size()) {
			continue;
		}
		CIcon &icon = *uins->Type->Icon.Icon;
		int flag = (ButtonAreaUnderCursor == ButtonArea::Transporting && static_cast<size_t>(ButtonUnderCursor) == j) ?
				   (IconActive | (MouseButtons & LeftButton)) : 0;
		const PixelPos posIcon(UI.TransportingButtons[j].X, UI.TransportingButtons[j].Y);
		icon.DrawContainedIcon(*UI.TransportingButtons[j].Style, flag, posIcon, "", *uins);
		int lastOccupiedButton = j + uins->Type->BoardSize - 1;
		const PixelPos pos = PixelPos(UI.TransportingButtons[lastOccupiedButton].X, UI.TransportingButtons[lastOccupiedButton].Y);
		UiDrawLifeBar(*uins, pos.x, pos.y,
					  UI.TransportingButtons[lastOccupiedButton].Style->Width,
					  UI.TransportingButtons[lastOccupiedButton].Style->Height);
		if (!uins->Type->CanCastSpell.empty() && uins->Variable[MANA_INDEX].Max) {
			UiDrawManaBar(*uins, pos.x, pos.y);
		}
		if (ButtonAreaUnderCursor == ButtonArea::Transporting) {
			for (int sub_j = j; sub_j <= lastOccupiedButton; sub_j++) {
				if (ButtonUnderCursor == sub_j) {
					UI.StatusLine.Set(uins->Type->Name);
				}
			}
		}
		j = lastOccupiedButton + 1;
	}
}

/**
**  Draw the unit info into top-panel.
**
**  @param unit  Pointer to unit.
*/
static void DrawUnitInfo(CUnit &unit)
{
	UpdateUnitVariables(unit);
	for (auto &infoPanelContent : UI.InfoPanelContents) {
		if (CanShowContent(infoPanelContent->Condition.get(), unit)) {
			for (auto &content : infoPanelContent->Contents) {
				if (CanShowContent(content->Condition.get(), unit)) {
					content->Draw(unit, infoPanelContent->DefaultFont);
				}
			}
		}
	}

	Assert(unit.Type);
	const CUnitType &type = *unit.Type;

	// Draw IconUnit
	DrawUnitInfo_portrait(unit);

	if (unit.Player != ThisPlayer && !ThisPlayer->IsAllied(*unit.Player)) {
		return;
	}

	//  Show progress if they are selected.
	if (IsOnlySelected(unit)) {
		if (DrawUnitInfo_single_selection(unit)) {
			return;
		}
	}

	//  Transporting units.
	if (type.CanTransport() && unit.BoardCount && CurrentButtonLevel == unit.Type->ButtonLevelForTransporter) {
		DrawUnitInfo_transporter(unit);
		return;
	}
}

/*----------------------------------------------------------------------------
--  RESOURCES
----------------------------------------------------------------------------*/

/**
**  Draw the player resource in top line.
**
**  @todo FIXME : make DrawResources more configurable (format, font).
*/
CFont *ResourcesFont = nullptr;   /// optional larger font for the resource line (set from Lua)
CFont *ObjectivesFont = nullptr;  /// optional HD font for the in-game objectives text (set from Lua)
/// PREF-4 "Hide Objectives HUD Display" (option_preference_hide_objectives). The original
/// keeps this in Preferences and it suppresses the objectives block over the map -- the
/// objectives themselves stay reachable from the in-game menu. Set from Lua.
bool HideObjectivesHud = false;

void DrawResources()
{
	CFont &resFont = ResourcesFont ? *ResourcesFont : GetGameFont();
	CLabel label(resFont);

	// Draw all icons of resource.
	for (int i = 0; i < FreeWorkersCount; ++i) {
		if (UI.Resources[i].G) {
			// M666-3: the picture is made once at a fixed size (44 px, ArtBaker RESOURCE_ICON) and
			// the row is 13.5 base units high in the original (40 px at k = 3, 27 px at k = 2), so
			// it is drawn at the width the UI names (CResourceInfo::IconWidth) when that differs.
			const CGraphic &g = *UI.Resources[i].G;
			const int w = UI.Resources[i].IconWidth;
			if (w > 0 && g.Width > 0 && w != g.Width) {
				g.DrawFrameClipScaled(UI.Resources[i].IconFrame, UI.Resources[i].IconX,
				                      UI.Resources[i].IconY, w, w * g.Height / g.Width);
			} else {
				g.DrawFrameClip(UI.Resources[i].IconFrame, UI.Resources[i].IconX, UI.Resources[i].IconY);
			}
		}
	}
	for (int i = 0; i < MaxCosts; ++i) {
		if (UI.Resources[i].TextX != -1) {
			const int resourceAmount = ThisPlayer->Resources[i];

			if (ThisPlayer->MaxResources[i] != -1) {
				const int resAmount = ThisPlayer->StoredResources[i] + ThisPlayer->Resources[i];
				char tmp[256];
				snprintf(tmp, sizeof(tmp), "%d (%d)", resAmount, ThisPlayer->MaxResources[i] - ThisPlayer->StoredResources[i]);
				label.SetFont(GetSmallFont());

				label.Draw(UI.Resources[i].TextX, UI.Resources[i].TextY + 3, tmp);
			} else {
				label.SetFont(resourceAmount > 99999 ? GetSmallFont() : resFont);

				// Plain digits: the original prints the stock with no thousands separator (captures
				// o20_h1_start, o24_h1_farm: four digits in a row for gold and lumber). CLabel::Draw
				// with a number groups the digits by three, so the amount goes in as text.
				label.Draw(UI.Resources[i].TextX, UI.Resources[i].TextY + (resourceAmount > 99999) * 3,
				           std::to_string(resourceAmount));
			}
		}
	}
	if (UI.Resources[FoodCost].TextX != -1) {
		char tmp[256];
		snprintf(tmp, sizeof(tmp), "%d/%d", ThisPlayer->Demand, ThisPlayer->Supply);
		label.SetFont(resFont);
		if (ThisPlayer->Supply < ThisPlayer->Demand && ResourcesFont) {
			// HUD-FOOD-RED: with more food used than there is, the original prints the USED figure
			// red and leaves the stroke and the supply white: "1/0" at a 2960x1848 client, ink of
			// the "1" (204, 0, 0) at x 2001..2007, "/0" (255, 255, 255) from x 2015
			// (work/agent-zoom-rule/captures/c_k_2960x1848.png). The baked HD face takes no font
			// colour (it is white in indices no CFontColor reaches), so the reverse colour below
			// never showed on it: the figure is tinted instead.
			const int x = UI.Resources[FoodCost].TextX;
			const int y = UI.Resources[FoodCost].TextY;
			SetGlyphTint(204, 0, 0);
			const int w = label.Draw(x, y, std::to_string(ThisPlayer->Demand));
			SetGlyphTint(255, 255, 255);
			label.Draw(x + w, y, "/" + std::to_string(ThisPlayer->Supply));
		} else if (ThisPlayer->Supply < ThisPlayer->Demand) {
			label.DrawReverse(UI.Resources[FoodCost].TextX, UI.Resources[FoodCost].TextY, tmp);
		} else {
			label.Draw(UI.Resources[FoodCost].TextX, UI.Resources[FoodCost].TextY, tmp);
		}
	}
	if (UI.Resources[ScoreCost].TextX != -1) {
		const int score = ThisPlayer->Score;

		label.SetFont(score > 99999 ? GetSmallFont() : resFont);
		label.Draw(UI.Resources[ScoreCost].TextX, UI.Resources[ScoreCost].TextY + (score > 99999) * 3, score);
	}
	if (UI.Resources[FreeWorkersCount].TextX != -1) {
		int textX = UI.Resources[FreeWorkersCount].TextX;
		if (textX < 0) {
			// XXX: this is hacky, but what use is that bit otherwise
			if (ThisPlayer->GetFreeWorkers().empty()) {
				return;
			} else {
				textX = -textX;
			}
		}
		if (UI.Resources[FreeWorkersCount].G) {
			UI.Resources[FreeWorkersCount].G->DrawFrameClip(UI.Resources[FreeWorkersCount].IconFrame,
											 UI.Resources[FreeWorkersCount].IconX, UI.Resources[FreeWorkersCount].IconY);
		}
		label.SetFont(resFont);
		label.Draw(
			textX, UI.Resources[FreeWorkersCount].TextY, ThisPlayer->GetFreeWorkers().size());
	}
}

/*----------------------------------------------------------------------------
--  MESSAGE
----------------------------------------------------------------------------*/

#define MESSAGES_MAX  10                       /// How many can be displayed

static char MessagesEvent[MESSAGES_MAX][256];  /// Array of event messages
static Vec2i MessagesEventPos[MESSAGES_MAX];   /// coordinate of event
static int MessagesEventCount;                 /// Number of event messages
static int MessagesEventIndex;                 /// FIXME: docu
static int MaxMessagesCount = MESSAGES_MAX;

class MessagesDisplay
{
public:
	MessagesDisplay() : show(true)
	{
#ifdef DEBUG
		showBuilList = false;
#endif
		CleanMessages();
	}

	void UpdateMessages();
	void AddUniqueMessage(const char *s);
	void DrawMessages();
	void CleanMessages();
	bool AnyShown() const { return show && Preference.ShowMessages && MessagesCount > 0; }
	/// NG50: how tall the visible message stack currently is, in screen pixels. The Remastered puts
	/// its message band and its objectives overlay in almost the same slot (message rect y=22,
	/// objectives y=24 in the HUD builder's own space), so something has to give when both have
	/// content. What the ORIGINAL does there is not yet decoded -- see F-8 -- so WargusHD takes the
	/// one reading that cannot be wrong either way: two texts are never drawn on top of each other.
	int VisibleHeight() const
	{
		if (!AnyShown() || UI.MessageFont == nullptr) {
			return 0;
		}
		return std::max(0, MessagesCount * (UI.MessageFont->Height() + 1) - MessagesScrollY);
	}
	void ToggleShowMessages() { show = !show; }
#ifdef DEBUG
	void ToggleShowBuilListMessages() { showBuilList = !showBuilList; }
#endif

protected:
	void ShiftMessages();
	void AddMessage(const char *msg);
	bool CheckRepeatMessage(const char *msg);

private:
	char Messages[MESSAGES_MAX][256];         /// Array of messages
	int  MessagesCount;                       /// Number of messages
	int  MessagesScrollY;
	unsigned long MessagesFrameTimeout;       /// Frame to expire message
	bool show;
#ifdef DEBUG
	bool showBuilList;
#endif
};

/**
**  Shift messages array by one.
*/
void MessagesDisplay::ShiftMessages()
{
	if (MessagesCount) {
		--MessagesCount;
		for (int z = 0; z < MessagesCount; ++z) {
			strcpy_s(Messages[z], sizeof(Messages[z]), Messages[z + 1]);
		}
	}
}

/**
**  Update messages
**
**  @todo FIXME: make scroll speed configurable.
*/
void MessagesDisplay::UpdateMessages()
{
	if (!MessagesCount) {
		return;
	}

	// Scroll/remove old message line
	const unsigned long ticks = GetTicks();
	// MESSAGE-LINE-LIFE: the line is gone at once when its time is over. The scroll-out that
	// stood here took one step per drawn frame, so the life depended on the frame rate
	// (measured 7.8 s and more than 8 s on one device).
	if (MessagesFrameTimeout < ticks) {
		MessagesFrameTimeout = ticks + WC2_MESSAGE_LIFE_MS;
		MessagesScrollY = 0;
		ShiftMessages();
	}
}

/**
**  Draw message(s).
**
**  @todo FIXME: make message font configurable.
*/
void MessagesDisplay::DrawMessages()
{
	if (show && Preference.ShowMessages) {
		CLabel label(*UI.MessageFont);
#ifdef DEBUG
		if (showBuilList && ThisPlayer->Ai) {
			char buffer[256];
			int count = ThisPlayer->Ai->UnitTypeBuilt.size();
			// Draw message line(s)
			for (int z = 0; z < count; ++z) {
				if (z == 0) {
					PushClipping();
					SetClipping(UI.MapArea.X + 8, UI.MapArea.Y + 8,
								Video.Width - 1, Video.Height - 1);
				}

				snprintf(buffer, 256, "%s (%d/%d) Wait %lu [%d,%d]",
						 ThisPlayer->Ai->UnitTypeBuilt[z].Type->Name.c_str(),
						 ThisPlayer->Ai->UnitTypeBuilt[z].Made,
						 ThisPlayer->Ai->UnitTypeBuilt[z].Want,
						 ThisPlayer->Ai->UnitTypeBuilt[z].Wait,
						 ThisPlayer->Ai->UnitTypeBuilt[z].Pos.x,
						 ThisPlayer->Ai->UnitTypeBuilt[z].Pos.y);

				label.DrawClip(UI.MapArea.X + 8,
							   UI.MapArea.Y + 8 + z * (UI.MessageFont->Height() + 1),
							   buffer);

				if (z == 0) {
					PopClipping();
				}
			}
		} else {
#endif
			// background so the text is easier to read
			/*
			if (MessagesCount) {
				int textHeight = MessagesCount * (UI.MessageFont->Height() + 1);
				Uint32 color = Video.MapRGB(TheScreen->format, 38, 38, 78);
				Video.FillTransRectangleClip(color, UI.MapArea.X + 7, UI.MapArea.Y + 7,
											 UI.MapArea.EndX - UI.MapArea.X - 16,
											 textHeight - MessagesScrollY + 1, 0x80);

				Video.DrawRectangle(color, UI.MapArea.X + 6, UI.MapArea.Y + 6,
									UI.MapArea.EndX - UI.MapArea.X - 15,
									textHeight - MessagesScrollY + 2);
			}
			*/

			// M666-6: the original prints a message near the BOTTOM of the map view, not at its
			// top. Measured (work/agent-fix-hudrest/tools, HUDREST_READY section M666-6):
			//   k = 3, picture 2960x1848 (agent-orig-scenes e01, e02, e10): first ink x 546 =
			//          182 base units; capital rows 1697..1720 -> top 151 px above the bottom
			//   k = 2, client 1684x1031 (agent-hud o53, o54): first ink x 365 = 182.5 units;
			//          capital rows 930..945 -> top 101 px above the bottom
			// i.e. x = 182 units, capital top 50.4 units above the picture's bottom (151.2 / 100.8).
			// 182 is also the x of the message rect in the original's HUD builder (note at
			// RM_ApplyMessageBand, scripts/ui.lua).
			// One line: a new message takes the row of the one before it (e01 then e02 on the same
			// row; work/agent-orig-scenes/SCENES.md section 2 -- two captures, no capture shows two
			// lines). So the newest line is drawn, alone, and nothing scrolls.
			// faceCapTop: the HUD face's distance from a text position to the top of a capital,
			// 25.5 px of its sheet (ui/fonts/blq55_small.png) = 17 px at k = 3 (capture c668_c:
			// text drawn at y 84 has its capitals from row 101). Measure again if the face changes.
			const int hudStep = Video.HudCanvas.Step;
			const int faceCapTop = (int) std::lround(25.5 * Video.HudCanvas.Scale);
			const int msgX = Video.HudCanvas.X + 182 * hudStep + UI.MessageOffsetX;
			const int msgY = Video.HudCanvas.Y + Video.HudCanvas.H - (int) std::lround(50.4 * hudStep)
			                 - faceCapTop + UI.MessageOffsetY;
			if (MessagesCount > 0) {
				PushClipping();
				SetClipping(msgX, msgY, Video.Width - 1, Video.Height - 1);
				/*
				 * Due parallel drawing we have to force message copy due temp
				 * std::string(Messages[z]) creation because
				 * char * pointer may change during text drawing.
				 */
				// Yellow: the brightest ink of the original's line is (255, 204, 0) (e01, e02, e10,
				// o53). The baked face takes no font colour, so the glyphs are tinted (SetGlyphTint).
				SetGlyphTint(255, 204, 0);
				label.DrawClip(msgX, msgY, std::string(Messages[MessagesCount - 1]));
				SetGlyphTint(255, 255, 255);
				PopClipping();
			}
#ifdef DEBUG
		}
#endif

	}
}

/**
**  Adds message to the stack
**
**  @param msg  Message to add.
*/
void MessagesDisplay::AddMessage(const char *msg)
{
	unsigned long ticks = GetTicks();

	if (!MessagesCount) {
		MessagesFrameTimeout = ticks + WC2_MESSAGE_LIFE_MS;
	}

	if (MessagesCount == MaxMessagesCount) {
		// Out of space to store messages, can't scroll smoothly
		ShiftMessages();
		MessagesFrameTimeout = ticks + WC2_MESSAGE_LIFE_MS;
		MessagesScrollY = 0;
	}
	char *ptr;
	char *next;
	char *message = Messages[MessagesCount];
	// Split long message into lines
	if (strlen(msg) >= sizeof(Messages[0])) {
		strncpy(message, msg, sizeof(Messages[0]) - 1);
		ptr = message + sizeof(Messages[0]) - 1;
		*ptr-- = '\0';
		next = ptr + 1;
		while (ptr >= message) {
			if (*ptr == ' ') {
				*ptr = '\0';
				next = ptr + 1;
				break;
			}
			--ptr;
		}
		if (ptr < message) {
			ptr = next - 1;
		}
	} else {
		strcpy_s(message, sizeof(Messages[MessagesCount]), msg);
		next = ptr = message + strlen(message);
	}

	while (UI.MessageFont->Width(message) + 8 >= UI.MapArea.EndX - UI.MapArea.X) {
		while (1) {
			--ptr;
			if (*ptr == ' ') {
				*ptr = '\0';
				next = ptr + 1;
				break;
			} else if (ptr == message) {
				break;
			}
		}
		// No space found, wrap in the middle of a word
		if (ptr == message) {
			ptr = next - 1;
			while (UI.MessageFont->Width(message) + 8 >= UI.MapArea.EndX - UI.MapArea.X) {
				*--ptr = '\0';
			}
			next = ptr + 1;
			break;
		}
	}

	++MessagesCount;

	if (strlen(msg) != (size_t)(ptr - message)) {
		AddMessage(msg + (next - message));
	}
}

/**
**  Check if this message repeats
**
**  @param msg  Message to check.
**
**  @return     true to skip this message
*/
bool MessagesDisplay::CheckRepeatMessage(const char *msg)
{
	if (MessagesCount < 1) {
		return false;
	}
	size_t sz = strlen(msg);
	for (int i = 0; i < MessagesCount; i++) {
		const char *pMsg = Messages[i];
		if (!strncmp(msg, pMsg, sz)) {

			size_t msgSz = strlen(pMsg);
			int n = 0;

			if (msgSz == sz || sscanf(pMsg + sz, " (~<%d~>", &n) == 1) {
				// This exact message was printed already.
				// n now holds the previous repeat count.
				if (n < 10) {
					// 1. Shift all following messages down
					for (int z = i; z < MessagesCount - 1; ++z) {
						strcpy_s(Messages[z], sizeof(Messages[z]), Messages[z + 1]);
					}

					// Update the message and append it to the end with the
					// new repeat count
					char temp[256];
					snprintf(temp, sizeof(temp), "%s (~<%d~>%s)", msg, n + 1, n == 9 ? "+" : "");
					MessagesCount--;
					AddMessage(temp);
				} else {
					return true; // skip so many repeated messages
				}
			}
			return true;
		}
	}
	return false;
}

/**
**  Add a new message to display only if it differs from the preceding one.
*/
void MessagesDisplay::AddUniqueMessage(const char *s)
{
	if (!CheckRepeatMessage(s)) {
		AddMessage(s);
	}
}

/**
**  Clean up messages.
*/
void MessagesDisplay::CleanMessages()
{
	MessagesCount = 0;
	MessagesScrollY = 0;
	MessagesFrameTimeout = 0;

	MessagesEventCount = 0;
	MessagesEventIndex = 0;
}

static MessagesDisplay allmessages;

/**
**  Update messages
*/
void UpdateMessages()
{
	allmessages.UpdateMessages();
}

/**
**  Clean messages
*/
void CleanMessages()
{
	allmessages.CleanMessages();
}

/**
**  Draw messages
*/
void DrawMessages()
{
	allmessages.DrawMessages();
}

/**
**  True if any message is currently visible over the map.
*/
bool AreMessagesShown()
{
	return allmessages.AnyShown();
}

/**
**  Set message to display.
**
**  @param fmt  To be displayed in text overlay.
*/
void SetMessage(const char *fmt, ...)
{
	char temp[512];
	va_list va;

	va_start(va, fmt);
	vsnprintf(temp, sizeof(temp) - 1, fmt, va);
	temp[sizeof(temp) - 1] = '\0';
	va_end(va);
	allmessages.AddUniqueMessage(temp);
}

/**
**  Shift messages events array by one.
*/
void ShiftMessagesEvent()
{
	if (MessagesEventCount) {
		--MessagesEventCount;
		for (int z = 0; z < MessagesEventCount; ++z) {
			MessagesEventPos[z] = MessagesEventPos[z + 1];
			strcpy_s(MessagesEvent[z], sizeof(MessagesEvent[z]), MessagesEvent[z + 1]);
		}
	}
}

/**
**  Set message to display.
**
**  @param pos    Message pos map origin.
**  @param fmt  To be displayed in text overlay.
**
**  @note FIXME: vladi: I know this can be just separated func w/o msg but
**               it is handy to stick all in one call, someone?
*/
void SetMessageEvent(const Vec2i &pos, const char *fmt, ...)
{
	Assert(Map.Info.IsPointOnMap(pos));

	char temp[256];
	va_list va;

	va_start(va, fmt);
	vsnprintf(temp, sizeof(temp) - 1, fmt, va);
	temp[sizeof(temp) - 1] = '\0';
	va_end(va);
	allmessages.AddUniqueMessage(temp);

	if (MessagesEventCount == MaxMessagesCount) {
		ShiftMessagesEvent();
	}

	strcpy_s(MessagesEvent[MessagesEventCount], sizeof(MessagesEvent[MessagesEventCount]), temp);
	MessagesEventPos[MessagesEventCount] = pos;
	MessagesEventIndex = MessagesEventCount;
	++MessagesEventCount;
}

/**
**  Goto message origin.
*/
void CenterOnMessage()
{
	if (MessagesEventIndex >= MessagesEventCount) {
		MessagesEventIndex = 0;
	}
	if (MessagesEventCount == 0) {
		return;
	}
	const Vec2i &pos(MessagesEventPos[MessagesEventIndex]);
	UI.SelectedViewport->Center(Map.TilePosToMapPixelPos_Center(pos));
	SetMessage(_("~<Event: %s~>"), MessagesEvent[MessagesEventIndex]);
	++MessagesEventIndex;
}

void ToggleShowMessages()
{
	allmessages.ToggleShowMessages();
}

#ifdef DEBUG
void ToggleShowBuilListMessages()
{
	allmessages.ToggleShowBuilListMessages();
}
#endif

void SetMaxMessageCount(int newMax)
{
	MaxMessagesCount = std::min(MESSAGES_MAX, newMax);
	allmessages.CleanMessages();
	MessagesEventCount = 0;
	MessagesEventIndex = 0;
}

/*----------------------------------------------------------------------------
--  INFO PANEL
----------------------------------------------------------------------------*/

/**
**  Draw info panel background.
**
**  @param frame  frame nr. of the info panel background.
*/
static void DrawInfoPanelBackground(unsigned frame)
{
	if (UI.InfoPanel.G) {
		UI.InfoPanel.G->DrawFrame(frame, UI.InfoPanel.X, UI.InfoPanel.Y);
	}
}

static void InfoPanel_draw_no_selection()
{
	DrawInfoPanelBackground(0);
	if (UnitUnderCursor && UnitUnderCursor->IsVisible(*ThisPlayer)
		&& !UnitUnderCursor->Type->BoolFlag[ISNOTSELECTABLE_INDEX].value) {
		// FIXME: not correct for enemies units
		DrawUnitInfo(*UnitUnderCursor);
	} else {
#ifdef USE_MNG
		if (UI.DefaultUnitPortrait.size() > 0) {
			DrawTypePortrait(UnitTypeByIdent(UI.DefaultUnitPortrait));
		}
#endif
		if (Preference.ShowNoSelectionStats) {
			// FIXME: need some cool ideas for this.
			int x = UI.InfoPanel.X + 16;
			int y = UI.InfoPanel.Y + 8;

			CLabel label(GetGameFont());
			label.Draw(x, y, "Stratagus");
			y += 16;
			label.Draw(x, y,  _("Cycle:"));
			label.Draw(x + 48, y, GameCycle);
			label.Draw(x + 110, y, CyclesPerSecond);
			y += 20;

			const auto [nc, rc] = GetDefaultTextColors();
			for (int i = 0; i < PlayerMax - 1; ++i) {
				if (Players[i].Type != PlayerTypes::PlayerNobody) {
					if (ThisPlayer->IsAllied(Players[i])) {
						label.SetNormalColor(FontGreen);
					} else if (ThisPlayer->IsEnemy(Players[i])) {
						label.SetNormalColor(FontRed);
					} else {
						label.SetNormalColor(nc);
					}
					label.Draw(x + 15, y, i);

					Video.DrawRectangleClip(ColorWhite, x, y, 12, 12);
					Video.FillRectangleClip(PlayerColorsRGB[GameSettings.Presets[i].PlayerColor][0], x + 1, y + 1, 10, 10);

					label.Draw(x + 27, y, Players[i].Name);
					label.Draw(x + 117, y, Players[i].Score);
					y += 14;
				}
			}
		}
	}
}

static void InfoPanel_draw_single_selection(CUnit *selUnit)
{
	CUnit &unit = (selUnit ? *selUnit : *Selected[0]);
	int panelIndex;

	// FIXME: not correct for enemy's units
	if (unit.Player == ThisPlayer
		|| ThisPlayer->IsTeamed(unit)
		|| ThisPlayer->IsAllied(unit)
		|| ReplayRevealMap) {
		if (unit.Orders[0]->Action == UnitAction::Built
			|| unit.Orders[0]->Action == UnitAction::Research
			|| unit.Orders[0]->Action == UnitAction::UpgradeTo
			|| unit.Orders[0]->Action == UnitAction::Train) {
			panelIndex = 3;
		} else if (unit.Variable[MANA_INDEX].Max) {
			panelIndex = 2;
		} else {
			panelIndex = 1;
		}
	} else {
		panelIndex = 0;
	}
	DrawInfoPanelBackground(panelIndex);
	DrawUnitInfo(unit);
	if (ButtonAreaUnderCursor == ButtonArea::Selected && ButtonUnderCursor == 0) {
		UI.StatusLine.Set(unit.Type->Name);
	}
}

static void InfoPanel_draw_multiple_selection()
{
	//  If there are more units selected draw their pictures and a health bar
	DrawInfoPanelBackground(0);

#ifdef USE_MNG
	if (!Selected[0]->Type->Portrait.Mngs.empty()) {
		DrawUnitInfo_portrait(*Selected[0]);
	}
#endif

	// PANEL-A: one cell per unit, the cell of a dead unit left empty; 3 or 4 columns.
	const std::vector<CUnit *> &cells = GetSelectionCells();
	const std::vector<CUIButton> &buttons = GetSelectionCellButtons(cells.size());
	for (size_t i = 0; i != std::min(cells.size(), buttons.size()); ++i) {
		if (cells[i] == nullptr) {
			continue;
		}
		const CUnit &unit = *cells[i];
		const CIcon &icon = *unit.Type->Icon.Icon;
		const PixelPos pos(buttons[i].X, buttons[i].Y);
		if (UI.SelectedBarHeight > 0) {
			UiDrawCellLifeBar(unit, buttons[i].X, buttons[i].Y,
			                  buttons[i].Style->Width, buttons[i].Style->Height);
		}
		icon.DrawGroupSelectionIcon(*buttons[i].Style,
						  (ButtonAreaUnderCursor == ButtonArea::Selected && ButtonUnderCursor == (int)i) ?
						  (IconActive | (MouseButtons & LeftButton)) : 0,
						  pos, "", unit);
		if (UI.SelectedBarHeight <= 0) {
			UiDrawLifeBar(unit, buttons[i].X, buttons[i].Y,
						  buttons[i].Style->Width, buttons[i].Style->Height);
		}

		if (ButtonAreaUnderCursor == ButtonArea::Selected && ButtonUnderCursor == (int) i) {
			UI.StatusLine.Set(unit.Type->Name);
		}
	}
	// More units than cells: only where no grid holds them all. The original prints nothing in
	// the panel (no name, no number, no count: captures u02..u11).
	if (cells.size() > buttons.size() && UI.MaxSelectedFont) {

		const std::string number_str = "+" + std::to_string(cells.size() - buttons.size());
		CLabel(*UI.MaxSelectedFont).Draw(UI.MaxSelectedTextX, UI.MaxSelectedTextY, number_str.c_str());
	}
}

/**
**  Draw info panel.
**
**  Panel:
**    neutral      - neutral or opponent
**    normal       - not 1,3,4
**    magic unit   - magic units
**    construction - under construction
*/
void CInfoPanel::Draw()
{
	if (UnitUnderCursor && Selected.empty() && !UnitUnderCursor->Type->BoolFlag[ISNOTSELECTABLE_INDEX].value
		&& (ReplayRevealMap || UnitUnderCursor->IsVisible(*ThisPlayer))) {
			InfoPanel_draw_single_selection(UnitUnderCursor);
	} else {
		switch (Selected.size()) {
			 case 0: { InfoPanel_draw_no_selection(); break; }
			 case 1: { InfoPanel_draw_single_selection(nullptr); break; }
			 default: { InfoPanel_draw_multiple_selection(); break; }
		}
	}
}

/*----------------------------------------------------------------------------
--  TIMER
----------------------------------------------------------------------------*/

/**
**  Draw the timer
**
**  @todo FIXME : make DrawTimer more configurable (Pos, format).
*/
void DrawTimer()
{
	if (!GameTimer.Init) {
		return;
	}

	int sec = GameTimer.Cycles / CYCLES_PER_SECOND;
	UI.Timer.Draw(sec);
}

/**
**  The notice plate over the map view which the scripts fill while a network game waits
**  (Lua SetNetWaitPlate). The engine only draws what it was given.
*/
struct CNetWaitPlateLine {
	std::string Text;
	CFont *Font = nullptr;
	std::string Color;
	int X = 0;
	int Y = 0;
	int Align = 0; /// 0 left, 1 centre, 2 right
};

struct CNetWaitPlate {
	bool Shown = false;
	std::string File;              /// Frame picture, empty = the flat fill
	std::shared_ptr<CGraphic> G;
	int SrcBorder = 0;             /// Border of the picture in its own pixels
	int DstBorder = 0;             /// The same border on screen
	int X = 0;
	int Y = 0;
	int W = 0;
	int H = 0;
	std::vector<CNetWaitPlateLine> Lines;
};

static CNetWaitPlate NetWaitPlate;

void ClearNetWaitPlate()
{
	NetWaitPlate.Shown = false;
	NetWaitPlate.Lines.clear();
}

/// Two numbers of a table field {a, b}
static void NetWaitPlatePair(lua_State *l, int table, const char *name, int &a, int &b)
{
	lua_getfield(l, table, name);
	if (lua_istable(l, -1)) {
		a = LuaToNumber(l, -1, 1);
		b = LuaToNumber(l, -1, 2);
	}
	lua_pop(l, 1);
}

/// A string of a table field, empty when absent
static std::string NetWaitPlateString(lua_State *l, int table, const char *name)
{
	std::string value;

	lua_getfield(l, table, name);
	if (lua_isstring(l, -1)) {
		value = lua_tostring(l, -1);
	}
	lua_pop(l, 1);
	return value;
}

/**
**  Show or hide the plate.
**
**  SetNetWaitPlate(nil) hides it. Otherwise one table, all positions in screen pixels:
**    Pos = {x, y}, Size = {w, h},
**    Image = "<frame picture>", Border = {<in the picture>, <on screen>},
**    Lines = { {Text = "...", Font = "<ident>", Color = "<font colour>", Pos = {x, y},
**               Align = "left" | "center" | "right"}, ... }
*/
int CclSetNetWaitPlate(lua_State *l)
{
	LuaCheckArgs(l, 1);
	if (!lua_istable(l, 1)) {
		ClearNetWaitPlate();
		return 0;
	}
	CNetWaitPlate &plate = NetWaitPlate;

	NetWaitPlatePair(l, 1, "Pos", plate.X, plate.Y);
	NetWaitPlatePair(l, 1, "Size", plate.W, plate.H);
	NetWaitPlatePair(l, 1, "Border", plate.SrcBorder, plate.DstBorder);
	const std::string file = NetWaitPlateString(l, 1, "Image");
	if (file != plate.File || (!file.empty() && !plate.G)) {
		plate.File = file;
		plate.G.reset();
		if (!file.empty()) {
			plate.G = CGraphic::New(file);
			plate.G->Load();
		}
	}
	plate.Lines.clear();
	lua_getfield(l, 1, "Lines");
	if (lua_istable(l, -1)) {
		const int count = lua_rawlen(l, -1);
		for (int i = 1; i <= count; ++i) {
			lua_rawgeti(l, -1, i);
			if (lua_istable(l, -1)) {
				const int line = lua_gettop(l);
				CNetWaitPlateLine entry;
				entry.Text = NetWaitPlateString(l, line, "Text");
				entry.Font = CFont::Get(NetWaitPlateString(l, line, "Font"));
				entry.Color = NetWaitPlateString(l, line, "Color");
				NetWaitPlatePair(l, line, "Pos", entry.X, entry.Y);
				const std::string align = NetWaitPlateString(l, line, "Align");
				entry.Align = (align == "center") ? 1 : (align == "right") ? 2 : 0;
				if (entry.Font && !entry.Text.empty()) {
					plate.Lines.push_back(entry);
				}
			}
			lua_pop(l, 1);
		}
	}
	lua_pop(l, 1);
	plate.Shown = plate.W > 0 && plate.H > 0;
	return 0;
}

/**
**  Draw the plate of a waiting network game, if the scripts show one.
*/
void DrawNetWaitPlate()
{
	const CNetWaitPlate &plate = NetWaitPlate;

	if (!plate.Shown || !GameRunning || Editor.Running == EditorEditing) {
		return;
	}
	if (plate.G) {
		const SDL_Rect dst = {plate.X, plate.Y, plate.W, plate.H};
		plate.G->DrawNineSliceScaled(dst, plate.SrcBorder, plate.DstBorder);
	} else {
		// No frame picture: the flat panel the command tooltip falls back to (ui/popup.cpp).
		Video.FillTransRectangle(ColorBlack, plate.X, plate.Y, plate.W, plate.H, 84);
		Video.DrawRectangle(ColorWhite, plate.X, plate.Y, plate.W, plate.H);
	}
	for (const CNetWaitPlateLine &line : plate.Lines) {
		const std::string &color = line.Color.empty() ? std::string("white") : line.Color;
		const CLabel label(*line.Font, color, color);
		if (line.Align == 1) {
			label.DrawCentered(line.X, line.Y, line.Text);
		} else if (line.Align == 2) {
			label.Draw(line.X - line.Font->Width(line.Text), line.Y, line.Text);
		} else {
			label.Draw(line.X, line.Y, line.Text);
		}
	}
}

/**
**  Draw the current mission objectives as a persistent text block overlaid on
**  the top-left of the world area.
**
**  The objectives are read from the Lua global `Objectives` (an array of
**  strings), the same source the in-game objectives menu reads. The block is
**  placed at the Remastered HUD objectives rectangle, authored at 3840x2160 and
**  mapped to the screen with the aspect-preserving uniform fit the rest of the
**  HD HUD uses. Nothing is drawn when there are no objectives.
*/
void DrawObjectives()
{
	if (!GameRunning || Editor.Running == EditorEditing || HideObjectivesHud) {
		return;
	}

	// Read the Lua global `Objectives` (array of strings) into a newline-joined
	// block, exactly as the objectives menu builds it.
	std::string text;
	lua_getglobal(Lua, "Objectives");
	if (lua_istable(Lua, -1)) {
		const size_t n = lua_rawlen(Lua, -1);
		for (size_t i = 1; i <= n; ++i) {
			lua_rawgeti(Lua, -1, i);
			if (lua_isstring(Lua, -1)) {
				if (!text.empty()) {
					text += '\n';
				}
				text += lua_tostring(Lua, -1);
			}
			lua_pop(Lua, 1);
		}
	}
	lua_pop(Lua, 1);

	if (text.empty()) {
		return;
	}

	// M666-6: the objectives rect in base units of the 640x480 layout: (184, 24, 230, 16 per line),
	// the literals of the original's HUD builder (note at RM_ApplyMessageBand, scripts/ui.lua).
	// Checked on work/agent-hud/captures o20, o41, o53 (k = 2): first ink x 369 = 184.5 units,
	// capitals from row 55 = 27.5 units = the rect's top + 3.5, lines 32 px = 16 units apart.
	// In the art space used below that is (828, 108, 1035, ...); it was (849, 126, 540, 111), read
	// off a 3440x1440 capture: 15.5 px right and 18.5 px low at k = 3, and 360 px wide against 690.
	// No capture of the original shows objectives at k = 3; the numbers above are k = 2 ones.
	const SDL_Rect logical = {828, 108, 1035, 72};
	// WORLD-ZOOM-RULE: the block belongs to the HUD, so it takes the HUD's own scale and the
	// picture's top-left (CVideo::HudCanvas; SPEC_WORLD_ZOOM_RULE.md R4: the HUD is anchored at the
	// picture's top-left and scaled by the step). The 16:9 fit this site used before
	// (ComputeUniformScale over the canvas rect) is the same number only on a 16:9 picture whose
	// height is a multiple of 480; on 2960x1848 it gave 0.7708 against the HUD's 0.6667.
	// CVideo::ComputeHudCanvas owns the rule; it is read here, never recomputed (NG81).
	const double hudScale = Video.HudCanvas.Scale;
	const SDL_Rect r = {Video.HudCanvas.X + (int) std::lround(logical.x * hudScale),
	                    Video.HudCanvas.Y + (int) std::lround(logical.y * hudScale),
	                    (int) std::lround(logical.w * hudScale),
	                    (int) std::lround(logical.h * hudScale)};

	// WargusHD: the classic small bitmap font never scales with resolution, so on an HD screen the
	// objectives text is tiny. Use the Lua-set HD font (SetObjectivesFont "remaster_small") when
	// present, matching how the resource line uses ResourcesFont; fall back to the classic font.
	CFont &font = ObjectivesFont ? *ObjectivesFont : GetSmallFont();
	CLabel label(font);
	// NG36: line pitch and the clip test both come from the original-client capture, not from the
	// font metrics. In ingame-human01 the two objective lines sit 47 capture px apart inside a 74 px
	// box (rows 0-23 and 47-71), i.e. a pitch of 47/74 of the authored box height, with the second
	// line's glyphs ending well inside the box. Using font.Height() for both the step and the clip
	// overstates a line's visible extent -- it counts ascender and descender the string never uses --
	// so the second line was being dropped even though the original clearly shows it.
	// M666-6: 16 base units a line (see the rect above): 48 px at k = 3, 32 px at k = 2.
	const int capturePitch = 16 * Video.HudCanvas.Step;
	// NG84: the authored pitch IS the ground truth -- 48 capture px in both `ingame-orc-hud-a` and
	// `ingame-human01-clean`, i.e. 72 logical, which is what 47/74 of the box reproduces. The old
	// `max(font.Height() + 2, capturePitch)` existed because the HD font sheet was baked ~27% too
	// small, so its cell was the smaller of the two and the max simply chose the authored pitch.
	// With the sheet scaled to the original's size the cell (leading included) is now TALLER than
	// the authored pitch, and the max started spacing the lines 84 logical apart instead of 72.
	// Take the authored pitch; the ink is 37 logical, so lines cannot collide at 72.
	const int lineHeight = capturePitch;
	// NG50: while a message is on screen the objectives start below it (see VisibleHeight above).
	// WargusHD-side rule, disclosed: the original's own rects put the two within two units of each
	// other, and which one yields is still undecoded (F-8).
	// M666-6: messages are at the bottom of the view now (MessagesDisplay::DrawMessages), so the
	// block no longer moves when one comes up (it jumped 11..34 px, captures c666_b, c668_m).
	// The capitals start 3.5 units under the rect's top (o20, o41); the face puts them faceCapTop
	// under the text position (25.5 px of its sheet, 17 px at k = 3 -- capture c668_c).
	const int faceCapTop = (int) std::lround(25.5 * hudScale);
	int y = r.y + (int) std::lround(3.5 * Video.HudCanvas.Step) - faceCapTop;
	std::string_view line;
	// NG36: there is no height to clip against. The 111 in `logical` above is what TWO lines
	// happened to occupy in the one capture it was measured from (the Oracle records that rect as
	// MEASURED_MIRRORED, E2, from ingame-human01-clean -- Hillsbrad, a two-objective mission), and
	// clipping to it silently dropped the third and fourth objectives of the five missions that
	// have them. The original computes this box's height at RUNTIME from the objectives list
	// itself (exe_ui_layout_formulas, ingame_hud/objectives#2, VA 5344283: x/y/w are literals,
	// h_expr is the objectives extent), so it has no constant capacity to reproduce. The WIDTH
	// stays authored -- the original's is literal too, which is why wrapping at r.w is right.
	for (unsigned int i = 1; (line = GetLineFont(i, text, r.w, &font)).length(); ++i) {
		label.DrawClip(r.x, y, line);
		y += lineHeight;
	}
}

/**
**  Update the timer
*/
void UpdateTimer()
{
	if (GameTimer.Running) {
		if (GameTimer.Increasing) {
			GameTimer.Cycles += GameCycle - GameTimer.LastUpdate;
		} else {
			GameTimer.Cycles -= GameCycle - GameTimer.LastUpdate;
			GameTimer.Cycles = std::max(GameTimer.Cycles, 0l);
		}
		GameTimer.LastUpdate = GameCycle;
	}
}

//@}
