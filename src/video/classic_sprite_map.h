//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \   __\_  __ \__  \   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name classic_sprite_map.h - Classic view: sprite file -> classic GRP group. */
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

#ifndef CLASSIC_SPRITE_MAP_H
#define CLASSIC_SPRITE_MAP_H

// Classic view (slice 3): which classic GRP group of the map's classic atlas
// (`Data/Art/hd/classic/classic_<tileset>.json`, frames "<Group>/<name>.grp/indexed_N") draws a sprite
// file of ours. Names and numbers only: no picture and no colour is in this file; the art is read from
// the player's game folder at run time.
//
// Key: the engine sprite file as the Lua names it (CUnitType::File, a carry sprite's File, a missile
// type's G->File), backslash -> '/', lower case. The per-tileset variant is already in the file name
// (tilesets/<tileset>/...), so one row is one tileset's picture.
//
// The rows were made from hd_units_map.json (231) and hd_missiles_map.json (29) by the rule of
// hdatlas::classicGroupNames (prefix without "_<digits>", folder Human / Orc by the atlas stem, then
// Other, Monster, Human, Orc), each checked to exist in the classic atlas with the frame count and the
// cell given here (work/agent-classic-data/classic_grp_inventory.tsv, 262 groups, identical in the four
// atlases; EVIDENCE-CLASSIC-001). Four rows differ from the HD map on purpose: the winter and swamp
// foundry construction sites take the classic variants s_found1 / x_found1 (the HD map names found1;
// the classic naming rule of CLASSIC_UNIT_TABLE.md section 4 and its supplement rows give the variant,
// which exists in the atlas). The table keeps the classic view apart from later changes of the HD
// art names; a file it lacks is resolved by the HD map rule (the caller logs that once).
// Not proven against the original's screen except Orc/peon.grp (EVIDENCE-CLASSIC-002), the land
// site, farm, town hall and the two flames (S2B_NOTE). Open names: see CLASSIC_S3.md "UNKNOWN".

#include "hd_atlas.h"

#include <cctype>
#include <string>
#include <string_view>

namespace classicmap
{

struct ClassicSpriteRow {
	const char *file;   // engine sprite file, '/' and lower case
	const char *group;  // classic group, "<Human|Orc|Monster|Other>/<name>.grp"
	int frames;         // frames of the group (indexed_0 .. frames-1)
	int cellW;          // classic pixels of one cell (all frames of a group have one cell)
	int cellH;
};

inline constexpr ClassicSpriteRow ClassicSpriteRows[] = {
	{"human/buildings/foundry_construction_site.png", "Human/found1.grp", 2, 96, 96},
	{"human/buildings/refinery_construction_site.png", "Human/ref1.grp", 2, 96, 96},
	{"human/buildings/shipyard_construction_site.png", "Human/ship1.grp", 2, 96, 96},
	{"human/units/ballista.png", "Human/catapult.grp", 20, 64, 64},
	{"human/units/battleship.png", "Human/battlshp.grp", 15, 88, 88},
	{"human/units/dwarven_demolition_squad.png", "Human/dwarves.grp", 65, 56, 56},
	{"human/units/elven_archer.png", "Human/spear.grp", 50, 72, 72},
	{"human/units/elven_destroyer.png", "Human/destroy.grp", 15, 80, 88},
	{"human/units/footman.png", "Human/grunt.grp", 60, 72, 72},
	{"human/units/gnomish_flying_machine.png", "Human/orn.grp", 20, 80, 80},
	{"human/units/gnomish_submarine.png", "Human/sub.grp", 15, 72, 72},
	{"human/units/gryphon_rider.png", "Human/griffon.grp", 65, 80, 80},
	{"human/units/knight.png", "Human/knight.grp", 70, 72, 72},
	{"human/units/mage.png", "Human/wizard.grp", 80, 72, 72},
	{"human/units/oil_tanker_empty.png", "Human/tanker.grp", 15, 72, 72},
	{"human/units/oil_tanker_full.png", "Human/tankero.grp", 15, 72, 72},
	{"human/units/peasant.png", "Human/peon.grp", 65, 72, 72},
	{"human/units/peasant_with_gold.png", "Human/peong.grp", 65, 72, 72},
	{"human/units/peasant_with_wood.png", "Human/peonl.grp", 65, 72, 72},
	{"human/units/transport.png", "Human/transp.grp", 15, 72, 72},
	{"human/x_startpoint.png", "Human/startloc.grp", 1, 32, 32},
	{"neutral/buildings/circle_of_power.png", "Other/vcircle.grp", 1, 64, 64},
	{"neutral/buildings/runestone.png", "Other/rock.grp", 1, 64, 64},
	{"neutral/units/skeleton.png", "Orc/skeleton.grp", 70, 56, 56},
	{"orc/buildings/foundry_construction_site.png", "Orc/found1.grp", 2, 96, 96},
	{"orc/buildings/refinery_construction_site.png", "Orc/ref1.grp", 2, 96, 96},
	{"orc/buildings/shipyard_construction_site.png", "Orc/ship1.grp", 2, 96, 96},
	{"orc/o_startpoint.png", "Orc/startloc.grp", 1, 32, 32},
	{"orc/units/catapult.png", "Orc/catapult.grp", 20, 64, 64},
	{"orc/units/death_knight.png", "Orc/dknight.grp", 65, 72, 72},
	{"orc/units/dragon.png", "Orc/dragon.grp", 50, 88, 80},
	{"orc/units/eye_of_kilrogg.png", "Orc/eyeofkil.grp", 5, 32, 32},
	{"orc/units/giant_turtle.png", "Orc/sub.grp", 15, 72, 72},
	{"orc/units/goblin_sappers.png", "Orc/goblins.grp", 75, 56, 56},
	{"orc/units/goblin_zeppelin.png", "Orc/zep.grp", 10, 72, 72},
	{"orc/units/grunt.png", "Orc/grunt.grp", 60, 72, 72},
	{"orc/units/ogre.png", "Orc/knight.grp", 70, 72, 72},
	{"orc/units/ogre_juggernaught.png", "Orc/battlshp.grp", 15, 88, 88},
	{"orc/units/oil_tanker_empty.png", "Orc/tanker.grp", 15, 72, 72},
	{"orc/units/oil_tanker_full.png", "Orc/tankero.grp", 15, 72, 72},
	{"orc/units/peon.png", "Orc/peon.grp", 65, 72, 72},
	{"orc/units/peon_with_gold.png", "Orc/peong.grp", 65, 72, 72},
	{"orc/units/peon_with_wood.png", "Orc/peonl.grp", 65, 72, 72},
	{"orc/units/transport.png", "Orc/transp.grp", 15, 72, 72},
	{"orc/units/troll_axethrower.png", "Orc/spear.grp", 60, 72, 72},
	{"orc/units/troll_destroyer.png", "Orc/destroy.grp", 15, 88, 88},
	{"tilesets/summer/human/buildings/barracks.png", "Human/barr.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/blacksmith.png", "Human/black.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/cannon_tower.png", "Human/towerc.grp", 2, 64, 64},
	{"tilesets/summer/human/buildings/castle.png", "Human/castle.grp", 2, 128, 128},
	{"tilesets/summer/human/buildings/church.png", "Human/church.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/elven_lumber_mill.png", "Human/lmill.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/farm.png", "Human/farm.grp", 2, 64, 64},
	{"tilesets/summer/human/buildings/foundry.png", "Human/found2.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/gnomish_inventor.png", "Human/invent.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/gryphon_aviary.png", "Human/aviary.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/guard_tower.png", "Human/towera.grp", 2, 64, 64},
	{"tilesets/summer/human/buildings/keep.png", "Human/keep.grp", 2, 128, 128},
	{"tilesets/summer/human/buildings/mage_tower.png", "Human/wtower.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/oil_platform.png", "Human/oplat2.grp", 3, 96, 96},
	{"tilesets/summer/human/buildings/oil_well_construction_site.png", "Human/oplat1.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/refinery.png", "Human/ref2.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/scout_tower.png", "Human/tower.grp", 2, 64, 64},
	{"tilesets/summer/human/buildings/shipyard.png", "Human/ship2.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/stables.png", "Human/stable.grp", 2, 96, 96},
	{"tilesets/summer/human/buildings/town_hall.png", "Human/thall.grp", 2, 128, 128},
	{"tilesets/summer/neutral/buildings/circle_of_power.png", "Other/vcircle.grp", 1, 64, 64},
	{"tilesets/summer/neutral/buildings/dark_portal.png", "Other/gate.grp", 1, 128, 128},
	{"tilesets/summer/neutral/buildings/destroyed_site.png", "Other/bldgdth.grp", 4, 64, 64},
	{"tilesets/summer/neutral/buildings/gold_mine.png", "Other/mine.grp", 2, 96, 96},
	{"tilesets/summer/neutral/buildings/oil_patch.png", "Other/patch.grp", 1, 96, 96},
	{"tilesets/summer/neutral/buildings/small_destroyed_site.png", "Other/dwall.grp", 2, 32, 32},
	{"tilesets/summer/neutral/buildings/wall.png", "Other/fbldwall.grp", 2, 32, 32},
	{"tilesets/summer/neutral/units/corpses.png", "Other/death.grp", 35, 72, 72},
	{"tilesets/summer/neutral/units/critter.png", "Monster/sheep.grp", 10, 32, 32},
	{"tilesets/summer/neutral/units/daemon.png", "Monster/demon.grp", 75, 72, 72},
	{"tilesets/summer/orc/buildings/altar_of_storms.png", "Orc/dtower.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/barracks.png", "Orc/barr.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/blacksmith.png", "Orc/black.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/cannon_tower.png", "Orc/towerc.grp", 2, 64, 64},
	{"tilesets/summer/orc/buildings/dragon_roost.png", "Orc/roost.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/fortress.png", "Orc/blakrock.grp", 2, 128, 128},
	{"tilesets/summer/orc/buildings/foundry.png", "Orc/found2.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/goblin_alchemist.png", "Orc/invent.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/great_hall.png", "Orc/thall.grp", 2, 128, 128},
	{"tilesets/summer/orc/buildings/guard_tower.png", "Orc/towera.grp", 2, 64, 64},
	{"tilesets/summer/orc/buildings/ogre_mound.png", "Orc/ogrecamp.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/oil_platform.png", "Orc/oplat2.grp", 3, 96, 96},
	{"tilesets/summer/orc/buildings/oil_well_construction_site.png", "Orc/oplat1.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/pig_farm.png", "Orc/farm.grp", 2, 64, 64},
	{"tilesets/summer/orc/buildings/refinery.png", "Orc/ref2.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/shipyard.png", "Orc/ship2.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/stronghold.png", "Orc/keep.grp", 2, 128, 128},
	{"tilesets/summer/orc/buildings/temple_of_the_damned.png", "Orc/temple.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/troll_lumber_mill.png", "Orc/lmill.grp", 2, 96, 96},
	{"tilesets/summer/orc/buildings/watch_tower.png", "Orc/tower.grp", 2, 64, 64},
	{"tilesets/swamp/human/buildings/barracks.png", "Human/x_barr.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/blacksmith.png", "Human/x_black.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/cannon_tower.png", "Human/x_towerc.grp", 2, 64, 64},
	{"tilesets/swamp/human/buildings/castle.png", "Human/x_castle.grp", 2, 128, 128},
	{"tilesets/swamp/human/buildings/church.png", "Human/x_church.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/elven_lumber_mill.png", "Human/x_lmill.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/farm.png", "Human/x_farm.grp", 2, 64, 64},
	{"tilesets/swamp/human/buildings/foundry.png", "Human/x_found2.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/foundry_construction_site.png", "Human/x_found1.grp", 2, 96, 96}, // classic variant, not the HD map's found1
	{"tilesets/swamp/human/buildings/gnomish_inventor.png", "Human/x_inv.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/gryphon_aviary.png", "Human/x_aviary.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/guard_tower.png", "Human/x_towera.grp", 2, 64, 64},
	{"tilesets/swamp/human/buildings/keep.png", "Human/x_keep.grp", 2, 128, 128},
	{"tilesets/swamp/human/buildings/mage_tower.png", "Human/x_wtower.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/oil_platform.png", "Human/x_oplat2.grp", 3, 96, 96},
	{"tilesets/swamp/human/buildings/oil_platform_construction_site.png", "Human/x_oplat1.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/refinery.png", "Human/x_ref2.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/refinery_construction_site.png", "Human/x_ref1.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/scout_tower.png", "Human/x_tower.grp", 2, 64, 64},
	{"tilesets/swamp/human/buildings/shipyard.png", "Human/x_ship2.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/shipyard_construction_site.png", "Human/x_ship1.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/stables.png", "Human/x_stable.grp", 2, 96, 96},
	{"tilesets/swamp/human/buildings/town_hall.png", "Human/x_thall.grp", 2, 128, 128},
	{"tilesets/swamp/human/units/gnomish_submarine.png", "Human/x_sub.grp", 15, 72, 72},
	{"tilesets/swamp/neutral/buildings/circle_of_power.png", "Other/x_vcir.grp", 1, 64, 64},
	{"tilesets/swamp/neutral/buildings/dark_portal.png", "Other/x_gate.grp", 1, 128, 128},
	{"tilesets/swamp/neutral/buildings/destroyed_site.png", "Other/x_blddth.grp", 4, 64, 64},
	{"tilesets/swamp/neutral/buildings/gold_mine.png", "Other/x_mine.grp", 2, 96, 96},
	{"tilesets/swamp/neutral/buildings/oil_patch.png", "Other/x_patch.grp", 2, 96, 96},
	{"tilesets/swamp/neutral/buildings/runestone.png", "Other/x_rock.grp", 1, 64, 64},
	{"tilesets/swamp/neutral/buildings/small_destroyed_site.png", "Other/x_dwall.grp", 2, 32, 32},
	{"tilesets/swamp/neutral/buildings/wall.png", "Other/sbldwall.grp", 2, 32, 32},
	{"tilesets/swamp/neutral/units/corpses.png", "Other/death.grp", 35, 72, 72},
	{"tilesets/swamp/neutral/units/critter.png", "Monster/hellhog.grp", 10, 32, 32},
	{"tilesets/swamp/neutral/units/daemon.png", "Monster/demon.grp", 75, 72, 72},
	{"tilesets/swamp/orc/buildings/altar_of_storms.png", "Orc/x_dtower.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/barracks.png", "Orc/x_barr.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/blacksmith.png", "Orc/x_black.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/cannon_tower.png", "Orc/x_towerc.grp", 2, 64, 64},
	{"tilesets/swamp/orc/buildings/dragon_roost.png", "Orc/x_roost.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/fortress.png", "Orc/x_blakrk.grp", 2, 128, 128},
	{"tilesets/swamp/orc/buildings/foundry.png", "Orc/x_found2.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/foundry_construction_site.png", "Orc/x_found1.grp", 2, 96, 96}, // classic variant, not the HD map's found1
	{"tilesets/swamp/orc/buildings/goblin_alchemist.png", "Orc/x_inv.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/great_hall.png", "Orc/x_thall.grp", 2, 128, 128},
	{"tilesets/swamp/orc/buildings/guard_tower.png", "Orc/x_towera.grp", 2, 64, 64},
	{"tilesets/swamp/orc/buildings/ogre_mound.png", "Orc/x_ogre.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/oil_platform.png", "Orc/x_oplat2.grp", 3, 96, 96},
	{"tilesets/swamp/orc/buildings/oil_platform_construction_site.png", "Orc/x_oplat1.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/pig_farm.png", "Orc/x_farm.grp", 2, 64, 64},
	{"tilesets/swamp/orc/buildings/refinery.png", "Orc/x_ref2.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/refinery_construction_site.png", "Orc/x_ref1.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/shipyard.png", "Orc/x_ship2.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/shipyard_construction_site.png", "Orc/x_ship1.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/stronghold.png", "Orc/x_keep.grp", 2, 128, 128},
	{"tilesets/swamp/orc/buildings/temple_of_the_damned.png", "Orc/x_temple.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/troll_lumber_mill.png", "Orc/x_lmill.grp", 2, 96, 96},
	{"tilesets/swamp/orc/buildings/watch_tower.png", "Orc/x_tower.grp", 2, 64, 64},
	{"tilesets/swamp/orc/units/giant_turtle.png", "Orc/x_sub.grp", 15, 72, 72},
	{"tilesets/wasteland/human/buildings/elven_lumber_mill.png", "Human/l_lmill.grp", 2, 96, 96},
	{"tilesets/wasteland/human/buildings/farm.png", "Human/l_farm.grp", 2, 64, 64},
	{"tilesets/wasteland/human/buildings/oil_platform.png", "Human/l_oplat2.grp", 3, 96, 96},
	{"tilesets/wasteland/human/buildings/oil_well_construction_site.png", "Human/l_oplat1.grp", 2, 96, 96},
	{"tilesets/wasteland/human/units/gnomish_submarine.png", "Human/l_sub.grp", 15, 72, 72},
	{"tilesets/wasteland/neutral/buildings/circle_of_power.png", "Other/vcircle.grp", 1, 64, 64},
	{"tilesets/wasteland/neutral/buildings/dark_portal.png", "Other/l_gate.grp", 1, 128, 128},
	{"tilesets/wasteland/neutral/buildings/destroyed_site.png", "Other/l_blddth.grp", 4, 64, 64},
	{"tilesets/wasteland/neutral/buildings/gold_mine.png", "Other/l_mine.grp", 2, 96, 96},
	{"tilesets/wasteland/neutral/buildings/oil_patch.png", "Other/l_patch.grp", 1, 96, 96},
	{"tilesets/wasteland/neutral/buildings/small_destroyed_site.png", "Other/l_dwall.grp", 2, 32, 32},
	{"tilesets/wasteland/neutral/buildings/wall.png", "Other/sbldwall.grp", 2, 32, 32},
	{"tilesets/wasteland/neutral/units/corpses.png", "Other/death.grp", 35, 72, 72},
	{"tilesets/wasteland/neutral/units/critter.png", "Monster/boar.grp", 10, 32, 32},
	{"tilesets/wasteland/neutral/units/daemon.png", "Monster/demon.grp", 75, 72, 72},
	{"tilesets/wasteland/orc/buildings/oil_platform.png", "Orc/l_oplat2.grp", 3, 96, 96},
	{"tilesets/wasteland/orc/buildings/oil_well_construction_site.png", "Orc/l_oplat1.grp", 2, 96, 96},
	{"tilesets/wasteland/orc/buildings/pig_farm.png", "Orc/l_farm.grp", 2, 64, 64},
	{"tilesets/wasteland/orc/buildings/troll_lumber_mill.png", "Orc/l_lmill.grp", 2, 96, 96},
	{"tilesets/wasteland/orc/units/giant_turtle.png", "Orc/l_sub.grp", 15, 72, 72},
	{"tilesets/winter/human/buildings/barracks.png", "Human/s_barr.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/blacksmith.png", "Human/s_black.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/cannon_tower.png", "Human/s_towerc.grp", 2, 64, 64},
	{"tilesets/winter/human/buildings/castle.png", "Human/s_castle.grp", 2, 128, 128},
	{"tilesets/winter/human/buildings/church.png", "Human/s_church.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/elven_lumber_mill.png", "Human/s_lmill.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/farm.png", "Human/s_farm.grp", 2, 64, 64},
	{"tilesets/winter/human/buildings/foundry.png", "Human/s_found2.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/foundry_construction_site.png", "Human/s_found1.grp", 2, 96, 96}, // classic variant, not the HD map's found1
	{"tilesets/winter/human/buildings/gnomish_inventor.png", "Human/s_inv.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/gryphon_aviary.png", "Human/s_aviary.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/guard_tower.png", "Human/s_towera.grp", 2, 64, 64},
	{"tilesets/winter/human/buildings/keep.png", "Human/s_keep.grp", 2, 128, 128},
	{"tilesets/winter/human/buildings/mage_tower.png", "Human/s_wtower.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/oil_platform.png", "Human/s_oplat2.grp", 3, 96, 96},
	{"tilesets/winter/human/buildings/oil_well_construction_site.png", "Human/s_oplat1.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/refinery.png", "Human/s_ref2.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/refinery_construction_site.png", "Human/s_ref1.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/scout_tower.png", "Human/s_tower.grp", 2, 64, 64},
	{"tilesets/winter/human/buildings/shipyard.png", "Human/s_ship2.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/shipyard_construction_site.png", "Human/s_ship1.grp", 3, 96, 96},
	{"tilesets/winter/human/buildings/stables.png", "Human/s_stable.grp", 2, 96, 96},
	{"tilesets/winter/human/buildings/town_hall.png", "Human/s_thall.grp", 2, 128, 128},
	{"tilesets/winter/neutral/buildings/circle_of_power.png", "Other/vcircle.grp", 1, 64, 64},
	{"tilesets/winter/neutral/buildings/dark_portal.png", "Other/s_gate.grp", 1, 128, 128},
	{"tilesets/winter/neutral/buildings/destroyed_site.png", "Other/s_blddth.grp", 4, 64, 64},
	{"tilesets/winter/neutral/buildings/gold_mine.png", "Other/s_mine.grp", 2, 96, 96},
	{"tilesets/winter/neutral/buildings/runestone.png", "Other/s_rock.grp", 1, 64, 64},
	{"tilesets/winter/neutral/buildings/small_destroyed_site.png", "Other/s_dwall.grp", 2, 32, 32},
	{"tilesets/winter/neutral/buildings/wall.png", "Other/ibldwall.grp", 2, 32, 32},
	{"tilesets/winter/neutral/units/corpses.png", "Other/death.grp", 35, 72, 72},
	{"tilesets/winter/neutral/units/critter.png", "Monster/seal.grp", 10, 32, 32},
	{"tilesets/winter/neutral/units/daemon.png", "Monster/demon.grp", 75, 72, 72},
	{"tilesets/winter/orc/buildings/altar_of_storms.png", "Orc/s_dtower.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/barracks.png", "Orc/s_barr.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/blacksmith.png", "Orc/s_black.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/cannon_tower.png", "Orc/s_towerc.grp", 2, 64, 64},
	{"tilesets/winter/orc/buildings/dragon_roost.png", "Orc/s_roost.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/fortress.png", "Orc/s_blakrk.grp", 2, 128, 128},
	{"tilesets/winter/orc/buildings/foundry.png", "Orc/s_found2.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/foundry_construction_site.png", "Orc/s_found1.grp", 2, 96, 96}, // classic variant, not the HD map's found1
	{"tilesets/winter/orc/buildings/goblin_alchemist.png", "Orc/s_inv.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/great_hall.png", "Orc/s_thall.grp", 2, 128, 128},
	{"tilesets/winter/orc/buildings/guard_tower.png", "Orc/s_towera.grp", 2, 64, 64},
	{"tilesets/winter/orc/buildings/ogre_mound.png", "Orc/s_ogre.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/oil_platform.png", "Orc/s_oplat2.grp", 3, 96, 96},
	{"tilesets/winter/orc/buildings/oil_well_construction_site.png", "Orc/s_oplat1.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/pig_farm.png", "Orc/s_farm.grp", 2, 64, 64},
	{"tilesets/winter/orc/buildings/refinery.png", "Orc/s_ref2.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/refinery_construction_site.png", "Orc/s_ref1.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/shipyard.png", "Orc/s_ship2.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/shipyard_construction_site.png", "Orc/s_ship1.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/stronghold.png", "Orc/s_keep.grp", 2, 128, 128},
	{"tilesets/winter/orc/buildings/temple_of_the_damned.png", "Orc/s_temple.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/troll_lumber_mill.png", "Orc/s_lmill.grp", 2, 96, 96},
	{"tilesets/winter/orc/buildings/watch_tower.png", "Orc/s_tower.grp", 2, 64, 64},
	{"missiles/arrow.png", "Other/arrow.grp", 5, 40, 40},
	{"missiles/axe.png", "Other/axe.grp", 15, 32, 32},
	{"missiles/ballista-catapult_impact.png", "Other/cathit.grp", 6, 48, 48},
	{"missiles/ballista_bolt.png", "Other/bolt.grp", 5, 64, 64},
	{"missiles/big_cannon.png", "Other/hbattle.grp", 20, 16, 16},
	{"missiles/big_fire.png", "Other/hvfire.grp", 10, 48, 48},
	{"missiles/blizzard.png", "Other/blizzard.grp", 4, 32, 32},
	{"missiles/cannon-tower_explosion.png", "Other/canboom.grp", 4, 32, 32},
	{"missiles/cannon.png", "Other/cannon.grp", 15, 32, 32},
	{"missiles/cannon_explosion.png", "Other/canboom.grp", 4, 32, 32},
	{"missiles/catapult_rock.png", "Other/stone.grp", 15, 32, 32},
	{"missiles/daemon_fire.png", "Other/fire.grp", 15, 32, 32},
	{"missiles/death_and_decay.png", "Other/rot.grp", 8, 32, 32},
	{"missiles/dragon_breath.png", "Other/fire.grp", 15, 32, 32},
	{"missiles/exorcism.png", "Other/exorcism.grp", 6, 48, 48},
	{"missiles/explosion.png", "Other/boom.grp", 16, 64, 64},
	{"missiles/fireball.png", "Other/fireball.grp", 5, 32, 32},
	{"missiles/flame_shield.png", "Other/charms.grp", 5, 16, 16},
	{"missiles/green_cross.png", "Other/blackx.grp", 4, 32, 32},
	{"missiles/gryphon_hammer.png", "Other/hammer.grp", 15, 32, 32},
	{"missiles/heal_effect.png", "Other/heal.grp", 6, 48, 48},
	{"missiles/lightning.png", "Other/lightng.grp", 30, 32, 32},
	{"missiles/normal_spell.png", "Other/sparkle.grp", 6, 32, 32},
	{"missiles/rune.png", "Other/rune.grp", 4, 16, 16},
	{"missiles/small_fire.png", "Other/ltfire.grp", 6, 32, 48},
	{"missiles/submarine_missile.png", "Other/torpedo.grp", 5, 40, 40},
	{"missiles/tornado.png", "Other/phoon.grp", 4, 56, 56},
	{"missiles/touch_of_death.png", "Other/dkatak.grp", 30, 32, 32},
	{"missiles/turtle_missile.png", "Other/torpedo.grp", 5, 40, 40},
};

inline constexpr int ClassicSpriteRowCount = (int) (sizeof(ClassicSpriteRows) / sizeof(ClassicSpriteRows[0]));

/// The key of a sprite file: backslash -> '/', lower case.
inline std::string classicSpriteKey(std::string_view file)
{
	std::string key(file);
	for (char &c : key) {
		if (c == '\\') {
			c = '/';
		}
		c = (char) std::tolower((unsigned char) c);
	}
	return key;
}

/// The row of a sprite file (any spelling of case and slashes), or nullptr.
inline const ClassicSpriteRow *classicSpriteRow(std::string_view file)
{
	const std::string key = classicSpriteKey(file);
	for (const ClassicSpriteRow &row : ClassicSpriteRows) {
		if (key == row.file) {
			return &row;
		}
	}
	return nullptr;
}

/// Classic view: engine missile frame -> frame of the classic group, as the classic sheet blit of
/// MissileType::DrawMissileType numbers it. Flip (the Lua default): a negative frame f is frame -f-1
/// mirrored (hdatlas::classicFrame). Without Flip the sheet holds every direction: a row of
/// NumDirections / 2 + 1 engine columns is spread over NumDirections sheet columns, the west half
/// counted back from the row's end; nothing is mirrored. -1 for a sheet with no direction.
inline int classicMissileFrame(int engineFrame, bool flip, int numDirections, bool &mirror)
{
	mirror = false;
	if (flip) {
		return hdatlas::classicFrame(engineFrame, mirror);
	}
	if (numDirections <= 0) {
		return -1;
	}
	const int row = numDirections / 2 + 1;
	if (engineFrame < 0) {
		const int f = -engineFrame - 1;
		return (f / row) * numDirections + numDirections - f % row;
	}
	return (engineFrame / row) * numDirections + engineFrame % row;
}

/// Classic view, the flame over a burning building: no correction of its own. The fire missile is
/// made at the measured place for its building's size (missile_cadence.h BurningFlameLift), which
/// the classic cell and the Remastered canvas share; the earlier fixed 6 classic px drop (one tile
/// less the 4 x 4 hall's 26) is gone with it.

} // namespace classicmap

#endif // !CLASSIC_SPRITE_MAP_H
