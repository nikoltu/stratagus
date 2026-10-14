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
/**@name test_wc2mission.cpp - The test file for wc2mission.cpp. */
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

#include <doctest.h>

#include "stratagus.h"

#include "wc2mission.h"
#include "unit.h"

#include <initializer_list>
#include <string>
#include <vector>

namespace
{

// The code below is made up for the tests: the instruction forms are the ones the reader looks for,
// the addresses, rule ids, masks, slots and unit types are arbitrary.
constexpr uint32_t CodeVa = 0x1000;
constexpr uint32_t LocalPlayer = 0x4000; // byte global: the local player's slot
constexpr uint32_t State = 0x5000;       // dword array: one spell-state mask per player

struct Code {
	std::string bytes;

	Code &b(std::initializer_list<int> list)
	{
		for (int v : list) {
			bytes.push_back((char) v);
		}
		return *this;
	}
	Code &u32(uint32_t v)
	{
		for (int k = 0; k < 4; ++k) {
			bytes.push_back((char)((v >> (8 * k)) & 0xFF));
		}
		return *this;
	}
	uint32_t here() const { return CodeVa + (uint32_t) bytes.size(); }
	/// cmp cx, id / jne over the next `skip` bytes
	Code &ruleId(int id, int skip) { return b({0x66, 0x83, 0xF9, id, 0x75, skip}); }
	/// movzx eax, byte [global]
	Code &loadSlot(uint32_t global) { return b({0x0F, 0xB6, 0x05}).u32(global); }
	/// call rel32; the target is patched later
	size_t call()
	{
		b({0xE8});
		u32(0);
		return bytes.size() - 4;
	}
	void patch(size_t at, uint32_t target)
	{
		const uint32_t rel = target - (CodeVa + (uint32_t) at + 4);
		for (int k = 0; k < 4; ++k) {
			bytes[at + k] = (char)((rel >> (8 * k)) & 0xFF);
		}
	}
};

} // namespace

TEST_CASE("wc2mission: spell-state mask to spell upgrades")
{
	uint32_t unknown = 1;
	std::vector<std::string> got = MissionSpellGrantUpgrades(0x0A, &unknown);
	CHECK(got == std::vector<std::string>{"upgrade-healing", "upgrade-exorcism"});
	CHECK(unknown == 0);

	got = MissionSpellGrantUpgrades(0xC00, &unknown);
	CHECK(got == std::vector<std::string>{"upgrade-eye-of-kilrogg", "upgrade-bloodlust"});
	CHECK(unknown == 0);

	// Bits without an established meaning grant nothing and are reported.
	got = MissionSpellGrantUpgrades((1u << 2) | (1u << 12) | (1u << 20) | (1u << 31), &unknown);
	CHECK(got.empty());
	CHECK(unknown == ((1u << 2) | (1u << 12) | (1u << 20) | (1u << 31)));

	// The two remaining Death Knight spells, each by its own bit.
	got = MissionSpellGrantUpgrades(1u << 14, &unknown);
	CHECK(got == std::vector<std::string>{"upgrade-death-coil"});
	CHECK(unknown == 0);
	got = MissionSpellGrantUpgrades(1u << 19, &unknown);
	CHECK(got == std::vector<std::string>{"upgrade-death-and-decay"});
	CHECK(unknown == 0);
	// A mask of two spells of two casters: the Fireball and the Death Coil.
	got = MissionSpellGrantUpgrades((1u << 5) | (1u << 14), &unknown);
	CHECK(got == std::vector<std::string>{"upgrade-fireball", "upgrade-death-coil"});
	CHECK(unknown == 0);
	got = MissionSpellGrantUpgrades((1u << 14) | (1u << 19) | 1u, &unknown);
	CHECK(got == std::vector<std::string>{"upgrade-holy-vision", "upgrade-death-coil", "upgrade-death-and-decay"});
	CHECK(unknown == 0);

	CHECK(MissionSpellGrantUpgrades(0).empty());
}

TEST_CASE("wc2mission: start actions read from dispatcher code")
{
	Code c;
	c.b({0x66, 0x85, 0xC9, 0x75, 0x05, 0xB8}).u32(0x2000);           // test cx,cx / jne / mov eax,imm
	c.ruleId(5, 15).loadSlot(LocalPlayer);
	c.b({0x83, 0x0C, 0x85}).u32(State).b({0x22});                     // or dword [eax*4+state], imm8
	c.ruleId(6, 10).b({0x81, 0x0D}).u32(State + 8).u32(0x223344);     // or dword [state + 2*4], imm32
	c.ruleId(7, 13).b({0x6A, 0x01, 0x6A, 0x0C, 0x6A, 0x06, 0x6A, 0x03}); // push flag, to, from, slot
	const size_t callConvert = c.call();
	c.ruleId(8, 5);
	const size_t callHelper = c.call();
	c.b({0xC3});
	const uint32_t convert = c.here();
	c.b({0xC3});
	const uint32_t helper = c.here();
	c.loadSlot(LocalPlayer).b({0x6A, 0x00, 0x6A, 0x0D, 0x6A, 0x07});
	c.b({0x81, 0x0C, 0x85}).u32(State).u32(0xC00);                    // or dword [eax*4+state], imm32
	c.b({0x50});                                                      // push eax: the local player
	const size_t callConvert2 = c.call();
	c.b({0xC3});
	c.patch(callConvert, convert);
	c.patch(callHelper, helper);
	c.patch(callConvert2, convert);

	CHECK(MissionStartActionsOfCode(c.bytes, CodeVa, State)
	      == "05 grant P 00000022\n"
	         "06 grant 2 00223344\n"
	         "07 convert 3 6 12 1\n"
	         "08 grant P 00000c00\n"
	         "08 convert P 7 13 0\n");
	// Another array is not the spell state: no grant is read.
	CHECK(MissionStartActionsOfCode(c.bytes, CodeVa, State + 0x1000) == "07 convert 3 6 12 1\n08 convert P 7 13 0\n");
}

TEST_CASE("wc2mission: the fourth argument of a conversion is read as written")
{
	// Two rule ids convert the same types for the same slot and differ only in the last argument.
	Code c;
	c.ruleId(3, 13).b({0x6A, 0x00, 0x6A, 0x09, 0x6A, 0x04, 0x6A, 0x05}); // push 0, to, from, slot
	const size_t first = c.call();
	c.ruleId(4, 13).b({0x6A, 0x01, 0x6A, 0x09, 0x6A, 0x04, 0x6A, 0x05}); // push 1, to, from, slot
	const size_t second = c.call();
	c.b({0xC3});
	const uint32_t convert = c.here();
	c.b({0xC3});
	c.patch(first, convert);
	c.patch(second, convert);
	CHECK(MissionStartActionsOfCode(c.bytes, CodeVa, State) == "03 convert 5 4 9 0\n04 convert 5 4 9 1\n");

	// A unit type that is not a constant: nothing is kept.
	Code e;
	e.ruleId(3, 12).b({0x6A, 0x00, 0x6A, 0x09, 0x51, 0x6A, 0x05});       // push 0, to, ecx, slot
	const size_t call = e.call();
	e.b({0xC3});
	const uint32_t target = e.here();
	e.b({0xC3});
	e.patch(call, target);
	CHECK(MissionStartActionsOfCode(e.bytes, CodeVa, State) == "");
}

TEST_CASE("wc2mission: a grant that is not indexed by the local player is refused")
{
	// The index register was never loaded from a global.
	Code c;
	c.ruleId(5, 8).b({0x83, 0x0C, 0x85}).u32(State).b({0x22}).b({0xC3});
	CHECK(MissionStartActionsOfCode(c.bytes, CodeVa, State) == "");

	// Two grants indexed through two different globals.
	Code e;
	e.ruleId(5, 15).loadSlot(LocalPlayer).b({0x83, 0x0C, 0x85}).u32(State).b({0x22});
	e.ruleId(6, 15).loadSlot(LocalPlayer + 1).b({0x83, 0x0C, 0x85}).u32(State).b({0x01});
	e.b({0xC3});
	CHECK(MissionStartActionsOfCode(e.bytes, CodeVa, State) == "");
}

TEST_CASE("wc2mission: spell-state array found behind the allow installer")
{
	Code c;
	c.b({0x89, 0xB0}).u32(0x7040);                 // mov [eax+disp], esi
	c.b({0xC7, 0x80}).u32(0x6040).u32(0x77);       // mov dword [eax+disp], constant
	c.b({0xC7, 0x80}).u32(0x8040).u32(0);
	c.b({0x83, 0xF8, 0xC0});                       // cmp eax, -0x40: sixteen slots
	CHECK(MissionSpellStateBaseOfCode(c.bytes) == 0x6000);

	Code two;
	two.b({0xC7, 0x80}).u32(0x6040).u32(0x77).b({0xC7, 0x80}).u32(0x8040).u32(0x11).b({0x83, 0xF8, 0xC0});
	CHECK(MissionSpellStateBaseOfCode(two.bytes) == 0);

	Code shortLoop;
	shortLoop.b({0xC7, 0x80}).u32(0x6040).u32(0x77).b({0x83, 0xF8, 0xE0});
	CHECK(MissionSpellStateBaseOfCode(shortLoop.bytes) == 0);

	CHECK(MissionSpellStateBaseOfCode(std::string("\xC3", 1)) == 0);
}

TEST_CASE("wc2mission: unit loops and the stance change read from dispatcher code")
{
	constexpr uint32_t Lists = 0x6000; // dword array: one unit list head per slot
	constexpr uint32_t Matrix = 0x7000; // 16 x 16 bytes
	Code c;
	// id 3: the local player and slot 5 become enemies - [P*16 + 5] and [5*16 + P]
	c.ruleId(3, 26).loadSlot(LocalPlayer).b({0x8B, 0xC8, 0xC1, 0xE0, 0x04}); // mov ecx,eax / shl eax,4
	c.b({0xC6, 0x80}).u32(Matrix + 5).b({0x00});                            // mov byte [eax+..], 0
	c.b({0xC6, 0x81}).u32(Matrix + 5 * 16).b({0x00});                       // mov byte [ecx+..], 0
	// id 7: the units of type 9 in slot 2's list lose the mark
	c.ruleId(7, 26).b({0x8B, 0x35}).u32(Lists + 2 * 4);                     // mov esi, [lists + 2*4]
	c.b({0xBF}).u32(0xFEFF);                                                // mov edi, 0xFEFF
	c.b({0x80, 0x7E, 0x27, 0x09, 0x75, 0x04});                              // cmp byte [esi+0x27], 9 / jne
	c.b({0x66, 0x21, 0x7E, 0x1C});                                          // and word [esi+0x1C], di
	c.b({0x8B, 0x76, 0x68, 0x85, 0xF6, 0x75, 0xEF});                        // mov esi,[esi+0x68] / test / jne
	// id 8: the units of type 11 in slot 15's list get it
	c.ruleId(8, 26).b({0x8B, 0x35}).u32(Lists + 15 * 4);
	c.b({0xBF}).u32(0x100);
	c.b({0x80, 0x7E, 0x27, 0x0B, 0x75, 0x04});
	c.b({0x66, 0x09, 0x7E, 0x1C});                                          // or word [esi+0x1C], di
	c.b({0x8B, 0x76, 0x68, 0x85, 0xF6, 0x75, 0xEF});
	// id 9: a routine sets the hit points of type 12 in the lists of slots 0..5
	c.ruleId(9, 5);
	const size_t callLoop = c.call();
	c.b({0xC3});
	const uint32_t loop = c.here();
	c.b({0xB9}).u32(Lists).b({0xBA}).u32(300);                              // mov ecx, lists / mov edx, 300
	c.b({0x8B, 0x01, 0x85, 0xC0, 0x74, 0x11});                              // mov eax,[ecx] / test / je
	c.b({0x80, 0x78, 0x27, 0x0C, 0x75, 0x04});                              // cmp byte [eax+0x27], 12 / jne
	c.b({0x66, 0x89, 0x50, 0x22});                                          // mov word [eax+0x22], dx
	c.b({0x8B, 0x40, 0x68, 0x85, 0xC0, 0x75, 0xEF});                        // mov eax,[eax+0x68] / test / jne
	c.b({0x83, 0xC1, 0x04, 0x81, 0xF9}).u32(Lists + 6 * 4).b({0x7C, 0xDE, 0xC3}); // add ecx,4 / cmp / jl / ret
	c.patch(callLoop, loop);

	CHECK(MissionStartActionsOfCode(c.bytes, CodeVa, State, Lists)
	      == "03 enemy P 5\n07 mark 2 9 0\n08 mark 15 11 1\n09 hitpoints 0-5 12 300\n");
	// Without the list heads the loops are not read; the stance change still is.
	CHECK(MissionStartActionsOfCode(c.bytes, CodeVa, State) == "03 enemy P 5\n");
}

TEST_CASE("wc2mission: a unit loop or stance change of another form turns the start actions off")
{
	constexpr uint32_t Lists = 0x6000;
	// Another bit of the flag word.
	Code c;
	c.ruleId(7, 21).b({0x8B, 0x35}).u32(Lists + 2 * 4).b({0xBF}).u32(0x200);
	c.b({0x80, 0x7E, 0x27, 0x09, 0x75, 0x04, 0x66, 0x09, 0x7E, 0x1C, 0xC3});
	CHECK(MissionStartActionsOfCode(c.bytes, CodeVa, State, Lists) == "");

	// Another field of the unit.
	Code f;
	f.ruleId(7, 21).b({0x8B, 0x35}).u32(Lists + 2 * 4).b({0xBF}).u32(0x100);
	f.b({0x80, 0x7E, 0x27, 0x09, 0x75, 0x04, 0x66, 0x09, 0x7E, 0x1E, 0xC3});
	CHECK(MissionStartActionsOfCode(f.bytes, CodeVa, State, Lists) == "");

	// No unit type compared.
	Code t;
	t.ruleId(7, 15).b({0x8B, 0x35}).u32(Lists + 2 * 4).b({0xBF}).u32(0x100);
	t.b({0x66, 0x09, 0x7E, 0x1C, 0xC3});
	CHECK(MissionStartActionsOfCode(t.bytes, CodeVa, State, Lists) == "");

	// One half of a stance change.
	Code h;
	h.ruleId(3, 14).loadSlot(LocalPlayer).b({0xC6, 0x80}).u32(0x7050).b({0x00}).b({0xC3});
	CHECK(MissionStartActionsOfCode(h.bytes, CodeVa, State, Lists) == "");

	// Two cells that are not one slot's pair.
	Code p;
	p.ruleId(3, 26).loadSlot(LocalPlayer).b({0x8B, 0xC8, 0xC1, 0xE0, 0x04});
	p.b({0xC6, 0x80}).u32(0x7005).b({0x00}).b({0xC6, 0x81}).u32(0x7051).b({0x00}).b({0xC3});
	CHECK(MissionStartActionsOfCode(p.bytes, CodeVa, State, Lists) == "");

	// A stance other than "enemy".
	Code v;
	v.ruleId(3, 26).loadSlot(LocalPlayer).b({0x8B, 0xC8, 0xC1, 0xE0, 0x04});
	v.b({0xC6, 0x80}).u32(0x7005).b({0x01}).b({0xC6, 0x81}).u32(0x7050).b({0x01}).b({0xC3});
	CHECK(MissionStartActionsOfCode(v.bytes, CodeVa, State, Lists) == "");
}

TEST_CASE("wc2mission: the generic arrival rule is won, lost or open")
{
	// Four units to bring, no hero asked for.
	CHECK(MissionArrivalOutcome(4, 0, false, 8, false) == 0);
	CHECK(MissionArrivalOutcome(4, 3, false, 1, false) == 0);
	CHECK(MissionArrivalOutcome(4, 4, false, 0, false) == 2);
	CHECK(MissionArrivalOutcome(4, 5, false, 0, false) == 2);
	// Too few are left to bring.
	CHECK(MissionArrivalOutcome(4, 1, false, 2, false) == 1);
	CHECK(MissionArrivalOutcome(4, 0, false, 0, false) == 1);
	// A hero that arrived changes nothing when none is asked for; a living one among the units
	// still to bring is added with its own weight whatever the parameter, so the rule stays open.
	CHECK(MissionArrivalOutcome(4, 1, true, 2, false) == 1);
	CHECK(MissionArrivalOutcome(4, 4, true, 0, false) == 2);
	CHECK(MissionArrivalOutcome(4, 1, false, 2, true) == 0);

	// One unit to bring, and a hero among the arrivals.
	const int hero = 0x1001;
	CHECK(MissionArrivalOutcome(hero, 0, false, 3, true) == 0);
	// Others arrived, the hero still lives: open.
	CHECK(MissionArrivalOutcome(hero, 2, false, 1, true) == 0);
	// The hero arrived: won, whoever else lives.
	CHECK(MissionArrivalOutcome(hero, 1, true, 0, false) == 2);
	CHECK(MissionArrivalOutcome(hero, 3, true, 4, false) == 2);
	// The hero is dead: lost, however many others are left or arrived.
	CHECK(MissionArrivalOutcome(hero, 0, false, 5, false) == 1);
	CHECK(MissionArrivalOutcome(hero, 2, false, 5, false) == 1);
	CHECK(MissionArrivalOutcome(hero, 0, false, 0, false) == 1);
}

TEST_CASE("wc2mission: the generic destroy-all rule is won over the tested slots only")
{
	MissionSlotCensus slots[3];
	slots[0].tested = true;
	slots[1].tested = true;
	slots[2].buildings = 4; // not tested (not at war with the player): does not hold the win
	slots[2].units = 9;
	CHECK(MissionGenericWinHolds(1, false, slots, 3));
	// carriers alone do not keep a slot in the game
	slots[0].units = 3;
	slots[0].carriers = 3;
	CHECK(MissionGenericWinHolds(1, false, slots, 3));
	slots[0].units = 4;
	CHECK_FALSE(MissionGenericWinHolds(1, false, slots, 3));
	slots[0].units = 3;
	slots[1].buildings = 1;
	CHECK_FALSE(MissionGenericWinHolds(1, false, slots, 3));
	slots[1].buildings = 0;
	// not tested while the player has no unit that is no building, trains or has something unfinished
	CHECK_FALSE(MissionGenericWinHolds(0, false, slots, 3));
	CHECK_FALSE(MissionGenericWinHolds(1, true, slots, 3));
	slots[2].tested = true;
	CHECK_FALSE(MissionGenericWinHolds(1, false, slots, 3));
}

TEST_CASE("wc2mission: the generic destroy-all rule waits for a rescue slot that is still its own")
{
	CHECK(MissionSlotTested(false, true, false));        // at war
	CHECK(MissionSlotTested(false, false, true));        // a rescue slot
	CHECK_FALSE(MissionSlotTested(false, false, false)); // an allied computer
	CHECK_FALSE(MissionSlotTested(true, false, false));  // the player's own slot
	CHECK_FALSE(MissionSlotTested(true, true, true));

	MissionSlotCensus slots[2];
	slots[0].tested = MissionSlotTested(false, true, false); // the enemy: nothing left
	slots[1].tested = MissionSlotTested(false, false, true); // the rescue slot holds a building
	slots[1].buildings = 1;
	CHECK_FALSE(MissionGenericWinHolds(1, false, slots, 2));
	slots[1].buildings = 0;                                  // all of it taken over
	CHECK(MissionGenericWinHolds(1, false, slots, 2));
}

TEST_CASE("unit: who can be taken over, and by whom")
{
	// A unit of a rescue player can, unless a start action took the mark away.
	CHECK(UnitCanBeTakenOver(0, PlayerTypes::PlayerRescuePassive));
	CHECK(UnitCanBeTakenOver(0, PlayerTypes::PlayerRescueActive));
	CHECK_FALSE(UnitCanBeTakenOver(1, PlayerTypes::PlayerRescuePassive));
	CHECK_FALSE(UnitCanBeTakenOver(1, PlayerTypes::PlayerRescueActive));
	// A unit of another player cannot, unless a start action gave it the mark.
	CHECK_FALSE(UnitCanBeTakenOver(0, PlayerTypes::PlayerNeutral));
	CHECK_FALSE(UnitCanBeTakenOver(0, PlayerTypes::PlayerComputer));
	CHECK(UnitCanBeTakenOver(2, PlayerTypes::PlayerNeutral));
	CHECK(UnitCanBeTakenOver(2, PlayerTypes::PlayerComputer));
	// A person's unit never.
	CHECK_FALSE(UnitCanBeTakenOver(0, PlayerTypes::PlayerPerson));
	CHECK_FALSE(UnitCanBeTakenOver(2, PlayerTypes::PlayerPerson));

	// A mobile unit of a person that can attack takes over (a worker as well as a soldier); a
	// building or a scout flyer never.
	CHECK(UnitCanTakeOver(PlayerTypes::PlayerPerson, false, true));
	CHECK_FALSE(UnitCanTakeOver(PlayerTypes::PlayerPerson, false, false));
	CHECK_FALSE(UnitCanTakeOver(PlayerTypes::PlayerPerson, true, true));
	CHECK_FALSE(UnitCanTakeOver(PlayerTypes::PlayerComputer, false, true));
	CHECK_FALSE(UnitCanTakeOver(PlayerTypes::PlayerRescueActive, false, true));
	CHECK_FALSE(UnitCanTakeOver(PlayerTypes::PlayerRescuePassive, false, true));
	CHECK_FALSE(UnitCanTakeOver(PlayerTypes::PlayerNeutral, false, true));
}
