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
/**@name wc2mission.cpp - per-mission campaign rules read from the original engine. */
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.

//@{

// The campaign .PUD files carry NO trigger section and NO ALOW section: the mission LOGIC never lived
// in the map file. The original engine keeps it in five per-mission tables in its own .data segment,
// installed at level start by the routine that stores one mask into all sixteen player slots:
//
//     unit/building allow   52 x u32     victory rule id   52 x u16     spell allow   52 x u32
//     upgrade allow         52 x u32     victory param     52 x u16
//
// This module reads those tables out of the user's own Warcraft II: Remastered executable and hands
// them to the engine's existing allow machinery. Nothing here is authored, converted or transcribed.
//
// The table addresses are deliberately NOT hardcoded. They are recovered from the two instruction
// sequences that load them, which are matched in .text and must occur exactly once each:
//
//   allow installer   8B 0C BD <upgrade_tbl>  8B 14 BD <spell_tbl>  8B 34 BD <unit_tbl>
//   victory loader    66 8B 04 4D <param_tbl> 66 A3 <index_global>  0F B7 04 4D <victory_tbl>
//
// A binary that does not match disables the whole feature rather than yielding a plausible but wrong
// tech tree from stale addresses.
//
// Mission index (same convention as the objective-string reader in wc2data.cpp): base campaigns
// Human/Orc level N=1..14 -> 2*(N-1)+race, expansion XHuman/XOrc N=1..12 -> 28+2*(N-1)+race,
// race human=0 orc=1.
//
// The bit -> ident tables below were derived from the consumer side of the same binary: each sidebar
// button record carries an icon id and a requirement predicate, the predicate names the bit it tests,
// and the icon id names the unit or upgrade (NG23, NG28).

#include "stratagus.h"

#include "wc2mission.h"

#include "actions.h"
#include "iolib.h"
#include "player.h"
#include "pud.h"
#include "script.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"
#include "upgrade.h"
#include "upgrade_structs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace
{

WC2MissionRules g_rules;

// Cached per process: the exe is parsed once, only the five rows' worth of tables are kept.
bool g_tablesLoaded = false;
bool g_tablesValid = false;
std::vector<uint32_t> g_unitAllow, g_upgradeAllow, g_spellAllow;
std::vector<uint16_t> g_victoryId, g_victoryParam;

constexpr int WC2_MISSIONS = 52;

// Same value CclDefineAllow uses for "allowed" (unit/upgrade.cpp).
constexpr int WC2_UNIT_ALLOW_MAX = 65536;

// ---- unit/building allow bits -------------------------------------------------------------------
// Index = bit number. A null pair means the bit is not mapped to any ident; bit 13 is set in no
// campaign mask and tested by no button, so it stays UNKNOWN and is deliberately ignored.
struct RacePair {
	const char *human;
	const char *orc;
};

const RacePair UnitAllowBits[32] = {
	/*  0 */ {"unit-footman", "unit-grunt"},
	/*  1 */ {"unit-peasant", "unit-peon"},
	/*  2 */ {"unit-ballista", "unit-catapult"},
	/*  3 */ {"unit-knight", "unit-ogre"},
	/*  4 */ {"unit-archer", "unit-axethrower"},
	/*  5 */ {"unit-mage", "unit-death-knight"},
	/*  6 */ {"unit-human-oil-tanker", "unit-orc-oil-tanker"},
	/*  7 */ {"unit-human-destroyer", "unit-orc-destroyer"},
	/*  8 */ {"unit-human-transport", "unit-orc-transport"},
	/*  9 */ {"unit-battleship", "unit-ogre-juggernaught"},
	/* 10 */ {"unit-gnomish-submarine", "unit-giant-turtle"},
	/* 11 */ {"unit-gnomish-flying-machine", "unit-zeppelin"},
	/* 12 */ {"unit-gryphon-rider", "unit-dragon"},
	/* 13 */ {nullptr, nullptr}, // UNKNOWN: no button predicate tests this bit
	/* 14 */ {"unit-dwarves", "unit-goblin-sappers"},
	/* 15 */ {"unit-gryphon-aviary", "unit-dragon-roost"},
	/* 16 */ {"unit-farm", "unit-pig-farm"},
	/* 17 */ {"unit-human-barracks", "unit-orc-barracks"},
	/* 18 */ {"unit-elven-lumber-mill", "unit-troll-lumber-mill"},
	/* 19 */ {"unit-stables", "unit-ogre-mound"},
	/* 20 */ {"unit-mage-tower", "unit-temple-of-the-damned"},
	/* 21 */ {"unit-human-foundry", "unit-orc-foundry"},
	/* 22 */ {"unit-human-refinery", "unit-orc-refinery"},
	/* 23 */ {"unit-gnomish-inventor", "unit-alchemist"},
	/* 24 */ {"unit-church", "unit-altar-of-storms"},
	/* 25 */ {"unit-human-watch-tower", "unit-orc-watch-tower"},
	/* 26 */ {"unit-town-hall", "unit-great-hall"},
	/* 27 */ {"unit-keep", "unit-stronghold"},
	/* 28 */ {"unit-castle", "unit-fortress"},
	/* 29 */ {"unit-human-blacksmith", "unit-orc-blacksmith"},
	/* 30 */ {"unit-human-shipyard", "unit-orc-shipyard"},
	/* 31 */ {"unit-human-wall", "unit-orc-wall"},
};

// The promoted units share their base unit's bit: the original gates paladin / ogre-mage on the
// knight / ogre bit and ranger / berserker on the archer / axethrower bit, and lets the upgrade decide
// which of the two a barracks actually trains.
struct ExtraBit {
	int bit;
	const char *human;
	const char *orc;
};

const ExtraBit UnitAllowExtra[] = {
	{3, "unit-paladin", "unit-ogre-mage"},
	{4, "unit-ranger", "unit-berserker"},
};

// Guard tower and cannon tower carry no bit of their own in the original: their predicate gates purely
// on the lumber-mill / blacksmith census, so they ride on the watch-tower path and are left untouched.

// ---- upgrade allow bits -------------------------------------------------------------------------
// Two-level families occupy consecutive bits (the original computes mask = base << level).
// Bits 10 and 11 are set in the expansion masks but tested by no button: UNKNOWN, left unmapped.
const RacePair UpgradeAllowBits[32] = {
	/*  0 */ {"upgrade-arrow1", "upgrade-throwing-axe1"},
	/*  1 */ {"upgrade-arrow2", "upgrade-throwing-axe2"},
	/*  2 */ {"upgrade-sword1", "upgrade-battle-axe1"},
	/*  3 */ {"upgrade-sword2", "upgrade-battle-axe2"},
	/*  4 */ {"upgrade-human-shield1", "upgrade-orc-shield1"},
	/*  5 */ {"upgrade-human-shield2", "upgrade-orc-shield2"},
	/*  6 */ {"upgrade-human-ship-cannon1", "upgrade-orc-ship-cannon1"},
	/*  7 */ {"upgrade-human-ship-cannon2", "upgrade-orc-ship-cannon2"},
	/*  8 */ {"upgrade-human-ship-armor1", "upgrade-orc-ship-armor1"},
	/*  9 */ {"upgrade-human-ship-armor2", "upgrade-orc-ship-armor2"},
	/* 10 */ {nullptr, nullptr}, // UNKNOWN
	/* 11 */ {nullptr, nullptr}, // UNKNOWN
	/* 12 */ {"upgrade-ballista1", "upgrade-catapult1"},
	/* 13 */ {"upgrade-ballista2", "upgrade-catapult2"},
	/* 14 */ {nullptr, nullptr},
	/* 15 */ {nullptr, nullptr},
	/* 16 */ {"upgrade-ranger", "upgrade-berserker"},
	/* 17 */ {"upgrade-longbow", "upgrade-light-axes"},
	/* 18 */ {"upgrade-ranger-scouting", "upgrade-berserker-scouting"},
	/* 19 */ {"upgrade-ranger-marksmanship", "upgrade-berserker-regeneration"},
	/* 20 */ {nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr},
	/* 24 */ {nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr},
	/* 28 */ {nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr},
};

inline uint16_t rdU16(const std::string &d, size_t off)
{
	return (uint16_t)((uint8_t) d[off] | ((uint16_t)(uint8_t) d[off + 1] << 8));
}

inline uint32_t rdU32(const std::string &d, size_t off)
{
	return (uint8_t) d[off] | ((uint32_t)(uint8_t) d[off + 1] << 8)
	     | ((uint32_t)(uint8_t) d[off + 2] << 16) | ((uint32_t)(uint8_t) d[off + 3] << 24);
}

/// One PE section, enough of it to translate a virtual address into a file offset.
struct PeSection {
	uint32_t va = 0;
	uint32_t vsize = 0;
	uint32_t raw = 0;
	uint32_t rawSize = 0;
};

struct PeImage {
	uint32_t imageBase = 0;
	std::vector<PeSection> sections;
	const PeSection *text = nullptr;

	bool parse(const std::string &d)
	{
		if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z') {
			return false;
		}
		const uint32_t peOff = rdU32(d, 0x3c);
		if (peOff + 0x78 >= d.size() || memcmp(d.data() + peOff, "PE\0\0", 4) != 0) {
			return false;
		}
		const uint16_t nSections = rdU16(d, peOff + 6);
		const uint16_t optSize = rdU16(d, peOff + 20);
		if (nSections == 0 || nSections > 96) {
			return false;
		}
		imageBase = rdU32(d, peOff + 24 + 28);
		const size_t secTable = peOff + 24 + optSize;
		if (secTable + (size_t) nSections * 40 > d.size()) {
			return false;
		}
		size_t textIdx = (size_t) -1;
		for (uint16_t i = 0; i < nSections; ++i) {
			const size_t s = secTable + (size_t) i * 40;
			PeSection ps;
			ps.vsize = rdU32(d, s + 8);
			ps.va = rdU32(d, s + 12);
			ps.rawSize = rdU32(d, s + 16);
			ps.raw = rdU32(d, s + 20);
			if ((size_t) ps.raw + ps.rawSize > d.size()) {
				return false;
			}
			if (memcmp(d.data() + s, ".text", 5) == 0) {
				textIdx = sections.size();
			}
			sections.push_back(ps);
		}
		if (textIdx == (size_t) -1) {
			return false;
		}
		text = &sections[textIdx];
		return true;
	}

	/// File offset of a virtual address, or SIZE_MAX when the address is not backed by raw data.
	size_t offsetOfVa(uint32_t va) const
	{
		if (va < imageBase) {
			return (size_t) -1;
		}
		const uint32_t rva = va - imageBase;
		for (const PeSection &s : sections) {
			const uint32_t span = s.vsize > s.rawSize ? s.vsize : s.rawSize;
			if (rva >= s.va && rva < s.va + span) {
				const uint32_t d = rva - s.va;
				return d < s.rawSize ? (size_t) s.raw + d : (size_t) -1;
			}
		}
		return (size_t) -1;
	}
};

/// Find `pattern` (with 0xFF-marked wildcard bytes in `mask`) inside .text. Returns the file offset
/// only when the match is unique -- more than one match means the signature is not the expected
/// anchor, and the caller must give up rather than pick one.
size_t findUniqueInText(const std::string &d, const PeImage &pe,
                        const uint8_t *pattern, const char *mask, size_t len)
{
	const size_t begin = pe.text->raw;
	const size_t end = begin + pe.text->rawSize;
	if (end > d.size() || len == 0 || end - begin < len) {
		return (size_t) -1;
	}
	size_t hit = (size_t) -1;
	for (size_t i = begin; i + len <= end; ++i) {
		bool ok = true;
		for (size_t j = 0; j < len; ++j) {
			if (mask[j] == 'x' && (uint8_t) d[i + j] != pattern[j]) {
				ok = false;
				break;
			}
		}
		if (!ok) {
			continue;
		}
		if (hit != (size_t) -1) {
			return (size_t) -1; // ambiguous
		}
		hit = i;
	}
	return hit;
}

template<typename T>
bool readTable(const std::string &d, const PeImage &pe, uint32_t va, std::vector<T> &out)
{
	const size_t off = pe.offsetOfVa(va);
	if (off == (size_t) -1 || off + sizeof(T) * WC2_MISSIONS > d.size()) {
		return false;
	}
	out.resize(WC2_MISSIONS);
	for (int i = 0; i < WC2_MISSIONS; ++i) {
		const size_t o = off + sizeof(T) * (size_t) i;
		out[i] = (T)(sizeof(T) == 2 ? (uint32_t) rdU16(d, o) : rdU32(d, o));
	}
	return true;
}

// ---- P1: the victory rule of a mission, read from the rule handlers --------------------------------
//
// The rule id of a mission (victory table) selects one handler routine in the original; the dispatcher
// that makes the selection is found through the rule-word global the victory loader stores into. What
// a handler tests is machine code, so it cannot be copied as a table -- but every NUMBER it uses is an
// operand of one of its compare instructions: the census row it reads (which names unit types through
// the type -> row pointer table), the slot, the count, the unit type of a hero. Those operands
// ("atoms") are collected here from the user's executable; RuleShape() below only says which atom
// plays which role for a rule id and refuses any handler whose atoms do not fit. No unit type, count
// or slot number of a mission is written in this file.

/// One decoded x86-32 instruction: only the fields the reader looks at.
struct X86Ins {
	uint32_t va = 0;
	int len = 0;
	uint8_t op = 0;
	uint8_t op2 = 0;       /// second opcode byte after 0F
	bool p66 = false;
	bool hasModrm = false;
	uint8_t mod = 0, reg = 0, rm = 0;
	bool hasSib = false;
	uint8_t sib = 0;
	bool hasDisp = false;
	uint32_t disp = 0;
	bool hasImm = false;
	uint32_t imm = 0;
	bool hasTarget = false;
	uint32_t target = 0;

	/// [disp32]
	bool memAbs(uint32_t &out) const
	{
		if (!hasModrm && op >= 0xA0 && op <= 0xA3) {
			out = disp;
			return true;
		}
		if (hasModrm && mod == 0 && rm == 5) {
			out = disp;
			return true;
		}
		return false;
	}
	/// [index * (1 << scale) + disp32], no base register
	bool memIndexed(int &index, int &scale, uint32_t &out) const
	{
		if (hasModrm && mod == 0 && rm == 4 && hasSib && (sib & 7) == 5 && ((sib >> 3) & 7) != 4) {
			index = (sib >> 3) & 7;
			scale = sib >> 6;
			out = disp;
			return true;
		}
		return false;
	}
	/// [reg + disp8]
	bool memBased(int &d8) const
	{
		if (hasModrm && mod == 1 && rm != 4) {
			d8 = (int8_t) disp;
			return true;
		}
		return false;
	}
};

/// Operand form of a one-byte opcode: -1 unknown, else bit 0 = ModRM, bits 1.. = immediate bytes
/// (4 means 32-bit, 16-bit under the 66 prefix). Only what the rule code uses is listed; an unknown
/// opcode ends the read and the mission keeps its present rules.
int x86OneByte(uint8_t op)
{
	if (op < 0x40) {
		const int k = op & 7;
		return k < 4 ? 1 : k == 4 ? (1 << 1) : k == 5 ? (4 << 1) : -1;
	}
	if (op < 0x60) {
		return 0; // inc / dec / push / pop reg
	}
	if ((op >= 0x70 && op <= 0x7F) || (op >= 0xB0 && op <= 0xB7)) {
		return 1 << 1;
	}
	if (op >= 0xB8 && op <= 0xBF) {
		return 4 << 1;
	}
	switch (op) {
		case 0x68: return 4 << 1;
		case 0x6A: return 1 << 1;
		case 0x69: return 1 | (4 << 1);
		case 0x6B: return 1 | (1 << 1);
		case 0x80: case 0x83: case 0xC0: case 0xC1: case 0xC6: return 1 | (1 << 1);
		case 0x81: case 0xC7: return 1 | (4 << 1);
		case 0x84: case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B:
		case 0x8D: case 0xD0: case 0xD1: case 0xD3: case 0xFE: case 0xFF: return 1;
		case 0x90: case 0x99: case 0xC3: case 0xCC: return 0;
		case 0xA8: return 1 << 1;
		case 0xA9: return 4 << 1;
		default: return -1;
	}
}

int x86TwoByte(uint8_t op2)
{
	if ((op2 >= 0x40 && op2 <= 0x4F) || (op2 >= 0x90 && op2 <= 0x9F)) {
		return 1; // cmovcc / setcc
	}
	switch (op2) {
		case 0x10: case 0x11: case 0x12: case 0x13: case 0x1F: case 0x57: case 0xA3: case 0xAB:
		case 0xAF: case 0xB6: case 0xB7: case 0xBE: case 0xBF: return 1;
		case 0xBA: return 1 | (1 << 1);
		default: return -1;
	}
}

bool x86Decode(const std::string &d, size_t off, uint32_t va, X86Ins &i)
{
	i = X86Ins();
	i.va = va;
	size_t p = off;
	if (p + 16 > d.size()) {
		return false;
	}
	for (;;) {
		const uint8_t b = (uint8_t) d[p];
		if (b == 0x66) {
			i.p66 = true;
		} else if (b != 0xF2 && b != 0xF3 && b != 0x64 && b != 0x2E && b != 0x3E) {
			break;
		}
		++p;
	}
	i.op = (uint8_t) d[p++];
	int form;
	if (i.op == 0x0F) {
		i.op2 = (uint8_t) d[p++];
		if (i.op2 >= 0x80 && i.op2 <= 0x8F) { // jcc rel32
			i.hasTarget = true;
			i.target = va + (uint32_t)(p - off) + 4 + rdU32(d, p);
			i.len = (int)(p + 4 - off);
			return true;
		}
		form = x86TwoByte(i.op2);
	} else if (i.op == 0xE8 || i.op == 0xE9) {
		i.hasTarget = true;
		i.target = va + (uint32_t)(p - off) + 4 + rdU32(d, p);
		i.len = (int)(p + 4 - off);
		return true;
	} else if (i.op == 0xEB || (i.op >= 0x70 && i.op <= 0x7F)) {
		i.hasTarget = true;
		i.target = va + (uint32_t)(p - off) + 1 + (uint32_t)(int32_t)(int8_t) d[p];
		i.len = (int)(p + 1 - off);
		return true;
	} else if (i.op >= 0xA0 && i.op <= 0xA3) { // mov al/eax <-> [moffs32]
		i.hasDisp = true;
		i.disp = rdU32(d, p);
		i.len = (int)(p + 4 - off);
		return true;
	} else if (i.op == 0xF6 || i.op == 0xF7) {
		form = 1; // the test forms (/0, /1) carry an immediate, added below
	} else {
		form = x86OneByte(i.op);
	}
	if (form < 0) {
		return false;
	}
	int immBytes = form >> 1;
	if (form & 1) {
		const uint8_t m = (uint8_t) d[p++];
		i.hasModrm = true;
		i.mod = m >> 6;
		i.reg = (m >> 3) & 7;
		i.rm = m & 7;
		if (i.mod != 3 && i.rm == 4) {
			i.hasSib = true;
			i.sib = (uint8_t) d[p++];
		}
		if (i.mod == 0 && (i.rm == 5 || (i.hasSib && (i.sib & 7) == 5))) {
			i.hasDisp = true;
			i.disp = rdU32(d, p);
			p += 4;
		} else if (i.mod == 1) {
			i.hasDisp = true;
			i.disp = (uint32_t)(int32_t)(int8_t) d[p];
			p += 1;
		} else if (i.mod == 2) {
			i.hasDisp = true;
			i.disp = rdU32(d, p);
			p += 4;
		}
		if ((i.op == 0xF6 || i.op == 0xF7) && i.op != 0x0F && i.reg <= 1) {
			immBytes = i.op == 0xF6 ? 1 : 4;
		}
	}
	if (immBytes == 1) {
		i.hasImm = true;
		i.imm = (uint8_t) d[p];
		p += 1;
	} else if (immBytes == 4) {
		i.hasImm = true;
		if (i.p66) {
			i.imm = rdU16(d, p);
			p += 2;
		} else {
			i.imm = rdU32(d, p);
			p += 4;
		}
	}
	i.len = (int)(p - off);
	return true;
}

/// Linear decode of one routine: ends at the first ret (or jump out) that no earlier forward branch
/// jumps over. False on an unknown opcode or when no end is found.
bool x86Function(const std::string &d, const PeImage &pe, uint32_t va, std::vector<X86Ins> &out)
{
	constexpr uint32_t limit = 0x600; // longest routine read here is the dispatcher, 0x3B0 bytes
	out.clear();
	uint32_t far = va;
	uint32_t pc = va;
	while (pc - va < limit) {
		const size_t off = pe.offsetOfVa(pc);
		X86Ins ins;
		if (off == (size_t) -1 || !x86Decode(d, off, pc, ins)) {
			return false;
		}
		out.push_back(ins);
		const uint32_t next = pc + (uint32_t) ins.len;
		const bool inside = ins.hasTarget && ins.target >= va && ins.target < va + limit;
		if (ins.hasTarget && ins.op != 0xE8 && inside && ins.target > far) {
			far = ins.target;
		}
		if (ins.op == 0xC3 && next > far) {
			return true;
		}
		if ((ins.op == 0xE9 || ins.op == 0xEB) && !inside && next > far) {
			return true;
		}
		pc = next;
	}
	return false;
}

// ---- rule terms --------------------------------------------------------------------------------

constexpr int WC2_PUD_TYPES = 105;   // wc2pud::UnitScriptNames covers 0..104
constexpr int WC2_RULE_SLOTS = 8;    // the handlers loop over slots 0..7: `cmp reg, 8` in each of them

enum class TermKind : uint8_t {
	CountGe,     /// living, finished units of `who` among `types` >= n
	CountZero,   /// none
	Eliminated,  /// every slot of `who`: no building, and no unit other than the carrier types
	NoBuilding,  /// every slot of `who`: no building
	Arrived,     /// n of the player's units have stood on a Circle of Power (types empty: units taken
	             /// over from a rescue slot)
	InRegion,    /// a finished unit of each type has a tile in the map region of the Circle of Power
	TookOver,    /// the player owns a unit taken over from another slot
	Poor         /// every slot of `who` holds less than n gold
};

struct RuleWho {
	uint16_t mask = 0;     /// fixed slots (bit 15 = neutral)
	bool self = false;     /// plus the local player
	bool others = false;   /// slots 0..7 except the local player and except `mask`
};

struct RuleTerm {
	TermKind kind = TermKind::CountZero;
	RuleWho who;
	std::vector<uint8_t> types; /// PUD unit type numbers
	int n = 0;
	bool negate = false;
};

struct RuleDef {
	bool ok = false;
	std::vector<RuleTerm> win;     /// all needed
	std::vector<RuleTerm> winAlt;  /// alternative: all of these instead
	std::vector<RuleTerm> lose;    /// any is enough
	std::vector<RuleTerm> gate;    /// when not empty: `win` counts only after these were once all true
	bool arrivalByParam = false;   /// the Arrived term takes its count from the mission's parameter
};

struct CensusAtom {
	uint32_t row = 0;
	int slot = -1;  /// -1: indexed by a register
	int imm = -1;   /// -1: no immediate (not a compare with a constant)
};

struct HandlerAtoms {
	std::vector<CensusAtom> census;
	std::vector<int> types;     /// immediates compared with a unit's type byte
	std::vector<int> small;     /// immediates of cmp reg32, imm8 ...
	std::vector<int> smallReg;  /// ... and the register of each
	std::vector<int> pushes;    /// the single immediate pushed before a call (a slot argument)
	std::vector<int> lists;     /// slots of absolute references into the per-slot unit list heads
	int gold = -1;              /// immediate compared with the per-slot amount array
	int callsG = 0;
};

/// What the reader learned about the original's rule code. Filled once per process.
struct RuleCode {
	bool valid = false;
	uint32_t ruleGlobal = 0;
	uint32_t handler[0x1F] = {};
	uint32_t generic = 0;                 /// the evaluator every handler calls first
	uint32_t rowUnits = 0, rowBuildings = 0, rowFly = 0, rowTransport = 0, rowTanker = 0;
	uint32_t censusEnd = 0;
	uint32_t goldArray = 0;
	uint32_t listBase = 0;
	uint32_t typeRow[WC2_PUD_TYPES] = {}; /// unit type -> census row address
	std::vector<uint8_t> carrierTypes;    /// types of the three rows the evaluator subtracts
	RuleDef defs[0x1F];
};

RuleCode g_code;

/// Units of a census row.
std::vector<uint8_t> RowTypes(const RuleCode &c, uint32_t row)
{
	std::vector<uint8_t> out;
	for (int t = 0; t < WC2_PUD_TYPES; ++t) {
		if (c.typeRow[t] == row) {
			out.push_back((uint8_t) t);
		}
	}
	return out;
}

bool CensusOf(const RuleCode &c, uint32_t va, uint32_t &row, int &slot)
{
	if (va < c.rowUnits || va >= c.censusEnd) {
		return false;
	}
	// A row is 16 slots x 16 bits: the evaluator's two totals are exactly 0x20 apart (checked below).
	row = c.rowUnits + ((va - c.rowUnits) / 0x20) * 0x20;
	slot = (int)((va - row) / 2);
	return true;
}

bool InText(const PeImage &pe, uint32_t va)
{
	const uint32_t lo = pe.imageBase + pe.text->va;
	return va >= lo && va < lo + pe.text->rawSize;
}

/// Find the unit type -> census row pointer table: the one run of at least WC2_PUD_TYPES 32-bit words
/// outside .text that are each zero or the address of a census row. Found by content, not by address.
bool FindRowTable(const std::string &d, const PeImage &pe, RuleCode &c)
{
	int found = 0;
	for (const PeSection &s : pe.sections) {
		if (&s == pe.text) {
			continue;
		}
		const size_t n = s.rawSize / 4;
		size_t run = 0;
		int nonZero = 0;
		for (size_t k = 0; k <= n; ++k) {
			const uint32_t v = k < n ? rdU32(d, s.raw + k * 4) : 1;
			const bool fits = v == 0
			               || (v >= c.rowUnits && v < c.rowUnits + 0x800 && (v - c.rowUnits) % 0x20 == 0);
			if (fits) {
				++run;
				nonZero += v != 0;
				continue;
			}
			if (run >= (size_t) WC2_PUD_TYPES && nonZero >= 60) {
				++found;
				const size_t first = s.raw + (k - run) * 4;
				uint32_t top = 0;
				for (int t = 0; t < WC2_PUD_TYPES; ++t) {
					c.typeRow[t] = rdU32(d, first + (size_t) t * 4);
					top = std::max(top, c.typeRow[t]);
				}
				c.censusEnd = top + 0x20;
			}
			run = 0;
			nonZero = 0;
		}
	}
	return found == 1;
}

/// Collect the operands of one handler, in code order.
bool ReadAtoms(const std::string &d, const PeImage &pe, const RuleCode &c, uint32_t va, HandlerAtoms &a)
{
	std::vector<X86Ins> ins;
	if (!x86Function(d, pe, va, ins)) {
		return false;
	}
	std::vector<int> pending;
	for (const X86Ins &i : ins) {
		const bool cmpImm = (i.op == 0x80 || i.op == 0x81 || i.op == 0x83) && i.reg == 7;
		uint32_t addr = 0;
		int index = 0, scale = 0;
		uint32_t row = 0;
		int slot = 0;
		if (i.memAbs(addr)) {
			if (CensusOf(c, addr, row, slot)) {
				a.census.push_back({row, slot, cmpImm ? (int) i.imm : -1});
			} else if (c.listBase && addr >= c.listBase && addr < c.listBase + 4 * PlayerMax
			           && (i.op == 0x8B || i.op == 0xA1)) {
				a.lists.push_back((int)((addr - c.listBase) / 4));
			}
		} else if (i.memIndexed(index, scale, addr)) {
			if (scale == 1 && CensusOf(c, addr, row, slot) && slot == 0) {
				a.census.push_back({row, -1, cmpImm ? (int) i.imm : -1});
			} else if (scale == 2 && addr == c.goldArray && i.op == 0x81 && i.reg == 7) {
				a.gold = (int) i.imm;
			}
		}
		int d8 = 0;
		if (i.op == 0x80 && i.hasModrm && i.reg == 7) {
			// cmp byte [unit + 0x27], type -- or [unit + 9] where the pointer was advanced by 0x1E --
			// or a register that holds the type byte
			if (i.mod == 3 || (i.memBased(d8) && (d8 == 0x27 || d8 == 0x09))) {
				a.types.push_back((int) i.imm);
			}
		} else if (i.op == 0x3C) {
			a.types.push_back((int) i.imm);
		} else if (i.op == 0x83 && i.hasModrm && i.mod == 3 && i.reg == 7 && !i.p66) {
			a.small.push_back((int) i.imm);
			a.smallReg.push_back(i.rm);
		}
		if (i.op == 0x6A) {
			pending.push_back((int) i.imm);
		} else if (i.op == 0xE8) {
			if (i.target == c.generic) {
				++a.callsG;
			} else if (pending.size() == 1) {
				a.pushes.push_back(pending[0]);
			}
			pending.clear();
		} else if (!(i.op >= 0x50 && i.op <= 0x57)) {
			pending.clear();
		}
	}
	return true;
}

RuleTerm MakeTerm(TermKind kind, RuleWho who, std::vector<uint8_t> types = {}, int n = 0)
{
	RuleTerm t;
	t.kind = kind;
	t.who = who;
	t.types = std::move(types);
	t.n = n;
	return t;
}

RuleWho WhoSelf()
{
	RuleWho w;
	w.self = true;
	return w;
}

RuleWho WhoSlot(int slot)
{
	RuleWho w;
	w.mask = (uint16_t)(1u << slot);
	return w;
}

RuleWho WhoOthers(uint16_t except = 0)
{
	RuleWho w;
	w.others = true;
	w.mask = except;
	return w;
}

RuleWho WhoAny()
{
	RuleWho w;
	w.mask = 0xFFFF;
	return w;
}

/// Assign the atoms of a handler to their roles. The shape of each rule id follows the decode of its
/// handler (the handler's address is read from the dispatcher at run time). Returns false when the
/// atoms do not fit: the rule stays off.
bool RuleShape(const RuleCode &c, int id, const HandlerAtoms &a, RuleDef &r)
{
	const std::vector<CensusAtom> &C = a.census;
	std::vector<CensusAtom> idx, ab;
	for (const CensusAtom &x : C) {
		(x.slot < 0 ? idx : ab).push_back(x);
	}
	std::vector<uint8_t> T;
	for (int t : a.types) {
		if (t < 0 || t >= WC2_PUD_TYPES) {
			return false;
		}
		T.push_back((uint8_t) t);
	}
	// Slots a loop over the slots leaves out: compares of the loop register, the one compared with 8.
	int loopReg = -1;
	for (size_t k = 0; k < a.small.size(); ++k) {
		if (a.small[k] == WC2_RULE_SLOTS && loopReg < 0) {
			loopReg = a.smallReg[k];
		}
	}
	uint16_t except = 0;
	int exceptCount = 0;
	for (size_t k = 0; k < a.small.size(); ++k) {
		if (loopReg >= 0 && a.smallReg[k] == loopReg && a.small[k] < WC2_RULE_SLOTS) {
			except |= (uint16_t)(1u << a.small[k]);
			++exceptCount;
		}
	}
	const auto row = [&](uint32_t va) { return RowTypes(c, va); };
	// "the heroes are alive": as many living units of the listed types as there are types. A handler
	// that lists several also compares its counter with that number.
	const auto heroes = [&](std::vector<uint8_t> types) {
		if (types.empty()) {
			return false;
		}
		if (types.size() > 1
		    && std::find(a.small.begin(), a.small.end(), (int) types.size()) == a.small.end()) {
			return false;
		}
		RuleTerm t = MakeTerm(TermKind::CountGe, WhoAny(), types, (int) types.size());
		t.negate = true;
		r.lose.push_back(t);
		return true;
	};
	const auto sameSlot = [](const std::vector<CensusAtom> &v) {
		if (v.empty()) {
			return -1;
		}
		for (const CensusAtom &x : v) {
			if (x.slot != v[0].slot) {
				return -1;
			}
		}
		return v[0].slot;
	};
	const auto fail = [&]() {
		r = RuleDef();
		return false;
	};
	r = RuleDef();

	switch (id) {
		case 0x00: case 0x01: // have N of each tested row
			if (C.empty() || !ab.empty() || !T.empty()) return fail();
			for (const CensusAtom &x : C) {
				if (x.imm < 0) return fail();
				r.win.push_back(MakeTerm(TermKind::CountGe, WhoSelf(), row(x.row), std::max(x.imm, 1)));
			}
			break;
		case 0x02: case 0x06: // the others have none of the tested rows
			if (C.empty() || !ab.empty() || !T.empty() || loopReg < 0) return fail();
			for (const CensusAtom &x : C) {
				if (x.imm != 0) return fail();
				r.win.push_back(MakeTerm(TermKind::CountZero, WhoOthers(), row(x.row)));
			}
			break;
		case 0x0F: case 0x18: // have N of the first row; the others none of the rest
			if (C.size() < 2 || !ab.empty() || loopReg < 0 || C[0].imm < 1) return fail();
			r.win.push_back(MakeTerm(TermKind::CountGe, WhoSelf(), row(C[0].row), C[0].imm));
			for (size_t k = 1; k < C.size(); ++k) {
				if (C[k].imm != 0) return fail();
				r.win.push_back(MakeTerm(TermKind::CountZero, WhoOthers(), row(C[k].row)));
			}
			break;
		case 0x03: case 0x17: // the others eliminated, and own one of the row
			if (C.size() != 1 || idx.size() != 1 || C[0].imm != 0) return fail();
			if (exceptCount != (id == 0x17 ? 1 : 0)) return fail();
			r.win.push_back(MakeTerm(TermKind::Eliminated, WhoOthers(except)));
			r.win.push_back(MakeTerm(TermKind::CountGe, WhoSelf(), row(C[0].row), 1));
			break;
		case 0x05: // staged: take a unit over, then eliminate
			if (!C.empty() || !T.empty()) return fail();
			r.gate.push_back(MakeTerm(TermKind::TookOver, WhoSelf()));
			r.win.push_back(MakeTerm(TermKind::Eliminated, WhoOthers()));
			break;
		case 0x07: // the dispatcher hands this id to the generic evaluator
			if (c.handler[id] != c.handler[4]) return fail();
			r.win.push_back(MakeTerm(TermKind::Eliminated, WhoOthers()));
			break;
		case 0x08: case 0x1E: // a fixed slot has none of the row (0x1E: and the hero lives)
			if (C.size() != 1 || ab.size() != 1 || C[0].imm != 0) return fail();
			r.win.push_back(MakeTerm(TermKind::CountZero, WhoSlot(C[0].slot), row(C[0].row)));
			if (id == 0x1E ? (T.size() != 1 || !heroes(T)) : !T.empty()) return fail();
			break;
		case 0x09: { // keep the object; the others lose the other row
			if (idx.size() != 1 || ab.size() != 2 * WC2_RULE_SLOTS) return fail();
			const uint32_t own = idx[0].row;
			uint32_t other = 0;
			uint16_t ownSlots = 0;
			for (const CensusAtom &x : ab) {
				if (x.row == own) {
					ownSlots |= (uint16_t)(1u << x.slot);
				} else if (other == 0 || other == x.row) {
					other = x.row;
				} else {
					return fail();
				}
			}
			if (ownSlots != 0x00FF || other == 0) return fail();
			RuleWho all;
			all.mask = ownSlots;
			r.lose.push_back(MakeTerm(TermKind::CountZero, all, row(own)));
			r.win.push_back(MakeTerm(TermKind::CountZero, WhoOthers(), row(other)));
			r.win.push_back(MakeTerm(TermKind::CountGe, WhoSelf(), row(own), 1));
			break;
		}
		case 0x0A: // two buildings in the region of the Circle of Power
			if (T.size() != 2 || idx.size() != 2) return fail();
			r.win.push_back(MakeTerm(TermKind::InRegion, WhoSelf(), T));
			break;
		case 0x0B: // the hero lives; one slot has no unit of a row and no building
			if (T.size() != 1 || ab.size() != 2 || ab[0].slot != ab[1].slot
			    || ab[1].row != c.rowBuildings || !heroes(T)) return fail();
			r.win.push_back(MakeTerm(TermKind::CountZero, WhoSlot(ab[0].slot), row(ab[0].row)));
			r.win.push_back(MakeTerm(TermKind::NoBuilding, WhoSlot(ab[1].slot)));
			break;
		case 0x0C: case 0x0D: case 0x15: case 0x1D: // the heroes live; the others eliminated
			if (!C.empty() || a.callsG != 1 || !heroes(T)) return fail();
			r.win.push_back(MakeTerm(TermKind::Eliminated, WhoOthers()));
			break;
		case 0x0E: { // capture: own a unit of row A; slot s has no unit of row B
			if (ab.size() != 2 || idx.size() != 2 || ab[0].row != idx[0].row || idx[0].row != idx[1].row
			    || ab[0].slot != ab[1].slot) return fail();
			RuleWho keepers = WhoSlot(ab[0].slot);
			keepers.self = true;
			r.lose.push_back(MakeTerm(TermKind::CountZero, keepers, row(ab[0].row)));
			r.win.push_back(MakeTerm(TermKind::CountGe, WhoSelf(), row(ab[0].row), 1));
			r.win.push_back(MakeTerm(TermKind::CountZero, WhoSlot(ab[1].slot), row(ab[1].row)));
			break;
		}
		case 0x10: // one slot's unit of one type is dead
			if (T.size() != 1 || a.lists.size() != 1 || !C.empty()) return fail();
			r.win.push_back(MakeTerm(TermKind::CountZero, WhoSlot(a.lists[0]), T));
			break;
		case 0x11: { // eliminate three slots, then the escorted unit stands on the circle
			const int s = sameSlot(ab);
			if (T.size() != 2 || a.pushes.size() != 2 || s < 0 || T[1] >= WC2_RULE_SLOTS) return fail();
			RuleWho owners = WhoSlot(T[1]); // the second compare is against the unit's owner slot
			owners.self = true;
			RuleTerm alive = MakeTerm(TermKind::CountGe, owners, {T[0]}, 1);
			alive.negate = true;
			r.lose.push_back(alive);
			r.win.push_back(MakeTerm(TermKind::Eliminated, WhoSlot(s)));
			for (int q : a.pushes) {
				if (q < 0 || q >= WC2_RULE_SLOTS) return fail();
				r.win.push_back(MakeTerm(TermKind::Eliminated, WhoSlot(q)));
			}
			r.win.push_back(MakeTerm(TermKind::Arrived, WhoSelf(), {T[0]}, 1));
			break;
		}
		case 0x12: { // one slot eliminated
			const int s = sameSlot(ab);
			if (s < 0 || !idx.empty() || !T.empty()) return fail();
			r.win.push_back(MakeTerm(TermKind::Eliminated, WhoSlot(s)));
			break;
		}
		case 0x13: { // capture the object and eliminate the others
			if (ab.size() != 1 || idx.size() != 2 || ab[0].row != idx[0].row || idx[0].row != idx[1].row) {
				return fail();
			}
			RuleWho keepers = WhoSlot(ab[0].slot);
			keepers.self = true;
			r.lose.push_back(MakeTerm(TermKind::CountZero, keepers, row(ab[0].row)));
			r.win.push_back(MakeTerm(TermKind::Eliminated, WhoOthers()));
			r.win.push_back(MakeTerm(TermKind::CountGe, WhoSelf(), row(ab[0].row), 1));
			break;
		}
		case 0x14: // the heroes live; <parameter> units taken over stand on the circle
			if (!C.empty() || !heroes(T)) return fail();
			r.win.push_back(MakeTerm(TermKind::Arrived, WhoSelf()));
			r.arrivalByParam = true;
			break;
		case 0x16: // staged: the others (but one slot) lose two rows, then the hero stands on the circle
			if (T.size() != 1 || idx.size() != 2 || !ab.empty() || exceptCount != 1 || !heroes(T)) {
				return fail();
			}
			for (const CensusAtom &x : idx) {
				r.gate.push_back(MakeTerm(TermKind::CountZero, WhoOthers(except), row(x.row)));
			}
			r.win.push_back(MakeTerm(TermKind::Arrived, WhoSelf(), T));
			r.arrivalByParam = true;
			break;
		case 0x19: { // staged: one slot eliminated, then the heroes stand on the circle
			const int s = sameSlot(ab);
			if (T.size() != 2 || s < 0 || !idx.empty() || !heroes(T)) return fail();
			r.gate.push_back(MakeTerm(TermKind::Eliminated, WhoSlot(s)));
			r.win.push_back(MakeTerm(TermKind::Arrived, WhoSelf(), T));
			r.arrivalByParam = true;
			break;
		}
		case 0x1A: // the heroes live; the first listed type is dead; the others have none of a row
			if (T.size() != 4 || C.size() != 1 || idx.size() != 1
			    || !heroes(std::vector<uint8_t>(T.begin() + 1, T.end()))) return fail();
			r.win.push_back(MakeTerm(TermKind::CountZero, WhoAny(), {T[0]}));
			r.win.push_back(MakeTerm(TermKind::CountZero, WhoOthers(), row(C[0].row)));
			break;
		case 0x1B: // fixed slots have none of the rows
			if (C.size() != 2 || ab.size() != 2 || !T.empty()) return fail();
			for (const CensusAtom &x : C) {
				if (x.imm != 0) return fail();
				r.win.push_back(MakeTerm(TermKind::CountZero, WhoSlot(x.slot), row(x.row)));
			}
			break;
		case 0x1C: // the heroes live; the others eliminated, or all of them short of gold
			if (!C.empty() || a.gold < 0 || !heroes(T)) return fail();
			r.win.push_back(MakeTerm(TermKind::Eliminated, WhoOthers()));
			r.winAlt.push_back(MakeTerm(TermKind::Poor, WhoOthers(), {}, a.gold));
			break;
		default:
			return fail();
	}
	// A row that names no unit type would make "none of the row" true for ever.
	for (const std::vector<RuleTerm> *list : {&r.win, &r.winAlt, &r.lose, &r.gate}) {
		for (const RuleTerm &t : *list) {
			if ((t.kind == TermKind::CountGe || t.kind == TermKind::CountZero) && t.types.empty()) {
				return fail();
			}
		}
	}
	r.ok = true;
	return true;
}

/// Read the dispatcher, the generic evaluator, the row table and every handler. `vicHit` is the file
/// offset of the victory-loader signature. Leaves g_code.valid false on any mismatch.
void ReadRuleCode(const std::string &d, const PeImage &pe, size_t vicHit)
{
	RuleCode &c = g_code;
	c = RuleCode();

	// The victory loader stores the parameter, then the rule word: 66 A3 <rule global> within the
	// next instructions (the default id is loaded on a second path in between).
	size_t store = (size_t) -1;
	for (size_t o = vicHit + 22; o + 6 <= d.size() && o < vicHit + 22 + 24; ++o) {
		if ((uint8_t) d[o] == 0x66 && (uint8_t) d[o + 1] == 0xA3) {
			store = o;
			break;
		}
	}
	if (store == (size_t) -1) {
		return;
	}
	c.ruleGlobal = rdU32(d, store + 2);

	// Dispatcher: mov cx,[rule] / je rel8 / mov eax,imm32 -- once in .text.
	uint8_t pat[10] = {0x66, 0x8B, 0x0D, 0, 0, 0, 0, 0x74, 0, 0xB8};
	memcpy(pat + 3, d.data() + store + 2, 4);
	const size_t dispHit = findUniqueInText(d, pe, pat, "xxxxxxxx?x", sizeof(pat));
	if (dispHit == (size_t) -1) {
		return;
	}
	const uint32_t dispVa = pe.imageBase + pe.text->va + (uint32_t)(dispHit - pe.text->raw);
	std::vector<X86Ins> ins;
	if (!x86Function(d, pe, dispVa, ins)) {
		return;
	}
	int cur = -1;
	for (const X86Ins &i : ins) {
		if (i.p66 && i.op == 0x85 && i.mod == 3 && i.reg == 1 && i.rm == 1) { // test cx,cx
			cur = 0;
		} else if (i.p66 && i.op == 0x83 && i.mod == 3 && i.reg == 7 && i.rm == 1) { // cmp cx,imm8
			cur = (int) i.imm;
		} else if (cur >= 0 && cur < 0x1F && i.op == 0xB8 && InText(pe, i.imm) && c.handler[cur] == 0) {
			c.handler[cur] = i.imm;
		}
	}
	for (int id = 0; id < 0x1F; ++id) {
		if (c.handler[id] == 0) {
			return;
		}
	}

	// Rule 4's handler is a bare jump to the generic evaluator.
	if (!x86Function(d, pe, c.handler[4], ins) || ins.size() != 1 || ins[0].op != 0xE9) {
		return;
	}
	c.generic = ins[0].target;
	if (!x86Function(d, pe, c.generic, ins)) {
		return;
	}
	// In code order the evaluator compares five word rows with zero (two guards, the unit total, the
	// hall count, the building total), subtracts three rows from the unit total (flying machine,
	// transport, tanker) and compares one dword array with a constant (the amount it calls enough).
	std::vector<uint32_t> zeroCmp, subs;
	for (const X86Ins &i : ins) {
		int index = 0, scale = 0;
		uint32_t addr = 0;
		if (!i.memIndexed(index, scale, addr)) {
			continue;
		}
		if (i.op == 0x83 && i.reg == 7 && i.p66 && scale == 1 && i.imm == 0) {
			zeroCmp.push_back(addr);
		} else if (i.op == 0x2B && i.p66 && scale == 1) {
			subs.push_back(addr);
		} else if (i.op == 0x81 && i.reg == 7 && scale == 2) {
			c.goldArray = addr;
		}
	}
	if (zeroCmp.size() != 5 || subs.size() != 3 || c.goldArray == 0) {
		return;
	}
	c.rowUnits = zeroCmp[2];
	c.rowBuildings = zeroCmp[4];
	c.rowFly = subs[0];
	c.rowTransport = subs[1];
	c.rowTanker = subs[2];
	if (c.rowBuildings != c.rowUnits + 0x20 || !FindRowTable(d, pe, c)) {
		return;
	}
	for (uint32_t r : {c.rowFly, c.rowTransport, c.rowTanker}) {
		for (uint8_t t : RowTypes(c, r)) {
			c.carrierTypes.push_back(t);
		}
	}
	if (c.carrierTypes.empty()) {
		return;
	}

	// Per-slot unit list heads: the one mov reg,[reg*4+disp32] of rule 0x0A's handler.
	if (!x86Function(d, pe, c.handler[0x0A], ins)) {
		return;
	}
	int listRefs = 0;
	for (const X86Ins &i : ins) {
		int index = 0, scale = 0;
		uint32_t addr = 0;
		if (i.op == 0x8B && i.memIndexed(index, scale, addr) && scale == 2) {
			c.listBase = addr;
			++listRefs;
		}
	}
	if (listRefs != 1) {
		return;
	}

	int shaped = 0;
	for (int id = 0; id < 0x1F; ++id) {
		if (id == 0x04) {
			continue; // arrival rule of the generic evaluator: stays with the existing trigger script
		}
		HandlerAtoms a;
		if (ReadAtoms(d, pe, c, c.handler[id], a) && RuleShape(c, id, a, c.defs[id])) {
			++shaped;
		}
	}
	c.valid = true;
	DebugPrint("ReadRuleCode: dispatcher %08x, evaluator %08x, %d rule handlers read\n",
	           dispVa, c.generic, shaped);
}

/// Read the five tables out of the Remastered executable. Runs at most once per process.
bool EnsureRuleTables()
{
	if (g_tablesLoaded) {
		return g_tablesValid;
	}
	g_tablesLoaded = true;
	g_tablesValid = false;

	if (WC2SourcePath.empty()) {
		return false;
	}
	const fs::path exe = fs::path(WC2SourcePath) / "Warcraft II.exe";
	std::error_code ec;
	const uintmax_t sz = fs::file_size(exe, ec);
	if (ec || sz < 0x1000 || sz > 64u * 1024u * 1024u) {
		return false;
	}
	std::string d;
	{
		std::ifstream in(exe, std::ios::binary);
		if (!in) {
			return false;
		}
		d.resize((size_t) sz);
		in.read(&d[0], (std::streamsize) sz);
		if ((uintmax_t) in.gcount() != sz) {
			return false;
		}
	}

	PeImage pe;
	if (!pe.parse(d)) {
		return false;
	}

	// mov ecx,[edi*4+upgrade] / mov edx,[edi*4+spell] / mov esi,[edi*4+unit]
	static const uint8_t allowPat[21] = {
		0x8B, 0x0C, 0xBD, 0, 0, 0, 0,
		0x8B, 0x14, 0xBD, 0, 0, 0, 0,
		0x8B, 0x34, 0xBD, 0, 0, 0, 0};
	static const char allowMask[] = "xxx????xxx????xxx????";
	// mov ax,[ecx*2+param] / mov [index],ax / movzx eax,[ecx*2+victory]
	static const uint8_t vicPat[22] = {
		0x66, 0x8B, 0x04, 0x4D, 0, 0, 0, 0,
		0x66, 0xA3, 0, 0, 0, 0,
		0x0F, 0xB7, 0x04, 0x4D, 0, 0, 0, 0};
	static const char vicMask[] = "xxxx????xx????xxxx????";

	const size_t allowHit = findUniqueInText(d, pe, allowPat, allowMask, sizeof(allowPat));
	const size_t vicHit = findUniqueInText(d, pe, vicPat, vicMask, sizeof(vicPat));
	if (allowHit == (size_t) -1 || vicHit == (size_t) -1) {
		return false;
	}

	const uint32_t upgradeVa = rdU32(d, allowHit + 3);
	const uint32_t spellVa = rdU32(d, allowHit + 10);
	const uint32_t unitVa = rdU32(d, allowHit + 17);
	const uint32_t paramVa = rdU32(d, vicHit + 4);
	const uint32_t victoryVa = rdU32(d, vicHit + 18);

	if (!readTable(d, pe, unitVa, g_unitAllow) || !readTable(d, pe, upgradeVa, g_upgradeAllow)
	    || !readTable(d, pe, spellVa, g_spellAllow) || !readTable(d, pe, victoryVa, g_victoryId)
	    || !readTable(d, pe, paramVa, g_victoryParam)) {
		return false;
	}

	g_tablesValid = true;
	// P1: the victory rules. A failure here leaves the tables above in use and every mission on the
	// rules it has today.
	ReadRuleCode(d, pe, vicHit);
	return true;
}

/// Mission index of a campaign map request, or -1. Mirrors ResolveCampaignPud's parse of
/// campaigns/<race>[-exp]/levelNN.smp so the two can never disagree about which level this is.
int MissionIndexOf(const fs::path &mapPath)
{
	std::string f = mapPath.string();
	for (char &c : f) {
		if (c == '\\') {
			c = '/';
		}
	}
	const auto pos = f.find("campaigns/");
	if (pos == std::string::npos) {
		return -1;
	}
	const std::string rest = f.substr(pos + 10);
	const auto slash = rest.find('/');
	if (slash == std::string::npos) {
		return -1;
	}
	const std::string dir = rest.substr(0, slash);
	std::string base = rest.substr(slash + 1);
	auto dot = base.find(".smp");
	if (dot == std::string::npos) {
		dot = base.find(".sms");
	}
	if (dot != std::string::npos) {
		base = base.substr(0, dot);
	}
	if (base.compare(0, 5, "level") != 0) {
		return -1;
	}
	size_t digit = 5;
	if (digit < base.size() && (base[digit] == 'x' || base[digit] == 'X')) {
		++digit;
	}
	std::string num;
	for (size_t i = digit; i < base.size() && std::isdigit((unsigned char) base[i]); ++i) {
		num += base[i];
	}
	if (num.empty()) {
		return -1;
	}
	const int n = std::stoi(num);
	const bool exp = dir.find("-exp") != std::string::npos;
	const bool orc = dir.compare(0, 3, "orc") == 0;
	if (n < 1 || n > (exp ? 12 : 14)) {
		return -1;
	}
	return (exp ? 28 : 0) + 2 * (n - 1) + (orc ? 1 : 0);
}

void ApplyUnitBit(const std::unordered_map<std::string, CUnitType *> &byIdent,
                  const char *ident, bool allowed, int &applied)
{
	if (!ident) {
		return;
	}
	auto it = byIdent.find(ident);
	if (it == byIdent.end()) {
		return; // race not loaded, or the type is not defined in this data set
	}
	for (int p = 0; p < PlayerMax; ++p) {
		AllowUnitId(Players[p], it->second->Slot, allowed ? WC2_UNIT_ALLOW_MAX : 0);
	}
	++applied;
}

void ApplyUpgradeBit(const char *ident, bool allowed, int &applied)
{
	if (!ident) {
		return;
	}
	const int id = UpgradeIdByIdent(ident);
	if (id == -1) {
		return;
	}
	for (int p = 0; p < PlayerMax; ++p) {
		// Never downgrade an upgrade the map already marked researched.
		if (!allowed && UpgradeIdAllowed(Players[p], id) == 'R') {
			continue;
		}
		AllowUpgradeId(Players[p], id, allowed ? 'A' : 'F');
	}
	++applied;
}

// ---- P1: the rule of the running mission ---------------------------------------------------------

/// State of the installed rule. Not saved with a game: after a load the gate and the arrivals are
/// found again from the map as it then stands (see MISSION_RULES_P1.md, "Save and load").
struct LiveRule {
	bool on = false;
	RuleDef def;
	int arrivalsNeeded = 0;
	bool gateOpen = false;
	std::vector<int> arrived;                    /// unit numbers already counted on the circle
	const CUnitType *type[WC2_PUD_TYPES] = {};   /// PUD type number -> engine type, null if undefined
	bool typesResolved = false;
	std::vector<uint16_t> region;                /// the map's REGM section
	int regionW = 0, regionH = 0;
	unsigned long lastCycle = 0;
	int lastOutcome = 0;
};

LiveRule g_live;

void ResolveRuleTypes()
{
	if (g_live.typesResolved) {
		return;
	}
	g_live.typesResolved = true;
	for (int t = 0; t < WC2_PUD_TYPES; ++t) {
		const char *ident = wc2pud::UnitScriptNames[t];
		g_live.type[t] = (ident && ident[0]) ? UnitTypeByIdentOrNull(ident) : nullptr;
	}
}

bool TypeIn(const CUnitType *type, const std::vector<uint8_t> &types)
{
	for (uint8_t t : types) {
		if (g_live.type[t] == type) {
			return true;
		}
	}
	return false;
}

uint16_t SlotsOf(const RuleWho &who)
{
	const int self = ThisPlayer ? ThisPlayer->Index : -1;
	uint16_t m = who.mask;
	if (who.others) {
		m = (uint16_t)(((1u << WC2_RULE_SLOTS) - 1) & ~who.mask);
		if (self >= 0) {
			m &= (uint16_t) ~(1u << self);
		}
	} else if (who.self && self >= 0) {
		m |= (uint16_t)(1u << self);
	}
	return m;
}

/// A census row counts a unit once it is complete (the original adds a unit to its type row only
/// with unit state bit 0x80 set, VA 0x4A1D37); the two totals count every unit.
bool Finished(const CUnit &u)
{
	return u.IsAlive() && u.CurrentAction() != UnitAction::Built;
}

int CountTypes(uint16_t slots, const std::vector<uint8_t> &types)
{
	int n = 0;
	for (int p = 0; p < PlayerMax; ++p) {
		if (!((slots >> p) & 1)) {
			continue;
		}
		for (const CUnit *u : Players[p].GetUnits()) {
			if (Finished(*u) && TypeIn(u->Type, types)) {
				++n;
			}
		}
	}
	return n;
}

bool IsBuilding(const CUnit &u)
{
	// Walls are map tiles in the original, not units; here they are units and are left out, as in
	// GetNumOpponents (game/trigger.cpp).
	return u.Type->Building && !u.Type->BoolFlag[WALL_INDEX].value;
}

bool HasBuilding(int p)
{
	for (const CUnit *u : Players[p].GetUnits()) {
		if (u->IsAlive() && IsBuilding(*u)) {
			return true;
		}
	}
	return false;
}

/// "Eliminated": no building, and no unit left once flying machines, transports and tankers are
/// taken off (the evaluator's subtraction, VA 0x4DDA8F).
bool Eliminated(int p)
{
	for (const CUnit *u : Players[p].GetUnits()) {
		if (!u->IsAlive() || u->Type->BoolFlag[WALL_INDEX].value) {
			continue;
		}
		if (IsBuilding(*u) || !TypeIn(u->Type, g_code.carrierTypes)) {
			return false;
		}
	}
	return true;
}

/// The Circle of Power units of the map (PUD unit type 100 in wc2pud::UnitScriptNames).
std::vector<const CUnit *> Circles()
{
	std::vector<const CUnit *> out;
	const CUnitType *circle = UnitTypeByIdentOrNull("unit-circle-of-power");
	if (!circle) {
		return out;
	}
	for (int p = 0; p < PlayerMax; ++p) {
		for (const CUnit *u : Players[p].GetUnits()) {
			if (u->Type == circle && u->IsAlive()) {
				out.push_back(u);
			}
		}
	}
	return out;
}

/// Count the player's units that stand on a circle; a unit counts once and stays counted (the
/// original bumps a counter and clears the unit's flag, VA 0x4A7732). The original scans the
/// circle's own tiles; where the circle type cannot be entered the tiles next to it count instead.
int Arrivals(const RuleTerm &t)
{
	if (!ThisPlayer) {
		return 0;
	}
	for (const CUnit *circle : Circles()) {
		const int reach = circle->Type->BoolFlag[NONSOLID_INDEX].value ? 0 : 1;
		for (const CUnit *u : ThisPlayer->GetUnits()) {
			if (!u->IsAliveOnMap() || u->Type->Building) {
				continue;
			}
			if (t.types.empty() ? u->RescuedFrom == nullptr : !TypeIn(u->Type, t.types)) {
				continue;
			}
			const int id = UnitNumber(*u);
			if (u->MapDistanceTo(*circle) <= reach
			    && std::find(g_live.arrived.begin(), g_live.arrived.end(), id) == g_live.arrived.end()) {
				g_live.arrived.push_back(id);
			}
		}
	}
	return (int) g_live.arrived.size();
}

/// Rule 0x0A: a finished unit of each type has at least one tile whose region word equals the one
/// under the Circle of Power (the original keeps that word at map load, VA 0x4BAF22, and tests the
/// buildings' tiles against it, VA 0x4A1760). The region words are the map's own REGM section.
bool InCircleRegion(const RuleTerm &t)
{
	const std::vector<const CUnit *> circles = Circles();
	if (!ThisPlayer || circles.empty() || g_live.region.empty()) {
		return false;
	}
	const auto word = [](int x, int y) -> int {
		if (x < 0 || y < 0 || x >= g_live.regionW || y >= g_live.regionH) {
			return -1;
		}
		return g_live.region[(size_t) y * g_live.regionW + x];
	};
	const int target = word(circles[0]->tilePos.x, circles[0]->tilePos.y);
	if (target < 0) {
		return false;
	}
	for (uint8_t type : t.types) {
		bool found = false;
		for (const CUnit *u : ThisPlayer->GetUnits()) {
			if (found || !Finished(*u) || u->Type != g_live.type[type]) {
				continue;
			}
			for (int dy = 0; dy < u->Type->TileHeight && !found; ++dy) {
				for (int dx = 0; dx < u->Type->TileWidth && !found; ++dx) {
					found = word(u->tilePos.x + dx, u->tilePos.y + dy) == target;
				}
			}
		}
		if (!found) {
			return false;
		}
	}
	return true;
}

bool TermHolds(const RuleTerm &t)
{
	const uint16_t slots = SlotsOf(t.who);
	bool v = false;
	switch (t.kind) {
		case TermKind::CountGe: v = CountTypes(slots, t.types) >= t.n; break;
		case TermKind::CountZero: v = CountTypes(slots, t.types) == 0; break;
		case TermKind::Eliminated:
		case TermKind::NoBuilding:
		case TermKind::Poor:
			v = true;
			for (int p = 0; p < PlayerMax && v; ++p) {
				if (!((slots >> p) & 1)) {
					continue;
				}
				v = t.kind == TermKind::Eliminated ? Eliminated(p)
				  : t.kind == TermKind::NoBuilding ? !HasBuilding(p)
				  : Players[p].Resources[GoldCost] < t.n;
			}
			break;
		case TermKind::Arrived: v = Arrivals(t) >= (g_live.def.arrivalByParam ? g_live.arrivalsNeeded : t.n); break;
		case TermKind::InRegion: v = InCircleRegion(t); break;
		case TermKind::TookOver:
			if (ThisPlayer) {
				for (const CUnit *u : ThisPlayer->GetUnits()) {
					if (u->IsAlive() && u->RescuedFrom != nullptr) {
						v = true;
						break;
					}
				}
			}
			break;
	}
	return t.negate ? !v : v;
}

/// Every term is evaluated, also after one has failed: an arrival is counted when it happens, not
/// only once the rest of the rule holds.
bool AllHold(const std::vector<RuleTerm> &terms)
{
	bool all = true;
	for (const RuleTerm &t : terms) {
		all = TermHolds(t) && all;
	}
	return all;
}

/// 0 nothing, 1 defeat, 2 victory -- the values the original's evaluator returns (VA 0x4DDA6F, 0x4DDAC1).
int EvaluateRule()
{
	ResolveRuleTypes();
	for (const RuleTerm &t : g_live.def.lose) {
		if (TermHolds(t)) {
			return 1;
		}
	}
	if (!g_live.def.gate.empty() && !g_live.gateOpen) {
		g_live.gateOpen = AllHold(g_live.def.gate);
		if (!g_live.gateOpen) {
			return 0;
		}
	}
	if (AllHold(g_live.def.win)) {
		return 2;
	}
	if (!g_live.def.winAlt.empty() && AllHold(g_live.def.winAlt)) {
		return 2;
	}
	return 0;
}

/// Read the REGM section (one 16-bit word per tile) of the mission's own map file.
void LoadRegionMap(const fs::path &mapPath)
{
	g_live.region.clear();
	g_live.regionW = g_live.regionH = 0;
	const fs::path pud = ResolveMapPud(mapPath.string());
	if (pud.empty()) {
		return;
	}
	std::ifstream in(pud, std::ios::binary);
	std::string d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	int w = 0, h = 0;
	size_t regm = 0, regmLen = 0;
	for (size_t o = 0; o + 8 <= d.size();) {
		const uint32_t len = rdU32(d, o + 4);
		if (o + 8 + (size_t) len > d.size()) {
			break;
		}
		if (memcmp(d.data() + o, "DIM ", 4) == 0 && len >= 4) {
			w = rdU16(d, o + 8);
			h = rdU16(d, o + 10);
		} else if (memcmp(d.data() + o, "REGM", 4) == 0) {
			regm = o + 8;
			regmLen = len;
		}
		o += 8 + (size_t) len;
	}
	if (w <= 0 || h <= 0 || regmLen != (size_t) w * h * 2) {
		return;
	}
	g_live.region.resize((size_t) w * h);
	for (size_t k = 0; k < g_live.region.size(); ++k) {
		g_live.region[k] = rdU16(d, regm + k * 2);
	}
	g_live.regionW = w;
	g_live.regionH = h;
}

/// Install the decoded rule of the mission's rule id, if there is one.
void InstallRule(const fs::path &mapPath)
{
	g_live = LiveRule();
	const int id = g_rules.victoryId;
	if (!g_code.valid || id < 0 || id >= 0x1F || !g_code.defs[id].ok) {
		return;
	}
	const RuleDef &def = g_code.defs[id];
	if (def.arrivalByParam) {
		// Parameter bit 0x1000 asks for a hero-class unit among the arrivals (VA 0x4A7711); no rule
		// read here uses it, and a parameter that does is left to the existing rules.
		if (g_rules.victoryParam == 0 || g_rules.victoryParam >= 0x1000) {
			return;
		}
		g_live.arrivalsNeeded = g_rules.victoryParam;
	}
	bool needsRegion = false;
	for (const RuleTerm &t : def.win) {
		needsRegion = needsRegion || t.kind == TermKind::InRegion;
	}
	if (needsRegion) {
		LoadRegionMap(mapPath);
		if (g_live.region.empty()) {
			return;
		}
	}
	// A type the rule names but the data does not define would make "none of them" true at once.
	ResolveRuleTypes();
	for (const std::vector<RuleTerm> *list : {&def.win, &def.winAlt, &def.lose, &def.gate}) {
		for (const RuleTerm &t : *list) {
			for (uint8_t type : t.types) {
				if (!g_live.type[type]) {
					DebugPrint("InstallRule: rule %02x names undefined unit type %d, not installed\n", id, type);
					g_live = LiveRule();
					return;
				}
			}
		}
	}
	g_live.def = def;
	g_live.on = true;
}

const char *TermName(TermKind k)
{
	switch (k) {
		case TermKind::CountGe: return "count>=";
		case TermKind::CountZero: return "none";
		case TermKind::Eliminated: return "eliminated";
		case TermKind::NoBuilding: return "nobuilding";
		case TermKind::Arrived: return "arrived";
		case TermKind::InRegion: return "inregion";
		case TermKind::TookOver: return "tookover";
		case TermKind::Poor: return "poor";
	}
	return "?";
}

/// One line per term: "<list> <kind> slots=<hex> n=<n> types=<idents> now=<0|1>". For the debug
/// bridge and the test recipes; not shown to the player.
std::string DescribeRule()
{
	std::string s;
	if (!g_live.on) {
		return s;
	}
	ResolveRuleTypes();
	const std::pair<const char *, const std::vector<RuleTerm> *> lists[] = {
		{"lose", &g_live.def.lose}, {"gate", &g_live.def.gate},
		{"win", &g_live.def.win}, {"winalt", &g_live.def.winAlt}};
	for (const auto &[name, list] : lists) {
		for (const RuleTerm &t : *list) {
			char head[96];
			const int n = t.kind == TermKind::Arrived && g_live.def.arrivalByParam ? g_live.arrivalsNeeded : t.n;
			snprintf(head, sizeof(head), "%s %s%s slots=%04x n=%d types=", name, t.negate ? "not " : "",
			         TermName(t.kind), (unsigned) SlotsOf(t.who), n);
			s += head;
			for (size_t k = 0; k < t.types.size(); ++k) {
				s += k ? "," : "";
				s += g_live.type[t.types[k]] ? wc2pud::UnitScriptNames[t.types[k]] : "(undefined)";
			}
			s += TermHolds(t) ? " now=1\n" : " now=0\n";
		}
	}
	return s;
}

/// Lua: MissionRuleOutcome() -> 0 nothing, 1 defeat, 2 victory. Evaluated once every 50 game cycles
/// (the original calls its handler every 0x32 ticks of its own counter, VA 0x4DE65A).
int CclMissionRuleOutcome(lua_State *l)
{
	LuaCheckArgs(l, 0);
	if (g_live.on && (GameCycle != g_live.lastCycle) && GameCycle % 50 == 0) {
		g_live.lastCycle = GameCycle;
		g_live.lastOutcome = EvaluateRule();
	}
	lua_pushnumber(l, g_live.on ? g_live.lastOutcome : 0);
	return 1;
}

/// Lua: MissionCircleArrivals([needHero]) -> how many of the player's units taken over from a
/// rescue slot have stood on a Circle of Power (counted once each, as Arrivals does). With needHero
/// the count is 0 until one of them is of a hero type (parameter bit 0x1000 of the generic rule,
/// VA 0x4A7711; "hero" is the unit type's own flag). A summoned unit was never taken over and
/// does not count.
int CclMissionCircleArrivals(lua_State *l)
{
	const bool needHero = lua_gettop(l) >= 1 && LuaToBoolean(l, 1);
	RuleTerm any;
	const int n = Arrivals(any);
	if (needHero) {
		const int flag = UnitTypeVar.BoolFlagNameLookup["hero"];
		bool hero = false;
		if (flag != -1) {
			for (const int id : g_live.arrived) {
				const CUnit &u = UnitManager->GetSlotUnit(id);
				if (u.Type != nullptr && u.Type->BoolFlag[flag].value) {
					hero = true;
					break;
				}
			}
		}
		lua_pushnumber(l, hero ? n : 0);
		return 1;
	}
	lua_pushnumber(l, n);
	return 1;
}

/// Lua: GetMissionRules() -> {index=, victory=, param=, unitAllow=, upgradeAllow=, rule=, gate=,
/// terms=} or nil. `rule` is true when the mission's own victory rule was read and installed;
/// `terms` lists it (see DescribeRule).
int CclGetMissionRules(lua_State *l)
{
	LuaCheckArgs(l, 0);
	if (!g_rules.valid) {
		lua_pushnil(l);
		return 1;
	}
	lua_newtable(l);
	lua_pushstring(l, "index");
	lua_pushnumber(l, g_rules.index);
	lua_rawset(l, -3);
	lua_pushstring(l, "victory");
	lua_pushnumber(l, g_rules.victoryId);
	lua_rawset(l, -3);
	lua_pushstring(l, "param");
	lua_pushnumber(l, g_rules.victoryParam);
	lua_rawset(l, -3);
	lua_pushstring(l, "unitAllow");
	lua_pushnumber(l, (double) g_rules.unitAllow);
	lua_rawset(l, -3);
	lua_pushstring(l, "upgradeAllow");
	lua_pushnumber(l, (double) g_rules.upgradeAllow);
	lua_rawset(l, -3);
	lua_pushstring(l, "rule");
	lua_pushboolean(l, g_live.on);
	lua_rawset(l, -3);
	lua_pushstring(l, "gate");
	lua_pushboolean(l, g_live.gateOpen);
	lua_rawset(l, -3);
	lua_pushstring(l, "terms");
	lua_pushstring(l, DescribeRule().c_str());
	lua_rawset(l, -3);
	return 1;
}

} // namespace

bool LoadWC2MissionRules(const fs::path &mapPath)
{
	g_rules = WC2MissionRules();
	g_live = LiveRule();

	const int index = MissionIndexOf(mapPath);
	if (index < 0 || index >= WC2_MISSIONS) {
		return false; // custom / melee / multiplayer map: the original allows everything
	}
	if (!EnsureRuleTables()) {
		DebugPrint("LoadWC2MissionRules: no rule tables available, mission %d keeps the generic rules\n",
		           index);
		return false;
	}
	g_rules.index = index;
	g_rules.unitAllow = g_unitAllow[index];
	g_rules.upgradeAllow = g_upgradeAllow[index];
	g_rules.spellAllow = g_spellAllow[index];
	g_rules.victoryId = g_victoryId[index];
	g_rules.victoryParam = g_victoryParam[index];
	g_rules.valid = true;
	InstallRule(mapPath);
	g_rules.ruleLoaded = g_live.on;
	DebugPrint("LoadWC2MissionRules: mission %d unit=%08x upgrade=%08x victory=%04x param=%04x rule=%d\n",
	           index, g_rules.unitAllow, g_rules.upgradeAllow, g_rules.victoryId, g_rules.victoryParam,
	           (int) g_live.on);
	return true;
}

void ApplyMissionAllow()
{
	if (!g_rules.valid) {
		return;
	}
	// UnitTypeByIdent aborts on an unknown ident, so go through the defined types instead.
	std::unordered_map<std::string, CUnitType *> byIdent;
	for (CUnitType *t : getUnitTypes()) {
		if (t) {
			byIdent.emplace(t->Ident, t);
		}
	}

	int units = 0;
	int upgrades = 0;
	for (int bit = 0; bit < 32; ++bit) {
		const bool allowed = (g_rules.unitAllow >> bit) & 1u;
		ApplyUnitBit(byIdent, UnitAllowBits[bit].human, allowed, units);
		ApplyUnitBit(byIdent, UnitAllowBits[bit].orc, allowed, units);
		for (const ExtraBit &e : UnitAllowExtra) {
			if (e.bit == bit) {
				ApplyUnitBit(byIdent, e.human, allowed, units);
				ApplyUnitBit(byIdent, e.orc, allowed, units);
			}
		}

		const bool research = (g_rules.upgradeAllow >> bit) & 1u;
		ApplyUpgradeBit(UpgradeAllowBits[bit].human, research, upgrades);
		ApplyUpgradeBit(UpgradeAllowBits[bit].orc, research, upgrades);
	}
	// The spell mask (g_rules.spellAllow) is deliberately NOT applied: six of its bits are still
	// undecoded, and a wrong bit would silently remove a spell the mission is supposed to have.
	DebugPrint("ApplyMissionAllow: mission %d gated %d unit types and %d upgrades\n",
	           g_rules.index, units, upgrades);
}

const WC2MissionRules &CurrentMissionRules()
{
	return g_rules;
}

bool MissionAllowsDamage(const CUnit *attacker, const CUnit &target)
{
	const WC2MissionRules &rules = CurrentMissionRules();
	if (!rules.valid || target.Type == nullptr || target.Type->Ident != "unit-dark-portal") {
		return true;
	}
	// Rule 0x08 (Tides of Darkness, Human XIV): the portal is the target and takes ordinary damage
	// (measured on the original: Gryphon Riders took it from 5000 to its destruction).
	// Rule 0x13 (Beyond the Dark Portal, Orc XII) is LOST when the portal is gone, so it can be
	// destroyed there; not measured on the original - left as it was (ordinary damage).
	if (rules.victoryId == 0x08 || rules.victoryId == 0x13) {
		return true;
	}
	// Any other campaign mission: the portal is scenery and nothing damages it (Beyond the Dark
	// Portal, Human IV: Knights, Demolition Squads, Fireball, a Mage - 5000 / 5000, ORIG3.md item 8;
	// Knights, Archers, Ballistas, a Gryphon Rider, ORIG_ART.md).
	if (rules.victoryId != 0x1E) {
		return false;
	}
	// The rule's hero is Khadgar (unit-white-mage).
	return attacker != nullptr && attacker->Type != nullptr && attacker->Type->Ident == "unit-white-mage";
}

void ClearMissionRules()
{
	g_rules = WC2MissionRules();
	g_live = LiveRule();
}

void WC2MissionCclRegister()
{
	lua_register(Lua, "GetMissionRules", CclGetMissionRules);
	lua_register(Lua, "MissionRuleOutcome", CclMissionRuleOutcome);
	lua_register(Lua, "MissionCircleArrivals", CclMissionCircleArrivals);
}

//@}
