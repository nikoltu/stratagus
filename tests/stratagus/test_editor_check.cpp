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
/**@name test_editor_check.cpp - The map editor's export and check (launchresult.cpp). */
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

#include "json.h"
#include "launchresult.h"
#include "tileset.h"

#include <string>
#include <vector>

namespace
{

/// A quad of one terrain on all four corners.
uint32_t Solid(uint32_t terrain)
{
	return terrain | (terrain << 8) | (terrain << 16) | (terrain << 24);
}

/// Corners ab / cd as CTileset::getQuadFromTile packs them.
uint32_t Quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	return (a << 24) | (b << 16) | (c << 8) | d;
}

content::JsonValue Parse(const std::string &text)
{
	content::JsonValue value;
	std::string error;
	REQUIRE_MESSAGE(content::parseJson(text, value, error), error << " in " << text);
	return value;
}

} // namespace

TEST_CASE("FindTileSeams: tiles that meet on the same terrain have no seam")
{
	// grass | grass-left / water-right | water
	const std::vector<uint32_t> quads = {Solid(1), Quad(1, 2, 1, 2), Solid(2)};
	const std::vector<bool> mixes(3, true);
	CHECK(FindTileSeams(3, 1, quads, mixes).empty());
}

TEST_CASE("FindTileSeams: a seam to the right and one below")
{
	// 2 x 2: grass grass / water grass. (0,0)-(0,1) and (0,1)-(1,1) do not meet.
	const std::vector<uint32_t> quads = {Solid(1), Solid(1), Solid(2), Solid(1)};
	const std::vector<bool> mixes(4, true);
	const std::vector<EditorSeam> seams = FindTileSeams(2, 2, quads, mixes);
	REQUIRE(seams.size() == 2);
	CHECK(seams[0].x == 0);
	CHECK(seams[0].y == 0);
	CHECK(seams[0].down);
	CHECK(seams[1].x == 0);
	CHECK(seams[1].y == 1);
	CHECK_FALSE(seams[1].down);
}

TEST_CASE("FindTileSeams: only the touching halves count")
{
	// Top half grass, bottom half water, above solid water: they meet.
	const std::vector<uint32_t> quads = {Quad(1, 1, 2, 2), Solid(2)};
	const std::vector<bool> mixes(2, true);
	CHECK(FindTileSeams(1, 2, quads, mixes).empty());
	// The other way round they do not.
	const std::vector<uint32_t> flipped = {Quad(2, 2, 1, 1), Solid(2)};
	CHECK(FindTileSeams(1, 2, flipped, mixes).size() == 1);
}

TEST_CASE("FindTileSeams: a tile that does not mix is left out")
{
	const std::vector<uint32_t> quads = {Solid(1), Solid(3), Solid(2)};
	std::vector<bool> mixes(3, true);
	CHECK(FindTileSeams(3, 1, quads, mixes).size() == 2);
	mixes[1] = false; // a wall between them
	CHECK(FindTileSeams(3, 1, quads, mixes).empty());
}

TEST_CASE("FindTileSeams: short or empty input gives nothing")
{
	CHECK(FindTileSeams(0, 0, {}, {}).empty());
	CHECK(FindTileSeams(2, 2, {Solid(1)}, {true}).empty());
}

TEST_CASE("FindTileSeams: the quads of CTileset::getQuadFromTile")
{
	CTileset tileset;
	tileset.tiles.resize(0x100);
	tileset.tiles[0x10].tileinfo = CTileInfo(1, 0); // solid terrain 1
	tileset.tiles[0x20].tileinfo = CTileInfo(2, 0); // solid terrain 2
	tileset.tiles[0x40].tileinfo = CTileInfo(1, 2); // mixed, row 4: left 1, right 2
	const uint32_t one = tileset.getQuadFromTile(0x10);
	const uint32_t two = tileset.getQuadFromTile(0x20);
	const uint32_t mixed = tileset.getQuadFromTile(0x40);
	const std::vector<bool> mixes(3, true);
	CHECK(FindTileSeams(3, 1, {one, mixed, two}, mixes).empty());
	const std::vector<EditorSeam> seams = FindTileSeams(3, 1, {two, mixed, one}, mixes);
	REQUIRE(seams.size() == 2);
	CHECK(seams[0].x == 0);
	CHECK(seams[1].x == 1);
}

TEST_CASE("EditorJsonString: quotes, backslashes and control signs")
{
	CHECK(EditorJsonString("plain") == "\"plain\"");
	CHECK(EditorJsonString("a\"b\\c") == "\"a\\\"b\\\\c\"");
	CHECK(EditorJsonString(std::string("x\ny\x01", 4)) == "\"x\\u000ay\\u0001\"");
	CHECK(Parse(EditorJsonString("Map \"one\"\t\\")).asString() == "Map \"one\"\t\\");
}

TEST_CASE("EditorCheckJson: the record the launcher reads")
{
	std::vector<EditorSeam> seams = {{3, 4, false}, {5, 6, true}};
	EditorMovedUnit unit;
	unit.ident = "unit-footman";
	unit.player = 2;
	unit.askedX = 10;
	unit.askedY = 11;
	unit.atX = 12;
	unit.atY = 11;
	const std::vector<EditorMovedUnit> moved = {unit};

	const content::JsonValue check = Parse(EditorCheckJson("Forest", 64, 32, seams, moved, 256));
	REQUIRE(check.isObject());
	CHECK(check.find("tileset")->asString() == "Forest");
	CHECK(check.find("w")->asInt() == 64);
	CHECK(check.find("h")->asInt() == 32);

	const content::JsonValue *seamPart = check.find("seams");
	REQUIRE(seamPart != nullptr);
	CHECK(seamPart->find("count")->asInt() == 2);
	const content::JsonArray &at = seamPart->find("at")->items();
	REQUIRE(at.size() == 2);
	CHECK(at[0].items()[0].asInt() == 3);
	CHECK(at[0].items()[1].asInt() == 4);
	CHECK(at[0].items()[2].asString() == "right");
	CHECK(at[1].items()[2].asString() == "down");

	const content::JsonValue *movedPart = check.find("moved");
	REQUIRE(movedPart != nullptr);
	CHECK(movedPart->find("count")->asInt() == 1);
	const content::JsonValue &first = movedPart->find("at")->items()[0];
	CHECK(first.find("type")->asString() == "unit-footman");
	CHECK(first.find("player")->asInt() == 2);
	CHECK(first.find("x")->asInt() == 10);
	CHECK(first.find("y")->asInt() == 11);
	CHECK(first.find("to_x")->asInt() == 12);
	CHECK(first.find("to_y")->asInt() == 11);
	CHECK(first.find("lost")->asBool(true) == false);
}

TEST_CASE("EditorCheckJson: the lists stop at the limit, the counts do not")
{
	std::vector<EditorSeam> seams;
	for (int i = 0; i < 10; ++i) {
		seams.push_back({i, 0, false});
	}
	const content::JsonValue check = Parse(EditorCheckJson("Swamp", 10, 1, seams, {}, 4));
	CHECK(check.find("seams")->find("count")->asInt() == 10);
	CHECK(check.find("seams")->find("at")->items().size() == 4);
	CHECK(check.find("moved")->find("count")->asInt() == 0);
	CHECK(check.find("moved")->find("at")->items().empty());
}

TEST_CASE("EditorNoteUnitMoved: noted only while a check is on")
{
	// Not on: a scenario's units are not kept.
	EditorNoteUnitMoved("unit-peasant", 0, 1, 1, 2, 1, false);
	EditorCheckBegin();
	EditorNoteUnitMoved("unit-grunt", 1, 5, 5, 6, 5, false);
	// The tileset and size of no map: the list of the units is what is looked at here.
	const content::JsonValue check = Parse(EditorCheckLoadedMap());
	CHECK(check.find("moved")->find("count")->asInt() == 1);
	CHECK(check.find("moved")->find("at")->items()[0].find("type")->asString() == "unit-grunt");
	// The read ends the check.
	EditorNoteUnitMoved("unit-grunt", 1, 5, 5, 6, 5, false);
	CHECK(Parse(EditorCheckLoadedMap()).find("moved")->find("count")->asInt() == 0);
}

TEST_CASE("SetHDArtPreload: on by default, off only while a run asks")
{
	// Every game, a loaded save included, preloads unless a launch mode turned it off.
	CHECK(HDArtPreloadOn());
	SetHDArtPreload(false);   // launch.lua, modes "export" / "validate"
	CHECK_FALSE(HDArtPreloadOn());
	SetHDArtPreload(true);    // and back after the run
	CHECK(HDArtPreloadOn());
}
