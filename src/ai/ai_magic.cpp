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
/**@name ai_magic.cpp - AI magic functions. */
//
//      (c) Copyright 2002-2005 by Lutz Sammer, Joris Dauphin
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
#include "unittype.h"
#include "unit.h"
#include "spells.h"
#include "actions.h"
#include "ai_local.h"
#include "unit_manager.h"
#include "wc2ai.h"

#include <set>

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

/**
**  Look at what one caster of a computer can do with magic: an idle caster casts the first spell
**  it may and can.
**
**  @param unit  The caster; AiPlayer is its player's AI.
*/
static void AiCheckMagicOf(CUnit &unit)
{
	CPlayer &player = *unit.Player;

	// Check only idle magic units. A caster that already has a cast order is passed over; the
	// casters after it still get their turn in this pass.
	for (const auto &order : unit.Orders) {
		if (order->Action == UnitAction::SpellCast) {
			return;
		}
	}
	const size_t spellCount = std::min(SpellTypeTable.size(), unit.Type->CanCastSpell.size());
	if (spellCount != SpellTypeTable.size()) {
		static std::set<std::string> warnedTypes;
		if (warnedTypes.insert(unit.Type->Ident).second) {
			ErrorPrint("Warning: unit type '%s' has CanCastSpell size %zu but %zu spells are defined; "
			           "AI magic will ignore missing spell slots\n",
			           unit.Type->Ident.c_str(),
			           unit.Type->CanCastSpell.size(),
			           SpellTypeTable.size());
		}
	}
	for (size_t j = 0; j < spellCount; ++j) {
		// Check if we can cast this spell. SpellIsAvailable checks for upgrades.
		if (unit.Type->CanCastSpell[j] && SpellIsAvailable(player, j)
			&& SpellTypeTable[j]->AICast) {
			if (AutoCastSpell(unit, *SpellTypeTable[j])) {
				break;
			}
		}
	}
}

/**
**  Check what computer units can do with magic.
**  In fact, turn on autocast for AI.
*/
void AiCheckMagic()
{
	CPlayer &player = *AiPlayer->Player;

	for (CUnit *unit : player.GetUnits()) {
		if (!unit->Type->CanCastSpell.empty()) {
			AiCheckMagicOf(*unit);
		}
	}
}

/**
**  The original's casting pass (0x4B5E20 part 2): one walk over all units in slot order for every
**  player run by its original script together, the players' casters interleaved.
*/
void AiCheckMagicAll()
{
	PlayerAi *const saved = AiPlayer;

	// a cast can make units: the count is read again on every step
	for (unsigned int slot = 0; slot < UnitManager->GetUsedSlotCount(); ++slot) {
		CUnit &unit = UnitManager->GetSlotUnit(slot);
		if (unit.Released || unit.Destroyed || unit.Player == nullptr || unit.Type == nullptr
			|| unit.Type->CanCastSpell.empty()) {
			continue;
		}
		CPlayer &player = *unit.Player;
		// only a unit in its player's list, as the pass of one player takes it
		if (unit.PlayerSlot >= static_cast<size_t>(player.GetUnitCount())
			|| &player.GetUnit(unit.PlayerSlot) != &unit) {
			continue;
		}
		if (!player.AiEnabled || !player.Ai || !Wc2AiRuns(player.Index)) {
			continue;
		}
		AiPlayer = player.Ai.get();
		AiCheckMagicOf(unit);
	}
	AiPlayer = saved;
}

//@}
