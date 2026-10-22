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
/**@name missile_.cpp - The missile . */
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

#include "missile.h"

#include "missile_cadence.h"

/**
**  Missile doesn't move, it will just cycle once and vanish.
**  Used for ui missiles (cross shown when you give and order)
*/
void MissileCycleOnce::Action() /* override */
{
	this->Wait = this->Type->Sleep;
	switch (this->State) {
		case 0:
		case 2:
			++this->State;
			break;
		case 1:
			if (this->NextMissileFrame(1, 0)) {
				++this->State;
			}
			break;
		case 3:
			if (this->NextMissileFrame(-1, 0)) {
				this->MissileHit();
				this->TTL = 0;
			}
			break;
	}
}

/**
**  The cross shown where an order is given: it stays, shows the pictures of the original's
**  script, a tick a cycle, and vanishes after the last one.
*/
void MissileClickCross::Action() /* override */
{
	const int picture = ClickCrossPictureOn(this->State++);

	this->Wait = 1;
	if (picture < 0) {
		this->TTL = 0;
		return;
	}
	this->SpriteFrame = std::min(picture, this->Type->SpriteFrames - 1);
}

//@}
