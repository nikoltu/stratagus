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
/**@name test_classic_sprite_map.cpp - Classic view: sprite file -> classic GRP group table. */
//
//      (c) Copyright 2026 by The Stratagus Project
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

#include "../../src/video/classic_sprite_map.h"

#include <set>
#include <string>

TEST_CASE("classic_sprite_map: every row is a well-formed, unique key with a classic group")
{
	CHECK(classicmap::ClassicSpriteRowCount == 260);
	std::set<std::string> keys;
	for (const classicmap::ClassicSpriteRow &row : classicmap::ClassicSpriteRows) {
		const std::string file = row.file;
		const std::string group = row.group;
		CAPTURE(file);
		CHECK(keys.insert(file).second);
		CHECK(file == classicmap::classicSpriteKey(file)); // stored as the key: '/' and lower case
		CHECK(file.size() > 4);
		CHECK(file.compare(file.size() - 4, 4, ".png") == 0);
		const bool folder = group.rfind("Human/", 0) == 0 || group.rfind("Orc/", 0) == 0
		                    || group.rfind("Monster/", 0) == 0 || group.rfind("Other/", 0) == 0;
		CHECK(folder);
		CHECK(group.size() > 4);
		CHECK(group.compare(group.size() - 4, 4, ".grp") == 0);
		CHECK(row.frames > 0);
		CHECK(row.cellW > 0);
		CHECK(row.cellH > 0);
	}
}

TEST_CASE("classic_sprite_map: lookup ignores case and slash kind")
{
	const classicmap::ClassicSpriteRow *peon = classicmap::classicSpriteRow("Orc\\Units\\PEON.png");
	REQUIRE(peon != nullptr);
	CHECK(std::string(peon->group) == "Orc/peon.grp");
	CHECK(peon->frames == 65);
	CHECK(peon->cellW == 72);
	CHECK(peon->cellH == 72);
	CHECK(classicmap::classicSpriteRow("orc/units/no_such_unit.png") == nullptr);
	CHECK(classicmap::classicSpriteRow("") == nullptr);
}

TEST_CASE("classic_sprite_map: rows checked against the classic inventory")
{
	struct Want {
		const char *file;
		const char *group;
		int frames, w, h;
	};
	// Values of work/agent-classic-data/classic_grp_inventory.tsv.
	const Want wants[] = {
		{"orc/units/ogre.png", "Orc/knight.grp", 70, 72, 72},                  // the ogre is the orc knight GRP
		{"human/units/peasant_with_gold.png", "Human/peong.grp", 65, 72, 72},  // carry sprite
		{"missiles/catapult_rock.png", "Other/stone.grp", 15, 32, 32},
		{"missiles/big_cannon.png", "Other/hbattle.grp", 20, 16, 16},
		{"missiles/small_fire.png", "Other/ltfire.grp", 6, 32, 48},
		{"missiles/big_fire.png", "Other/hvfire.grp", 10, 48, 48},
		{"missiles/green_cross.png", "Other/blackx.grp", 4, 32, 32},
		{"tilesets/winter/human/buildings/foundry_construction_site.png", "Human/s_found1.grp", 2, 96, 96},
		{"tilesets/swamp/orc/buildings/foundry_construction_site.png", "Orc/x_found1.grp", 2, 96, 96},
		{"tilesets/swamp/neutral/buildings/circle_of_power.png", "Other/x_vcir.grp", 1, 64, 64},
	};
	for (const Want &w : wants) {
		CAPTURE(w.file);
		const classicmap::ClassicSpriteRow *row = classicmap::classicSpriteRow(w.file);
		REQUIRE(row != nullptr);
		CHECK(std::string(row->group) == w.group);
		CHECK(row->frames == w.frames);
		CHECK(row->cellW == w.w);
		CHECK(row->cellH == w.h);
	}
}

TEST_CASE("classic_sprite_map: every effect of hd_missiles_map has a row")
{
	const char *const effects[] = {
		"missiles/arrow.png", "missiles/axe.png", "missiles/ballista-catapult_impact.png",
		"missiles/ballista_bolt.png", "missiles/big_cannon.png", "missiles/big_fire.png",
		"missiles/blizzard.png", "missiles/cannon-tower_explosion.png", "missiles/cannon.png",
		"missiles/cannon_explosion.png", "missiles/catapult_rock.png", "missiles/daemon_fire.png",
		"missiles/death_and_decay.png", "missiles/dragon_breath.png", "missiles/exorcism.png",
		"missiles/explosion.png", "missiles/fireball.png", "missiles/flame_shield.png",
		"missiles/green_cross.png", "missiles/gryphon_hammer.png", "missiles/heal_effect.png",
		"missiles/lightning.png", "missiles/normal_spell.png", "missiles/rune.png",
		"missiles/small_fire.png", "missiles/submarine_missile.png", "missiles/tornado.png",
		"missiles/touch_of_death.png", "missiles/turtle_missile.png"};
	for (const char *e : effects) {
		CAPTURE(e);
		const classicmap::ClassicSpriteRow *row = classicmap::classicSpriteRow(e);
		REQUIRE(row != nullptr);
		CHECK(std::string(row->group).rfind("Other/", 0) == 0);
	}
}

TEST_CASE("classic_sprite_map: missile frame numbering as the classic sheet blit")
{
	bool mirror = true;
	// Flip (the Lua default): the unit rule, west = east column mirrored.
	CHECK(classicmap::classicMissileFrame(0, true, 9, mirror) == 0);
	CHECK_FALSE(mirror);
	CHECK(classicmap::classicMissileFrame(7, true, 9, mirror) == 7);
	CHECK_FALSE(mirror);
	CHECK(classicmap::classicMissileFrame(-3, true, 9, mirror) == 2);
	CHECK(mirror);
	CHECK(classicmap::classicMissileFrame(-1, true, 1, mirror) == 0);
	CHECK(mirror);
	// No Flip: the sheet holds all directions, nothing is mirrored (MissileType::DrawMissileType).
	CHECK(classicmap::classicMissileFrame(2, false, 8, mirror) == 2);
	CHECK_FALSE(mirror);
	CHECK(classicmap::classicMissileFrame(5, false, 8, mirror) == 8);  // second row, column 0
	CHECK(classicmap::classicMissileFrame(7, false, 8, mirror) == 10);
	CHECK(classicmap::classicMissileFrame(-2, false, 8, mirror) == 7); // NW = last column
	CHECK_FALSE(mirror);
	CHECK(classicmap::classicMissileFrame(-4, false, 8, mirror) == 5); // SW
	CHECK(classicmap::classicMissileFrame(3, false, 1, mirror) == 3);  // one direction: identity
	CHECK(classicmap::classicMissileFrame(3, false, 0, mirror) == -1);
}
