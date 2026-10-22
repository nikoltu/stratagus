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
/**@name action_attack.h - The actions headerfile. */
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

#ifndef __ACTION_ATTACK_H__
#define __ACTION_ATTACK_H__

#include "actions.h"

//@{

class COrder_Attack : public COrder
{
	friend std::unique_ptr<COrder> COrder::NewActionAttack(const CUnit &attacker, CUnit &target);
	friend std::unique_ptr<COrder> COrder::NewActionAttack(const CUnit &attacker, const Vec2i &dest);
	friend std::unique_ptr<COrder> COrder::NewActionAttackGround(const CUnit &attacker, const Vec2i &dest);
public:
	explicit COrder_Attack(bool ground) :
		COrder(ground ? UnitAction::AttackGround : UnitAction::Attack)
	{}

	std::unique_ptr<COrder> Clone() const override { return std::make_unique<COrder_Attack>(*this); }

	bool IsValid() const override;
	void Save(CFile &file, const CUnit &unit) const override;
	bool
	ParseSpecificData(lua_State *l, int &j, std::string_view value, const CUnit &unit) override;

	void Execute(CUnit &unit) override;
	void OnAnimationAttack(CUnit &unit) override;
	PixelPos Show(const CViewport &vp, const PixelPos &lastScreenPos) const override;
	void UpdatePathFinderData(PathFinderInput &input) override;
	bool OnAiHitUnit(CUnit &unit, CUnit *attacker, int /*damage*/) override;
	const Vec2i GetGoalPos() const override { return goalPos; }

	bool IsAutoTargeting() const;
	void OfferNewTarget(const CUnit &unit, CUnit &target);
	wc2ai::Reissue ReissueReady(const CUnit &unit, unsigned long cycle) override;
	/// Is this an attack-move to a point (the original's order 0x0A memory)?
	bool IsRememberedAttackMove() const;
	/// The path of an attack-move to its point failed: idle, keeping it as the unit's memory.
	void EndBlockedAttackMove(CUnit &unit);
	/// Read-only views for the network sync hash.
	int GetRange() const { return Range; }
	const Vec2i &GetAttackMovePos() const { return attackMovePos; }
	bool GetGoalPulled() const { return GoalPulled; }

private:
	bool IsMovingToAttackPos() const;
	bool IsAttackGroundOrWall() const;
	bool IsTargetTooClose(const CUnit &unit) const;
	CUnit &BestTarget(const CUnit &unit, CUnit &target1, CUnit &target2) const;
	bool CheckIfGoalValid(CUnit &unit);
	void TurnToTarget(CUnit &unit, const CUnit *target);
	void SetAutoTarget(CUnit &unit, CUnit *target);
	bool EndActionAttack(CUnit &unit, const bool canBeFinished);
	void RememberAttackMove(CUnit &unit) const;
	void MoveToBetterPos(CUnit &unit);
	bool AutoSelectTarget(CUnit &unit);
	bool CheckForTargetInRange(CUnit &unit);
	void MoveToAttackPos(CUnit &unit, const int pfReturn);
	void MoveToTarget(CUnit &unit);
	void AttackTarget(CUnit &unit);
	/// Is goalPos the attack-move's own point (as ordered, or pulled), not a target's tile?
	bool HeadsForOwnPoint() const;
	/// A person's ship whose target died: notes the kill and keeps the ship in its fight for
	/// wc2ai::ShipKillHold cycles (true = this cycle is spent holding, or the hold has just ended
	/// with the next target or the re-issue).
	bool HoldAfterKill(CUnit &unit);

private:
	int State = 0;
	int MinRange = 0;
	int Range = 0;
	int SkirmishRange = 0;
	CUnitPtr offeredTarget = nullptr; // Stores pointer to target offered from outside (f.e. by HitUnit_AttackBack() event).
	Vec2i goalPos{-1, -1}; // Current goal position
	Vec2i attackMovePos{-1, -1}; // If attack-move was ordered
	unsigned char Sleep = 0;
	unsigned long ReissueAt = 0; // game cycle from which the remembered attack-move may be sent again
	unsigned long KillSeenAt = 0; // game cycle a ship saw its target dead (0 = none); saved "kill-seen-at"
	/// An attack-move to a point: goalPos is the point pulled toward the unit (0x4C2630) from the
	/// tile it stood on when the order (or its re-issue) became its order; attackMovePos stays the
	/// ordered point. False while the pull is still to come, and while goalPos is a target's tile.
	/// Saved as "goal-pulled".
	bool GoalPulled = false;
};
//@}

#endif // !__ACTION_ATTACK_H__
