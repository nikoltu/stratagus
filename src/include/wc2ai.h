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

/// False when the script's cadence table says "never" (0xFFFF) for a building type (0x3A..0x5C,
/// the orc type odd): a building of that type never decides anything. Types outside the table: true.
bool BuildingDecides(const Script &script, unsigned buildingType);

/// What one decision of a barracks or a shipyard trains.
enum class Train : uint8_t {
	None, Footman, Archer, Ballista, Knight, Tanker, Destroyer, Transport, Battleship, Submarine
};

/// A chance of 1 in `sides`: true when the dice fall that way. Asked only where a rule has dice.
using Dice = bool (*)(int sides, void *context);

/// What a barracks sees when it decides: per class the units the player has (the upgraded form
/// counts with its class), the wanted number of the script, and whether the class can be trained
/// (archers: lumber mill; ballistas: blacksmith and lumber mill; knights: blacksmith and stables).
struct BarracksView {
	int haveFootmen = 0, haveArchers = 0, haveBallistas = 0, haveKnights = 0;
	int wantFootmen = 0, wantArchers = 0, wantBallistas = 0, wantKnights = 0;
	bool archersCan = false, ballistasCan = false, knightsCan = false;
};

/// One decision of a barracks: a footman when footmen are wanted and there is none, else a knight
/// when wanted and none, else an archer when wanted and none; then, below the wanted number, an
/// archer with a chance of 1 in 4, a ballista 1 in 8, a knight, a footman.
Train BarracksDecision(const BarracksView &view, Dice dice, void *context);

/// What a shipyard sees when it decides (battleships: foundry; submarines: inventor).
struct ShipyardView {
	int haveTankers = 0, haveDestroyers = 0, haveTransports = 0, haveBattleships = 0, haveSubmarines = 0;
	int wantTankers = 0, wantDestroyers = 0, wantTransports = 0, wantBattleships = 0, wantSubmarines = 0;
	bool battleshipsCan = false, submarinesCan = false;
};

/// One decision of a shipyard: a tanker when wanted and none, else a destroyer when wanted and
/// none, else a transport when wanted and none; then, below the wanted number, a destroyer with a
/// chance of 1 in 4, a battleship 1 in 8, a submarine 1 in 8, a transport 1 in 8, a tanker, a
/// destroyer.
Train ShipyardDecision(const ShipyardView &view, Dice dice, void *context);

/// The program of the game folder in use (Data/Rez/ai.bin), read once. Not loaded when the file is
/// absent or does not decode: the caller keeps its own behaviour then.
const Program &GameProgram();

// ---- The attack rates (Data/Rez/script.bin of the game folder, the player's own, never shipped).
// Layout used:
//   u16 offset[type]   per unit type 0x00..0x39: the type's animation table (a run of u16)
//   table entry 4      file offset of the attack animation's opcodes
//   opcodes            one byte, operands after it: 0 counts one frame and the second 0 ends the
//                      animation; 1, 7, 8, 9, 0x0C count their one operand byte; 3 continues at
//                      its u16 operand; 4, 5, 0x0D skip one byte; 6, 0x0E skip two; others nothing
// A type's rate is 10 x (length of type 0) / (its own length), the length kept as a byte.

constexpr int RateTypes = 0x3A;      ///< unit types 0x00..0x39 have a rate
constexpr int RateAttackEntry = 4;   ///< the attack animation in a type's animation table

/// Length of the animation that starts at `offset`; -1 when the walk leaves the file or does not
/// end within `size` steps.
int AnimationLength(const uint8_t *data, size_t size, uint16_t offset);
/// Fills rates[RateTypes]; false (rates all 0) when the file is too short or a walk fails.
bool AttackRates(const uint8_t *data, size_t size, uint8_t rates[RateTypes]);
/// What one unit type is worth in a fight: ((basic - 1) / 2 + piercing) * rate / rateOfGrunt; 0
/// when rate or rateOfGrunt is 0.
constexpr int StrengthWeight(int basicDamage, int piercingDamage, int rate, int rateOfGrunt)
{
	return (rate == 0 || rateOfGrunt == 0) ? 0 : ((basicDamage - 1) / 2 + piercingDamage) * rate / rateOfGrunt;
}

/// The rates of the game folder in use, RateTypes of them, read once; all 0 when the file is
/// absent or does not decode.
const uint8_t *GameAttackRates();

/// The original's number of a unit type: the place of its ident in `names` (the map converter's
/// table, which is in the original's order). -1 when the ident is not among the first `count`.
int TypeNumberOfIdent(const char *const *names, int count, const std::string &ident);

// ---- Where a wave goes. The snapshots hold a player's living units in the order of its unit list.

/// One unit of the player a wave is sent at.
struct PlaceUnit {
	int x = 0, y = 0, w = 1, h = 1;   ///< tile and size
	bool ship = false, flyer = false, fighter = false, building = false;
	bool hall = false;                ///< town hall, keep, castle, both races
	bool platform = false;            ///< oil platform, both races
	bool shipyard = false;
};
struct Place {
	bool found = false;
	int x = 0, y = 0;
};

/// A sea wave goes to the target's first oil platform; else to its last shipyard; else to its
/// ship farthest from the reference point (the first one of equals); else nowhere. The reference
/// point is the target's hall nearest to its first unit, without a hall the first unit.
Place SeaPlace(const std::vector<PlaceUnit> &target);
/// An air wave goes to the target's last hall; else to its unit farthest from the reference point
/// (the first one of equals). Found for every target that has a unit.
Place AirPlace(const std::vector<PlaceUnit> &target);

// ---- The strength map: the map in cells of 8 x 8 tiles, per cell what each of the slots 0..7
// has standing there against land and against sea, and the slot that holds the cell.

constexpr int CellTiles = 8;
constexpr int CellSlots = 8;

/// One tile a unit covers (a unit of several tiles is entered once per tile).
struct CellUnit {
	int x = 0, y = 0;
	int slot = 0;                     ///< 0..7 are counted
	bool alive = true, mobile = true, worker = false, tanker = false, transport = false, hall = false;
	bool hitsLand = false, hitsSea = false;
	int hitPoints = 0, maxHitPoints = 1, weight = 0;   ///< weight: StrengthWeight of the type
};
struct CellRecord {
	int8_t owner = -1;                ///< the slot with the greatest value that is not 0 in the cell's own column
	uint8_t kind = 2;                 ///< 0 land, 1 water, 2 neither
	int16_t land[CellSlots] = {};     ///< strength against land, per slot
	int16_t sea[CellSlots] = {};      ///< strength against sea, per slot
};
struct CellMap {
	int cellsX = 0, cellsY = 0;
	std::vector<CellRecord> cells;
};

/// What one unit adds to a column of its cell: a worker -2, a hall -1, a transport -5, a tanker
/// -2, any other building -1, a fighter that can hit the column's kind its weight by its health.
int CellValue(const CellUnit &unit, bool canHit);
/// The kind of a cell from its tiles (coast tiles are in neither number): 0 with more land than
/// water, 1 with fewer, 2 with as many.
constexpr uint8_t CellKind(int landTiles, int waterTiles)
{
	return landTiles > waterTiles ? 0 : (landTiles < waterTiles ? 1 : 2);
}
/// `kinds`: per cell 0 / 1 / 2 (more land tiles than water tiles, fewer, as many).
CellMap BuildCellMap(int mapWidth, int mapHeight, const std::vector<uint8_t> &kinds,
                     const std::vector<CellUnit> &units);
/// The cell of a tile; nullptr outside the cells.
const CellRecord *CellAt(const CellMap &cells, int x, int y);
/// Does a guard of `slot` on the tile (x, y) break off (StrengthRetreats of the home leash, asked
/// with the cell's column the unit uses: a ship the sea column, any other the land column)?
bool GuardRetreats(const CellMap &cells, int x, int y, int slot, bool ship);

/// The original measures a cell's distance to the reference unit with the cell's two axes
/// exchanged. Kept as found; one switch for the day a live check says otherwise.
constexpr bool LandPlaceSwapsAxes = true;

/// A land wave goes to the target's unit (no ship, no flying fighter, no oil platform) nearest to
/// the centre of the land cell the target holds farthest from its reference unit (its hall
/// nearest to its first unit, else its first building, else its first unit); without such a cell
/// the reference unit's own cell; without such a unit the centre itself.
Place LandPlace(int targetSlot, const std::vector<PlaceUnit> &target, const CellMap &cells);

/// The last stand: when a building dies and at most one building that is no oil platform is left
/// after it, every unit goes out. `buildings` and `oilPlatforms` are the numbers left, the dying
/// building counted out. Measured on the original (run 32, item 2, Tides Human V): with two
/// buildings and a platform left nothing moved for 88 s; at the death that left one building and
/// the platform every unit went. The decoded form "buildings - platforms < 3, asked before the
/// dying building is taken off" is this rule. The reading "platforms counted in, asked after the
/// death" fits the same numbers; the scene does not split the two.
constexpr bool AllOutNow(int buildings, int oilPlatforms)
{
	return buildings - oilPlatforms < 2;
}

// ---- What a worker that has nothing to do decides (local evidence records AI-SPLIT-002,
// AI-REPAIR-001, AI-PLACE-001; all read from the program, none seen in a game yet).

/// How many of a player's workers are wanted on lumber, from its two stocks and its number of
/// workers. A worker goes to lumber while fewer than that many cut it, else to gold.
constexpr int LumberWorkers(int gold, int lumber, int workers)
{
	return (gold < 500 && workers < 5) ? 0
	       : lumber >= 2000 ? 0
	       : gold < 1000 ? (lumber < 500 ? workers / 2 : 0)
	       : (lumber < 500 ? 3 * (workers + 1) / 4 : workers / 2);
}

/// How many workers of `workers` may repair at one time (kept in a byte, as found).
constexpr int RepairersAllowed(int workers)
{
	return static_cast<uint8_t>((workers - 1) / 2 + 1);
}

/// One unit of the player, as the repair rule sees it.
struct RepairView {
	bool finishedBuilding = false;  ///< a building that stands ready
	bool oilPlatform = false;
	bool hallClass = false;         ///< town hall, keep, castle, both races
	bool sameGround = false;        ///< a worker can walk to it
	int hp = 0, maxHp = 0;
	bool enemyFighterNear = false;  ///< an enemy that can attack in the 9 x 9 tiles from (x - 3, y - 3)
	int priority = 0;               ///< the type's priority; 0 is never repaired
};

/// The building a worker goes to repair: while fewer than RepairersAllowed repair, among the
/// ready buildings under three quarters of their hit points, on the worker's ground, no oil
/// platform, no enemy fighter near - with under 200 gold and under 100 lumber only a hall - the
/// one of the greatest priority, the first in the list of equals. Index into `units`, or -1.
int RepairPick(int gold, int lumber, int workers, int repairers, const std::vector<RepairView> &units);

/// How far apart the original tries places for a building type (its own type number, 0x3A..):
/// 2 a farm, 6 the land buildings (barracks, church, towers, stables, inventor, aviary, lumber
/// mill, mage tower, blacksmith), 1 the shore buildings and the halls, 0 a type the walk never
/// places (the oil platform) or does not know.
constexpr int PlaceStep(int buildingType)
{
	return (buildingType == 0x3A || buildingType == 0x3B) ? 2
	       : ((buildingType >= 0x3C && buildingType <= 0x47) || buildingType == 0x4C || buildingType == 0x4D
	          || (buildingType >= 0x50 && buildingType <= 0x53) || (buildingType >= 0x60 && buildingType <= 0x63)) ? 6
	       : ((buildingType >= 0x48 && buildingType <= 0x4B) || buildingType == 0x4E || buildingType == 0x4F
	          || buildingType == 0x54 || buildingType == 0x55 || (buildingType >= 0x58 && buildingType <= 0x5B)) ? 1
	       : 0;
}

/// Where a building goes: square rings round the reference tile, ring k = 1, 2, ... with its
/// corner at ref - (k * step, k * step); the four sides in the order +x, +y, -x, -y, on each
/// 2k + 1 candidates `step` tiles apart (so each corner is tried twice). A candidate off the map
/// or not on the worker's ground (`sameGround`) is passed over; the first one that `fits` is the
/// place. A ring with no candidate on the worker's ground ends the walk, and so does a ring whose
/// side (2k + 1) * step reaches the greatest of the reference tile's distances to the map's
/// edges less one (both kept in a byte, as found).
Place PlaceWalk(int refX, int refY, int step, int mapSide, bool (*sameGround)(int x, int y, void *context),
                bool (*fits)(int x, int y, void *context), void *context);

/// The casting pass of a computer's units falls on every fiftieth game cycle (local evidence
/// record PAI-001).
constexpr int CastPassCycles = 50;
constexpr bool CastPassDue(unsigned long cycle)
{
	return cycle % CastPassCycles == 0;
}

/// Conditions 4 / 5 / 6 ("enough fighters, ships, flyers for the next wave") count every living
/// fighter of the kind that the map did not fix, the ones already sent included: the number does
/// not drop when a wave leaves (run 32, item 1b: five launches).
constexpr bool Counted(bool alive, bool fighter, bool rightKind, bool mapFixed)
{
	return alive && fighter && rightKind && !mapFixed;
}

/// A wave takes a counted unit unless it is still held to a wave place (home role 2).
constexpr bool Pickable(bool counted, int homeRole)
{
	return counted && homeRole != 2;
}

} // namespace wc2ai

/// The AI id the map gave a player slot (the PUD "AIPL" byte), or -1.
int Wc2AiIdOf(int player);
/// True when the player is run by an original script (the map named one and the game folder's
/// definition file holds it): the same answer as Wc2AiSecond, without doing anything.
bool Wc2AiRuns(int player);
class CPlayer;
class CUnit;
class CUnitType;
/// wc2ai::PlaceStep of an engine unit type (0 for a type the original does not number).
int Wc2AiPlaceStepOf(const CUnitType &type);
/// The tile the original starts the search for a building place from: the player's hall-class
/// building nearest to the worker, without one the worker's own tile.
void Wc2AiPlaceReference(const CUnit &worker, int &x, int &y);
/// The player's workers (no tankers) and how many of them wc2ai::LumberWorkers wants on lumber.
int Wc2AiWorkerCount(const CPlayer &player);
int Wc2AiLumberWanted(const CPlayer &player);
/// The building wc2ai::RepairPick sends a worker to, or nullptr. `unreachable`: buildings no
/// worker could be sent to in this pass (they count as not on the workers' ground).
CUnit *Wc2AiRepairTarget(const CPlayer &player, const std::vector<const CUnit *> &unreachable);
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
/// The strength map of this stretch of 50 game cycles, built when first asked for in it. To be
/// asked only on behalf of a player an original script runs.
const wc2ai::CellMap &Wc2AiCells();
/// A building of a player an original script runs was destroyed (it is no longer among the
/// player's units): with fewer than 3 buildings left that are no oil platforms every unit of the
/// player goes out.
class CUnit;
void Wc2AiBuildingLost(const CUnit &building);

#endif // WC2AI_H
