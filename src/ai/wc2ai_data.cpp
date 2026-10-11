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
/**@name wc2ai_data.cpp - Reader of the original game's computer-player definitions. */
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

#include "wc2ai.h"

#include <set>

namespace wc2ai
{

namespace
{

uint16_t U16(const std::vector<uint8_t> &d, size_t o)
{
	return static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
}

bool Fail(std::string *error, const std::string &why)
{
	if (error) {
		*error = why;
	}
	return false;
}

} // namespace

bool Program::Decode(uint16_t address, Instruction &out) const
{
	const size_t size = bytes.size();
	if (address >= size) {
		return false;
	}
	out = Instruction();
	out.address = address;
	switch (bytes[address]) {
		case 0:
			if (address + 3u > size) {
				return false;
			}
			out.op = Op::Set;
			out.a = bytes[address + 1];
			out.b = bytes[address + 2];
			out.next = static_cast<uint16_t>(address + 3);
			return true;
		case 1:
			if (address + 3u > size) {
				return false;
			}
			out.op = Op::Goto;
			out.a = U16(bytes, address + 1);
			out.next = static_cast<uint16_t>(out.a);
			return out.a < size;
		case 2:
			if (address + 5u > size) {
				return false;
			}
			out.op = Op::Wait;
			out.a = static_cast<uint32_t>(bytes[address + 1]) | (static_cast<uint32_t>(bytes[address + 2]) << 8)
			        | (static_cast<uint32_t>(bytes[address + 3]) << 16)
			        | (static_cast<uint32_t>(bytes[address + 4]) << 24);
			out.next = static_cast<uint16_t>(address + 5);
			return true;
		case 3:
			if (address + 2u > size) {
				return false;
			}
			out.op = Op::Until;
			out.a = bytes[address + 1];
			out.next = static_cast<uint16_t>(address + 2);
			return true;
		default:
			return false;
	}
}

int Program::InstructionCount(int id) const
{
	const Script *script = Get(id);
	if (!script) {
		return -1;
	}
	std::set<uint16_t> seen;
	uint16_t ip = script->first;
	int count = 0;
	while (seen.insert(ip).second) {
		Instruction in;
		if (!Decode(ip, in)) {
			return -1;
		}
		++count;
		ip = in.next;
	}
	return count;
}

const Script *Program::Get(int id) const
{
	if (id < 0 || id >= static_cast<int>(scripts.size())) {
		return nullptr;
	}
	return &scripts[id];
}

bool Program::Parse(const uint8_t *data, size_t size, std::string *error)
{
	bytes.clear();
	scripts.clear();
	const size_t tableBytes = (ScriptCount + 2) * 2;
	if (!data || size < tableBytes || size > 0xFFFF) {
		return Fail(error, "the file is too small or too large to be the definition file");
	}
	bytes.assign(data, data + size);

	std::vector<Script> parsed(ScriptCount);
	for (int id = 0; id < ScriptCount; ++id) {
		Script &s = parsed[id];
		s.entry = U16(bytes, id * 2);
		if (s.entry < tableBytes || s.entry + 4u > size) {
			bytes.clear();
			return Fail(error, "script " + std::to_string(id) + ": entry outside the file");
		}
		s.first = static_cast<uint16_t>(s.entry + 4);
		const uint16_t list = U16(bytes, s.entry);
		const uint16_t cadence = U16(bytes, s.entry + 2);
		if (list >= size || cadence + CadenceCount * 2u > size) {
			bytes.clear();
			return Fail(error, "script " + std::to_string(id) + ": build list or cadence table outside the file");
		}
		for (size_t o = list; o < size && bytes[o] != 0xFF && s.buildList.size() < BuildListMax; ++o) {
			s.buildList.push_back(bytes[o]);
		}
		s.cadence.resize(CadenceCount);
		for (int i = 0; i < CadenceCount; ++i) {
			s.cadence[i] = U16(bytes, cadence + i * 2);
		}
	}
	scripts = std::move(parsed);

	// Every script must decode to the point where it loops, with parameters in range.
	for (int id = 0; id < ScriptCount; ++id) {
		std::set<uint16_t> seen;
		uint16_t ip = scripts[id].first;
		while (seen.insert(ip).second) {
			Instruction in;
			if (!Decode(ip, in)) {
				scripts.clear();
				bytes.clear();
				return Fail(error, "script " + std::to_string(id) + ": not an instruction at offset "
				                   + std::to_string(ip));
			}
			if (in.op == Op::Set && (in.a < ParamFirst || in.a > ParamLast)) {
				scripts.clear();
				bytes.clear();
				return Fail(error, "script " + std::to_string(id) + ": parameter out of range at offset "
				                   + std::to_string(ip));
			}
			if (in.op == Op::Until && in.a > 7) {
				scripts.clear();
				bytes.clear();
				return Fail(error, "script " + std::to_string(id) + ": condition out of range at offset "
				                   + std::to_string(ip));
			}
			ip = in.next;
		}
	}
	return true;
}

bool Start(const Program &program, int id, State &state)
{
	const Script *script = program.Get(id);
	state = State();
	if (!script) {
		return false;
	}
	state.id = id;
	state.ip = script->first;
	state.params[ParamLast] = 0xFF; // the whole build list is active until a script says otherwise
	return true;
}

void Step(const Program &program, State &state, bool (*holds)(int condition, void *context), void *context)
{
	if (state.id < 0) {
		return;
	}
	if (state.wait > 0) {
		--state.wait;
		return;
	}
	// A script never carries out more than a handful of instructions between two waits; the bound
	// only keeps a damaged state from holding the game.
	for (int guard = 0; guard < 512; ++guard) {
		Instruction in;
		if (!program.Decode(state.ip, in)) {
			state.id = -1;
			return;
		}
		switch (in.op) {
			case Op::Set:
				if (in.a <= static_cast<uint32_t>(ParamLast)) {
					state.params[in.a] = static_cast<uint8_t>(in.b);
				}
				break;
			case Op::Goto:
				break;
			case Op::Wait:
				state.wait = in.a;
				state.ip = in.next;
				++state.executed;
				return;
			case Op::Until:
				if (!holds || !holds(static_cast<int>(in.a), context)) {
					return; // asked again next cycle
				}
				break;
		}
		state.ip = in.next;
		++state.executed;
	}
}

} // namespace wc2ai
