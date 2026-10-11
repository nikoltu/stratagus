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

/// The rules for the level currently being created.
const WC2MissionRules &CurrentMissionRules();

/// PORTAL-ATTACK-ORDER: in the mission whose rule is 0x1E (Beyond the Dark Portal, Human XII) the
/// neutral Dark Portal takes damage from Khadgar only - the mission's own objective says "Only
/// Khadgar can destroy the Portal", and on the original a group without him left it at 5000 / 5000
/// while a group with him destroyed it (work/agent-orig-4/ORIG4.md, sections 7-11). False = this hit
/// does nothing. In the mission of rule 0x08 (Tides of Darkness, Human XIV) the portal takes ordinary
/// damage; in every other campaign mission it takes none (measured, ORIG3.md item 8, ORIG5.md).
/// Outside the campaigns (custom games) and for every other target: true - not measured there.
class CUnit;
bool MissionAllowsDamage(const CUnit *attacker, const CUnit &target);

/// Drop the rules (game teardown).
void ClearMissionRules();

/// Register the Lua side (GetMissionRules, MissionRuleOutcome).
void WC2MissionCclRegister();

//@}

#endif // !__WC2MISSION_H__
