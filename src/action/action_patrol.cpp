//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//           Stratagus - A free fantasy real time strategy game engine
//
/**@name action_patrol.cpp - The patrol action. */
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

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "stratagus.h"

#include "action/action_patrol.h"

#include "animation.h"
#include "goal_pull.h"
#include "iolib.h"
#include "map.h"
#include "pathfinder.h"
#include "script.h"
#include "ui.h"
#include "unit.h"
#include "unittype.h"
#include "video.h"
#include "wc2ai.h"

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

/* static */ std::unique_ptr<COrder> COrder::NewActionPatrol(const Vec2i &currentPos, const Vec2i &dest)
{
	Assert(Map.Info.IsPointOnMap(currentPos));
	Assert(Map.Info.IsPointOnMap(dest));

	auto order = std::make_unique<COrder_Patrol>();

	order->goalPos = dest;
	order->WayPoint = currentPos;
	return order;
}


void COrder_Patrol::Save(CFile &file, const CUnit &unit) const /* override */
{
	file.printf("{\"action-patrol\",");

	if (this->Finished) {
		file.printf(" \"finished\", ");
	}
	file.printf(" \"tile\", {%d, %d},", this->goalPos.x, this->goalPos.y);
	file.printf(" \"goal-pulled\", %d,", this->GoalPulled ? 1 : 0);
	if (this->Ordered != Vec2i(-1, -1)) {
		file.printf(" \"ordered\", {%d, %d},", this->Ordered.x, this->Ordered.y);
	}
	file.printf(" \"range\", %d,", this->Range);

	if (this->WaitingCycle != 0) {
		file.printf(" \"waiting-cycle\", %d,", this->WaitingCycle);
	}
	if (this->ReissueAt != 0) {
		file.printf(" \"reissue-at\", %lu,", this->ReissueAt);
	}
	file.printf(" \"patrol\", {%d, %d}", this->WayPoint.x, this->WayPoint.y);
	file.printf("}");
}

bool COrder_Patrol::ParseSpecificData(lua_State *l,
                                      int &j,
                                      std::string_view value,
                                      const CUnit &unit) /* override */
{
	if (value == "patrol") {
		++j;
		lua_rawgeti(l, -1, j + 1);
		CclGetPos(l, &this->WayPoint);
		lua_pop(l, 1);
	} else if (value == "waiting-cycle") {
		++j;
		this->WaitingCycle = LuaToNumber(l, -1, j + 1);
	} else if (value == "reissue-at") {
		++j;
		this->ReissueAt = LuaToNumber(l, -1, j + 1);
	} else if (value == "range") {
		++j;
		this->Range = LuaToNumber(l, -1, j + 1);
	} else if (value == "tile") {
		++j;
		lua_rawgeti(l, -1, j + 1);
		CclGetPos(l, &this->goalPos);
		lua_pop(l, 1);
		// A save written before the goal pull has no "goal-pulled" (written after "tile"): the leg
		// keeps its goal as it is.
		this->GoalPulled = true;
	} else if (value == "goal-pulled") {
		++j;
		this->GoalPulled = LuaToNumber(l, -1, j + 1) != 0;
	} else if (value == "ordered") {
		++j;
		lua_rawgeti(l, -1, j + 1);
		CclGetPos(l, &this->Ordered);
		lua_pop(l, 1);
	} else {
		return false;
	}
	return true;
}

bool COrder_Patrol::IsValid() const /* override */
{
	return true;
}

PixelPos COrder_Patrol::Show(const CViewport &vp,
                             const PixelPos &lastScreenPos) const /* override */
{
	const PixelPos pos1 = vp.TilePosToScreen_Center(this->goalPos);
	const PixelPos pos2 = vp.TilePosToScreen_Center(this->WayPoint);

	Video.DrawLineClip(ColorGreen, lastScreenPos, pos1);
	Video.FillCircleClip(ColorBlue, pos1, 2);
	Video.DrawLineClip(ColorBlue, pos1, pos2);
	Video.FillCircleClip(ColorBlue, pos2, 3);
	return pos2;
}

void COrder_Patrol::UpdatePathFinderData(PathFinderInput &input) /* override */
{
	input.SetMinRange(0);
	input.SetMaxRange(this->Range);
	const Vec2i tileSize(0, 0);
	input.SetGoal(this->goalPos, tileSize);
}


/**
**  The original's patrol memory: every land unit, a flyer or a ship only on a leg off both axes by more
**  than one tile (wc2ai::PatrolRememberLeg). The answer is the same for both legs of a patrol.
*/
bool COrder_Patrol::Remembered(const CUnit &unit) const
{
	const bool airOrSea = unit.Type->MoveType == EMovement::Fly || unit.Type->MoveType == EMovement::Naval;
	return wc2ai::PatrolRememberLeg(airOrSea, goalPos.x - WayPoint.x, goalPos.y - WayPoint.y);
}

/**
**  The re-issue of a remembered leg: only when its counter is 0; it then runs for 40 game cycles.
*/
wc2ai::Reissue COrder_Patrol::ReissueReady(const CUnit &, unsigned long cycle) /* override */
{
	if (!wc2ai::PatrolReissueReady(cycle, this->ReissueAt)) {
		return wc2ai::Reissue::Wait;
	}
	this->ReissueAt = cycle + wc2ai::PatrolReissueWait;
	// The re-issue sets the leg again from its ordered point (+0x94; DECODE_B11_ROUND2 rule 3) through
	// the order setter 0x4D8970, which pulls it from where the unit stands now (DECODE_GOAL_PULL PULL-6).
	if (this->GoalPulled && this->Ordered != Vec2i(-1, -1)) {
		this->goalPos = this->Ordered;
		this->GoalPulled = false;
		this->Range = 0;
	}
	return wc2ai::Reissue::Ready;
}

/**
**  The patrol ends and the unit stands; a remembered leg is kept as the saved order, so the end of a
**  later fight sends it again.
*/
static void PatrolStopKeepingMemory(CUnit &unit, COrder_Patrol &order)
{
	if (order.Remembered(unit) && unit.SavedOrder == nullptr) {
		unit.SavedOrder = order.Clone();
	}
	order.Finished = true;
}

void COrder_Patrol::Execute(CUnit &unit) /* override */
{
	if (IsWaiting(unit)) {
		return;
	}
	StopWaiting(unit);
	if (!this->GoalPulled) {
		// A leg has just begun: a blocked goal is pulled toward the unit from where it stands now
		// (order 5 is set through 0x4D8970, which pulls; the turn-round 0x496960 sets it again).
		this->GoalPulled = true;
		this->Ordered = this->goalPos;
		this->goalPos = PullGoalTowardUnit(unit, this->goalPos);
		// No way at all from here (as Move): the unit stands with the memory kept, as at a blocked end.
		if (PathStepFindsNoWay(unit, this->goalPos)) {
			this->WaitingCycle = 0;
			this->Range = 0;
			PatrolStopKeepingMemory(unit, *this);
			return;
		}
	}

	switch (DoActionMove(unit)) {
		case PF_FAILED:
			this->WaitingCycle = 0;
			break;
		case PF_UNREACHABLE:
			// No path to the (pulled) goal: as Move, the range round it widens until a tile of the ring
			// can be reached, with no wait. An enclosed unit stands at the map's size, without the
			// re-issue; the memory is kept (0x4C2A9B).
			this->WaitingCycle = 0;
			if (this->Range > std::max(Map.Info.MapWidth, Map.Info.MapHeight)) {
				this->Range = 0;
				PatrolStopKeepingMemory(unit, *this);
				return;
			}
			this->Range++;
			break;

		case PF_REACHED:
			if (this->Range > 0) {
				// Came as near as it could to a goal it cannot stand on: it stands there with the memory
				// kept, as a move to such a goal ends (DECODE_GOAL_PULL section 2). The original does not
				// turn round here either (run 49 item 1: 5 cases, standing 1350 - 2350 frames).
				this->WaitingCycle = 0;
				this->Range = 0;
				PatrolStopKeepingMemory(unit, *this);
				return;
			}
			// Turn round; the new leg starts with the counter at 0 (0x496960 -> 0x4C1550) and its goal
			// is pulled again from the tile reached.
			this->WaitingCycle = 1;
			this->Range = 0;
			std::swap(this->WayPoint, this->goalPos);
			this->ReissueAt = 0;
			this->GoalPulled = false;

			break;
		case PF_WAIT:
			// Wait for a while then give up: a remembered leg is sent again to the same goal when its
			// counter is 0, else the unit stands with the memory kept (0x4C2D80 -> 0x4D87E0).
			this->WaitingCycle++;
			if (this->WaitingCycle == 5) {
				this->WaitingCycle = 0;
				unit.pathFinderData->output.Cycles = 0; //moving counter
				if (!this->Remembered(unit) || this->ReissueReady(unit, GameCycle) != wc2ai::Reissue::Ready) {
					PatrolStopKeepingMemory(unit, *this);
					return;
				}
			}
			break;
		default: // moving
			this->WaitingCycle = 0;
			break;
	}

	if (!unit.Anim.Unbreakable) {
		if (AutoAttack(unit) || AutoRepair(unit) || AutoCast(unit)) {
			this->Finished = true;
		}
	}
}

/**
**  Get goal position
*/
const Vec2i COrder_Patrol::GetGoalPos() const /* override */
{
	const Vec2i invalidPos(-1, -1);
	if (goalPos != invalidPos) {
		return goalPos;
	}
	if (this->HasGoal()) {
		return this->GetGoal()->tilePos;
	}
	return invalidPos;
}

//@}
