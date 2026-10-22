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
/**@name unittype.cpp - The unit types. */
//
//      (c) Copyright 1998-2015 by Lutz Sammer, Jimmy Salmon and Andrettin
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

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "unittype.h"
#include "network.h"

#include "animation.h"
#include "animation/animation_exactframe.h"
#include "animation/animation_frame.h"
#include "construct.h"
#include "iolib.h"
#include "luacallback.h"
#include "map.h"
#include "missile.h"
#include "palette_cycle.h"
#include "player.h"
#include "script.h"
#include "sound.h"
#include "spells.h"
#include "tileset.h"
#include "translate.h"
#include "unit.h"
#include "ui.h"
#include "unitsound.h"
#include "util.h"
#include "video.h"
#include "json.h"
#include "../video/classic_sprite_map.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL_image.h>

/*----------------------------------------------------------------------------
-- Documentation
----------------------------------------------------------------------------*/

/**
**  @class CUnitType unittype.h
**
**  \#include "unittype.h"
**
**  This class contains the information that is shared between all
**  units of the same type and determins if a unit is a building,
**  a person, ...
**
**  The unit-type class members:
**
**  CUnitType::Ident
**
**    Unique identifier of the unit-type, used to reference it in
**    config files and during startup. As convention they start with
**    "unit-" fe. "unit-farm".
**  @note Don't use this member in game, use instead the pointer
**  to this structure. See UnitTypeByIdent().
**
**  CUnitType::Name
**
**    Pretty name shown by the engine. The name should be shorter
**    than 17 characters and no word can be longer than 8 characters.
**
**  CUnitType::File
**
**    Path file name of the sprite file.
**
**  CUnitType::ShadowFile
**
**    Path file name of shadow sprite file.
**
**  CUnitType::DrawLevel
**
**    The Level/Order to draw this type of unit in. 0-255 usually.
**
**  CUnitType::Width CUnitType::Height
**
**    Size of a sprite frame in pixels. All frames of a sprite have
**    the same size. Also all sprites (tilesets) must have the same
**    size.
**
**  CUnitType::ShadowWidth CUnitType::ShadowHeight
**
**    Size of a shadow sprite frame in pixels. All frames of a sprite
**    have the same size. Also all sprites (tilesets) must have the
**    same size.
**
**  CUnitType::ShadowOffsetX CUnitType::ShadowOffsetY
**
**    Vertical offset to draw the shadow in pixels.
**
**  CUnitType::Animations
**
**    Animation scripts for the different actions. Currently the
**    animations still, move, attack and die are supported.
**  @see CAnimations
**  @see CAnimation
**
**  CUnitType::Icon
**
**    Icon to display for this unit-type. Contains configuration and
**    run time variable.
**  @note This icon can be used for training, but isn't used.
**
**  CUnitType::Missile
**
**    Configuration and run time variable of the missile weapon.
**  @note It is planned to support more than one weapons.
**  And the sound of the missile should be used as fire sound.
**
**  CUnitType::Explosion
**
**    Configuration and run time variable of the missile explosion.
**    This is the explosion that happens if unit is set to
**    ExplodeWhenKilled
**
**  CUnitType::CorpseName
**
**    Corpse unit-type name, should only be used during setup.
**
**  CUnitType::CorpseType
**
**    Corpse unit-type pointer, only this should be used during run
**    time. Many unit-types can share the same corpse.
**
**
**  @todo continue this documentation
**
**  CUnitType::Construction
**
**    What is shown in construction phase.
**
**  CUnitType::SightRange
**
**    Sight range
**
**  CUnitType::_HitPoints
**
**    Maximum hit points
**
**
**  CUnitType::_Costs[::MaxCosts]
**
**    How many resources needed
**
**  CUnitType::RepairHP
**
**    The HP given to a unit each cycle it's repaired.
**    If zero, unit cannot be repaired
**
**    CUnitType::RepairCosts[::MaxCosts]
**
**    Costs per repair cycle to fix a unit.
**
**  CUnitType::TileWidth
**
**    Tile size on map width
**
**  CUnitType::TileHeight
**
**    Tile size on map height
**
**  CUnitType::BoxWidth
**
**    Selected box size width
**
**  CUnitType::BoxHeight
**
**    Selected box size height
**
**  CUnitType::NumDirections
**
**    Number of directions the unit can face
**
**  CUnitType::MinAttackRange
**
**    Minimal attack range
**
**  CUnitType::MinImpactRange
**
**    Nearest distance (tiles, larger axis offset) at which the unit's
**    missile comes down. The unit attacks a nearer target from where it
**    stands; its missile flies over it to that distance.
**
**  CUnitType::ReactRangeComputer
**
**    Reacts on enemy for computer
**
**  CUnitType::ReactRangePerson
**
**    Reacts on enemy for person player
**
**  CUnitType::Priority
**
**    Priority value / AI Treatment
**
**  CUnitType::BurnPercent
**
**    The burning limit in percent. If the unit has lees than
**    this it will start to burn.
**
**  CUnitType::BurnDamageRate
**
**    Burn rate in HP per second
**
**  CUnitType::UnitType
**
**    Land / fly / naval
**
**  @note original only visual effect, we do more with this!
**
**  CUnitType::DecayRate
**
**    Decay rate in 1/6 seconds
**
**  CUnitType::AnnoyComputerFactor
**
**    How much this annoys the computer
**
**  @todo not used
**
**  CUnitType::MouseAction
**
**    Right click action
**
**  CUnitType::Points
**
**    How many points you get for unit. Used in the final score table.
**
**  CUnitType::CanTarget
**
**    Which units can it attack
**
**  CUnitType::LandUnit
**
**    Land animated
**
**  CUnitType::AirUnit
**
**    Air animated
**
**  CUnitType::SeaUnit
**
**    Sea animated
**
**  CUnitType::ExplodeWhenKilled
**
**    Death explosion animated
**
**  CUnitType::RandomMovementProbability
**
**    When the unit is idle this is the probability that it will
**    take a step in a random direction, in percent.
**
**  CUnitType::ClicksToExplode
**
**    If this is non-zero, then after that many clicks the unit will
**    commit suicide. Doesn't work with resource workers/resources.
**
**  CUnitType::Building
**
**    Unit is a Building
**
**  CUnitType::Transporter
**
**    Can transport units
**
**  CUnitType::MaxOnBoard
**
**    Maximum units on board (for transporters), and resources
**
**  CUnitType::StartingResources
**    Amount of Resources a unit has when It's Built
**
**  CUnitType::DamageType
**    Unit's missile damage type (used for extra death animations)
**
**  CUnitType::GivesResource
**
**    This equals to the resource Id of the resource given
**    or 0 (TimeCost) for other buildings.
**
**  CUnitType::ResInfo[::MaxCosts]
**
**    Information about resource harvesting. If nullptr, it can't
**    harvest it.
**
**  CUnitType::NeutralMinimapColorRGB
**
**    Says what color a unit will have when it's neutral and
**    is displayed on the minimap.
**
**  CUnitType::CanStore[::MaxCosts]
**
**    What resource types we can store here.
**
**  CUnitType::CanCastSpell
**
**    Unit is able to use spells
**
**  CUnitType::CanAttack
**
**    Unit is able to attack.
**
**  CUnitType::RepairRange
**
**    Unit can repair buildings. It will use the attack animation.
**    It will heal 4 points for every repair cycle, and cost 1 of
**    each resource, alternatively(1 cycle wood, 1 cycle gold)
**  @todo The above should be more configurable.
**    If units have a repair range, they can repair, and this is the
**    distance.
**
**    CUnitType::ShieldPiercing
**
**    Can directly damage shield-protected units, without shield damaging.
**
**  CUnitType::Sound
**
**    Sounds for events
**
**  CUnitType::Weapon
**
**    Current sound for weapon
**
**  @todo temporary solution
**
**  CUnitType::FieldFlags
**
**    Flags that are set, if a unit enters a map field or cleared, if
**    a unit leaves a map field.
**
**  CUnitType::MovementMask
**
**    Movement mask, this value is and'ed to the map field flags, to
**    see if a unit can enter or placed on the map field.
**
**  CUnitType::Stats[::PlayerMax]
**
**    Unit status for each player
**  @todo This stats should? be moved into the player struct
**
**  CUnitType::Type
**
**    Type as number
**  @todo Should us a general name f.e. Slot here?
**
**  CUnitType::Sprite
**
**    Sprite images
**
**  CUnitType::ShadowSprite
**
**    Shadow sprite images
**
**
*/
/**
**
**  @class ResourceInfo unittype.h
**
** \#include "unittype.h"
**
**    This class contains information about how a unit will harvest a resource.
**
**  ResourceInfo::FileWhenLoaded
**
**    The harvester's animation file will change when it's loaded.
**
**  ResourceInfo::FileWhenEmpty;
**
**    The harvester's animation file will change when it's empty.
**    The standard animation is used only when building/repairing.
**
**
**  ResourceInfo::HarvestFromOutside
**
**    Unit will harvest from the outside. The unit will use it's
**    Attack animation (seems it turned into a generic Action anim.)
**
**  ResourceInfo::ResourceId
**
**    The resource this is for. Mostly redundant.
**
**  ResourceInfo::FinalResource
**
**    The resource is converted to this at the depot. Useful for
**    a fisherman who harvests fish, but it all turns to food at the
**    depot.
**
**  ResourceInfo::WaitAtResource
**
**    Cycles the unit waits while inside a resource.
**
**  ResourceInfo::ResourceStep
**
**    The unit makes so-called mining cycles. Each mining cycle
**    it does some sort of animation and gains ResourceStep
**    resources. You can stop after any number of steps.
**    when the quantity in the harvester reaches the maximum
**    (ResourceCapacity) it will return home. I this is 0 then
**    it's considered infinity, and ResourceCapacity will now
**    be the limit.
**
**  ResourceInfo::ResourceCapacity
**
**    Maximum amount of resources a harvester can carry. The
**    actual amount can be modified while unloading.
**
**  ResourceInfo::LoseResources
**
**    Special lossy behaviour for loaded harvesters. Harvesters
**    with loads other than 0 and ResourceCapacity will lose their
**    cargo on any new order.
**
**  ResourceInfo::WaitAtDepot
**
**    Cycles the unit waits while inside the depot to unload.
**
**  ResourceInfo::TerrainHarvester
**
**    The unit will harvest terrain. For now this only works
**    for wood. maybe it could be made to work for rocks, but
**    more than that requires a tileset rewrite.
**  @todo more configurable.
**
*/

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

static std::vector<CUnitType *> UnitTypes;   /// unit-types definition
static std::map<std::string, std::unique_ptr<CUnitType>, std::less<>> UnitTypeMap;

const std::vector<CUnitType *> &getUnitTypes()
{
	return UnitTypes;
}

/**
**  Next unit type are used hardcoded in the source.
**
**  @todo find a way to make it configurable!
*/
CUnitType *UnitTypeHumanWall;       /// Human wall
CUnitType *UnitTypeOrcWall;         /// Orc wall

/**
**  Default incomes for a new player.
*/
int DefaultIncomes[MaxCosts];

/**
**  Default action for the resources.
*/
std::string DefaultActions[MaxCosts];

/**
**  Default names for the resources.
*/
std::string DefaultResourceNames[MaxCosts];

/**
**  Default amounts for the resources.
*/
int DefaultResourceAmounts[MaxCosts];

/**
**  Default max amounts for the resources.
*/
int DefaultResourceMaxAmounts[MaxCosts];

/**
**  Default names for the resources.
*/
std::string ExtraDeathTypes[ANIMATIONS_DEATHTYPES];

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

int GetResourceIdByName(std::string_view resourceName)
{
	for (unsigned int res = 0; res < MaxCosts; ++res) {
		if (resourceName == DefaultResourceNames[res]) {
			return res;
		}
	}
	return -1;
}

int GetResourceIdByName(lua_State *l, std::string_view resourceName)
{
	const int res = GetResourceIdByName(resourceName);
	if (res == -1) {
		LuaError(l, "Resource not found: %s", resourceName.data());
	}
	return res;
}

PixelSize CUnitType::GetPixelSize() const
{
	return PixelSize(TileWidth * PixelTileSize.x, TileHeight * PixelTileSize.y);
}

bool CUnitType::CheckUserBoolFlags(const std::vector<ECondition> &BoolFlags) const
{
	if (BoolFlags.empty()) {
		return true;
	}
	for (unsigned int i = 0; i < UnitTypeVar.GetNumberBoolFlag(); ++i) { // User defined flags
		if (BoolFlags[i] != ECondition::Ignore
		    && ((BoolFlags[i] == ECondition::ShouldBeTrue) ^ (this->BoolFlag[i].value))) {
			return false;
		}
	}
	return true;
}

bool CUnitType::CanMove() const
{
	return Animations && !Animations->Move.empty();
}

bool CUnitType::CanSelect(EGroupSelectionMode mode) const
{
	if (!BoolFlag[ISNOTSELECTABLE_INDEX].value) {
		switch (mode) {
			case EGroupSelectionMode::SelectableByRectangleOnly:
				return BoolFlag[SELECTABLEBYRECTANGLE_INDEX].value;
			case EGroupSelectionMode::NonSelectableByRectangleOnly:
				return !BoolFlag[SELECTABLEBYRECTANGLE_INDEX].value;
			default:
			case EGroupSelectionMode::SelectAll:
				return true;
		}
	}
	return false;
}

void UpdateUnitStats(CUnitType &type, int reset)
{
	if (reset) {
		type.MapDefaultStat = type.DefaultStat;
		for (int player = 0; player < PlayerMax; ++player) {
			type.Stats[player] = type.MapDefaultStat;
		}
		type.MapSound = type.Sound;
	}

	// Non-solid units can always be entered and they don't block anything
	if (type.BoolFlag[NONSOLID_INDEX].value) {
		if (type.Building) {
			if (type.BoolFlag[DECORATION_INDEX].value && type.MapDefaultStat.Variables[HP_INDEX].Max == 0) {
				// special case, a decoration with no HP
				// it cannot be placed *over* walls, rocks, forests, *under* buildings, or where no buildings are allowed
				type.MovementMask = MapFieldWall | MapFieldRocks | MapFieldForest | MapFieldBuilding | MapFieldNoBuilding;
				// it does not influence the field (anything can overbuild it later)
				type.FieldFlags = 0;
			} else {
				type.MovementMask = MapFieldLandUnit |
					MapFieldSeaUnit |
					MapFieldBuilding |
					MapFieldCoastAllowed |
					MapFieldWaterAllowed |
					MapFieldNoBuilding |
					MapFieldUnpassable;
				type.FieldFlags = MapFieldNoBuilding;
			}
		} else {
			type.MovementMask = 0;
			type.FieldFlags = 0;
		}
		return;
	}

	//  As side effect we calculate the movement flags/mask here.
	switch (type.MoveType) {
		case EMovement::Land: // on land
			type.MovementMask =
				MapFieldLandUnit |
				MapFieldSeaUnit |
				MapFieldBuilding | // already occupied
				MapFieldCoastAllowed |
				MapFieldWaterAllowed | // can't move on this
				MapFieldUnpassable;
			break;
		case EMovement::Fly: // in air
			type.MovementMask = MapFieldAirUnit; // already occupied
			break;
		case EMovement::Naval: // on water
			if (type.CanTransport()) {
				type.MovementMask =
					MapFieldLandUnit |
					MapFieldSeaUnit |
					MapFieldBuilding | // already occupied
					MapFieldLandAllowed; // can't move on this
				// Johns: MapFieldUnpassable only for land units?
			} else {
				type.MovementMask =
					MapFieldLandUnit |
					MapFieldSeaUnit |
					MapFieldBuilding | // already occupied
					MapFieldCoastAllowed |
					MapFieldLandAllowed | // can't move on this
					MapFieldUnpassable;
			}
			break;
		default:
			DebugPrint("Where moves this unit?\n");
			type.MovementMask = 0;
			break;
	}
	if (type.Building || type.BoolFlag[SHOREBUILDING_INDEX].value) {
		// Shore building is something special.
		if (type.BoolFlag[SHOREBUILDING_INDEX].value) {
			type.MovementMask =
				MapFieldLandUnit |
				MapFieldSeaUnit |
				MapFieldBuilding | // already occupied
				MapFieldLandAllowed; // can't build on this
		}
		type.MovementMask |= MapFieldNoBuilding;
		//
		// A little chaos, buildings without HP can be entered.
		// The oil-patch is a very special case.
		//
		if (type.MapDefaultStat.Variables[HP_INDEX].Max) {
			type.FieldFlags = MapFieldBuilding;
		} else {
			type.FieldFlags = MapFieldNoBuilding;
		}
	} else {
		switch (type.MoveType) {
			case EMovement::Land: type.FieldFlags = MapFieldLandUnit; break;
			case EMovement::Fly: type.FieldFlags = MapFieldAirUnit; break;
			case EMovement::Naval: type.FieldFlags = MapFieldSeaUnit; break;
			default:
				DebugPrint("Where moves this unit?\n");
				type.FieldFlags = 0;
				break;
		}
	}
}


/**
**  Update the player stats for changed unit types.
**  @param reset indicates wether default value should be set to each stat (level, upgrades)
*/
void UpdateStats(int reset)
{
	// Update players stats
	for (CUnitType *type : UnitTypes) {
		UpdateUnitStats(*type, reset);
	}
}

/**
**  Save state of an unit-stats to file.
**
**  @param stats  Unit-stats to save.
**  @param ident  Unit-type ident.
**  @param plynr  Player number.
**  @param file   Output file.
*/
static bool SaveUnitStats(const CUnitStats &stats, const CUnitType &type, int plynr,
						  CFile &file)
{
	Assert(plynr < PlayerMax);

	if (stats == type.DefaultStat) {
		return false;
	}
	file.printf("DefineUnitStats(\"%s\", %d, {\n  ", type.Ident.c_str(), plynr);
	for (unsigned int i = 0; i < UnitTypeVar.GetNumberVariable(); ++i) {
		file.printf("\"%s\", {Value = %d, Max = %d, Increase = %d%s},\n  ",
		            UnitTypeVar.VariableNameLookup[i].data(),
		            stats.Variables[i].Value,
		            stats.Variables[i].Max,
		            stats.Variables[i].Increase,
		            stats.Variables[i].Enable ? ", Enable = true" : "");
	}
	file.printf("\"costs\", {");
	for (unsigned int i = 0; i < MaxCosts; ++i) {
		if (i) {
			file.printf(" ");
		}
		file.printf("\"%s\", %d,", DefaultResourceNames[i].c_str(), stats.Costs[i]);
	}
	file.printf("},\n\"storing\", {");
	for (unsigned int i = 0; i < MaxCosts; ++i) {
		if (i) {
			file.printf(" ");
		}
		file.printf("\"%s\", %d,", DefaultResourceNames[i].c_str(), stats.Storing[i]);
	}
	file.printf("},\n\"improve-production\", {");
	for (unsigned int i = 0; i < MaxCosts; ++i) {
		if (i) {
			file.printf(" ");
		}
		file.printf("\"%s\", %d,", DefaultResourceNames[i].c_str(), stats.ImproveIncomes[i]);
	}
	file.printf("}})\n");
	return true;
}

/**
**  Save state of the unit-type table to file.
**
**  @param file  Output file.
*/
void SaveUnitTypes(CFile &file)
{
	file.printf("\n--- -----------------------------------------\n");
	file.printf("--- MODULE: unittypes\n\n");

	// Save all stats
	for (const CUnitType *type : UnitTypes) {
		bool somethingSaved = false;

		for (int j = 0; j < PlayerMax; ++j) {
			if (Players[j].Type != PlayerTypes::PlayerNobody) {
				somethingSaved |= SaveUnitStats(type->Stats[j], *type, j, file);
			}
		}
		if (somethingSaved) {
			file.printf("\n");
		}
	}
}

/**
**  Find unit-type by identifier.
**
**  @param ident  The unit-type identifier.
**
**  @return       Unit-type pointer.
*/
CUnitType *UnitTypeByIdentOrNull(std::string_view ident)
{
	auto ret = UnitTypeMap.find(ident);
	return ret != UnitTypeMap.end() ? (*ret).second.get() : nullptr;
}

CUnitType &UnitTypeByIdent(std::string_view ident)
{
	if (CUnitType *type = UnitTypeByIdentOrNull(ident)) {
		return *type;
	}
	// `ident` is a string_view and is NOT guaranteed null-terminated, so `%s` with .data() reads
	// past the end whenever the view is a slice of a longer buffer. Print the exact length instead.
	ErrorPrint("Unknown unitType '%.*s'\n", (int) ident.size(), ident.data());
	ExitFatal(1);
}

/**
**  Allocate an empty unit-type slot or return existing one.
**
**  @param ident  Identifier to identify the slot.
**
**  @return       New allocated (zeroed) unit-type pointer and redefined flag.
*/
std::pair<CUnitType *, bool> NewUnitTypeSlot(std::string_view ident)
{
	if (auto ret = UnitTypeMap.find(ident); ret != UnitTypeMap.end()) {
		return {(*ret).second.get(), true};
	}

	size_t new_bool_size = UnitTypeVar.GetNumberBoolFlag();
	std::unique_ptr<CUnitType> type = std::make_unique<CUnitType>();

	type->Slot = UnitTypes.size();
	type->Ident = ident;
	type->BoolFlag.resize(new_bool_size);

	type->DefaultStat.Variables = UnitTypeVar.Variable;

	UnitTypes.push_back(type.get());

	UnitTypeMap[std::string(ident)] = std::move(type);
	return {UnitTypes.back(), false};
}

/**
**  Draw unit-type on map.
**
**  @param type    Unit-type pointer.
**  @param sprite  Sprite to use for drawing
**  @param player  Player number for color substitution.
**  @param frame   Animation frame of unit-type.
**  @param screenPos  Screen pixel (top left) position to draw unit-type.
**
**  @todo  Do screen position calculation in high level.
**         Better way to handle in x mirrored sprites.
*/
/**
**  World HD (P2): lazily load a building's HD atlas beside its classic sprite PNG.
**
**  The classic sprite path is `type.File` (e.g. tilesets/summer/human/buildings/town_hall.png). The
**  HD base atlas is baked alongside it with the same dir+basename and a `.hdatlas` extension; the
**  optional player-colour mask is `<basename>_team.hdatlas`. Attempted once per type (guarded by
**  `type.HDSpriteTried`); on any failure `type.HDSprite` stays null and the caller keeps the classic
**  CPlayerColorGraphic path. CAtlasGraphic::Load resolves the path through LibraryFileName itself.
*/
// WargusHD: map from a unit's classic sprite path (type.File) to its native Remastered atlas + frame
// prefix (e.g. "human/units/footman.png" -> {units_human_common, grunt}). Baked offline from the unit
// roster into hd_units_map.json (deployed beside the scripts) so the engine reads the Remastered unit
// atlases directly, with no per-unit conversion. Loaded once, lazily.
struct HDUnitMapEntry { std::string atlas; std::string prefix; };
static std::unordered_map<std::string, HDUnitMapEntry> g_hdUnitMap;
static bool g_hdUnitMapTried = false;
// ART-CYCLES: families whose picture moves while the unit stands (Dark Portal and Runestone glow,
// winter Church / Goblin Alchemist, sunk-ship rings, Circle of Power). Keyed by frame prefix; the
// numbers are optional keys of hd_units_map.json: "cycle_frames" (pictures in the loop) and either
// "cycle" (the original's palette-cycle record, the sprites.json value: the step comes from
// palette_cycle.h, 20 ms of real time per tick) or "cycle_step10" (game cycles per picture, times ten,
// as measured on the original; the Circle of Power, whose record 0 is not ported).
// No key, no cycle.
struct HDCycle { int frames = 0; int step10 = 0; int record = -1; };
static std::unordered_map<std::string, HDCycle> g_hdCycle;
// SKELETON-SIZE: families whose sheet is stored reduced (Skeleton, Eye of Kilrogg). Keyed by frame
// prefix; the number is the optional key "canvas" of hd_units_map.json: the canvas the family has
// by the rule 6.75 * classic cell + 122, to which the frame's own canvas is stretched at draw time.
// No key, no stretch.
static std::unordered_map<std::string, int> g_hdCanvas;

static void LoadHDUnitMap()
{
	if (g_hdUnitMapTried) {
		return;
	}
	g_hdUnitMapTried = true;
	// HD-MISS-INVISIBLE: this map is the single point of failure for all unit rendering -- none of
	// its mapped paths has a classic PNG to fall back to, so anything that does not resolve renders
	// nothing. Every early return below reports with ErrorPrint, not DebugPrint: it has to be
	// visible without a runtime flag.
	const fs::path resolved = LibraryFileName("hd_units_map.json");
	if (resolved.empty()) {
		ErrorPrint("hd_units_map.json NOT FOUND -- no unit will draw its HD sprite\n");
		return;
	}
	std::error_code ec;
	const uintmax_t sz = fs::file_size(resolved, ec);
	if (ec || sz == 0 || sz > (uintmax_t) 8 * 1024 * 1024) {
		ErrorPrint("hd_units_map.json unusable size (%s, %llu bytes) -- map not loaded\n",
		           resolved.string().c_str(), (unsigned long long) sz);
		return;
	}
	std::string text;
	{
		auto fp = std::make_unique<CFile>();
		if (fp->open(resolved.string().c_str(), CL_OPEN_READ) == -1) {
			ErrorPrint("hd_units_map.json could not be opened (%s) -- map not loaded\n",
			           resolved.string().c_str());
			return;
		}
		text.resize((size_t) sz);
		const bool ok = (size_t) fp->read(&text[0], (size_t) sz) == (size_t) sz;
		fp->close();
		if (!ok) {
			ErrorPrint("hd_units_map.json short read (expected %llu bytes) -- map not loaded\n",
			           (unsigned long long) sz);
			return;
		}
	}
	content::JsonValue root;
	std::string jerr;
	if (!content::parseJson(text, root, jerr) || !root.isObject() || !root.object) {
		ErrorPrint("hd_units_map.json did not parse as an object (%s) -- map not loaded\n",
		           jerr.c_str());
		return;
	}
	int skipped = 0;
	for (const auto &kv : *root.object) {
		const content::JsonValue *a = kv.second.find("atlas");
		const content::JsonValue *p = kv.second.find("prefix");
		if (a && p) {
			g_hdUnitMap[kv.first] = { a->asString(), p->asString() };
			const content::JsonValue *cf = kv.second.find("cycle_frames");
			const content::JsonValue *cs = kv.second.find("cycle_step10");
			const content::JsonValue *cr = kv.second.find("cycle");
			if (cf && cr && cf->asInt() > 1 && cr->asInt() >= 0 && cr->asInt() < palette_cycle::RecordCount) {
				g_hdCycle[p->asString()] = { (int) cf->asInt(), 0, (int) cr->asInt() };
			} else if (cf && cs && cf->asInt() > 1 && cs->asInt() > 0) {
				g_hdCycle[p->asString()] = { (int) cf->asInt(), (int) cs->asInt() };
			}
			const content::JsonValue *cv = kv.second.find("canvas");
			if (cv && cv->asInt() > 0) {
				g_hdCanvas[p->asString()] = (int) cv->asInt();
			}
		} else {
			++skipped;
			ErrorPrint("hd_units_map.json entry '%s' has no atlas/prefix -- skipped, it will draw nothing\n",
			           kv.first.c_str());
		}
	}
	ErrorPrint("hd_units_map.json loaded: %d entries, %d skipped\n", (int) g_hdUnitMap.size(), skipped);
}

// Shared native atlas cache: many unit types map to the same atlas (e.g. every human land unit ->
// units_human_common_sprites), so load each atlas once and share the pointer.
static std::unordered_map<std::string, std::shared_ptr<CAtlasGraphic>> &NativeAtlasCache()
{
	static std::unordered_map<std::string, std::shared_ptr<CAtlasGraphic>> cache;
	return cache;
}

// decoded: the atlas page decoded ahead by the preload (taken and freed here), or null.
static std::shared_ptr<CAtlasGraphic> GetSharedNativeAtlas(const std::string &stem, bool teamMask = false,
                                                           const AtlasPageDecode *decoded = nullptr)
{
	auto &cache = NativeAtlasCache();
	auto it = cache.find(stem);
	if (it != cache.end()) {
		if (decoded && decoded->surface) {
			SDL_FreeSurface(decoded->surface);
		}
		return it->second;
	}
	auto atlas = std::make_shared<CAtlasGraphic>();
	if (!atlas->LoadTexturePacker("Data/Art/hd/unit/" + stem + ".json", teamMask, false, nullptr, false,
	                              nullptr, nullptr, nullptr, decoded)) {
		atlas.reset(); // remember the failure (null) so we don't retry every unit
	}
	cache[stem] = atlas;
	return atlas;
}

// EFFECT-ATLAS-LOADED-TWICE: the effects (missile.cpp) kept a cache of their own and loaded the
// biome's common atlas a second time at the first shot of a fight. They share this one now.
std::shared_ptr<CAtlasGraphic> GetSharedHDAtlas(const std::string &stem)
{
	return GetSharedNativeAtlas(stem);
}

// The answers of ResolveHDForFile, per sprite path (a function-local map until the release below
// had to reach it).
static std::map<std::string, std::pair<std::shared_ptr<CAtlasGraphic>, std::string>> &HDFileCache()
{
	static std::map<std::string, std::pair<std::shared_ptr<CAtlasGraphic>, std::string>> cache;
	return cache;
}

// NG15/NG22: resolve the HD atlas+prefix for an ARBITRARY sprite path. The per-type cache below is
// keyed on type.File, which is correct for the unit's own sprite but wrong for the CARRY sprites: a
// loaded worker is drawn from resinfo->SpriteWhenLoaded (peasant_with_gold.png / _with_wood.png),
// and those have their own roster entries (peong / peonl). Without this the HD branch always drew
// the empty-handed prefix, so gold and lumber frames were never selected.
// Results are cached per path; peon/peong/peonl each have 65 contiguous frames, so the absolute
// frame index maps 1:1 between them and no index translation is needed.
static int HDFrameByNumber(const CAtlasGraphic &atlas, const std::string &prefix, int n,
                           const char *suffix);

// LOAD-CRASH-3473 (work/LOAD_CRASH_3473.md): the atlas stem a roster entry draws from on this map.
// A unit whose script file belongs to another tileset (Wasteland Town Hall uses the summer file)
// takes the family from the map's own biome atlas when that atlas holds it, so a Wasteland map does
// not decode the forest atlas in the middle of play (3.4.73: 22-26 s at the first frame after a
// load). The preload holds the map's biome atlases, so the check costs no decode. When the map's
// atlas lacks the family, the roster's atlas stays.
static std::string HDAtlasStemForMap(const HDUnitMapEntry &entry)
{
	const std::string stem = hdatlas::stemInBiome(entry.atlas, CurrentEffectTileset());
	if (stem != entry.atlas) {
		const std::shared_ptr<CAtlasGraphic> own = GetSharedNativeAtlas(stem + "_sprites");
		if (own && own->IsLoaded() && HDFrameByNumber(*own, entry.prefix, 0, "") >= 0) {
			return stem;
		}
	}
	return entry.atlas;
}

bool HDAtlasResident(const std::string &stem)
{
	const auto &cache = NativeAtlasCache();
	const auto it = cache.find(stem);
	return it != cache.end() && it->second && it->second->IsLoaded();
}

// PRELOAD-PLAN: the atlas stem (no _sprites/_masks) `file` draws from on this map, decided as
// HDAtlasStemForMap decides it but from the cache alone: nothing is loaded here. A map-biome
// stem that is not in the cache yet comes back as itself (it is what the resolver tries first).
// Empty when the file has no roster entry.
static std::string HDPlannedStemForFile(const std::string &file)
{
	LoadHDUnitMap();
	std::string key = file;
	for (char &c : key) { c = (char) std::tolower((unsigned char) c); }
	const auto mit = g_hdUnitMap.find(key);
	if (mit == g_hdUnitMap.end()) {
		return std::string();
	}
	const std::string stem = hdatlas::stemInBiome(mit->second.atlas, CurrentEffectTileset());
	if (stem == mit->second.atlas) {
		return stem;
	}
	const auto &cache = NativeAtlasCache();
	const auto it = cache.find(stem + "_sprites");
	if (it == cache.end()) {
		return stem;
	}
	if (it->second && it->second->IsLoaded() && HDFrameByNumber(*it->second, mit->second.prefix, 0, "") >= 0) {
		return stem;
	}
	return mit->second.atlas;
}

// CLASSIC-SHEET-LOOKUPS: `file` is drawn from a native HD atlas that is loaded now, so its classic
// sheet (which the game data does not hold; the classic view draws from the classic atlas) is
// never searched for. Nothing is loaded here.
static bool HDArtCoversFile(const std::string &file)
{
	const std::string stem = HDPlannedStemForFile(file);
	return !stem.empty() && HDAtlasResident(stem + "_sprites");
}

static bool ResolveHDForFile(const std::string &file,
                             std::shared_ptr<CAtlasGraphic> &outSprite,
                             std::string &outPrefix)
{
	auto &cache = HDFileCache();
	if (file.size() < 4 || file.compare(file.size() - 4, 4, ".png") != 0) {
		return false;
	}
	std::string key = file;
	for (char &c : key) { c = (char) std::tolower((unsigned char) c); }
	auto cit = cache.find(key);
	if (cit != cache.end()) {
		outSprite = cit->second.first;
		outPrefix = cit->second.second;
		return outSprite && outSprite->IsLoaded();
	}
	LoadHDUnitMap();
	std::shared_ptr<CAtlasGraphic> sprites;
	std::string prefix;
	auto mit = g_hdUnitMap.find(key);
	if (mit != g_hdUnitMap.end()) {
		sprites = GetSharedNativeAtlas(HDAtlasStemForMap(mit->second) + "_sprites");
		prefix = mit->second.prefix;
	}
	cache[key] = {sprites, prefix};
	outSprite = sprites;
	outPrefix = prefix;
	return sprites && sprites->IsLoaded();
}

// Frame lookup by number, in the spellings the original's atlases use:
//   "<prefix>_<N>"                 the common case
//   "<prefix>_f<N>"                Circle of Power (prefix carries the "_0")
//   "<prefix>_<N>_f0"              a frame that ships only as a cycle (winter Church, winter Goblin
//                                  Alchemist: s_church_0_f0..f2, s_inv_0_f0..f2) -- first cycle frame
//   "<prefix>_<N>_f0_b<biome>"     the same, with one picture per biome (sunk-ship remains:
//                                  death_30_f0_bf / _bl / _bs / _bx)
// Each later spelling is tried only when every earlier one is absent, so nothing that resolves
// today changes. `suffix` is appended to all of them ("" for the body, "_team" for the mask).
static int HDFrameByNumber(const CAtlasGraphic &atlas, const std::string &prefix, int n,
                           const char *suffix)
{
	const std::string num = std::to_string(n);
	int f = atlas.FrameByName((prefix + "_" + num + suffix).c_str());
	if (f < 0) {
		f = atlas.FrameByName((prefix + "_f" + num + suffix).c_str());
	}
	if (f < 0) {
		f = atlas.FrameByName((prefix + "_" + num + "_f0" + suffix).c_str());
	}
	if (f < 0) {
		const std::string ts = CurrentEffectTileset();
		const char *biome = ts == "forest" ? "_bf" : ts == "swamp" ? "_bl" : ts == "winter" ? "_bs"
		                    : ts == "xswamp" ? "_bx" : nullptr;
		if (biome) {
			f = atlas.FrameByName((prefix + "_" + num + "_f0" + biome + suffix).c_str());
		}
	}
	return f;
}

// ART-CYCLES: step `k` of the cycle that belongs to frame `n` of a family, in the spellings the
// original's atlases use: "<prefix>_<N>_f<k>" (portal, Runestone, winter Church), the same with
// the biome letter (sunk-ship rings), and "<prefix>_f<k>" for a family whose prefix already
// carries the frame number (Circle of Power: vcircle_0_f0..).
static int HDCycleFrame(const CAtlasGraphic &atlas, const std::string &prefix, int n, int k)
{
	const std::string base = prefix + "_" + std::to_string(n) + "_f" + std::to_string(k);
	int f = atlas.FrameByName(base.c_str());
	if (f < 0) {
		const std::string ts = CurrentEffectTileset();
		const char *biome = ts == "forest" ? "_bf" : ts == "swamp" ? "_bl" : ts == "winter" ? "_bs"
		                    : ts == "xswamp" ? "_bx" : nullptr;
		if (biome) {
			f = atlas.FrameByName((base + biome).c_str());
		}
	}
	if (f < 0 && n == 0) {
		f = atlas.FrameByName((prefix + "_f" + std::to_string(k)).c_str());
	}
	return f;
}

static void EnsureHDUnitSprite(const CUnitType &type)
{
	// HD-ART-STALE-TILESET: the next mission of a campaign runs in the same process and its
	// tileset script gives a building another File (tilesets/winter/... -> tilesets/summer/...).
	// The art resolved for the old File must not be kept: a summer map drew winter buildings.
	if (type.HDSpriteTried && type.HDSpriteFile == type.File) {
		return;
	}
	type.HDSpriteTried = true;
	type.HDSpriteFile = type.File;
	type.HDSprite.reset();
	type.HDSpriteTeam.reset();
	type.HDSpritePrefix.clear();

	const std::string &file = type.File;
	if (file.size() < 4 || file.compare(file.size() - 4, 4, ".png") != 0) {
		return;
	}

	// Native Remastered atlas first (no conversion): look the unit's sprite path up in the roster map,
	// share one atlas per (race/tileset) group, and address frames by "<prefix>_<N>".
	LoadHDUnitMap();
	std::string key = file;
	for (char &c : key) { c = (char) std::tolower((unsigned char) c); }
	auto mit = g_hdUnitMap.find(key);
	if (mit != g_hdUnitMap.end()) {
		const std::string stem = HDAtlasStemForMap(mit->second);
		auto sprites = GetSharedNativeAtlas(stem + "_sprites");
		// NG8 diagnostic (one-shot per type): report how buildings resolve their HD atlas so a
		// classic-scale fallback can be traced to a map miss vs an atlas-load miss vs a frame miss.
		if (key.find("buildings/") != std::string::npos) {
			DebugPrint("NG8 building file='%s' -> atlas='%s_sprites' prefix='%s' loaded=%d\n",
			           key.c_str(), stem.c_str(), mit->second.prefix.c_str(),
			           (int) (sprites && sprites->IsLoaded()));
		}
		if (sprites && sprites->IsLoaded()) {
			type.HDSprite = sprites;
			type.HDSpritePrefix = mit->second.prefix;
			type.HDSpriteTeam = GetSharedNativeAtlas(stem + "_masks", /*teamMask=*/true); // optional
			return;
		}
	} else if (key.find("buildings/") != std::string::npos) {
		DebugPrint("NG8 building file='%s' NOT found in hd_units_map\n", key.c_str());
	}

	// Legacy fallback: a per-unit .hdatlas baked beside the classic PNG (numeric frame names).
	const std::string base = file.substr(0, file.size() - 4);
	auto atlas = std::make_shared<CAtlasGraphic>();
	if (!atlas->Load(base + ".hdatlas")) {
		return; // absent -> classic path (graceful degradation)
	}
	type.HDSprite = std::move(atlas);
	auto team = std::make_shared<CAtlasGraphic>();
	if (team->Load(base + "_team.hdatlas")) {
		type.HDSpriteTeam = std::move(team);
	}
}

// CULL-HD-EXTENT: the canvas DrawUnitType centres on the footprint, from frame 0 of the family (a
// family's frames share one canvas), stretched to the rule canvas where the family is stored
// reduced (SKELETON-SIZE), exactly as hdStoredCanvasScale does at draw time.
void GetHDUnitCanvas(const CUnitType &type, int &w, int &h)
{
	EnsureHDUnitSprite(type);
	const CAtlasGraphic *atlas = type.HDSprite.get();
	if (atlas != type.HDCanvasOf) {
		type.HDCanvasOf = atlas;
		type.HDCanvasW = 0;
		type.HDCanvasH = 0;
		if (atlas && atlas->IsLoaded()) {
			const std::string &prefix = type.HDSpritePrefix;
			const int f = prefix.empty() ? atlas->FrameByName("0") : HDFrameByNumber(*atlas, prefix, 0, "");
			if (f >= 0) {
				const hdatlas::HdFrameRecord &rec = atlas->Frame(f);
				int rule = 0;
				if (!prefix.empty()) {
					const auto cv = g_hdCanvas.find(prefix);
					if (cv != g_hdCanvas.end()) {
						rule = cv->second;
					}
				}
				const float k = hdatlas::hdStoredCanvasScale(1.0f, rule, rec.cw);
				type.HDCanvasW = (int) std::ceil(rec.cw * k);
				type.HDCanvasH = (int) std::ceil(rec.ch * k);
			}
		}
	}
	w = type.HDCanvasW;
	h = type.HDCanvasH;
}

// NG21: resolve the HD construction-scaffold atlas for a building. The scaffold frames live in the
// per-biome COMMON atlas (build_1_0/1, or s_build1_0/1 in winter), but the biome cannot be derived
// from the construction file: summer, swamp and wasteland all share
// neutral/buildings/land_construction_site.png and only winter differs. It also cannot be derived by
// reading the biome out of the path, because the roster's biome tokens are transposed
// (tilesets/swamp -> units_*_xswamp, tilesets/wasteland -> units_*_swamp). So take the BUILDING's own
// resolved atlas (units_<race>_<biome>) and swap the race token for "common" -- that carries the
// transposition across for free.
std::shared_ptr<CAtlasGraphic> GetHDConstructionAtlas(const CUnitType &building, std::string &outPrefix)
{
	LoadHDUnitMap();
	// A construction that has its own art in the original (shipyard, foundry, refinery, oil
	// platform: families ship1 / found1 / ref1 / oplat1) is looked up by its own file, like any
	// other sprite path. Only a construction without a map entry takes the land scaffold below.
	if (building.Construction && !building.Construction->File.File.empty()) {
		std::shared_ptr<CAtlasGraphic> own;
		std::string ownPrefix;
		if (ResolveHDForFile(building.Construction->File.File, own, ownPrefix)) {
			outPrefix = ownPrefix;
			return own;
		}
	}
	std::string key = building.File;
	for (char &c : key) { c = (char) std::tolower((unsigned char) c); }
	auto it = g_hdUnitMap.find(key);
	if (it == g_hdUnitMap.end()) {
		return nullptr;
	}
	const std::string atlas = HDAtlasStemForMap(it->second); // units_<race>_<biome>, the map's biome
	const size_t a = atlas.find('_');
	const size_t b = (a == std::string::npos) ? std::string::npos : atlas.find('_', a + 1);
	if (b == std::string::npos) {
		return nullptr;
	}
	const std::string biome = atlas.substr(b + 1);
	outPrefix = (biome == "winter") ? "s_build1" : "build_1";
	return GetSharedNativeAtlas("units_common_" + biome + "_sprites");
}

// NG82: a flying unit's ground shadow. Only the eight airborne types declare a shadow
// (units.lua ShadowDefinition), and the art ships as shadow_0/1/2 in every per-tileset common
// atlas. Ground units are untouched: their shadow is baked into their own sprite.
//
// The construction-site trick above (take the building's own atlas and swap the race token for
// "common") does not work here: buildings live in units_<race>_<biome>, but mobile units live
// in units_<race>_common, where "common" is the tileset-independent slot rather than a biome.
// The shadow is cast on the map, so it takes the map's tileset, resolved by the one shared
// CurrentEffectTileset() (missile.h).
//
// The original gives every flying unit the same shadow, so its size is a single measured
// world constant (unit_draw.cpp); this function returns only the atlas.
std::shared_ptr<CAtlasGraphic> GetHDShadowAtlas(const CUnitType &type)
{
	// Every exit reports ONCE per type. Two cycles were spent on this path reading silence as
	// "drew correctly": a wrong atlas name simply failed to load and said nothing. A shadow that
	// cannot resolve is a defect, so it is audible -- same rule as HD-MISS-INVISIBLE above.
	static std::set<std::string> reportedShadowAtlas;
	if (type.ShadowFile.empty()) {
		return nullptr;                                   // no file-based shadow declared: not a defect
	}
	const std::string ts = CurrentEffectTileset();
	if (ts.empty()) {
		if (reportedShadowAtlas.insert("ts|" + type.Ident).second) {
			ErrorPrint("HD shadow: '%s' -- tileset unknown (Map.Tileset.ImageFile '%s'), no shadow\n",
			           type.Ident.c_str(), Map.Tileset.ImageFile.c_str());
		}
		return nullptr;
	}
	const std::string stem = "units_common_" + ts + "_sprites";
	auto atlas = GetSharedNativeAtlas(stem);
	if (!atlas || !atlas->IsLoaded()) {
		if (reportedShadowAtlas.insert("load|" + type.Ident).second) {
			ErrorPrint("HD shadow: '%s' -- atlas '%s' did not load, no shadow\n",
			           type.Ident.c_str(), stem.c_str());
		}
		return nullptr;
	}
	if (reportedShadowAtlas.insert("ok|" + type.Ident).second) {
		ErrorPrint("HD shadow: '%s' -> atlas '%s'\n", type.Ident.c_str(), stem.c_str());
	}
	return atlas;
}

// Classic view: the group of a sprite file in the classic atlas, or -1. First the classic sprite
// table (classic_sprite_map.h); a file the table lacks, or whose group the atlas lacks, is looked up
// in the HD sprite map (read only), whose entry gives the names to try (hdatlas::classicGroupNames).
static int ResolveClassicGroup(const std::string &file, const CAtlasGraphic &atlas)
{
	if (const classicmap::ClassicSpriteRow *row = classicmap::classicSpriteRow(file)) {
		const int group = atlas.ClassicGroupByName(row->group);
		if (group >= 0) {
			return group;
		}
		ErrorPrint("Classic sprite: '%s' -> '%s' is not in the classic atlas, trying the HD map\n",
		           file.c_str(), row->group);
	} else {
		ErrorPrint("Classic sprite: '%s' is not in the classic sprite table, trying the HD map\n", file.c_str());
	}
	LoadHDUnitMap();
	std::string key = file;
	for (char &c : key) { c = (char) std::tolower((unsigned char) c); }
	const auto mit = g_hdUnitMap.find(key);
	if (mit == g_hdUnitMap.end()) {
		return -1;
	}
	for (const std::string &name : hdatlas::classicGroupNames(mit->second.atlas, mit->second.prefix)) {
		const int group = atlas.ClassicGroupByName(name);
		if (group >= 0) {
			return group;
		}
	}
	return -1;
}

// Classic view: draw the body from the classic 8-bit art of the map's classic atlas. The cell is
// centred on the unit's tile box, one classic pixel = PixelTileSize * zoom / ClassicTilePx screen
// pixels (the density of the classic terrain), team-colour pixels through the player's palette run,
// negative frames mirrored. False when anything is missing: the caller then draws the Remastered art.
static bool DrawClassicUnitType(const CUnitType &type, const CPlayerColorGraphic *sprite, int colorIndex,
                                int frame, const PixelPos &screenPos)
{
	static std::set<std::string> reportedClassicMiss; // own set: an HD miss must not silence a classic one
	CAtlasGraphic *atlas = Map.ClassicTileAtlas.get();
	if (!atlas || !atlas->ClassicSerial()) {
		return false;
	}
	int group;
	const std::string *file = &type.File;
	if (sprite && !sprite->File.empty() && sprite->File != type.File) {
		// A carry sprite (worker with gold / lumber) has its own entry, as in the HD branch.
		static std::map<std::string, std::pair<unsigned, int>> carry;
		static std::string carryFile;
		carryFile = sprite->File.string();
		file = &carryFile;
		auto it = carry.find(carryFile);
		if (it == carry.end() || it->second.first != atlas->ClassicSerial()) {
			it = carry.insert_or_assign(carryFile, std::make_pair(atlas->ClassicSerial(),
			                                                      ResolveClassicGroup(carryFile, *atlas))).first;
		}
		group = it->second.second;
	} else {
		if (type.ClassicSpriteSerial != atlas->ClassicSerial() || type.ClassicSpriteFile != type.File) {
			type.ClassicSpriteSerial = atlas->ClassicSerial();
			type.ClassicSpriteFile = type.File;
			type.ClassicSpriteGroup = ResolveClassicGroup(type.File, *atlas);
		}
		group = type.ClassicSpriteGroup;
	}
	if (group < 0) {
		if (reportedClassicMiss.insert("group|" + *file).second) {
			ErrorPrint("Classic sprite: '%s' has no classic group, it stays Remastered\n", file->c_str());
		}
		return false;
	}
	const int rampStart = (colorIndex >= 0 && colorIndex < (int) PlayerColorNames.size())
	                          ? hdatlas::classicRampStart(PlayerColorNames[colorIndex].c_str()) : -1;
	if (rampStart < 0) {
		if (reportedClassicMiss.insert("colour|" + std::to_string(colorIndex)).second) {
			ErrorPrint("Classic sprite: player colour %d has no palette run, its units stay Remastered\n",
			           colorIndex);
		}
		return false;
	}
	const CAtlasGraphic *cells = atlas->ClassicGroup(group, rampStart);
	bool mirror = false;
	const int n = hdatlas::classicFrame(frame, mirror);
	if (!cells || n >= cells->NumFrames() || cells->Frame(n).sw == 0) {
		if (reportedClassicMiss.insert("frame|" + *file + "#" + std::to_string(n)).second) {
			ErrorPrint("Classic sprite: '%s' frame %d is not in its classic group, it stays Remastered\n",
			           file->c_str(), n);
		}
		return false;
	}
	const float z = GpuWorldDrawZoom;
	const float s = PixelTileSize.x * z / ClassicTilePx;
	const float cx = screenPos.x + (type.TileWidth * PixelTileSize.x / 2.0f + type.Offset.x) * z;
	const float cy = screenPos.y + (type.TileHeight * PixelTileSize.y / 2.0f + type.Offset.y) * z;
	cells->DrawAtlasFrameCentred(n, cx, cy, s, mirror && type.Flip);
	return true;
}

bool DrawClassicLandSite(const CUnitType &building, int frame, const PixelPos &centre)
{
	CAtlasGraphic *atlas = Map.ClassicTileAtlas.get();
	if (!atlas || !atlas->ClassicSerial() || !building.Construction) {
		return false;
	}
	const char *name = hdatlas::classicLandSiteGroup(building.Construction->File.File, CurrentEffectTileset());
	if (!name) {
		return false;
	}
	static unsigned serial = 0;
	static const char *latchedName = nullptr;
	static int group = -1;
	if (serial != atlas->ClassicSerial() || latchedName != name) {
		serial = atlas->ClassicSerial();
		latchedName = name;
		group = atlas->ClassicGroupByName(name);
	}
	bool mirror = false;
	const int n = hdatlas::classicFrame(frame, mirror);
	// The site has no team-colour pixels, so any run gives the same cut.
	const CAtlasGraphic *cells = group >= 0 ? atlas->ClassicGroup(group, hdatlas::ClassicTeamFirst) : nullptr;
	if (!cells || n >= hdatlas::ClassicLandSiteFrames || n >= cells->NumFrames() || cells->Frame(n).sw == 0) {
		static std::set<std::string> reported;
		if (reported.insert(std::string(name) + "#" + std::to_string(n)).second) {
			ErrorPrint("Classic sprite: construction site '%s' frame %d is missing, it stays Remastered\n",
			           name, n);
		}
		return false;
	}
	const float z = GpuWorldDrawZoom;
	cells->DrawAtlasFrameCentred(n, centre.x + building.Offset.x * z, centre.y + building.Offset.y * z,
	                             PixelTileSize.x * z / ClassicTilePx, false);
	return true;
}

bool DrawClassicFlyerShadow(const CUnitType &type, const PixelPos &screenPos, int zDisplacement)
{
	CAtlasGraphic *atlas = Map.ClassicTileAtlas.get();
	if (!atlas || !atlas->ClassicSerial()) {
		return false;
	}
	static unsigned serial = 0;
	static int group = -1;
	if (serial != atlas->ClassicSerial()) {
		serial = atlas->ClassicSerial();
		group = atlas->ClassicGroupByName(hdatlas::ClassicFlyerShadowGroup);
	}
	const int n = hdatlas::ClassicFlyerShadowFrame;
	// The disc has no team-colour pixels, so any run gives the same cut.
	const CAtlasGraphic *cells = group >= 0 ? atlas->ClassicGroup(group, hdatlas::ClassicTeamFirst) : nullptr;
	if (!cells || n >= cells->NumFrames() || cells->Frame(n).sw == 0) {
		static unsigned reported = 0;
		if (reported != serial) {
			reported = serial;
			ErrorPrint("Classic sprite: flyer shadow '%s' frame %d is missing, it stays Remastered\n",
			           hdatlas::ClassicFlyerShadowGroup, n);
		}
		return false;
	}
	const hdatlas::ClassicFlyerShadowPlace p =
		hdatlas::classicFlyerShadowPlace(type.TileWidth, type.TileHeight, PixelTileSize.x, PixelTileSize.y,
		                                 type.Offset.x, type.Offset.y, zDisplacement, GpuWorldDrawZoom);
	cells->DrawAtlasFrameCentred(n, screenPos.x + p.cx, screenPos.y + p.cy, p.scale, false);
	return true;
}

void DrawUnitType(const CUnitType &type, CPlayerColorGraphic *sprite, int colorIndex, int frame, const PixelPos &screenPos)
{
	if (IsClassicView() && DrawClassicUnitType(type, sprite, colorIndex, frame, screenPos)) {
		return;
	}
	PixelPos pos = screenPos;
	// FIXME: move this calculation to high level.
	// These are NATIVE-pixel offsets that centre the sprite over the unit's tile footprint. On the
	// GPU-direct zoomed render (GpuWorldDrawZoom != 1) the sprite is drawn at Width*zoom and its
	// anchor (screenPos) is already zoom-scaled, so the centring delta must scale by the same zoom
	// or the sprite lands off its footprint. At GpuWorldDrawZoom==1 (native / offscreen / crisp
	// paths) this is byte-identical to the classic integer offset.
	int offX = -(type.Width - type.TileWidth * PixelTileSize.x) / 2 + type.Offset.x;
	int offY = -(type.Height - type.TileHeight * PixelTileSize.y) / 2 + type.Offset.y;
	if (GpuWorldDrawZoom != 1.0f) {
		offX = static_cast<int>(std::lround(offX * GpuWorldDrawZoom));
		offY = static_cast<int>(std::lround(offY * GpuWorldDrawZoom));
	}
	pos.x += offX;
	pos.y += offY;

	// --- World HD (P2): draw from a per-building .hdatlas when one is deployed beside the classic
	// PNG. Strictly additive: buildings without an atlas fall through to the unchanged classic path
	// below. Only the pixel blit changes here; the frame index, centring and screenPos are the same
	// values the classic path uses.
	EnsureHDUnitSprite(type);
	// NG15/NG22: the caller may hand us a DIFFERENT sprite than the type's own (the resource-carry
	// swap in CUnit::Draw). Prefer that sprite's own HD entry so peong/peonl are actually used.
	std::shared_ptr<CAtlasGraphic> hdSprite = type.HDSprite;
	std::string hdPrefix = type.HDSpritePrefix;
	if (sprite && !sprite->File.empty() && sprite->File != type.File) {
		std::shared_ptr<CAtlasGraphic> carry;
		std::string carryPrefix;
		if (ResolveHDForFile(sprite->File.string(), carry, carryPrefix)) {
			hdSprite = carry;
			hdPrefix = carryPrefix;
		}
	}
	if (hdSprite && hdSprite->IsLoaded()) {
		// Engine frame -> atlas frame is by numeric name; the atlas stores the absolute (unmirrored)
		// frame, mirroring is applied at draw time via the flip variant (matching the classic path,
		// which flips when type.Flip && frame < 0).
		const int absFrame = (frame < 0) ? (-frame - 1) : frame;
		// Native Remastered atlas frames are named "<prefix>_<N>" (e.g. "grunt_0"); the legacy .hdatlas
		// path uses bare numeric names. Build the right key from HDSpritePrefix.
		const std::string frameName = hdPrefix.empty()
		                                  ? std::to_string(absFrame)
		                                  : hdPrefix + "_" + std::to_string(absFrame);
		int f = hdSprite->FrameByName(frameName.c_str());
		const bool bodyByName = f >= 0;
		// NG29: a few native assets number their frames "<prefix>_f<N>" instead of "<prefix>_<N>"
		// (the Circle of Power ships as vcircle_0_f0..f7), and a few ship a frame only as a cycle
		// ("<prefix>_<N>_f0", optionally per biome). HDFrameByNumber tries them in order.
		if (f < 0 && !hdPrefix.empty()) {
			f = HDFrameByNumber(*hdSprite, hdPrefix, absFrame, "");
		}
		// ART-CYCLES: a family with a cycle shows step k of it, k taken from its palette-cycle record
		// (real time) or, without one, from the game cycle. Where the
		// frame also ships as a whole picture the step is drawn OVER it (glow); where it ships only
		// as a cycle the step IS the picture.
		int overlayFrame = -1;
		if (!hdPrefix.empty()) {
			const auto cyc = g_hdCycle.find(hdPrefix);
			if (cyc != g_hdCycle.end()) {
				const int k = cyc->second.record >= 0
				                  ? PaletteCycleStep(cyc->second.record) % cyc->second.frames
				                  : (int) ((GameCycle * 10UL / (unsigned long) cyc->second.step10)
				                           % (unsigned long) cyc->second.frames);
				const int cf = HDCycleFrame(*hdSprite, hdPrefix, absFrame, k);
				if (cf >= 0) {
					if (bodyByName) {
						overlayFrame = cf;
					} else {
						f = cf;
					}
				}
			}
		}
		// One line per file and frame that resolved through a later spelling: it tells which name a
		// unit is really drawn from (sunk-ship remains were invisible although a name resolved).
		if (!bodyByName && f >= 0 && !hdPrefix.empty()) {
			static std::set<std::string> told;
			if (told.insert(type.File + "#" + std::to_string(absFrame)).second) {
				ErrorPrint("HD frame by later spelling: %s prefix '%s' frame %d -> atlas frame %d\n",
				           type.File.c_str(), hdPrefix.c_str(), absFrame, f);
			}
		}
		// HD-MISS-INVISIBLE case B: the atlas loaded but neither spelling resolves, so the block
		// below is skipped and the unit draws NOTHING. Report the two names actually tried -- that
		// is the question MISSILE-ROCK and VCIRCLE-ADDR each cost a cycle to answer by other means.
		// ONCE per key: this is the per-frame draw path.
		if (f < 0) {
			static std::set<std::string> reportedFrameMiss;
			const std::string key = type.File + "|" + hdPrefix;
			if (reportedFrameMiss.insert(key).second) {
				ErrorPrint("HD frame MISS: %s prefix '%s' frame %d -- tried '%s' and '%s', drawing nothing\n",
				           type.File.c_str(), hdPrefix.c_str(), absFrame, frameName.c_str(),
				           (hdPrefix + "_f" + std::to_string(absFrame)).c_str());
			}
		}
		if (f >= 0) {
			const bool flip = (frame < 0) && type.Flip;
			// SPRITE-SCALE-TRUE: the original draws every sheet frame at one density -- HdNativeTilePx
			// native pixels per map tile -- with the frame's own canvas centred on the unit's tile
			// footprint. The Lua `Image size` (type.Width / type.Height) does not size HD art; it stays
			// the logical sprite box (culling, classic branch below). `screenPos` is the footprint's
			// top-left on screen, as every caller passes it; `pos` above is used by the classic branch only.
			const float z = GpuWorldDrawZoom;
			const float s = PixelTileSize.x * z / HdNativeTilePx;
			const float cx = screenPos.x + (type.TileWidth * PixelTileSize.x / 2.0f + type.Offset.x) * z;
			const float cy = screenPos.y + (type.TileHeight * PixelTileSize.y / 2.0f + type.Offset.y) * z;
			// SKELETON-SIZE: a family stored reduced is stretched from its own canvas to its rule
			// canvas; the body and the mask each by their own frame record (the Eye of Kilrogg's mask
			// is stored at full size, its body is not). Every other family: `s`, as it was.
			int ruleCanvas = 0;
			if (!hdPrefix.empty()) {
				const auto cv = g_hdCanvas.find(hdPrefix);
				if (cv != g_hdCanvas.end()) {
					ruleCanvas = cv->second;
				}
			}
			hdSprite->DrawAtlasFrameCentred(f, cx, cy,
			                                hdatlas::hdStoredCanvasScale(s, ruleCanvas, hdSprite->Frame(f).cw), flip);
			if (overlayFrame >= 0) {
				hdSprite->DrawAtlasFrameCentred(overlayFrame, cx, cy,
				                                hdatlas::hdStoredCanvasScale(s, ruleCanvas,
				                                                             hdSprite->Frame(overlayFrame).cw),
				                                flip);
			}
			// Team-colour overlay: tint the `_team` mask to the player's canonical ramp colour
			// (ramp[0], matching CPlayerColorGraphic::DrawPlayerColorFrameClipTex) and draw it over
			// the base with the same centre and scale, from the mask's own frame record (a mask's
			// canvas is not always its body's). Sparse: a building without team colour has no mask.
			if (type.HDSpriteTeam && type.HDSpriteTeam->IsLoaded()) {
				const std::string teamName = hdPrefix.empty()
				                                 ? std::to_string(absFrame)
				                                 : hdPrefix + "_" + std::to_string(absFrame) + "_team";
				int tf = type.HDSpriteTeam->FrameByName(teamName.c_str());
				if (tf < 0 && !hdPrefix.empty()) { // NG29: "<prefix>_f<N>_team" spelling
					tf = HDFrameByNumber(*type.HDSpriteTeam, hdPrefix, absFrame, "_team");
				}
				if (tf >= 0) {
					Uint8 r = 255, g = 255, b = 255;
					if (colorIndex >= 0 && colorIndex < (int) PlayerColorsRGB.size()
						&& !PlayerColorsRGB[colorIndex].empty()) {
						const CColor &c = PlayerColorsRGB[colorIndex][0];
						r = c.R;
						g = c.G;
						b = c.B;
					}
					type.HDSpriteTeam->DrawAtlasFrameCentred(
						tf, cx, cy, hdatlas::hdStoredCanvasScale(s, ruleCanvas, type.HDSpriteTeam->Frame(tf).cw),
						flip, r, g, b);
				}
			}
			return; // HD drawn; skip the classic blit for this unit
		}
	} else {
		// HD-MISS-INVISIBLE case A: no HD sprite resolved at all -- either hd_units_map.json has no
		// entry for this File, or its atlas failed to load. The classic blit below is not a fallback
		// here: 0 of the mapped paths ships a classic PNG, so this unit draws NOTHING. ONCE per file.
		static std::set<std::string> reportedSpriteMiss;
		if (reportedSpriteMiss.insert(type.File).second) {
			ErrorPrint("HD sprite MISS: '%s' resolved no HD atlas -- drawing nothing\n",
			           type.File.c_str());
		}
	}

	if (type.Flip) {
		if (frame < 0) {
			sprite->DrawPlayerColorFrameClipX(colorIndex, -frame - 1, pos.x, pos.y);
		} else {
			sprite->DrawPlayerColorFrameClip(colorIndex, frame, pos.x, pos.y);
		}
	} else {
		const int row = type.NumDirections / 2 + 1;

		if (frame < 0) {
			frame = ((-frame - 1) / row) * type.NumDirections + type.NumDirections - (-frame - 1) % row;
		} else {
			frame = (frame / row) * type.NumDirections + frame % row;
		}
		sprite->DrawPlayerColorFrameClip(colorIndex, frame, pos.x, pos.y);
	}
}

/**
**  Get the still animation frame
*/
static int GetStillFrame(const CUnitType &type)
{
	for (auto& anim : type.Animations->Still) {
		auto frame = anim->GetStillFrame(type);
		if (frame) {
			return *frame;
		}
	}
	return type.NumDirections / 2;
}

/**
**  Init unit types.
*/
void InitUnitTypes(int reset_player_stats)
{
	for (size_t i = 0; i < UnitTypes.size(); ++i) {
		CUnitType &type = *UnitTypes[i];
		Assert(type.Slot == (int)i);

		if (type.Animations == nullptr) {
			DebugPrint(_("unit-type '%s' without animations, ignored.\n"), type.Ident.c_str());
			continue;
		}
		// Determine still frame
		type.StillFrame = GetStillFrame(type);

		// Lookup BuildingTypes
		for (auto &&b : type.BuildingRules) {
			b->Init();
		}

		// Lookup AiBuildingTypes
		for (auto &&b : type.AiBuildingRules) {
			b->Init();
		}
	}

	// LUDO : called after game is loaded -> don't reset stats !
	UpdateStats(reset_player_stats); // Calculate the stats
}

/**
**  Loads the Sprite for a unit type
**
**  @param type  type of unit to load
*/
void LoadUnitTypeSprite(CUnitType &type)
{
	// CLASSIC-SHEET-LOOKUPS: a sheet the loaded HD art replaces becomes the placeholder at once.
	// A flyer's shadow is drawn from the biome's common atlas (GetHDShadowAtlas).
	const bool hdBody = HDArtCoversFile(type.File);
	if (!type.ShadowFile.empty()) {
		type.ShadowSprite = CGraphic::ForceNew(type.ShadowFile, type.ShadowWidth, type.ShadowHeight);
		if (hdBody && HDAtlasResident("units_common_" + CurrentEffectTileset() + "_sprites")) {
			type.ShadowSprite->NoClassicFile = true;
		}
		type.ShadowSprite->Load();
		if (type.ShadowScale != 1) {
			type.ShadowSprite->Resize(type.ShadowSprite->GetGraphicWidth() / type.ShadowScale,
			                          type.ShadowSprite->GetGraphicHeight() / type.ShadowScale);
		}
		if (!type.ShadowSpriteFrame) {
			if (type.Flip) {
				type.ShadowSprite->Flip();
			}
			type.ShadowSprite->MakeShadow(type.ShadowOffset);
		}
	}

	if (type.BoolFlag[HARVESTER_INDEX].value) {
		for (int i = 0; i < MaxCosts; ++i) {
			auto &resinfo = type.ResInfo[i];
			if (!resinfo) {
				continue;
			}
			if (!resinfo->FileWhenLoaded.empty()) {
				resinfo->SpriteWhenLoaded = CPlayerColorGraphic::New(resinfo->FileWhenLoaded,
																	 type.Width, type.Height);
				if (HDArtCoversFile(resinfo->FileWhenLoaded)) {
					resinfo->SpriteWhenLoaded->NoClassicFile = true;
				}
				resinfo->SpriteWhenLoaded->Load();
				if (type.Flip) {
					resinfo->SpriteWhenLoaded->Flip();
				}
				resinfo->SpriteWhenLoaded->LoadTeamMask(resinfo->FileWhenLoaded);
			}
			if (!resinfo->FileWhenEmpty.empty()) {
				resinfo->SpriteWhenEmpty = CPlayerColorGraphic::New(resinfo->FileWhenEmpty,
																	type.Width, type.Height);
				if (HDArtCoversFile(resinfo->FileWhenEmpty)) {
					resinfo->SpriteWhenEmpty->NoClassicFile = true;
				}
				resinfo->SpriteWhenEmpty->Load();
				if (type.Flip) {
					resinfo->SpriteWhenEmpty->Flip();
				}
				resinfo->SpriteWhenEmpty->LoadTeamMask(resinfo->FileWhenEmpty);
			}
		}
	}

	if (!type.File.empty()) {
		type.Sprite = CPlayerColorGraphic::New(type.File, type.Width, type.Height);
		if (hdBody) {
			type.Sprite->NoClassicFile = true;
		}
		type.Sprite->Load();
		if (type.Flip) {
			type.Sprite->Flip();
		}
		type.Sprite->LoadTeamMask(type.File);
	}

	if (!type.AltFile.empty()) {
		type.AltSprite = CPlayerColorGraphic::New(type.AltFile, type.Width, type.Height);
		type.AltSprite->Load();
		if (type.Flip) {
			type.AltSprite->Flip();
		}
	}

#ifdef USE_MNG
	if (!type.Portrait.Mngs.empty()) {
		for (size_t i = 0; i < type.Portrait.Mngs.size(); ++i) {
			type.Portrait.Mngs[i] = Mng::New(type.Portrait.Files[i]);
			type.Portrait.Mngs[i]->Load();
		}
		// FIXME: should be configurable
		type.Portrait.CurrMng = 0;
		type.Portrait.NumIterations = MyRand() % 16 + 1;
	}
#endif
}

/**
** Load the graphics for the unit-types.
*/
void LoadUnitTypes()
{
	for (CUnitType *type : UnitTypes) {
		// Lookup icons.
		type->Icon.Load();
		// Lookup missiles.
		type->Missile.MapMissile();
		type->Explosion.MapMissile();

		// Lookup impacts
		for (int i = 0; i < ANIMATIONS_DEATHTYPES + 2; ++i) {
			type->Impact[i].MapMissile();
		}
		// Lookup corpse.
		if (!type->CorpseName.empty()) {
			type->CorpseType = &UnitTypeByIdent(type->CorpseName);
		}
#ifndef DYNAMIC_LOAD
		// Load Sprite
		if (!type->Sprite) {
			ShowLoadProgress(_("Unit \"%s\""), type->Name.c_str());
			LoadUnitTypeSprite(*type);
		}
#endif
		// FIXME: should i copy the animations of same graphics?
	}
}

/// Worker threads of the HD preload; 0 = every atlas is decoded on the main thread, one by one.
static int HDPreloadWorkers = 2;
/// Decoded pages that may exist at one time, in decode, waiting or in upload (a common page is
/// about 240 MB, so every step up costs that much at the peak).
static int HDPreloadWindow = 2;
static constexpr int HDPreloadWindowMax = 4;
/// LOAD-TIME: workers and window follow the device (hdatlas::preloadDecodeWorkers) until a setter
/// below is called.
static bool HDPreloadAuto = true;

void SetHDPreloadWorkers(int count)
{
	HDPreloadAuto = false;
	HDPreloadWorkers = count < 0 ? 0 : (count > HDPreloadWindowMax ? HDPreloadWindowMax : count);
}

void SetHDPreloadWindow(int count)
{
	HDPreloadAuto = false;
	HDPreloadWindow = count < 1 ? 1 : (count > HDPreloadWindowMax ? HDPreloadWindowMax : count);
}

/// LOAD-TIME: the RGBA bytes of a page PNG from its header; 0 when it cannot be read.
static uint64_t HDPngPageBytes(const fs::path &png)
{
	CFile fp;
	if (png.empty() || fp.open(png.string().c_str(), CL_OPEN_READ) == -1) {
		return 0;
	}
	unsigned char head[24] = {};
	const int n = fp.read(head, sizeof(head));
	fp.close();
	return n > 0 ? hdatlas::pngRgbaBytes(head, (size_t) n) : 0;
}

/// The threads the preload starts: a thread more than the window allows pages would only wait.
static int HDPreloadWorkersInUse()
{
	return HDPreloadWorkers > HDPreloadWindow ? HDPreloadWindow : HDPreloadWorkers;
}

/**
**  The preload with the pages decoded on worker threads: the same atlases as the plain loop, put
**  into the cache by the main thread in the same order. A worker reads and decodes a page file
**  and nothing else; the frame table and the upload stay on the main thread.
**
**  @return  the number of atlases of the list.
*/
static int PreloadHDArtDecodedAhead(const std::vector<std::string> &stems)
{
	struct Job {
		std::string stem;
		std::string label;
		bool teamMask;
		fs::path png;
	};
	std::vector<Job> list;
	for (const std::string &stem : stems) {
		list.push_back({stem + "_sprites", stem, false, {}});
		list.push_back({stem + "_masks", stem, true, {}}); // a first-draw load of the masks was seen in-game (STALL-HUNT)
	}
	// Only what the cache does not hold is decoded; the search roots are read here, not by a worker.
	std::vector<Job> jobs;
	for (Job &job : list) {
		if (NativeAtlasCache().count(job.stem)) {
			continue;
		}
		const fs::path json = LibraryFileName("Data/Art/hd/unit/" + job.stem + ".json");
		if (!json.empty()) {
			job.png = json;
			job.png.replace_extension(".png");
		}
		jobs.push_back(job);
	}
	{
		// LOAD-TIME: the largest pages first (hdatlas::preloadDecodeOrder); the order of the
		// atlases in the cache does not matter, every page of the list is loaded before play.
		std::vector<uint64_t> bytes;
		for (const Job &job : jobs) {
			bytes.push_back(HDPngPageBytes(job.png));
		}
		std::vector<Job> sorted;
		for (int i : hdatlas::preloadDecodeOrder(bytes)) {
			sorted.push_back(jobs[i]);
		}
		jobs.swap(sorted);
	}
	IMG_Init(IMG_INIT_PNG); // not first called by two workers at once
	hdatlas::runOrdered<AtlasPageDecode>(
		(int) jobs.size(), HDPreloadWorkersInUse(), HDPreloadWindow, 100,
		[&jobs](int i) {
			const Job &job = jobs[i];
			return job.png.empty() ? AtlasPageDecode() : DecodeAtlasPage(job.png, job.teamMask, false, false);
		},
		[&jobs](int i, AtlasPageDecode page) {
			const Job &job = jobs[i];
			ShowLoadProgress("%s", job.label.c_str());
			// Without a page the atlas goes the plain way and fails or loads as it would there.
			GetSharedNativeAtlas(job.stem, job.teamMask, page.surface ? &page : nullptr);
			NetworkLoadingPing(); // LAN: alive between two atlases
		},
		[&jobs](int i) {
			NetworkLoadingPing();
			ShowLoadProgress("%s", jobs[i].label.c_str());
		});
	return (int) list.size();
}

/**
**  HD-ATLAS-CACHE-GROWS: one process plays several missions and the cache kept the pages of every
**  biome it had seen. The atlases of the other biomes are let go here, before the first page of
**  the present biome is decoded, so the two sets are never held together. A race's common pages
**  stay. Everything that points at a dropped atlas is cleared first, and a unit type is marked
**  "not tried", so its next draw resolves again for the File it has then (HD-ART-STALE-TILESET).
**  The textures are destroyed with the last pointer: here, on the main thread, during loading.
*/
static void ReleaseHDArtOfOtherBiomes(const std::string &biome)
{
	auto &cache = NativeAtlasCache();
	std::set<const CAtlasGraphic *> dropped;
	int count = 0;
	for (auto it = cache.begin(); it != cache.end();) {
		if (hdatlas::stemIsOfOtherBiome(it->first, biome)) {
			if (it->second) {
				dropped.insert(it->second.get());
				++count;
			}
			it = cache.erase(it); // a remembered failure goes too: asked again when that biome returns
		} else {
			++it;
		}
	}
	if (!dropped.empty()) {
		for (CUnitType *type : UnitTypes) {
			if (dropped.count(type->HDSprite.get()) || dropped.count(type->HDSpriteTeam.get())) {
				type->HDSprite.reset();
				type->HDSpriteTeam.reset();
				type->HDSpritePrefix.clear();
				type->HDSpriteFile.clear();
				type->HDSpriteTried = false;
			}
		}
		auto &files = HDFileCache();
		for (auto it = files.begin(); it != files.end();) {
			if (dropped.count(it->second.first.get())) {
				it = files.erase(it);
			} else {
				++it;
			}
		}
	}
	ReleaseHDMissileArtOfOtherBiomes();
	ErrorPrint("HD release: %d atlases of other biomes, cache now %d\n", count, (int) cache.size());
}

/// PRELOAD-PLAN: load the atlases of `stems` (each its _sprites and _masks page) that the cache
/// does not hold, the pages decoded on the workers when there are any. Returns the pages asked for.
static int PreloadHDStems(const std::vector<std::string> &stems)
{
	if (stems.empty()) {
		return 0;
	}
	if (HDPreloadWorkers > 0) {
		return PreloadHDArtDecodedAhead(stems);
	}
	for (const std::string &stem : stems) {
		ShowLoadProgress("%s", stem.c_str());
		GetSharedNativeAtlas(stem + "_sprites");
		NetworkLoadingPing(); // LAN: alive between two atlases
		GetSharedNativeAtlas(stem + "_masks", /*teamMask=*/true);
		NetworkLoadingPing();
	}
	return (int) stems.size() * 2;
}

/// PRELOAD-BUDGET: the RGBA bytes of the atlas page `page` (a stem with _sprites / _masks), read
/// from its PNG header; 0 when the page is not found or is not a PNG.
static uint64_t HDAtlasPageBytes(const std::string &page)
{
	const fs::path json = LibraryFileName("Data/Art/hd/unit/" + page + ".json");
	if (json.empty()) {
		return 0;
	}
	fs::path png = json;
	png.replace_extension(".png");
	return HDPngPageBytes(png);
}

/// PRELOAD-PLAN: the sprite files a unit type draws from: its body, the carry sprites of a worker
/// and the construction's own art (shipyard, foundry, refinery, oil platform).
static void AppendHDFilesOfType(const CUnitType &type, std::vector<std::string> &files)
{
	files.push_back(type.File);
	for (int i = 0; i < MaxCosts; ++i) {
		const auto &resinfo = type.ResInfo[i];
		if (resinfo) {
			files.push_back(resinfo->FileWhenLoaded);
			files.push_back(resinfo->FileWhenEmpty);
		}
	}
	if (type.Construction) {
		files.push_back(type.Construction->File.File);
	}
}

/// PRELOAD-SCRIPT-UNITS: the text of the map's setup script (the .sms beside a .smp, plain or
/// packed - CFile tries .gz / .bz2); empty for a map without one (a native PUD) or one not found.
static std::string ReadMapSetupText()
{
	std::string name = Map.Info.Filename;
	const size_t smp = name.rfind(".smp");
	if (smp != std::string::npos) {
		name.replace(smp, 4, ".sms");
	}
	if (name.find(".sms") == std::string::npos) {
		return std::string();
	}
	const std::string path = LibraryFileName(name);
	CFile fp;
	if (path.empty() || fp.open(path.c_str(), CL_OPEN_READ) == -1) {
		return std::string();
	}
	std::string text;
	char buffer[16384];
	int n = 0;
	while (text.size() < (8u << 20) && (n = fp.read(buffer, sizeof(buffer))) > 0) {
		text.append(buffer, (size_t) n);
	}
	fp.close();
	return text;
}

/**
**  STUTTER-FIRST-DRAW-LOAD / PRELOAD-PLAN (work/PRELOAD_IMPL.md): load the native HD atlases this
**  map can draw from while the loading screen is up, so that no atlas is decoded and uploaded in
**  the middle of a frame at the first draw of a unit, a building (upgrade forms included), a
**  construction or an effect.
**
**  1. The base set (hdatlas::preloadBaseStems): the biome's common atlas and, for every race in
**     play, its unit atlas and its building atlas of the biome, each with the player-colour masks.
**     A race is in play for (a) every player of the map, units at the start or not (a player whose
**     units come later by a trigger, a rescue player), and (b) the atlas of every unit on the map,
**     whoever owns it. The local player's race is loaded first.
**  2. The extras: every unit type the map can show - each type of a race in play (all it can
**     train, build or upgrade to) and each type on the map and its corpse, with the carry and
**     construction art - is resolved to the atlas it draws from on this map
**     (HDPlannedStemForFile). An atlas not held yet (a family the map's biome atlas lacks, so the
**     roster's atlas of another biome) is loaded while the resident set stays within the budget
**     (hdatlas::hdPreloadBudgetMiB); one that does not fit is left to its first draw, as before,
**     and named in the log.
**  3. Every type and effect whose atlas is held is resolved now, its classic sheet a placeholder
**     made without a file search (CLASSIC-SHEET-LOOKUPS), so its first draw does no load at all.
**  The cache is the shared one (units and effects), so nothing is loaded twice.
*/
void PreloadHDArtForMap()
{
	const std::string biome = CurrentEffectTileset();
	if (biome.empty()) {
		return;
	}
	// STALL-HUNT (work/STALL_HUNT.md): a race missed here is decoded on the main thread when its
	// first unit is drawn - S8U: human / orc common sprites 4.6 - 5.4 s each, ~6-8 s for a race's
	// set.
	LoadHDUnitMap();
	const auto raceOfAtlas = [](const std::string &atlas) -> std::string {
		// "units_<race>_<group>"; only the two races have atlases of their own.
		if (atlas.compare(0, 12, "units_human_") == 0) {
			return "human";
		}
		if (atlas.compare(0, 10, "units_orc_") == 0) {
			return "orc";
		}
		return std::string();
	};
	const auto rosterAtlasOf = [](const std::string &file) -> std::string {
		std::string key = file;
		for (char &c : key) { c = (char) std::tolower((unsigned char) c); }
		const auto mit = g_hdUnitMap.find(key);
		return mit != g_hdUnitMap.end() ? mit->second.atlas : std::string();
	};
	std::vector<std::string> races; // the local player's race first
	const auto addRace = [&races](const std::string &race) {
		if ((race == "human" || race == "orc") && std::find(races.begin(), races.end(), race) == races.end()) {
			races.push_back(race);
		}
	};
	if (ThisPlayer) {
		addRace(PlayerRaces.Name[ThisPlayer->Race]);
	}
	std::vector<const CUnitType *> onMap;
	for (int p = 0; p < PlayerMax; ++p) {
		const CPlayer &player = Players[p];
		if (player.Type == PlayerTypes::PlayerNobody) {
			continue;
		}
		addRace(PlayerRaces.Name[player.Race]);
		for (const CUnit *unit : player.GetUnits()) {
			if (!unit || !unit->Type) {
				continue;
			}
			onMap.push_back(unit->Type);
			addRace(raceOfAtlas(rosterAtlasOf(unit->Type->File)));
		}
	}
	ReleaseHDArtOfOtherBiomes(biome); // before the first decode: the peak is one biome's set
	if (HDPreloadAuto) {
		HDPreloadWorkers = HDPreloadWindow =
			hdatlas::preloadDecodeWorkers((uint64_t) SDL_GetSystemRAM(), SDL_GetCPUCount());
	}
	const unsigned long start = GetTicks();

	// 1. The base set.
	const std::vector<std::string> base = hdatlas::preloadBaseStems(biome, races);
	int count = PreloadHDStems(base);

	// 2. The extras, within the budget.
	std::vector<const CUnitType *> needed = onMap;
	for (const CUnitType *type : UnitTypes) {
		const std::string race = raceOfAtlas(rosterAtlasOf(type->File));
		if (!race.empty() && std::find(races.begin(), races.end(), race) != races.end()) {
			needed.push_back(type);
		}
	}
	for (const CUnitType *type : onMap) {
		if (type->CorpseType) {
			needed.push_back(type->CorpseType);
		}
	}
	// PRELOAD-SCRIPT-UNITS: the types the map's own triggers create during play, so that one of a
	// race no player has (no base set for it) is an extra here, within the budget, and not a
	// mid-game decode at its first draw (S20+ 3.4.84: units_orc_common 1.9 s).
	int scriptTypes = 0;
	for (const std::string &ident : hdatlas::scriptPlayUnitIdents(ReadMapSetupText())) {
		if (const CUnitType *type = UnitTypeByIdentOrNull(ident)) {
			needed.push_back(type);
			if (type->CorpseType) {
				needed.push_back(type->CorpseType);
			}
			++scriptTypes;
		}
	}
	std::sort(needed.begin(), needed.end(),
	          [](const CUnitType *a, const CUnitType *b) { return a->Slot < b->Slot; });
	needed.erase(std::unique(needed.begin(), needed.end()), needed.end());
	std::vector<std::string> files;
	for (const CUnitType *type : needed) {
		AppendHDFilesOfType(*type, files);
	}
	const uint64_t budget = hdatlas::hdPreloadBudgetMiB((uint64_t) SDL_GetSystemRAM()) * 1024 * 1024;
	uint64_t resident = 0;
	int extras = 0;
	std::string skipped;
	// Two rounds: an extra map-biome atlas, once held, can send a family on to the roster's atlas.
	for (int round = 0; round < 2; ++round) {
		std::vector<std::string> wanted;
		for (const std::string &file : files) {
			if (!file.empty()) {
				wanted.push_back(HDPlannedStemForFile(file));
			}
		}
		std::vector<std::string> have;
		resident = 0;
		for (const auto &[page, atlas] : NativeAtlasCache()) {
			const size_t cut = page.rfind('_');
			if (cut != std::string::npos) {
				have.push_back(page.substr(0, cut)); // held, or a remembered failure: not asked again
			}
			if (atlas && atlas->IsLoaded()) {
				resident += HDAtlasPageBytes(page);
			}
		}
		const std::vector<std::string> extra = hdatlas::preloadExtraStems(wanted, have);
		if (extra.empty()) {
			break;
		}
		std::vector<uint64_t> bytes;
		for (const std::string &stem : extra) {
			bytes.push_back(HDAtlasPageBytes(stem + "_sprites") + HDAtlasPageBytes(stem + "_masks"));
		}
		const std::vector<int> kept = hdatlas::fitPreloadBudget(resident, bytes, budget);
		std::vector<std::string> load;
		for (int i = 0, k = 0; i < (int) extra.size(); ++i) {
			if (k < (int) kept.size() && kept[k] == i) {
				load.push_back(extra[i]);
				resident += bytes[i];
				++k;
			} else if (skipped.find(extra[i]) == std::string::npos) {
				skipped += (skipped.empty() ? "" : ", ") + extra[i] + " ("
				           + std::to_string((unsigned long) (bytes[i] >> 20)) + " MiB)";
			}
		}
		if (load.empty()) {
			break;
		}
		count += PreloadHDStems(load);
		extras += (int) load.size();
	}

	// 3. Resolve what is held; no atlas is loaded below.
	int resolved = 0;
	for (const CUnitType *type : needed) {
		if (!HDArtCoversFile(type->File)) {
			continue; // no HD art held: left to the first draw, as before
		}
		CUnitType &t = *const_cast<CUnitType *>(type);
		EnsureHDUnitSprite(t);
		if (!t.Sprite) {
			LoadUnitTypeSprite(t);
		}
		for (int i = 0; i < MaxCosts; ++i) {
			const auto &resinfo = t.ResInfo[i];
			if (!resinfo) {
				continue;
			}
			for (const std::string *file : {&resinfo->FileWhenLoaded, &resinfo->FileWhenEmpty}) {
				std::shared_ptr<CAtlasGraphic> sprite;
				std::string prefix;
				if (!file->empty() && HDArtCoversFile(*file)) {
					ResolveHDForFile(*file, sprite, prefix);
				}
			}
		}
		++resolved;
	}
	const int effects = PreloadHDMissileArt();
	// The time is the wall time of the whole preload; with workers the read+decode and convert
	// columns of the ATLAS load lines overlap and add up to more than it.
	ErrorPrint("HD preload: %d atlases for biome '%s', %d race(s), %lu ms, %d worker(s), window %d\n",
	           count, biome.c_str(), (int) races.size(), GetTicks() - start, HDPreloadWorkersInUse(),
	           HDPreloadWindow);
	ErrorPrint("HD preload plan: %d base + %d extra atlas(es), %lu MiB resident of a %lu MiB budget, "
	           "%d unit type(s) and %d effect(s) resolved, %d type(s) from the map's triggers%s%s\n",
	           (int) base.size(), extras, (unsigned long) (resident >> 20), (unsigned long) (budget >> 20),
	           resolved, effects, scriptTypes, skipped.empty() ? "" : "; over the budget: ", skipped.c_str());
}

void CUnitTypeVar::Init()
{
	// Variables.
	Variable.resize(GetNumberVariable());
	size_t new_size = UnitTypeVar.GetNumberBoolFlag();
	for (CUnitType *type : UnitTypes) { // adjust array for unit already defined
		type->BoolFlag.resize(new_size);
	}
}

void CUnitTypeVar::Clear()
{
	Variable.clear();
	DecoVar.clear();
}

/**
**  Cleanup the unit-type module.
*/
void CleanUnitTypes()
{
	DebugPrint("FIXME: icon, sounds not freed.\n");
	FreeAnimations();

	// Clean all unit-types
	UnitTypes.clear();
	UnitTypeMap.clear();
	UnitTypeVar.Clear();

	// Clean hardcoded unit types.
	UnitTypeHumanWall = nullptr;
	UnitTypeOrcWall = nullptr;
}

//@}
