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
/**@name test_anim_wait.cpp - Haste / Slow on the animation waits, per wait kind. */
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

#include <doctest.h>

#include "stratagus.h"
#include "animation/animation_wait.h"

namespace
{
constexpr int Hasted = 1;
constexpr int Slowed = -1;
constexpr int Normal = 0;
}

TEST_CASE("anim wait: plain wait (opcode 1) never changes")
{
	for (int sign : {Normal, Hasted, Slowed}) {
		CHECK(ScaleAnimationWait(EAnimWaitKind::Fixed, 4, sign, false) == 4);
		CHECK(ScaleAnimationWait(EAnimWaitKind::Fixed, 1, sign, false) == 1);
		CHECK(ScaleAnimationWait(EAnimWaitKind::Fixed, 7, sign, true) == 7);
	}
}

TEST_CASE("anim wait: haste-slow (opcode 7) halves and doubles")
{
	CHECK(ScaleAnimationWait(EAnimWaitKind::HasteSlow, 10, Normal, false) == 10);
	CHECK(ScaleAnimationWait(EAnimWaitKind::HasteSlow, 10, Hasted, false) == 5);
	CHECK(ScaleAnimationWait(EAnimWaitKind::HasteSlow, 10, Slowed, false) == 20);
	// odd waits: the halving drops the half cycle, never below 1
	CHECK(ScaleAnimationWait(EAnimWaitKind::HasteSlow, 3, Hasted, false) == 1);
	CHECK(ScaleAnimationWait(EAnimWaitKind::HasteSlow, 5, Hasted, false) == 2);
	CHECK(ScaleAnimationWait(EAnimWaitKind::HasteSlow, 1, Hasted, false) == 1);
	CHECK(ScaleAnimationWait(EAnimWaitKind::HasteSlow, 3, Slowed, false) == 6);
	// a load only matters to opcode 12
	CHECK(ScaleAnimationWait(EAnimWaitKind::HasteSlow, 3, Normal, true) == 3);
}

TEST_CASE("anim wait: haste only (opcode 8)")
{
	CHECK(ScaleAnimationWait(EAnimWaitKind::Haste, 6, Normal, false) == 6);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Haste, 6, Hasted, false) == 3);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Haste, 7, Hasted, false) == 3);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Haste, 1, Hasted, false) == 1);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Haste, 6, Slowed, false) == 6);
}

TEST_CASE("anim wait: slow only (opcode 9)")
{
	CHECK(ScaleAnimationWait(EAnimWaitKind::Slow, 1, Normal, false) == 1);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Slow, 1, Slowed, false) == 2);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Slow, 5, Slowed, false) == 10);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Slow, 5, Hasted, false) == 5);
	// the scripts use opcode 9 only with 1, where it behaves as opcode 7
	for (int sign : {Normal, Hasted, Slowed}) {
		CHECK(ScaleAnimationWait(EAnimWaitKind::Slow, 1, sign, false)
		      == ScaleAnimationWait(EAnimWaitKind::HasteSlow, 1, sign, false));
	}
}

TEST_CASE("anim wait: loaded (opcode 12) adds 1 with a load, then as opcode 7")
{
	CHECK(ScaleAnimationWait(EAnimWaitKind::Loaded, 2, Normal, false) == 2);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Loaded, 2, Normal, true) == 3);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Loaded, 2, Hasted, false) == 1);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Loaded, 2, Hasted, true) == 1); // (2 + 1) >> 1
	CHECK(ScaleAnimationWait(EAnimWaitKind::Loaded, 1, Hasted, true) == 1); // (1 + 1) >> 1
	CHECK(ScaleAnimationWait(EAnimWaitKind::Loaded, 3, Hasted, true) == 2); // (3 + 1) >> 1
	CHECK(ScaleAnimationWait(EAnimWaitKind::Loaded, 2, Slowed, false) == 4);
	CHECK(ScaleAnimationWait(EAnimWaitKind::Loaded, 2, Slowed, true) == 6); // (2 + 1) * 2
	CHECK(ScaleAnimationWait(EAnimWaitKind::Loaded, 1, Slowed, true) == 4);
}

TEST_CASE("anim wait: footman swing under haste and slow")
{
	// the original's Footman attack: one opcode 0 cycle, then 3 3 3 5 10 as opcode 7
	const int waits[] = {3, 3, 3, 5, 10};
	int normal = 1, hasted = 1, slowed = 1;
	for (int wait : waits) {
		normal += ScaleAnimationWait(EAnimWaitKind::HasteSlow, wait, Normal, false);
		hasted += ScaleAnimationWait(EAnimWaitKind::HasteSlow, wait, Hasted, false);
		slowed += ScaleAnimationWait(EAnimWaitKind::HasteSlow, wait, Slowed, false);
	}
	CHECK(normal == 25);
	CHECK(hasted == 11);
	CHECK(slowed == 49);
}
