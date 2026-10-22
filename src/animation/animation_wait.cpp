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
/**@name animation_wait.cpp - The animation Wait. */
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

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "stratagus.h"

#include "animation/animation_wait.h"

#include "unit.h"

#include <algorithm>

/**
**  Haste / Slow per wait kind, as the original's script interpreter does it: Haste halves
**  (at least 1), Slow doubles; opcode 7 both, 8 Haste only, 9 Slow only, 1 never; opcode 12
**  adds 1 while a load is carried, then works as 7.
*/
int ScaleAnimationWait(EAnimWaitKind kind, int wait, int speedSign, bool carriesLoad)
{
	if (kind == EAnimWaitKind::Loaded && carriesLoad) {
		++wait;
	}
	const bool byHaste = kind == EAnimWaitKind::HasteSlow || kind == EAnimWaitKind::Haste
	                     || kind == EAnimWaitKind::Loaded;
	const bool bySlow = kind == EAnimWaitKind::HasteSlow || kind == EAnimWaitKind::Slow
	                    || kind == EAnimWaitKind::Loaded;
	if (speedSign > 0 && byHaste) {
		wait = std::max(wait >> 1, 1);
	} else if (speedSign < 0 && bySlow) {
		wait <<= 1;
	}
	return wait;
}

/**
**  The original keeps Haste and Slow in one signed timer, so a unit is never both; the port
**  keeps two variables (the spells clear the other one), and both at once cancel out.
*/
int AnimationWaitSpeedSign(const CUnit &unit)
{
	const bool hasted = unit.Variable[HASTE_INDEX].Value > 0;
	const bool slowed = unit.Variable[SLOW_INDEX].Value > 0;
	return hasted == slowed ? 0 : (hasted ? 1 : -1);
}

void CAnimation_Wait::Action(CUnit &unit, int & /*move*/, int scale) const /* override */
{
	Assert(unit.Anim.CurrAnim);
	Assert((*unit.Anim.CurrAnim)[unit.Anim.Anim].get() == this);
	unit.Anim.Wait = ScaleAnimationWait(this->kind, ParseAnimInt(unit, this->wait) << scale >> 8,
	                                    AnimationWaitSpeedSign(unit), unit.ResourcesHeld > 0);
	if (unit.Anim.Wait <= 0) {
		unit.Anim.Wait = 1;
	}
}

void CAnimation_Wait::Init(std::string_view s, lua_State *) /* override */
{
	this->wait = s;
}

//@}
