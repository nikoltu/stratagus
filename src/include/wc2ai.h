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

/// What a worker takes next when it has delivered its goods (run 34, item 4: the original's worker
/// decides again after every delivery, so the split follows the stocks trip by trip). True: lumber.
/// `othersOnLumber` counts the player's other workers on lumber, the worker itself left out.
constexpr bool LumberNext(int othersOnLumber, int wanted, bool canGold, bool canLumber)
{
	return canLumber && (!canGold || othersOnLumber < wanted);
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
	int priority = 0;               ///< the type's annoy-computer factor; 0 is never repaired
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

/// The candidate nearest to a reference, from one distance per candidate (-1: not a candidate).
/// Of equals the later one when `laterOnTie` (the shipyard's oil patch, nearest to the worker),
/// else the first (the first hall's gold mine). -1 when there is no candidate.
int NearestOf(const std::vector<int> &distances, bool laterOnTie);

/// A foundry or a refinery (0x4E, 0x4F, 0x54, 0x55) is placed only while the slot has a finished
/// shipyard of either race; every other type does not ask.
constexpr bool ShipyardWorkAllowed(int buildingType, int finishedShipyards)
{
	return finishedShipyards > 0
	       || !(buildingType == 0x4E || buildingType == 0x4F || buildingType == 0x54 || buildingType == 0x55);
}

/// What a tanker's choice of its own oil platform sees of one platform.
struct PlatformView {
	bool usable = false;   ///< finished, the slot's own, oil left
	int toDepot = 0;       ///< distance from the tanker's depot
	int toTanker = 0;      ///< distance from the tanker
	bool building = false; ///< the slot's own, still under construction
};
/// The platform a tanker goes to: the lowest toDepot + toTanker / 2 of the usable ones, the first
/// of equals. -1 when none is usable.
int PlatformPick(const std::vector<PlatformView> &platforms);
/// With no usable platform: the unfinished one a tanker waits beside (live run 62: the second
/// tanker stood beside the platform under construction and entered it the moment it was done,
/// 2 of 2), by the same score, the first of equals. -1 when none is being built.
int PlatformWaitPick(const std::vector<PlatformView> &platforms);

/// A site another worker of the slot is on its way to build on (top-left tile and size).
struct Site {
	int x = 0, y = 0, w = 1, h = 1;
};
/// Does the square of size w x h at (x, y) overlap one of the planned sites?
bool SiteTaken(int x, int y, int w, int h, const std::vector<Site> &sites);

/// The port's haul way between a depot and a resource it is harvested from (a port rule, not the
/// original's): the tiles of the straight line from the depot's centre tile to the resource's
/// centre tile, each step rounded to the nearest tile, the tiles under either building left out.
std::vector<Place> HaulLineTiles(const Site &depot, const Site &resource);
/// Does a building of size w x h at (x, y) stand on the haul way: it covers a tile of the line, or
/// it lies beside a line tile whose tile across the line is not open (`open`, with `context`), so
/// that the way would be one tile wide there. A building that only touches the line - beside it or
/// at a corner - with open ground across is allowed (live run 43, item 2: the original's computer
/// put a Pig Farm right beside its mine-hall line).
bool HaulLineBlocked(int x, int y, int w, int h, const std::vector<Place> &line,
                     bool (*open)(int x, int y, void *context), void *context);

// ---- A further hall (0x4C4B10, DECODE_B8 item 1).

/// Gold per unit of a mine's stock word (+0x82). Not read: hundreds is the decode's likely unit
/// (NEEDS LIVE: a mine with 4,000 gold left).
constexpr int MineStockUnit = 100;
/// A gold mine is looked at for a further hall when its stock word is at least 50.
constexpr bool FurtherHallMineStock(int gold)
{
	return gold >= 50 * MineStockUnit;
}
/// Does a hall-class building (box x, y, w, h) have a tile in the 13 x 13 square from
/// (mineX - 5, mineY - 5)? Any player's hall counts.
constexpr bool FurtherHallWindowHit(int mineX, int mineY, int x, int y, int w, int h)
{
	return x <= mineX + 7 && x + w - 1 >= mineX - 5 && y <= mineY + 7 && y + h - 1 >= mineY - 5;
}
/// 0x4A1140: the greater axis distance from a tile to the nearest tile of a box.
int PointToBoxDistance(int px, int py, int bx, int by, int bw, int bh);
/// A further hall's place is kept when its top-left tile is at most 10 tiles from the mine.
constexpr bool FurtherHallKeeps(int distance)
{
	return distance <= 10;
}

// ---- Reserved tiles (tile flag 0x400, DECODE_B8 item 2): set by a scripted slot's build orders,
// haul roads and new buildings, read only by the scripted slots' placement walk.

/// What a new building of a scripted slot marks (table 0x8A1AD8): nothing (farm, scout tower,
/// platform, mine and every type not numbered), its footprint with a one-tile border (land
/// buildings and halls), or the coast tiles of the 13 x 13 square from (x - 6, y - 6) (shipyard,
/// foundry, refinery).
enum class ReserveKind { None, Border, Coast };
constexpr ReserveKind ReserveKindOf(int typeNumber)
{
	return (typeNumber == 0x48 || typeNumber == 0x49 || typeNumber == 0x4E || typeNumber == 0x4F
	        || typeNumber == 0x54 || typeNumber == 0x55) ? ReserveKind::Coast
	       : ((typeNumber >= 0x3C && typeNumber <= 0x3F) || (typeNumber >= 0x42 && typeNumber <= 0x47)
	          || (typeNumber >= 0x4A && typeNumber <= 0x4D) || (typeNumber >= 0x50 && typeNumber <= 0x53)
	          || (typeNumber >= 0x58 && typeNumber <= 0x5B)) ? ReserveKind::Border
	       : ReserveKind::None;
}
/// Mark (value 1) the square (x - grow, y - grow) of side size + 2 * grow, cut to the map; with
/// `footprint` false the inner size x size square is left as it is (only the border is marked).
void ReserveSquare(std::vector<uint8_t> &tiles, int mapWidth, int mapHeight, int x, int y, int size, int grow,
                   bool footprint);
/// Is any tile of the w x h box at (x, y) marked? Tiles off the map are not.
bool ReservedAny(const std::vector<uint8_t> &tiles, int mapWidth, int mapHeight, int x, int y, int w, int h);
/// The haul road of a worker sent to gold (0x4C35B0): the tiles a line stepper visits from the
/// depot's top-left tile toward the mine's top-left tile, each after its step, until a step
/// reaches the mine's column or row (that tile included). None when the two share a column or a row.
std::vector<Place> RoadTiles(int x0, int y0, int x1, int y1);
/// The marked tiles as text for a saved game: pairs "start,count" of runs of marked tiles in map
/// order, separated by commas; empty when none is marked.
std::string ReservedText(const std::vector<uint8_t> &tiles);
/// The marks back from ReservedText for a map of `size` tiles; runs off the map are cut.
std::vector<uint8_t> ReservedFromText(const std::string &text, size_t size);

// ---- A computer's unit that is hit (0x495600, DECODE_B7 item 3).

/// The kinds that run from a blow instead of fighting: the workers, Mage / Death Knight and their
/// heroes, Dwarves / Sappers (their original type numbers).
constexpr bool FleeKind(int typeNumber)
{
	return typeNumber == 0x02 || typeNumber == 0x03 || typeNumber == 0x0A || typeNumber == 0x0B
	       || typeNumber == 0x0E || typeNumber == 0x0F || typeNumber == 0x15 || typeNumber == 0x18;
}
/// One of the eight directions N, NE, E, SE, S, SW, W, NW (0..7) from an engine heading (0..255,
/// 0 = north, clockwise).
constexpr int Direction8(int heading)
{
	return ((heading + 16) >> 5) & 7;
}
/// Where a fleeing unit on (x, y) runs: four tiles along the direction its attacker faces, then
/// -2 + `randX` / `randY` (each taken as 0..7) on the two axes, kept on the map.
Place FleePoint(int x, int y, int direction8, int randX, int randY, int mapWidth, int mapHeight);

// ---- The target score of a unit looking for an enemy (0x495F40, DECODE_B8_ROUND2 item 1): the
// same formula for a person's and a computer's unit; the highest score wins, 0 is never chosen.

/// The score after the refusals: an enemy that cannot attack 0; one that can, farther than the
/// searcher's attack range 0x10000; within it 0x20000, or 0x30000 when it flies or dives; plus the
/// target type's Priority, minus d * d / 4 (d = tiles), at least 1.
constexpr int TargetScore(bool targetCanAttack, bool targetAirOrSub, bool inRange, int priority, int d)
{
	const int cls = !targetCanAttack ? 0 : !inRange ? 0x10000 : targetAirOrSub ? 0x30000 : 0x20000;
	const int score = cls + priority - d * d / 4;
	return score > 1 ? score : 1;
}
/// Targets nearer than this are refused (byte table 0x89C8EC by missile, minus 1): 2 for the
/// ballista and the catapult, 1 for the destroyers, the battleship, the juggernaut and the cannon
/// towers, else 0. By the original's type number.
constexpr int MinSearchDistance(int typeNumber)
{
	return (typeNumber == 0x04 || typeNumber == 0x05) ? 2
	       : (typeNumber == 0x1E || typeNumber == 0x1F || typeNumber == 0x20 || typeNumber == 0x21
	          || typeNumber == 0x62 || typeNumber == 0x63) ? 1
	       : 0;
}
/// The submarines (Gnomish Submarine, Giant Turtle), by the original's type number.
constexpr bool SubmarineKind(int typeNumber)
{
	return typeNumber == 0x26 || typeNumber == 0x27;
}

/// The casting pass of a computer's units falls on every fiftieth game cycle (local evidence
/// record PAI-001), the first on the first game cycle: 1, 51, 101, ... (its counter starts at 1).
constexpr int CastPassCycles = 50;
constexpr bool CastPassDue(unsigned long cycle)
{
	return cycle % CastPassCycles == 1;
}

/// A person's units never cast by themselves: the original's only automatic casting is the computer's
/// casting pass (0x4B5F9E tests the computer slot); a person's caster casts only on an order
/// (DECODE_B11_ROUND2 4, owner 2026-10-15: no autocast for the person, 1:1).
constexpr bool UnitMayAutoCast(bool personOwned)
{
	return !personOwned;
}

/// Nor do they repair by themselves: the original has no auto-repair for the person (no Ctrl + click
/// toggle on Repair); a person's Peasant or Peon repairs only on an order (owner 2026-10-15,
/// TOUCH_KEYBOARD_AUDIT). The computer repairs through its own pass (AiCheckRepair), not this flag.
constexpr bool UnitMayAutoRepair(bool personOwned)
{
	return !personOwned;
}

/// The cool-down counter 20 of order 0x0A: set when a remembered attack-move is sent again at the
/// end of a fight or a cast, it bars the next re-issue for this many game cycles. It is no polling
/// period: a blocked attack-move is not tried again on a timer (ORIG46 item 2).
constexpr int AttackMoveRetryWait = 20;

/// A person's ship on an attack-move to a point whose target has died stays in its fight, without
/// a target, for this many game cycles: no search, no re-issue. Then it takes the next target or,
/// with none in reach, is sent on to its point. Calibrated on the original's unit memory read live
/// (frames = game cycles, as the counter 20 above): from the kill to the re-issue run 49 H1 112,
/// H2 111; to the next target taken run 49 H2 109, H3 112, ORIG47 E4 110 / 113 - 6 cases.
/// A land unit's attack-move went on 15 frames after its kill (ORIG47 A4, 1 case), so the wait
/// is a ship's only.
constexpr int ShipKillHold = 111;
constexpr bool HoldsAfterKill(bool person, bool naval, bool attackMoveToPoint)
{
	return person && naval && attackMoveToPoint;
}
/// An attack-move is kept (blocked, or at a re-issue check) while the unit's tile is 2 or more tiles
/// from the point on either axis; within 1 on both it is done.
constexpr bool AttackMoveRetry(int dx, int dy)
{
	return dx >= 2 || dx <= -2 || dy >= 2 || dy <= -2;
}

/// The answer of a remembered order at the re-issue check (0x4D87E0 with order 2): send it again now,
/// stand and keep it (its counter runs), or stand and forget it.
enum class Reissue : uint8_t { Ready = 0, Wait = 1, Forget = 2 };

/// A remembered attack-move (memory 0x0A) at the end of a fight or a cast (0x4D8890-0x4D891B): while its
/// counter runs the unit stands and keeps it; with the counter at 0 it is sent again when the unit's tile
/// is 2 or more tiles from the point on either axis (counter 20 again), else it is forgotten.
/// reissueAt is the game cycle at which the counter is 0 (0 = at 0).
constexpr Reissue AttackMoveReissue(unsigned long cycle, unsigned long reissueAt, int dx, int dy)
{
	if (cycle < reissueAt) {
		return Reissue::Wait;
	}
	return AttackMoveRetry(dx, dy) ? Reissue::Ready : Reissue::Forget;
}

/// A patrol leg is remembered (order 5 kept for the re-issue after a fight) for every land unit; a flyer
/// or a ship only when the leg is off both axes by more than one tile (patrol start 0x4C1550, type flags
/// 0x0A).
constexpr bool PatrolRememberLeg(bool airOrSea, int dx, int dy)
{
	return !airOrSea || ((dx > 1 || dx < -1) && (dy > 1 || dy < -1));
}

/// A remembered patrol is sent again only when its counter is 0; the re-issue sets it to 40 game cycles
/// (0x4D8849-0x4D8883), so the next one is due 40 cycles later.
constexpr int PatrolReissueWait = 40;
constexpr bool PatrolReissueReady(unsigned long cycle, unsigned long reissueAt)
{
	return cycle >= reissueAt;
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
/// The player's workers on lumber as the split counts them (a lone gather order on wood, or idle
/// with wood in hand), `except` left out.
int Wc2AiOnLumber(const CPlayer &player, const CUnit *except);
/// The building wc2ai::RepairPick sends a worker to, or nullptr. `unreachable`: buildings no
/// worker could be sent to in this pass (they count as not on the workers' ground).
CUnit *Wc2AiRepairTarget(const CPlayer &player, const std::vector<const CUnit *> &unreachable);
/// Forget the ids (a new map is about to be loaded).
void Wc2AiClearIds();
/// Forget the id of one slot: a computer player of a mod runs that seat (wc2bot.h), so the map's
/// original script must not run beside it.
void Wc2AiDropId(int player);
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
/// Reserved tiles of the scripted slots (DECODE_B8 item 2). A worker of a scripted slot is sent
/// to build at (x, y): the one-tile border round the site is marked for good (a farm: nothing; the
/// bare site is a planned site while the order lives).
void Wc2AiReservePlanned(const CUnitType &type, int x, int y);
/// A worker of a scripted slot is sent to gold: its haul road is marked.
void Wc2AiReserveRoad(int depotX, int depotY, int mineX, int mineY);
/// A building of a scripted slot came into being: its marks (wc2ai::ReserveKindOf). `mapPlaced`:
/// placed with the map (its footprint marked too); else a foundation laid by a worker, whose
/// footprint the end of the worker's task leaves unmarked.
void Wc2AiReserveBuilding(const CUnit &building, bool mapPlaced);
/// Is a tile of the w x h box at (x, y) reserved?
bool Wc2AiReservedAt(int x, int y, int w, int h);

#endif // WC2AI_H
