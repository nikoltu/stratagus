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
/**@name wc2mission.h - per-mission campaign rules read from the original engine. */
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.

#ifndef __WC2MISSION_H__
#define __WC2MISSION_H__

//@{

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/// The five per-mission rule columns the original engine keeps for a campaign level.
struct WC2MissionRules {
	int index = -1;                 /// 0..51 linear mission index, -1 = not a campaign mission
	uint32_t unitAllow = 0;         /// buildable/trainable bitmask
	uint32_t upgradeAllow = 0;      /// researchable bitmask
	uint32_t spellAllow = 0;        /// read for completeness; NOT applied (six bits undecoded)
	uint16_t victoryId = 0;         /// victory rule id
	uint16_t victoryParam = 0;      /// victory rule parameter
	bool ruleLoaded = false;        /// the mission's own victory rule was read and is evaluated
	bool valid = false;             /// false: behave exactly as if this module did not exist
};

/// Derive the mission index from a map request and read its rule row. Never throws; on any
/// failure the rules stay invalid and nothing in the engine is touched.
bool LoadWC2MissionRules(const std::filesystem::path &mapPath);

/// Apply the unit and upgrade masks to every player. No-op unless the rules are valid.
void ApplyMissionAllow();

/// Apply the start actions the original performs for the mission's rule id: the spells a player holds
/// from the start are marked researched, unit types are converted, the "can be taken over" mark and
/// the hit points of single unit types are set, a slot becomes the player's enemy.
/// After ApplyMissionAllow; a spell already held is left alone.
void ApplyMissionStart();

/// Upgrade idents of the spells a spell-state mask grants, in bit order. `unknown` receives the bits
/// that are set but have no established meaning (they grant nothing).
std::vector<std::string> MissionSpellGrantUpgrades(uint32_t mask, uint32_t *unknown = nullptr);

/// For the host tests: the start actions found in a stretch of dispatcher code (`code` loaded at
/// `va`, spell-state array at `stateBase`), one line each; and the spell-state array found in the
/// code behind the allow installer's table loads (0: none). `listBase` is the array of the per-slot
/// unit list heads (0: the unit loops are not read), `entry` where the dispatcher starts (0: at `va`).
std::string MissionStartActionsOfCode(const std::string &code, uint32_t va, uint32_t stateBase,
                                      uint32_t listBase = 0, uint32_t entry = 0);
uint32_t MissionSpellStateBaseOfCode(const std::string &code);

/// The generic arrival rule (ids 0x200 and 0x04) as the original's evaluator tests it: 2 = won,
/// 1 = lost (it can no longer be met), 0 = neither. `param` is the mission's parameter (the number
/// of units to bring; bit 0x1000: one of them must be a hero), `arrived` the units counted on the
/// circle, `marked` the living units that could still be brought, `heroArrived` / `heroMarked`
/// whether a hero is among each.
int MissionArrivalOutcome(int param, int arrived, bool heroArrived, int marked, bool heroMarked);

/// One other slot as the generic "destroy all" rule (rule word bit 0x100) sees it.
struct MissionSlotCensus {
	bool tested = false; /// the slot is looked at (MissionSlotTested)
	int buildings = 0;   /// its buildings
	int units = 0;       /// its units that are no buildings
	int carriers = 0;    /// of those: flying machines, transports, tankers
};

/// A slot the generic "destroy all" rule looks at: one at war with the player, and a rescue slot
/// that is still its own - seen on the original (run 32, item 3, Tides Human V): with every enemy
/// dead Victory waited until the rescue slot's last units had become the player's. A computer
/// allied with the player is not looked at (not measured).
constexpr bool MissionSlotTested(bool self, bool atWar, bool rescue)
{
	return !self && (atWar || rescue);
}

/// The generic "destroy all" rule as the original's evaluator tests it: won when every tested
/// slot has no building and no unit other than carriers. The test is made only while the player
/// has a unit that is no building (`ownUnits`), and not while he trains a unit or has something
/// unfinished (`ownProducing`).
bool MissionGenericWinHolds(int ownUnits, bool ownProducing, const MissionSlotCensus *slots, int count);

/// The rules for the level currently being created.
const WC2MissionRules &CurrentMissionRules();

/// PORTAL-ATTACK-ORDER: in the mission whose rule is 0x1E (Beyond the Dark Portal, Human XII) the
/// neutral Dark Portal takes damage from Khadgar only - the mission's own objective says "Only
/// Khadgar can destroy the Portal", and on the original a group without him left it at 5000 / 5000
/// while a group with him destroyed it. False = this hit does nothing. In the mission of rule 0x08
/// (Tides of Darkness, Human XIV) the portal takes ordinary damage; in every other campaign
/// mission it takes none (measured on the original).
/// Outside the campaigns (custom games) and for every other target: true - not measured there.
class CUnit;
bool MissionAllowsDamage(const CUnit *attacker, const CUnit &target);

/// What a running rule has counted and a saved game must carry: the units already counted on the
/// circle and the rule's opened gate. One Lua line for the save file ("" when there is nothing),
/// read back by MissionRuleState; and its writer.
std::string MissionRuleStateLine(const std::vector<int> &arrived, bool gateOpen);
class CFile;
void SaveMissionRuleState(CFile &file);

/// Drop the rules (game teardown).
void ClearMissionRules();

/// Register the Lua side (GetMissionRules, MissionRuleOutcome).
void WC2MissionCclRegister();

//@}

#endif // !__WC2MISSION_H__
