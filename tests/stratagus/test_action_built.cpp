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
/**@name test_action_built.cpp - The test file for action_built.cpp. */
//
//      (c) Copyright 2024 by Matthias Schwarzott
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
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//      02111-1307, USA.
//

#include <doctest.h>

#include "stratagus.h"
#include "unit.h"
#include "action/action_built.h"
#include "construct.h"
#include "fov.h"
#include "map.h"
#include "settings.h"

#include <map>
#include <vector>

class tested_COrder_Built : public COrder_Built
{
public:
	using COrder_Built::ProgressCounter;

	int testUpdateConstrFrame_Percent(CUnit &unit, int percent, int initialFrame = 9999)
	{
		// One percent of the construction, as COrder_Built::UpdateConstructionFrame takes it
		// (src/action/action_built.cpp): the data cost plus the measured extra time unit.
		const int progressOnePercent = (unit.Type->Stats[0].Costs[TimeCost] + WC2_BUILD_EXTRA_TIME_UNITS) * WC2_BUILD_FRAMES_PER_TIME_UNIT;
		return testUpdateConstrFrame_Progress(unit, percent * progressOnePercent, initialFrame);
	}

	int testUpdateConstrFrame_Progress(CUnit &unit, int progress, int initialFrame = 9999)
	{
		unit.Frame = initialFrame; // to see if it was set
		Frame = -5; // force re-calculation
		ProgressCounter = progress;
		UpdateConstructionFrame(unit);
		return unit.Frame;
	}
};

namespace {
	std::vector<CConstructionFrame> makeFrames(
		const std::vector<std::pair<int, int>>& vec)
	{
		std::vector<CConstructionFrame> cframes;
		const std::size_t size = vec.size();
		cframes.resize(size);
		for (int i = 0; i < size; ++i) {
			cframes[i].Percent = vec[i].first;
			cframes[i].Frame = vec[i].second;
		}
		return cframes;
	}
};

TEST_CASE("COrder_Built Frame")
{
	CPlayer player;
	player.Index = 0;
	CUnitType type;
	type.Stats[0].Costs[TimeCost] = 1;
	CConstruction construction;
	type.Construction = &construction;
	auto &frames = construction.Frames;
	CUnit unit;
	unit.Type = &type;
	unit.Player = &player;
	tested_COrder_Built order;

	SUBCASE("init values") {
		CHECK(unit.Frame == 0);
		CHECK(order.ProgressCounter == 0);
	}

	SUBCASE("size=0") {
		frames = makeFrames({});

		// check unit.Frame is not modified
		CHECK(order.testUpdateConstrFrame_Percent(unit, 0, 123) == 123);
	}

	SUBCASE("size=1") {
		frames = makeFrames({
			{0, 0},
		});

		CHECK(order.testUpdateConstrFrame_Percent(unit,   0) == 0);
		CHECK(order.testUpdateConstrFrame_Percent(unit,   1) == 0);
		CHECK(order.testUpdateConstrFrame_Percent(unit, 100) == 0);
	}

	SUBCASE("large steps") {
		frames = makeFrames({
			{ 0, 100},
			{50, 110},
		});

		SUBCASE("normal") {
			CHECK(order.testUpdateConstrFrame_Percent(unit,   0) == 100);
			CHECK(order.testUpdateConstrFrame_Percent(unit,   1) == 100);
			CHECK(order.testUpdateConstrFrame_Percent(unit,  49) == 100);
			CHECK(order.testUpdateConstrFrame_Percent(unit,  50) == 110);
			CHECK(order.testUpdateConstrFrame_Percent(unit,  51) == 110);
			CHECK(order.testUpdateConstrFrame_Percent(unit, 100) == 110);
		}

		SUBCASE("mirrored") {
			CHECK(order.testUpdateConstrFrame_Percent(unit, 49, -1) == -101);
			CHECK(order.testUpdateConstrFrame_Percent(unit, 50, -1) == -111);
		}

		SUBCASE("out of bounds") {
			// return frame of first entry
			CHECK(order.testUpdateConstrFrame_Percent(unit,  -1) == 100);
			CHECK(order.testUpdateConstrFrame_Percent(unit, 101) == 110);
		}
	}

	SUBCASE("partially dense")
	{
		frames = makeFrames({
			{  0, 0},
			{  1, 1},
			{  2, 2},
			{ 65, 3},
			{ 66, 4},
			{ 67, 5},
			{ 99, 6},
			{100, 7},
		});

		CHECK(order.testUpdateConstrFrame_Percent(unit,   0) == 0);
		CHECK(order.testUpdateConstrFrame_Percent(unit,   1) == 1);
		CHECK(order.testUpdateConstrFrame_Percent(unit,   2) == 2);
		CHECK(order.testUpdateConstrFrame_Percent(unit,   3) == 2);

		CHECK(order.testUpdateConstrFrame_Percent(unit,  50) == 2);

		CHECK(order.testUpdateConstrFrame_Percent(unit,  64) == 2);
		CHECK(order.testUpdateConstrFrame_Percent(unit,  65) == 3);
		CHECK(order.testUpdateConstrFrame_Percent(unit,  66) == 4);
		CHECK(order.testUpdateConstrFrame_Percent(unit,  67) == 5);
		CHECK(order.testUpdateConstrFrame_Percent(unit,  68) == 5);

		CHECK(order.testUpdateConstrFrame_Percent(unit,  98) == 5);
		CHECK(order.testUpdateConstrFrame_Percent(unit,  99) == 6);
		CHECK(order.testUpdateConstrFrame_Percent(unit, 100) == 7);

		SUBCASE("by progress")
		{
			// One percent of progress for the time cost 1 of this test (see the helper above).
			const int one = (1 + WC2_BUILD_EXTRA_TIME_UNITS) * WC2_BUILD_FRAMES_PER_TIME_UNIT;
			CHECK(order.testUpdateConstrFrame_Progress(unit, 0) == 0);
			CHECK(order.testUpdateConstrFrame_Progress(unit, one - 1) == 0);
			CHECK(order.testUpdateConstrFrame_Progress(unit, one) == 1);
			CHECK(order.testUpdateConstrFrame_Progress(unit, 99 * one) == 6);
			CHECK(order.testUpdateConstrFrame_Progress(unit, 100 * one - 1) == 6);
			CHECK(order.testUpdateConstrFrame_Progress(unit, 100 * one) == 7);
		}
	}

	SUBCASE("repeated values")
	{
		frames = makeFrames({
			{  0, 8},
			{ 80, 1},
			{ 90, 8},
		});

		CHECK(order.testUpdateConstrFrame_Percent(unit, 79) == 8);
		CHECK(order.testUpdateConstrFrame_Percent(unit, 80) == 1);
		CHECK(order.testUpdateConstrFrame_Percent(unit, 89) == 1);
		CHECK(order.testUpdateConstrFrame_Percent(unit, 90) == 8);
	}

	SUBCASE("first key larger") {
		frames = makeFrames({
			{ 10, 5},
		});

		CHECK(order.testUpdateConstrFrame_Percent(unit,  9) == 5);
		CHECK(order.testUpdateConstrFrame_Percent(unit, 10) == 5);
	}

	SUBCASE("duplicate keys")
	{
		frames = makeFrames({
			{  0, 0},
			{ 20, 1},
			{ 20, 2},
			{ 60, 3},
		});

		CHECK(order.testUpdateConstrFrame_Percent(unit, 19) == 0);
		CHECK(order.testUpdateConstrFrame_Percent(unit, 20) == 2);
		CHECK(order.testUpdateConstrFrame_Percent(unit, 21) == 2);
	}

	SUBCASE("wrong key order") {
		frames = makeFrames({
			{ 75, 9},
			{ 50, 8},
			{  0, 7},
		});

		CHECK_FALSE(ranges::is_sorted(frames, std::less<>{}, &CConstructionFrame::Percent));
		const bool allgood =
			order.testUpdateConstrFrame_Percent(unit,   0) == 7 &&
			order.testUpdateConstrFrame_Percent(unit,  49) == 7 &&
			order.testUpdateConstrFrame_Percent(unit,  50) == 8 &&
			order.testUpdateConstrFrame_Percent(unit,  74) == 8 &&
			order.testUpdateConstrFrame_Percent(unit,  75) == 9 &&
			order.testUpdateConstrFrame_Percent(unit,  76) == 9;
		CHECK_FALSE(allgood);
	}

	SUBCASE("too small key value") {
		frames = makeFrames({
			{ -1, 7},
			{100, 8},
		});

		CHECK(order.testUpdateConstrFrame_Percent(unit,   0) == 7);
		CHECK(order.testUpdateConstrFrame_Percent(unit,   1) == 7);
		CHECK(order.testUpdateConstrFrame_Percent(unit,  99) == 7);
		CHECK(order.testUpdateConstrFrame_Percent(unit, 100) == 8);
	}

	SUBCASE("too large key value") {
		frames = makeFrames({
			{  0, 7},
			{101, 8},
		});

		CHECK(order.testUpdateConstrFrame_Percent(unit,   0) == 7);
		CHECK(order.testUpdateConstrFrame_Percent(unit, 100) == 7);
	}
}

// Sight of a unit: the outlines measured on the original (work/agent-orig-12/ORIG12.md section 2,
// fit_output.txt). Tiles are map tiles of Dark Paths BNE (64 x 64).
namespace {
	struct SightRun { int y0, y1, x0, x1; };  // rows y0..y1 are lit from x0 to x1
	struct SightSample {
		const char *name;
		int x, y, w, h;          // top-left tile and size of the unit
		int range;
		int bx0, bx1, by0, by1;  // the tile box that was read; every tile outside the runs is dark
		                         // (the view of o12_15 and o12_39 ended at column 62)
		std::vector<SightRun> runs;
	};

	const std::vector<SightSample> sightSamples = {
		{"o12_15 Town Hall 1", 55, 20, 4, 4, 4, 51, 62, 14, 29, {{18, 18, 54, 60}, {19, 25, 53, 61}, {26, 26, 54, 60}}},
		{"o12_37 Town Hall 2", 46, 11, 4, 4, 4, 42, 54, 9, 17, {{9, 9, 45, 51}, {10, 16, 44, 52}, {17, 17, 45, 51}}},
		{"o12_21 Farm", 42, 20, 2, 2, 3, 37, 48, 15, 27, {{18, 18, 41, 45}, {19, 23, 40, 46}, {24, 24, 41, 45}}},
		{"o12_24 Barracks", 45, 4, 3, 3, 3, 40, 52, 0, 9, {{2, 2, 44, 48}, {3, 7, 43, 49}, {8, 8, 44, 48}}},
		{"o12_15 Peasant a", 43, 21, 1, 1, 4, 37, 49, 15, 27, {{17, 17, 40, 46}, {18, 24, 39, 47}, {25, 25, 40, 46}}},
		{"o12_25 Peasant b", 56, 37, 1, 1, 4, 48, 63, 30, 44, {{33, 33, 53, 59}, {34, 40, 52, 60}, {41, 41, 53, 59}}},
		{"o12_39 Elven Archer", 57, 3, 1, 1, 5, 52, 62, 0, 9, {{0, 6, 52, 62}, {7, 7, 53, 61}, {8, 8, 54, 60}}},
		{"o12_29 Scout Tower a", 53, 37, 2, 2, 9, 40, 63, 27, 47, {{29, 29, 47, 61}, {30, 30, 46, 62}, {31, 45, 45, 63}, {46, 46, 46, 62}, {47, 47, 47, 61}}},
		{"o12_32 Scout Tower b", 53, 37, 2, 2, 9, 40, 63, 27, 47, {{29, 29, 47, 61}, {30, 30, 46, 62}, {31, 45, 45, 63}, {46, 46, 46, 62}, {47, 47, 47, 61}}},
		// buildings under construction: range 3
		{"o12_19 site Barracks", 45, 4, 3, 3, 3, 40, 52, 0, 9, {{2, 2, 44, 48}, {3, 7, 43, 49}, {8, 8, 44, 48}}},
		{"o12_27 site Scout Tower", 53, 37, 2, 2, 3, 44, 63, 27, 47, {{35, 35, 52, 56}, {36, 40, 51, 57}, {41, 41, 52, 56}}},
		{"o12_31 site Town Hall early", 46, 11, 4, 4, 3, 42, 54, 9, 17, {{10, 10, 46, 50}, {11, 15, 45, 51}, {16, 16, 46, 50}}},
		{"o12_34 site Town Hall later", 46, 11, 4, 4, 3, 42, 54, 9, 17, {{10, 10, 46, 50}, {11, 15, 45, 51}, {16, 16, 46, 50}}},
		{"o12_19 site Farm", 42, 20, 2, 2, 3, 37, 48, 17, 22, {{18, 18, 41, 45}, {19, 22, 40, 46}}},
	};

	bool sightMeasuredLit(const SightSample &s, int x, int y)
	{
		for (const SightRun &run : s.runs) {
			if (run.y0 <= y && y <= run.y1) {
				return run.x0 <= x && x <= run.x1;
			}
		}
		return false;
	}

	std::map<unsigned int, int> sightMarks;
	void sightTestMark(const CPlayer &, const unsigned int index) { ++sightMarks[index]; }
	void sightTestUnmark(const CPlayer &, const unsigned int index)
	{
		if (--sightMarks[index] == 0) {
			sightMarks.erase(index);
		}
	}

	// dx^2 + dy^2 <= num / den * r^2, written without the function under test
	int sightFittedHalfWidth(int range, int dy, int num, int den)
	{
		int dx = -1;
		for (int x = 0; x <= range; ++x) {
			if (den * (x * x + dy * dy) <= num * range * range) {
				dx = x;
			}
		}
		return dx;
	}
};

TEST_CASE("Sight shape: the measured outlines")
{
	int tiles = 0;
	int misfits = 0;
	for (const SightSample &s : sightSamples) {
		const int cx = s.x + s.w / 2;
		const int cy = s.y + s.h / 2;
		for (int y = s.by0; y <= s.by1; ++y) {
			const int half = SightHalfWidth(s.range, y - cy);
			for (int x = s.bx0; x <= s.bx1; ++x) {
				const bool lit = half >= 0 && x >= cx - half && x <= cx + half;
				++tiles;
				if (lit != sightMeasuredLit(s, x, y)) {
					++misfits;
					CAPTURE(s.name);
					CAPTURE(x);
					CAPTURE(y);
					CHECK(lit == sightMeasuredLit(s, x, y));
				}
			}
		}
	}
	CHECK(tiles == 2978); // the tiles read in the run
	CHECK(misfits == 0);
}

TEST_CASE("Sight shape: rows")
{
	CHECK(SightHalfWidth(0, 0) == -1);
	CHECK(SightHalfWidth(4, 5) == -1);
	CHECK(SightHalfWidth(4, -5) == -1);
	CHECK(SightHalfWidth(4, -4) == SightHalfWidth(4, 4));

	// Every constant of the measured interval [1.605, 1.64) gives the same rows for the radii 1 .. 9,
	// and the measured radii equal that form.
	for (int r = 1; r <= 9; ++r) {
		for (int dy = 0; dy <= r; ++dy) {
			CAPTURE(r);
			CAPTURE(dy);
			CHECK(SightHalfWidth(r, dy) == sightFittedHalfWidth(r, dy, 1605, 1000));
			CHECK(SightHalfWidth(r, dy) == sightFittedHalfWidth(r, dy, 1639, 1000));
		}
	}

	// Radii the unit data uses that were NOT measured: what the fitted form gives.
	CHECK(SightHalfWidth(1, 0) == 1);
	CHECK(SightHalfWidth(1, 1) == 0);
	CHECK(SightHalfWidth(2, 1) == 2);
	CHECK(SightHalfWidth(2, 2) == 1);
	CHECK(SightHalfWidth(6, 4) == 6);
	CHECK(SightHalfWidth(6, 5) == 5);
	CHECK(SightHalfWidth(6, 6) == 4);
	CHECK(SightHalfWidth(8, 6) == 8);
	CHECK(SightHalfWidth(8, 7) == 7);
	CHECK(SightHalfWidth(8, 8) == 6);
}

TEST_CASE("Sight marking: from the centre tile, clipped to the map, unmark is the same shape")
{
	const int oldWidth = Map.Info.MapWidth;
	const int oldHeight = Map.Info.MapHeight;
	const FieldOfViewTypes oldFoV = GameSettings.FoV;
	Map.Info.MapWidth = 64;
	Map.Info.MapHeight = 64;
	GameSettings.FoV = FieldOfViewTypes::cSimpleRadial;

	CPlayer player;
	player.Index = 0;
	CUnitType type;
	CUnit unit;
	unit.Type = &type;
	unit.Player = &player;
	sightMarks.clear();

	for (const SightSample &s : sightSamples) {
		CAPTURE(s.name);
		type.Building = s.w > 1; // the larger samples are buildings; a moving unit lights from its own tile
		MapSight(player, unit, Vec2i(s.x, s.y), s.w, s.h, s.range, sightTestMark);

		int misfits = 0;
		for (int y = s.by0; y <= s.by1; ++y) {
			for (int x = s.bx0; x <= s.bx1; ++x) {
				const auto it = sightMarks.find(x + y * 64);
				const int count = it == sightMarks.end() ? 0 : it->second;
				misfits += count != (sightMeasuredLit(s, x, y) ? 1 : 0);
			}
		}
		CHECK(misfits == 0);
		for (const auto &[index, count] : sightMarks) {
			CHECK(index < 64u * 64u);
			CHECK(count == 1);
		}

		MapSight(player, unit, Vec2i(s.x, s.y), s.w, s.h, s.range, sightTestUnmark);
		CHECK(sightMarks.empty());
	}

	SUBCASE("full shapes") {
		const int lit[][2] = {{3, 45}, {4, 77}, {5, 109}, {9, 349}}; // ORIG12 section 0
		for (const auto &entry : lit) {
			MapSight(player, unit, Vec2i(30, 30), 1, 1, entry[0], sightTestMark);
			CHECK(sightMarks.size() == size_t(entry[1]));
			sightMarks.clear();
		}
	}

	SUBCASE("map corner") {
		MapSight(player, unit, Vec2i(0, 0), 1, 1, 4, sightTestMark);
		CHECK(sightMarks.size() == 24u); // rows 0..3: 0..4, row 4: 0..3
		CHECK(sightMarks.count(4 + 3 * 64) == 1);
		CHECK(sightMarks.count(4 + 4 * 64) == 0);
		sightMarks.clear();

		MapSight(player, unit, Vec2i(62, 62), 2, 2, 3, sightTestMark); // centre tile 63,63
		CHECK(sightMarks.size() == 15u); // rows 61..63: 60..63, row 60: 61..63
		CHECK(sightMarks.count(60 + 60 * 64) == 0);
		sightMarks.clear();
	}

	SUBCASE("range 0 marks nothing") {
		MapSight(player, unit, Vec2i(30, 30), 4, 4, 0, sightTestMark);
		CHECK(sightMarks.empty());
	}

	Map.Info.MapWidth = oldWidth;
	Map.Info.MapHeight = oldHeight;
	GameSettings.FoV = oldFoV;
}

TEST_CASE("Sight range of a building under construction")
{
	CPlayer player;
	player.Index = 0;
	CUnitType type;
	CUnit unit;
	unit.Type = &type;
	unit.Player = &player;
	unit.Constructed = 1;
	unit.CurrentSightRange = 0;

	UpdateUnitSightRange(unit);
	CHECK(unit.CurrentSightRange == 3); // ORIG12: o12_19, o12_27, o12_31, o12_34
}

// Movement on the two-tile grid (ships and flyers). Synthetic maps only.

#include "actions.h"
#include "pathfinder.h"
#include "unittype.h"

#include <string>

extern int AStarFindPath(const Vec2i &startPos, const Vec2i &goalPos, int gw, int gh,
                         int tilesizex, int tilesizey, int minrange, int maxrange,
                         char *path, int pathlen, const CUnit &unit, bool squareRange = false);

namespace
{
/// A synthetic map with every tile free, the pathfinder set up on it.
struct SyntheticMap
{
	SyntheticMap(int w, int h)
	{
		Map.Info.MapWidth = w;
		Map.Info.MapHeight = h;
		Map.Create();
		InitPathfinder();
		AStarKnowUnseenTerrain = true;
	}
	~SyntheticMap()
	{
		AStarKnowUnseenTerrain = false;
		Map.Fields.clear();
		FreePathfinder();
	}
	void Fill(const Vec2i &topLeft, const Vec2i &bottomRight, tile_flags flags)
	{
		for (int y = topLeft.y; y <= bottomRight.y; ++y) {
			for (int x = topLeft.x; x <= bottomRight.x; ++x) {
				Map.Field(x, y)->Flags |= flags;
			}
		}
	}
	/// A unit at rest on its tile, as MarkUnitFieldFlags and CMap::Insert leave it.
	void Rest(CUnit &unit, const Vec2i &pos, tile_flags flag)
	{
		unit.tilePos = pos;
		Map.Field(pos)->Flags |= flag;
		Map.Field(pos)->UnitCache.push_back(&unit);
	}
};

/// The stored path in walking order, one heading digit per step, then '+' and the length not stored.
std::string PathString(const PathFinderOutput &data)
{
	std::string res;
	for (int i = 0; i != data.Length; ++i) {
		res += char('0' + data.Path[data.Length - 1 - i]);
	}
	res += '+';
	res += std::to_string(data.OverflowLength);
	return res;
}

std::string FirstPath(CUnit &unit, const Vec2i &dest)
{
	unit.Orders.push_back(COrder::NewActionMove(dest));
	NextPathElement(unit);
	std::string res = PathString(unit.pathFinderData->output);
	unit.Orders.clear();
	return res;
}

struct WalkResult
{
	std::string steps; /// 'S' straight, 'D' diagonal, in the order walked
	int end = 0;       /// the pathfinder's last answer
	bool onGrid = true; /// every tile stood on had even coordinates
};

/// Order the unit to a tile and take path elements until the pathfinder stops it.
WalkResult Walk(CUnit &unit, const Vec2i &click)
{
	WalkResult res;
	unit.Orders.push_back(COrder::NewActionMove(click));
	unit.pathFinderData->output.Length = 0;
	for (int i = 0; i != 200; ++i) {
		const auto [d, dir] = NextPathElement(unit);
		res.end = d;
		if (d == PF_WAIT && unit.pathFinderData->output.Length <= 0) {
			// the stored path is used up: the next call searches again
			continue;
		}
		if (d <= 0) {
			break;
		}
		res.steps += (dir.x && dir.y) ? 'D' : 'S';
		unit.tilePos += dir;
		res.onGrid = res.onGrid && unit.tilePos.x % 2 == 0 && unit.tilePos.y % 2 == 0;
	}
	unit.Orders.clear();
	return res;
}

constexpr tile_flags NavalMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding
                                 | MapFieldCoastAllowed | MapFieldLandAllowed | MapFieldUnpassable;

struct GridFixture
{
	GridFixture() : map(128, 128)
	{
		player.Index = 0;
		for (CUnitType *type : {&ship, &flyer}) {
			type->TileWidth = 1;
			type->TileHeight = 1;
			type->GridStep = 2;
			type->BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		}
		ship.MoveType = EMovement::Naval;
		ship.MovementMask = NavalMask;
		flyer.MoveType = EMovement::Fly;
		flyer.MovementMask = MapFieldAirUnit;
		unit.Player = &player;
		unit.Type = &ship;
		other.Player = &player;
		other.Type = &ship;
	}
	SyntheticMap map;
	CPlayer player;
	CUnitType ship;
	CUnitType flyer;
	CUnit unit;
	CUnit other;
};
} // namespace

TEST_CASE("PathFinding: paths of a unit with step 1 are as before the grid")
{
	// The expected strings were taken from the pathfinder before a step other than 1 existed.
	SyntheticMap map(64, 64);
	CPlayer player;
	player.Index = 0;
	CUnitType type;
	type.TileWidth = 1;
	type.TileHeight = 1;
	type.MovementMask = MapFieldUnpassable;
	type.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
	CUnit unit;
	unit.Player = &player;
	unit.Type = &type;

	CHECK(type.GridStep == 1);

	SUBCASE("open ground, oblique")
	{
		unit.tilePos = {5, 5};
		CHECK(FirstPath(unit, {20, 11}) == "333333222222222+0");
	}
	SUBCASE("wall with a gap")
	{
		map.Fill({12, 0}, {12, 17}, MapFieldUnpassable);
		map.Fill({12, 19}, {12, 30}, MapFieldUnpassable);
		unit.tilePos = {5, 5};
		CHECK(FirstPath(unit, {20, 5}) == "33333344444431111111100000+0");
	}
	SUBCASE("2x2 unit around a block")
	{
		type.TileWidth = 2;
		type.TileHeight = 2;
		map.Fill({10, 8}, {13, 12}, MapFieldUnpassable);
		unit.tilePos = {6, 10};
		CHECK(FirstPath(unit, {18, 10}) == "333222211122+0");
	}
	SUBCASE("goal one tile away")
	{
		unit.tilePos = {5, 5};
		CHECK(FirstPath(unit, {6, 6}) == "3+0");
	}
	SUBCASE("any tile is a place for it")
	{
		CHECK(UnitTypeCanBeAt(type, {7, 9}));
		CHECK(UnitTypeCanBeAt(type, {8, 9}));
	}
}

TEST_CASE("Grid step 2: the measured rows of an order to a tile on open water")
{
	GridFixture f;

	struct Row
	{
		Vec2i start;
		Vec2i click;
		Vec2i rest;
		const char *steps; /// nullptr: the way was not recorded, only the resting tile
	};
	// Start, clicked tile, resting tile and the way of each measured order (run 15 of the
	// measurements on the original: rows 07..28 and the five further rows of its table A).
	const Row rows[] = {
		{{46, 90}, {41, 91}, {42, 90}, "SS"},
		{{46, 86}, {41, 91}, {40, 90}, "DSD"},
		{{40, 84}, {41, 91}, {40, 90}, "SSS"},
		{{42, 84}, {41, 90}, {40, 90}, "SDS"},
		{{46, 92}, {40, 91}, {40, 90}, "SDS"},
		{{40, 96}, {41, 91}, {40, 92}, "SS"},
		{{36, 98}, {41, 93}, {40, 92}, "DSD"},
		{{44, 96}, {39, 93}, {40, 94}, "DS"},
		{{22, 90}, {27, 95}, {26, 94}, "DD"},
		{{26, 90}, {27, 95}, {26, 94}, "SS"},
		{{22, 100}, {27, 95}, {26, 94}, "DSD"},
		{{28, 100}, {27, 95}, {26, 96}, "SD"},
		{{32, 100}, {27, 95}, {28, 96}, "DD"},
		{{34, 96}, {27, 95}, {28, 94}, "SDS"},
		{{22, 94}, {27, 95}, {26, 94}, "SS"},
		{{34, 92}, {27, 95}, {28, 94}, "SDS"},
		{{32, 100}, {27, 94}, {26, 94}, "DDD"},
		{{22, 100}, {28, 95}, {28, 94}, "DDD"},
		{{22, 96}, {28, 95}, {28, 94}, "SDS"},
		{{36, 96}, {28, 95}, {28, 94}, "SDSS"},
		{{28, 100}, {27, 94}, {26, 94}, "SDS"},
		{{22, 96}, {27, 95}, {26, 94}, "DS"},
		{{32, 94}, {35, 97}, {34, 96}, nullptr},
		{{40, 94}, {37, 91}, {38, 92}, nullptr},
		{{10, 108}, {15, 111}, {14, 110}, nullptr},
		{{64, 114}, {59, 111}, {60, 112}, nullptr},
		{{38, 94}, {37, 91}, {36, 92}, nullptr},
	};
	for (const Row &row : rows) {
		CAPTURE(row.start);
		CAPTURE(row.click);
		f.unit.tilePos = row.start;
		const WalkResult walk = Walk(f.unit, row.click);
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == row.rest);
		CHECK(walk.onGrid);
		if (row.steps) {
			CHECK(walk.steps == row.steps);
		}
	}
}

TEST_CASE("Grid step 2: goals")
{
	GridFixture f;

	SUBCASE("an even tile is reached exactly")
	{
		f.unit.tilePos = {10, 10};
		const WalkResult walk = Walk(f.unit, {20, 16});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(20, 16));
		CHECK(walk.onGrid);
		CHECK(walk.steps == "DSDSD"); // a = 5, b = 3
	}
	SUBCASE("no move when already within one tile of the clicked tile")
	{
		for (const Vec2i click : {Vec2i(41, 91), Vec2i(41, 90), Vec2i(40, 91), Vec2i(39, 89), Vec2i(40, 90)}) {
			CAPTURE(click);
			f.unit.tilePos = {40, 90};
			const WalkResult walk = Walk(f.unit, click);
			CHECK(walk.end == PF_REACHED);
			CHECK(walk.steps.empty());
			CHECK(f.unit.tilePos == Vec2i(40, 90));
		}
	}
	SUBCASE("an odd tile two tiles away is one step")
	{
		f.unit.tilePos = {40, 90};
		const WalkResult walk = Walk(f.unit, {43, 90});
		CHECK(walk.steps == "S");
		CHECK(f.unit.tilePos == Vec2i(42, 90));
	}
	SUBCASE("a way longer than the stored path stays on the line")
	{
		f.unit.tilePos = {2, 2};
		const WalkResult walk = Walk(f.unit, {122, 2});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(122, 2));
		CHECK(walk.steps == std::string(60, 'S'));
	}
}

TEST_CASE("Grid step 2: terrain is tested on the unit's one tile")
{
	GridFixture f;

	SUBCASE("only grid tiles are a place for it")
	{
		CHECK(UnitTypeCanBeAt(f.ship, {10, 10}));
		CHECK_FALSE(UnitTypeCanBeAt(f.ship, {11, 10}));
		CHECK_FALSE(UnitTypeCanBeAt(f.ship, {10, 11}));
		CHECK_FALSE(UnitTypeCanBeAt(f.ship, {11, 11}));
	}
	SUBCASE("water with coast on each of the four sides")
	{
		for (const Vec2i side : {Vec2i(29, 30), Vec2i(31, 30), Vec2i(30, 29), Vec2i(30, 31)}) {
			f.map.Fill(side, side, MapFieldCoastAllowed);
		}
		CHECK(UnitTypeCanBeAt(f.ship, {30, 30}));
		// the tiles between are coast, the diagonals are open
		f.unit.tilePos = {26, 26};
		const WalkResult walk = Walk(f.unit, {30, 30});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(30, 30));
	}
	SUBCASE("a tile directly beside a building")
	{
		f.map.Fill({31, 28}, {33, 31}, MapFieldBuilding | MapFieldLandAllowed);
		CHECK(UnitTypeCanBeAt(f.ship, {30, 30}));
		f.unit.tilePos = {20, 30};
		const WalkResult walk = Walk(f.unit, {30, 30});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(30, 30));
	}
	SUBCASE("a coast tile is refused")
	{
		f.map.Fill({30, 30}, {30, 30}, MapFieldCoastAllowed);
		CHECK_FALSE(UnitTypeCanBeAt(f.ship, {30, 30}));
		f.unit.tilePos = {20, 30};
		const WalkResult walk = Walk(f.unit, {30, 30});
		CHECK(walk.end == PF_UNREACHABLE);
		CHECK(f.unit.tilePos != Vec2i(30, 30));
		CHECK(walk.onGrid);
	}
	SUBCASE("the tile between the ends of a diagonal step may be coast")
	{
		f.map.Fill({21, 101}, {21, 101}, MapFieldCoastAllowed);
		f.unit.tilePos = {20, 102};
		WalkResult walk = Walk(f.unit, {22, 100});
		CHECK(walk.steps == "D");
		CHECK(f.unit.tilePos == Vec2i(22, 100));
		walk = Walk(f.unit, {20, 102});
		CHECK(walk.steps == "D");
		CHECK(f.unit.tilePos == Vec2i(20, 102));
	}
	SUBCASE("the tile between the ends of a straight step is not tested")
	{
		f.map.Fill({21, 50}, {21, 50}, MapFieldCoastAllowed);
		f.unit.tilePos = {20, 50};
		const WalkResult walk = Walk(f.unit, {24, 50});
		CHECK(walk.steps == "SS");
		CHECK(f.unit.tilePos == Vec2i(24, 50));
	}
	SUBCASE("a tile on land: the widening range of the order ends it on a grid water tile at the shore")
	{
		// Which shore tile the original takes is not determined; this fixes only that the
		// existing widening of the range works on the grid.
		f.map.Fill({0, 100}, {127, 127}, MapFieldLandAllowed);
		f.unit.tilePos = {36, 94};
		char path[PathFinderOutput::MAX_PATH_LENGTH];
		int length = PF_UNREACHABLE;
		int range = 0;
		for (; range != 20 && length == PF_UNREACHABLE; ++range) {
			length = AStarFindPath(f.unit.tilePos, {36, 104}, 0, 0, 1, 1, 0, range, path, sizeof(path), f.unit);
		}
		REQUIRE(length > 0);
		Vec2i pos = f.unit.tilePos;
		for (int i = length - 1; i >= 0; --i) {
			pos.x += Heading2X[(int) path[i]] * 2;
			pos.y += Heading2Y[(int) path[i]] * 2;
			CHECK(UnitTypeCanBeAt(f.ship, pos));
		}
		CHECK(pos.y == 98);
		CHECK(pos.x % 2 == 0);
	}
	SUBCASE("around land the way stays on grid tiles")
	{
		f.map.Fill({30, 20}, {33, 40}, MapFieldLandAllowed);
		f.unit.tilePos = {26, 30};
		const WalkResult walk = Walk(f.unit, {38, 30});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(38, 30));
		CHECK(walk.onGrid);
		CHECK(walk.steps.size() > 6);
	}
}

TEST_CASE("Grid step 2: other units")
{
	GridFixture f;

	SUBCASE("a second ship sent to a taken tile stops two tiles from it, on its side")
	{
		f.map.Rest(f.other, {40, 94}, MapFieldSeaUnit);
		f.unit.tilePos = {30, 94};
		// with the range of a plain order the taken tile cannot be had
		WalkResult walk = Walk(f.unit, {40, 94});
		CHECK(walk.end == PF_UNREACHABLE);
		CHECK(f.unit.tilePos == Vec2i(30, 94));
		// the move order widens its range by one per failure: at 2 the neighbours on the grid count
		char path[PathFinderOutput::MAX_PATH_LENGTH];
		CHECK(AStarFindPath(f.unit.tilePos, {40, 94}, 0, 0, 1, 1, 0, 1, path, sizeof(path), f.unit) == PF_UNREACHABLE);
		const int length = AStarFindPath(f.unit.tilePos, {40, 94}, 0, 0, 1, 1, 0, 2, path, sizeof(path), f.unit);
		REQUIRE(length == 4);
		Vec2i pos = f.unit.tilePos;
		for (int i = length - 1; i >= 0; --i) {
			pos.x += Heading2X[(int) path[i]] * 2;
			pos.y += Heading2Y[(int) path[i]] * 2;
			CHECK(pos != f.other.tilePos);
		}
		CHECK(pos == Vec2i(38, 94));
	}
	SUBCASE("a ship in the way is gone around, never nearer than two tiles")
	{
		f.map.Rest(f.other, {34, 94}, MapFieldSeaUnit);
		f.unit.tilePos = {30, 94};
		const WalkResult walk = Walk(f.unit, {40, 94});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(40, 94));
		CHECK(walk.onGrid);
		CHECK(walk.steps.size() == 5);
	}
	SUBCASE("an odd click beside a resting ship ends on another grid tile within one of it")
	{
		f.map.Rest(f.other, {38, 92}, MapFieldSeaUnit);
		f.unit.tilePos = {38, 100};
		const WalkResult walk = Walk(f.unit, {37, 91});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos != f.other.tilePos);
		CHECK(std::abs(f.unit.tilePos.x - 37) <= 1);
		CHECK(std::abs(f.unit.tilePos.y - 91) <= 1);
		CHECK(walk.onGrid);
	}
	SUBCASE("a flyer rests over a ship, over land and over a building")
	{
		f.unit.Type = &f.flyer;
		f.map.Rest(f.other, {40, 94}, MapFieldSeaUnit);
		f.map.Fill({50, 94}, {53, 97}, MapFieldBuilding | MapFieldLandAllowed);
		f.unit.tilePos = {30, 94};
		WalkResult walk = Walk(f.unit, {40, 94});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(40, 94));
		walk = Walk(f.unit, {52, 96});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(52, 96));
	}
	SUBCASE("a flyer does not rest on another flyer's tile")
	{
		f.unit.Type = &f.flyer;
		f.other.Type = &f.flyer;
		f.map.Rest(f.other, {40, 94}, MapFieldAirUnit);
		CHECK_FALSE(UnitTypeCanBeAt(f.flyer, {40, 94}));
		f.unit.tilePos = {30, 94};
		const WalkResult walk = Walk(f.unit, {40, 94});
		CHECK(walk.end == PF_UNREACHABLE);
		char path[PathFinderOutput::MAX_PATH_LENGTH];
		const int length = AStarFindPath(f.unit.tilePos, {40, 94}, 0, 0, 1, 1, 0, 2, path, sizeof(path), f.unit);
		CHECK(length == 4); // 38,94: two tiles from the other flyer
	}
}

TEST_CASE("Grid step 2: a created or dropped unit lands on a free grid tile")
{
	GridFixture f;

	Vec2i pos;
	FindNearestDrop(f.ship, {31, 31}, pos, LookingW);
	CHECK(pos.x % 2 == 0);
	CHECK(pos.y % 2 == 0);
	CHECK(std::abs(pos.x - 31) <= 1);
	CHECK(std::abs(pos.y - 31) <= 1);

	// the grid tiles around are taken or land: the search goes on to the next free one
	f.map.Fill({28, 28}, {33, 33}, MapFieldLandAllowed);
	FindNearestDrop(f.ship, {31, 31}, pos, LookingW);
	CHECK(pos.x % 2 == 0);
	CHECK(pos.y % 2 == 0);
	CHECK(UnitTypeCanBeAt(f.ship, pos));
	CHECK((pos.x < 28 || pos.x > 33 || pos.y < 28 || pos.y > 33));

	f.map.Rest(f.other, {40, 40}, MapFieldSeaUnit);
	FindNearestDrop(f.ship, {40, 40}, pos, LookingW);
	CHECK(pos != Vec2i(40, 40));
	CHECK(pos.x % 2 == 0);
	CHECK(pos.y % 2 == 0);
}

// Transporters on the two-tile grid: the coast tile they stand on, where their passengers appear.
// Synthetic shores; the tile classes and coordinates follow the measured scenes.

#include "action/action_unload.h"

#include <set>

namespace
{
constexpr tile_flags TransporterMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding | MapFieldLandAllowed;
constexpr tile_flags LandMask = MapFieldLandUnit | MapFieldSeaUnit | MapFieldBuilding | MapFieldCoastAllowed
                                | MapFieldWaterAllowed | MapFieldUnpassable;

struct ShoreFixture : GridFixture
{
	ShoreFixture()
	{
		transport.TileWidth = 1;
		transport.TileHeight = 1;
		transport.GridStep = 2;
		transport.MaxOnBoard = 6;
		transport.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		transport.MoveType = EMovement::Naval;
		transport.MovementMask = TransporterMask;
		footman.TileWidth = 1;
		footman.TileHeight = 1;
		footman.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		footman.MoveType = EMovement::Land;
		footman.MovementMask = LandMask;
		unit.Type = &transport;
		for (CUnit &passenger : passengers) {
			passenger.Player = &player;
			passenger.Type = &footman;
		}
		map.Fill({0, 0}, {127, 127}, MapFieldWaterAllowed);
	}

	/// '.' water, 'c' coast, '#' plain ground, 'X' wall
	void Paint(const Vec2i &topLeft, const std::vector<std::string> &rows)
	{
		for (size_t y = 0; y != rows.size(); ++y) {
			for (size_t x = 0; x != rows[y].size(); ++x) {
				CMapField &mf = *Map.Field(topLeft.x + int(x), topLeft.y + int(y));
				switch (rows[y][x]) {
					case 'c': mf.Flags = MapFieldCoastAllowed; break;
					case '#': mf.Flags = MapFieldLandAllowed; break;
					case 'X': mf.Flags = MapFieldLandAllowed | MapFieldUnpassable; break;
					default: break;
				}
			}
		}
	}
	/// The edge of the land faces north; from 0,116
	void NorthShore()
	{
		Paint({0, 116}, {"cccc..................",
		                 "###cc.................",
		                 "####cc................",
		                 "#####cccccc...........",
		                 "##########ccc.........",
		                 "############ccc.......",
		                 "##############cc......",
		                 "###############cc....."});
	}
	/// The edge of the land faces west; from 18,92
	void WestShore()
	{
		Paint({18, 92}, {"...........c",
		                 ".........ccc",
		                 ".......ccc##",
		                 "......cc####",
		                 "......c##X##",
		                 "......c#XX##",
		                 "......c#X###",
		                 ".....cc#X###",
		                 ".....c##X###",
		                 "...ccc######"});
	}
	/// The edge of the land faces east; from 12,122
	void EastShore()
	{
		Paint({12, 122}, {"##cc......",
		                  "###cc.....",
		                  "####cc....",
		                  "#####c....",
		                  "#####c....",
		                  "#####c...."});
	}

	/// Send the transporter toward a tile: the tiles of its way to the coast tile it picks.
	std::vector<Vec2i> Dock(const Vec2i &from, const Vec2i &toward)
	{
		std::vector<Vec2i> tiles;
		unit.tilePos = from;
		const auto dock = FindDockTile(unit, toward);
		if (!dock) {
			return tiles;
		}
		unit.Orders.push_back(COrder::NewActionMove(*dock));
		unit.pathFinderData->output.Length = 0;
		for (int i = 0; i != 100; ++i) {
			const auto [d, dir] = NextPathElement(unit);
			if (d == PF_WAIT && unit.pathFinderData->output.Length <= 0) {
				continue;
			}
			if (d <= 0) {
				break;
			}
			unit.tilePos += dir;
			tiles.push_back(unit.tilePos);
		}
		unit.Orders.clear();
		CHECK(unit.tilePos == *dock);
		return tiles;
	}

	/// Unload count passengers from the transporter standing on pos: the tiles in the order taken.
	std::vector<Vec2i> Unload(const Vec2i &pos, int count)
	{
		std::vector<Vec2i> tiles;
		map.Rest(unit, pos, MapFieldSeaUnit);
		for (int i = 0; i != count; ++i) {
			const auto tile = FindUnloadTile(passengers[i], pos);
			if (!tile) {
				break;
			}
			const tile_flags flags = Map.Field(*tile)->Flags;
			CHECK((flags & MapFieldLandAllowed) != 0);
			CHECK((flags & (MapFieldCoastAllowed | MapFieldWaterAllowed | MapFieldUnpassable)) == 0);
			CHECK(std::max(std::abs(tile->x - pos.x), std::abs(tile->y - pos.y)) <= 2);
			map.Rest(passengers[i], *tile, MapFieldLandUnit);
			tiles.push_back(*tile);
		}
		return tiles;
	}

	CUnitType transport;
	CUnitType footman;
	CUnit passengers[12];
};

std::set<std::pair<int, int>> AsSet(const std::vector<Vec2i> &tiles)
{
	std::set<std::pair<int, int>> res;
	for (const Vec2i &tile : tiles) {
		res.insert({tile.x, tile.y});
	}
	return res;
}
} // namespace

TEST_CASE("Transporter: the measured dockings")
{
	ShoreFixture f;
	f.NorthShore();
	f.WestShore();
	f.EastShore();
	using Tiles = std::vector<Vec2i>;

	SUBCASE("north-facing shore, passenger diagonally behind the coast tile")
	{
		CHECK(f.Dock({6, 118}, {4, 120}) == Tiles{{5, 119}});
	}
	SUBCASE("north-facing shore, passenger straight south")
	{
		CHECK(f.Dock({6, 118}, {6, 120}) == Tiles{{6, 119}});
	}
	SUBCASE("north-facing shore, from open water, passenger five tiles inland")
	{
		const Tiles way = f.Dock({8, 116}, {7, 124});
		REQUIRE(!way.empty());
		CHECK(way.back() == Vec2i(8, 119));
	}
	SUBCASE("north-facing shore, unload ordered on the ground behind the coast tile")
	{
		const Tiles way = f.Dock({10, 114}, {11, 121});
		REQUIRE(!way.empty());
		CHECK(way.back() == Vec2i(11, 120));
	}
	SUBCASE("west-facing shore: two one-tile steps, the first diagonal, past the even coast tiles")
	{
		CHECK(f.Dock({22, 98}, {26, 96}) == Tiles{{23, 97}, {24, 97}});
	}
	SUBCASE("east-facing shore")
	{
		CHECK(f.Dock({18, 126}, {16, 126}) == Tiles{{17, 126}});
	}
	SUBCASE("on a coast tile it stays, wherever the order points")
	{
		CHECK(f.Dock({11, 120}, {10, 121}).empty());
		CHECK(f.unit.tilePos == Vec2i(11, 120));
		CHECK(f.Dock({11, 120}, {7, 122}).empty());
		CHECK(f.unit.tilePos == Vec2i(11, 120));
		CHECK(f.Dock({24, 97}, {15, 126}).empty());
		CHECK(f.unit.tilePos == Vec2i(24, 97));
		CHECK(f.Dock({17, 126}, {15, 126}).empty());
		CHECK(f.Dock({8, 119}, {8, 121}).empty());
		// the test of the unload button: the tile it stands on is coast
		CHECK(Map.Field(f.unit.tilePos)->CoastOnMap());
	}
	SUBCASE("no coast on the line: no tile")
	{
		f.unit.tilePos = {40, 40};
		CHECK_FALSE(FindDockTile(f.unit, {60, 44}).has_value());
		// a ship resting on the line
		f.map.Rest(f.other, {8, 118}, MapFieldSeaUnit);
		f.unit.tilePos = {8, 116};
		CHECK_FALSE(FindDockTile(f.unit, {7, 124}).has_value());
	}
}

TEST_CASE("Transporter: where it can stand and how it gets back on the grid")
{
	ShoreFixture f;
	f.NorthShore();
	f.WestShore();

	SUBCASE("off the grid only on a coast tile")
	{
		CHECK(UnitTypeCanBeAt(f.transport, {11, 120}));
		CHECK(UnitTypeCanBeAt(f.transport, {5, 119}));
		CHECK(UnitTypeCanBeAt(f.transport, {24, 96}));
		CHECK(UnitTypeCanBeAt(f.transport, {10, 114}));
		CHECK_FALSE(UnitTypeCanBeAt(f.transport, {11, 119}));
		CHECK_FALSE(UnitTypeCanBeAt(f.transport, {23, 97}));
		CHECK_FALSE(UnitTypeCanBeAt(f.transport, {10, 121}));
		// a warship: no coast tile at all
		CHECK_FALSE(UnitTypeCanBeAt(f.ship, {11, 120}));
		CHECK_FALSE(UnitTypeCanBeAt(f.ship, {24, 96}));
		// a land unit: plain ground only
		CHECK(UnitTypeCanBeAt(f.footman, {10, 121}));
		CHECK_FALSE(UnitTypeCanBeAt(f.footman, {11, 120}));
		CHECK_FALSE(UnitTypeCanBeAt(f.footman, {11, 119}));
		CHECK_FALSE(UnitTypeCanBeAt(f.footman, {26, 97}));
	}
	SUBCASE("from a coast tile to open water it rests on the grid")
	{
		for (const Vec2i from : {Vec2i(11, 120), Vec2i(5, 119), Vec2i(24, 97), Vec2i(8, 119)}) {
			for (const Vec2i click : {Vec2i(8, 116), Vec2i(9, 115), Vec2i(14, 108), Vec2i(20, 106)}) {
				f.unit.tilePos = from;
				const WalkResult walk = Walk(f.unit, click);
				CHECK(walk.end == PF_REACHED);
				CHECK(f.unit.tilePos.x % 2 == 0);
				CHECK(f.unit.tilePos.y % 2 == 0);
				CHECK(std::abs(f.unit.tilePos.x - click.x) <= 1);
				CHECK(std::abs(f.unit.tilePos.y - click.y) <= 1);
				CHECK_FALSE(Map.Field(f.unit.tilePos)->CoastOnMap());
			}
		}
	}
	SUBCASE("on open water it moves like every ship")
	{
		f.unit.tilePos = {40, 60};
		WalkResult walk = Walk(f.unit, {52, 66});
		f.other.tilePos = {40, 60};
		const WalkResult ship = Walk(f.other, {52, 66});
		CHECK(walk.steps == ship.steps);
		CHECK(walk.onGrid);
		CHECK(f.unit.tilePos == f.other.tilePos);
		// stopped between two one-tile steps on water off the grid, it goes back onto the grid
		f.unit.tilePos = {23, 97};
		walk = Walk(f.unit, {20, 96});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos.x % 2 == 0);
		CHECK(f.unit.tilePos.y % 2 == 0);
	}
	SUBCASE("a passenger on the ground touching its coast tile is one tile from it")
	{
		f.unit.tilePos = {11, 120};
		f.passengers[0].tilePos = {10, 121};
		CHECK(f.passengers[0].MapDistanceTo(f.unit) == 1);
		f.passengers[0].tilePos = {9, 121};
		CHECK(f.passengers[0].MapDistanceTo(f.unit) == 2);
	}
}

TEST_CASE("Transporter: the measured unload tiles")
{
	ShoreFixture f;
	f.NorthShore();
	f.WestShore();
	f.EastShore();
	using Set = std::set<std::pair<int, int>>;

	SUBCASE("north-facing shore, transporter on 11,120, one unit")
	{
		CHECK(AsSet(f.Unload({11, 120}, 1)) == Set{{10, 121}});
	}
	SUBCASE("north-facing shore, transporter on 11,120, six units")
	{
		CHECK(AsSet(f.Unload({11, 120}, 6)) == Set{{10, 121}, {11, 121}, {9, 120}, {9, 121}, {9, 122}, {10, 122}});
	}
	SUBCASE("north-facing shore, transporter on 8,119, one unit")
	{
		CHECK(AsSet(f.Unload({8, 119}, 1)) == Set{{7, 120}});
	}
	SUBCASE("west-facing shore, transporter on 24,97, six units: three touching, three in the second ring")
	{
		const std::vector<Vec2i> tiles = f.Unload({24, 97}, 6);
		REQUIRE(tiles.size() == 6);
		CHECK(AsSet({tiles.begin(), tiles.begin() + 3}) == Set{{25, 96}, {25, 97}, {25, 98}});
		CHECK(AsSet({tiles.begin() + 3, tiles.end()}) == Set{{26, 95}, {26, 96}, {25, 99}});
	}
	SUBCASE("west-facing shore, three units")
	{
		CHECK(AsSet(f.Unload({24, 97}, 3)) == Set{{25, 97}, {25, 98}, {25, 96}});
	}
	SUBCASE("east-facing shore, transporter on 17,126, three units, the north-west tile first")
	{
		const std::vector<Vec2i> tiles = f.Unload({17, 126}, 3);
		REQUIRE(tiles.size() == 3);
		CHECK(tiles[0] == Vec2i(16, 125));
		CHECK(AsSet(tiles) == Set{{16, 125}, {16, 126}, {16, 127}});
	}
	SUBCASE("more units than ground in two rings: the rest stay aboard")
	{
		// 24,97 has six tiles of plain ground within two tiles
		CHECK(f.Unload({24, 97}, 12).size() == 6);
		CHECK_FALSE(FindUnloadTile(f.passengers[11], {24, 97}).has_value());
	}
	SUBCASE("no ground within two tiles: nobody leaves")
	{
		CHECK(f.Unload({40, 40}, 1).empty());
	}
}

// Tankers on the two-tile grid: the tile they go into a platform and a depot from, the tile they come
// out on, the tile of a new ship. Synthetic water; buildings stand where the measured ones stood.

namespace
{
struct OilFixture : GridFixture
{
	OilFixture()
	{
		for (CUnitType *type : {&patchType, &platformType, &yardType, &refineryType, &hallType}) {
			type->TileWidth = 3;
			type->TileHeight = 3;
			type->Building = 1;
			type->BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		}
		patchType.GivesResource = OilCost;
		platformType.GivesResource = OilCost;
		for (CUnitType *type : {&yardType, &refineryType}) {
			type->BoolFlag[SHOREBUILDING_INDEX].value = true;
			type->CanStore[OilCost] = 1;
		}
		refineryType.DefaultStat.ImproveIncomes[OilCost] = 125;
		walker.TileWidth = 1;
		walker.TileHeight = 1;
		walker.BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		walker.MoveType = EMovement::Naval;
		walker.MovementMask = NavalMask;
		third.Player = &player;
		third.Type = &ship;
	}

	/// A 3x3 building on the map; a patch can be entered, every other building cannot.
	void Stand(CUnit &building, CUnitType &type, const Vec2i &pos)
	{
		building.Type = &type;
		building.Player = &player;
		building.tilePos = pos;
		building.Refs = 1;
		building.Orders.clear();
		building.Orders.push_back(COrder::NewActionStill());
		const tile_flags flag = &type == &patchType ? MapFieldNoBuilding : MapFieldBuilding;
		for (int y = pos.y; y != pos.y + 3; ++y) {
			for (int x = pos.x; x != pos.x + 3; ++x) {
				Map.Field(x, y)->Flags |= flag;
				Map.Field(x, y)->UnitCache.push_back(&building);
			}
		}
	}

	/// The shore of the measured Shipyard (top-left 48,93): coast under its south and east edge, land beyond.
	void YardShore()
	{
		map.Fill({51, 94}, {60, 100}, MapFieldLandAllowed);
		map.Fill({49, 96}, {60, 100}, MapFieldLandAllowed);
		for (const Vec2i &pos : {Vec2i(50, 94), Vec2i(48, 95), Vec2i(49, 95), Vec2i(50, 95), Vec2i(51, 93), Vec2i(48, 96)}) {
			Map.Field(pos)->Flags |= MapFieldCoastAllowed;
		}
	}

	/// Give the tanker an order and take path elements until the pathfinder stops it.
	WalkResult Go(const Vec2i &from, std::unique_ptr<COrder> order)
	{
		WalkResult res;
		unit.tilePos = from;
		unit.Orders.push_back(std::move(order));
		unit.pathFinderData->output.Length = 0;
		for (int i = 0; i != 200; ++i) {
			const auto [d, dir] = NextPathElement(unit);
			res.end = d;
			if (d == PF_WAIT && unit.pathFinderData->output.Length <= 0) {
				continue;
			}
			if (d <= 0) {
				break;
			}
			res.steps += (dir.x && dir.y) ? 'D' : 'S';
			unit.tilePos += dir;
			res.onGrid = res.onGrid && unit.tilePos.x % 2 == 0 && unit.tilePos.y % 2 == 0;
		}
		unit.Orders.clear();
		return res;
	}
	WalkResult Harvest(const Vec2i &from, CUnit &mine) { return Go(from, COrder::NewActionResource(unit, mine)); }
	WalkResult Return(const Vec2i &from, CUnit &depot) { return Go(from, COrder::NewActionReturnGoods(unit, &depot)); }
	WalkResult Build(const Vec2i &from, const Vec2i &pos) { return Go(from, COrder::NewActionBuild(unit, pos, platformType)); }

	/// Tiles between the unit and the nearest tile of a 3x3 building
	int Distance(const CUnit &building) const
	{
		const int dx = std::max({building.tilePos.x - unit.tilePos.x, unit.tilePos.x - building.tilePos.x - 2, 0});
		const int dy = std::max({building.tilePos.y - unit.tilePos.y, unit.tilePos.y - building.tilePos.y - 2, 0});
		return std::max(dx, dy);
	}

	CUnitType patchType;
	CUnitType platformType;
	CUnitType yardType;
	CUnitType refineryType;
	CUnitType hallType;
	CUnitType walker; /// a ship with step 1
	CUnit building;
	CUnit third;
};
} // namespace

TEST_CASE("Tanker: it rests on the grid like every ship")
{
	OilFixture f;
	struct Row { Vec2i start, click, rest; const char *steps; };
	// the clean rows of the measured table: start, clicked tile, rest tile, the way
	const Row rows[] = {
		{{50, 92}, {56, 84}, {56, 84}, "DDSD"},
		{{56, 84}, {51, 79}, {52, 80}, "DD"},
		{{50, 92}, {44, 84}, {44, 84}, "DDSD"},
		{{44, 84}, {41, 81}, {42, 82}, "D"},
		{{42, 82}, {47, 82}, {46, 82}, "SS"},
		{{46, 82}, {46, 87}, {46, 86}, "SS"},
		{{46, 86}, {53, 89}, {52, 88}, "SDS"},
	};
	for (const Row &row : rows) {
		CAPTURE(row.click.x);
		CAPTURE(row.click.y);
		f.unit.tilePos = row.start;
		const WalkResult walk = Walk(f.unit, row.click);
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == row.rest);
		CHECK(walk.steps == row.steps);
		CHECK(walk.onGrid);
	}
}

TEST_CASE("Tanker: building a platform, it sails onto the centre tile of the patch")
{
	OilFixture f;

	SUBCASE("patch 49,73")
	{
		f.Stand(f.building, f.patchType, {49, 73});
		// the patch does not close its tiles; the one grid tile of the 3x3 is its centre
		CHECK(UnitTypeCanBeAt(f.ship, {50, 74}));
		WalkResult walk = f.Build({48, 78}, {49, 73});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(50, 74));
		CHECK(walk.steps.size() == 2);
		MESSAGE("from 48,78 onto the centre by " << walk.steps);
		CHECK(walk.onGrid);
		walk = f.Build({50, 76}, {49, 73});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(50, 74));
		CHECK(walk.steps == "S");
	}
	SUBCASE("patch 9,91")
	{
		f.Stand(f.building, f.patchType, {9, 91});
		const WalkResult walk = f.Build({12, 90}, {9, 91});
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(10, 92));
		CHECK(walk.steps == "D");
	}
	SUBCASE("the finished platform closes its tiles")
	{
		f.Stand(f.building, f.platformType, {49, 73});
		CHECK_FALSE(UnitTypeCanBeAt(f.ship, {50, 74}));
	}
}

TEST_CASE("Tanker: into the platform and out of it")
{
	OilFixture f;
	f.Stand(f.building, f.platformType, {49, 73});

	SUBCASE("from the south along x = 48: in from 48,76")
	{
		const WalkResult walk = f.Harvest({48, 90}, f.building);
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(48, 76));
		CHECK(walk.onGrid);
	}
	SUBCASE("from the south-west: in from 48,76")
	{
		const WalkResult walk = f.Harvest({46, 78}, f.building);
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(48, 76));
	}
	SUBCASE("from the east along row 76: in after one step, from a tile that touches the corner")
	{
		const WalkResult walk = f.Harvest({54, 76}, f.building);
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos.x == 52);
		CHECK(walk.steps.size() == 1);
		CHECK(f.Distance(f.building) == 1);
		MESSAGE("from the east it goes in from " << f.unit.tilePos.x << "," << f.unit.tilePos.y << " by " << walk.steps);
	}
	SUBCASE("the tile it goes in from touches the platform, from every side")
	{
		for (const Vec2i &from : {Vec2i(40, 60), Vec2i(50, 60), Vec2i(60, 60), Vec2i(60, 74), Vec2i(60, 90), Vec2i(40, 74)}) {
			CAPTURE(from.x);
			CAPTURE(from.y);
			const WalkResult walk = f.Harvest(from, f.building);
			CHECK(walk.end == PF_REACHED);
			CHECK(walk.onGrid);
			CHECK(f.Distance(f.building) == 1);
		}
	}
	SUBCASE("out on the tile west of the middle row, whatever side it went in from")
	{
		CHECK(FindGridExitTile(f.unit, f.building, EGridExit::Resource) == Vec2i(48, 74));
	}
	SUBCASE("that tile taken: no tile of its own, the search for any unit follows")
	{
		f.map.Rest(f.other, {48, 74}, MapFieldSeaUnit);
		CHECK_FALSE(FindGridExitTile(f.unit, f.building, EGridExit::Resource).has_value());
	}
	SUBCASE("the second platform: 8,92")
	{
		CUnit second;
		f.Stand(second, f.platformType, {9, 91});
		CHECK(FindGridExitTile(f.unit, second, EGridExit::Resource) == Vec2i(8, 92));
	}
	SUBCASE("a unit with step 1 and a building that is no resource have no such tile")
	{
		f.unit.Type = &f.walker;
		CHECK_FALSE(FindGridExitTile(f.unit, f.building, EGridExit::Resource).has_value());
		f.unit.Type = &f.ship;
		CUnit yard;
		f.Stand(yard, f.yardType, {8, 21});
		CHECK_FALSE(FindGridExitTile(f.unit, yard, EGridExit::Resource).has_value());
	}
}

TEST_CASE("Tanker: into the Shipyard and out of it")
{
	OilFixture f;
	f.YardShore();
	f.Stand(f.building, f.yardType, {48, 93});

	SUBCASE("from the north along x = 48: in from 48,92, the touching tile")
	{
		const WalkResult walk = f.Return({48, 74}, f.building);
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(48, 92));
		CHECK(walk.onGrid);
	}
	SUBCASE("from the north-east: in from 52,92, two tiles from the building")
	{
		const WalkResult walk = f.Return({54, 90}, f.building);
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(52, 92));
		CHECK(walk.steps == "D");
		CHECK(f.Distance(f.building) == 2);
	}
	SUBCASE("from the west: in from 46,92, two tiles from the building")
	{
		const WalkResult walk = f.Return({42, 94}, f.building);
		CHECK(walk.end == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(46, 92));
		CHECK(walk.steps == "DS");
		CHECK(walk.onGrid);
		CHECK(f.Distance(f.building) == 2);
	}
	SUBCASE("the tiles above the building taken: it ends on a free grid tile within two")
	{
		f.map.Rest(f.other, {48, 92}, MapFieldSeaUnit);
		f.map.Rest(f.third, {50, 92}, MapFieldSeaUnit);
		const WalkResult walk = f.Return({48, 74}, f.building);
		CHECK(walk.end == PF_REACHED);
		CHECK(walk.onGrid);
		CHECK(f.Distance(f.building) == 2);
		MESSAGE("with 48,92 and 50,92 taken it goes in from " << f.unit.tilePos.x << "," << f.unit.tilePos.y);
	}
	SUBCASE("out above the top-left tile; that taken, above the top-right tile; both taken, no tile of its own")
	{
		CHECK(FindGridExitTile(f.unit, f.building, EGridExit::Depot) == Vec2i(48, 92));
		f.map.Rest(f.other, {48, 92}, MapFieldSeaUnit);
		CHECK(FindGridExitTile(f.unit, f.building, EGridExit::Depot) == Vec2i(50, 92));
		f.map.Rest(f.third, {50, 92}, MapFieldSeaUnit);
		CHECK_FALSE(FindGridExitTile(f.unit, f.building, EGridExit::Depot).has_value());
	}
	SUBCASE("a new ship: above the top-right tile; that taken, above the top-left tile; both taken, the search")
	{
		CHECK(FindGridExitTile(f.unit, f.building, EGridExit::Trained) == Vec2i(50, 92));
		f.map.Rest(f.other, {50, 92}, MapFieldSeaUnit);
		CHECK(FindGridExitTile(f.unit, f.building, EGridExit::Trained) == Vec2i(48, 92));
		f.map.Rest(f.third, {48, 92}, MapFieldSeaUnit);
		CHECK_FALSE(FindGridExitTile(f.unit, f.building, EGridExit::Trained).has_value());
		// the search that follows keeps the new ship on the grid
		Vec2i pos;
		FindNearestDrop(f.ship, {49, 92}, pos, LookingW);
		CHECK(pos.x % 2 == 0);
		CHECK(pos.y % 2 == 0);
		CHECK(UnitTypeCanBeAt(f.ship, pos));
	}
	SUBCASE("a flyer has the same tiles at a shore building, none at a land building")
	{
		f.unit.Type = &f.flyer;
		CHECK(FindGridExitTile(f.unit, f.building, EGridExit::Trained) == Vec2i(50, 92));
		CUnit hall;
		f.Stand(hall, f.hallType, {20, 21});
		CHECK_FALSE(FindGridExitTile(f.unit, hall, EGridExit::Trained).has_value());
		CHECK_FALSE(FindGridExitTile(f.unit, hall, EGridExit::Depot).has_value());
	}
	SUBCASE("a depot that refines, a building off these tiles' grid, a unit with step 1: no tile of its own")
	{
		CUnit refinery;
		f.Stand(refinery, f.refineryType, {20, 21});
		CHECK_FALSE(FindGridExitTile(f.unit, refinery, EGridExit::Depot).has_value());
		CUnit odd;
		f.Stand(odd, f.yardType, {31, 41});
		CHECK_FALSE(FindGridExitTile(f.unit, odd, EGridExit::Depot).has_value());
		CHECK_FALSE(FindGridExitTile(f.unit, odd, EGridExit::Trained).has_value());
		f.unit.Type = &f.walker;
		CHECK_FALSE(FindGridExitTile(f.unit, f.building, EGridExit::Depot).has_value());
		CHECK_FALSE(FindGridExitTile(f.unit, f.building, EGridExit::Trained).has_value());
	}
}

// Attack range: the larger axis gap to the nearest tile of the target.

#include "upgrade_structs.h"

namespace
{
struct RangeRow
{
	Vec2i attacker; /// the attacker's tile
	Vec2i target;   /// top-left tile of the target
	int size;       /// the target's footprint is size x size
	int range;      /// the attacker's Range
	bool fired;
};

// The measured rows: the tile an attack was made from (fired) and the last tile passed or stood
// on without one (not fired).
const RangeRow RangeRows[] = {
	// Range 4 against a one-tile unit on 17,107
	{{21, 111}, {17, 107}, 1, 4, true}, {{21, 111}, {17, 107}, 1, 4, true}, {{21, 110}, {17, 107}, 1, 4, true},
	{{20, 111}, {17, 107}, 1, 4, true}, {{21, 109}, {17, 107}, 1, 4, true}, {{21, 110}, {17, 107}, 1, 4, true},
	{{21, 107}, {17, 107}, 1, 4, true}, {{21, 107}, {17, 107}, 1, 4, true}, {{21, 103}, {17, 107}, 1, 4, true},
	{{21, 108}, {17, 107}, 1, 4, true}, {{21, 104}, {17, 107}, 1, 4, true}, {{20, 111}, {17, 107}, 1, 4, true},
	{{13, 107}, {17, 107}, 1, 4, true}, {{13, 111}, {17, 107}, 1, 4, true},
	{{22, 112}, {17, 107}, 1, 4, false}, {{22, 110}, {17, 107}, 1, 4, false}, {{22, 111}, {17, 107}, 1, 4, false},
	{{22, 107}, {17, 107}, 1, 4, false}, {{22, 107}, {17, 107}, 1, 4, false}, {{22, 102}, {17, 107}, 1, 4, false},
	{{22, 107}, {17, 107}, 1, 4, false}, {{22, 103}, {17, 107}, 1, 4, false}, {{21, 112}, {17, 107}, 1, 4, false},
	{{12, 107}, {17, 107}, 1, 4, false},
	// Range 4 against buildings: 4x4 on 14,101; 2x2 on 19,98 and on 24,101; 3x3 on 27,104
	{{21, 102}, {14, 101}, 4, 4, true}, {{21, 103}, {14, 101}, 4, 4, true}, {{21, 108}, {14, 101}, 4, 4, true},
	{{21, 108}, {14, 101}, 4, 4, true}, {{21, 108}, {14, 101}, 4, 4, true}, {{20, 103}, {19, 98}, 2, 4, true},
	{{20, 103}, {19, 98}, 2, 4, true}, {{24, 103}, {19, 98}, 2, 4, true}, {{24, 98}, {19, 98}, 2, 4, true},
	{{20, 103}, {19, 98}, 2, 4, true}, {{23, 110}, {27, 104}, 3, 4, true}, {{20, 97}, {24, 101}, 2, 4, true},
	{{22, 102}, {14, 101}, 4, 4, false}, {{22, 109}, {14, 101}, 4, 4, false}, {{22, 109}, {14, 101}, 4, 4, false},
	{{20, 104}, {19, 98}, 2, 4, false}, {{21, 104}, {19, 98}, 2, 4, false}, {{19, 96}, {24, 101}, 2, 4, false},
	// Range 8 against the 2x2 on 19,98 and a 3x3 on 10,101
	{{20, 107}, {19, 98}, 2, 8, true}, {{11, 107}, {19, 98}, 2, 8, true}, {{11, 104}, {19, 98}, 2, 8, true},
	{{11, 104}, {10, 101}, 3, 8, true},
	{{20, 108}, {19, 98}, 2, 8, false}, {{10, 108}, {19, 98}, 2, 8, false},
	// Range 1: a unit on the diagonal, a 3x3 on 8,105
	{{12, 105}, {13, 104}, 1, 1, true}, {{11, 106}, {8, 105}, 3, 1, true}, {{12, 105}, {8, 105}, 3, 1, false},
	// Range 4, standing with no order, against a 2x2 on 32,116
	{{28, 115}, {32, 116}, 2, 4, true}, {{27, 116}, {32, 116}, 2, 4, false},
	// Range 4 from a ship: one-tile units, a 3x3 on 9,91
	{{44, 90}, {42, 94}, 1, 4, true}, {{14, 120}, {12, 124}, 1, 4, true}, {{12, 114}, {10, 118}, 1, 4, true},
	{{10, 114}, {14, 118}, 1, 4, true}, {{8, 96}, {9, 91}, 3, 4, true}, {{22, 94}, {25, 96}, 1, 4, true},
	{{44, 88}, {42, 94}, 1, 4, false}, {{16, 118}, {12, 124}, 1, 4, false}, {{14, 112}, {10, 118}, 1, 4, false},
	{{8, 112}, {14, 118}, 1, 4, false}, {{8, 98}, {9, 91}, 3, 4, false}, {{20, 94}, {25, 96}, 1, 4, false},
};

struct RangeFixture
{
	RangeFixture() : map(64, 64)
	{
		player.Index = 0;
		player.Type = PlayerTypes::PlayerPerson;
		for (CUnitType *type : {&one, &two, &three, &four}) {
			type->MovementMask = MapFieldUnpassable;
			type->BoolFlag.resize(UnitTypeVar.GetNumberBoolFlag());
		}
		Size(one, 1);
		Size(two, 2);
		Size(three, 3);
		Size(four, 4);
		stats.Variables.resize(ATTACKRANGE_INDEX + 1);
		for (CUnit *u : {&unit, &target}) {
			u->Player = &player;
			u->Type = &one;
			u->Stats = &stats;
		}
	}
	static void Size(CUnitType &type, int size)
	{
		type.TileWidth = size;
		type.TileHeight = size;
	}
	void Range(int min, int max)
	{
		one.MinAttackRange = min;
		stats.Variables[ATTACKRANGE_INDEX].Max = max;
	}
	SyntheticMap map;
	CPlayer player;
	CUnitType one;
	CUnitType two;
	CUnitType three;
	CUnitType four;
	CUnitStats stats;
	CUnit unit;
	CUnit target;
};
} // namespace

TEST_CASE("Attack range: every measured row")
{
	CHECK(std::size(RangeRows) == 65);
	int fired = 0;
	int mapDistanceFits = 0;
	CUnitType one;
	one.TileWidth = 1;
	one.TileHeight = 1;
	for (const RangeRow &row : RangeRows) {
		CAPTURE(row.attacker.x);
		CAPTURE(row.attacker.y);
		CAPTURE(row.target.x);
		CAPTURE(row.target.y);
		const Vec2i size(row.size, row.size);
		CHECK((ChebyshevEdgeDistance(row.attacker, {1, 1}, row.target, size) <= row.range) == row.fired);
		CHECK(ChebyshevEdgeDistance(row.target, size, row.attacker, {1, 1})
		      == ChebyshevEdgeDistance(row.attacker, {1, 1}, row.target, size));
		CUnitType type;
		type.TileWidth = row.size;
		type.TileHeight = row.size;
		CHECK((AttackDistanceBetweenTypes(one, row.attacker, type, row.target) <= row.range) == row.fired);
		fired += row.fired;
		mapDistanceFits += (MapDistanceBetweenTypes(one, row.attacker, type, row.target) <= row.range) == row.fired;
	}
	CHECK(fired == 39);
	// the map distance, which every other rule keeps, answers 47 of them
	CHECK(mapDistanceFits == 47);
}

TEST_CASE("Attack range: the edge distance of two footprints")
{
	CHECK(ChebyshevEdgeDistance({5, 5}, {1, 1}, {5, 5}, {1, 1}) == 0);
	CHECK(ChebyshevEdgeDistance({5, 5}, {2, 2}, {6, 6}, {3, 3}) == 0);
	CHECK(ChebyshevEdgeDistance({5, 5}, {2, 2}, {7, 7}, {1, 1}) == 1);
	CHECK(ChebyshevEdgeDistance({5, 5}, {2, 2}, {1, 6}, {1, 1}) == 4);
	CHECK(ChebyshevEdgeDistance({5, 5}, {2, 2}, {10, 0}, {2, 2}) == 4);
	CHECK(ChebyshevEdgeDistance({10, 0}, {2, 2}, {5, 5}, {2, 2}) == 4);
	CHECK(ChebyshevEdgeDistance({0, 0}, {1, 1}, {3, 9}, {4, 4}) == 9);
}

TEST_CASE("Attack range: the map distance and what keeps it are as before")
{
	RangeFixture f;
	// offset of a one-tile unit from a one-tile unit -> the value before the attack distance existed
	const int before[][3] = {{0, 0, 0}, {1, 0, 1}, {1, 1, 1}, {2, 1, 2}, {2, 2, 2}, {3, 2, 3}, {3, 3, 4},
	                         {4, 0, 4}, {4, 1, 4}, {4, 2, 4}, {4, 3, 5}, {3, 4, 5}, {4, 4, 5}, {5, 0, 5},
	                         {5, 5, 7}, {6, 6, 8}, {8, 5, 9}, {8, 8, 11}, {0, 9, 9}, {9, 9, 12}};
	for (const auto &row : before) {
		CAPTURE(row[0]);
		CAPTURE(row[1]);
		const Vec2i a(20, 20);
		const Vec2i b(20 + row[0], 20 - row[1]);
		CHECK(MapDistanceBetweenTypes(f.one, a, f.one, b) == row[2]);
		CHECK(MapDistanceBetweenTypes(f.one, b, f.one, a) == row[2]);
		CHECK(Distance(a, b) == row[2]);
		f.unit.tilePos = a;
		f.target.tilePos = b;
		CHECK(f.unit.MapDistanceTo(f.target) == row[2]);
		CHECK(f.unit.MapDistanceTo(b) == row[2]);
		// from the far side of a 4x4 and of a 2x2 footprint
		CHECK(MapDistanceBetweenTypes(f.four, Vec2i(17, 20), f.one, b) == row[2]);
		CHECK(MapDistanceBetweenTypes(f.one, b, f.two, Vec2i(19, 20)) == row[2]);
		f.unit.Type = &f.four;
		f.unit.tilePos = {17, 20};
		CHECK(f.unit.MapDistanceTo(b) == row[2]);
		f.unit.Type = &f.one;
	}

	f.unit.tilePos = {20, 20};
	f.target.tilePos = {24, 24};
	SUBCASE("reaction range")
	{
		f.one.ReactRangePerson = 4;
		CHECK_FALSE(InReactRange(f.unit, f.target));
		f.one.ReactRangePerson = 5;
		CHECK(InReactRange(f.unit, f.target));
	}
	SUBCASE("an attack on a tile")
	{
		f.Range(0, 4);
		CHECK_FALSE(InAttackRange(f.unit, Vec2i(24, 24)));
		CHECK_FALSE(InAttackRange(f.unit, Vec2i(24, 23)));
		CHECK(InAttackRange(f.unit, Vec2i(24, 22)));
		f.Range(0, 5);
		CHECK(InAttackRange(f.unit, Vec2i(24, 24)));
	}
	SUBCASE("the minimal range")
	{
		f.Range(2, 4);
		f.target.tilePos = {21, 21};
		CHECK_FALSE(InAttackRange(f.unit, f.target));
		f.target.tilePos = {22, 21};
		CHECK(InAttackRange(f.unit, f.target));
		f.Range(6, 8);
		f.target.tilePos = {24, 24}; // map distance 5
		CHECK_FALSE(InAttackRange(f.unit, f.target));
		f.target.tilePos = {25, 24}; // map distance 6
		CHECK(InAttackRange(f.unit, f.target));
	}
}

TEST_CASE("Attack range: a unit against a unit or a building")
{
	RangeFixture f;
	f.Range(0, 4);
	f.unit.tilePos = {20, 20};
	SUBCASE("a one-tile target")
	{
		f.target.tilePos = {24, 24};
		CHECK(InAttackRange(f.unit, f.target));
		f.target.tilePos = {16, 23};
		CHECK(InAttackRange(f.unit, f.target));
		f.target.tilePos = {25, 20};
		CHECK_FALSE(InAttackRange(f.unit, f.target));
		f.target.tilePos = {24, 25};
		CHECK_FALSE(InAttackRange(f.unit, f.target));
	}
	SUBCASE("a building: its nearest tile")
	{
		f.target.Type = &f.four;
		f.target.tilePos = {13, 13}; // nearest tile 16,16
		CHECK(InAttackRange(f.unit, f.target));
		f.target.tilePos = {12, 13};
		CHECK_FALSE(InAttackRange(f.unit, f.target));
		f.target.tilePos = {24, 24};
		CHECK(InAttackRange(f.unit, f.target));
	}
	SUBCASE("ranges 1 and 2 hold the tiles they held")
	{
		for (int range = 1; range <= 2; ++range) {
			f.Range(0, range);
			for (int dy = -4; dy <= 4; ++dy) {
				for (int dx = -4; dx <= 4; ++dx) {
					f.target.tilePos = {short(20 + dx), short(20 + dy)};
					CHECK(InAttackRange(f.unit, f.target) == (f.unit.MapDistanceTo(f.target) <= range));
				}
			}
		}
	}
}

TEST_CASE("Attack range: the path ends where the attack is possible")
{
	RangeFixture f;
	char path[PathFinderOutput::MAX_PATH_LENGTH];

	SUBCASE("the goal area of an attack is the tiles in attack range, every other the map distance")
	{
		for (CUnitType *goalType : {&f.one, &f.three}) {
			// a minimal range is tried against the one-tile goal only: beside a larger goal the
			// short way of the pathfinder counts it from the goal's top-left tile
			for (int min = 0; min <= (goalType == &f.one ? 2 : 0); min += 2) {
				f.Range(min, 4);
				f.target.Type = goalType;
				f.target.tilePos = {30, 30};
				const int size = goalType->TileWidth;
				for (int y = 22; y <= 40; ++y) {
					for (int x = 22; x <= 40; ++x) {
						f.unit.tilePos = {short(x), short(y)};
						if (f.unit.MapDistanceTo(f.target) == 0) {
							continue;
						}
						CAPTURE(x);
						CAPTURE(y);
						CAPTURE(size);
						CAPTURE(min);
						const int square = AStarFindPath(f.unit.tilePos, f.target.tilePos, size, size, 1, 1, min, 4,
						                                 path, sizeof(path), f.unit, true);
						CHECK((square == PF_REACHED) == InAttackRange(f.unit, f.target));
						const int circle = AStarFindPath(f.unit.tilePos, f.target.tilePos, size, size, 1, 1, min, 4,
						                                 path, sizeof(path), f.unit);
						const int d = f.unit.MapDistanceTo(f.target);
						CHECK((circle == PF_REACHED) == (min <= d && d <= 4));
					}
				}
			}
		}
	}
	SUBCASE("from the diagonal: two steps to (4,4), three with the map distance")
	{
		f.unit.tilePos = {36, 36};
		CHECK(AStarFindPath(f.unit.tilePos, {30, 30}, 1, 1, 1, 1, 0, 4, path, sizeof(path), f.unit, true) == 2);
		CHECK(AStarFindPath(f.unit.tilePos, {30, 30}, 1, 1, 1, 1, 0, 4, path, sizeof(path), f.unit) == 3);
		f.target.tilePos = {30, 30};
		CHECK(CalcPathLengthToUnit(f.unit, f.target, 0, 4, true) == 2);
		CHECK(CalcPathLengthToUnit(f.unit, f.target, 0, 4) == 3);
		CHECK(UnitReachable(f.unit, f.target, 4, false, true) == 2);
		CHECK(UnitReachable(f.unit, f.target, 4, false) == 3);
	}
	SUBCASE("the shape belongs to the call that sets the range")
	{
		PathFinderInput input;
		input.SetUnit(f.unit);
		input.SetMaxRange(4, true);
		CHECK(input.IsSquareRange());
		input.PathRecalculated();
		input.SetMaxRange(4, true);
		CHECK_FALSE(input.IsRecalculateNeeded());
		input.SetMaxRange(4);
		CHECK_FALSE(input.IsSquareRange());
		CHECK(input.IsRecalculateNeeded());
	}
	SUBCASE("an attack order on a unit walks to the first tile in attack range; other orders as before")
	{
		f.Range(0, 4);
		f.target.tilePos = {30, 30};
		const auto walk = [&](std::unique_ptr<COrder> order) {
			f.unit.tilePos = {37, 37};
			f.unit.Orders.push_back(std::move(order));
			f.unit.pathFinderData->output.Length = 0;
			int end = PF_WAIT;
			for (int i = 0; i != 40; ++i) {
				const auto [d, dir] = NextPathElement(f.unit);
				end = d;
				if (d == PF_WAIT && f.unit.pathFinderData->output.Length <= 0) {
					continue;
				}
				if (d <= 0) {
					break;
				}
				f.unit.tilePos += dir;
			}
			f.unit.Orders.clear();
			return end;
		};
		CHECK(walk(COrder::NewActionAttack(f.unit, f.target)) == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(34, 34));
		CHECK(f.unit.pathFinderData->input.IsSquareRange());
		CHECK(InAttackRange(f.unit, f.target));

		CHECK(walk(COrder::NewActionAttackGround(f.unit, Vec2i(30, 30))) == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(33, 33));
		CHECK_FALSE(f.unit.pathFinderData->input.IsSquareRange());
		CHECK(InAttackRange(f.unit, Vec2i(30, 30)));

		CHECK(walk(COrder::NewActionMove(Vec2i(30, 30))) == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(30, 30));
		CHECK_FALSE(f.unit.pathFinderData->input.IsSquareRange());
	}
}

TEST_CASE("Tanker: a depot that no tile of its grid touches is still found")
{
	OilFixture f;

	SUBCASE("the measured Shipyard (top-left 48,93): reached within one tile")
	{
		f.YardShore();
		f.Stand(f.building, f.yardType, {48, 93});
		f.unit.tilePos = {48, 74};
		CHECK(UnitReachable(f.unit, f.building, 1, false) > 0);
		CHECK(UnitReachable(f.unit, f.building, std::max<int>(1, f.unit.Type->GridStep), false) > 0);
	}
	SUBCASE("a depot on an even, even tile: no grid tile touches it, the way in is from two tiles")
	{
		f.Stand(f.building, f.yardType, {48, 94});
		f.unit.tilePos = {48, 74};
		// the range of one tile finds no way: with it the depot search passed such a depot over
		CHECK(UnitReachable(f.unit, f.building, 1, false) == 0);
		// the range the return order itself uses does
		CHECK(UnitReachable(f.unit, f.building, std::max<int>(1, f.unit.Type->GridStep), false) > 0);
		const WalkResult walk = f.Return({48, 74}, f.building);
		CHECK(walk.end == PF_REACHED);
		CHECK(walk.onGrid);
		CHECK(f.Distance(f.building) == 2);
	}
	SUBCASE("a unit with step 1: the range stays one tile")
	{
		CHECK(std::max<int>(1, f.walker.GridStep) == 1);
	}
}

// Minimal impact distance: a missile comes down no nearer than it, along the same line.

#include "missile.h"

namespace
{
struct ImpactRow
{
	Vec2i offset;  /// attacker's tile -> the ordered tile
	Vec2i impact2; /// attacker's tile -> the impact, in half tiles
};

// The measured attack-ground rows of the Ballista and the Catapult (minimal impact distance 3).
const ImpactRow ImpactRows[] = {
	// Ballista, nearer than 3
	{{1, 0}, {6, 0}}, {{2, 0}, {6, 0}}, {{1, 1}, {6, 6}}, {{2, 2}, {6, 6}}, {{0, 1}, {0, 6}}, {{0, 2}, {0, 6}},
	{{-1, 0}, {-6, 0}}, {{2, 1}, {6, 3}}, {{2, 1}, {6, 3}}, {{1, 2}, {3, 6}}, {{-2, -1}, {-6, -3}},
	// Ballista, 3 and more
	{{3, 0}, {6, 0}}, {{4, 0}, {8, 0}}, {{5, 0}, {10, 0}}, {{3, 3}, {6, 6}}, {{3, 1}, {6, 2}}, {{3, 2}, {6, 4}},
	{{4, 2}, {8, 4}},
	// Catapult, nearer than 3
	{{1, 0}, {6, 0}}, {{1, 0}, {6, 0}}, {{2, 0}, {6, 0}}, {{1, 1}, {6, 6}}, {{2, 2}, {6, 6}}, {{2, 1}, {6, 3}},
	{{0, 1}, {0, 6}}, {{0, 2}, {0, 6}}, {{-1, 0}, {-6, 0}}, {{-1, 0}, {-6, 0}}, {{-2, 0}, {-6, 0}},
	{{0, -1}, {0, -6}}, {{-2, -1}, {-6, -3}},
	// Catapult, 3 and more
	{{3, 0}, {6, 0}}, {{4, 0}, {8, 0}}, {{3, 3}, {6, 6}}, {{6, 0}, {12, 0}}, {{8, 8}, {16, 16}},
};

struct ImpactFixture : RangeFixture
{
	ImpactFixture()
	{
		unit.tilePos = {30, 30};
	}
	/// The impact as an offset from the centre of the firing unit's tile, in half tiles.
	Vec2i Impact2(const CUnit *goal, const Vec2i &goalPos)
	{
		const PixelPos point = MissileImpactPoint(unit, goal, goalPos, overshot);
		const PixelPos diff = point - Map.TilePosToMapPixelPos_Center(unit.tilePos);

		CHECK(diff.x % (PixelTileSize.x / 2) == 0);
		CHECK(diff.y % (PixelTileSize.y / 2) == 0);
		return Vec2i(diff.x / (PixelTileSize.x / 2), diff.y / (PixelTileSize.y / 2));
	}
	/// The point a missile flew to before there was a minimal impact distance.
	static PixelPos CentreOf(const CUnit &goal)
	{
		PixelPos point = Map.TilePosToMapPixelPos_Center(goal.tilePos + goal.Type->GetHalfTileSize());

		if (goal.Type->TileWidth % 2 == 0) {
			point -= PixelTileSize / 2;
		}
		return point;
	}
	bool overshot = false;
};
} // namespace

TEST_CASE("Minimal impact distance: every measured row")
{
	ImpactFixture f;
	f.Range(0, 8);
	f.one.MinImpactRange = 3;

	SUBCASE("attack ground")
	{
		CHECK(std::size(ImpactRows) == 36);
		int pushed = 0;
		for (const ImpactRow &row : ImpactRows) {
			CAPTURE(row.offset.x);
			CAPTURE(row.offset.y);
			CHECK(f.Impact2(nullptr, f.unit.tilePos + row.offset) == row.impact2);
			CHECK(f.overshot == (std::max(std::abs(row.offset.x), std::abs(row.offset.y)) < 3));
			pushed += f.overshot;
		}
		CHECK(pushed == 24);
	}
	SUBCASE("a unit: the missile flies over a near one and has no goal unit then")
	{
		const ImpactRow units[] = {
			{{2, -2}, {6, -6}}, {{1, 0}, {6, 0}}, {{3, 0}, {6, 0}}, {{1, -1}, {6, -6}}, {{2, 0}, {6, 0}},
			{{1, 1}, {6, 6}},
		};
		for (const ImpactRow &row : units) {
			CAPTURE(row.offset.x);
			CAPTURE(row.offset.y);
			f.target.tilePos = f.unit.tilePos + row.offset;
			CHECK(f.Impact2(&f.target, f.target.tilePos) == row.impact2);
			CHECK(f.overshot == (row.offset != Vec2i(3, 0)));
		}
	}
	SUBCASE("a 2x2 building beside it: over it, on the line to its centre")
	{
		// the unit on 99,92, the building on 99..100 x 90..91, the impact on 100,89
		f.target.Type = &f.two;
		f.target.tilePos = f.unit.tilePos + Vec2i(0, -2);
		CHECK(f.Impact2(&f.target, f.target.tilePos) == Vec2i(2, -6));
		CHECK(f.overshot);
		const PixelPos point = MissileImpactPoint(f.unit, &f.target, f.target.tilePos, f.overshot);
		CHECK(Map.MapPixelPosToTilePos(point) == f.unit.tilePos + Vec2i(1, -3));
	}
	SUBCASE("the distance is the type's number")
	{
		f.one.MinImpactRange = 5;
		CHECK(f.Impact2(nullptr, f.unit.tilePos + Vec2i(2, 1)) == Vec2i(10, 5));
		CHECK(f.Impact2(nullptr, f.unit.tilePos + Vec2i(-4, 4)) == Vec2i(-10, 10));
		CHECK(f.Impact2(nullptr, f.unit.tilePos + Vec2i(5, -1)) == Vec2i(10, -2));
		CHECK_FALSE(f.overshot);
		// no direction: the unit's own tile stays the point
		CHECK(f.Impact2(nullptr, f.unit.tilePos) == Vec2i(0, 0));
		CHECK_FALSE(f.overshot);
	}
}

TEST_CASE("Minimal impact distance: a type without it is as before")
{
	ImpactFixture f;
	f.Range(2, 8);

	CHECK(f.one.MinImpactRange == 0);
	for (CUnitType *goalType : {&f.one, &f.two, &f.three, &f.four}) {
		f.target.Type = goalType;
		for (int dy = -4; dy <= 4; ++dy) {
			for (int dx = -4; dx <= 4; ++dx) {
				CAPTURE(dx);
				CAPTURE(dy);
				const Vec2i pos = f.unit.tilePos + Vec2i(dx, dy);
				f.target.tilePos = pos;
				f.overshot = true;
				CHECK(MissileImpactPoint(f.unit, &f.target, pos, f.overshot) == ImpactFixture::CentreOf(f.target));
				CHECK_FALSE(f.overshot);
				CHECK(MissileImpactPoint(f.unit, nullptr, pos, f.overshot) == Map.TilePosToMapPixelPos_Center(pos));
				CHECK_FALSE(f.overshot);
			}
		}
	}
	// the minimal attack range of such a type still refuses a near target, by the map distance
	f.target.Type = &f.one;
	f.target.tilePos = f.unit.tilePos + Vec2i(1, 1);
	CHECK_FALSE(InAttackRange(f.unit, f.target));
	CHECK_FALSE(InAttackRange(f.unit, f.unit.tilePos + Vec2i(1, 0)));
	f.target.tilePos = f.unit.tilePos + Vec2i(2, 0);
	CHECK(InAttackRange(f.unit, f.target));
	// the point between two tiles, by itself
	const PixelPos tile(32, 32);
	CHECK(PushOutToRange({16, 16}, {80, 48}, 0, tile) == PixelPos(80, 48));
	CHECK(PushOutToRange({16, 16}, {80, 48}, 3, tile) == PixelPos(112, 64));
	CHECK(PushOutToRange({16, 16}, {-48, -16}, 3, tile) == PixelPos(-80, -32));
	CHECK(PushOutToRange({16, 16}, {112, 16}, 3, tile) == PixelPos(112, 16));
	CHECK(PushOutToRange({16, 16}, {16, 16}, 3, tile) == PixelPos(16, 16));
}

TEST_CASE("Minimal impact distance: the unit stands when ordered onto a target beside it")
{
	ImpactFixture f;
	f.Range(0, 8);
	f.one.MinImpactRange = 3;

	const auto walk = [&](std::unique_ptr<COrder> order) {
		f.unit.Orders.push_back(std::move(order));
		f.unit.pathFinderData->output.Length = 0;
		int end = PF_WAIT;
		for (int i = 0; i != 40; ++i) {
			const auto [d, dir] = NextPathElement(f.unit);
			end = d;
			if (d == PF_WAIT && f.unit.pathFinderData->output.Length <= 0) {
				continue;
			}
			if (d <= 0) {
				break;
			}
			f.unit.tilePos += dir;
		}
		f.unit.Orders.clear();
		return end;
	};
	for (const Vec2i offset : {Vec2i(1, 0), Vec2i(1, 1), Vec2i(0, -1), Vec2i(2, 0), Vec2i(2, 2), Vec2i(3, 0)}) {
		CAPTURE(offset.x);
		CAPTURE(offset.y);
		f.unit.tilePos = {30, 30};
		f.target.tilePos = f.unit.tilePos + offset;
		CHECK(InAttackRange(f.unit, f.target));
		CHECK(walk(COrder::NewActionAttack(f.unit, f.target)) == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(30, 30));
		CHECK(InAttackRange(f.unit, f.target.tilePos));
		CHECK(walk(COrder::NewActionAttackGround(f.unit, f.target.tilePos)) == PF_REACHED);
		CHECK(f.unit.tilePos == Vec2i(30, 30));
	}
}
