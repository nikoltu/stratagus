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
/**@name mouse.cpp - The mouse handling. */
//
//      (c) Copyright 1998-2015 by Lutz Sammer, Jimmy Salmon and Andrettin
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

#define ICON_SIZE_X (UI.ButtonPanel.Buttons[0].Style->Width)
#define ICON_SIZE_Y (UI.ButtonPanel.Buttons[0].Style->Height)

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "action/action_build.h"
#include "action/action_train.h"
#include "actions.h"
#include "commands.h"
#include "cursor.h"
#include "font.h"
#include "group_move.h"
#include "interface.h"
#include "map.h"
#include "menus.h"
#include "minimap.h"
#include "missile.h"
#include "network.h"
#include "player.h"
#include "sound.h"
#include "spells.h"
#include "stratagus.h"
#include "tileset.h"
#include "translate.h"
#include "ui.h"
#include "unit.h"
#include "unit_find.h"
#include "unit_manager.h"
#include "unitsound.h"
#include "unittype.h"
#include "video.h"
#include "widgets.h"
#include "../video/hd_classic_hud.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <guisan/actionevent.hpp>
#include <stdexcept>

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

int MouseButtons;                            /// Current pressed mouse buttons

int KeyModifiers;                            /// Current keyboard modifiers

CUnit *UnitUnderCursor;                      /// Unit under cursor
std::optional<ButtonArea> ButtonAreaUnderCursor; /// Button area under cursor
int ButtonUnderCursor = -1;                  /// Button under cursor
int OldButtonUnderCursor = -1;               /// Button under cursor
bool GameMenuButtonClicked;                  /// Menu button was clicked
bool GameDiplomacyButtonClicked;             /// Diplomacy button was clicked
bool LeaveStops;                             /// Mouse leaves windows stops scroll

ECursorOn CursorOn = ECursorOn::Unknown;        /// Cursor on field

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/
static void HandlePieMenuMouseSelection();

CUnit *GetUnitUnderCursor()
{
	return UnitUnderCursor;
}

/**
**  Cancel building cursor mode.
*/
void CancelBuildingMode()
{
	CursorBuilding = nullptr;
	ResetBuildingPreview();
	UI.StatusLine.Clear();
	UI.StatusLine.ClearCosts();
	CurrentButtonLevel = 0;
	UI.ButtonPanel.Update();
}

static bool CanBuildOnArea(const CUnit &unit, const Vec2i &pos)
{
	for (int j = 0; j < unit.Type->TileHeight; ++j) {
		for (int i = 0; i < unit.Type->TileWidth; ++i) {
			const Vec2i tempPos(i, j);
			if (!Map.Field(pos + tempPos)->playerInfo.IsExplored(*ThisPlayer)) {
				return false;
			}
		}
	}
	return true;
}

/**
**  The goals of a group order of the player to one tile.
**
**  Land units of one tile that move tile by tile are sent as a group (GroupMoveGoals over
**  these units alone); so are the ships that rest on every second tile (GroupMoveGridGoals
**  over the ships alone, transports left out); every other unit gets the target itself.
**
**  @param units   the ordered units.
**  @param target  the ordered tile.
**
**  @return        one goal per unit, in the order of @p units.
*/
std::vector<Vec2i> GroupMoveGoalsForUnits(const std::vector<CUnit *> &units, const Vec2i &target)
{
	std::vector<Vec2i> goals(units.size(), target);

	if (!Preference.FormationMovement) {
		return goals;
	}
	std::vector<size_t> members;
	std::vector<Vec2i> starts;
	std::vector<size_t> ships;
	std::vector<Vec2i> shipStarts;

	for (size_t i = 0; i != units.size(); ++i) {
		const CUnit *unit = units[i];
		if (unit == nullptr || unit->Removed || !unit->CanMove()) {
			continue;
		}
		const CUnitType &type = *unit->Type;
		if (type.TileWidth != 1 || type.TileHeight != 1) {
			continue;
		}
		if (type.MoveType == EMovement::Land && type.GridStep == 1) {
			members.push_back(i);
			starts.push_back(unit->tilePos);
		} else if (type.MoveType == EMovement::Naval && type.GridStep == ShipGridStep && !type.CanTransport()) {
			// a transport may be sent onto the coast, which is off the grid
			ships.push_back(i);
			shipStarts.push_back(unit->tilePos);
		}
	}
	const std::vector<Vec2i> memberGoals = GroupMoveGoals(starts, target, Map.Info.MapWidth, Map.Info.MapHeight);

	for (size_t i = 0; i != members.size(); ++i) {
		goals[members[i]] = memberGoals[i];
	}
	// One ship alone keeps the target: where it stops at a tile off the grid is the move's matter.
	if (ships.size() >= 2) {
		const std::vector<Vec2i> shipGoals =
			GroupMoveGridGoals(shipStarts, target, Map.Info.MapWidth, Map.Info.MapHeight, ShipGridStep);

		for (size_t i = 0; i != ships.size(); ++i) {
			goals[ships[i]] = shipGoals[i];
		}
	}
	return goals;
}

static void DoRightButton_ForForeignUnit(CUnit *dest)
{
	CUnit &unit = *Selected[0];

	if (unit.Player->Index != PlayerNumNeutral || dest == nullptr
		|| !(dest->Player == ThisPlayer || dest->IsTeamed(*ThisPlayer))) {
		return;
	}
	// tell to go and harvest from a unit
	const int res = unit.Type->GivesResource;

	if (res
		&& dest->Type->BoolFlag[HARVESTER_INDEX].value
		&& dest->Type->ResInfo[res]
		&& dest->ResourcesHeld < dest->Type->ResInfo[res]->ResourceCapacity
		&& unit.Type->BoolFlag[CANHARVEST_INDEX].value) {
		unit.Blink = 4;
		//  Right mouse with SHIFT appends command to old commands.
		const EFlushMode flush = !(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;
		SendCommandResource(*dest, unit, flush);
	}
}

static bool DoRightButton_Transporter(CUnit &unit, CUnit *dest, EFlushMode flush, int &acknowledged)
{
	//  Enter transporters ?
	if (dest == nullptr) {
		return false;
	}
	// dest is the transporter
	if (dest->Type->CanTransport()) {
		// Let the transporter move to the unit. And QUEUE!!!
		if (dest->CanMove() && CanTransport(*dest, unit)) {
			DebugPrint("Send command follow\n");
			// is flush value correct ?
			if (!acknowledged) {
				PlayUnitSound(unit, EUnitVoice::Acknowledging);
				acknowledged = 1;
			}
			SendCommandFollow(*dest, unit, EFlushMode::Off);
		}
		// FIXME : manage correctly production units.
		if (!unit.CanMove() || CanTransport(*dest, unit)) {
			dest->Blink = 4;
			DebugPrint("Board transporter\n");
			if (!acknowledged) {
				PlayUnitSound(unit, EUnitVoice::Acknowledging);
				acknowledged = 1;
			}
			SendCommandBoard(unit, *dest, flush);
			return true;
		}
	}
	//  unit is the transporter
	//  FIXME : Make it more configurable ? NumSelect == 1, lua option
	if (CanTransport(unit, *dest)) {
		// Let the transporter move to the unit. And QUEUE!!!
		if (unit.CanMove()) {
			DebugPrint("Send command follow\n");
			// is flush value correct ?
			if (!acknowledged) {
				PlayUnitSound(unit, EUnitVoice::Acknowledging);
				acknowledged = 1;
			}
			SendCommandFollow(unit, *dest, EFlushMode::Off);
		} else if (!dest->CanMove()) {
			DebugPrint("Want to transport but no unit can move\n");
			return true;
		}
		dest->Blink = 4;
		DebugPrint("Board transporter\n");
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Acknowledging);
			acknowledged = 1;
		}
		SendCommandBoard(*dest, unit, flush);
		return true;
	}
	return false;
}

static bool
DoRightButton_Harvest_Unit(CUnit &unit, CUnit &dest, EFlushMode flush, int &acknowledged)
{
	// Return a loaded harvester to deposit
	if (unit.ResourcesHeld > 0
		&& dest.Type->CanStore[unit.CurrentResource]
		&& (dest.Player == unit.Player
			|| (dest.Player->IsAllied(*unit.Player) && unit.Player->IsAllied(*dest.Player)))) {
		dest.Blink = 4;
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Acknowledging);
			acknowledged = 1;
		}
		SendCommandReturnGoods(unit, &dest, flush);
		return true;
	}
	// Go and harvest from a unit
	const int res = dest.Type->GivesResource;
	const CUnitType &type = *unit.Type;
	if (res && type.ResInfo[res] && dest.Type->BoolFlag[CANHARVEST_INDEX].value
	    && (dest.Player == unit.Player || dest.Player->Index == PlayerNumNeutral)) {
		if (unit.CurrentResource != res
		    || unit.ResourcesHeld < type.ResInfo[res]->ResourceCapacity) {
			dest.Blink = 4;
			SendCommandResource(unit, dest, flush);
			if (!acknowledged) {
				PlayUnitSound(unit, EUnitVoice::Harvesting);
				acknowledged = 1;
			}
			return true;
		} else {
			CUnit *depot = FindDeposit(unit, 1000, unit.CurrentResource);
			if (depot) {
				dest.Blink = 4;
				if (!acknowledged) {
					PlayUnitSound(unit, EUnitVoice::Acknowledging);
					acknowledged = 1;
				}
				SendCommandReturnGoods(unit, depot, flush);
				return true;
			}
		}
	}
	return false;
}

static bool
DoRightButton_Harvest_Pos(CUnit &unit, const Vec2i &pos, EFlushMode flush, int &acknowledged)
{
	if (!Map.Field(pos)->playerInfo.IsExplored(*unit.Player)) {
		return false;
	}
	const CUnitType &type = *unit.Type;
	// FIXME: support harvesting more types of terrain.
	for (int res = 0; res < MaxCosts; ++res) {
		if (type.ResInfo[res]
			&& type.ResInfo[res]->TerrainHarvester
			&& Map.Field(pos)->IsTerrainResourceOnMap(res)) {
			if (unit.CurrentResource != res || unit.ResourcesHeld < type.ResInfo[res]->ResourceCapacity) {
				SendCommandResourceLoc(unit, pos, flush);
				if (!acknowledged) {
					PlayUnitSound(unit, EUnitVoice::Harvesting);
					acknowledged = 1;
				}
				return true;
			} else {
				CUnit *depot = FindDeposit(unit, 1000, unit.CurrentResource);
				if (depot) {
					if (!acknowledged) {
						PlayUnitSound(unit, EUnitVoice::Acknowledging);
						acknowledged = 1;
					}
					SendCommandReturnGoods(unit, depot, flush);
					return true;
				}
			}
		}
	}
	return false;
}

static bool DoRightButton_Worker(
	CUnit &unit, CUnit *dest, const Vec2i &pos, const Vec2i &goalPos, EFlushMode flush, int &acknowledged)
{
	const CUnitType &type = *unit.Type;

	// Go and repair
	if (type.RepairRange && dest != nullptr
		&& dest->Type->RepairHP
		&& dest->Variable[HP_INDEX].Value < dest->Variable[HP_INDEX].Max
		&& (dest->Player == unit.Player || unit.IsAllied(*dest))) {
		dest->Blink = 4;
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Repairing);
			acknowledged = 1;
		}
		SendCommandRepair(unit, pos, dest, flush);
		return true;
	}
	// Harvest
	if (type.BoolFlag[HARVESTER_INDEX].value) {
		if (dest != nullptr) {
			if (DoRightButton_Harvest_Unit(unit, *dest, flush, acknowledged)) {
				return true;
			}
		} else {
			if (DoRightButton_Harvest_Pos(unit, pos, flush, acknowledged)) {
				return true;
			}
		}
	}
	// Follow another unit
	if (UnitUnderCursor != nullptr && dest != nullptr && dest != &unit
		&& (dest->Player == unit.Player || unit.IsAllied(*dest) || dest->Player->Index == PlayerNumNeutral)) {
		dest->Blink = 4;
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Acknowledging);
			acknowledged = 1;
		}
		if (dest->Type->CanMove() == false && !dest->Type->BoolFlag[TELEPORTER_INDEX].value) {
			SendCommandMove(unit, pos, flush);
		} else {
			SendCommandFollow(unit, *dest, flush);
		}
		return true;
	}
	// Move
	if (!acknowledged) {
		PlayUnitSound(unit, EUnitVoice::Acknowledging);
		acknowledged = 1;
	}
	SendCommandMove(unit, goalPos, flush);
	return true;
}

static bool DoRightButton_AttackUnit(
	CUnit &unit, CUnit &dest, const Vec2i &pos, EFlushMode flush, int &acknowledged)
{
	const CUnitType &type = *unit.Type;
	const EMouseAction action = type.MouseAction;

	if (action == EMouseAction::SpellCast || unit.IsEnemy(dest)) {
		dest.Blink = 4;
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Attack);
			acknowledged = 1;
		}
		if (action == EMouseAction::SpellCast) {
			// This is for demolition squads and such
			size_t spellnum;
			const size_t spellCount = std::min(SpellTypeTable.size(), type.CanCastSpell.size());
			for (spellnum = 0; spellnum < spellCount && !type.CanCastSpell[spellnum]; spellnum++) {
			}
			if (spellnum == spellCount) {
				ErrorPrint("Warning: right-click spell action requested for unit type '%s', "
				           "but it has no castable spell slots (CanCastSpell=%zu, spells=%zu)\n",
				           type.Ident.c_str(),
				           type.CanCastSpell.size(),
				           SpellTypeTable.size());
				return false;
			}
			SendCommandSpellCast(unit, pos, &dest, spellnum, flush);
		} else {
			if (CanTarget(type, *dest.Type)) {
				SendCommandAttack(unit, pos, &dest, flush);
			} else { // No valid target
				SendCommandAttack(unit, pos, nullptr, flush);
			}
		}
		return true;
	}
	if ((dest.Player == unit.Player || unit.IsAllied(dest) || dest.Player->Index == PlayerNumNeutral) && &dest != &unit) {
		dest.Blink = 4;
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Acknowledging);
			acknowledged = 1;
		}
		if (!dest.Type->CanMove() && !dest.Type->BoolFlag[TELEPORTER_INDEX].value) {
			SendCommandMove(unit, pos, flush);
		} else {
			SendCommandFollow(unit, dest, flush);
		}
		return true;
	}
	return false;
}

static void DoRightButton_Attack(
	CUnit &unit, CUnit *dest, const Vec2i &pos, const Vec2i &goalPos, EFlushMode flush, int &acknowledged)
{
	if (dest != nullptr && unit.CurrentAction() != UnitAction::Built) {
		if (DoRightButton_AttackUnit(unit, *dest, pos, flush, acknowledged)) {
			return;
		}
	}
	if (Map.WallOnMap(pos)) {
		// XXX: hardcoded for humans and orcs
		if (PlayerRaces.Name[unit.Player->Race] == "human" && Map.OrcWallOnMap(pos)) {
			SendCommandAttack(unit, pos, nullptr, flush);
			return;
		}
		if (PlayerRaces.Name[unit.Player->Race] == "orc" && Map.HumanWallOnMap(pos)) {
			SendCommandAttack(unit, pos, nullptr, flush);
			return;
		}
	}
	// empty space: the unit goes to its own tile of the group
	if ((KeyModifiers & ModifierControl)) {
		if (RightButtonAttacks) {
			SendCommandMove(unit, goalPos, flush);
			if (!acknowledged) {
				PlayUnitSound(unit, EUnitVoice::Acknowledging);
				acknowledged = 1;
			}
		} else {
			if (!acknowledged) {
				PlayUnitSound(unit, EUnitVoice::Attack);
				acknowledged = 1;
			}
			SendCommandAttack(unit, goalPos, nullptr, flush);
		}
	} else {
		if (RightButtonAttacks) {
			if (!acknowledged) {
				PlayUnitSound(unit, EUnitVoice::Attack);
				acknowledged = 1;
			}
			SendCommandAttack(unit, goalPos, nullptr, flush);
		} else {
			// Note: move is correct here, right default is move
			if (!acknowledged) {
				PlayUnitSound(unit, EUnitVoice::Acknowledging);
				acknowledged = 1;
			}
			SendCommandMove(unit, goalPos, flush);
		}
	}
	// FIXME: ALT-RIGHT-CLICK, move but fight back if attacked.
}

static bool DoRightButton_Follow(CUnit &unit, CUnit &dest, EFlushMode flush, int &acknowledged)
{
	if (dest.Player == unit.Player || unit.IsAllied(dest) || dest.Player->Index == PlayerNumNeutral) {
		dest.Blink = 4;
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Acknowledging);
			acknowledged = 1;
		}
		if (dest.Type->CanMove() == false && !dest.Type->BoolFlag[TELEPORTER_INDEX].value) {
			SendCommandMove(unit, dest.tilePos, flush);
		} else {
			SendCommandFollow(unit, dest, flush);
		}
		return true;
	}
	return false;
}

static bool
DoRightButton_Harvest_Reverse(CUnit &unit, CUnit &dest, EFlushMode flush, int &acknowledged)
{
	const CUnitType &type = *unit.Type;

	// tell to return a loaded harvester to deposit
	if (dest.ResourcesHeld > 0
		&& type.CanStore[dest.CurrentResource]
		&& dest.Player == unit.Player) {
		dest.Blink = 4;
		SendCommandReturnGoods(dest, &unit, flush);
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Acknowledging);
			acknowledged = 1;
		}
		return true;
	}
	// tell to go and harvest from a building
	const int res = type.GivesResource;
	if (res
		&& dest.Type->ResInfo[res]
		&& dest.ResourcesHeld < dest.Type->ResInfo[res]->ResourceCapacity
		&& type.BoolFlag[CANHARVEST_INDEX].value
		&& dest.Player == unit.Player) {
		unit.Blink = 4;
		SendCommandResource(dest, unit, flush);
		return true;
	}
	return false;
}

static bool DoRightButton_NewOrder(
	CUnit &unit, CUnit *dest, const Vec2i &pos, EFlushMode flush, int &acknowledged)
{
	// Go and harvest from a unit
	if (dest != nullptr && dest->Type->GivesResource && dest->Type->BoolFlag[CANHARVEST_INDEX].value
		&& (dest->Player == unit.Player || dest->Player->Index == PlayerNumNeutral)) {
		dest->Blink = 4;
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Acknowledging);
			acknowledged = 1;
		}
		SendCommandResource(unit, *dest, flush);
		return true;
	}
	// FIXME: support harvesting more types of terrain.
	const CMapField &mf = *Map.Field(pos);
	if (mf.playerInfo.IsExplored(*unit.Player) && mf.IsTerrainResourceOnMap()) {
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Acknowledging);
			acknowledged = 1;
		}
		SendCommandResourceLoc(unit, pos, flush);
		return true;
	}
	return false;
}

static void DoRightButton_ForSelectedUnit(
	CUnit &unit, CUnit *dest, const Vec2i &pos, const Vec2i &goalPos, int &acknowledged)
{
	// don't self targeting.
	if (dest == &unit) {
		return;
	}
	if (unit.Removed) {
		return;
	}
	const CUnitType &type = *unit.Type;
	const EMouseAction action = type.MouseAction;
	//  Right mouse with SHIFT appends command to old commands.
	const EFlushMode flush = !(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;

	//  Control + alt click - ground attack
	if ((KeyModifiers & ModifierControl) && (KeyModifiers & ModifierAlt)) {
		if (unit.Type->BoolFlag[GROUNDATTACK_INDEX].value) {
			if (!acknowledged) {
				PlayUnitSound(unit, EUnitVoice::Attack);
				acknowledged = 1;
			}
			SendCommandAttackGround(unit, pos, flush);
			return;
		}
	}
	//  Control + right click on unit is follow anything.
	if ((KeyModifiers & ModifierControl) && dest) {
		dest->Blink = 4;
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Acknowledging);
			acknowledged = 1;
		}
		SendCommandFollow(unit, *dest, flush);
		return;
	}

	//  Alt + right click on unit is defend anything.
	if ((KeyModifiers & ModifierAlt) && dest) {
		dest->Blink = 4;
		if (!acknowledged) {
			PlayUnitSound(unit, EUnitVoice::Acknowledging);
			acknowledged = 1;
		}
		SendCommandDefend(unit, *dest, flush);
		return;
	}

	if (DoRightButton_Transporter(unit, dest, flush, acknowledged)) {
		return;
	}

	//  Handle resource workers.
	if (action == EMouseAction::Harvest) {
		DoRightButton_Worker(unit, dest, pos, goalPos, flush, acknowledged);
		return;
	}

	//  Fighters
	if (action == EMouseAction::SpellCast || action == EMouseAction::Attack) {
		DoRightButton_Attack(unit, dest, pos, goalPos, flush, acknowledged);
		return;
	}

	// FIXME: attack/follow/board ...
	if (dest != nullptr && (action == EMouseAction::Move || action == EMouseAction::Sail)) {
		if (DoRightButton_Follow(unit, *dest, flush, acknowledged)) {
			return;
		}
	}

	// Manage harvester from the destination side.
	if (dest != nullptr && dest->Type->BoolFlag[HARVESTER_INDEX].value) {
		if (DoRightButton_Harvest_Reverse(unit, *dest, flush, acknowledged)) {
			return;
		}
	}

	// Manage new order.
	if (!unit.CanMove()) {
		if (DoRightButton_NewOrder(unit, dest, pos, flush, acknowledged)) {
			return;
		}
	}
	if (!acknowledged) {
		PlayUnitSound(unit, EUnitVoice::Acknowledging);
		acknowledged = 1;
	}
	SendCommandMove(unit, goalPos, flush);
}

/**
**  Called when right button is pressed
**
**  @param mapPixelPos  map position in pixels.
*/
void DoRightButton(const PixelPos &mapPixelPos)
{
	// No unit selected
	if (Selected.empty()) {
		return;
	}
	const Vec2i pos = Map.MapPixelPosToTilePos(mapPixelPos);
	CUnit *dest;            // unit under the cursor if any.

	if (UnitUnderCursor != nullptr && !UnitUnderCursor->Type->BoolFlag[DECORATION_INDEX].value) {
		dest = UnitUnderCursor;
	} else {
		dest = nullptr;
	}

	//  Unit selected isn't owned by the player.
	//  You can't select your own units + foreign unit(s)
	//  except if it is neutral and it is a resource.
	if (!CanSelectMultipleUnits(*Selected[0]->Player)) {
		DoRightButton_ForForeignUnit(dest);
		return;
	}

	if (dest != nullptr && dest->Type->CanTransport()) {
		for (const CUnit *unit : Selected) {
			if (CanTransport(*dest, *unit)) {
				// We are clicking on a transporter. We have to:
				// 1) Flush the transporters orders.
				// 2) Tell the transporter to follow the units. We have to queue all
				//    these follow orders, so the transport will go and pick them up
				// 3) Tell all selected land units to go board the transporter.

				// Here we flush the order queue
				SendCommandStopUnit(*dest);
				break;
			}
		}
	}

	int acknowledged = 0; // to play sound

	// An order to a tile sends the units as a group, each one to its own goal; an order on a
	// unit is the same for all of them.
	const std::vector<Vec2i> goals =
		dest == nullptr ? GroupMoveGoalsForUnits(Selected, pos) : std::vector<Vec2i>(Selected.size(), pos);

	for (size_t i = 0; i < Selected.size(); ++i) {
		Assert(Selected[i]);
		CUnit &unit = *Selected[i];

		DoRightButton_ForSelectedUnit(unit, dest, pos, goals[i], acknowledged);
	}
	ShowOrdersCount = GameCycle + Preference.ShowOrders * CYCLES_PER_SECOND;
}

/**
**  Check if the mouse is on a button
**
**  @param screenPos  screen coordinate.
**
**  @return True if mouse is on the button, False otherwise.
*/
bool CUIButton::Contains(const PixelPos &screenPos) const
{
	Assert(this->Style);

	return this->X <= screenPos.x && screenPos.x < this->X + this->Style->Width
		   && this->Y <= screenPos.y && screenPos.y < this->Y + this->Style->Height;
}

/**
**  Set flag on which area is the cursor.
**
**  @param screenPos  screen position.
*/
static void HandleMouseOn(const PixelPos screenPos)
{
	MouseScrollState = ScrollNone;
	ButtonAreaUnderCursor.reset();
	ButtonUnderCursor = -1;

	if (IsDemoMode()) {
		// If we are in "demo mode", do nothing.
		return;
	}

	// BigMapMode is the mode which show only the map (without panel, minimap)
	if (BigMapMode) {
		CursorOn = ECursorOn::Map;
		//  Scrolling Region Handling.
		HandleMouseScrollArea(screenPos);
		return;
	}

	// Classic view: in the box only the Menu button, the minimap and the map view take the pointer;
	// outside the box nothing does. The Menu button is user button 0 (the Game Menu), at the
	// classic button's place.
	if (ClassicHudActive()) {
		if (!UI.UserButtons.empty() && UI.UserButtons[0].Button.X != -1
			&& ClassicMenuButtonContains(screenPos)) {
			ButtonAreaUnderCursor = ButtonArea::User;
			ButtonUnderCursor = 0;
			CursorOn = ECursorOn::Button;
			return;
		}
		// User button 1 is "Pause", at the classic Pause button's place.
		if (UI.UserButtons.size() > 1 && UI.UserButtons[1].Button.X != -1
			&& ClassicPauseButtonContains(screenPos.x, screenPos.y)) {
			ButtonAreaUnderCursor = ButtonArea::User;
			ButtonUnderCursor = 1;
			CursorOn = ECursorOn::Button;
			return;
		}
		// The command card: the engine's buttons in the classic cells.
		const int commandCell = ClassicCommandCellAt(screenPos.x, screenPos.y);
		if (commandCell >= 0) {
			bool allowed = false;
			bool marked = false;
			if (ClassicCommandButtonShown(commandCell, allowed, marked) && allowed) {
				ButtonAreaUnderCursor = ButtonArea::Button;
				ButtonUnderCursor = commandCell;
				CursorOn = ECursorOn::Button;
			} else {
				CursorOn = ECursorOn::Unknown;
			}
			return;
		}
		// A unit of the selected group in its cell of the info panel: the cell's number, as in
		// the Remastered panel.
		const int groupCell = Selected.size() > 1 ? ClassicGroupCellAt(screenPos.x, screenPos.y) : -1;
		if (groupCell >= 0) {
			ButtonAreaUnderCursor = ButtonArea::Selected;
			ButtonUnderCursor = groupCell;
			CursorOn = ECursorOn::Button;
			return;
		}
		// A passenger of the selected transport.
		const int passenger = ClassicPassengerCellAt(screenPos.x, screenPos.y);
		if (passenger >= 0 && passenger < (int) UI.TransportingButtons.size()) {
			ButtonAreaUnderCursor = ButtonArea::Transporting;
			ButtonUnderCursor = passenger;
			CursorOn = ECursorOn::Button;
			return;
		}
		if (UI.Minimap.Contains(screenPos)) {
			CursorOn = ECursorOn::Minimap;
			return;
		}
		CViewport *vp = UI.MapArea.Contains(screenPos) ? GetViewport(screenPos) : nullptr;
		if (vp == nullptr) {
			CursorOn = ECursorOn::Unknown;
			return;
		}
		UI.MouseViewport = vp;
		CursorOn = ECursorOn::Map;
		HandleMouseScrollArea(screenPos);
		return;
	}

	//  Handle buttons
	if (!IsNetworkGame()) {
		if (UI.MenuButton.X != -1) {
			if (UI.MenuButton.Contains(screenPos)) {
				ButtonAreaUnderCursor = ButtonArea::Menu;
				ButtonUnderCursor = ButtonUnderMenu;
				CursorOn = ECursorOn::Button;
				return;
			}
		}
	} else {
		if (UI.NetworkMenuButton.X != -1) {
			if (UI.NetworkMenuButton.Contains(screenPos)) {
				ButtonAreaUnderCursor = ButtonArea::Menu;
				ButtonUnderCursor = ButtonUnderNetworkMenu;
				CursorOn = ECursorOn::Button;
				return;
			}
		}
		if (UI.NetworkDiplomacyButton.X != -1) {
			if (UI.NetworkDiplomacyButton.Contains(screenPos)) {
				ButtonAreaUnderCursor = ButtonArea::Menu;
				ButtonUnderCursor = ButtonUnderNetworkDiplomacy;
				CursorOn = ECursorOn::Button;
				return;
			}
		}
	}
	if (UI.Resources[FreeWorkersCount].TextX != -1) {
		int x, y;
		CGraphic* g = UI.Resources[FreeWorkersCount].G.get();
		if (g && (x = UI.Resources[FreeWorkersCount].IconX) && (y = UI.Resources[FreeWorkersCount].IconY)) {
			int textX = UI.Resources[FreeWorkersCount].TextX;
			if (textX > 0 || !ThisPlayer->GetFreeWorkers().empty()) {
				if (screenPos > PixelPos(x, y) && screenPos < PixelPos(x + g->getWidth(), y + g->getHeight())) {
					ButtonAreaUnderCursor = ButtonArea::Menu;
					ButtonUnderCursor = ButtonUnderFreeWorkers;
					CursorOn = ECursorOn::Button;
					return;
				}
			}
		}
	}
	for (size_t i = 0; i < UI.UserButtons.size(); ++i) {
		const CUIUserButton &button = UI.UserButtons[i];

		if (button.Button.X != -1) {
			if (button.Button.Contains(screenPos)) {
				ButtonAreaUnderCursor = ButtonArea::User;
				ButtonUnderCursor = i;
				CursorOn = ECursorOn::Button;
				return;
			}
		}
	}
	const size_t buttonCount = UI.ButtonPanel.Buttons.size();
	for (unsigned int j = 0; j < buttonCount; ++j) {
		if (UI.ButtonPanel.Buttons[j].Contains(screenPos)) {
			ButtonAreaUnderCursor = ButtonArea::Button;
			if (!CurrentButtons.empty() && CurrentButtons[j].Pos != -1) {
				ButtonUnderCursor = j;
				CursorOn = ECursorOn::Button;
				return;
			}
		}
	}
	if (!Selected.empty()) {
		if (Selected.size() == 1 && Selected[0]->Type->CanTransport() && Selected[0]->BoardCount) {
			const size_t size = UI.TransportingButtons.size();

			for (size_t i = std::min<size_t>(Selected[0]->BoardCount, size); i != 0;) {
				--i;
				if (UI.TransportingButtons[i].Contains(screenPos)) {
					ButtonAreaUnderCursor = ButtonArea::Transporting;
					ButtonUnderCursor = i;
					CursorOn = ECursorOn::Button;
					return;
				}
			}
		}
		if (Selected.size() == 1) {
			if (Selected[0]->CurrentAction() == UnitAction::Train) {
				if (Selected[0]->Orders.size() == 1) {
					if (UI.SingleTrainingButton->Contains(screenPos)) {
						ButtonAreaUnderCursor = ButtonArea::Training;
						ButtonUnderCursor = 0;
						CursorOn = ECursorOn::Button;
						return;
					}
				} else {
					const size_t size = UI.TrainingButtons.size();

					for (size_t i = std::min(Selected[0]->Orders.size(), size); i != 0;) {
						--i;
						if (Selected[0]->Orders[i]->Action == UnitAction::Train
							&& UI.TrainingButtons[i].Contains(screenPos)) {
							ButtonAreaUnderCursor = ButtonArea::Training;
							ButtonUnderCursor = i;
							CursorOn = ECursorOn::Button;
							return;
						}
					}
				}
			} else if (Selected[0]->CurrentAction() == UnitAction::UpgradeTo) {
				if (UI.UpgradingButton->Contains(screenPos)) {
					ButtonAreaUnderCursor = ButtonArea::Upgrading;
					ButtonUnderCursor = 0;
					CursorOn = ECursorOn::Button;
					return;
				}
			} else if (Selected[0]->CurrentAction() == UnitAction::Research) {
				if (UI.ResearchingButton->Contains(screenPos)) {
					ButtonAreaUnderCursor = ButtonArea::Researching;
					ButtonUnderCursor = 0;
					CursorOn = ECursorOn::Button;
					return;
				}
			}
		}
		if (Selected.size() == 1) {
			if (UI.SingleSelectedButton && UI.SingleSelectedButton->Contains(screenPos)) {
				ButtonAreaUnderCursor = ButtonArea::Selected;
				ButtonUnderCursor = 0;
				CursorOn = ECursorOn::Button;
				return;
			}
		} else {
			// PANEL-A: the cells as they are drawn (3 or 4 columns); an empty cell is no button.
			// ButtonUnderCursor is the CELL's number.
			const std::vector<CUnit *> &cells = GetSelectionCells();
			const std::vector<CUIButton> &cellButtons = GetSelectionCellButtons(cells.size());

			for (size_t i = std::min(cells.size(), cellButtons.size()); i != 0;) {
				--i;
				if (cells[i] != nullptr && cellButtons[i].Contains(screenPos)) {
					ButtonAreaUnderCursor = ButtonArea::Selected;
					ButtonUnderCursor = i;
					CursorOn = ECursorOn::Button;
					return;
				}
			}
		}
	}

	//  Minimap
	if (UI.Minimap.Contains(screenPos)) {
		CursorOn = ECursorOn::Minimap;
		return;
	}

	//  On UI graphic
	bool on_ui = false;
	const size_t size = UI.Fillers.size();
	for (unsigned int j = 0; j < size; ++j) {
		if (UI.Fillers[j].OnGraphic(screenPos.x, screenPos.y)) {
			on_ui = true;
			break;
		}
	}

	//  Map
	if (!on_ui && UI.MapArea.Contains(screenPos)) {
		CViewport *vp = GetViewport(screenPos);
		Assert(vp);
		// NG77: `CursorOn == Map` is an invariant meaning UI.MouseViewport is valid --
		// UIHandleMouseMove dereferences it on exactly that basis (the IsInsideMapArea test).
		// The Assert above says so, but a release build asserts nothing, and GetViewport returns
		// null whenever no viewport covers the point: the viewports are not built yet (a pointer
		// event reaching the game loop while StartMap is still bringing the map up), or the point
		// falls in a gap between UI.MapArea and the viewport rects. That crashed the app with
		// SIGSEGV at faultAddr 0. Keep the invariant instead: no viewport, no Map.
		if (vp == nullptr) {
			CursorOn = ECursorOn::Unknown;
		} else {
			// viewport changed
			if (UI.MouseViewport != vp) {
				UI.MouseViewport = vp;
				DebugPrint("current viewport changed to %ld.\n",
				           static_cast<long int>(vp - UI.Viewports));
			}

			// Note cursor on map can be in scroll area
			CursorOn = ECursorOn::Map;
		}
	} else {
		CursorOn = ECursorOn::Unknown;
	}

	//  Scrolling Region Handling.
	HandleMouseScrollArea(screenPos);
}

/**
**  Handle cursor exits the game window (only for some videomodes)
**  @todo FIXME: make it so that the game is partially 'paused'.
**         Game should run (for network play), but not react on or show
**         interactive events.
*/
void HandleMouseExit()
{
	// Disabled
	if (!LeaveStops) {
		return;
	}
	// Denote cursor not on anything in window (used?)
	CursorOn = ECursorOn::Unknown;

	// Prevent scrolling while out of focus (on other applications) */
	KeyScrollState = MouseScrollState = ScrollNone;

	// Show hour-glass (to denote to the user, the game is waiting)
	// FIXME: couldn't define a hour-glass that easily, so used pointer
	CursorScreenPos.x = Video.Width / 2;
	CursorScreenPos.y = Video.Height / 2;
	GameCursor = UI.Point.Cursor;
}

/**
**  A touch ended while the pointer stood on a HUD button: nothing is under a finger any more,
**  so the button is no longer lit and its popup is no longer drawn (the button panel draws
**  both from ButtonAreaUnderCursor / ButtonUnderCursor). The cursor itself stays where it is:
**  in game it is drawn and it drives build placement. The next finger-down sends a motion and
**  HandleMouseOn sets the state again.
*/
void UIClearButtonHover()
{
	if (CursorOn != ECursorOn::Button) {
		return;
	}
	ButtonAreaUnderCursor.reset();
	ButtonUnderCursor = -1;
	CursorOn = ECursorOn::Unknown;
}

/**
**  Restrict mouse cursor to viewport.
*/
void RestrictCursorToViewport()
{
	UI.SelectedViewport->Restrict(CursorScreenPos.x, CursorScreenPos.y);
	UI.MouseWarpPos = CursorStartScreenPos = CursorScreenPos;
	CursorOn = ECursorOn::Map;
}

/**
**  Restrict mouse cursor to minimap
*/
void RestrictCursorToMinimap()
{
	clamp(&CursorScreenPos.x, UI.Minimap.X, UI.Minimap.X + UI.Minimap.W - 1);
	clamp(&CursorScreenPos.y, UI.Minimap.Y, UI.Minimap.Y + UI.Minimap.H - 1);

	UI.MouseWarpPos = CursorStartScreenPos = CursorScreenPos;
	CursorOn = ECursorOn::Minimap;
}

//.............................................................................
//  Touch routes (owner's rule). The original has no touch input.

/**
**  TOUCH-R2: a hold on the map has given (or is about to give) an order. May the same finger
**  go on to queue waypoints? Only when that hold is a plain right-click order of own units:
**  not while a target or a building place is being chosen (there the hold cancels), not on
**  the HUD, the minimap or a scroll edge, not for an observer, not while paused.
**  Read for the pointer's present place, so the caller moves the pointer first.
*/
bool UITouchOrderChainAllowed()
{
	return GameRunning && !GameObserve && !GamePaused && !GameEstablishing
		   && CursorState == CursorStates::Point && CursorBuilding == nullptr
		   && CursorOn == ECursorOn::Map && UI.MouseViewport != nullptr
		   && UI.MouseViewport->IsInsideMapArea(CursorScreenPos)
		   && !Selected.empty() && CanSelectMultipleUnits(*Selected[0]->Player);
}

/**
**  TOUCH-R5: may a tap-then-drag add its box to the selection? Only where a drag draws a
**  selection box at all: in pointer mode. An empty selection is allowed (the box then
**  simply selects).
*/
bool UITouchAddBoxAllowed()
{
	return GameRunning && !GameEstablishing
		   && CursorState == CursorStates::Point && CursorBuilding == nullptr;
}

// TOUCH-ADD-BOX: a click on empty ground clears the selection (the original's behaviour),
// and the first tap of a tap-then-drag is such a click. What it cleared is kept here for a
// second so that the box of the pair is added to it, as TOUCH-R5 says.
static std::vector<int> TouchClearedUnits;       /// unit numbers of the own selection just cleared
static unsigned long TouchClearedTicks = 0;      /// when

static void TouchRememberClearedSelection()
{
	TouchClearedUnits.clear();
	if (Selected.empty() || Selected[0]->Player != ThisPlayer) {
		return;
	}
	for (const CUnit *unit : Selected) {
		TouchClearedUnits.push_back(UnitNumber(*unit));
	}
	TouchClearedTicks = GetTicks();
}

void UITouchRestoreClearedSelection()
{
	if (TouchClearedUnits.empty()) {
		return;
	}
	if (Selected.empty() && GetTicks() - TouchClearedTicks <= 1000) {
		for (const int number : TouchClearedUnits) {
			CUnit &unit = UnitManager->GetSlotUnit(number);
			if (!unit.IsAliveOnMap() || unit.Player != ThisPlayer) {
				continue;
			}
			if (Selected.empty()) {
				SelectSingleUnit(unit);
			} else {
				SelectUnit(unit);
			}
		}
		if (!Selected.empty()) {
			NetworkSendSelection(&Selected[0], Selected.size());
			SelectionChanged();
		}
	}
	TouchClearedUnits.clear();
}

// TOUCH-R7 (option B): the press of a finger in placement mode only takes hold of the
// preview; what it was is known at the release.
static bool TouchPlacePressed = false;   /// left button went down on the map in placement mode
static bool TouchPlaceOnPreview = false; /// ... on the preview itself
static bool TouchPlaceDragged = false;   /// the pointer moved before the release: a drag, not a tap
static PixelPos TouchPlaceGrab(0, 0);    /// finger minus the preview's top-left corner, in map pixels; kept while dragging

/**
**  TOUCH-R7: top-left tile of the preview for a finger at a screen point that holds the
**  building at TouchPlaceGrab. Owner's rule: the finger marks the middle of the building,
**  and a drag keeps the grabbed point. The corner is rounded to the nearest tile; the
**  footprint is kept inside the map.
**
**  @param vp         viewport the finger is on.
**  @param screenPos  the finger.
*/
static Vec2i TouchPlacementTile(const CViewport &vp, const PixelPos &screenPos)
{
	const PixelPos corner = vp.ScreenToMapPixelPos(screenPos) - TouchPlaceGrab;
	int tx = static_cast<int>(std::floor(static_cast<double>(corner.x) / PixelTileSize.x + 0.5));
	int ty = static_cast<int>(std::floor(static_cast<double>(corner.y) / PixelTileSize.y + 0.5));

	tx = std::max(0, std::min(tx, static_cast<int>(Map.Info.MapWidth) - CursorBuilding->TileWidth));
	ty = std::max(0, std::min(ty, static_cast<int>(Map.Info.MapHeight) - CursorBuilding->TileHeight));
	return Vec2i(tx, ty);
}

/**
**  Use the mouse to scroll the map
**
**  @param pos  Screen position.
*/
static void MouseScrollMap(const PixelPos &pos)
{
	const int speed = (KeyModifiers & ModifierControl) ? UI.MouseScrollSpeedControl : UI.MouseScrollSpeedDefault;
	const PixelDiff diff(pos - CursorStartScreenPos);

	UI.MouseViewport->Set(UI.MouseViewport->MapPos, UI.MouseViewport->Offset + speed * diff);
	UI.MouseWarpPos = CursorStartScreenPos;
}

/**
**  Handle movement of the cursor.
**
**  @param cursorPos  Screen X position.
*/
void UIHandleMouseMove(const PixelPos &cursorPos)
{
	ECursorOn OldCursorOn;

	if (IsDemoMode()) {
		// If we are in "demo mode", exit now.
		void ActionDraw();
		ActionDraw();
		return;
	}

	if (TouchPlacePressed) {
		// TOUCH-R7: a tap sends its press and release with no pointer motion between them
		// (video/sdl.cpp FlushPendingTouchClick); any motion while pressed makes it a drag.
		TouchPlaceDragged = true;
	}

	OldCursorOn = CursorOn;
	//  Selecting units.
	if (CursorState == CursorStates::Rectangle) {
		// Restrict cursor to viewport.
		UI.SelectedViewport->Restrict(CursorScreenPos.x, CursorScreenPos.y);
		UI.MouseWarpPos = CursorScreenPos;
		return;
	}

	//  Move map.
	if (GameCursor == UI.Scroll.Cursor) {
		MouseScrollMap(cursorPos);
		return;
	}

	UnitUnderCursor = nullptr;
	GameCursor = UI.Point.Cursor;  // Reset
	HandleMouseOn(cursorPos);

	//  Make the piemenu "follow" the mouse
	if (CursorState == CursorStates::PieMenu && CursorOn == ECursorOn::Map) {
		clamp(&CursorStartScreenPos.x, CursorScreenPos.x - UI.PieMenu.X[2], CursorScreenPos.x + UI.PieMenu.X[2]);
		clamp(&CursorStartScreenPos.y, CursorScreenPos.y - UI.PieMenu.Y[4], CursorScreenPos.y + UI.PieMenu.Y[4]);
		return;
	}

	// Restrict mouse to minimap when dragging
	if (OldCursorOn == ECursorOn::Minimap && CursorOn != ECursorOn::Minimap && (MouseButtons & LeftButton)) {
		const Vec2i cursorPos = UI.Minimap.ScreenToTilePos(CursorScreenPos);

		RestrictCursorToMinimap();
		UI.SelectedViewport->Center(Map.TilePosToMapPixelPos_Center(cursorPos));
		return;
	}

	//  User may be draging with button pressed.
	if (GameMenuButtonClicked || GameDiplomacyButtonClicked) {
		return;
	} else {
		if (ranges::any_of(UI.UserButtons,
		                   [](const CUIUserButton &button) { return button.Clicked; })) {
			return;
		}
	}

	// This is forbidden for unexplored and not visible space
	// FIXME: This must done new, moving units, scrolling...
	if (CursorOn == ECursorOn::Map && UI.MouseViewport->IsInsideMapArea(CursorScreenPos)) {
		const CViewport &vp = *UI.MouseViewport;
		const Vec2i tilePos = vp.ScreenToTilePos(cursorPos);

		if (TouchPlacePressed && CursorBuilding && (MouseButtons & LeftButton)) {
			// TOUCH-R7: the preview goes with the dragging finger, held at the grabbed point.
			SetBuildingPreviewTile(vp, TouchPlacementTile(vp, cursorPos));
		}

		try {
			if (CursorBuilding && (MouseButtons & LeftButton) && Selected.at(0)
				&& (KeyModifiers & (ModifierAlt | ModifierShift))) {
				const CUnit &unit = *Selected[0];
				const Vec2i tilePos = UI.MouseViewport->ScreenToTilePos(CursorScreenPos);
				bool explored = CanBuildOnArea(*Selected[0], tilePos);

				// We now need to check if there are another build commands on this build spot
				bool buildable = true;
				for (auto& order : unit.Orders) {
					if (order->Action == UnitAction::Build) {
						COrder_Build &build = dynamic_cast<COrder_Build &>(*order);
						if (tilePos.x >= build.GetGoalPos().x
							&& tilePos.x < build.GetGoalPos().x + build.GetUnitType().TileWidth
							&& tilePos.y >= build.GetGoalPos().y
							&& tilePos.y < build.GetGoalPos().y + build.GetUnitType().TileHeight) {
							buildable = false;
							break;
						}
					}
				}

				// 0 Test build, don't really build
				if (CanBuildUnitType(Selected[0], *CursorBuilding, tilePos, 0) && buildable && (explored || ReplayRevealMap)) {
					const EFlushMode flush =
						!(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;
					for (CUnit *unit : Selected) {
						SendCommandBuildBuilding(*unit, tilePos, *CursorBuilding, flush);
					}
					if (!(KeyModifiers & (ModifierAlt | ModifierShift))) {
						CancelBuildingMode();
					}
				}
			}
		} catch (const std::out_of_range &oor) {
			DebugPrint("Selected is empty: %s\n", oor.what());
		}
		if (Preference.ShowNameDelay) {
			ShowNameDelay = GameCycle + Preference.ShowNameDelay;
			ShowNameTime = GameCycle + Preference.ShowNameDelay + Preference.ShowNameTime;
		}

		bool show = ReplayRevealMap;
		if (show == false) {
			CMapField &mf = *Map.Field(tilePos);
			for (int i = 0; i < PlayerMax; ++i) {
				if (mf.playerInfo.IsExplored(Players[i])
					&& (i == ThisPlayer->Index || Players[i].HasSharedVisionWith(*ThisPlayer))) {
					show = true;
					break;
				}
			}
		}

		if (show) {
			const PixelPos mapPixelPos = vp.ScreenToMapPixelPos(cursorPos);
			UnitUnderCursor = UnitOnScreen(mapPixelPos.x, mapPixelPos.y);
		}
	} else if (CursorOn == ECursorOn::Minimap) {
		const Vec2i tilePos = UI.Minimap.ScreenToTilePos(cursorPos);

		if (Map.Field(tilePos)->playerInfo.IsExplored(*ThisPlayer) || ReplayRevealMap) {
			UnitUnderCursor = UnitOnMapTile(tilePos, std::nullopt);
		}
	}

	// NOTE: If unit is not selectable as a goal, you can't get a cursor hint
	if (UnitUnderCursor != nullptr && !UnitUnderCursor->IsVisibleAsGoal(*ThisPlayer) &&
		!ReplayRevealMap) {
		UnitUnderCursor = nullptr;
	}

	//  Selecting target.
	if (CursorState == CursorStates::Select) {
		if (CursorOn == ECursorOn::Map || CursorOn == ECursorOn::Minimap) {
			if (CustomCursor.length() && CursorByIdent(CustomCursor)) {
				GameCursor = CursorByIdent(CustomCursor);
			} else {
				GameCursor = UI.YellowHair.Cursor;
			}
			if (UnitUnderCursor != nullptr && !UnitUnderCursor->Type->BoolFlag[DECORATION_INDEX].value) {
				if (UnitUnderCursor->Player == ThisPlayer ||
					ThisPlayer->IsAllied(*UnitUnderCursor)) {
					if (CustomCursor.length() && CursorByIdent(CustomCursor)) {
						GameCursor = CursorByIdent(CustomCursor);
					} else {
						GameCursor = UI.YellowHair.Cursor;
					}
				} else if (UnitUnderCursor->Player->Index != PlayerNumNeutral) {
					if (CustomCursor.length() && CursorByIdent(CustomCursor)) {
						GameCursor = CursorByIdent(CustomCursor);
					} else {
						GameCursor = UI.YellowHair.Cursor;
					}
				}
			}
			if (CursorOn == ECursorOn::Minimap && (MouseButtons & RightButton)) {
				const Vec2i cursorPos = UI.Minimap.ScreenToTilePos(CursorScreenPos);
				//  Minimap move viewpoint
				UI.SelectedViewport->Center(Map.TilePosToMapPixelPos_Center(cursorPos));
			}
		}
		// FIXME: must move minimap if right button is down !
		return;
	}

	//  Cursor pointing.
	if (CursorOn == ECursorOn::Map) {
		//  Map
		if (UnitUnderCursor != nullptr && !UnitUnderCursor->Type->BoolFlag[DECORATION_INDEX].value
			&& (UnitUnderCursor->IsVisible(*ThisPlayer) || ReplayRevealMap)) {
			GameCursor = UI.Glass.Cursor;
		}
		return;
	}

	if (CursorOn == ECursorOn::Minimap && (MouseButtons & LeftButton)) {
		//  Minimap move viewpoint
		const Vec2i cursorPos = UI.Minimap.ScreenToTilePos(CursorScreenPos);

		UI.SelectedViewport->Center(Map.TilePosToMapPixelPos_Center(cursorPos));
		CursorStartScreenPos = CursorScreenPos;
		return;
	}
}

//.............................................................................

/**
**  Send selected units to repair
**
**  @param tilePos  tile map position.
*/
static int SendRepair(const Vec2i &tilePos)
{
	CUnit *dest = UnitUnderCursor;
	int ret = 0;

	// Check if the dest is repairable!
	if (dest && dest->Variable[HP_INDEX].Value < dest->Variable[HP_INDEX].Max
		&& dest->Type->RepairHP
		&& (dest->Player == ThisPlayer || ThisPlayer->IsAllied(*dest))) {
		for (CUnit *unit : Selected) {
			if (unit->Type->RepairRange) {
				const EFlushMode flush =
					!(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;

				SendCommandRepair(*unit, tilePos, dest, flush);
				ret = 1;
			} else {
				DebugPrint("Non-worker repairs\n");
			}
		}
	}
	return ret;
}

/**
**  Send selected units to point.
**
**  @param tilePos  tile map position.
**
**  @todo To reduce the CPU load for pathfinder, we should check if
**        the destination is reachable and handle nice group movements.
*/
static int SendMove(const Vec2i &tilePos)
{
	CUnit *goal = UnitUnderCursor;
	int ret = 0;
	const EFlushMode flush = !(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;

	// Alt makes unit to defend goal
	if (goal && (KeyModifiers & ModifierAlt)) {
		for (CUnit *unit : Selected) {
			goal->Blink = 4;
			SendCommandDefend(*unit, *goal, flush);
			ret = 1;
		}
	} else {
		// Move to a transporter.
		if (goal && goal->Type->CanTransport()) {
			if (ranges::any_of(Selected,
			                   [&](const CUnit *unit) { return CanTransport(*goal, *unit); })) {
				SendCommandStopUnit(*goal);
				ret = 1;
			} else {
				goal = nullptr;
			}
		} else {
			goal = nullptr;
		}

		const std::vector<Vec2i> goals = GroupMoveGoalsForUnits(Selected, tilePos);

		for (size_t i = 0; i < Selected.size(); ++i) {
			CUnit *unit = Selected[i];
			if (goal && CanTransport(*goal, *unit)) {
				goal->Blink = 4;
				SendCommandFollow(*goal, *unit, EFlushMode::Off);
				SendCommandBoard(*unit, *goal, flush);
				ret = 1;
			} else {
				SendCommandMove(*unit, goals[i], flush);
				ret = 1;
			}
		}
	}
	return ret;
}

/**
**  Send the current selected group attacking.
**
**  To empty field:
**    Move to this field attacking all enemy units in reaction range.
**
**  To unit:
**    Move to unit attacking and tracing the unit until dead.
**
**  @param tilePos  tile map position.
**
**  @return 1 if any unit have a new order, 0 else.
**
**  @see Selected
*/
static int SendAttack(const Vec2i &tilePos)
{
	const EFlushMode flush = !(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;
	CUnit *dest = UnitUnderCursor;
	int ret = 0;

	if (dest && dest->Type->BoolFlag[DECORATION_INDEX].value) {
		dest = nullptr;
	}
	// To an empty field the units go as a group, each one to its own goal.
	const std::vector<Vec2i> goals =
		dest == nullptr ? GroupMoveGoalsForUnits(Selected, tilePos) : std::vector<Vec2i>(Selected.size(), tilePos);

	for (size_t i = 0; i < Selected.size(); ++i) {
		CUnit *unit = Selected[i];
		if (unit->Type->CanAttack) {
			if (!dest || (dest != unit && CanTarget(*unit->Type, *dest->Type))) {
				if (dest) {
					dest->Blink = 4;
				}
				SendCommandAttack(*unit, goals[i], dest, flush);
				ret = 1;
			}
		} else {
			if (unit->CanMove()) {
				SendCommandMove(*unit, goals[i], flush);
				ret = 1;
			}
		}
	}
	return ret;
}

/**
**  Send the current selected group ground attacking.
**
**  @param tilePos  tile map position.
*/
static int SendAttackGround(const Vec2i &tilePos)
{
	const EFlushMode flush = !(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;
	int ret = 0;

	for (CUnit *unit : Selected) {
		if (unit->Type->CanAttack) {
			SendCommandAttackGround(*unit, tilePos, flush);
			ret = 1;
		} else {
			SendCommandMove(*unit, tilePos, flush);
			ret = 1;
		}
	}
	return ret;
}

/**
**  Let units patrol between current position and the selected.
**
**  @param tilePos  tile map position.
*/
static int SendPatrol(const Vec2i &tilePos)
{
	const EFlushMode flush = !(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;

	for (CUnit *unit : Selected) {
		SendCommandPatrol(*unit, tilePos, flush);
	}
	return Selected.empty() ? 0 : 1;
}

/**
**  Let units harvest wood/mine gold/haul oil
**
**  @param pos  tile map position
**
**  @see Selected
*/
static int SendResource(const Vec2i &pos)
{
	int res;
	CUnit *dest = UnitUnderCursor;
	int ret = 0;
	const EFlushMode flush = !(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;
	const CMapField &mf = *Map.Field(pos);

	for (CUnit *unit : Selected) {
		if (unit->Type->BoolFlag[HARVESTER_INDEX].value) {
			if (dest
				&& (res = dest->Type->GivesResource) != 0
				&& unit->Type->ResInfo[res]
				&& unit->ResourcesHeld < unit->Type->ResInfo[res]->ResourceCapacity
				&& dest->Type->BoolFlag[CANHARVEST_INDEX].value
				&& (dest->Player == unit->Player || dest->Player->Index == PlayerMax - 1)) {
				dest->Blink = 4;
				SendCommandResource(*unit, *dest, flush);
				ret = 1;
				continue;
			} else {
				for (res = 0; res < MaxCosts; ++res) {
					if (unit->Type->ResInfo[res]
						&& unit->Type->ResInfo[res]->TerrainHarvester
						&& mf.playerInfo.IsExplored(*unit->Player)
						/// By disabling this, we allow the harvester to find the nearest tile with a resource by itself, in case mf is empty.
						/*&& mf.IsTerrainResourceOnMap(res)*/
						&& unit->ResourcesHeld < unit->Type->ResInfo[res]->ResourceCapacity
						&& (unit->CurrentResource != res || unit->ResourcesHeld < unit->Type->ResInfo[res]->ResourceCapacity)) {
						SendCommandResourceLoc(*unit, pos, flush);
						ret = 1;
						break;
					}
				}
				if (res != MaxCosts) {
					continue;
				}
			}
		}
		if (!unit->CanMove()) {
			if (dest && dest->Type->GivesResource && dest->Type->BoolFlag[CANHARVEST_INDEX].value) {
				dest->Blink = 4;
				SendCommandResource(*unit, *dest, flush);
				ret = 1;
				continue;
			}
			if (mf.playerInfo.IsExplored(*unit->Player) && mf.IsTerrainResourceOnMap()) {
				SendCommandResourceLoc(*unit, pos, flush);
				ret = 1;
				continue;
			}
			SendCommandMove(*unit, pos, flush);
			ret = 1;
			continue;
		}
	}
	return ret;
}

/**
**  Send selected units to unload passengers.
**
**  @param tilePos  tile map position.
*/
static int SendUnload(const Vec2i &tilePos)
{
	const EFlushMode flush = !(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;

	for (CUnit *unit : Selected) {
		// FIXME: not only transporter selected?
		SendCommandUnload(*unit, tilePos, nullptr, flush);
	}
	return Selected.empty() ? 0 : 1;
}

/**
**  Send the current selected group for spell cast.
**
**  To empty field:
**  To unit:
**    Spell cast on unit or on map spot.
**
**  @param tilePos  tile map position.
**
**  @see Selected
*/
static int SendSpellCast(const Vec2i &tilePos)
{
	const EFlushMode flush = !(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;
	CUnit *dest = UnitUnderCursor;
	int ret = 0;

	/* NOTE: Vladi:
	   This is a high-level function, it sends target spot and unit
	   (if exists). All checks are performed at spell cast handle
	   function which will cancel function if cannot be executed
	 */
	for (CUnit *unit : Selected) {
		if (unit->Type->CanCastSpell.empty()) {
			DebugPrint(
				"but unit %d(%s) can't cast spells?\n", UnitNumber(*unit), unit->Type->Name.c_str());
			// this unit cannot cast spell
			continue;
		}
		// CursorValue here holds the spell type id
		const SpellType *spell = SpellTypeTable.at(CursorValue).get();
		if (!spell) {
			ErrorPrint("unknown spell-id: %d\n", CursorValue);
			ExitFatal(1);
		}
		if (dest && dest == unit
		    && (!spell->Condition || spell->Condition->TargetSelf == ECondition::ShouldBeFalse)) {
			// Only spells with explicit 'self: true' allows self targetting
			continue;
		}
		SendCommandSpellCast(*unit,
		                     tilePos,
		                     spell->Target == ETarget::Position ? nullptr : dest,
		                     CursorValue,
		                     flush);
		ret = 1;
	}
	return ret;
}

/**
**  Send a command to selected units.
**
**  @param tilePos  tile map position.
*/
static void SendCommand(const Vec2i &tilePos)
{
	int ret = 0;

	CurrentButtonLevel = 0;
	UI.ButtonPanel.Update();
	switch (CursorAction) {
		case ButtonCmd::Move:
			ret = SendMove(tilePos);
			break;
		case ButtonCmd::Repair:
			ret = SendRepair(tilePos);
			break;
		case ButtonCmd::Attack:
			ret = SendAttack(tilePos);
			break;
		case ButtonCmd::AttackGround:
			ret = SendAttackGround(tilePos);
			break;
		case ButtonCmd::Patrol:
			ret = SendPatrol(tilePos);
			break;
		case ButtonCmd::Harvest:
			ret = SendResource(tilePos);
			break;
		case ButtonCmd::Unload:
			ret = SendUnload(tilePos);
			break;
		case ButtonCmd::SpellCast:
			ret = SendSpellCast(tilePos);
			break;
		default:
			ErrorPrint("Unsupported send action %d\n", static_cast<int>(CursorAction));
			break;
	}
	if (ret) {
		// Acknowledge the command with first selected unit.
		for (CUnit *unit : Selected) {
			if (CursorAction == ButtonCmd::Attack || CursorAction == ButtonCmd::AttackGround || CursorAction == ButtonCmd::SpellCast) {
				if (unit->Type->MapSound.Attack.Sound) {
					PlayUnitSound(*unit, EUnitVoice::Attack);
					break;
				} else if (unit->Type->MapSound.Acknowledgement.Sound) {
					PlayUnitSound(*unit, EUnitVoice::Acknowledging);
					break;
				}
			} else if (CursorAction == ButtonCmd::Repair && unit->Type->MapSound.Repair.Sound) {
				PlayUnitSound(*unit, EUnitVoice::Repairing);
				break;
			} else if (CursorAction == ButtonCmd::Build && unit->Type->MapSound.Build.Sound) {
				PlayUnitSound(*unit, EUnitVoice::Build);
				break;
			} else if (unit->Type->MapSound.Acknowledgement.Sound) {
				PlayUnitSound(*unit, EUnitVoice::Acknowledging);
				break;
			}
		}
		ShowOrdersCount = GameCycle + Preference.ShowOrders * CYCLES_PER_SECOND;
	}
}

//.............................................................................

/**
**  Mouse button press on selection/group area.
**
**  @param num     Button number.
**  @param button  Mouse Button pressed.
*/
static void DoSelectionButtons(int num, unsigned)
{
	if (GameObserve || GamePaused || GameEstablishing) {
		return;
	}

	// PANEL-A: `num` is a cell of the selection grid
	const std::vector<CUnit *> &cells = GetSelectionCells();
	if (static_cast<size_t>(num) >= cells.size() || cells[num] == nullptr
		|| !(MouseButtons & LeftButton)) {
		return;
	}

	CUnit &unit = *cells[num];

	if ((KeyModifiers & ModifierControl) || (MouseButtons & (LeftButton << MouseDoubleShift))) {
		if (KeyModifiers & ModifierShift) {
			ToggleUnitsByType(unit);
		} else {
			SelectUnitsByType(unit);
		}
	} else if (KeyModifiers & ModifierAlt) {
		if (KeyModifiers & ModifierShift) {
			AddGroupFromUnitToSelection(unit);
		} else {
			SelectGroupFromUnit(unit);
		}
	} else if (KeyModifiers & ModifierShift) {
		ToggleSelectUnit(unit);
	} else {
		SelectSingleUnit(unit);
	}

	UI.StatusLine.Clear();
	UI.StatusLine.ClearCosts();
	CurrentButtonLevel = 0;
	SelectionChanged();
}

//.............................................................................

/**
**  Handle mouse button pressed in select state.
**
**  Select state is used for target of patrol, attack, move, ....
**
**  @param button  Button pressed down.
*/
static void UISelectStateButtonDown(unsigned)
{
	if (GameObserve || GamePaused || GameEstablishing) {
		return;
	}

	//
	//  Clicking on the map.
	//
	if (CursorOn == ECursorOn::Map && UI.MouseViewport->IsInsideMapArea(CursorScreenPos)) {
		UI.StatusLine.Clear();
		UI.StatusLine.ClearCosts();
		CursorState = CursorStates::Point;
		GameCursor = UI.Point.Cursor;
		CustomCursor.clear();
		CurrentButtonLevel = 0;
		UI.ButtonPanel.Update();

		if (MouseButtons & LeftButton) {
			const CViewport &vp = *UI.MouseViewport;
			const PixelPos mapPixelPos = vp.ScreenToMapPixelPos(CursorScreenPos);

			if (!ClickMissile.empty()) {
				MakeLocalMissile(MissileTypeByIdent(ClickMissile), mapPixelPos, mapPixelPos);
			}
			SendCommand(Map.MapPixelPosToTilePos(mapPixelPos));
		}
		return;
	}

	//
	//  Clicking on the minimap.
	//
	if (CursorOn == ECursorOn::Minimap) {
		const Vec2i cursorTilePos = UI.Minimap.ScreenToTilePos(CursorScreenPos);

		if (MouseButtons & LeftButton) {
			const PixelPos mapPixelPos = Map.TilePosToMapPixelPos_Center(cursorTilePos);

			UI.StatusLine.Clear();
			UI.StatusLine.ClearCosts();
			CursorState = CursorStates::Point;
			GameCursor = UI.Point.Cursor;
			CustomCursor.clear();
			CurrentButtonLevel = 0;
			UI.ButtonPanel.Update();
			if (!ClickMissile.empty()) {
				MakeLocalMissile(MissileTypeByIdent(ClickMissile), mapPixelPos, mapPixelPos);
			}
			SendCommand(cursorTilePos);
		} else {
			UI.SelectedViewport->Center(Map.TilePosToMapPixelPos_Center(cursorTilePos));
		}
		return;
	}

	if (CursorOn == ECursorOn::Button) {
		// FIXME: other buttons?
		// 74145: Spell-cast on unit portrait
		if (Selected.size() > 1 && ButtonAreaUnderCursor == ButtonArea::Selected
			&& CursorAction == ButtonCmd::SpellCast) {
			if (GameObserve || GamePaused || GameEstablishing) {
				return;
			}
			int num = ButtonUnderCursor;

			// PANEL-A: `num` is a cell of the selection grid
			const std::vector<CUnit *> &cells = GetSelectionCells();
			if (static_cast<size_t>(num) >= cells.size() || cells[num] == nullptr
				|| !(MouseButtons & LeftButton)) {
				return;
			}

			CUnit &unit = *cells[num];

			const Vec2i tilePos = unit.tilePos;
			UnitUnderCursor = &unit;
			SendSpellCast(tilePos);
			UnitUnderCursor = nullptr;
		}
		if (ButtonAreaUnderCursor == ButtonArea::Button) {
			OldButtonUnderCursor = ButtonUnderCursor;
			return;
		}
	}

	UI.StatusLine.Clear();
	UI.StatusLine.ClearCosts();
	CursorState = CursorStates::Point;
	if (CustomCursor.length() && CursorByIdent(CustomCursor)) {
		GameCursor = CursorByIdent(CustomCursor);
	} else {
		GameCursor = UI.YellowHair.Cursor;
	}
	CurrentButtonLevel = 0;
	UI.ButtonPanel.Update();
}


/**
**  Place the building of the placement mode on a tile (the body of a placing click).
**
**  @param tilePos  top-left tile of the building.
*/
static void PlaceBuildingAt(const Vec2i &tilePos)
{
	bool explored = CanBuildOnArea(*Selected[0], tilePos);

	// 0 Test build, don't really build
	if (CanBuildUnitType(Selected[0], *CursorBuilding, tilePos, 0) && (explored || ReplayRevealMap)) {
		const EFlushMode flush =
			!(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;
		PlayGameSound(GameSounds.PlacementSuccess[ThisPlayer->Race].Sound.get(), MaxSampleVolume);
		PlayUnitSound(*Selected[0], EUnitVoice::Build);
		for (CUnit *unit : Selected) {
			SendCommandBuildBuilding(*unit, tilePos, *CursorBuilding, flush);
		}
		if (!(KeyModifiers & (ModifierAlt | ModifierShift))) {
			CancelBuildingMode();
		}
	} else {
		PlayGameSound(GameSounds.PlacementError[ThisPlayer->Race].Sound.get(),
		              MaxSampleVolume);
	}
}

/**
**  TOUCH-R7: the left button went down on the map in placement mode. On the preview: it is
**  held at the touched point (a release without motion will place). Elsewhere: the preview
**  goes there, its middle under the finger (Q4 = B).
**
**  @param screenPos  the pointer.
*/
static void TouchPlacementPress(const PixelPos &screenPos)
{
	const CViewport &vp = *UI.MouseViewport;
	const Vec2i tilePos = vp.ScreenToTilePos(screenPos);
	Vec2i preview;

	TouchPlacePressed = true;
	TouchPlaceDragged = false;
	if (GetBuildingPreviewTile(vp, preview)
		&& tilePos.x >= preview.x && tilePos.x < preview.x + CursorBuilding->TileWidth
		&& tilePos.y >= preview.y && tilePos.y < preview.y + CursorBuilding->TileHeight) {
		TouchPlaceOnPreview = true;
		TouchPlaceGrab = vp.ScreenToMapPixelPos(screenPos) - Map.TilePosToMapPixelPos_TopLeft(preview);
	} else {
		TouchPlaceOnPreview = false;
		// half the footprint, in map pixels: the middle of the building
		TouchPlaceGrab = PixelPos(CursorBuilding->TileWidth * PixelTileSize.x / 2,
		                          CursorBuilding->TileHeight * PixelTileSize.y / 2);
		SetBuildingPreviewTile(vp, TouchPlacementTile(vp, screenPos));
	}
}

/**
**  TOUCH-R7: the left button came up after TouchPlacementPress. A tap on the preview places
**  the building; a tap elsewhere and every drag have only moved the preview.
*/
static void TouchPlacementRelease()
{
	const bool confirm = TouchPlaceOnPreview && !TouchPlaceDragged;
	Vec2i tilePos;

	TouchPlacePressed = false;
	TouchPlaceOnPreview = false;
	TouchPlaceDragged = false;
	if (confirm && CursorBuilding && !Selected.empty() && UI.MouseViewport
		&& GetBuildingPreviewTile(*UI.MouseViewport, tilePos)) {
		PlaceBuildingAt(tilePos);
	}
}

static void UIHandleButtonDown_OnMap(unsigned button)
{
	Assert(UI.MouseViewport);
	if ((MouseButtons & LeftButton) && UI.SelectedViewport != UI.MouseViewport) {
		UI.SelectedViewport = UI.MouseViewport;
		DebugPrint("selected viewport changed to %ld.\n",
		           static_cast<long int>(UI.SelectedViewport - UI.Viewports));
	}

	// to redraw the cursor immediately (and avoid up to 1 sec delay
	if (CursorBuilding) {
		// Possible Selected[0] was removed from map
		// need to make sure there is a unit to build
		if (Selected[0] && (MouseButtons & LeftButton)
			&& UI.MouseViewport->IsInsideMapArea(CursorScreenPos)) {// enter select mode
			const Vec2i tilePos = UI.MouseViewport->ScreenToTilePos(CursorScreenPos);

			if (UiTouchMode()) {
				// TOUCH-R7: a finger's press does not place; see TouchPlacementRelease.
				TouchPlacementPress(CursorScreenPos);
			} else {
				PlaceBuildingAt(tilePos);
			}
		} else {
			CancelBuildingMode();
		}
		return;
	}

	if (MouseButtons & UI.PieMenu.MouseButton) { // enter pie menu
		UnitUnderCursor = nullptr;
		GameCursor = UI.Point.Cursor;  // Reset
		CursorStartScreenPos = CursorScreenPos;
		if (!Selected.empty() && Selected[0]->Player == ThisPlayer && CursorState == CursorStates::Point) {
			CursorState = CursorStates::PieMenu;
		}
	} else if (MouseButtons & RightButton) {
		if (!GameObserve && !GamePaused && !GameEstablishing && UI.MouseViewport->IsInsideMapArea(CursorScreenPos)) {
			CUnit *unit;
			// FIXME: Rethink the complete chaos of coordinates here
			// FIXME: Johns: Perhaps we should use a pixel map coordinates
			const Vec2i tilePos = UI.MouseViewport->ScreenToTilePos(CursorScreenPos);

			if (UnitUnderCursor != nullptr && (unit = UnitOnMapTile(tilePos, std::nullopt))
				&& !UnitUnderCursor->Type->BoolFlag[DECORATION_INDEX].value) {
				unit->Blink = 4;                // if right click on building -- blink
			} else { // if not not click on building -- green cross
				if (!ClickMissile.empty()) {
					const PixelPos mapPixelPos = UI.MouseViewport->ScreenToMapPixelPos(CursorScreenPos);

					MakeLocalMissile(MissileTypeByIdent(ClickMissile), mapPixelPos, mapPixelPos);
				}
			}
			const PixelPos mapPixelPos = UI.MouseViewport->ScreenToMapPixelPos(CursorScreenPos);
			DoRightButton(mapPixelPos);
		}
	} else if (MouseButtons & LeftButton) { // enter select mode
		CursorStartScreenPos = CursorScreenPos;
		CursorStartMapPos = UI.MouseViewport->ScreenToMapPixelPos(CursorScreenPos);
		GameCursor = UI.Cross.Cursor;
		CursorState = CursorStates::Rectangle;
	} else if (MouseButtons & MiddleButton) {// enter move map mode
		CursorStartScreenPos = CursorScreenPos;
		GameCursor = UI.Scroll.Cursor;
	}
}

static void UIHandleButtonDown_OnMinimap(unsigned button)
{
	const Vec2i cursorTilePos = UI.Minimap.ScreenToTilePos(CursorScreenPos);

	if (MouseButtons & LeftButton) { // enter move mini-mode
		UI.SelectedViewport->Center(Map.TilePosToMapPixelPos_Center(cursorTilePos));
	} else if (MouseButtons & RightButton) {
		if (!GameObserve && !GamePaused && !GameEstablishing) {
			const PixelPos mapPixelPos = Map.TilePosToMapPixelPos_Center(cursorTilePos);
			if (!ClickMissile.empty()) {
				MakeLocalMissile(MissileTypeByIdent(ClickMissile), mapPixelPos, mapPixelPos);
			}
			DoRightButton(mapPixelPos);
		}
	}
}

static void UIHandleButtonDown_OnButton(unsigned button)
{
	// clicked on info panel - selection shown
	if (Selected.size() > 1 && ButtonAreaUnderCursor == ButtonArea::Selected) {
		PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
		DoSelectionButtons(ButtonUnderCursor, button);
	} else if ((MouseButtons & LeftButton)) {
		//  clicked on menu button
		if (ButtonAreaUnderCursor == ButtonArea::Menu) {
			if ((ButtonUnderCursor == ButtonUnderMenu || ButtonUnderCursor == ButtonUnderNetworkMenu)
				&& !GameMenuButtonClicked) {
				PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
				GameMenuButtonClicked = true;
			} else if (ButtonUnderCursor == ButtonUnderNetworkDiplomacy && !GameDiplomacyButtonClicked) {
				PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
				GameDiplomacyButtonClicked = true;
			} else if (ButtonUnderCursor == ButtonUnderFreeWorkers) {
				UiFindIdleWorker();
			}
			//  clicked on user buttons
		} else if (ButtonAreaUnderCursor == ButtonArea::User) {
			for (size_t i = 0; i < UI.UserButtons.size(); ++i) {
				CUIUserButton &button = UI.UserButtons[i];

				if (i == size_t(ButtonUnderCursor) && !button.Clicked) {
					PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
					button.Clicked = true;
				}
			}
			//  clicked on selected button
		} else if (ButtonAreaUnderCursor == ButtonArea::Selected) {
			//  clicked on single unit shown
			if (ButtonUnderCursor == 0 && Selected.size() == 1) {
				PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
				UI.SelectedViewport->Center(Selected[0]->GetMapPixelPosCenter());
			}
			//  clicked on training button
		} else if (ButtonAreaUnderCursor == ButtonArea::Training) {
			if (!GameObserve && !GamePaused && !GameEstablishing && ThisPlayer->IsTeamed(*Selected[0])) {
				if (static_cast<size_t>(ButtonUnderCursor) < Selected[0]->Orders.size()
					&& Selected[0]->Orders[ButtonUnderCursor]->Action == UnitAction::Train) {
					const COrder_Train &order = static_cast<COrder_Train &>(*Selected[0]->Orders[ButtonUnderCursor]);

					DebugPrint("Cancel slot %d %s\n",
					           ButtonUnderCursor,
					           order.GetUnitType().Ident.c_str());
					PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
					SendCommandCancelTraining(*Selected[0], ButtonUnderCursor, &order.GetUnitType());
				}
			}
			//  clicked on upgrading button
		} else if (ButtonAreaUnderCursor == ButtonArea::Upgrading) {
			if (!GameObserve && !GamePaused && !GameEstablishing && ThisPlayer->IsTeamed(*Selected[0])) {
				if (ButtonUnderCursor == 0 && Selected.size() == 1) {
					DebugPrint("Cancel upgrade %s\n", Selected[0]->Type->Ident.c_str());
					PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
					SendCommandCancelUpgradeTo(*Selected[0]);
				}
			}
			//  clicked on researching button
		} else if (ButtonAreaUnderCursor == ButtonArea::Researching) {
			if (!GameObserve && !GamePaused && !GameEstablishing && ThisPlayer->IsTeamed(*Selected[0])) {
				if (ButtonUnderCursor == 0 && Selected.size() == 1) {
					DebugPrint("Cancel research %s\n", Selected[0]->Type->Ident.c_str());
					PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
					SendCommandCancelResearch(*Selected[0]);
				}
			}
			//  clicked on button panel
		} else if (ButtonAreaUnderCursor == ButtonArea::Transporting) {
			//  for transporter
			if (!GameObserve && !GamePaused && !GameEstablishing && ThisPlayer->IsTeamed(*Selected[0])) {
				if (Selected[0]->BoardCount >= ButtonUnderCursor) {
					size_t j = 0;

					for (CUnit *uins : Selected[0]->InsideUnits) {
						if (!uins->Boarded || j >= UI.TransportingButtons.size() || (Selected[0]->Player != ThisPlayer && uins->Player != ThisPlayer)) {
							continue;
						}
						int lastOccupiedButton = j + uins->Type->BoardSize - 1;
						if (ButtonAreaUnderCursor == ButtonArea::Transporting) {
							const EFlushMode flush =
								!(KeyModifiers & ModifierShift) ? EFlushMode::On : EFlushMode::Off;
							for (int sub_j = j; sub_j <= lastOccupiedButton; sub_j++) {
								if (static_cast<size_t>(ButtonUnderCursor) == sub_j) {
									Assert(uins->Boarded);
									if (ThisPlayer->IsTeamed(*Selected[0]) || uins->Player == ThisPlayer) {
										PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
										SendCommandUnload(*Selected[0], Selected[0]->tilePos, uins, flush);
									}
								}
							}
						}
						j = lastOccupiedButton + 1;
					}
				}
			}
		} else if (ButtonAreaUnderCursor == ButtonArea::Button) {
			if (!GameObserve && !GamePaused && !GameEstablishing && (ThisPlayer->IsTeamed(*Selected[0]) || Selected[0]->Player->Index == PlayerMax - 1)) {
				PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
				OldButtonUnderCursor = ButtonUnderCursor;
			}
		}
	} else if ((MouseButtons & MiddleButton)) {
		//  clicked on info panel - single unit shown
		if (ButtonAreaUnderCursor == ButtonArea::Selected && ButtonUnderCursor == 0 && Selected.size() == 1) {
			PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
			if (UI.SelectedViewport->Unit == Selected[0]) {
				UI.SelectedViewport->Unit = nullptr;
			} else {
				UI.SelectedViewport->Unit = Selected[0];
			}
		}
	} else if ((MouseButtons & RightButton)) {
	}
}

/**
**  Called if mouse button pressed down.
**
**  @param button  Button pressed down.
*/
void UIHandleButtonDown(unsigned button)
{
	// Detect long left selection click
	const bool longLeftButton = (MouseButtons & ((LeftButton << MouseHoldShift))) != 0;

	if (IsDemoMode()) {
		// If we are in "demo mode", exit no matter what we click on.
		void ActionDraw();
		ActionDraw();
		return;
	}

	static bool OldShowSightRange;
	static bool OldShowReactionRange;
	static bool OldShowAttackRange;
	static bool OldValid = false;
	OldButtonUnderCursor = -1;

	// Reset the ShowNameDelay counters
	ShowNameDelay = ShowNameTime = GameCycle;

	if (longLeftButton) {
		if (!OldValid) {
			OldShowSightRange = Preference.ShowSightRange;
			OldShowAttackRange = Preference.ShowAttackRange;
			OldShowReactionRange = Preference.ShowReactionRange;
			OldValid = true;

			Preference.ShowSightRange = true;
			Preference.ShowAttackRange = true;
			Preference.ShowReactionRange = true;
		}
	} else if (OldValid) {
		Preference.ShowSightRange = OldShowSightRange;
		Preference.ShowAttackRange = OldShowAttackRange;
		Preference.ShowReactionRange = OldShowReactionRange;
		OldValid = false;
	}

	// select mode
	if (CursorState == CursorStates::Rectangle) {
		return;
	}
	// CursorOn should have changed with BigMapMode, so recompute it.
	HandleMouseOn(CursorScreenPos);
	//  Selecting target. (Move,Attack,Patrol,... commands);
	if (CursorState == CursorStates::Select) {
		UISelectStateButtonDown(button);
		return;
	}

	if (CursorState == CursorStates::PieMenu) {
		if (CursorOn == ECursorOn::Map) {
			HandlePieMenuMouseSelection();
			return;
		} else {
			// Pie Menu canceled
			CursorState = CursorStates::Point;
			// Don't return, we might be over another button
		}
	}

	//  Cursor is on the map area
	if (CursorOn == ECursorOn::Map) {
		UIHandleButtonDown_OnMap(button);
	} else if (CursorOn == ECursorOn::Minimap) {
		//  Cursor is on the minimap area
		UIHandleButtonDown_OnMinimap(button);
	} else if (CursorOn == ECursorOn::Button) {
		//  Cursor is on the buttons: group or command
		UIHandleButtonDown_OnButton(button);
	}
}

/**
**  Called if mouse button released.
**
**  @param button  Button released.
*/
void UIHandleButtonUp(unsigned button)
{
	if (TouchPlacePressed && (1 << (button & 0xFF)) == LeftButton) {
		// TOUCH-R7: the release that belongs to TouchPlacementPress, with or without the
		// double-click bit (a double tap is "put the preview there", then "place").
		TouchPlacementRelease();
		return;
	}

	//
	//  Move map.
	//
	if (GameCursor == UI.Scroll.Cursor) {
		GameCursor = UI.Point.Cursor;
		return;
	}

	//
	//  Pie Menu
	//
	if (CursorState == CursorStates::PieMenu) {
		// Little threshold
		if (1 < abs(CursorStartScreenPos.x - CursorScreenPos.x)
			|| 1 < abs(CursorStartScreenPos.y - CursorScreenPos.y)) {
			// there was a move, handle the selected button/pie
			HandlePieMenuMouseSelection();
		}
	}

	if ((1 << button) == LeftButton) {
		//
		//  Menu (F10) button
		//
		if (GameMenuButtonClicked) {
			GameMenuButtonClicked = false;
			if (ButtonAreaUnderCursor == ButtonArea::Menu) {
				if (ButtonUnderCursor == ButtonUnderMenu || ButtonUnderCursor == ButtonUnderNetworkMenu) {
					// FIXME: Not if, in input mode.
					if (!IsNetworkGame()) {
						GamePaused = true;
						UI.StatusLine.Set(_("Game Paused"));
					}
					if (ButtonUnderCursor == ButtonUnderMenu) {
						if (UI.MenuButton.Callback) {
							UI.MenuButton.Callback->action(gcn::ActionEvent{nullptr, ""});
						}
					} else {
						if (UI.NetworkMenuButton.Callback) {
							UI.NetworkMenuButton.Callback->action(gcn::ActionEvent{nullptr, ""});
						}
					}
					return;
				}
			}
		}

		//
		//  Diplomacy button
		//
		if (GameDiplomacyButtonClicked) {
			GameDiplomacyButtonClicked = false;
			if (ButtonAreaUnderCursor == ButtonArea::Menu && ButtonUnderCursor == ButtonUnderNetworkDiplomacy) {
				if (UI.NetworkDiplomacyButton.Callback) {
					UI.NetworkDiplomacyButton.Callback->action(gcn::ActionEvent{nullptr, ""});
				}
				return;
			}
		}

		//
		//  User buttons
		//
		for (CUIUserButton &button : UI.UserButtons) {
			if (button.Clicked) {
				button.Clicked = false;
				if (ButtonAreaUnderCursor == ButtonArea::User) {
					if (button.Button.Callback) {
						button.Button.Callback->action(gcn::ActionEvent{nullptr, ""});
					}
					return;
				}
			}
		}
		if (!GameObserve && !GamePaused && !GameEstablishing && Selected.empty() == false && ThisPlayer->IsTeamed(*Selected[0])) {
			if (OldButtonUnderCursor != -1 && OldButtonUnderCursor == ButtonUnderCursor) {
				UI.ButtonPanel.DoClicked(ButtonUnderCursor);
				OldButtonUnderCursor = -1;
				return;
			}
		}
		if (CursorOn == ECursorOn::Button) {
			// FIXME: other buttons?
			if (ButtonAreaUnderCursor == ButtonArea::Button && OldButtonUnderCursor != -1 && OldButtonUnderCursor == ButtonUnderCursor) {
				UI.ButtonPanel.DoClicked(ButtonUnderCursor);
				return;
			}
		}
	}

	// FIXME: should be completly rewritten
	// FIXME: must selecting!  (lokh: what does this mean? is this done now?)

	// SHIFT toggles select/unselect a single unit and
	// add the content of the rectangle to the selectection
	// ALT takes group of unit
	// CTRL takes all units of same type (st*rcr*ft)
	if (CursorState == CursorStates::Rectangle && !(MouseButtons & LeftButton)) { // leave select mode
		int num = 0;
		//
		//  Little threshold
		//
		if (CursorStartScreenPos.x < CursorScreenPos.x - 1 || CursorScreenPos.x + 1 < CursorStartScreenPos.x
			|| CursorStartScreenPos.y < CursorScreenPos.y - 1 || CursorScreenPos.y + 1 < CursorStartScreenPos.y) {
			PixelPos pos0 = CursorStartMapPos;
			const PixelPos cursorMapPos = UI.MouseViewport->ScreenToMapPixelPos(CursorScreenPos);
			PixelPos pos1 = cursorMapPos;

			if (pos0.x > pos1.x) {
				std::swap(pos0.x, pos1.x);
			}
			if (pos0.y > pos1.y) {
				std::swap(pos0.y, pos1.y);
			}
			if (KeyModifiers & ModifierShift) {
				if (KeyModifiers & ModifierAlt) {
					num = AddSelectedGroundUnitsInRectangle(pos0, pos1);
				} else if (KeyModifiers & ModifierControl) {
					num = AddSelectedAirUnitsInRectangle(pos0, pos1);
				} else {
					num = AddSelectedUnitsInRectangle(pos0, pos1);
				}
			} else {
				if (KeyModifiers & ModifierAlt) {
					num = SelectGroundUnitsInRectangle(pos0, pos1);
				} else if (KeyModifiers & ModifierControl) {
					num = SelectAirUnitsInRectangle(pos0, pos1);
				} else {
					num = SelectUnitsInRectangle(pos0, pos1);
				}
			}
		} else {
			//
			// Select single unit
			//
			// cade: cannot select unit on invisible space
			// FIXME: johns: only complete invisibile units
			const Vec2i cursorTilePos = UI.MouseViewport->ScreenToTilePos(CursorScreenPos);
			CUnit *unit = nullptr;
			if (ReplayRevealMap || Map.Field(cursorTilePos)->playerInfo.IsTeamVisible(*ThisPlayer)) {
				const PixelPos cursorMapPos = UI.MouseViewport->ScreenToMapPixelPos(CursorScreenPos);

				unit = UnitOnScreen(cursorMapPos.x, cursorMapPos.y);
			}
			if (unit) {
				// FIXME: Not nice coded, button number hardcoded!
				if ((KeyModifiers & ModifierControl)
					|| (button & (1 << MouseDoubleShift))) {
					if (KeyModifiers & ModifierShift) {
						num = ToggleUnitsByType(*unit);
					} else {
						num = SelectUnitsByType(*unit);
					}
				} else if ((KeyModifiers & ModifierAlt) && unit->LastGroup) {
					if (KeyModifiers & ModifierShift) {
						num = AddGroupFromUnitToSelection(*unit);
					} else {
						num = SelectGroupFromUnit(*unit);
					}

					// Don't allow to select own and enemy units.
					// Don't allow mixing buildings
				} else if (KeyModifiers & ModifierShift
						   && (unit->Player == ThisPlayer || ThisPlayer->IsTeamed(*unit))
						   && !unit->Type->Building
						   && (Selected.size() != 1 || !Selected[0]->Type->Building)
						   && (Selected.size() != 1 || Selected[0]->Player == ThisPlayer || ThisPlayer->IsTeamed(*Selected[0]))) {
					num = ToggleSelectUnit(*unit);
					if (!num) {
						SelectionChanged();
					}
				} else {
					SelectSingleUnit(*unit);
					num = 1;
				}
			} else {
				num = 0;
				if (!Selected.empty()) {
					// A left click on ground with no unit under it clears the selection: the
					// original's own behaviour (as UiUnselectAll).
					TouchRememberClearedSelection();
					UnSelectAll();
					NetworkSendSelection((CUnit **)nullptr, 0);
					SelectionChanged();
				}
			}
		}

		if (num) {
			UI.StatusLine.Clear();
			UI.StatusLine.ClearCosts();
			CurrentButtonLevel = 0;
			SelectionChanged();

			//
			//  Play selecting sound.
			//    Buildings,
			//    This player, or neutral unit (goldmine,critter)
			//    Other clicks.
			//
			if (Selected.size() == 1) {
				if (Selected[0]->CurrentAction() == UnitAction::Built && Selected[0]->Player->Index == ThisPlayer->Index) {
					PlayUnitSound(*Selected[0], EUnitVoice::Building);
				} else if (Selected[0]->Burning) {
					// FIXME: use GameSounds.Burning
					PlayGameSound(SoundForName("burning").get(), MaxSampleVolume);
				} else if (Selected[0]->Player == ThisPlayer || ThisPlayer->IsTeamed(*Selected[0])
						   || Selected[0]->Player->Type == PlayerTypes::PlayerNeutral) {
					PlayUnitSound(*Selected[0], EUnitVoice::Selected);
				} else {
					PlayGameSound(GameSounds.Click.Sound.get(), MaxSampleVolume);
				}
				// M666-4: the original prints nothing at the bottom of the map view when a unit is
				// selected (captures s01 i03 u02 u05 hs00 k24 c25 b02 b03 t11).
				// Preference.NoStatusLineTooltips is the HUD's existing "no hint line" switch
				// (scripts/ui.lua, HUD-003); the classic line stays for a UI that keeps hints.
				if (Selected[0]->Player == ThisPlayer && !Preference.NoStatusLineTooltips) {
					char buf[64];
					if (Selected[0]->Player->UnitTypesCount[Selected[0]->Type->Slot] > 1) {
						snprintf(buf, sizeof(buf), _("You have ~<%d~> %ss"),
								 Selected[0]->Player->UnitTypesCount[Selected[0]->Type->Slot],
								 Selected[0]->Type->Name.c_str());
					} else {
						snprintf(buf, sizeof(buf), _("You have ~<%d~> %s(s)"),
								 Selected[0]->Player->UnitTypesCount[Selected[0]->Type->Slot],
								 Selected[0]->Type->Name.c_str());
					}
					UI.StatusLine.Set(buf);
				}
			}
		}

		CursorStartScreenPos.x = 0;
		CursorStartScreenPos.y = 0;
		GameCursor = UI.Point.Cursor;
		CursorState = CursorStates::Point;
	}
}

/**
**  Get pie menu under the cursor
**
**  @return  Index of the pie menu under the cursor or -1 for none
*/
static int GetPieUnderCursor()
{
	int x = CursorScreenPos.x - (CursorStartScreenPos.x - ICON_SIZE_X / 2);
	int y = CursorScreenPos.y - (CursorStartScreenPos.y - ICON_SIZE_Y / 2);
	for (int i = 0; i < 9; ++i) {
		if (x > UI.PieMenu.X[i] && x < UI.PieMenu.X[i] + ICON_SIZE_X
			&& y > UI.PieMenu.Y[i] && y < UI.PieMenu.Y[i] + ICON_SIZE_Y) {
			return i;
		}
	}
	return -1; // no pie under cursor
}

/**
**  Draw Pie Menu
*/
void DrawPieMenu()
{
	char buf[2] = "?";

	if (CursorState != CursorStates::PieMenu) {
		return;
	}

	if (CurrentButtons.empty()) { // no buttons
		CursorState = CursorStates::Point;
		return;
	}
	// Pie menu composites into the CPU overlay (icons/labels/background). Mark it dirty so it uploads.
	OverlayDirty = true;
	std::vector<ButtonAction> &buttons(CurrentButtons);
	CLabel label(GetGameFont());
	CViewport *vp = UI.SelectedViewport;
	PushClipping();
	vp->SetClipping();

	// Draw background
	if (UI.PieMenu.G) {
		UI.PieMenu.G->DrawFrameClip(0,
									CursorStartScreenPos.x - UI.PieMenu.G->Width / 2,
									CursorStartScreenPos.y - UI.PieMenu.G->Height / 2);
	}
	for (int i = 0; i < (int)UI.ButtonPanel.Buttons.size() && i < 9; ++i) {
		if (buttons[i].Pos != -1) {
			int x = CursorStartScreenPos.x - ICON_SIZE_X / 2 + UI.PieMenu.X[i];
			int y = CursorStartScreenPos.y - ICON_SIZE_Y / 2 + UI.PieMenu.Y[i];
			const PixelPos pos(x, y);

			const bool gray = ranges::any_of(
				Selected, [&](const CUnit *unit) { return !IsButtonAllowed(*unit, buttons[i]); });
			// Draw icon
			if (gray) {
				buttons[i].Icon.Icon->DrawGrayscaleIcon(pos);
			} else {
				buttons[i].Icon.Icon->DrawIcon(pos, ThisPlayer->Index);
			}

			// Tutorial show command key in icons
			if (UI.ButtonPanel.ShowCommandKey) {
				const char *text;

				if (buttons[i].Key == 27) {
					text = "ESC";
				} else {
					buf[0] = toupper(buttons[i].Key);
					text = buf;
				}
				label.DrawClip(x + 4, y + 4, text);
			}
		}
	}

	PopClipping();

	int i = GetPieUnderCursor();
	if (i != -1 && KeyState != EKeyState::Input && buttons[i].Pos != -1) {
		if (!Preference.NoStatusLineTooltips) {
			UpdateStatusLineForButton(buttons[i]);
		}
		DrawPopup(buttons[i], UI.ButtonPanel.Buttons[i],
				  CursorStartScreenPos.x + UI.PieMenu.X[i], CursorStartScreenPos.y + UI.PieMenu.Y[i]);
	}
}

/**
**  Handle pie menu mouse selection
*/
static void HandlePieMenuMouseSelection()
{
	if (CurrentButtons.empty()) {  // no buttons
		return;
	}

	int pie = GetPieUnderCursor();
	if (pie != -1) {
		const ButtonCmd action = CurrentButtons[pie].Action;
		UI.ButtonPanel.DoClicked(pie);
		if (action == ButtonCmd::Button) {
			// there is a submenu => stay in piemenu mode
			// and recenter the piemenu around the cursor
			CursorStartScreenPos = CursorScreenPos;
		} else {
			if (CursorState == CursorStates::PieMenu) {
				CursorState = CursorStates::Point;
			}
			CursorOn = ECursorOn::Unknown;
			UIHandleMouseMove(CursorScreenPos); // recompute CursorOn and company
		}
	} else {
		CursorState = CursorStates::Point;
		CursorOn = ECursorOn::Unknown;
		UIHandleMouseMove(CursorScreenPos); // recompute CursorOn and company
	}
}
//@}
