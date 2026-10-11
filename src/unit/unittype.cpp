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
#include "player.h"
#include "script.h"
#include "sound.h"
#include "spells.h"
#include "tileset.h"
#include "translate.h"
#include "ui.h"
#include "unitsound.h"
#include "util.h"
#include "video.h"
#include "json.h"

#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

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
// numbers are optional keys of hd_units_map.json: "cycle_frames" (pictures in the loop) and
// "cycle_step10" (game cycles per picture, times ten) as measured on the original
// (work/agent-orig-art/ORIG_ART.md). No key, no cycle.
struct HDCycle { int frames = 0; int step10 = 0; };
static std::unordered_map<std::string, HDCycle> g_hdCycle;

static void LoadHDUnitMap()
{
	if (g_hdUnitMapTried) {
		return;
	}
	g_hdUnitMapTried = true;
	// HD-MISS-INVISIBLE: this map is the single point of failure for ALL unit rendering -- 0 of its
	// mapped paths has a classic PNG to fall back to, so anything that does not resolve renders
	// NOTHING. Every early return below used to be silent, which made a half-loaded map
	// indistinguishable from a loaded one. ErrorPrint, not DebugPrint: this has to be visible
	// without a runtime flag.
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
			if (cf && cs && cf->asInt() > 1 && cs->asInt() > 0) {
				g_hdCycle[p->asString()] = { (int) cf->asInt(), (int) cs->asInt() };
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
static std::shared_ptr<CAtlasGraphic> GetSharedNativeAtlas(const std::string &stem, bool teamMask = false)
{
	static std::unordered_map<std::string, std::shared_ptr<CAtlasGraphic>> cache;
	auto it = cache.find(stem);
	if (it != cache.end()) {
		return it->second;
	}
	auto atlas = std::make_shared<CAtlasGraphic>();
	if (!atlas->LoadTexturePacker("Data/Art/hd/unit/" + stem + ".json", teamMask)) {
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

// NG15/NG22: resolve the HD atlas+prefix for an ARBITRARY sprite path. The per-type cache below is
// keyed on type.File, which is correct for the unit's own sprite but wrong for the CARRY sprites: a
// loaded worker is drawn from resinfo->SpriteWhenLoaded (peasant_with_gold.png / _with_wood.png),
// and those have their own roster entries (peong / peonl). Without this the HD branch always drew
// the empty-handed prefix, so gold and lumber frames were never selected.
// Results are cached per path; peon/peong/peonl each have 65 contiguous frames, so the absolute
// frame index maps 1:1 between them and no index translation is needed.
static bool ResolveHDForFile(const std::string &file,
                             std::shared_ptr<CAtlasGraphic> &outSprite,
                             std::string &outPrefix)
{
	static std::map<std::string, std::pair<std::shared_ptr<CAtlasGraphic>, std::string>> cache;
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
		sprites = GetSharedNativeAtlas(mit->second.atlas + "_sprites");
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
		auto sprites = GetSharedNativeAtlas(mit->second.atlas + "_sprites");
		// NG8 diagnostic (one-shot per type): report how buildings resolve their HD atlas so a
		// classic-scale fallback can be traced to a map miss vs an atlas-load miss vs a frame miss.
		if (key.find("buildings/") != std::string::npos) {
			DebugPrint("NG8 building file='%s' -> atlas='%s_sprites' prefix='%s' loaded=%d\n",
			           key.c_str(), mit->second.atlas.c_str(), mit->second.prefix.c_str(),
			           (int) (sprites && sprites->IsLoaded()));
		}
		if (sprites && sprites->IsLoaded()) {
			type.HDSprite = sprites;
			type.HDSpritePrefix = mit->second.prefix;
			type.HDSpriteTeam = GetSharedNativeAtlas(mit->second.atlas + "_masks", /*teamMask=*/true); // optional
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
	const std::string &atlas = it->second.atlas;          // units_<race>_<biome>
	const size_t a = atlas.find('_');
	const size_t b = (a == std::string::npos) ? std::string::npos : atlas.find('_', a + 1);
	if (b == std::string::npos) {
		return nullptr;
	}
	const std::string biome = atlas.substr(b + 1);
	outPrefix = (biome == "winter") ? "s_build1" : "build_1";
	return GetSharedNativeAtlas("units_common_" + biome + "_sprites");
}

// NG82: a FLYING unit's ground shadow. Only the eight airborne types declare a shadow
// (units.lua ShadowDefinition), and the art ships as shadow_0/1/2 in every per-tileset common
// atlas. Ground units are untouched: their shadow is baked into their own sprite, so a second one
// would double it.
//
// The construction-site trick above (take the building's own atlas and swap the race token for
// "common") does NOT work here and was the first attempt's defect: buildings live in
// units_<race>_<biome>, but MOBILE units live in units_<race>_common, where "common" is the
// tileset-independent slot rather than a biome. Swapping tokens produced
// "units_common_common_sprites", which does not exist -- the atlas silently failed to load, so
// nothing drew and nothing was reported. The shadow is cast on the map, so it takes the MAP's
// tileset, resolved by the one shared CurrentEffectTileset() (missile.h) rather than a second copy.
//
// AMEND B reverses AMEND A's per-unit derivation. The original client was captured with a Dragon,
// a Gryphon Rider and a Gnomish Flying Machine in ONE frame: their shadows measure 59x62, 59x63 and
// 60x62, FWHM 45/46/45 px -- identical to 1-2 px despite wildly different sprite sizes. The original
// gives every flying unit the SAME shadow, so a footprint derived from each unit's own art scale
// cannot reproduce it. The size is a single measured world constant instead; this function now
// returns only the atlas.
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

void DrawUnitType(const CUnitType &type, CPlayerColorGraphic *sprite, int colorIndex, int frame, const PixelPos &screenPos)
{
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
		// ART-CYCLES: a family with a cycle shows step k of it, k taken from the game cycle. Where the
		// frame also ships as a whole picture the step is drawn OVER it (glow); where it ships only
		// as a cycle the step IS the picture.
		int overlayFrame = -1;
		if (!hdPrefix.empty()) {
			const auto cyc = g_hdCycle.find(hdPrefix);
			if (cyc != g_hdCycle.end()) {
				const int k = (int) ((GameCycle * 10UL / (unsigned long) cyc->second.step10)
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
			// SPRITE-SCALE-TRUE (SPEC_SPRITE_SCALE_TRUE.md sections 1 and 4): the original draws
			// every sheet frame at one density -- HdNativeTilePx native pixels per map tile -- with
			// the frame's own canvas centred on the unit's tile footprint. The Lua `Image size`
			// (type.Width / type.Height) no longer sizes HD art; it stays the logical sprite box
			// (culling, classic branch below). `screenPos` is the footprint's top-left on screen,
			// as every caller passes it; `pos` above is used by the classic branch only.
			const float z = GpuWorldDrawZoom;
			const float s = PixelTileSize.x * z / HdNativeTilePx;
			const float cx = screenPos.x + (type.TileWidth * PixelTileSize.x / 2.0f + type.Offset.x) * z;
			const float cy = screenPos.y + (type.TileHeight * PixelTileSize.y / 2.0f + type.Offset.y) * z;
			hdSprite->DrawAtlasFrameCentred(f, cx, cy, s, flip);
			if (overlayFrame >= 0) {
				hdSprite->DrawAtlasFrameCentred(overlayFrame, cx, cy, s, flip);
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
					type.HDSpriteTeam->DrawAtlasFrameCentred(tf, cx, cy, s, flip, r, g, b);
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
	if (!type.ShadowFile.empty()) {
		type.ShadowSprite = CGraphic::ForceNew(type.ShadowFile, type.ShadowWidth, type.ShadowHeight);
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
				resinfo->SpriteWhenLoaded->Load();
				if (type.Flip) {
					resinfo->SpriteWhenLoaded->Flip();
				}
				resinfo->SpriteWhenLoaded->LoadTeamMask(resinfo->FileWhenLoaded);
			}
			if (!resinfo->FileWhenEmpty.empty()) {
				resinfo->SpriteWhenEmpty = CPlayerColorGraphic::New(resinfo->FileWhenEmpty,
																	type.Width, type.Height);
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

/**
**  STUTTER-FIRST-DRAW-LOAD: load the native HD atlases this map will draw from while the loading
**  screen is up. Without it each atlas (a whole race's units, a race's buildings of one biome,
**  the biome's common art) was decoded and uploaded in the middle of a frame at the first draw of
**  a unit of that family: measured on a tablet as a stop of about 2 s, once 5-7 s (the first enemy
**  unit of the other race). Loaded: for every race that has units on the map its unit atlas and
**  its building atlas of the map's biome, each with the player-colour masks, and the biome's
**  common atlas. The cache is the shared one, so nothing is loaded twice.
*/
void PreloadHDArtForMap()
{
	const std::string biome = CurrentEffectTileset();
	if (biome.empty()) {
		return;
	}
	std::set<std::string> races;
	for (int p = 0; p < PlayerMax; ++p) {
		const CPlayer &player = Players[p];
		if (player.Type == PlayerTypes::PlayerNobody || player.GetUnitCount() == 0) {
			continue;
		}
		const std::string &race = PlayerRaces.Name[player.Race];
		if (race == "human" || race == "orc") {
			races.insert(race);
		}
	}
	const unsigned long start = GetTicks();
	int count = 0;
	GetSharedNativeAtlas("units_common_" + biome + "_sprites");
	++count;
	for (const std::string &race : races) {
		for (const std::string &group : {std::string("common"), biome}) {
			const std::string stem = "units_" + race + "_" + group;
			ShowLoadProgress("%s", stem.c_str());
			GetSharedNativeAtlas(stem + "_sprites");
			NetworkLoadingPing(); // LAN: alive between two atlases
			GetSharedNativeAtlas(stem + "_masks", /*teamMask=*/true);
			NetworkLoadingPing();
			count += 2;
		}
	}
	ErrorPrint("HD preload: %d atlases for biome '%s', %d race(s), %lu ms\n",
	           count, biome.c_str(), (int) races.size(), GetTicks() - start);
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
