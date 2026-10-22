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
/**@name action_patrol.h - The actions headerfile. */
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

#ifndef __ACTION_PATROL_H__
#define __ACTION_PATROL_H__

#include "actions.h"

//@{

class COrder_Patrol : public COrder
{
	friend std::unique_ptr<COrder> COrder::NewActionPatrol(const Vec2i &currentPos, const Vec2i &dest);
public:
	COrder_Patrol() : COrder(UnitAction::Patrol) {}

	std::unique_ptr<COrder> Clone() const override { return std::make_unique<COrder_Patrol>(*this); }

	bool IsValid() const override;

	void Save(CFile &file, const CUnit &unit) const override;
	bool
	ParseSpecificData(lua_State *l, int &j, std::string_view value, const CUnit &unit) override;

	void Execute(CUnit &unit) override;
	PixelPos Show(const CViewport &vp, const PixelPos &lastScreenPos) const override;
	void UpdatePathFinderData(PathFinderInput &input) override;
	const Vec2i GetGoalPos() const override;
	wc2ai::Reissue ReissueReady(const CUnit &unit, unsigned long cycle) override;

	const Vec2i &GetWayPoint() const { return WayPoint; }
	/// Read-only views for the network sync hash.
	int GetRange() const { return Range; }
	bool GetGoalPulled() const { return GoalPulled; }
	/// Is this patrol remembered for a re-issue after a fight (the original's order 5 memory)?
	bool Remembered(const CUnit &unit) const;
private:
	Vec2i WayPoint{-1, -1}; /// position for patroling.
	unsigned int WaitingCycle = 0; /// number of cycle pathfinder wait.
	int Range = 0;
	Vec2i goalPos{-1, -1};
	unsigned long ReissueAt = 0; /// game cycle from which the remembered leg may be sent again.
	/// The leg's goal has been pulled toward the unit (0x4C2630) from the tile it stood on when the
	/// leg began (each turn-round is a new order 5 through 0x4D8970). Saved as "goal-pulled".
	bool GoalPulled = false;
	/// The leg's ordered point before the pull (the original's memory +0x94, the click: run 49 P1 / P2).
	/// A re-issue of the remembered leg sets the order again from it (0x4D892B -> 0x4D8970), so it is
	/// pulled again from the tile the unit stands on then. (-1, -1): unknown (a save written before
	/// this key), the pulled goal is kept. Saved as "ordered".
	Vec2i Ordered{-1, -1};
};

//@}

#endif // !__ACTION_PATROL_H__
