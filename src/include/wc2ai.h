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
/**@name wc2ai.h - The original game's computer-player definitions (Data/Rez/ai.bin). */
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

#ifndef WC2AI_H
#define WC2AI_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// The file is the player's own (read from the game folder at run time, never shipped). Layout:
///   u16 offset[83]   script entry per AI id 0x00..0x52 (the PUD "AIPL" byte of a player slot)
///   u16 offset[2]    two more table words
///   script: u16 -> build list (bytes, 0xFF ends it, at most 64 used)
///           u16 -> cadence table (35 x u16, one per building type 0x3A..0x5C, 0xFFFF = never)
///           bytecode, four instructions:
///             00 pp vv         parameter pp := vv
///             01 lo hi         continue at file offset
///             02 n0 n1 n2 n3   wait n game cycles
///             03 cc            wait until condition cc holds
namespace wc2ai
{

constexpr int ScriptCount = 83;      ///< AI ids 0x00..0x52
constexpr int BuildListMax = 64;     ///< entries of a build list that are used
constexpr int CadenceCount = 35;     ///< building types 0x3A..0x5C
constexpr int ParamFirst = 0x09;     ///< first parameter a script sets
constexpr int ParamLast = 0x22;      ///< last one (the build-list limit)

enum class Op : uint8_t { Set = 0, Goto = 1, Wait = 2, Until = 3 };

/// One decoded instruction.
struct Instruction {
	uint16_t address = 0; ///< file offset of the instruction
	Op op = Op::Set;
	uint32_t a = 0;       ///< Set: parameter; Goto: target offset; Wait: cycles; Until: condition
	uint32_t b = 0;       ///< Set: value
	uint16_t next = 0;    ///< file offset of the instruction after this one (Goto: the target)
};

/// One script as the file holds it.
struct Script {
	uint16_t entry = 0;                 ///< file offset of the script
	uint16_t first = 0;                 ///< file offset of its first instruction (entry + 4)
	std::vector<uint8_t> buildList;     ///< building types (< 0x80) and research entries (>= 0x80)
	std::vector<uint16_t> cadence;      ///< CadenceCount values
};

/// The whole file, checked.
class Program
{
public:
	/// Take the file's bytes. False (with a reason) when the table or any script does not decode;
	/// the program is then empty.
	bool Parse(const uint8_t *data, size_t size, std::string *error = nullptr);

	bool Loaded() const { return !scripts.empty(); }
	const Script *Get(int id) const;

	/// Decode the instruction at a file offset. False when the offset is outside the file or the
	/// byte there is not one of the four instructions.
	bool Decode(uint16_t address, Instruction &out) const;

	/// How many instructions a script has when followed from its start until it comes back to an
	/// instruction already seen (every script ends in a loop).
	int InstructionCount(int id) const;

private:
	std::vector<uint8_t> bytes;
	std::vector<Script> scripts;
};

/// What one player's script has reached. The script only sets parameters and waits; what the
/// parameters make the player do is not part of this state.
struct State {
	int id = -1;                          ///< AI id, -1 = no script
	uint16_t ip = 0;                      ///< file offset of the next instruction
	uint32_t wait = 0;                    ///< cycles still to wait
	uint8_t params[ParamLast + 1] = {};   ///< parameters by number (0x09..0x22 are used)
	uint32_t executed = 0;                ///< instructions carried out so far
};

/// Put a player's state at the start of script `id`. False when there is no such script.
bool Start(const Program &program, int id, State &state);

/// One game cycle of a script: count a wait down, else carry out instructions until one waits.
/// `holds(condition)` answers a "wait until"; a null `holds` means no condition holds yet.
void Step(const Program &program, State &state, bool (*holds)(int condition, void *context), void *context);

/// The program of the game folder in use (Data/Rez/ai.bin), read once. Not loaded when the file is
/// absent or does not decode: the caller keeps its own behaviour then.
const Program &GameProgram();

} // namespace wc2ai

/// The AI id the map gave a player slot (the PUD "AIPL" byte), or -1.
int Wc2AiIdOf(int player);
/// Forget the ids (a new map is about to be loaded).
void Wc2AiClearIds();
/// Lua: SetWc2AiId(player, id), GetWc2AiId(player), GetWc2AiInfo().
void Wc2AiCclRegister();
/// One game cycle of the player's original script, when it has one (parameters only: no effect on play yet).
void Wc2AiCycle(int player);
/// Once a second: hand what the player's original script wants to the engine's own production
/// (workers, footmen / grunts, and the buildings of the active part of the build list). True when
/// the player is run by an original script: its Lua AI script must not run beside it.
bool Wc2AiSecond(int player);
/// The players' script states as Lua lines, for the saved game.
std::string Wc2AiSaveText();

#endif // WC2AI_H
