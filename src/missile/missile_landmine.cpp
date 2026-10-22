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
/**@name missile_landmine.cpp - The missile LandMine. */
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
#include "map.h"
#include "missile_cadence.h"
#include "unit.h"

struct LandMineTargetFinder {
	const CUnit *const source;
	int CanHitOwner;
	LandMineTargetFinder(const CUnit *unit, int hit):
		source(unit), CanHitOwner(hit) {}
	bool operator()(const CUnit *const unit) const
	{
		return (!(unit == source && !CanHitOwner) && unit->Type->MoveType != EMovement::Fly
		        && unit->CurrentAction() != UnitAction::Die);
	}
	CUnit *FindOnTile(const CMapField *const mf) const
	{
		auto it = ranges::find_if(mf->UnitCache, *this);
		return it != mf->UnitCache.end() ? *it : nullptr;
	}
};

/**
**  Land mine controller.
*/
void MissileLandMine::Action() /* override */
{
	const Vec2i pos = Map.MapPixelPosToTilePos(this->position);

	if (CUnit *stepper = LandMineTargetFinder(this->SourceUnit, this->Type->CanHitOwner).FindOnTile(Map.Field(pos))) {
		DebugPrint("Landmine explosion at %d,%d.\n", pos.x, pos.y);
		// Only the unit that stepped on it is hit (original rune tick 0x4CC600: that ONE unit loses
		// the number), not every unit of the tile - a flyer above it is spared.
		this->MissileHit(stepper);
		this->TTL = 0;
		return;
	}
	// Seen only as a flash on every 256th cycle of its life (RuneFlashPicture); between the
	// flashes it is not drawn. The step test above comes first: a rune that is not drawn goes off.
	const int picture = RuneFlashPicture(RuneFlashPhase(this->TTL));
	this->Hidden = picture < 0 ? 1 : 0;
	if (picture >= 0) {
		this->SpriteFrame = std::min(picture, std::max(0, this->Type->SpriteFrames - 1));
	}
	this->Wait = 1;
}

//@}
