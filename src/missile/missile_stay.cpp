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
/**@name missile_stay.cpp - The missile Stay. */
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
**  Missile don't move, than disappears
*/
void MissileStay::Action() /* override */
{
	this->Wait = this->Type->Sleep;
	if (this->NextMissileFrame(1, 0)) {
		this->MissileHit();
		this->TTL = 0;
	}
}

/**
**  One round of the Death and Decay: the missile stays, shows the pictures of the original's
**  script, hits on the script's handler tick and goes when the next round begins.
*/
void MissileDeathAndDecay::Action() /* override */
{
	const int tick = this->State++;

	this->Wait = 1;
	// AnimWait: set by the spell for a round that follows another one
	this->SpriteFrame = DeathAndDecayPictureOn(tick, this->AnimWait != 0);
	if (this->SpriteFrame >= this->Type->SpriteFrames) {
		this->SpriteFrame = this->Type->SpriteFrames - 1;
	}
	if (tick == DeathAndDecayFirstHit) {
		this->MissileHit();
	}
	if (tick + 1 >= DeathAndDecayPeriod) {
		this->TTL = 0;
	}
}

//@}
