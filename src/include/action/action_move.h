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
/**@name action_move.h - The actions headerfile. */
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

#ifndef __ACTION_MOVE_H__
#define __ACTION_MOVE_H__

#include "actions.h"

//@{

class COrder_Move : public COrder
{
	friend std::unique_ptr<COrder> COrder::NewActionMove(const Vec2i &pos);
public:
	COrder_Move() : COrder(UnitAction::Move) {}

	std::unique_ptr<COrder> Clone() const override { return std::make_unique<COrder_Move>(*this); }

	bool IsValid() const override;

	void Save(CFile &file, const CUnit &unit) const override;
	bool
	ParseSpecificData(lua_State *l, int &j, std::string_view value, const CUnit &unit) override;

	void Execute(CUnit &unit) override;
	PixelPos Show(const CViewport &vp, const PixelPos &lastScreenPos) const override;
	void UpdatePathFinderData(PathFinderInput &input) override;
	const Vec2i GetGoalPos() const override;
	/// Read-only views for the network sync hash.
	int GetRange() const { return Range; }
	bool GetGoalPulled() const { return GoalPulled; }
	unsigned long GetOrderedCycle() const { return OrderedCycle; }
	int GetUnitBlockedWaits() const { return UnitBlockedWaits; }

private:
	void TakeRingPlace(CUnit &unit);

	int Range = 0;
	int GoalHolderWaits = 0; /// cycles waited for the unit on the goal to walk off (not saved)
	/// Searches in a row that found no way only because standing units of the player on their way
	/// held it (AStarLastFailByUnits); each is followed by a wait, not a wider Range (not saved).
	int UnitBlockedWaits = 0;
	/// The unit has taken a step under this order (MoveDropsWhenBoxedIn acts only before; not saved:
	/// a loaded order counts as not stepped).
	bool Stepped = false;
	/// The goal has been pulled toward the unit (0x4C2630) from the tile it stood on when the order
	/// became its order; a new order is pulled at its first Execute. Saved as "goal-pulled".
	bool GoalPulled = false;
	/// The game cycle the order was given (NewActionMove): of two units met head-on in a road the
	/// one ordered later backs out (run 54 item 5). Saved as "ordered-cycle"; 0 in an older save.
	unsigned long OrderedCycle = 0;
	/// The unit took a way round a moving mate of its group (live run 67 M1): a ring place taken on
	/// that way keeps the cached way (live the rear units went on round, 5 of 5). Saved as
	/// "going-round" when set.
	bool GoingRound = false;
	/// The order has made its first search (live run 73 S: its first look decides). Not saved: a
	/// loaded order whose goal was pulled counts as having looked.
	bool Looked = false;
	/// The first search found the way closed by units (MoveFirstLookDrops): the unit holds still
	/// and the order ends MoveBoxedDropCycles after it was given (not saved).
	bool FirstLookClosed = false;
	Vec2i goalPos{-1, -1};
};

/// The goal's neighbour a player's land unit ends on when a standing unit holds its goal tile:
/// left of the heading (the way's longer axis); (0, 0) for a way as long on both axes.
Vec2i OccupiedGoalSide(const Vec2i &unitPos, const Vec2i &goal);

/// The places round a held goal tile in the order a folded group fills them (behind, behind-left,
/// left, behind-right, front-left, right, front, front-right of the heading); returns 8, or 0 for
/// a way as long on both axes. `step`: the grid of the unit (ships 2).
int RingPlacesAround(const Vec2i &unitPos, const Vec2i &goal, Vec2i places[8], int step = 1);

/// A player's land worker on its way to a mine or a depot whose cached step is held (PF_WAIT):
/// no queue (live run 68 M3). A standing holder: the way is searched again round it next cycle;
/// a holder on its way: round it at once when at most 4 x the way left, else the crossing wait.
bool HarvestStepHeld(CUnit &unit);
/// The rule above is on (default); host tests turn it off to measure the handling before it.
extern bool HarvestTurnsAway;
/// A Move whose search fails only because own units on their way stand in the way waits and
/// searches again instead of widening its Range (round 3.4.80b); host tests turn it off.
extern bool MoveWaitsOutUnitJam;
/// Host tests: the goal pull (0x4C2630) at the first Execute (on in the game).
extern bool MoveGoalPull;
/// Host tests: the go-round of a moving group mate (live run 67 M1; on in the game).
extern bool MoveGoesRoundMates;
/// Live run 69 F / H and run 70 G (on in the game; 0 walks round a forest goal in 27 live unit runs): a player's land unit whose blocked
/// goal (Range > 0) has every tile in reach held by units standing for good ends its order where it
/// stands instead of widening the ring (round 3.4.82: no detours round the forest, no shuffling).
extern bool MoveStopsAtStandingCrowd;
/// Live run 69 H5 (on in the game; host tests turn it off): a player's land unit whose search
/// enters no tile round it while a unit standing for good is among those holding them (boxed in)
/// ends its order MoveBoxedDropCycles after the order (run 72), no jam wait and no Range++ ramp (AStarLastFailBoxed).
extern bool MoveDropsWhenBoxedIn;
/// Live runs 72 (B1 3 / 3, m4-m6), 73 (S1-S4) and 69 (H5 1570, F5 1571): "a way now or drop". A
/// unit whose first search finds its way closed by units (boxed in, or the only way held by units
/// standing) does not look again: it holds still and its order ends this many cycles after it was
/// given (drops 2-3 frames after the order, 8 of 9 at 3). A mate that stepped off before that first
/// search (ordered earlier, or handled earlier in the same cycle) has freed the way and the unit walks.
constexpr unsigned long MoveBoxedDropCycles = 3;
/// On in the game; host tests turn it off for the rule before run 73 (look again every cycle, a mate
/// ordered with the unit keeps the jam wait).
extern bool MoveFirstLookDecides;
/// Live run 73 R (closed 1-tile road, 2 against 2, 3 head-on trials): a unit that waits for the
/// tile of one waiting for its own, with no way round that one, ends its order (no back-out, no
/// step aside, no swap); the units behind then find their way held by units standing for good and
/// end theirs by the crowd stop / Range rules. On in the game.
extern bool MoveHeadOnClosedDrops;
/// Host tests: orders ended by the two rules above.
extern int MoveFirstLookDrops;
extern int MoveHeadOnClosedDropCount;
/// Round 3.4.81 head-on deadlock (UNMEASURED; on in the game, host tests turn them off): a jammed
/// unit keeps the way through own units waiting on their way, and of a head-on pair of waiting
/// units one steps aside.
extern bool MoveJamTakesWayThroughMates;
extern bool MoveHeadOnStepsAside;
/// Live run 69 H6 (on in the game; host tests turn it off for the round 3.4.81 rule): of a
/// face-to-face pair the later to arrive (the one that finds the other already waiting for its
/// tile) backs out, whatever the order of the orders.
extern bool MoveHeadOnLaterArrivalYields;
/// Host tests: the Range++ of Move orders by the reason of the failed search (AStarFailReason;
/// AStarFailCount: no search in that cycle), and the waits of the rule above.
extern int MoveRangeReasons[];
extern int MoveJamWaits;

//@}

#endif // !__ACTION_MOVE_H__
