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
/**@name spell_demolish.cpp - The spell demolish. */
//
//      (c) Copyright 1998-2012 by Vladi Belperchinov-Shabanski, Lutz Sammer,
//                                 Jimmy Salmon, and Joris DAUPHIN
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

#include "stratagus.h"

#include "spell/spell_demolish.h"

#include "script.h"
#include "map.h"
#include "missile.h"
#include "missile_cadence.h"
#include "unit.h"
#include "unit_find.h"

/**
**  If the explosion takes the wall on a tile.
**
**  @param offset       Tile offset from the caster.
**  @param damageRange  Tiles around the caster that are hit; below 0: no limit of its own.
**
**  @return             true if the wall falls.
*/
bool DemolishTakesWall(const Vec2i &offset, int damageRange)
{
	return damageRange < 0 || (std::abs(offset.x) <= damageRange && std::abs(offset.y) <= damageRange);
}

void Spell_Demolish::Parse(lua_State *l, int startIndex, int endIndex) /* override */
{
	for (int j = startIndex; j < endIndex; ++j) {
		const std::string_view value = LuaToString(l, -1, j + 1);
		++j;
		if (value == "range") {
			this->Range = LuaToNumber(l, -1, j + 1);
		} else if (value == "damage") {
			this->Damage = LuaToNumber(l, -1, j + 1);
		} else if (value == "damage-range") {
			this->DamageRange = LuaToNumber(l, -1, j + 1);
		} else if (value == "unit-missile") {
			this->UnitMissile = &MissileTypeByIdent(LuaToString(l, -1, j + 1));
		} else {
			LuaError(l, "Unsupported demolish tag: %s", value.data());
		}
	}
}

/**
**  Cast demolish
**  @param caster       Unit that casts the spell
**  @param spell        Spell-type pointer
**  @param target       Target unit that spell is addressed to
**  @param goalPos      tilePos of target spot when/if target does not exist
**
**  @return             =!0 if spell should be repeated, 0 if not
*/
int Spell_Demolish::Cast(CUnit &caster,
                         const SpellType &,
                         CUnit *&,
                         const Vec2i &goalPos) /* override */
{
	// Allow error margins. (Lame, I know)
	const Vec2i offset(this->Range + 2, this->Range + 2);
	Vec2i minpos = goalPos - offset;
	Vec2i maxpos = goalPos + offset;

	Map.FixSelectionArea(minpos, maxpos);

	//
	// Terrain effect of the explosion
	//
	Vec2i ipos;
	for (ipos.x = minpos.x; ipos.x <= maxpos.x; ++ipos.x) {
		for (ipos.y = minpos.y; ipos.y <= maxpos.y; ++ipos.y) {
			const CMapField &mf = *Map.Field(ipos);
			if (SquareDistance(ipos, goalPos) > square(this->Range)) {
				// Not in circle range
				continue;
			} else if (mf.isAWall()) {
				// With a damage range a wall falls only on the tiles around the caster.
				if (DemolishTakesWall(ipos - goalPos, this->DamageRange)) {
					Map.ClearTile(ipos);
				}
			} else if (mf.RockOnMap() || mf.ForestOnMap()) {
				Map.ClearTile(ipos);
			}
		}
	}

	//
	//  Effect of the explosion on units. Don't bother if damage is 0
	//
	if (this->Damage) {
		std::vector<CUnit *> table = SelectFixed(minpos, maxpos);
		for (CUnit *unit : table) {
			if (unit->Type->MoveType != EMovement::Fly && unit->IsAlive()
			    && unit->MapDistanceTo(goalPos) <= (this->DamageRange >= 0 ? this->DamageRange : this->Range)) {
				// Don't hit flying units!
				// The original makes one explosion at every unit it hits, before the hit
				// (0x4969F0 -> 0x49B790), 8 classic pixels above the unit's centre.
				if (this->UnitMissile) {
					const PixelPos pixelPos =
						unit->GetMapPixelPosCenter() - PixelPos(0, UnitExplosionLift(PixelTileSize.y));
					MakeMissile(*this->UnitMissile, pixelPos, pixelPos);
				}
				HitUnit(&caster, *unit, this->Damage);
			}
		}
	}

	return 1;
}



//@}
