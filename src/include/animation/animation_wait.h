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
/**@name animation_wait.h - The animation Wait headerfile. */
//
//      (c) Copyright 2012 by Joris Dauphin
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

#ifndef ANIMATION_WAIT_H
#define ANIMATION_WAIT_H

//@{

#include <string>
#include "animation.h"

/**
**  How a wait reacts to Haste / Slow, after the wait opcodes of the original's unit scripts.
**  The Lua verb names the kind; the plain "wait" never changes.
*/
enum class EAnimWaitKind {
	Fixed,     /// "wait": opcode 1, never scaled
	HasteSlow, /// "wait-haste-slow": opcode 7, halved by Haste, doubled by Slow
	Haste,     /// "wait-haste": opcode 8, halved by Haste only
	Slow,      /// "wait-slow": opcode 9, doubled by Slow only
	Loaded     /// "wait-loaded": opcode 12, +1 while carrying a load, then as opcode 7
};

/**
**  Scale one wait by its kind.
**
**  @param kind         the kind of the wait
**  @param wait         the wait in cycles
**  @param speedSign    > 0 hasted, < 0 slowed, 0 neither
**  @param carriesLoad  the unit carries a load (only read by EAnimWaitKind::Loaded)
**
**  @return the scaled wait; a halved wait is at least 1
*/
int ScaleAnimationWait(EAnimWaitKind kind, int wait, int speedSign, bool carriesLoad);

/// The Haste / Slow sign of a unit: 1 hasted, -1 slowed, 0 neither (or both)
int AnimationWaitSpeedSign(const CUnit &unit);

class CAnimation_Wait : public CAnimation
{
public:
	explicit CAnimation_Wait(EAnimWaitKind kind = EAnimWaitKind::Fixed) : kind(kind) {}

	void Action(CUnit &unit, int &move, int scale) const override;
	void Init(std::string_view s, lua_State *l) override;

	EAnimWaitKind GetKind() const { return kind; }

private:
	std::string wait;
	EAnimWaitKind kind;
};

//@}

#endif // ANIMATION_WAIT_H
