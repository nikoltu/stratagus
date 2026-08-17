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
/**@name wc2data.cpp - Native reader for the Remastered gameplay data tables (D2). */
//
//      (c) Copyright 2026 by the WargusHD project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.

//@{

// D2: read the Remastered engine's own gameplay stat table Data/Rez/unitdata.dat (UDTA) directly and
// overwrite the numeric unit stats, instead of trusting the hand-transcribed Lua constants (which the
// gameplay audit found deviate from the Remastered values in 69 fields, chiefly building sight radius).
// The Remastered engine (a recompiled War2BNE) reads this exact file, so it IS the authoritative source.
//
// This runs as a POST-pass AFTER all unit Lua has defined the CUnitType objects (the Lua allocates the
// types and sets identity/sprites/abilities; only the numeric stats are overwritten here). It is invoked
// from Lua via ApplyRemasteredUnitData() at the end of the unit scripts. Per-map PUD UDTA overrides layer
// on top later (D1).
//
// Layout (field-major, absolute offsets verified against the decoded table; little-endian):
//   sight_range  @1236 stride4 u32   hit_points @1676 stride2 u16   build_time @2006 stride1 u8
//   gold*10 @2116 u8   lumber*10 @2226 u8   oil*10 @2336 u8   attack_range @3326 u8
//   armor @3656 u8   priority @3876 u8   basic_damage @3986 u8   piercing_damage @4096 u8
//   kill_points @4924 stride2 u16
// Records 0..104 map to wc2pud::UnitScriptNames[]; records 105..109 are HP=255 padding (skipped);
// empty idents (34/36/37/48/54) are skipped.

#include "stratagus.h"

#include "iolib.h"
#include "pud.h"
#include "script.h"
#include "unittype.h"
#include "upgrade_structs.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "parameters.h"

namespace fs = std::filesystem;

namespace
{

constexpr size_t UDTA_MIN_SIZE = 5948;
constexpr int WC2_UNIT_RECORDS = 105; // UnitScriptNames covers 0..104; 105..109 are padding

inline uint32_t rdU8(const std::string &d, size_t off) { return (uint8_t) d[off]; }
inline uint32_t rdU16(const std::string &d, size_t off)
{
	return (uint8_t) d[off] | ((uint32_t)(uint8_t) d[off + 1] << 8);
}
inline uint32_t rdU32(const std::string &d, size_t off)
{
	return (uint8_t) d[off] | ((uint32_t)(uint8_t) d[off + 1] << 8)
	     | ((uint32_t)(uint8_t) d[off + 2] << 16) | ((uint32_t)(uint8_t) d[off + 3] << 24);
}

// Set a stat variable the same way the Lua parser does: Value == Max, enabled.
inline void setVar(CUnitType &t, int index, int value)
{
	t.DefaultStat.Variables[index].Value = value;
	t.DefaultStat.Variables[index].Max = value;
	t.DefaultStat.Variables[index].Enable = 1;
}

// Overwrite one unit type's numeric stats from record i of a field-major UDTA table. `base` is the byte
// offset of field 0 within `d`: 0 for the standalone Data/Rez/unitdata.dat (flag-less), 2 for a per-map PUD
// UDTA chunk (a 2-byte use_default flag precedes the field table). Field layout verified against Oracle
// gameplay_loader_descriptors + real .PUD stat cross-check (footman rec0 = hp60/armor2/dmg6). Only offsets
// below the classic field-table end are read, so both PUD variants (5696 classic / 5950 expansion) work.
inline void applyUdtaRecord(CUnitType &t, const std::string &d, int i, size_t base)
{
	setVar(t, SIGHTRANGE_INDEX, (int) rdU32(d, base + 1236 + i * 4));
	setVar(t, HP_INDEX, (int) rdU16(d, base + 1676 + i * 2));
	setVar(t, ARMOR_INDEX, (int) rdU8(d, base + 3656 + i));
	setVar(t, PRIORITY_INDEX, (int) rdU8(d, base + 3876 + i));
	setVar(t, BASICDAMAGE_INDEX, (int) rdU8(d, base + 3986 + i));
	setVar(t, PIERCINGDAMAGE_INDEX, (int) rdU8(d, base + 4096 + i));
	setVar(t, POINTS_INDEX, (int) rdU16(d, base + 4924 + i * 2));
	setVar(t, ATTACKRANGE_INDEX, (int) rdU8(d, base + 3326 + i));

	t.DefaultStat.Costs[TimeCost] = (int) rdU8(d, base + 2006 + i);
	t.DefaultStat.Costs[GoldCost] = (int) rdU8(d, base + 2116 + i) * 10;
	t.DefaultStat.Costs[WoodCost] = (int) rdU8(d, base + 2226 + i) * 10;
	t.DefaultStat.Costs[OilCost] = (int) rdU8(d, base + 2336 + i) * 10;
}

// Baseline snapshot of DefaultStat per type, captured once after the boot-time table (D2) settles, so a
// per-map UDTA override can be reverted before the next map is loaded (unit types persist across games --
// CreateGame does not re-run the unit Lua, so overrides would otherwise accumulate).
std::unordered_map<CUnitType *, CUnitStats> g_statBaseline;
bool g_baselineCaptured = false;

// PUD UGRD record index (0..51) -> engine upgrade ident. Order VERIFIED (Oracle + real upgrades.dat
// icon_id -> icons.lua -> upgrade.lua ident chain, cost-corroborated across both races): see
// .wargushd/RE_UGRD_UPGRADE_ORDER_2026-08-17.md. Unlike unitdata.dat (complete), the standalone
// upgrades.dat carries ZERO/partial spell costs (recs 34..51); the authoritative complete table is the
// PUD UGRD body itself (all campaign maps agree), so the body is applied for EVERY map, not gated by the
// use_default flag.
const char *const UpgradeScriptNames[52] = {
	"upgrade-sword1", "upgrade-sword2", "upgrade-battle-axe1", "upgrade-battle-axe2",
	"upgrade-arrow1", "upgrade-arrow2", "upgrade-throwing-axe1", "upgrade-throwing-axe2",
	"upgrade-human-shield1", "upgrade-human-shield2", "upgrade-orc-shield1", "upgrade-orc-shield2",
	"upgrade-human-ship-cannon1", "upgrade-human-ship-cannon2", "upgrade-orc-ship-cannon1",
	"upgrade-orc-ship-cannon2", "upgrade-human-ship-armor1", "upgrade-human-ship-armor2",
	"upgrade-orc-ship-armor1", "upgrade-orc-ship-armor2", "upgrade-catapult1", "upgrade-catapult2",
	"upgrade-ballista1", "upgrade-ballista2", "upgrade-ranger", "upgrade-longbow",
	"upgrade-ranger-scouting", "upgrade-ranger-marksmanship", "upgrade-berserker", "upgrade-light-axes",
	"upgrade-berserker-scouting", "upgrade-berserker-regeneration", "upgrade-ogre-mage",
	"upgrade-paladin", "upgrade-holy-vision", "upgrade-healing", "upgrade-exorcism",
	"upgrade-flame-shield", "upgrade-fireball", "upgrade-slow", "upgrade-invisibility",
	"upgrade-polymorph", "upgrade-blizzard", "upgrade-eye-of-kilrogg", "upgrade-bloodlust",
	"upgrade-raise-dead", "upgrade-death-coil", "upgrade-whirlwind", "upgrade-haste",
	"upgrade-unholy-armor", "upgrade-runes", "upgrade-death-and-decay",
};

// Baseline snapshot of every listed upgrade's Costs, same lifecycle as g_statBaseline.
std::unordered_map<CUpgrade *, std::array<int, MaxCosts>> g_upgradeBaseline;

} // namespace

// Read Data/Rez/unitdata.dat and overwrite the numeric stats of every already-defined unit type.
static void ApplyRemasteredUnitData()
{
	const fs::path resolved = LibraryFileName("Data/Rez/unitdata.dat");
	if (resolved.empty()) {
		return; // classic install without the Remastered data tree -> keep the Lua stats
	}
	std::error_code ec;
	const uintmax_t sz = fs::file_size(resolved, ec);
	if (ec || sz < UDTA_MIN_SIZE) {
		return;
	}
	std::string d;
	{
		auto fp = std::make_unique<CFile>();
		if (fp->open(resolved.string().c_str(), CL_OPEN_READ) == -1) {
			return;
		}
		d.resize((size_t) sz);
		const bool ok = (size_t) fp->read(&d[0], (size_t) sz) == (size_t) sz;
		fp->close();
		if (!ok) {
			return;
		}
	}

	// Build an ident -> type lookup (UnitTypeByIdent ExitFatals on a missing ident, so it can't be used
	// for the possibly-undefined nothing-* slots or a race that wasn't loaded).
	std::unordered_map<std::string, CUnitType *> byIdent;
	for (CUnitType *t : getUnitTypes()) {
		if (t) {
			byIdent.emplace(t->Ident, t);
		}
	}

	int applied = 0;
	for (int i = 0; i < WC2_UNIT_RECORDS; ++i) {
		const char *ident = wc2pud::UnitScriptNames[i];
		if (!ident || ident[0] == '\0' || (ident[1] == '\0')) {
			continue; // empty slot (nothing-*)
		}
		auto it = byIdent.find(ident);
		if (it == byIdent.end()) {
			continue; // ident not defined by the Lua (e.g. this race not loaded) -> skip
		}
		applyUdtaRecord(*it->second, d, i, 0); // standalone table: field 0 at byte 0 (no flag prefix)
		++applied;
	}
	DebugPrint("ApplyRemasteredUnitData: overwrote stats for %d unit types from Data/Rez/unitdata.dat\n",
	           applied);
}

// Capture (once) the current DefaultStat of every unit type + Costs of every upgrade as the baseline to
// revert per-map overrides to (types/upgrades persist across games; CreateGame does not re-run their Lua).
static void CaptureStatBaseline()
{
	if (g_baselineCaptured) {
		return;
	}
	for (CUnitType *t : getUnitTypes()) {
		if (t) {
			g_statBaseline[t] = t->DefaultStat;
		}
	}
	for (auto &up : AllUpgrades) {
		if (up) {
			std::array<int, MaxCosts> costs;
			for (int k = 0; k < MaxCosts; ++k) {
				costs[k] = up->Costs[k];
			}
			g_upgradeBaseline[up.get()] = costs;
		}
	}
	g_baselineCaptured = true;
}

// Read a cached PUD chunk sidecar (<stem>.udta / <stem>.ugrd) into `out`. false if absent/too short.
static bool LoadSidecar(const std::string &stem, const char *ext, size_t minSize, std::string &out)
{
	const fs::path sc = Parameters::Instance.GetUserDirectory() / "wc2mapcache" / (stem + ext);
	std::error_code ec;
	const uintmax_t sz = fs::file_size(sc, ec);
	if (ec || sz < minSize) {
		return false;
	}
	auto fp = std::make_unique<CFile>();
	if (fp->open(sc.string().c_str(), CL_OPEN_READ) == -1) {
		return false;
	}
	out.resize((size_t) sz);
	const bool ok = (size_t) fp->read(&out[0], (size_t) sz) == (size_t) sz;
	fp->close();
	return ok;
}

// Per-map UDTA: overwrite unit stats for THIS map, but only when the chunk's use_default flag is 0 (a
// use_default==1 map keeps the D2 Remastered baseline). Field table follows a 2-byte flag prefix (base 2).
static void ApplyMapUdta(const std::string &stem)
{
	std::string d;
	if (!LoadSidecar(stem, ".udta", 5254, d)) {
		return;
	}
	if (rdU16(d, 0) != 0) {
		return; // use_default -> keep baseline
	}
	std::unordered_map<std::string, CUnitType *> byIdent;
	for (CUnitType *t : getUnitTypes()) {
		if (t) {
			byIdent.emplace(t->Ident, t);
		}
	}
	int applied = 0;
	for (int i = 0; i < WC2_UNIT_RECORDS; ++i) {
		const char *ident = wc2pud::UnitScriptNames[i];
		if (!ident || ident[0] == '\0' || ident[1] == '\0') {
			continue;
		}
		auto it = byIdent.find(ident);
		if (it == byIdent.end()) {
			continue;
		}
		applyUdtaRecord(*it->second, d, i, 2);
		++applied;
	}
	DebugPrint("ApplyMapUnitData: applied per-map UDTA for %d types\n", applied);
}

// Per-map UGRD: overwrite upgrade costs from THIS map's chunk. Unlike UDTA the body is applied for EVERY
// map (the standalone upgrades.dat has zero/partial spell costs, so the PUD UGRD body is the authoritative
// complete table). Body follows a 2-byte prefix; field-major per record (base 2): time u8 @0, gold u16
// @52, lumber u16 @156, oil u16 @260. Record i -> UpgradeScriptNames[i] (VERIFIED order).
static void ApplyMapUgrd(const std::string &stem)
{
	std::string d;
	if (!LoadSidecar(stem, ".ugrd", 782, d)) { // 2-byte flag + 780-byte body
		return;
	}
	std::unordered_map<std::string, CUpgrade *> byIdent;
	for (auto &up : AllUpgrades) {
		if (up) {
			byIdent.emplace(up->Ident, up.get());
		}
	}
	const size_t B = 2;
	int applied = 0;
	for (int i = 0; i < 52; ++i) {
		auto it = byIdent.find(UpgradeScriptNames[i]);
		if (it == byIdent.end()) {
			continue;
		}
		CUpgrade *u = it->second;
		u->Costs[TimeCost] = (int) rdU8(d, B + 0 + i);
		u->Costs[GoldCost] = (int) rdU16(d, B + 52 + i * 2);
		u->Costs[WoodCost] = (int) rdU16(d, B + 156 + i * 2);
		u->Costs[OilCost] = (int) rdU16(d, B + 260 + i * 2);
		++applied;
	}
	DebugPrint("ApplyMapUnitData: applied per-map UGRD for %d upgrades\n", applied);
}

// D1 per-map data: before a map loads, revert unit stats + upgrade costs to the boot baseline, then apply
// this map's cached PUD UDTA/UGRD sidecars. Runs before InitUnitTypes(1) so UpdateStats() fans unit stats
// out to every player's Stats. Maps without sidecars simply get the baseline.
void ApplyMapUnitData(const fs::path &mapPath)
{
	if (!g_baselineCaptured) {
		CaptureStatBaseline(); // first map: current values ARE the baseline, nothing to revert yet
	} else {
		for (auto &kv : g_statBaseline) {
			kv.first->DefaultStat = kv.second;
		}
		for (auto &kv : g_upgradeBaseline) {
			for (int k = 0; k < MaxCosts; ++k) {
				kv.first->Costs[k] = kv.second[k];
			}
		}
	}

	// Sidecar stem = map filename up to the first '.', matching the stem the PUD converters write.
	const std::string leaf = mapPath.filename().string();
	const std::string stem = leaf.substr(0, leaf.find('.'));
	if (stem.empty()) {
		return;
	}
	ApplyMapUdta(stem);
	ApplyMapUgrd(stem);
}

static int CclApplyRemasteredUnitData(lua_State *l)
{
	LuaCheckArgs(l, 0);
	ApplyRemasteredUnitData();
	return 0;
}

void WC2DataCclRegister()
{
	lua_register(Lua, "ApplyRemasteredUnitData", CclApplyRemasteredUnitData);
}

//@}
