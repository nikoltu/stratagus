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
/**@name test_saveload.cpp - the pure parts of saving and loading a game. */
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.
//

#include <doctest.h>

#include "stratagus.h"

#include "game.h"
#include "iolib.h"
#include "replay.h"
#include "tile.h"
#include "unit.h"
#include "wc2mission.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{

fs::path Scratch(const char *name)
{
	std::error_code ec;
	const fs::path dir = fs::temp_directory_path(ec) / "stratagus_saveload_test" / name;
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
	return dir;
}

void Put(const fs::path &file, const std::string &text)
{
	std::ofstream out(file, std::ios::binary);
	out << text;
}

std::string Get(const fs::path &file)
{
	std::ifstream in(file, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("saveload: a save is written under a name no list shows")
{
	const fs::path target = fs::path("save") / "game.sav";
	CHECK(SaveGameTemporary(target).filename().string() == "game.sav.tmp");
	// With the file layer's ".gz" it ends in neither ".sav" nor ".sav.gz".
	const std::string packed = SaveGameTemporary(target).filename().string() + ".gz";
	CHECK(packed == "game.sav.tmp.gz");
	CHECK(packed.rfind(".sav.gz") != packed.size() - 7);
}

TEST_CASE("saveload: a complete save replaces the one that was there")
{
	const fs::path dir = Scratch("complete");
	const fs::path target = dir / "game.sav";
	Put(dir / "game.sav.gz", "old");
	Put(dir / "game.sav.tmp.gz", "new");
	CHECK(CommitSaveGame(target, true));
	CHECK(Get(dir / "game.sav.gz") == "new");
	CHECK_FALSE(fs::exists(dir / "game.sav.tmp.gz"));
	std::error_code ec;
	fs::remove_all(dir, ec);
}

TEST_CASE("saveload: a save that was not written whole leaves the one that was there")
{
	const fs::path dir = Scratch("cut");
	const fs::path target = dir / "game.sav";
	Put(dir / "game.sav.gz", "old");
	Put(dir / "game.sav.tmp.gz", "ha");
	CHECK_FALSE(CommitSaveGame(target, false));
	CHECK(Get(dir / "game.sav.gz") == "old");
	CHECK_FALSE(fs::exists(dir / "game.sav.tmp.gz"));
	std::error_code ec;
	fs::remove_all(dir, ec);
}

TEST_CASE("saveload: a save the file layer could not compress is moved under the plain name")
{
	const fs::path dir = Scratch("plain");
	const fs::path target = dir / "game.sav";
	Put(dir / "game.sav.tmp", "plain");
	CHECK(CommitSaveGame(target, true));
	CHECK(Get(dir / "game.sav") == "plain");
	CHECK_FALSE(fs::exists(dir / "game.sav.tmp"));
	// Nothing written at all: nothing to move.
	CHECK_FALSE(CommitSaveGame(target, true));
	std::error_code ec;
	fs::remove_all(dir, ec);
}

TEST_CASE("saveload: a file of a later format is refused, an older one and one without a number are read")
{
	CHECK(SavedGameFormat == 3);
	CHECK(SavedGameFormatReadable(0));
	CHECK(SavedGameFormatReadable(1));
	CHECK(SavedGameFormatReadable(2));
	CHECK(SavedGameFormatReadable(3));
	CHECK_FALSE(SavedGameFormatReadable(4));
	CHECK_FALSE(SavedGameFormatReadable(99));
}

TEST_CASE("saveload: the unit's small counters go into the file only when set")
{
	CHECK(UnitSmallFieldsText(0, 0, false, -1).empty());
	CHECK(UnitSmallFieldsText(7, 3, true, 2)
	      == R"( "under-attack", 7, "just-moved", 3, "z-displaced", "colors", 2,)");
	CHECK(UnitSmallFieldsText(0, 0, true, -1) == R"( "z-displaced",)");
}

TEST_CASE("saveload: an autosave is due at every game's start and every 60 s of wall time after it")
{
	unsigned long next = 0;
	const unsigned long start = 5000; // the wall clock does not begin with the game

	// 0 s: the start writes.
	CHECK(AutosaveDue(start, next, 5, false, false, true));
	CHECK(next == start + 60000);
	CHECK_FALSE(AutosaveDue(start + 1, next, 5, false, false, false));
	CHECK_FALSE(AutosaveDue(start + 59900, next, 5, false, false, false));
	CHECK(AutosaveDue(start + 60000, next, 5, false, false, false));
	CHECK_FALSE(AutosaveDue(start + 60001, next, 5, false, false, false));
	// A late frame does not move the grid of the start.
	CHECK(AutosaveDue(start + 120040, next, 5, false, false, false));
	CHECK(next == start + 180000);
	CHECK_FALSE(AutosaveDue(start + 179999, next, 5, false, false, false));

	// A game comes up from a save at 130 s: it writes and the 60 s begin there.
	CHECK(AutosaveDue(start + 130000, next, 5, false, false, true));
	CHECK(next == start + 190000);
	CHECK_FALSE(AutosaveDue(start + 180000, next, 5, false, false, false));
	CHECK_FALSE(AutosaveDue(start + 189900, next, 5, false, false, false));
	CHECK(AutosaveDue(start + 190000, next, 5, false, false, false));

	// A stall of several periods: one write, the next a period later.
	CHECK(AutosaveDue(start + 500000, next, 5, false, false, false));
	CHECK(next == start + 560000);
	CHECK_FALSE(AutosaveDue(start + 500016, next, 5, false, false, false));

	// Switched off, a network game, a replay: never, and the clock is left alone.
	unsigned long untouched = 77;
	CHECK_FALSE(AutosaveDue(1000, untouched, 0, false, false, true));
	CHECK_FALSE(AutosaveDue(900000, untouched, 0, false, false, false));
	CHECK_FALSE(AutosaveDue(1000, untouched, 5, true, false, true));
	CHECK_FALSE(AutosaveDue(900000, untouched, 5, true, false, false));
	CHECK_FALSE(AutosaveDue(1000, untouched, 5, false, true, true));
	CHECK_FALSE(AutosaveDue(900000, untouched, 5, false, true, false));
	CHECK(untouched == 77);
}

TEST_CASE("saveload: the mission rule's counts go into the file as one guarded line")
{
	// Nothing counted, gate shut: the file carries no line.
	CHECK(MissionRuleStateLine({}, false).empty());
	CHECK(MissionRuleStateLine({12, 7, 301}, false)
	      == "if (MissionRuleState ~= nil) then MissionRuleState({gate = false, arrived = {12, 7, 301}}) end\n");
	CHECK(MissionRuleStateLine({}, true)
	      == "if (MissionRuleState ~= nil) then MissionRuleState({gate = true, arrived = {}}) end\n");
}

TEST_CASE("replay: an input line is a cheat word; only a debug build runs a console line")
{
	// What the recording game writes: a recognised cheat word (any build), or - in a debug build
	// only - a console line with a leading '-'.
	CHECK_FALSE(ReplayInputRunsLua("glittering prizes", false));
	CHECK_FALSE(ReplayInputRunsLua("it is a good day to die", false));
	CHECK_FALSE(ReplayInputRunsLua("glittering prizes", true));
	// A '-' line in a replay file never runs in a release build, whatever it says.
	CHECK_FALSE(ReplayInputRunsLua("-os.exit(0)", false));
	CHECK_FALSE(ReplayInputRunsLua("-", false));
	CHECK_FALSE(ReplayInputRunsLua("--", false));
	CHECK_FALSE(ReplayInputRunsLua("- SetPlayerData(0, 'Resources', 'gold', 1)", false));
	CHECK(ReplayInputRunsLua("-print(1)", true));
	// Only the first character counts; nothing is read from an empty line.
	CHECK_FALSE(ReplayInputRunsLua(" -print(1)", true));
	CHECK_FALSE(ReplayInputRunsLua("fow -off", true));
	CHECK_FALSE(ReplayInputRunsLua("", true));
	CHECK_FALSE(ReplayInputRunsLua("", false));
}

// FOW-SAVE (work/FOW_BUG.md): a unit saved inside a building gets back exactly the sight that
// leaving the building takes away, whatever the order of the two records in the file.
#include "map.h"
#include "player.h"
#include "settings.h"
#include "unit_manager.h"
#include "unittype.h"
#include "viewport.h"

#include <algorithm>
#include <iterator>
#include <vector>

namespace
{
class InsideSightWorld
{
public:
	InsideSightWorld()
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		savedFoV = GameSettings.FoV;
		GameSettings.FoV = FieldOfViewTypes::cSimpleRadial;
		if (UnitManager == nullptr) {
			UnitManager = new CUnitManager;
		}
		UnitManager->Init();
		UnitTypeVar.Init();
		Map.Info.MapWidth = 40;
		Map.Info.MapHeight = 40;
		Map.Create();
		for (int i = 0; i < PlayerMax; ++i) {
			Players[i].Clear();
			Players[i].Index = i;
			Players[i].Type = PlayerTypes::PlayerNobody;
		}
		Players[0].Type = PlayerTypes::PlayerPerson;
		Players[0].UnitLimit = Players[0].BuildingLimit = Players[0].TotalUnitLimit = 100;
		NumPlayers = 1;
		ThisPlayer = &Players[0];
		for (int y = 0; y < 40; ++y) {
			for (int x = 0; x < 40; ++x) {
				Map.Field(x, y)->Flags = MapFieldLandAllowed;
			}
		}
		MakeType(hall, "unit-test-hall", 0, 4, MapFieldBuilding);
		hall.Building = true;
		MakeType(peasant, "unit-test-peasant", 1, 1, MapFieldLandUnit);
		// FOW-BUG2: the hall upgraded in place (same size and flags), with a longer sight.
		MakeType(keep, "unit-test-keep", 2, 4, MapFieldBuilding);
		keep.Building = true;
		for (int i = 0; i < PlayerMax; ++i) {
			keep.Stats[i].Variables[SIGHTRANGE_INDEX].Max = 6;
			keep.Stats[i].Variables[SIGHTRANGE_INDEX].Value = 6;
		}
	}

	~InsideSightWorld()
	{
		for (int i = 0; i < PlayerMax; ++i) {
			Players[i].Clear();
		}
		UnitManager->Init();
		Map.Fields.clear();
		Map.Info.MapWidth = 0;
		Map.Info.MapHeight = 0;
		NumPlayers = savedNumPlayers;
		ThisPlayer = savedThisPlayer;
		GameSettings.FoV = savedFoV;
	}

	static std::vector<unsigned short> Seen()
	{
		std::vector<unsigned short> v;
		for (int i = 0; i < 40 * 40; ++i) {
			v.push_back(Map.Field(static_cast<unsigned int>(i))->playerInfo.Visible[0]);
		}
		return v;
	}

	CUnitType hall;
	CUnitType peasant;
	CUnitType keep;

private:
	void MakeType(CUnitType &type, const char *ident, int slot, int size, tile_flags fieldFlags)
	{
		type.Ident = ident;
		type.Name = ident;
		type.Slot = slot;
		type.TileWidth = size;
		type.TileHeight = size;
		type.GridStep = 1;
		type.NumDirections = 8;
		type.MoveType = EMovement::Land;
		type.MovementMask = MapFieldLandUnit | MapFieldBuilding | MapFieldUnpassable;
		type.FieldFlags = fieldFlags;
		type.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		type.DefaultStat.Variables.resize(UnitTypeVar.GetNumberVariable());
		type.DefaultStat.Variables[HP_INDEX].Max = 60;
		type.DefaultStat.Variables[HP_INDEX].Value = 60;
		type.DefaultStat.Variables[HP_INDEX].Enable = 1;
		type.DefaultStat.Variables[SIGHTRANGE_INDEX].Max = 4;
		type.DefaultStat.Variables[SIGHTRANGE_INDEX].Value = 4;
		type.MapDefaultStat = type.DefaultStat;
		for (int i = 0; i < PlayerMax; ++i) {
			type.Stats[i] = type.DefaultStat;
		}
	}

	CPlayer *savedThisPlayer = nullptr;
	int savedNumPlayers = 0;
	FieldOfViewTypes savedFoV = FieldOfViewTypes::cShadowCasting;
};
} // namespace

TEST_CASE("saveload: a unit read inside a building lights what leaving the building takes away")
{
	InsideSightWorld w;
	CUnit *hall = MakeUnitAndPlace({10, 10}, w.hall, &Players[0]);
	CUnit *peasant = MakeUnitAndPlace({30, 30}, w.peasant, &Players[0]);
	REQUIRE(hall != nullptr);
	REQUIRE(peasant != nullptr);
	peasant->Remove(hall); // inside: its sight from the hall's middle, with the hall's range
	const std::vector<unsigned short> withPeasant = InsideSightWorld::Seen();
	MapUnmarkUnitSight(*peasant); // the file keeps no sight counts: only the hall's share
	const std::vector<unsigned short> hallOnly = InsideSightWorld::Seen();
	REQUIRE(hallOnly != withPeasant);

	// The read now: marked once every record is read, with the container known; leaving the
	// hall takes the same share away again.
	MapMarkInsideUnitSightAfterLoad(*peasant);
	CHECK(InsideSightWorld::Seen() == withPeasant);
	MapUnmarkUnitSight(*peasant);
	CHECK(InsideSightWorld::Seen() == hallOnly);

	// The old read: "host-info" marked while unit.Container was still unset (the hall's record
	// later in the file) - a one-tile shape round the hall's corner 10,10, not the shape round its
	// middle 12,12 that leaving takes away.
	CUnit *const host = peasant->Container;
	peasant->Container = nullptr;
	MapSight(Players[0], *peasant, hall->tilePos, 4, 4, host->CurrentSightRange, MapMarkTileSight);
	peasant->Container = host;
	MapUnmarkUnitSight(*peasant);
	// The owner's report: a tile the hall itself sees falls to explored (fog in the hall's own
	// sight), and a tile nothing sees stays lit (a square without fog).
	CHECK(hallOnly[12 * 40 + 16] == 2);
	CHECK(Map.Field(16, 12)->playerInfo.Visible[0] == 1);
	CHECK(hallOnly[10 * 40 + 6] == 0);
	CHECK(Map.Field(6, 10)->playerInfo.Visible[0] == 2);
}

// LOAD-CRASH-3473 (work/LOAD_CRASH_3473.md): the 3.4.73 repro in small - five Peasants inside a
// Town Hall, the game saved and read back. The file keeps no sight counts and no unit cache, so
// the read state is: explored tiles at 1, every count 0, the hall read as standing, the Peasants
// read as removed inside it. PlaceLoadedUnits (LoadGame) must give back exactly the sight of the
// running game, and the hall must be visible to its owner on the first frame.
TEST_CASE("saveload: five units read inside a building - the sight and the hall's visibility come back as they were")
{
	InsideSightWorld w;
	w.hall.BoolFlag[VISIBLEUNDERFOG_INDEX].value = true;
	CUnit *hall = MakeUnitAndPlace({10, 10}, w.hall, &Players[0]);
	REQUIRE(hall != nullptr);
	std::vector<CUnit *> peasants;
	for (int i = 0; i < 5; ++i) {
		CUnit *peasant = MakeUnitAndPlace({30, 30}, w.peasant, &Players[0]);
		REQUIRE(peasant != nullptr);
		peasant->Remove(hall);
		peasants.push_back(peasant);
	}
	const std::vector<unsigned short> inGame = InsideSightWorld::Seen();
	for (CUnit *peasant : peasants) {
		MapUnmarkUnitSight(*peasant);
	}
	const std::vector<unsigned short> hallOnly = InsideSightWorld::Seen();
	for (CUnit *peasant : peasants) {
		MapMarkUnitSight(*peasant);
	}
	REQUIRE(InsideSightWorld::Seen() == inGame);
	REQUIRE(hall->IsVisible(Players[0]));

	// What the file gives back.
	hall->Remove(nullptr); // its sight and its passengers' go; the tiles stay explored
	for (int i = 0; i < 40 * 40; ++i) {
		CHECK(Map.Field(static_cast<unsigned int>(i))->playerInfo.Visible[0] == (inGame[i] ? 1 : 0));
	}
	for (CUnit *unit : UnitManager->GetUnits()) {
		std::fill(std::begin(unit->VisCount), std::end(unit->VisCount), 0);
	}
	hall->Removed = 0; // read as standing; PlaceLoadedUnits puts it on the map
	for (CUnit *peasant : peasants) {
		REQUIRE(static_cast<bool>(peasant->Removed));
		REQUIRE(peasant->Container == hall);
	}

	PlaceLoadedUnits();

	CHECK(InsideSightWorld::Seen() == inGame);
	CHECK(hall->IsVisible(Players[0]));
	CHECK(hall->VisCount[0] > 0);
	// Leaving the hall takes each Peasant's share away; the hall's own sight stays as it was.
	for (CUnit *peasant : peasants) {
		MapUnmarkUnitSight(*peasant);
	}
	CHECK(InsideSightWorld::Seen() == hallOnly);
}

// LOAD-CRASH-3473: a building the player knows, that is under fog now and whose remembered look was
// never filled (it never went under fog for the player before the save). 3.4.73 drew it from
// Seen.Type == nullptr and crashed (SIGSEGV at 0x958 = CUnitType::Construction).
TEST_CASE("saveload: a building known under fog without a remembered look is not drawn from it; the load fills the look")
{
	InsideSightWorld w;
	w.hall.BoolFlag[VISIBLEUNDERFOG_INDEX].value = true;
	w.hall.Width = w.hall.Height = 4 * PixelTileSize.x;
	Players[1].Type = PlayerTypes::PlayerComputer;
	Players[1].UnitLimit = Players[1].BuildingLimit = Players[1].TotalUnitLimit = 100;
	CUnit *enemy = MakeUnitAndPlace({30, 10}, w.hall, &Players[1]);
	REQUIRE(enemy != nullptr);
	REQUIRE_FALSE(enemy->IsVisible(Players[0]));
	enemy->Seen.ByPlayer |= 1 << 0; // seen by player 0 ...
	REQUIRE(enemy->Seen.Type == nullptr); // ... but never under fog for it

	CViewport vp;
	vp.MapPos = Vec2i(0, 0);
	vp.MapWidth = 40;
	vp.MapHeight = 40;
	vp.TopLeftPos = PixelPos(0, 0);
	vp.BottomRightPos = PixelPos(40 * PixelTileSize.x - 1, 40 * PixelTileSize.y - 1);
	CHECK_FALSE(enemy->IsVisibleInViewport(vp));
	enemy->Draw(vp); // the last guard: returns before any read through Seen.Type

	UnitFillMissingSeenAfterLoad(*enemy);
	CHECK(enemy->Seen.Type == &w.hall);
	CHECK(enemy->Seen.tilePos == Vec2i(30, 10));
	CHECK(enemy->IsVisibleInViewport(vp));

	// A unit the player sees, or one it never knew, keeps no remembered look.
	CUnit *own = MakeUnitAndPlace({10, 10}, w.hall, &Players[0]);
	REQUIRE(own != nullptr);
	UnitFillMissingSeenAfterLoad(*own);
	CHECK(own->Seen.Type == nullptr);
	CUnit *unknown = MakeUnitAndPlace({30, 30}, w.hall, &Players[1]);
	REQUIRE(unknown != nullptr);
	unknown->Seen.ByPlayer = 0;
	UnitFillMissingSeenAfterLoad(*unknown);
	CHECK(unknown->Seen.Type == nullptr);
}

// FOW-BUG2 (work/FOW_BUG2.md): fog over own units and buildings in play. TransformUnitIntoType
// kept a unit upgraded in place (Town Hall -> Keep: same size and flags) on the map without giving
// back its old sight: the Keep lit the Hall's tiles while CurrentSightRange said the Keep's range.
#include "actions.h"

namespace
{
unsigned short VisibleOf(int player, int x, int y)
{
	return Map.Field(x, y)->playerInfo.Visible[player];
}

bool NoCountAboveExplored(int player)
{
	for (int i = 0; i < 40 * 40; ++i) {
		if (Map.Field(static_cast<unsigned int>(i))->playerInfo.Visible[player] > 1) {
			return false;
		}
	}
	return true;
}

void SetUpOwner()
{
	// Not ThisPlayer: the transform's button refresh is for the local player only.
	Players[1].Type = PlayerTypes::PlayerPerson;
	Players[1].UnitLimit = Players[1].BuildingLimit = Players[1].TotalUnitLimit = 100;
}
} // namespace

TEST_CASE("sight: a hall upgraded in place lights with the new range and gives back exactly that")
{
	InsideSightWorld w;
	SetUpOwner();
	// Hall 10,10 (4x4, middle 12,12): range 4 lights row 12 x 8..16, range 6 x 6..18.
	CUnit *hall = MakeUnitAndPlace({10, 10}, w.hall, &Players[1]);
	CUnit *sentinel = MakeUnitAndPlace({20, 12}, w.peasant, &Players[1]); // row 12 x 16..24
	REQUIRE(hall != nullptr);
	REQUIRE(sentinel != nullptr);
	REQUIRE(VisibleOf(1, 18, 12) == 2); // the sentinel only

	COrder::NewActionTransformInto(w.keep)->Execute(*hall);
	REQUIRE(hall->Type == &w.keep);
	CHECK(hall->CurrentSightRange == 6);
	CHECK(VisibleOf(1, 18, 12) == 3); // the Keep's range lights it now

	// The Keep goes (destroyed): the sentinel's own tile stays lit.
	hall->Remove(nullptr);
	CHECK(VisibleOf(1, 18, 12) == 2);
	CHECK(VisibleOf(1, 17, 12) == 2);
	MapUnmarkUnitSight(*sentinel);
	CHECK(NoCountAboveExplored(1));
}

TEST_CASE("sight: a peasant inside the hall when it becomes a keep takes away only what it gave")
{
	InsideSightWorld w;
	SetUpOwner();
	CUnit *hall = MakeUnitAndPlace({10, 10}, w.hall, &Players[1]);
	CUnit *sentinel = MakeUnitAndPlace({20, 12}, w.peasant, &Players[1]);
	CUnit *peasant = MakeUnitAndPlace({30, 30}, w.peasant, &Players[1]);
	REQUIRE(hall != nullptr);
	REQUIRE(sentinel != nullptr);
	REQUIRE(peasant != nullptr);
	peasant->Remove(hall); // delivering when the upgrade completes

	COrder::NewActionTransformInto(w.keep)->Execute(*hall);
	REQUIRE(hall->Type == &w.keep);
	CHECK(VisibleOf(1, 18, 12) == 4); // Keep + Peasant inside (range 6) + sentinel

	// The owner's report: the Peasant comes out and a tile beside his own unit fell to fog.
	peasant->Place({30, 30});
	CHECK(VisibleOf(1, 18, 12) == 3);
	CHECK(VisibleOf(1, 17, 12) == 3);

	hall->Remove(nullptr);
	peasant->Remove(nullptr);
	CHECK(VisibleOf(1, 18, 12) == 2);
	MapUnmarkUnitSight(*sentinel);
	CHECK(NoCountAboveExplored(1));
}

TEST_CASE("sight: refreshing every unit's sight leaves the counts of units inside a building as they were")
{
	InsideSightWorld w;
	CUnit *hall = MakeUnitAndPlace({10, 10}, w.hall, &Players[0]);
	CUnit *peasant = MakeUnitAndPlace({30, 30}, w.peasant, &Players[0]);
	REQUIRE(hall != nullptr);
	REQUIRE(peasant != nullptr);
	peasant->Remove(hall);
	const std::vector<unsigned short> before = InsideSightWorld::Seen();

	MapRefreshUnitsSight(true);
	MapRefreshUnitsSight();
	CHECK(InsideSightWorld::Seen() == before);

	MapRefreshUnitsSight(Vec2i(12, 12), true);
	MapRefreshUnitsSight(Vec2i(12, 12));
	CHECK(InsideSightWorld::Seen() == before);

	// And the Peasant still takes away exactly its share when it leaves.
	peasant->Place({30, 30});
	peasant->Remove(nullptr);
	hall->Remove(nullptr);
	CHECK(NoCountAboveExplored(0));
}

namespace
{

/// Read a save file back through the file layer (it unpacks ".gz" itself).
std::string Unpacked(const fs::path &file)
{
	fs::path name = file;
	if (!fs::exists(name)) {
		name += ".gz";
	}
	CFile in;
	if (in.open(name.string().c_str(), CL_OPEN_READ) == -1) {
		return "<unreadable>";
	}
	std::string text;
	char buf[8192];
	int n;
	while ((n = in.read(buf, sizeof(buf))) > 0) {
		text.append(buf, n);
	}
	in.close();
	return text;
}

/// A save-sized text (about 1 MB, map-field lines as a real save has them).
std::string SaveSizedText(int seed)
{
	std::string text = "-- save " + std::to_string(seed) + "\n";
	for (int i = 0; i < 16384; ++i) {
		text += "  {" + std::to_string((i * 7 + seed) % 400) + ", 326,  0,  8, \"explored\", 0, \"water\"},";
		text += (i & 1) ? "\n" : " ";
	}
	return text;
}

bool NoTemporaryLeft(const fs::path &dir)
{
	for (const auto &entry : fs::directory_iterator(dir)) {
		if (entry.path().filename().string().find(".tmp") != std::string::npos) {
			return false;
		}
	}
	return true;
}

} // namespace

TEST_CASE("autosave: the save text is built in memory, byte for byte what a file would get")
{
	CFile mem;
	REQUIRE(mem.open("", CL_WRITE_MEMORY | CL_OPEN_WRITE) == 0);
	mem.printf("GameCycle = %lu\n", 46529ul);
	mem.write("SetGodMode(false)\n");
	CHECK(mem.TakeText() == "GameCycle = 46529\nSetGodMode(false)\n");
	CHECK(mem.TakeText().empty());
	CHECK(mem.close() == 0);
}

TEST_CASE("autosave: a map field goes into the file as one piece, as it always read")
{
	CMapField mf;
	mf.setGraphicTile(12);
	mf.playerInfo.SeenTile = 11;
	mf.Value = 7;
	mf.playerInfo.Visible[0] = 1;
	mf.playerInfo.Visible[3] = 1;
	mf.playerInfo.Visible[4] = 2; // seen now, not "explored"
	mf.Flags = MapFieldLandAllowed | MapFieldUnpassable | MapFieldWall | MapFieldBuilding;
	CFile mem;
	REQUIRE(mem.open("", CL_WRITE_MEMORY | CL_OPEN_WRITE) == 0);
	mf.Save(mem);
	CHECK(mem.TakeText()
	      == R"(  { 12,  11,  7,  0, "explored", 0, "explored", 3, "land", "block", "wall", "building"})");
	CMapField empty;
	empty.Save(mem);
	CHECK(mem.TakeText() == "  {  0,   0,  0,  0}");
	mem.close();
}

TEST_CASE("autosave: a save written on the writer thread is the same file as one written in the frame")
{
	const fs::path dir = Scratch("async_same");
	const std::string text = SaveSizedText(1);
	REQUIRE(WriteSaveText(dir / "sync.sav", text));
	REQUIRE(WriteSaveTextInBackground(dir / "async.sav", text));
	WaitForSaveGame();
	CHECK_FALSE(SaveGameWriting());
	CHECK(fs::exists(dir / "async.sav.gz"));
	CHECK(Unpacked(dir / "async.sav") == text);
	CHECK(Unpacked(dir / "sync.sav") == text);
	// The same compressor on the same text: the two files are equal byte for byte, so a load
	// reads the same from both.
	CHECK(Get(dir / "async.sav.gz") == Get(dir / "sync.sav.gz"));
	CHECK(NoTemporaryLeft(dir));
	std::error_code ec;
	fs::remove_all(dir, ec);
}

TEST_CASE("autosave: two saves in a row do not interleave; one write in flight at a time")
{
	const fs::path dir = Scratch("async_twice");
	const fs::path target = dir / "autosave.sav";
	const std::string first = SaveSizedText(2);
	const std::string second = SaveSizedText(3);
	REQUIRE(WriteSaveTextInBackground(target, first));
	// While the first still writes a second is refused, not started beside it.
	const bool secondStarted = WriteSaveTextInBackground(target, second);
	WaitForSaveGame();
	// Accepted only when the first had already ended: then the second replaced it whole.
	CHECK(Unpacked(target) == (secondStarted ? second : first));
	// A save in the frame after a background one waits for it: the later one is what stays.
	REQUIRE(WriteSaveTextInBackground(target, first));
	WaitForSaveGame();
	REQUIRE(WriteSaveText(target, second));
	CHECK(Unpacked(target) == second);
	// Back to back on the writer thread, each waited for: always the last one, whole.
	for (int i = 0; i < 4; ++i) {
		const std::string text = SaveSizedText(10 + i);
		REQUIRE(WriteSaveTextInBackground(target, text));
		WaitForSaveGame();
		CHECK(Unpacked(target) == text);
	}
	CHECK(NoTemporaryLeft(dir));
	std::error_code ec;
	fs::remove_all(dir, ec);
}

TEST_CASE("autosave: a background save goes through a temporary file and leaves no part behind")
{
	const fs::path dir = Scratch("async_partial");
	const fs::path target = dir / "autosave.sav";
	REQUIRE(WriteSaveText(target, "old"));
	CHECK(Unpacked(target) == "old");
	// What a write cut short by the process ending leaves: a part under the temporary name.
	Put(dir / "autosave.sav.tmp.gz", "\x1f\x8b half");
	const std::string text = SaveSizedText(4);
	REQUIRE(WriteSaveTextInBackground(target, text));
	WaitForSaveGame();
	CHECK(Unpacked(target) == text);
	CHECK(NoTemporaryLeft(dir));

	// A write that cannot be made (its directory has gone) leaves nothing and no running thread;
	// the save that was there stays.
	const fs::path gone = dir / "gone" / "autosave.sav";
	REQUIRE(WriteSaveTextInBackground(gone, text));
	WaitForSaveGame();
	CHECK_FALSE(SaveGameWriting());
	CHECK_FALSE(fs::exists(gone.parent_path()));
	CHECK(Unpacked(target) == text);
	CHECK(NoTemporaryLeft(dir));
	std::error_code ec;
	fs::remove_all(dir, ec);
}

// Timing harness, only when asked: STRATAGUS_SAVE_TIMING=<a save's text, unpacked>.
TEST_CASE("autosave: timing of the save's parts (STRATAGUS_SAVE_TIMING)")
{
	const char *path = std::getenv("STRATAGUS_SAVE_TIMING");
	if (!path || !*path) {
		return;
	}
	const std::string real = Get(path);
	REQUIRE(!real.empty());
	using Clock = std::chrono::steady_clock;
	const auto ms = [](Clock::time_point a, Clock::time_point b) {
		return std::chrono::duration<double, std::milli>(b - a).count();
	};
	const fs::path dir = Scratch("timing");

	// The map fields of a 128x128 map, as the real save has them.
	CMapField mf;
	mf.setGraphicTile(326);
	mf.playerInfo.SeenTile = 326;
	mf.playerInfo.Visible[0] = 1;
	mf.playerInfo.Visible[1] = 1;
	mf.Flags = MapFieldWaterAllowed;
	const int fields = 128 * 128;

	// Before: six formatted writes per field straight into the gzip stream.
	auto t0 = Clock::now();
	{
		CFile gz;
		gz.open((dir / "old.sav").string().c_str(), CL_WRITE_GZ | CL_OPEN_WRITE);
		for (int i = 0; i < fields; ++i) {
			gz.printf("  {%3d, %3d, %2d, %2d", 326, 326, 0, 8);
			gz.printf(", \"explored\", %d", 0);
			gz.printf(", \"explored\", %d", 1);
			gz.printf(", \"water\"");
			gz.printf("}");
			gz.printf((i & 1) ? ",\n" : ", ");
		}
		gz.close();
	}
	auto t1 = Clock::now();
	// After: one write per field into memory.
	std::string mapText;
	{
		CFile mem;
		mem.open("", CL_WRITE_MEMORY | CL_OPEN_WRITE);
		for (int i = 0; i < fields; ++i) {
			mf.Save(mem);
			mem.write((i & 1) ? std::string_view(",\n") : std::string_view(", "));
		}
		mapText = mem.TakeText();
	}
	auto t2 = Clock::now();
	// The real save's text line by line through formatted writes: old (into gzip) and new (memory).
	{
		CFile gz;
		gz.open((dir / "lines.sav").string().c_str(), CL_WRITE_GZ | CL_OPEN_WRITE);
		size_t at = 0;
		while (at < real.size()) {
			size_t end = real.find('\n', at);
			end = end == std::string::npos ? real.size() : end + 1;
			gz.printf("%s", real.substr(at, end - at).c_str());
			at = end;
		}
		gz.close();
	}
	auto t3 = Clock::now();
	std::string built;
	{
		CFile mem;
		mem.open("", CL_WRITE_MEMORY | CL_OPEN_WRITE);
		size_t at = 0;
		while (at < real.size()) {
			size_t end = real.find('\n', at);
			end = end == std::string::npos ? real.size() : end + 1;
			mem.printf("%s", real.substr(at, end - at).c_str());
			at = end;
		}
		built = mem.TakeText();
	}
	auto t4 = Clock::now();
	// The part that moved off the game thread: compress, write, rename.
	REQUIRE(WriteSaveText(dir / "real.sav", built));
	auto t5 = Clock::now();
	CHECK(built == real);
	MESSAGE("bytes=" << real.size() << " packed=" << fs::file_size(dir / "real.sav.gz")
	        << " | map fields old(gzip, 6 writes/field)=" << ms(t0, t1)
	        << " ms new(memory, 1 write/field)=" << ms(t1, t2)
	        << " ms | whole text by lines old(gzip)=" << ms(t2, t3)
	        << " ms new(memory)=" << ms(t3, t4)
	        << " ms | compress+write+rename (writer thread)=" << ms(t4, t5) << " ms");
	std::error_code ec;
	fs::remove_all(dir, ec);
}
