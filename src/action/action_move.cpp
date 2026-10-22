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
/**@name action_move.cpp - The move action. */
//
//      (c) Copyright 1998-2006 by Lutz Sammer and Jimmy Salmon
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

#include "action/action_move.h"

#include "ai.h"
#include "goal_pull.h"
#include "animation.h"
#include "interface.h"
#include "iolib.h"
#include "map.h"
#include "pathfinder.h"
#include "player.h"
#include "script.h"
#include "settings.h"
#include "sound.h"
#include "tileset.h"
#include "ui.h"
#include "unit.h"
#include "unit_find.h"
#include "unittype.h"
#include "video.h"

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

/* static */ std::unique_ptr<COrder> COrder::NewActionMove(const Vec2i &pos)
{
	Assert(Map.Info.IsPointOnMap(pos));

	auto order = std::make_unique<COrder_Move>();

	order->goalPos = pos;
	order->OrderedCycle = GameCycle;
	return order;
}

void COrder_Move::Save(CFile &file, const CUnit &unit) const /* override */
{
	file.printf("{\"action-move\",");

	if (this->Finished) {
		file.printf(" \"finished\", ");
	}
	file.printf(" \"range\", %d,", this->Range);
	file.printf(" \"tile\", {%d, %d},", this->goalPos.x, this->goalPos.y);
	file.printf(" \"goal-pulled\", %d,", this->GoalPulled ? 1 : 0);
	file.printf(" \"ordered-cycle\", %lu", this->OrderedCycle);
	if (this->GoingRound) {
		file.printf(", \"going-round\"");
	}

	file.printf("}");
}

bool COrder_Move::ParseSpecificData(lua_State *l,
                                    int &j,
                                    std::string_view value,
                                    const CUnit &unit) /* override */
{
	if (value == "range") {
		++j;
		this->Range = LuaToNumber(l, -1, j + 1);
	} else if (value == "tile") {
		++j;
		lua_rawgeti(l, -1, j + 1);
		CclGetPos(l, &this->goalPos);
		lua_pop(l, 1);
		// A save written before the goal pull has no "goal-pulled" (written after "tile"): its goal
		// is kept as it is, not pulled again from a tile on the way.
		this->GoalPulled = true;
	} else if (value == "goal-pulled") {
		++j;
		this->GoalPulled = LuaToNumber(l, -1, j + 1) != 0;
		// An order that had begun has made its first search (run 73 S: only that one decides).
		this->Looked = this->GoalPulled;
	} else if (value == "ordered-cycle") {
		++j;
		this->OrderedCycle = static_cast<unsigned long>(LuaToNumber(l, -1, j + 1));
	} else if (value == "going-round") {
		this->GoingRound = true;
	} else {
		return false;
	}
	return true;
}

bool COrder_Move::IsValid() const /* override */
{
	return true;
}

PixelPos COrder_Move::Show(const CViewport &vp, const PixelPos &lastScreenPos) const /* override */
{
	const PixelPos targetPos = vp.TilePosToScreen_Center(this->goalPos);

	Video.FillCircleClip(ColorGreen, lastScreenPos, 2);
	Video.DrawLineClip(ColorGreen, lastScreenPos, targetPos);
	Video.FillCircleClip(ColorGreen, targetPos, 3);
	return targetPos;
}

void COrder_Move::UpdatePathFinderData(PathFinderInput &input) /* override */
{
	const Vec2i tileSize(0, 0);
	input.SetGoal(this->goalPos, tileSize);

	int distance = this->Range;
	if (GameSettings.Inside) {
		CheckObstaclesBetweenTiles(input.GetUnitPos(), this->HasGoal() ? this->GetGoal()->tilePos : this->goalPos, MapFieldRocks | MapFieldForest, &distance);
	}
	input.SetMaxRange(distance);
	input.SetMinRange(0);
}

static inline void applyResidualDisplacementCorrection(short *id, int move) {
	// in case of residual displacement due to non-tile
	// aligned movement, apply correction at movement speed
	if (*id > 0) {
		*id = std::max(0, *id - move);
	} else if (*id < 0) {
		*id = std::min(0, *id + move);
	}
}

static inline void resetDisplacement(CUnit &unit) {
	unit.IX = 0;
	unit.IY = 0;
	unit.ZDisplaced = 0;
}

/**
**  Unit moves! Generic function called from other actions.
**
**  @param unit  Pointer to unit.
**
**  @return      >0 remaining path length, 0 wait for path, -1
**               reached goal, -2 can't reach the goal.
*/
int DoActionMove(CUnit &unit)
{
	Vec2i posd{}; // movement in tile.
	int d{};

	if (unit.Moving != 1
	    && (&unit.Type->Animations->Move != unit.Anim.CurrAnim
	        || (unit.Anim.Wait == 0 && unit.Anim.Anim == 0))) {
		if (unit.Anim.Unbreakable && unit.Moving > 1) {
			// subtile movement, we're finished, but inside an unbreakable animation that we have to finish
			int m = UnitShowAnimationScaled(unit, &unit.Type->Animations->Move, 1) >> 1;
			applyResidualDisplacementCorrection(&unit.IX, m);
			applyResidualDisplacementCorrection(&unit.IY, m);
			if (unit.Anim.Unbreakable) {
				return PF_WAIT;
			} else {
				resetDisplacement(unit);
				bool wasReached = unit.Moving == 3;
				unit.Moving = 0;
				return wasReached ? PF_REACHED : PF_UNREACHABLE;
			}
		}

		UnmarkUnitFieldFlags(unit);
		std::tie(d, posd) = NextPathElement(unit);
		MarkUnitFieldFlags(unit);
		switch (d) {
			case PF_UNREACHABLE: // Can't reach, stop
				if (unit.Player->AiEnabled) {
					AiCanNotMove(unit);
				}
				if (unit.Anim.Unbreakable) {
					unit.Moving = 2;
					return PF_MOVE;
				} else {
					resetDisplacement(unit);
					return d;
				}
			case PF_REACHED: // Reached goal, stop
				if (unit.Anim.Unbreakable) {
					unit.Moving = 3;
					return PF_MOVE;
				} else {
					resetDisplacement(unit);
					return d;
				}
			case PF_WAIT: // No path, wait
				// PathWaitDefaultCycles, or what is left of the expiry (live run 67)
				unit.Wait = unit.pathFinderData->output.LastWait;
				return d;
			default: // On the way moving
				Assert(unit.CanMove());
				unit.Moving = 1;
				break;
		}

		if (unit.Type->MoveType == EMovement::Naval) { // Boat (un)docking?
			bool foundCoast = false;

			for (int i = 0; i < unit.Type->TileWidth && !foundCoast; i++) {
				for (int j = 0; j < unit.Type->TileHeight && !foundCoast; j++) {
					const Vec2i offset(i, j);
					const CMapField &mf_next = *Map.Field(unit.tilePos + posd + offset);
					const CMapField &mf_cur = *Map.Field(unit.tilePos + offset);

					if (mf_cur.CoastOnMap() ^ mf_next.CoastOnMap()) {
						foundCoast = true;
					}
				}
			}
			if (foundCoast) {
				// Should play, even if unit already speaking
				PlayUnitSound(unit, EUnitVoice::Docking, true);
			}
		}
		Vec2i pos = unit.tilePos + posd;
		unit.MoveToXY(pos);

		// Remove unit from the current selection
		if (unit.Selected && !Map.Field(pos)->playerInfo.IsTeamVisible(*ThisPlayer)) {
			if (IsOnlySelected(unit)) { //  Remove building cursor
				CancelBuildingMode();
			}
			if (!ReplayRevealMap) {
				UnSelectUnit(unit);
				SelectionChanged();
			}
		}

		if (unit.ZDisplaced) {
			// A hovering flyer leaves its height in IY; from here on IY is the way to the next tile.
			unit.IY = 0;
			unit.ZDisplaced = 0;
		}
		unit.IX += -posd.x * PixelTileSize.x;
		unit.IY += -posd.y * PixelTileSize.y;
		unit.Frame = unit.Type->StillFrame;
		UnitHeadingFromDeltaXY(unit, posd);
	} else {
		const auto direction =
			unit.pathFinderData->output.Path[unit.pathFinderData->output.Length - 1];
		const int length = PathElementLength(direction, unit.Type->GridStep);
		posd.x = Heading2X[PathElementHeading(direction)] * length;
		posd.y = Heading2Y[PathElementHeading(direction)] * length;
		d = unit.pathFinderData->output.Length;
	}

	unit.pathFinderData->output.Cycles++;// reset have to be manually controlled by caller.
	int move = UnitShowAnimationScaled(unit, &unit.Type->Animations->Move, Map.Field(unit.Offset)->getMoveCost());
	// The per-cycle pixel step from the Move animation ("move N") is authored for the classic
	// 32px logical tile. IX/IY span one tile = PixelTileSize (line above inits to -PixelTileSize),
	// so on a larger (HD) tile the same step takes proportionally more cycles to cross a tile,
	// making units move slower than intended. Scale the step to the tile size to keep real-time
	// speed identical to the classic 32px behaviour (no-op when PixelTileSize == 32).
	move = move * PixelTileSize.x / 32;

	bool reached_next_tile = false;
	// posd may span several tiles (GridStep); the speed per tile stays the same.
	if (posd.x) {
		unit.IX += posd.x > 0 ? move : -move;
		reached_next_tile = ((posd.x < 0) == (unit.IX < 0));
	} else {
		applyResidualDisplacementCorrection(&unit.IX, move);
	}
	if (posd.y) {
		unit.IY += posd.y > 0 ? move : -move;
		reached_next_tile = (reached_next_tile || ((posd.y < 0) == (unit.IY < 0)));
	} else {
		applyResidualDisplacementCorrection(&unit.IY, move);
	}

	// Finished move to next tile, set Moving to 0 so we recalculate the path
	// next frame
	if ((!unit.Anim.Unbreakable && !unit.IX && !unit.IY) || reached_next_tile) {
		unit.Moving = 0;
	}
	return d;
}


/// Longest wait for the unit on the goal to walk off, in cycles.
static constexpr int MaxGoalHolderWaits = 30;
/// Searches in a row a unit waits through (PathWaitExpiryCycles each, about 1 s) while only own
/// units on their way close its way, before the old Range++ retry goes on: a jam that does not
/// clear (two groups head-on in a 1-tile gap with no way round) still ends. Not measured live;
/// 10 s holds a crossing of 20 Footmen through a 1-tile gap (20 steps x 16 frames = 8.3 s).
static constexpr int MaxUnitBlockedWaits = 10;
bool MoveWaitsOutUnitJam = true;
bool MoveGoalPull = true;
bool MoveGoesRoundMates = true;
bool MoveDropsWhenBoxedIn = true; // live run 69 H5 1570 (BOXED_IN_IMPL.md)
bool MoveStopsAtStandingCrowd = true; // live runs 69 F5 / 70 G: no walk round a forest goal (FOREST_EMU_3482.md, run 70 follow-up)
int MoveRangeReasons[AStarFailCount + 1] = {};
int MoveJamWaits = 0;
bool MoveFirstLookDecides = true; // live runs 72 / 73 / 69 / 74: "a way now (round, too) or drop" (CALIB_RUN73.md S, CALIB_RUN74.md)
bool MoveHeadOnClosedDrops = true; // live run 73 R: a closed 1-tile road jams with idle units (CALIB_RUN73.md R)
int MoveFirstLookDrops = 0;
int MoveHeadOnClosedDropCount = 0;

/// Does an own unit next to `unit` walk under a Move order given in the same cycle as the unit's
/// or before it (a mate of the same group order)? Live run 72: mates ordered with the unit let it
/// through (m6), a mate ordered after it does not (m4, m5). Since run 73 (S3: same frame, the rear
/// dropped) the first look decides (MoveFirstLookDecides); this stays for a later boxed search and
/// for the switch off.
static bool BoxHasMateOrderedWithIt(const CUnit &unit, unsigned long orderedCycle)
{
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			const Vec2i pos = unit.tilePos + Vec2i(dx, dy);
			if ((!dx && !dy) || !Map.Info.IsPointOnMap(pos)) {
				continue;
			}
			const CUnit *mate = UnitOnMapTile(pos, unit.Type->MoveType);
			if (mate == nullptr || mate == &unit || mate->Player != unit.Player
			    || mate->CurrentAction() != UnitAction::Move) {
				continue;
			}
			if (static_cast<const COrder_Move *>(mate->CurrentOrder())->GetOrderedCycle() <= orderedCycle) {
				return true;
			}
		}
	}
	return false;
}

/// Is this a land unit of one tile, moving tile by tile, under a player's command?
static bool IsPlayerLandUnitOfOneTile(const CUnit &unit)
{
	const CUnitType &type = *unit.Type;
	return !unit.Player->AiEnabled && type.MoveType == EMovement::Land && type.TileWidth == 1
	       && type.TileHeight == 1 && type.GridStep == 1;
}

/**
**  Is the goal close by and held by a unit of the same player that has a move order to
**  another tile and has not taken its first step yet? A group that was just sent off: the
**  tile is about to be free.
*/
static bool GoalHolderLeaves(const CUnit &unit, const Vec2i &goalPos)
{
	if (!IsPlayerLandUnitOfOneTile(unit) || !Map.Info.IsPointOnMap(goalPos)
		|| std::abs(goalPos.x - unit.tilePos.x) > 4 || std::abs(goalPos.y - unit.tilePos.y) > 4) {
		return false;
	}
	const CUnit *holder = UnitOnMapTile(goalPos, unit.Type->MoveType);
	return holder != nullptr && holder != &unit && holder->Player == unit.Player && holder->Moving == 0
	       && holder->CurrentAction() == UnitAction::Move
	       && holder->CurrentOrder()->GetGoalPos() != holder->tilePos;
}

/// The wait of a player's land unit whose next tile is held by a unit on its way: it steps in
/// 15 frames late and walks on (ORIG50 G3m_1.1 / 1.2: f173 -> f188, f174 -> f189; 2 of 2).
static constexpr int CrossingUnitWait = 15;

/**
**  The goal's neighbour a player's land unit ends on when its goal tile is held by a standing
**  unit: the one on the left of its heading (ORIG50 s.4, minimap orders onto a standing Footman:
**  from 3 and 8 tiles east it ends south of the goal (4 of 4), from 3 tiles north east of it (1);
**  the tile on its way was passed). The heading is the longer axis of the way; a way as long on
**  both axes has no live case and gets none (0, 0).
*/
Vec2i OccupiedGoalSide(const Vec2i &unitPos, const Vec2i &goal)
{
	const int dx = goal.x - unitPos.x;
	const int dy = goal.y - unitPos.y;
	if (std::abs(dx) == std::abs(dy)) {
		return Vec2i(0, 0);
	}
	const Vec2i heading = std::abs(dx) > std::abs(dy) ? Vec2i(dx > 0 ? 1 : -1, 0) : Vec2i(0, dy > 0 ? 1 : -1);
	// map y grows southward: facing west the left hand points south
	return Vec2i(heading.y, -heading.x);
}

/// A unit of the same player with a Move of its own still to go (a group mate sent off with it,
/// or one waiting in the same queue): it is about to leave the tile it holds.
static bool HolderHasWayToGo(const CUnit &unit, const CUnit &holder)
{
	return holder.Player == unit.Player && holder.CurrentAction() == UnitAction::Move
	       && holder.CurrentOrder()->GetGoalPos() != holder.tilePos;
}

/// Live run 76 B (CALIB_RUN76.md): the core of a 3 x 3 group sent off in one right click walked out
/// in 4 of 4 trials (B1 / B5: the core had the lowest index; B5: it started in the same frame as the
/// mate whose tile it took and stepped 1 frame after that mate's step), while a core ordered alone in
/// a ring of idle units dropped 3 frames after its order (B4, as run 72 B1). The core waits for its
/// mates only while every tile round it is terrain or held by an own unit with a Move of its own
/// still to go, ordered in the core's cycle or before (one click). Ceiling: one step time (not
/// measured: live the core never waited more than 1 frame past the mate's step).
/// Live run 77 C (CALIB_RUN77.md; the same 3 x 3 block with 5 idle Peasants on the tiles beyond its
/// north-west corner, one click to the north-west, 3 trials, 27 unit runs): no unit dropped; the units
/// whose way was held by mates that were held themselves waited 19, 33, 34, 35, 49 and 64 frames from
/// their order and walked - the core 34 (C1), 3 (C2, C3); a corner boxed by 5 idle units and 3 mates 35
/// (C3), a side unit boxed by 2 idle units and mates 64 (C2). So idle units may be among the walls as
/// long as a mate of its click is round it, and the ceiling is the longest wait seen, 64 frames (4
/// steps; past it not measured). Ordered alone in an idle ring (run 76 B4, run 72 B1) or with a mate
/// ordered after it (run 72 m4 / m5) it still drops.
static constexpr int CoreWaitsForItsClickCycles = 64;
bool MoveCoreWaitsForItsClick = true;

static bool BoxedOnlyByMatesOfItsClick(const CUnit &unit, unsigned long orderedCycle)
{
	const tile_flags terrain = unit.Type->MovementMask & ~(MapFieldLandUnit | MapFieldSeaUnit | MapFieldAirUnit);
	int mates = 0;
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			const Vec2i pos = unit.tilePos + Vec2i(dx, dy);
			if ((!dx && !dy) || !Map.Info.IsPointOnMap(pos) || Map.Field(pos)->CheckMask(terrain)) {
				continue;
			}
			const CUnit *mate = UnitOnMapTile(pos, unit.Type->MoveType);
			if (mate == nullptr || mate == &unit) {
				return false; // a free tile
			}
			if (!HolderHasWayToGo(unit, *mate)) {
				continue; // a unit standing for good: a wall (live run 77 C)
			}
			if (static_cast<const COrder_Move *>(mate->CurrentOrder())->GetOrderedCycle() > orderedCycle) {
				return false; // a mate ordered later (run 72 m4 / m5)
			}
			++mates;
		}
	}
	return mates > 0;
}

/// The tile of the cached step a unit waits for after NextPathElement answered PF_WAIT on a
/// blocked step (not the wait inside an unbreakable animation: Moving is then 2 or 3).
static bool BlockedNextTile(const CUnit &unit, Vec2i &next)
{
	const PathFinderOutput &output = unit.pathFinderData->output;
	if (unit.Moving != 0 || output.Length == 0) {
		return false;
	}
	const int element = output.Path[output.Length - 1];
	const int length = PathElementLength(element, unit.Type->GridStep);
	next = unit.tilePos + Vec2i(Heading2X[PathElementHeading(element)] * length,
	                            Heading2Y[PathElementHeading(element)] * length);
	return next != unit.tilePos && Map.Info.IsPointOnMap(next);
}

/**
**  Two land units of one player on Move orders met head-on: the holder of this unit's next tile
**  waits for this unit's tile. Live run 54 item 5 (a 1-tile road between Farms, two Peasants sent
**  through it opposite ways): the unit ordered second backs out of the whole road and goes round,
**  the first walks through behind it - 3 of 3 (L2, L3, L4; not the deeper one, L4); no swap, no
**  deadlock. True when this unit is the one to back out: its order is the later one. Orders of the
**  same cycle (an older save: 0) are left to the waits below, as before.
**  Live run 69 H (two groups of 4 Footmen head-on through a 1-tile gap, 4 trials, 32 unit runs):
**  one face-to-face pair in the gap (H6); the unit that came second (1576, on 38,42 at f360, 7
**  frames after 1569 stood on 38,41 waiting for that tile) backed out at its next step (17 frames,
**  0.44 s) and went round W (23 steps); 1569 stood 22 frames more and walked on through. With
**  MoveHeadOnLaterArrivalYields the unit that finds the other already waiting for its own tile
**  (output.Fast: it made a blocked call before) - the later to arrive - backs out, unless the other
**  was ordered later (then that one backs out at its next poll: run 54 L2-L4, 3 of 3, the later
**  order; on the host lane 54 scene the earlier order arrives later). Orders of one cycle (one group
**  order, no live case) go by the arrival alone; this replaces the direction rule of round 3.4.81.
*/
bool MoveHeadOnLaterArrivalYields = true;

/// The holder waits for its cached step onto this unit's tile (it made a blocked call: Fast).
static bool WaitsForMyTile(const CUnit &unit, const CUnit &holder)
{
	Vec2i holderNext;
	return BlockedNextTile(holder, holderNext) && holderNext == unit.tilePos && holder.pathFinderData != nullptr
	       && holder.pathFinderData->output.Fast != 0;
}

/// The holder's Move was given after this unit's (orders of one cycle - one group order, or an
/// older save's 0 - are not told apart).
static bool OrderedLater(const CUnit &holder, unsigned long orderedCycle)
{
	return static_cast<const COrder_Move *>(holder.CurrentOrder())->GetOrderedCycle() > orderedCycle;
}

static bool BacksOutHeadOn(const CUnit &unit, const CUnit &holder, unsigned long orderedCycle)
{
	if (holder.Player != unit.Player || !IsPlayerLandUnitOfOneTile(holder)
	    || holder.CurrentAction() != UnitAction::Move) {
		return false;
	}
	if (MoveHeadOnLaterArrivalYields) {
		return WaitsForMyTile(unit, holder) && !OrderedLater(holder, orderedCycle);
	}
	Vec2i holderNext;
	if (!BlockedNextTile(holder, holderNext) || holderNext != unit.tilePos) {
		return false;
	}
	const unsigned long holderOrdered = static_cast<const COrder_Move *>(holder.CurrentOrder())->GetOrderedCycle();
	return holderOrdered < orderedCycle;
}

/// A land unit of one tile of the same player on a Move given in the same cycle as this unit's
/// (one group order). Orders of cycle 0 (an older save, or a test that does not count cycles)
/// are not told apart and are not mates.
static bool IsGroupMate(const CUnit &unit, const CUnit &holder, unsigned long orderedCycle)
{
	return orderedCycle != 0 && holder.Player == unit.Player && IsPlayerLandUnitOfOneTile(holder)
	       && holder.CurrentAction() == UnitAction::Move
	       && static_cast<const COrder_Move *>(holder.CurrentOrder())->GetOrderedCycle() == orderedCycle;
}

/// The longest way round a moving mate a unit takes, against the way left through it. Live run 67
/// M1 (4 Footmen, one order through a 1-tile gap; 3 trials, 12 unit runs): 5 of the 6 rear runs
/// went round at once when the gap tile was held by a moving mate (4 west, 1 east), the longest
/// 24 steps round from 37,43 against 6 left through the gap (t1; t3 the same), 37 against ~10 from
/// the start (t2 east). 1 rear run waited 1.24 s and took the gap. No case of a longer detour:
/// beyond 4 x the way through, the unit waits as before (CrossingUnitWait).
static constexpr int MateDetourFactor = 4;

/**
**  Live run 67 M1: a player's land unit whose next step is held by a moving mate of the same
**  group order searches a way round that mate (as a wall) at once and takes it when it is at
**  most MateDetourFactor x the way left through the held tile.
*/
static bool GoesRoundMate(CUnit &unit, const CUnit &holder)
{
	const PathFinderOutput &output = unit.pathFinderData->output;
	// the blocked step is stored twice (NextPathElement on PF_WAIT): one of them is not a step
	const int left = std::max(1, int(output.Length) - 1 + int(output.OverflowLength));
	UnmarkUnitFieldFlags(unit);
	const bool round = PathRoundUnit(unit, holder, MateDetourFactor * left);
	MarkUnitFieldFlags(unit);
	return round;
}

bool MoveJamTakesWayThroughMates = true;
bool MoveHeadOnStepsAside = true;

/// Two land units of one player on Move orders, each waiting for the other's tile (both stand).
static bool HeadOnMutual(const CUnit &unit, const CUnit &holder)
{
	if (holder.Player != unit.Player || !IsPlayerLandUnitOfOneTile(holder)
	    || holder.CurrentAction() != UnitAction::Move || holder.Moving != 0) {
		return false;
	}
	Vec2i holderNext;
	return BlockedNextTile(holder, holderNext) && holderNext == unit.tilePos;
}

/// A free neighbour of the unit's tile other than `from`, as far from `from` as there is one
/// (Chebyshev, then the square of the distance; ties: the first in row order).
static bool AsideTile(const CUnit &unit, const Vec2i &from, Vec2i &aside)
{
	int best = -1;
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			const Vec2i p = unit.tilePos + Vec2i(dx, dy);
			if ((dx == 0 && dy == 0) || p == from || !Map.Info.IsPointOnMap(p) || !UnitCanBeAt(unit, p)) {
				continue;
			}
			const int cx = std::abs(p.x - from.x);
			const int cy = std::abs(p.y - from.y);
			const int score = std::max(cx, cy) * 64 + cx * cx + cy * cy;
			if (score > best) {
				best = score;
				aside = p;
			}
		}
	}
	return best >= 0;
}

/// Of a head-on pair the one that backs out first: the later to arrive, the one that finds the
/// other already waiting for its tile, the later order first (live run 69 H6, run 54; as
/// BacksOutHeadOn). Without
/// MoveHeadOnLaterArrivalYields (round 3.4.81, UNMEASURED): the later order, and for orders of the
/// same cycle the one whose held step goes south (or east on one row).
static bool YieldsFirst(const CUnit &unit, const CUnit &holder, unsigned long orderedCycle)
{
	if (MoveHeadOnLaterArrivalYields) {
		return WaitsForMyTile(unit, holder) && !OrderedLater(holder, orderedCycle);
	}
	const unsigned long holderOrdered = static_cast<const COrder_Move *>(holder.CurrentOrder())->GetOrderedCycle();
	if (holderOrdered != orderedCycle) {
		return orderedCycle > holderOrdered;
	}
	const Vec2i d = holder.tilePos - unit.tilePos;
	return d.y > 0 || (d.y == 0 && d.x > 0);
}

/**
**  Round 3.4.81 item 2 (4 + 4 units head-on through a 1-tile gap with no way round: a unit stood
**  in the gap and nobody moved for 89 s): of two units each waiting for the other's tile, one
**  steps onto a free neighbour away from the other, so the other walks on; then it searches its
**  goal again. The one of YieldsFirst steps when it has a free neighbour, else the other one does.
**  Live run 69 H: the unit that came second backs out at its next step (H6, 0.44 s) - here a step
**  back, as far from the other as there is, when no way round exists (BacksOutHeadOn otherwise).
**  Without a way round: live run 73 R (closed 1-tile road, 3 head-on trials) nobody steps aside or
**  backs out, the orders are dropped (MoveHeadOnClosedDrops, before this in COrder_Move::Execute);
**  a closed face-off ends there first; what is left here (the room made behind a pair, a pair seen
**  by one side only) is the round 3.4.81 rule (UNMEASURED).
*/
/**
**  Must the unit `h` (standing, waiting for a held step) get room behind it? It has no free
**  neighbour, and it is the one of a head-on pair to step aside while the other has no free
**  neighbour either, or it waits for a mate of its order that must get room (a file of one group
**  in a gap 2 deep or more). Followed at most 8 units up the file.
*/
static bool MustMakeRoom(const CUnit &h, int depth)
{
	if (depth > 8 || !IsPlayerLandUnitOfOneTile(h) || h.CurrentAction() != UnitAction::Move || h.Moving != 0) {
		return false;
	}
	Vec2i next;
	if (!BlockedNextTile(h, next)) {
		return false;
	}
	const CUnit *h2 = UnitOnMapTile(next, h.Type->MoveType);
	if (h2 == nullptr || h2 == &h || h2->Player != h.Player || !IsPlayerLandUnitOfOneTile(*h2)
	    || h2->CurrentAction() != UnitAction::Move) {
		return false;
	}
	Vec2i aside;
	if (AsideTile(h, h2->tilePos, aside)) {
		return false; // it steps aside itself
	}
	const unsigned long hOrdered = static_cast<const COrder_Move *>(h.CurrentOrder())->GetOrderedCycle();
	if (HeadOnMutual(h, *h2)) {
		return YieldsFirst(h, *h2, hOrdered) && !AsideTile(*h2, h.tilePos, aside);
	}
	return IsGroupMate(h, *h2, hOrdered) && MustMakeRoom(*h2, depth + 1);
}

static bool HeadOnStepAside(CUnit &unit, const CUnit &holder, unsigned long orderedCycle)
{
	if (!MoveHeadOnStepsAside) {
		return false;
	}
	Vec2i mine;
	if (!HeadOnMutual(unit, holder)) {
		// behind a mate of its order that is stuck head-on with no free neighbour: make room
		if (IsGroupMate(unit, holder, orderedCycle) && MustMakeRoom(holder, 0)
		    && AsideTile(unit, holder.tilePos, mine)) {
			PathStepAside(unit, mine - unit.tilePos);
			return true;
		}
		return false;
	}
	Vec2i theirs;
	const bool canMine = AsideTile(unit, holder.tilePos, mine);
	const bool canTheirs = AsideTile(holder, unit.tilePos, theirs);
	const bool yield = YieldsFirst(unit, holder, orderedCycle) ? canMine : (canMine && !canTheirs);
	if (!yield) {
		return false;
	}
	PathStepAside(unit, mine - unit.tilePos);
	return true;
}

/// Is there a way for the unit round the standing holder (the holder a wall)? It is cached.
static bool WayRoundHolder(CUnit &unit, const CUnit &holder)
{
	UnmarkUnitFieldFlags(unit);
	const bool round = PathRoundUnit(unit, holder, 1 << 20);
	MarkUnitFieldFlags(unit);
	return round;
}

bool HarvestTurnsAway = true;

/**
**  Live run 68 M3 (8 Peasants harvesting through a 2-tile lane, 1 trial, 69 legs): a worker whose
**  next step is held does not queue. 0 tile swaps, 15 side-by-side passes in the lane, 13 legs
**  round the west of the forest (24-45 steps against 13-23 through the lane), 22 turn-backs (a
**  step toward the lane, a step back, then round); the longest stand while walking 72 frames.
**  Here, for a player's land worker of one tile on its way to a mine or a depot whose cached step
**  is held after NextPathElement answered PF_WAIT:
**  - held by a unit that stands and is not itself waiting for a step (a mate in its exit pause,
**    an idle unit): the way is dropped and searched next cycle with it as a wall (as COrder_Move,
**    ORIG50 s.0 / live run 67 M2b);
**  - held by a unit on its way (moving, or waiting for a step of its own): one search with that
**    unit as a wall, taken when at most MateDetourFactor x the way left (the other tile of the
**    lane - a pass side by side - or round the forest); else the old wait (CrossingUnitWait).
**  Returns true when it changed the way or the wait.
*/
bool HarvestStepHeld(CUnit &unit)
{
	Vec2i next;
	if (!HarvestTurnsAway || !IsPlayerLandUnitOfOneTile(unit)
	    || !BlockedNextTile(unit, next)) {
		return false;
	}
	const CUnit *holder = UnitOnMapTile(next, unit.Type->MoveType);
	if (holder == nullptr || holder == &unit || holder->Type->Building) {
		return false;
	}
	const bool waitsForStep = holder->Moving == 0 && holder->pathFinderData != nullptr
	                          && holder->pathFinderData->output.Fast != 0
	                          && (holder->CurrentAction() == UnitAction::Resource
	                              || holder->CurrentAction() == UnitAction::Move);
	if (holder->Moving == 0 && !waitsForStep && !HolderHasWayToGo(unit, *holder)) {
		unit.pathFinderData->output.Length = 0;
		unit.pathFinderData->output.Fast = 0;
		unit.pathFinderData->output.Waited = 0;
		unit.Wait = 1;
		return true;
	}
	if (GoesRoundMate(unit, *holder)) {
		unit.Wait = 1;
		return true;
	}
	SetPathWait(unit, CrossingUnitWait);
	return true;
}

/**
**  The places round a goal tile T held by a standing unit, in the order a folded group fills
**  them, relative to the heading of the unit that comes: behind, behind-left, left, behind-right,
**  front-left, right, front, front-right (ORIG51 s.4: 4 units T, b, bl, l from the east and from
**  the south, 6 of 6; 9 units the full 3 x 3, this sequence in 3 of 6, its first three in 6 of 6;
**  ORIG50 G1e 3 of 3, G1n 2 of 3, L4m1 / L8m2 2 of 2; G1w 3 of 3 ended T, b, bl, behind-right).
**  The heading is the longer axis of the way to T; a way as long on both axes gives no places.
**  Ships take the places of their grid, `step` tiles apart (ORIG51 s.3 SL4a-c: four Destroyers
**  folded from the west ended T, (-2,0), (-2,-2), (0,-2), 3 of 3).
*/
int RingPlacesAround(const Vec2i &unitPos, const Vec2i &goal, Vec2i places[8], int step)
{
	Vec2i left = OccupiedGoalSide(unitPos, goal);
	if (left == Vec2i(0, 0)) {
		return 0;
	}
	left = Vec2i(left.x * step, left.y * step);
	const Vec2i ahead(-left.y, left.x); // the heading: left turned back right
	const Vec2i behind(-ahead.x, -ahead.y);
	const Vec2i right(-left.x, -left.y);
	places[0] = goal + behind;
	places[1] = goal + behind + left;
	places[2] = goal + left;
	places[3] = goal + behind + right;
	places[4] = goal + ahead + left;
	places[5] = goal + right;
	places[6] = goal + ahead;
	places[7] = goal + ahead + right;
	return 8;
}

/// Is another unit of the same player on a Move to this tile (a place in the ring it has taken)?
static bool RingPlaceTaken(const CUnit &unit, const Vec2i &pos)
{
	for (const CUnit *other : unit.Player->GetUnits()) {
		if (other != &unit && !other->Removed && other->CurrentAction() == UnitAction::Move
		    && !other->CurrentOrder()->Finished && other->CurrentOrder()->GetGoalPos() == pos) {
			return true;
		}
	}
	return false;
}

/// A player's ship of one tile on the 2-tile grid that is sent with a group (not a transport).
static bool IsPlayerGroupShip(const CUnit &unit)
{
	const CUnitType &type = *unit.Type;
	return !unit.Player->AiEnabled && type.MoveType == EMovement::Naval && type.TileWidth == 1
	       && type.TileHeight == 1 && type.GridStep == 2 && !type.CanTransport();
}

/**
**  A player's land unit (or ship) on its way to a goal tile that a standing unit now holds (the
**  first of a folded group has arrived on it): the unit's goal becomes the first place of the
**  ring round the goal that is free and not taken by another unit of its player. Nothing
**  changes when no place is free (the old ring end: the range widens).
*/
void COrder_Move::TakeRingPlace(CUnit &unit)
{
	if (this->HasGoal() || this->Range != 0 || unit.Moving != 0
	    || !(IsPlayerLandUnitOfOneTile(unit) || IsPlayerGroupShip(unit)) || !Map.Info.IsPointOnMap(this->goalPos)) {
		return;
	}
	const int step = unit.Type->GridStep;
	const Vec2i goalTile(this->goalPos.x - this->goalPos.x % step, this->goalPos.y - this->goalPos.y % step);
	if (goalTile == unit.tilePos) {
		return;
	}
	const CUnit *holder = UnitOnMapTile(goalTile, unit.Type->MoveType);
	if (holder == nullptr || holder == &unit || holder->Moving != 0 || HolderHasWayToGo(unit, *holder)) {
		return;
	}
	Vec2i places[8];
	const int count = RingPlacesAround(unit.tilePos, goalTile, places, step);
	for (int i = 0; i != count; ++i) {
		const Vec2i &place = places[i];
		if (!Map.Info.IsPointOnMap(place)) {
			continue;
		}
		if (place == unit.tilePos || (UnitCanBeAt(unit, place) && !RingPlaceTaken(unit, place))) {
			this->goalPos = place;
			if (this->GoingRound && unit.pathFinderData->output.Length > 0) {
				// Live run 67 M1: on its way round a mate the unit goes on round (the 5 rear runs
				// that went round arrived round, while the front pair already stood on the goal):
				// the new goal is beside the old one, the cached way is kept, not searched anew
				// back through the gap.
				this->UpdatePathFinderData(unit.pathFinderData->input);
				unit.pathFinderData->input.PathRecalculated();
			}
			return;
		}
	}
}

void COrder_Move::Execute(CUnit &unit) /* override */
{
	Assert(unit.CanMove());

	if (IsWaiting(unit)) {
		return;
	}
	StopWaiting(unit);
	if (this->FirstLookClosed) {
		// Its first search found the way closed by units: no second look, the order ends
		// MoveBoxedDropCycles after it was given (live run 73 S1 / S4: the front stepped 1 frame
		// after the rear had dropped; run 72 B1: the box opened later, the unit stayed).
		if (GameCycle >= this->OrderedCycle + MoveBoxedDropCycles) {
			++MoveFirstLookDrops;
			this->Finished = true;
			if (!unit.JustMoved) {
				unit.JustMoved = 0b111;
			}
		}
		return;
	}
	if (!this->GoalPulled) {
		// The order has just become the unit's order: a blocked goal is pulled toward the unit
		// from where it stands now (0x4D8970 -> 0x4C2880 -> 0x4C2630).
		this->GoalPulled = true;
		if (!this->HasGoal()) {
			if (MoveGoalPull) {
				this->goalPos = PullGoalTowardUnit(unit, this->goalPos);
			}
			// The first path step finds no way at all (the first line step blocked and nothing free
			// up to the blocked goal): the order drops to idle where the unit stands (0x4D5E20 ->
			// 0x4C2A9B; ORIG48 trials 5, 7, 8, 14).
			if (PathStepFindsNoWay(unit, this->goalPos)) {
				this->Finished = true;
				return;
			}
			// Far from a goal with no reachable free tile round it (live run 70 G: walked straight,
			// stopped at the near edge): pulled once more to the edge the unit can reach.
			if (MoveGoalPull && IsPlayerLandUnitOfOneTile(unit)) {
				this->goalPos = PullGoalIntoReach(unit, this->goalPos);
			}
			// A goal held by a standing unit (not one about to leave it): the unit walks to the
			// goal's neighbour on the left of its heading and stays there, no retry (ORIG50 s.4).
			if (this->Range == 0 && IsPlayerLandUnitOfOneTile(unit) && Map.Info.IsPointOnMap(this->goalPos)) {
				const CUnit *holder = UnitOnMapTile(this->goalPos, unit.Type->MoveType);
				if (holder != nullptr && holder != &unit && holder->Moving == 0
				    && !HolderHasWayToGo(unit, *holder)) {
					const Vec2i side = OccupiedGoalSide(unit.tilePos, this->goalPos);
					const Vec2i beside = this->goalPos + side;
					if (side != Vec2i(0, 0) && Map.Info.IsPointOnMap(beside)
					    && (beside == unit.tilePos || UnitCanBeAt(unit, beside))) {
						this->goalPos = beside;
					}
				}
			}
		}
	}
	TakeRingPlace(unit);
	// FIXME: (mr-russ) Make a reachable goal here with GoalReachable ...

	AStarLastFailByUnits = false;
	AStarLastFailReason = AStarFailNone;
	const int searchesBefore = AStarSearchCalls;
	const int moveResult = DoActionMove(unit);
	const int failReason = AStarSearchCalls == searchesBefore ? int(AStarFailCount) : AStarLastFailReason;
	const bool jam = AStarLastFailByUnits;
	const bool boxedIn = AStarSearchCalls != searchesBefore && AStarLastFailBoxed;
	const bool firstLook = !this->Looked && AStarSearchCalls != searchesBefore;
	if (AStarSearchCalls != searchesBefore) {
		this->Looked = true;
	}
	switch (moveResult) { // reached end-point?
		case PF_UNREACHABLE:
			if (this->Range == 0 && this->GoalHolderWaits < MaxGoalHolderWaits
				&& GoalHolderLeaves(unit, this->goalPos)) {
				// The goal is held by a unit that is leaving it: try again.
				this->GoalHolderWaits++;
				break;
			}
			if (MoveFirstLookDecides && MoveDropsWhenBoxedIn && firstLook && !this->Stepped
			    && IsPlayerLandUnitOfOneTile(unit)
			    && (boxedIn || failReason == AStarFailFloodUnitsOnTheWay || failReason == AStarFailFloodStandingUnits)) {
				// "A way now or drop" (live runs 72, 73, 69; CALIB_RUN73.md S). Each unit decides once,
				// at its first look; units are handled one after another, so a mate that has stepped
				// off by then (ordered earlier, or handled earlier in the same cycle) frees the way:
				// - same frame, rear handled first: dropped, never moved (run 73 S3: rear 1567 below
				//   the front 1576; front stepped f283, rear dropped f283);
				// - same frame, mate handled first: walks (run 72 m6, X 1576 above the mate 1573:
				//   stepped in the same frame; run 69 F5 1571 above 1568, stepped as it left);
				// - rear ordered 1-2 frames first: dropped 3 frames after its order (run 73 S1 f275 ->
				//   f278, S4 f280 -> f283; run 72 m4 / m5; run 69 H5 1570);
				// - front ordered 1 frame first: the rear stepped 1 frame after the front's step (run 73
				//   S2) - here the front has stepped before the rear's first look, so it walks too.
				// 4 drop / 1 walk in run 73 + run 72 m4-m6 + run 69 = 6 drop / 3 walk, no exception.
				// The original takes its first step 3 frames after the order; ours at the first
				// Execute, so the same order of steps, 3 cycles earlier. The drop itself stays at
				// MoveBoxedDropCycles (2-3 frames live, 8 of 9 at 3); a way that opens before it is
				// not taken (S1: the front stepped 1 frame after the rear's drop). Old rule: switch off.
				// Only a way closed by units counts (the region check keeps terrain out of it).
				// Reconciled with live run 74 (CALIB_RUN74.md): "closed" is the whole search with the
				// standing units (idle or not yet stepped) as walls, not the next tile - a way round of
				// any length is a way. 4a (a column of 4 in one click through a 1-tile neck, a ~35-step
				// way round the forest): 0 of 8 dropped, the mates whose neck tile was held went round at
				// once (one stood 33 frames: its first step was taken by a mate on the same way round,
				// the crossing wait), the ones whose next tile was free passed; 4b (2-tile neck): 8 of 8
				// passed by the second lane, stands of 31-32 frames (a step + CrossingUnitWait). Run 73 S
				// dropped because the pocket behind the front was sealed: no way at all.
				// Live run 76 B: closed in on all sides only by mates of its own click (on their way),
				// the unit looks again next cycle instead (a core walks out 1 frame after a mate's
				// step); S3's rear had free tiles round it and is not affected.
				if (MoveCoreWaitsForItsClick && GameCycle < this->OrderedCycle + CoreWaitsForItsClickCycles
				    && BoxedOnlyByMatesOfItsClick(unit, this->OrderedCycle)) {
					this->Looked = false;
					break;
				}
				if (GameCycle >= this->OrderedCycle + MoveBoxedDropCycles) {
					++MoveFirstLookDrops;
					this->Finished = true;
				} else {
					this->FirstLookClosed = true;
				}
				break;
			}
			if (MoveDropsWhenBoxedIn && boxedIn && !this->Stepped && IsPlayerLandUnitOfOneTile(unit)
			    && (!jam || !MoveWaitsOutUnitJam || this->UnitBlockedWaits >= MaxUnitBlockedWaits
			        || !BoxHasMateOrderedWithIt(unit, this->OrderedCycle))) {
				// Live run 72: "a free tile now or drop", mates on their way or not. B1 (8 idle round,
				// 3 / 3): dropped 2-3 frames after the order, never moved, stayed idle when the box
				// opened. One ordered mate in the box: X dropped when its order came 2 frames before
				// the mate's (m4, m5, also run 69 H5 1570), walked through the gap when both orders
				// came in the same frame and the mate stepped off (m6, also run 69 F5 1571). So the
				// unit looks again each cycle until MoveBoxedDropCycles after its order, then drops
				// (was: a box with mates on their way waited the jam wait, 10 x 38 cycles).
				// A box with a mate on its way ordered with the unit or before it (one right click: m6,
				// F5 1571 walked) keeps the jam wait below; one of idle units, or of mates ordered
				// after it (m4, m5), drops. Host forest 3482: a group order's inner units are boxed
				// by mates of the same click that step off later than 3 cycles.
				if (this->UnitBlockedWaits < MaxUnitBlockedWaits
				    && GameCycle < this->OrderedCycle + MoveBoxedDropCycles) {
					break;
				}
				// Boxed in before its first step: no tile round the unit to step on and at least one
				// of the units round it stands for good. The order ends where the unit stands - no
				// jam wait, no Range++ ramp (host "boxed 3483 report": the ramp walked a unit whose
				// box opened to a ring 40 short). Idle units never step aside (run 69 H2 / H5, run 72
				// B1: none of 8 changed tile or order). A unit that already walked keeps the old
				// handling (crowd stop).
				this->Finished = true;
				break;
			}
			if (MoveWaitsOutUnitJam && jam && this->UnitBlockedWaits < MaxUnitBlockedWaits) {
				++MoveJamWaits;
				// No way only because own units on their way stand in it (a group crossing this
				// one in a gap, mates queued in a neck): the goal is not given up - the unit waits
				// the re-route time and searches again (round 3.4.80b: Range++ every cycle of such
				// a jam stopped units 14-27 tiles short of a reachable goal, for good).
				this->UnitBlockedWaits++;
				unit.Wait = PathWaitExpiryCycles;
				if (MoveJamTakesWayThroughMates && IsPlayerLandUnitOfOneTile(unit)
				    && (failReason == AStarFailFloodUnitsOnTheWay || failReason == AStarFailGoalUnitsOnTheWay)) {
					// Round 3.4.81: the way through the waiting units is kept, so the unit steps on
					// as soon as its next tile is free and a head-on pair sees each other
					// (HeadOnStepAside); a goal tile held by a unit about to leave it (the other
					// group of a head-on) no longer holds the whole group 10 waits. UNMEASURED.
					UnmarkUnitFieldFlags(unit);
					const bool through = PathThroughWaitingMates(unit);
					MarkUnitFieldFlags(unit);
					if (through) {
						unit.Wait = 1;
					}
				}
				break;
			}
			if (MoveStopsAtStandingCrowd && this->Range > 0 && !jam && IsPlayerLandUnitOfOneTile(unit)
			    && (failReason == AStarFailSimple || failReason == AStarFailGoalFull
			        || failReason == AStarFailFloodStandingUnits)
			    && std::max(std::abs(unit.tilePos.x - this->goalPos.x), std::abs(unit.tilePos.y - this->goalPos.y))
			           <= this->Range + 3) {
				// The goal is blocked (forest, a building: the range already widened once), every
				// tile round it in reach is held by units standing for good and the unit is near
				// (within 3 of that ring; live H2 / H5 dropped at d4 / d3 from the goal): the order
				// ends where the unit stands, no
				// wider ring and no walk round to a far side (a unit farther off widens as before,
				// so a long file does not stop on its way) (live run 69: F2 5 of 9 dropped in 3-4 frames when the
				// neighbours of their goals were held by mates; F5 nobody went round to the free far
				// side, all idle 3.5 s; H2 / H5 a unit whose way near its goal is held by a standing
				// unit drops the order for good). Round 3.4.82: the widening ring sent units of a
				// group 18-30 steps round the forest and kept a crowd shuffling 20-29 s.
				this->Finished = true;
				break;
			}
			++MoveRangeReasons[failReason];
			// Some tries to reach the goal
			this->Range++;
			break;

		case PF_REACHED:
			// Beside a blocked (pulled) goal after a walk: one more step onto the next free tile
			// round the goal, straight on / 45 left / 45 right (PULL-10 calibration, live run 71 G
			// and ORIG48 / ORIG55; PulledGoalStopStep). The new goal is free, so the next arrival
			// ends the order.
			if (MoveGoalPull && MoveGoalPullStopStep && this->Stepped && !this->HasGoal()
			    && IsPlayerLandUnitOfOneTile(unit) && Map.Info.IsPointOnMap(this->goalPos)
			    && std::max(std::abs(unit.tilePos.x - this->goalPos.x), std::abs(unit.tilePos.y - this->goalPos.y)) == 1
			    && Map.Field(this->goalPos)->CheckMask(tile_flags(unit.Type->MovementMask)
			                                           & ~(MapFieldLandUnit | MapFieldSeaUnit))) {
				const int dir = ((unit.Direction + NextDirection / 2) & 0xFF) / NextDirection;
				const Vec2i heading(Heading2X[dir], Heading2Y[dir]);
				const Vec2i next = PulledGoalStopStep(unit.tilePos, heading, this->goalPos, [&](const Vec2i &p) {
					return Map.Info.IsPointOnMap(p) && UnitCanBeAt(unit, p);
				});
				if (next != unit.tilePos) {
					this->goalPos = next;
					this->Range = 0;
					unit.pathFinderData->output.Length = 0;
					break;
				}
			}
			this->Finished = true;
			break;

		case PF_WAIT:
		{
			// A unit on a grid aims at the grid tile of the goal and is next to it one step away.
			const int step = unit.Type->GridStep;
			const Vec2i goalTile(this->goalPos.x - this->goalPos.x % step, this->goalPos.y - this->goalPos.y % step);
			const CUnit *blocker = UnitOnMapTile(goalTile, unit.Type->MoveType);
			if (blocker) {
				const int distToBlocker = MapDistanceBetweenTypes(*(unit.Type), unit.tilePos, *(blocker->Type), blocker->tilePos);
				// A land unit of one tile under a player's command does not wait for a unit that
				// only crosses its goal: it has arrived beside it and stays.
				const bool staysBeside = IsPlayerLandUnitOfOneTile(unit);
				if (distToBlocker == step && this->GoalHolderWaits < MaxGoalHolderWaits
					&& GoalHolderLeaves(unit, this->goalPos)) {
					// Between two steps of its own way: the holder is off the goal at once.
					this->GoalHolderWaits++;
					unit.Wait = 1;
				} else if (distToBlocker == step && (unit.IsEnemy(*blocker) || blocker->Moving == 0 || staysBeside)) {
					unit.Wait = 0;
					this->Finished = true;
				}
			}
			Vec2i next;
			if (!this->Finished && IsPlayerLandUnitOfOneTile(unit) && BlockedNextTile(unit, next)) {
				const CUnit *holder = UnitOnMapTile(next, unit.Type->MoveType);
				if (holder != nullptr && holder != &unit && MoveHeadOnClosedDrops && HeadOnMutual(unit, *holder)) {
					// Live run 73 R (Human II neck 52,42, forest x 51 / coast x 53, the pocket north of it
					// sealed; 2 Footmen each side ordered through it; 38.5 cycles = 1 s): face to face in
					// the road nobody backs out, steps aside or swaps - each order is dropped and the four
					// stand idle for good (3 of 3 head-on trials R2-R4; the 4th, R1, never met). The one
					// that comes to stand face to face drops at that step's end, 16-17 frames after it set
					// foot on the tile (R2 1567 f362 -> f379, R3 1576 f357 -> f374, R4 1576 f389 -> f405);
					// the one in the neck at its next look (R2 +16, R3 +26, R4 +2 frames after the other
					// came); the units behind within 16-38 frames (their way is then held by standing
					// units: the crowd stop / Range rules end them). A way round the other (own units on
					// their way crossed, so a crossing elsewhere does not count) keeps the run 69 H6 /
					// run 54 back-out below.
					// Closed means the search for a way round ends on units (a flood): a goal that is
					// full or held (an arrived unit on it) is not a closed road (host group 3480: the
					// crossing groups' shared goal held by the first to arrive).
					UnmarkUnitFieldFlags(unit);
					const bool closed = !PathHasWayRound(unit, *holder)
					                    && (AStarLastFailReason == AStarFailFloodStandingUnits
					                        || AStarLastFailReason == AStarFailFloodUnitsOnTheWay);
					MarkUnitFieldFlags(unit);
					if (closed) {
						++MoveHeadOnClosedDropCount;
						unit.Wait = 0;
						this->Finished = true;
					}
				}
				if (!this->Finished && holder != nullptr && holder != &unit) {
					if (BacksOutHeadOn(unit, *holder, this->OrderedCycle)
					    && (!MoveHeadOnStepsAside || WayRoundHolder(unit, *holder))) {
						// Met head-on by a unit waiting there before it (run 69 H6: the later to arrive
						// backs out at its next step, 0.44 s; run 54: the later order): the cached way is dropped and the
						// next cycle searches again with the holder standing in it - round the road.
						unit.pathFinderData->output.Length = 0;
						unit.pathFinderData->output.Fast = 0;
						unit.Wait = 1;
					} else if (HeadOnStepAside(unit, *holder, this->OrderedCycle)) {
						// No way round the other: the later to arrive steps back, away from the other (round
						// 3.4.81; with MoveHeadOnClosedDrops a closed face-off has ended above, run 73 R).
						unit.Wait = 1;
					} else if (holder->Moving == 0 && !HolderHasWayToGo(unit, *holder)) {
						// Held by a standing unit: no wait - the cached way is dropped and the
						// next cycle searches round it (ORIG50 s.0: nobody waits, 22 orders; L9m1
						// 1495 paused one step). A unit crossing keeps the wait below.
						// No way round it at all (own units on their way crossed): the order ends
						// where the unit stands (live run 73 R: the units behind a dropped face-off
						// pair, R2 1566, R3 1574 / 1567, 16-38 frames; run 69 H5 1567: next tile
						// held by a standing unit -> dropped 16 frames after arriving, idle in the gap
						// for good). Before: the search failed on the mates behind it (jam wait,
						// 10 x 38 cycles) or widened the Range.
						// Only a free goal (Range 0) whose way the search finds closed (a flood that
						// ends on units): a crowd round a full or blocked goal keeps the old handling
						// (host live 3481 / forest 3482: the rear joins the crowd, UNMEASURED live).
						bool closed = false;
						if (MoveHeadOnClosedDrops && this->Range == 0) {
							UnmarkUnitFieldFlags(unit);
							closed = !PathHasWayRound(unit, *holder)
							         && (AStarLastFailReason == AStarFailFloodStandingUnits
							             || AStarLastFailReason == AStarFailFloodUnitsOnTheWay);
							MarkUnitFieldFlags(unit);
						}
						if (closed) {
							++MoveHeadOnClosedDropCount;
							unit.Wait = 0;
							this->Finished = true;
						} else {
							unit.pathFinderData->output.Length = 0;
							unit.pathFinderData->output.Fast = 0;
							unit.Wait = 1;
						}
					} else if (MoveGoesRoundMates && holder->Moving != 0 && IsGroupMate(unit, *holder, this->OrderedCycle)
					           && GoesRoundMate(unit, *holder)) {
						// Live run 67 M1: the step is held by a moving mate of the same order: no
						// queue, the unit goes round it at once (the way round is in cache).
						this->GoingRound = true;
						unit.Wait = 1;
					} else if (holder->Moving != 0) {
						SetPathWait(unit, CrossingUnitWait);
					}
				}
			} else if (!this->Finished && IsPlayerGroupShip(unit) && BlockedNextTile(unit, next)) {
				// The same for a player's ship: held by a standing ship (not one with a way of its
				// own to go) the way is dropped and searched round it next cycle. Ships had only the
				// wait expiry (C5 follow-up); live run 67 M2b (land, 1 case): the walker re-routes
				// 0.37 s after the holder stands, no push, no give-up.
				const CUnit *holder = UnitOnMapTile(next, unit.Type->MoveType);
				if (holder != nullptr && holder != &unit && holder->Moving == 0
				    && !HolderHasWayToGo(unit, *holder)) {
					unit.pathFinderData->output.Length = 0;
					unit.pathFinderData->output.Fast = 0;
					unit.Wait = 1;
				}
			}
		}	break;
		default:
			if (moveResult > 0) {
				this->UnitBlockedWaits = 0; // on its way again
				this->Stepped = true;
			}
			break;
	}

	if (this->Finished && !unit.JustMoved) {
		unit.JustMoved = 0b111;
	}
}

/**
**  Get goal position
*/
const Vec2i COrder_Move::GetGoalPos() const /* override */
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
