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
/**@name missile_flameshield.cpp - The missile FlameShield. */
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

#include "actions.h"
#include "missile_cadence.h"
#include "unit.h"
#include "unit_find.h"

/**
**  FlameShield controller
*/
void MissileFlameShield::Action() /* override */
{
	this->Wait = this->Type->Sleep;
	// Each picture is held for two calls.
	if (this->AnimWait-- <= 0) {
		if (this->NextMissileFrame(1, 0)) {
			this->SpriteFrame = 0;
		}
		this->AnimWait = FlameShieldPictureCalls - 1;
	}
	// The flame's place on the ring: a step counter of its own (State), one step a call. The ring
	// is the original's, in pixels of the classic tile, from the centre of the unit to the flame's
	// point (the middle of its picture, where the area hit is made from).
	const int step = this->State;
	this->State = FlameShieldNextStep(step);
	const int dx = FlameShieldOffset(step, PixelTileSize.x, false);
	const int dy = FlameShieldOffset(step, PixelTileSize.y, true);
	CUnit *unit = this->TargetUnit;
	//
	// Show around the top most unit.
	// FIXME: conf, do we hide if the unit is contained or not?
	//
	while (unit->Container) {
		unit = unit->Container;
	}
	const Vec2i upos = unit->tilePos;
	const int ix = unit->IX;
	const int iy = unit->IY;
	const int uw = unit->Type->TileWidth;
	const int uh = unit->Type->TileHeight;
	this->position.x = upos.x * PixelTileSize.x + ix + uw * PixelTileSize.x / 2 + dx - this->Type->size.x / 2;
	this->position.y = upos.y * PixelTileSize.y + iy + uh * PixelTileSize.y / 2 + dy - this->Type->size.y / 2;
	if (unit->CurrentAction() == UnitAction::Die) {
		this->TTL = this->TTL % 36;
	}

	if (unit->Container) {
		this->Hidden = 1;
		return;  // Hidden missile don't do damage.
	} else {
		this->Hidden = 0;
	}

	// One area hit from the flame's own place on every 16th cycle; the unit it circles is left out
	// (MissileHitsZones).
	if (!FlameShieldHitsOn(this->TTL)) {
		return;
	}
	this->MissileHit();
}

//@}
