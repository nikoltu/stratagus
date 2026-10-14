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
/**@name missile.cpp - The missiles. */
//
//      (c) Copyright 1998-2007 by Lutz Sammer and Jimmy Salmon
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

#include "missile.h"

#include "action/action_spellcast.h"
#include "actions.h"
#include "animation.h"
#include "font.h"
#include "iolib.h"
#include "luacallback.h"
#include "map.h"
#include "missile_cadence.h"
#include "player.h"
#include "sound.h"
#include "spells.h"
#include "trigger.h"
#include "ui.h"
#include "unit.h"
#include "unit_find.h"
#include "unitsound.h"
#include "unittype.h"
#include "video.h"
#include "json.h"

#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/

unsigned int Missile::Count = 0;

static std::vector<std::unique_ptr<Missile>> GlobalMissiles;    /// all global missiles on map
static std::vector<std::unique_ptr<Missile>> LocalMissiles;     /// all local missiles on map

/// lookup table for missile names
using MissileTypeMap = std::map<std::string, std::unique_ptr<MissileType>, std::less<>>;
static MissileTypeMap MissileTypes;

std::vector<BurningBuildingFrame> BurningBuildingFrames; /// Burning building frames

extern std::unique_ptr<INumberDesc> Damage;                   /// Damage calculation for missile.

/*----------------------------------------------------------------------------
--  Functions
----------------------------------------------------------------------------*/

namespace
{
/**
**  Find nearest point of unit.
**
**  @param unit  Pointer to unit.
**  @param pos   tile map position.
**  @return nearest point tile map position to (tx,ty).
*/
Vec2i NearestOfUnit(const CUnit &unit, const Vec2i &pos)
{
	const int x = unit.tilePos.x;
	const int y = unit.tilePos.y;

	Vec2i res = pos;
	clamp<short int>(&res.x, x, x + unit.Type->TileWidth - 1);
	clamp<short int>(&res.y, y, y + unit.Type->TileHeight - 1);
	return res;
}

} // namespace

/**
**  Load the graphics for a missile type
*/
void MissileType::LoadMissileSprite()
{
	if (this->G && !this->G->IsLoaded(this->Flip)) {
		this->G->Load();
		if (this->Flip) {
			this->G->Flip();
		}

		// Correct the number of frames in graphic
		DebugPrint("%s - %d>=%d\n", this->G->File.string().c_str(), this->G->NumFrames, this->SpriteFrames);
		Assert(this->G->NumFrames >= this->SpriteFrames);
		this->G->NumFrames = this->SpriteFrames;
		// FIXME: Don't use NumFrames as number of frames.
	}
}

/**
**  Load the graphics for all missiles types
*/
void LoadMissileSprites()
{
#ifndef DYNAMIC_LOAD
	for (auto &[key, missileType] : MissileTypes) {
		missileType->LoadMissileSprite();
	}
#endif
}
/**
**  Get Missile type by identifier.
**
**  @param ident  Identifier.
**
**  @return       Missile type pointer.
*/
MissileType &MissileTypeByIdent(std::string_view ident)
{
	MissileTypeMap::iterator it = MissileTypes.find(ident);
	if (it != MissileTypes.end()) {
		return *it->second;
	}
	ErrorPrint("Unknown missiletype '%s'\n", ident.data());
	ExitFatal(1);
}

/**
**  Allocate an empty missile-type slot.
**
**  @param ident  Identifier to identify the slot.
**
**  @return       New allocated (zeroed) missile-type pointer.
*/
MissileType *NewMissileTypeSlot(const std::string &ident)
{
	auto &res = MissileTypes[ident];

	if (res == nullptr) {
		res = std::make_unique<MissileType>(ident);
	} else {
		DebugPrint("Redefining missile-type '%s'\n", ident.c_str());
	}
	return res.get();
}

/**
**  Initialize a new made missile.
**
**  @param mtype      Type pointer of missile.
**  @param sourcePos  Missile start point in pixel.
**  @param destPos    Missile destination point in pixel.
**
**  @return       created missile.
*/
/* static */ std::unique_ptr<Missile>
Missile::Init(const MissileType &mtype, const PixelPos &startPos, const PixelPos &destPos)
{
	std::unique_ptr<Missile> missile;

	switch (mtype.Class) {
		case MissileClass::Nothing: missile = std::make_unique<MissileNone>(); break;
		case MissileClass::PointToPoint: missile = std::make_unique<MissilePointToPoint>(); break;
		case MissileClass::PointToPointWithHit: missile = std::make_unique<MissilePointToPointWithHit>(); break;
		case MissileClass::PointToPointCycleOnce: missile = std::make_unique<MissilePointToPointCycleOnce>(); break;
		case MissileClass::PointToPointBounce: missile = std::make_unique<MissilePointToPointBounce>(); break;
		case MissileClass::Stay: missile = std::make_unique<MissileStay>(); break;
		case MissileClass::CycleOnce: missile = std::make_unique<MissileCycleOnce>(); break;
		case MissileClass::Fire: missile = std::make_unique<MissileFire>(); break;
		case MissileClass::Hit: missile = std::make_unique<::MissileHit>(); break;
		case MissileClass::Parabolic: missile = std::make_unique<MissileParabolic>(); break;
		case MissileClass::LandMine: missile = std::make_unique<MissileLandMine>(); break;
		case MissileClass::Whirlwind: missile = std::make_unique<MissileWhirlwind>(); break;
		case MissileClass::FlameShield: missile = std::make_unique<MissileFlameShield>(); break;
		case MissileClass::DeathCoil: missile = std::make_unique<MissileDeathCoil>(); break;
		case MissileClass::Tracer: missile = std::make_unique<MissileTracer>(); break;
		case MissileClass::ClipToTarget: missile = std::make_unique<MissileClipToTarget>(); break;
		case MissileClass::Continuous: missile = std::make_unique<MissileContinious>(); break;
		case MissileClass::StraightFly: missile = std::make_unique<MissileStraightFly>(); break;
		case MissileClass::DeathAndDecay: missile = std::make_unique<MissileDeathAndDecay>(); break;
	}
	const PixelPos halfSize = mtype.size / 2;
	missile->position = startPos - halfSize;
	missile->destination = destPos - halfSize;
	missile->source = missile->position;
	missile->Type = &mtype;
	missile->Wait = mtype.Sleep;
	missile->Delay = mtype.StartDelay;
	missile->TTL = mtype.TTL;
	if (mtype.FiredSound.Sound) {
		PlayMissileSound(*missile, mtype.FiredSound.Sound.get());
	}

	return missile;
}

/**
**  Create a new global missile at (x,y).
**
**  @param mtype    Type pointer of missile.
**  @param startPos Missile start point in pixel.
**  @param destPos  Missile destination point in pixel.
**
**  @return       created missile.
*/
Missile *MakeMissile(const MissileType &mtype, const PixelPos &startPos, const PixelPos &destPos)
{
	auto missile = Missile::Init(mtype, startPos, destPos);

	GlobalMissiles.push_back(std::move(missile));
	return GlobalMissiles.back().get();
}

/**
**  Create a new local missile at (x,y).
**
**  @param mtype     Type pointer of missile.
**  @param startPos  Missile start point in pixel.
**  @param destPos   Missile destination point in pixel.
**
**  @return       created missile.
*/
Missile *MakeLocalMissile(const MissileType &mtype, const PixelPos &startPos, const PixelPos &destPos)
{
	auto missile = Missile::Init(mtype, startPos, destPos);

	missile->Local = 1;
	LocalMissiles.push_back(std::move(missile));
	return LocalMissiles.back().get();
}

/**
**  Damage of one hit, from the numbers of the two units and one random draw.
**
**  The armor takes from the basic damage only and can take all of it; the piercing
**  damage passes whole. Half of that sum, rounded up, is certain; the draw adds
**  0 .. that half on top, every value equally likely. Bloodlust doubles the basic
**  and the piercing damage before the armor is taken off. A sum of 0 gives 0.
**
**  @param basic_damage     Basic damage of the attacker (upgrades included).
**  @param piercing_damage  Piercing damage of the attacker (upgrades included).
**  @param armor            Armor of the goal (upgrades included).
**  @param bloodlust        If attacker has bloodlust
**  @param draw             The random draw of this hit.
**
**  @return                 damage inflicted to goal, 0 .. 255.
*/
int HitDamage(int basic_damage, int piercing_damage, int armor, bool bloodlust, unsigned int draw)
{
	if (bloodlust) {
		basic_damage *= 2;
		piercing_damage *= 2;
	}

	const int total = std::max<int>(basic_damage - armor, 0) + std::max<int>(piercing_damage, 0);
	const int half = (total + 1) / 2;
	const int damage = half + static_cast<int>(draw % static_cast<unsigned int>(half + 1));

	return std::min<int>(damage, 255);
}

/**
**  Zone of a zone-splash missile the centre of a unit lies in.
**
**  The offset is taken on each axis; the larger one decides. Up to 22/32 of a
**  tile the unit is in the inner zone, up to 42/32 of a tile in the outer one.
**
**  @param impact    Map pixel position of the impact.
**  @param centre    Map pixel position of the centre of the unit.
**  @param tileSize  Size of a tile in map pixels.
**
**  @return          0 inner zone, 2 outer zone (the right shift of the damage),
**                   -1 out of reach.
*/
int SplashZone(const PixelPos &impact, const PixelPos &centre, const PixelSize &tileSize)
{
	const int dx = std::abs(impact.x - centre.x) * 32;
	const int dy = std::abs(impact.y - centre.y) * 32;

	if (dx <= 22 * tileSize.x && dy <= 22 * tileSize.y) {
		return 0;
	}
	if (dx <= 42 * tileSize.x && dy <= 42 * tileSize.y) {
		return 2;
	}
	return -1;
}

/**
**  Damage a zone-splash missile carries: the basic and the piercing damage of
**  the unit that fired it, both doubled by Bloodlust, at most 255.
**
**  @param basic_damage     Basic damage of the attacker (upgrades included).
**  @param piercing_damage  Piercing damage of the attacker (upgrades included).
**  @param bloodlust        If attacker has bloodlust
**
**  @return                 the damage before zone and armor, 0 .. 255.
*/
int SplashRawDamage(int basic_damage, int piercing_damage, bool bloodlust)
{
	const int raw = std::max<int>(basic_damage, 0) + std::max<int>(piercing_damage, 0);

	return std::min<int>(bloodlust ? raw * 2 : raw, 255);
}

/**
**  What is left of the damage of a zone-splash missile for one unit: the outer
**  zone takes a quarter of it, then the armor is taken off the whole of it.
**
**  @param raw    Damage the missile carries.
**  @param zone   Zone of the unit (SplashZone).
**  @param armor  Armor of the unit (upgrades included).
**
**  @return       the damage left; nothing is dealt at 0 or below.
*/
int SplashLeft(int raw, int zone, int armor)
{
	return (raw >> zone) - armor;
}

/**
**  Damage of a zone-splash missile on one unit: half of what is left, rounded
**  up, is certain; the draw adds 0 .. that half on top.
**
**  @param raw    Damage the missile carries.
**  @param zone   Zone of the unit (SplashZone).
**  @param armor  Armor of the unit (upgrades included).
**  @param draw   The random draw of this unit.
**
**  @return       damage inflicted to the unit.
*/
int SplashDamage(int raw, int zone, int armor, unsigned int draw)
{
	const int left = SplashLeft(raw, zone, armor);

	if (left <= 0) {
		return 0;
	}
	return HitDamage(left, 0, 0, false, draw);
}

/**
**  If a zone-splash missile takes the unit at all. The owner of the unit is
**  not asked: units of the shooter's player and of his allies are hit like any
**  other. Flying units are in the area only when the missile was fired at one.
**
**  @param shooter        The unit is the one that fired the missile.
**  @param flying         The unit flies.
**  @param aimedAtFlying  The missile was fired at a flying unit.
**
**  @return               true if the unit is hit by its zone.
*/
bool SplashTakesUnit(bool shooter, bool flying, bool aimedAtFlying)
{
	return !shooter && (!flying || aimedAtFlying);
}

/**
**  Steps a wall loses by a hit. A wall stands 20 steps; a hit takes a quarter
**  of its damage in steps, at least one.
**
**  @param damage  Damage that reaches the wall tile.
**
**  @return        the steps taken, 1 or more.
*/
int WallSteps(int damage)
{
	return std::max<int>(damage >> 2, 1);
}

/**
**  What a death coil takes from the units in its area. The unit with the
**  fewest hit points is served first (units with equal hit points in the
**  given order): each gives all it has while the sum stays within the budget;
**  the first that does not fit gives what is left of the budget, the rest
**  nothing.
**
**  @param hitPoints  Hit points of the units in the area.
**  @param budget     Hit points the coil can take in all.
**
**  @return           the hit points taken from each unit, in the given order.
*/
std::vector<int> DeathCoilShares(const std::vector<int> &hitPoints, int budget)
{
	std::vector<size_t> order(hitPoints.size());
	for (size_t i = 0; i != order.size(); ++i) {
		order[i] = i;
	}
	std::stable_sort(order.begin(), order.end(),
					 [&](size_t a, size_t b) { return hitPoints[a] < hitPoints[b]; });

	std::vector<int> shares(hitPoints.size(), 0);
	int sum = 0;
	for (const size_t i : order) {
		if (sum >= budget) {
			break;
		}
		if (sum + hitPoints[i] > budget) {
			shares[i] = budget - sum;
			break;
		}
		shares[i] = hitPoints[i];
		sum += hitPoints[i];
	}
	return shares;
}

/**
**  Calculate damage.
**
**  @todo NOTE: different targets (big are hit by some missiles better)
**  @todo NOTE: lower damage for hidden targets.
**  @todo NOTE: lower damage for targets on higher ground.
**
**  @param attacker_stats  Attacker attributes.
**  @param goal_stats      Goal attributes.
**  @param bloodlust       If attacker has bloodlust
**  @param xp              Experience of attacker.
**
**  @return                damage inflicted to goal.
*/
static int CalculateDamageStats(const CUnitStats &attacker_stats,
								const CUnitStats &goal_stats, int bloodlust)
{
	// One draw for every hit, whatever the numbers are.
	return HitDamage(attacker_stats.Variables[BASICDAMAGE_INDEX].Value,
					 attacker_stats.Variables[PIERCINGDAMAGE_INDEX].Value,
					 goal_stats.Variables[ARMOR_INDEX].Value, bloodlust != 0, SyncRand());
}

/**
**  Calculate damage.
**
**  @param attacker  Attacker.
**  @param goal      Goal unit.
**  @param formula   Formula used to calculate damage.
**
**  @return          damage produces on goal.
*/
int CalculateDamage(const CUnit &attacker, const CUnit &goal, const INumberDesc *formula)
{
	if (!formula) { // Use old method.
		return CalculateDamageStats(*attacker.Stats, *goal.Stats,
									attacker.Variable[BLOODLUST_INDEX].Value);
	}
	Assert(formula);

	UpdateUnitVariables(const_cast<CUnit &>(attacker));
	UpdateUnitVariables(const_cast<CUnit &>(goal));
	TriggerData.Attacker = const_cast<CUnit *>(&attacker);
	TriggerData.Defender = const_cast<CUnit *>(&goal);
	const int res = EvalNumber(*formula);
	TriggerData.Attacker = nullptr;
	TriggerData.Defender = nullptr;
	return res;
}

/**
**  Hit of a unit on something without armor (a wall, a point): one draw.
**
**  @param attacker  Unit that hits.
**
**  @return          the damage of the hit.
*/
static int WallHitDamage(const CUnit &attacker)
{
	return HitDamage(attacker.Stats->Variables[BASICDAMAGE_INDEX].Value,
					 attacker.Stats->Variables[PIERCINGDAMAGE_INDEX].Value,
					 0, attacker.Variable[BLOODLUST_INDEX].Value != 0, SyncRand());
}

/**
**  A wall tile is hit: it loses the steps of the damage (WallSteps) out of the
**  20 it stands, counted in the hit points of the wall type.
**
**  @param tilePos  Map tile position.
**  @param damage   Damage that reaches the tile.
*/
static void HitWallBySteps(const Vec2i &tilePos, int damage)
{
	if (!Map.Info.IsPointOnMap(tilePos) || !Map.WallOnMap(tilePos)) {
		return;
	}
	const CUnitType *wall = Map.HumanWallOnMap(tilePos) ? UnitTypeHumanWall : UnitTypeOrcWall;

	Map.HitWall(tilePos, (WallSteps(damage) * wall->MapDefaultStat.Variables[HP_INDEX].Max + 19) / 20);
}

/**
**  Fire missile.
**
**  @param unit  Unit that fires the missile.
*/
void FireMissile(CUnit &unit, CUnit *goal, const Vec2i &goalPos)
{
	Vec2i newgoalPos = goalPos;
	// Goal dead?
	if (goal) {
		Assert(!unit.Type->Missile.Missile->AlwaysFire || unit.Type->Missile.Missile->Range);
		if (goal->Destroyed) {
			DebugPrint("destroyed unit\n");
			return;
		}
		if (goal->Removed) {
			return;
		}
		if (goal->CurrentAction() == UnitAction::Die) {
			if (unit.Type->Missile.Missile->AlwaysFire) {
				newgoalPos = goal->tilePos;
				goal = nullptr;
			} else {
				return;
			}
		}
	}

	// No missile hits immediately!
	if (unit.Type->Missile.Missile->Class == MissileClass::Nothing
	    || (unit.Type->Animations && !unit.Type->Animations->Attack.empty()
	        && !unit.Type->Animations->RangedAttack.empty()
	        && !unit.IsAttackRanged(
				goal,
				goalPos)) // treat melee attacks from units that have both attack and ranged attack animations as having missile class none
	) {
		// No goal, take target coordinates
		if (!goal) {
			if (Map.WallOnMap(goalPos)) {
				// A wall has no armor: the hit as against nothing, in steps of the wall.
				HitWallBySteps(goalPos, WallHitDamage(unit));
				return;
			}
			DebugPrint("Missile-none hits no unit, shouldn't happen!\n");
			return;
		}
		HitUnit(&unit, *goal, CalculateDamage(unit, *goal, Damage.get()));
		return;
	}

	// If Firing from inside a Bunker
	CUnit *from = GetFirstContainer(unit);
	const int dir = ((unit.Direction + NextDirection / 2) & 0xFF) / NextDirection;
	const PixelPos startPixelPos = from->GetMapPixelPosCenter()
								   + unit.Type->MissileOffsets[dir][0];

	if (goal) {
		Assert(goal->Type);  // Target invalid?
		// Moved out of attack range?

		if (unit.MapDistanceTo(*goal) < unit.Type->MinAttackRange) {
			DebugPrint("Missile target too near %d,%d\n",
			           unit.MapDistanceTo(*goal),
			           unit.Type->MinAttackRange);
			// FIXME: do something other?
			return;
		}
	}

	bool overshot = false;
	const PixelPos destPixelPos = MissileImpactPoint(unit, goal, newgoalPos, overshot);
	Missile *missile = MakeMissile(*unit.Type->Missile.Missile, startPixelPos, destPixelPos);
	//
	// Damage of missile
	//
	// A missile that flies over its goal is a missile at a point: it hits what is there.
	if (goal && !overshot) {
		missile->TargetUnit = goal;
	}
	missile->SourceUnit = &unit;
	if (missile->Type->ZoneNoArmor) {
		// The number is drawn at the shot, against the unit fired at.
		missile->Damage = goal ? CalculateDamage(unit, *goal, Damage.get()) : WallHitDamage(unit);
	}
}

/**
**  The point a missile of the unit flies to.
**
**  It is the centre of the goal (its nearest tile for a unit firing from inside
**  a container) or of the tile. A unit type with a MinImpactRange does not bring
**  its missile down nearer than that: a nearer point is moved out along the line
**  from the centre of the firing unit (of its container) to the point until the
**  larger axis offset is MinImpactRange tiles.
**
**  @param unit      Unit that fires.
**  @param goal      Goal unit, or nullptr.
**  @param goalPos   Goal tile when there is no goal unit.
**  @param overshot  Set when the point was moved out.
**
**  @return          the map pixel position of the impact.
*/
PixelPos MissileImpactPoint(const CUnit &unit, const CUnit *goal, const Vec2i &goalPos, bool &overshot)
{
	Vec2i dpos;
	bool divisionOffset = false;
	if (goal) {
		// Fire to nearest point of the unit!
		// If Firing from inside a Bunker
		if (unit.Container) {
			dpos = NearestOfUnit(*goal, GetFirstContainer(unit)->tilePos);
		} else {
			if (goal->Type->TileWidth % 2 == 0)
				divisionOffset = true;
			dpos = goal->tilePos + goal->Type->GetHalfTileSize();
		}
	} else {
		dpos = goalPos;
		// FIXME: Can this be too near??
	}

	PixelPos destPixelPos = Map.TilePosToMapPixelPos_Center(dpos);
	if (divisionOffset)
		destPixelPos -= PixelTileSize/2;

	overshot = false;
	if (unit.Type->MinImpactRange > 0) {
		const CUnit &from = *GetFirstContainer(unit);
		const PixelPos centre = Map.TilePosToMapPixelPos_TopLeft(from.tilePos) + from.Type->GetPixelSize() / 2;
		const PixelPos pushed = PushOutToRange(centre, destPixelPos, unit.Type->MinImpactRange, PixelTileSize);

		overshot = pushed != destPixelPos;
		destPixelPos = pushed;
	}
	return destPixelPos;
}

/**
**  Get area of tiles covered by missile
**
**  @param missile  Missile to be checked and set.
**
**  @return         tiles area in Map
*/
static std::pair<Vec2i, Vec2i> GetMissileMapArea(const Missile &missile)
{
	PixelSize missileSize(missile.Type->Width(), missile.Type->Height());
	PixelDiff margin(PixelTileSize.x - 1, PixelTileSize.y - 1);
	Vec2i boxMin = Map.MapPixelPosToTilePos(missile.position);
	Vec2i boxMax = Map.MapPixelPosToTilePos(missile.position + missileSize + margin);
	Map.Clamp(boxMin);
	Map.Clamp(boxMax);
	return {boxMin, boxMax};
}

/**
**  Check missile visibility in a given viewport.
**
**  @param vp       Viewport to be checked.
**  @param missile  Missile pointer to check if visible.
**
**  @return         Returns true if visible, false otherwise.
*/
static bool MissileVisibleInViewport(const CViewport &vp, const Missile &missile)
{
	const auto [boxmin, boxmax] = GetMissileMapArea(missile);
	if (!vp.AnyMapAreaVisibleInViewport(boxmin, boxmax)) {
		return false;
	}
	Vec2i pos;
	for (pos.x = boxmin.x; pos.x <= boxmax.x; ++pos.x) {
		for (pos.y = boxmin.y; pos.y <= boxmax.y; ++pos.y) {
			if (ReplayRevealMap || Map.Field(pos)->playerInfo.IsTeamVisible(*ThisPlayer)) {
				return true;
			}
		}
	}
	return false;
}

/**
**  Draw missile.
**
**  @param frame  Animation frame
**  @param pos    Screen pixel position
*/
// WargusHD: missile/effect sprite path -> native Remastered effect-atlas frame prefix (e.g.
// "missiles/green_cross.png" -> "blackx"). Baked into hd_missiles_map.json; all effects live in the
// per-tileset units_common_<tileset> atlas, so the prefix is what is stored. Loaded once, lazily.
// Most families use the SAME prefix in every biome (arrow, axe, boom, ...). The catapult rock does
// not: its frames are rock_0 in forest/wasteland but s_rock_0 in winter and x_rock_0 in swamp. So an
// entry may carry optional per-tileset overrides keyed by the effect tileset name; when the current
// tileset has no override the plain prefix is used, which leaves every other entry unchanged.
struct HDMissileEntry {
	std::string prefix;                                   /// default, used when no override matches
	std::map<std::string, std::string> byTileset;         /// effect tileset name -> prefix
};
static std::unordered_map<std::string, HDMissileEntry> g_hdMissileMap; // File -> prefixes
static bool g_hdMissileMapTried = false;

static void LoadHDMissileMap()
{
	if (g_hdMissileMapTried) {
		return;
	}
	g_hdMissileMapTried = true;
	// HD-MISS-MISSILES: every early return here reports, as for the unit map (unittype.cpp).
	// ErrorPrint, not DebugPrint: this must be visible without a runtime flag.
	const fs::path resolved = LibraryFileName("hd_missiles_map.json");
	if (resolved.empty()) {
		ErrorPrint("hd_missiles_map.json NOT FOUND -- no effect will draw its HD sprite\n");
		return;
	}
	std::error_code ec;
	const uintmax_t sz = fs::file_size(resolved, ec);
	if (ec || sz == 0 || sz > (uintmax_t) 4 * 1024 * 1024) {
		ErrorPrint("hd_missiles_map.json unusable size (%s, %llu bytes) -- map not loaded\n",
		           resolved.string().c_str(), (unsigned long long) sz);
		return;
	}
	std::string text;
	{
		auto fp = std::make_unique<CFile>();
		if (fp->open(resolved.string().c_str(), CL_OPEN_READ) == -1) {
			ErrorPrint("hd_missiles_map.json could not be opened (%s) -- map not loaded\n",
			           resolved.string().c_str());
			return;
		}
		text.resize((size_t) sz);
		const bool ok = (size_t) fp->read(&text[0], (size_t) sz) == (size_t) sz;
		fp->close();
		if (!ok) {
			ErrorPrint("hd_missiles_map.json short read (expected %llu bytes) -- map not loaded\n",
			           (unsigned long long) sz);
			return;
		}
	}
	content::JsonValue root;
	std::string jerr;
	if (!content::parseJson(text, root, jerr) || !root.isObject() || !root.object) {
		ErrorPrint("hd_missiles_map.json did not parse as an object (%s) -- map not loaded\n",
		           jerr.c_str());
		return;
	}
	int skipped = 0;
	static const char *const tilesetKeys[] = {"forest", "winter", "swamp", "xswamp"};
	for (const auto &kv : *root.object) {
		const content::JsonValue *p = kv.second.find("prefix");
		if (!p) {
			++skipped;
			ErrorPrint("hd_missiles_map.json entry '%s' has no prefix -- skipped\n", kv.first.c_str());
			continue;
		}
		HDMissileEntry e;
		e.prefix = p->asString();
		for (const char *ts : tilesetKeys) {
			if (const content::JsonValue *o = kv.second.find(ts)) {
				const std::string s = o->asString();
				if (!s.empty()) {
					e.byTileset[ts] = s;
				}
			}
		}
		g_hdMissileMap[kv.first] = std::move(e);
	}
	ErrorPrint("hd_missiles_map.json loaded: %d entries, %d skipped\n",
	           (int) g_hdMissileMap.size(), skipped);
}

// Current map's tileset name in the UNIT/effect atlas namespace (summer->forest, winter->winter).
// D5/NG-TERR1: classic "wasteland"->swamp and classic "swamp"->xswamp -- this was reversed here (it
// disagreed with hd_units_map, where wasteland units live in units_*_swamp and swamp units in
// units_*_xswamp), so effects on swamp/wasteland maps loaded the wrong-biome atlas. Empty when unknown.
std::string CurrentEffectTileset()
{
	const std::string &img = Map.Tileset.ImageFile;
	if (img.find("summer") != std::string::npos) { return "forest"; }
	if (img.find("winter") != std::string::npos) { return "winter"; }
	if (img.find("wasteland") != std::string::npos) { return "swamp"; }
	if (img.find("swamp") != std::string::npos) { return "xswamp"; }
	return "";
}

// EFFECT-ATLAS-LOADED-TWICE: one cache for units and effects (GetSharedHDAtlas, unittype.cpp).
static std::shared_ptr<CAtlasGraphic> GetSharedEffectAtlas(const std::string &stem)
{
	return GetSharedHDAtlas(stem);
}

static void EnsureHDMissileSprite(const MissileType &type)
{
	// HD-ART-STALE-TILESET: the next mission of a campaign runs in the same process; an effect
	// resolved for the biome of the mission before is resolved again for the present one.
	if (type.HDTried && type.HDTileset == CurrentEffectTileset()) {
		return;
	}
	type.HDTried = true;
	type.HDTileset = CurrentEffectTileset();
	type.HDG.reset();
	type.HDPrefix.clear();
	if (!type.G) {
		return;
	}
	LoadHDMissileMap();
	std::string key = type.G->File.string();
	for (char &c : key) {
		if (c == '\\') { c = '/'; }
		c = (char) std::tolower((unsigned char) c);
	}
	auto mit = g_hdMissileMap.find(key);
	if (mit == g_hdMissileMap.end()) {
		// HD-MISS-MISSILES: no map entry, so this effect never takes the HD path. Once per key.
		static std::set<std::string> reportedNoEntry;
		if (reportedNoEntry.insert(key).second) {
			ErrorPrint("HD effect UNMAPPED: '%s' has no hd_missiles_map entry\n", key.c_str());
		}
		return;
	}
	const std::string ts = CurrentEffectTileset();
	if (ts.empty()) {
		// Deliberately silent (INV-3): the tileset is simply not known yet before a map loads.
		// Logging it would emit on every launch and train the reader to ignore this channel.
		return;
	}
	auto atlas = GetSharedEffectAtlas("units_common_" + ts + "_sprites");
	if (atlas && atlas->IsLoaded()) {
		type.HDG = atlas;
		// Per-tileset override when this family is biome-prefixed (catapult rock); plain prefix
		// otherwise, which is every other entry.
		auto oit = mit->second.byTileset.find(ts);
		type.HDPrefix = (oit != mit->second.byTileset.end()) ? oit->second : mit->second.prefix;
	}
}

// HD-ATLAS-CACHE-GROWS: an effect resolved for a biome other than the map's lets its atlas go and
// is resolved again at its next draw (the preload drops that atlas from the shared cache).
void ReleaseHDMissileArtOfOtherBiomes()
{
	const std::string ts = CurrentEffectTileset();
	for (auto &[key, type] : MissileTypes) {
		if (type->HDTried && type->HDTileset != ts) {
			type->HDTried = false;
			type->HDG.reset();
			type->HDPrefix.clear();
		}
	}
}

void MissileType::DrawMissileType(int frame, const PixelPos &pos) const
{
#ifdef DYNAMIC_LOAD
	if (!this->G->IsLoaded(this->Flip)) {
		((MissileType*)this)->LoadMissileSprite();
	}
#endif

	// WargusHD: draw the effect from the native Remastered atlas when one is mapped. The HD frame is
	// scaled to the classic footprint (size * viewport zoom) and re-centred on the same spot, so the
	// effect matches the zoomed world instead of drawing at a tiny native size.
	EnsureHDMissileSprite(*this);
	if (this->HDG && this->HDG->IsLoaded() && !this->HDPrefix.empty()) {
		const int absFrame = (frame < 0) ? (-frame - 1) : frame;
		const std::string name = this->HDPrefix + "_" + std::to_string(absFrame);
		int f = this->HDG->FrameByName(name.c_str());
		// MISSILE-ROCK: a few native effect families number their frames "<prefix>_f<N>" rather than
		// "<prefix>_<N>" -- the catapult rock ships as rock_0 plus rock_0_f0..f4, so with the prefix
		// carrying the _0 only frame 0 could ever resolve and the projectile drew nothing. This is the
		// same fallback the unit path has carried since NG29 (unittype.cpp), tried only when the
		// primary name is absent so nothing that already resolves changes behaviour.
		if (f < 0) {
			f = this->HDG->FrameByName((this->HDPrefix + "_f" + std::to_string(absFrame)).c_str());
		}
		// HD-MISS-MISSILES: neither spelling resolved, so this falls through to the classic blit --
		// which MISSILE-ROCK measured as producing nothing observable for the rock family. Report the
		// two names actually tried; that is the question MISSILE-ROCK cost a cycle to answer. ONCE
		// per prefix+frame, since this is the per-frame draw path.
		if (f < 0) {
			static std::set<std::string> reportedFrameMiss;
			const std::string k = this->HDPrefix + "|" + std::to_string(absFrame);
			if (reportedFrameMiss.insert(k).second) {
				ErrorPrint("HD effect frame MISS: prefix '%s' frame %d -- tried '%s' and '%s'\n",
				           this->HDPrefix.c_str(), absFrame, name.c_str(),
				           (this->HDPrefix + "_f" + std::to_string(absFrame)).c_str());
			}
		}
		if (f >= 0) {
			// SPRITE-SCALE-TRUE R4: the effect sheets are at the same density as the terrain
			// (canvas = classic cell * 6.75 + 120, EVIDENCE-SST-002), so the frame is drawn at
			// PixelTileSize * zoom / HdNativeTilePx with its own canvas centred on the centre of
			// the logical box. `pos` is the screen position of the box's top-left and the box is
			// size * zoom on screen, hence the centre pos + size * zoom / 2. The Lua `Size` stays
			// the logical box (start point, flight, visibility); it does not size the picture.
			const float z = GpuWorldDrawZoom;
			const float s = PixelTileSize.x * z / HdNativeTilePx;
			const float cx = pos.x + this->size.x * z / 2.0f;
			const float cy = pos.y + this->size.y * z / 2.0f;
			this->HDG->DrawAtlasFrameCentred(f, cx, cy, s, frame < 0 && this->Flip);
			return;
		}
	}

	if (this->Flip) {
		if (frame < 0) {
			if (this->Transparency > 0) {
				this->G->DrawFrameClipTransX(-frame - 1, pos.x, pos.y, int(256 - 2.56 * Transparency));
			} else {
				this->G->DrawFrameClipX(-frame - 1, pos.x, pos.y);
			}
		} else {
			if (this->Transparency > 0) {
				this->G->DrawFrameClipTrans(frame, pos.x, pos.y, int(256 - 2.56 * Transparency));
			} else {
				this->G->DrawFrameClip(frame, pos.x, pos.y);
			}
		}
	} else {
		const int row = this->NumDirections / 2 + 1;

		if (frame < 0) {
			frame = ((-frame - 1) / row) * this->NumDirections + this->NumDirections - (-frame - 1) % row;
		} else {
			frame = (frame / row) * this->NumDirections + frame % row;
		}
		if (this->Transparency > 0) {
			this->G->DrawFrameClipTrans(frame, pos.x, pos.y, int(256 - 2.56 * Transparency));
		} else {
			this->G->DrawFrameClip(frame, pos.x, pos.y);
		}
	}
}

/**
**  Draw missile.
*/
void Missile::DrawMissile(const CViewport &vp) const
{
	Assert(this->Type);
	CUnit *sunit = this->SourceUnit;
	// FIXME: I should copy SourcePlayer for second level missiles.
	if (sunit && sunit->Player) {
#ifdef DYNAMIC_LOAD
		if (!this->Type->G->IsLoaded(this->Type->Flip)) {
			((MissileType*)this->Type)->LoadMissileSprite();
		}
#endif
	}
	const PixelPos screenPixelPos = vp.MapToScreenPixelPos(this->position);

	switch (this->Type->Class) {
		case MissileClass::Hit:
			CLabel(GetGameFont()).DrawClip(screenPixelPos.x, screenPixelPos.y, this->Damage);
			break;
		default:
			if (Type->G) {
				this->Type->DrawMissileType(this->SpriteFrame, screenPixelPos);
			}
			break;
	}
}

static bool MissileDrawLevelCompare(const Missile *const l, const Missile *const r)
{
	if (l->Type->DrawLevel == r->Type->DrawLevel) {
		return l->Slot < r->Slot;
	} else {
		return l->Type->DrawLevel < r->Type->DrawLevel;
	}
}

/**
**  Sort visible missiles on map for display.
**
**  @param vp         Viewport pointer.
**  @return array of missile to display sorted by DrawLevel.
*/
std::vector<Missile *> FindAndSortMissiles(const CViewport &vp)
{
	std::vector<Missile *> table;
	// Loop through global missiles, then through locals.
	for (auto& missilePtr : GlobalMissiles) {
		Missile &missile = *missilePtr;
		if (missile.Delay || missile.Hidden) {
			continue;  // delayed or hidden -> aren't shown
		}
		// Draw only visible missiles
		if (MissileVisibleInViewport(vp, missile)) {
			table.push_back(&missile);
		}
	}

	for (auto& missilePtr : LocalMissiles) {
		Missile &missile = *missilePtr;
		if (missile.Delay || missile.Hidden) {
			continue;  // delayed or hidden -> aren't shown
		}
		// Local missile are visible.
		table.push_back(&missile);
	}
	ranges::sort(table, MissileDrawLevelCompare);
	return table;
}

/**
**  Change missile heading from x,y.
**
**  @param delta    Delta movement
**
**  @internal We have : SpriteFrame / (2 * (Numdirection - 1)) == DirectionToHeading / 256.
*/
void Missile::MissileNewHeadingFromXY(const PixelPos &delta)
{
	if (this->Type->NumDirections == 1 || (delta.x == 0 && delta.y == 0)) {
		return;
	}

	if (this->SpriteFrame < 0) {
		this->SpriteFrame = -this->SpriteFrame - 1;
	}
	this->SpriteFrame /= this->Type->NumDirections / 2 + 1;
	this->SpriteFrame *= this->Type->NumDirections / 2 + 1;

	const int nextdir = 256 / this->Type->NumDirections;
	Assert(nextdir != 0);
	const int dir = ((DirectionToHeading(delta) + nextdir / 2) & 0xFF) / nextdir;
	if (dir <= LookingS / nextdir) { // north->east->south
		this->SpriteFrame += dir;
	} else {
		this->SpriteFrame += 256 / nextdir - dir;
		this->SpriteFrame = -this->SpriteFrame - 1;
	}
}

/**
**  Init the move.
**
**  @param missile  missile to initialise for movement.
**
**  @return         true if goal is reached, false else.
*/
bool MissileInitMove(Missile &missile, bool pointToPoint)
{
	const PixelPos source = pointToPoint ? missile.source : missile.position;
	const PixelPos heading = missile.destination - source;

	missile.MissileNewHeadingFromXY(heading);
	if (!(missile.State & 1)) {
		missile.CurrentStep = 0;
		missile.TotalStep = 0;
		if (heading.x == 0 && heading.y == 0) {
			return true;
		}
		// initialize
		missile.TotalStep = Distance(missile.source, missile.destination);
		missile.State++;
		return false;
	}
	Assert(missile.TotalStep != 0);
	if (missile.Type->Class == MissileClass::Hit) {
		// the damage number is not a missile of the original: its own pixels
		missile.CurrentStep += missile.Type->Speed;
	} else {
		// Speed is in pixels of the classic 32 pixel tile along the longer axis of the line;
		// a bouncing missile goes at half of it close to its aimed point and behind it.
		const PixelPos line = missile.destination - missile.source;
		const int major = std::max(abs(line.x), abs(line.y));
		const int left = (missile.TotalStep - missile.CurrentStep) * major / missile.TotalStep;
		const bool slow = missile.Type->Class == MissileClass::PointToPointBounce
		                  && (missile.State > 1 || left < MissileSlowDistance * PixelTileSize.x / 32);
		missile.CurrentStep += MissileFlightStep(missile.Type->Speed, PixelTileSize.x, major, missile.TotalStep, slow);
	}
	if (missile.CurrentStep >= missile.TotalStep) {
		missile.CurrentStep = missile.TotalStep;
		return true;
	}
	return false;
}

void MissileHandlePierce(Missile &missile, const Vec2i &pos)
{
	if (Map.Info.IsPointOnMap(pos) == false) {
		return;
	}
	std::vector<CUnit *> units = Select(pos, pos);
	for (CUnit *unitPtr : units) {
		CUnit &unit = *unitPtr;

		if (unit.IsAliveOnMap()
			&& (missile.Type->FriendlyFire == false || unit.IsEnemy(*missile.SourceUnit->Player))) {
			missile.MissileHit(&unit);
		}
	}
}

bool MissileHandleBlocking(Missile &missile, const PixelPos &position)
{
	const MissileType &mtype = *missile.Type;
	if (missile.SourceUnit) {
		bool shouldHit = false;
		if (missile.TargetUnit
		    && missile.SourceUnit->Type->MoveType == missile.TargetUnit->Type->MoveType) {
			shouldHit = true;
		}
		if (mtype.Range && mtype.CorrectSphashDamage) {
			shouldHit = true;
		}
		if (shouldHit) {
			// search for blocking units
			const Vec2i missilePos = Map.MapPixelPosToTilePos(position);
			std::vector<CUnit *> blockingUnits = Select(missilePos, missilePos);
			for (CUnit *unitPtr : blockingUnits) {
				CUnit &unit = *unitPtr;
				// If land unit shoots at land unit, missile can be blocked by Wall units
				if (!missile.Type->IgnoreWalls
				    && missile.SourceUnit->Type->MoveType == EMovement::Land) {
					if (!missile.TargetUnit
					    || missile.TargetUnit->Type->MoveType == EMovement::Land) {
						if (&unit != missile.SourceUnit && unit.Type->BoolFlag[WALL_INDEX].value
						    && unit.Player != missile.SourceUnit->Player
						    && unit.IsAllied(*missile.SourceUnit) == false) {
							if (missile.TargetUnit) {
								missile.TargetUnit = &unit;
								if (unit.Type->TileWidth == 1 || unit.Type->TileHeight == 1) {
									missile.position =
										Map.TilePosToMapPixelPos_TopLeft(unit.tilePos);
								}
							} else {
								missile.position = position;
							}
							missile.DestroyMissile = 1;
							return true;
						}
					}
				}
				// missile can kill any unit on it's way
				if (missile.Type->KillFirstUnit && &unit != missile.SourceUnit) {
					// can't kill non-solid or dead units
					if (unit.IsAliveOnMap() == false || unit.Type->BoolFlag[NONSOLID_INDEX].value) {
						continue;
					}
					if (missile.Type->FriendlyFire == false || unit.IsEnemy(*missile.SourceUnit->Player)) {
						missile.TargetUnit = &unit;
						if (unit.Type->TileWidth == 1 || unit.Type->TileHeight == 1) {
							missile.position = Map.TilePosToMapPixelPos_TopLeft(unit.tilePos);
						}
						missile.DestroyMissile = 1;
						return true;
					}
				}
			}
		}
	}
	return false;
}

/**
**  Handle point to point missile.
**
**  @param missile  Missile pointer.
**
**  @return         true if goal is reached, false else.
*/
bool PointToPointMissile(Missile &missile)
{
	MissileInitMove(missile, true);
	if (missile.TotalStep == 0) {
		return true;
	}
	Assert(missile.Type != nullptr);
	Assert(missile.TotalStep != 0);

	const PixelPos diff = (missile.destination - missile.source);
	const PixelPrecise sign(diff.x >= 0 ? 1.0 : -1.0, diff.y >= 0 ? 1.0 : -1.0); // Remember sign to move into correct direction
	const PixelPrecise oldPos((double)missile.position.x, (double)missile.position.y); // Remember old position
	PixelPrecise pos(oldPos);
	missile.position = missile.source + diff * missile.CurrentStep / missile.TotalStep;

	for (; pos.x * sign.x <= missile.position.x * sign.x
		 && pos.y * sign.y <= missile.position.y * sign.y;
		 pos.x += (double)diff.x * missile.Type->SmokePrecision / missile.TotalStep,
		 pos.y += (double)diff.y * missile.Type->SmokePrecision / missile.TotalStep) {
		const PixelPos position((int)pos.x + missile.Type->size.x / 2,
								(int)pos.y + missile.Type->size.y / 2);

		if (missile.Type->Smoke.Missile && (missile.CurrentStep || missile.State > 1)) {
			Missile *smoke = MakeMissile(*missile.Type->Smoke.Missile, position, position);
			if (smoke && smoke->Type->NumDirections > 1) {
				smoke->MissileNewHeadingFromXY(diff);
			}
		}

		if (missile.Type->SmokeParticle && (missile.CurrentStep || missile.State > 1)) {
			missile.Type->SmokeParticle(position.x, position.y);
		}

		if (missile.Type->Pierce) {
			const PixelPos posInt((int)pos.x, (int)pos.y);
			MissileHandlePierce(missile, Map.MapPixelPosToTilePos(posInt));
		}
	}

	// Handle wall blocking and kill first enemy
	for (pos = oldPos; pos.x * sign.x <= missile.position.x * sign.x
		 && pos.y * sign.y <= missile.position.y * sign.y;
		 pos.x += (double)diff.x / missile.TotalStep,
		 pos.y += (double)diff.y / missile.TotalStep) {
		const PixelPos position((int)pos.x + missile.Type->size.x / 2,
								(int)pos.y + missile.Type->size.y / 2);
		const Vec2i tilePos(Map.MapPixelPosToTilePos(position));

		if (Map.Info.IsPointOnMap(tilePos) && MissileHandleBlocking(missile, position)) {
			return true;
		}
		if (missile.Type->MissileStopFlags) {
			if (!Map.Info.IsPointOnMap(tilePos)) { // gone outside
				missile.TTL = 0;
				return false;
			}
			const CMapField &mf = *Map.Field(tilePos);
			if (missile.Type->MissileStopFlags & mf.getFlags()) { // incompatible terrain
				missile.position = position;
				missile.MissileHit();
				missile.TTL = 0;
				return false;
			}
		}
	}

	if (missile.CurrentStep == missile.TotalStep) {
		missile.position = missile.destination;
		return true;
	}
	return false;
}

/**
**  Missile hits the goal.
**
**  @param missile  Missile hitting the goal.
**  @param goal     Goal of the missile.
**  @param splash   Splash damage divisor.
*/
static void MissileHitsGoal(const Missile &missile, CUnit &goal, int splash)
{
	if (!missile.Type->CanHitOwner && missile.SourceUnit == &goal) {
		return;
	}

	if (goal.CurrentAction() != UnitAction::Die) {
		int damage;

		if (missile.Type->Damage) {   // custom formula
			Assert(missile.SourceUnit != nullptr);
			damage = CalculateDamage(*missile.SourceUnit, goal, missile.Type->Damage.get()) / splash;
		} else if (missile.Damage) {  // direct damage, spells mostly
			damage = missile.Damage / splash;
		} else {
			Assert(missile.SourceUnit != nullptr);
			damage = CalculateDamage(*missile.SourceUnit, goal, Damage.get()) / splash;
		}
		if (missile.Type->Pierce) {  // Handle pierce factor
			for (size_t i = 0; i < (missile.PiercedUnits.size() - 1); ++i) {
				damage *= (double)missile.Type->ReduceFactor / 100;
			}
		}

		HitUnit(missile.SourceUnit, goal, damage, &missile);
	}
}

/**
**  Missile hits wall.
**
**  @param missile  Missile hitting the goal.
**  @param tilePos  Wall map tile position.
**  @param splash   Splash damage divisor.
**
**  @todo FIXME: Support for more races.
*/
static void MissileHitsWall(const Missile &missile, const Vec2i &tilePos, int splash)
{
	if (!Map.WallOnMap(tilePos)) {
		return;
	}
	if (missile.Damage) {  // direct damage, spells mostly
		HitWallBySteps(tilePos, missile.Damage / splash);
		return;
	}

	Assert(missile.SourceUnit != nullptr);
	// A wall has no armor: the hit as against nothing, in steps of the wall.
	HitWallBySteps(tilePos, WallHitDamage(*missile.SourceUnit) / splash);
}

/**
**  A zone-splash missile hits a wall tile. A wall stands 20 steps; a hit takes
**  a quarter of its damage in steps, at least one.
**
**  @param tilePos  Map tile position.
**  @param damage   Damage that reaches the tile.
*/
static void SplashHitsWall(const Vec2i &tilePos, int damage)
{
	HitWallBySteps(tilePos, damage);
}

/**
**  A zone-splash missile comes down: every unit around the impact point is hit
**  by the zone its centre lies in.
**
**  @param missile  Missile that comes down.
**  @param impact   Map pixel position of the impact.
**  @param pos      Map tile of the impact.
*/
static void MissileHitsZones(const Missile &missile, const PixelPos &impact, const Vec2i &pos)
{
	const CUnit &source = *missile.SourceUnit;
	// A finished number is carried as it is and meets no armor; flying units are in its area.
	const bool finished = missile.Type->ZoneNoArmor;
	const int raw = finished ? std::min<int>(missile.Damage, 255)
							 : SplashRawDamage(source.Stats->Variables[BASICDAMAGE_INDEX].Value,
											   source.Stats->Variables[PIERCINGDAMAGE_INDEX].Value,
											   source.Variable[BLOODLUST_INDEX].Value != 0);
	const bool aimedAtFlying = finished || (missile.TargetUnit != nullptr
											&& missile.TargetUnit->Type->MoveType == EMovement::Fly);

	if (raw <= 0) {
		return;
	}
	// The unit a missile belongs to is left out: the shooter, for a flame of the Flame Shield the
	// unit it circles.
	const CUnit *spared = missile.Type->Class == MissileClass::FlameShield ? static_cast<CUnit *>(missile.TargetUnit)
	                                                                       : static_cast<CUnit *>(missile.SourceUnit);

	// The wall under the impact takes the whole damage, the four beside it a quarter.
	SplashHitsWall(pos, raw);
	SplashHitsWall(pos + Vec2i(1, 0), raw >> 2);
	SplashHitsWall(pos + Vec2i(-1, 0), raw >> 2);
	SplashHitsWall(pos + Vec2i(0, 1), raw >> 2);
	SplashHitsWall(pos + Vec2i(0, -1), raw >> 2);

	// Three tiles each way hold every unit whose centre can lie in the outer zone.
	const Vec2i range(3, 3);
	std::vector<CUnit *> table = Select(pos - range, pos + range);
	for (CUnit *goal : table) {
		if (!goal->IsAliveOnMap() || goal->CurrentAction() == UnitAction::Die
			|| !SplashTakesUnit(goal == spared, goal->Type->MoveType == EMovement::Fly, aimedAtFlying)) {
			continue;
		}
		const int zone = SplashZone(impact, goal->GetMapPixelPosCenter(), PixelTileSize);
		if (zone < 0) {
			continue;
		}
		const int armor = finished ? 0 : goal->Stats->Variables[ARMOR_INDEX].Value;
		// No draw for a unit whose armor takes it all.
		if (SplashLeft(raw, zone, armor) <= 0) {
			continue;
		}
		HitUnit(missile.SourceUnit, *goal, SplashDamage(raw, zone, armor, SyncRand()), &missile);
	}
}

/**
**  Check if missile has already pierced that unit
**
**  @param missile  Current missile.
**  @param unit     Target unit.
**
**  @return         true if goal is pierced, false else.
*/

static bool IsPiercedUnit(const Missile &missile, const CUnit &unit)
{
	for (const CUnit *punit : missile.PiercedUnits) {
		if (UnitNumber(unit) == UnitNumber(*punit)) {
			return true;
		}
	}
	return false;
}

/**
**  Work for missile hit.
*/
void Missile::MissileHit(CUnit *unit)
{
	const MissileType &mtype = *this->Type;

	if (mtype.ImpactSound.Sound) {
		PlayMissileSound(*this, mtype.ImpactSound.Sound.get());
	}
	const PixelPos pixelPos = this->position + this->Type->size / 2;

	//
	// The impact generates a new missile.
	//
	if (mtype.Impact.empty() == false) {
		for (const auto &mc : mtype.Impact) {
			Missile *impact = MakeMissile(*mc.Missile, pixelPos, pixelPos);
			if (impact && impact->Type->Damage) {
				impact->SourceUnit = this->SourceUnit;
			}
		}
	}
	if (mtype.ImpactParticle) {
		mtype.ImpactParticle(pixelPos.x, pixelPos.y);
	}

	if (!this->SourceUnit) {  // no owner - green-cross ...
		return;
	}

	const Vec2i pos = Map.MapPixelPosToTilePos(pixelPos);

	if (!Map.Info.IsPointOnMap(pos)) {
		// FIXME: this should handled by caller?
		DebugPrint("Missile gone outside of map!\n");
		return;  // outside the map.
	}

	//
	// Choose correct goal.
	//
	if (unit) {
		if (unit->Destroyed) {
			return;
		}
		if (mtype.Pierce && mtype.PierceOnce) {
			if (IsPiercedUnit(*this, *unit)) {
				return;
			} else {
				PiercedUnits.insert(this->PiercedUnits.begin(), unit);
			}
		}
		MissileHitsGoal(*this, *unit, 1);
		return;
	}
	if (mtype.ZoneSplash) {
		MissileHitsZones(*this, pixelPos, pos);
		return;
	}
	if (this->TargetUnit
		&& AimedHitTakes(mtype.AimedHit, this->TargetUnit->IsAliveOnMap(),
						 mtype.FriendlyFire && this->TargetUnit->Player->Index == this->SourceUnit->Player->Index)) {
		//
		// The unit fired at takes the hit where it stands now.
		//
		MissileHitsGoal(*this, *this->TargetUnit, 1);
		return;
	}
	if (!mtype.Range) {
		if (this->TargetUnit && (mtype.FriendlyFire == false
								 || this->TargetUnit->Player->Index != this->SourceUnit->Player->Index)) {
			//
			// Missiles without range only hits the goal always.
			//
			CUnit &goal = *this->TargetUnit;
			if (mtype.Pierce && mtype.PierceOnce) {
				if (IsPiercedUnit(*this, goal)) {
					return;
				} else {
					PiercedUnits.insert(this->PiercedUnits.begin(), &goal);
				}
			}
			if (goal.Destroyed) {
				this->TargetUnit = nullptr;
				return;
			}
			MissileHitsGoal(*this, goal, 1);
			return;
		}
		MissileHitsWall(*this, pos, 1);
		return;
	}

	{
		//
		// Hits all units in range.
		//
		const Vec2i range(mtype.Range - 1, mtype.Range - 1);
		std::vector<CUnit *> table = Select(pos - range, pos + range);
		Assert(this->SourceUnit != nullptr);
		for (CUnit *goal : table) {
			//
			// Can the unit attack this unit-type?
			// NOTE: perhaps this should be come a property of the missile.
			// Also check CorrectSphashDamage so land explosions can't hit the air units
			//
			if (CanTarget(*this->SourceUnit->Type, *goal->Type)
				&& (mtype.FriendlyFire == false || goal->Player->Index != this->SourceUnit->Player->Index)) {
				bool shouldHit = true;

				if (mtype.Pierce && mtype.PierceOnce) {
					if (IsPiercedUnit(*this, *goal)) {
						shouldHit = false;
					} else {
						PiercedUnits.insert(this->PiercedUnits.begin(), goal);
					}
				}

				if (mtype.CorrectSphashDamage == true) {
					bool isPosition = false;
					if (this->TargetUnit == nullptr) {
						if (this->SourceUnit->CurrentAction() == UnitAction::SpellCast) {
							const COrder_SpellCast &order = *static_cast<COrder_SpellCast *>(this->SourceUnit->CurrentOrder());
							if (order.GetSpell().Target == ETarget::Position) {
								isPosition = true;
							}
						} else {
							isPosition = true;
						}
					}
					if (isPosition || this->SourceUnit->CurrentAction() == UnitAction::AttackGround) {
						if (goal->Type->MoveType != this->SourceUnit->Type->MoveType) {
							shouldHit = false;
						}
					} else {
						if (this->TargetUnit == nullptr
						    || goal->Type->MoveType != this->TargetUnit->Type->MoveType) {
							shouldHit = false;
						}
					}
				}
				if (shouldHit) {
					int splash = goal->MapDistanceTo(pos);

					if (splash) {
						splash *= mtype.SplashFactor;
					} else {
						splash = 1;
					}
					MissileHitsGoal(*this, *goal, splash);
				}
			}
		}
	}

	// Missile hits ground.
	const Vec2i offset(mtype.Range, mtype.Range);
	const Vec2i posmin = pos - offset;
	for (int i = mtype.Range * 2; --i;) {
		for (int j = mtype.Range * 2; --j;) {
			const Vec2i posIt(posmin.x + i, posmin.y + j);

			if (Map.Info.IsPointOnMap(posIt)) {
				int d = Distance(pos, posIt);
				d *= mtype.SplashFactor;
				if (d == 0) {
					d = 1;
				}
				MissileHitsWall(*this, posIt, d);
			}
		}
	}
}

/**
**  Pass to the next frame for animation.
**
**  @param sign           1 for next frame, -1 for previous frame.
**  @param longAnimation  1 if Frame is conditioned by covered distance, 0 else.
**
**  @return               true if animation is finished, false else.
*/
bool Missile::NextMissileFrame(char sign, char longAnimation)
{
	int neg = 0; // True for mirroring sprite.
	bool animationIsFinished = false;
	int numDirections = this->Type->NumDirections / 2 + 1;
	if (this->SpriteFrame < 0) {
		neg = 1;
		this->SpriteFrame = -this->SpriteFrame - 1;
	}
	if (longAnimation) {
		// Total distance to cover.
		const int totalx = Distance(this->destination, this->source);
		// Covered distance.
		const int dx = Distance(this->position, this->source);
		// Total number of frame (for one direction).
		const int totalf = this->Type->SpriteFrames / numDirections;
		// Current frame (for one direction).
		const int df = this->SpriteFrame / numDirections;

		if ((sign == 1 && dx * totalf <= df * totalx)
			|| (sign == -1 && dx * totalf > df * totalx)) {
			return animationIsFinished;
		}
	}
	this->SpriteFrame += sign * numDirections;
	if (sign > 0) {
		if (this->SpriteFrame >= this->Type->SpriteFrames) {
			this->SpriteFrame -= this->Type->SpriteFrames;
			animationIsFinished = true;
		}
	} else {
		if (this->SpriteFrame < 0) {
			this->SpriteFrame += this->Type->SpriteFrames;
			animationIsFinished = true;
		}
	}
	if (neg) {
		this->SpriteFrame = -this->SpriteFrame - 1;
	}
	return animationIsFinished;
}

/**
**  Pass the next frame of the animation.
**  This animation goes from start to finish ONCE on the way
*/
void Missile::NextMissileFrameCycle()
{
	int neg = 0;

	if (this->SpriteFrame < 0) {
		neg = 1;
		this->SpriteFrame = -this->SpriteFrame - 1;
	}
	const int totalx = abs(this->destination.x - this->source.x);
	const int dx = abs(this->position.x - this->source.x);
	int f = this->Type->SpriteFrames / (this->Type->NumDirections / 2 + 1);
	f = 2 * f - 1;
	for (int i = 1, j = 1; i <= f; ++i) {
		if (dx * f / i < totalx) {
			if ((i - 1) * 2 < f) {
				j = i - 1;
			} else {
				j = f - i;
			}
			this->SpriteFrame = this->SpriteFrame % (this->Type->NumDirections / 2 + 1) +
								j * (this->Type->NumDirections / 2 + 1);
			break;
		}
	}
	if (neg) {
		this->SpriteFrame = -this->SpriteFrame - 1;
	}
}

/**
**  Handle all missile actions of global/local missiles.
**
**  @param missiles  Table of missiles.
*/
static void MissilesActionLoop(std::vector<std::unique_ptr<Missile>> &missiles)
{
	for (size_t i = 0; i != missiles.size(); /* empty */) {
		Missile &missile = *missiles[i];

		if (missile.Delay) {
			missile.Delay--;
			++i;
			continue;  // delay start of missile
		}
		if (missile.TTL > 0) {
			missile.TTL--;  // overall time to live if specified
		}
		if (missile.TTL == 0) {
			missiles.erase(missiles.begin() + i);
			continue;
		}
		Assert(missile.Wait);
		if (--missile.Wait) {  // wait until time is over
			++i;
			continue;
		}
		missile.Action(); // may create other missiles, and so modifies the array
		if (missile.TTL == 0) {
			missiles.erase(missiles.begin() + i);
			continue;
		}
		++i;
	}
}

/**
**  Handle all missile actions.
*/
void MissileActions()
{
	MissilesActionLoop(GlobalMissiles);
	MissilesActionLoop(LocalMissiles);
}

/**
**  Calculate distance from view-point to missile.
**
**  @param missile  Missile pointer for distance.
**
**  @return the computed value.
*/
int ViewPointDistanceToMissile(const Missile &missile)
{
	const PixelPos pixelPos = missile.position + missile.Type->size / 2;
	const Vec2i tilePos = Map.MapPixelPosToTilePos(pixelPos);

	return ViewPointDistance(tilePos);
}

/**
**  Get the burning building missile based on hp percent.
**
**  @param percent  HP percent
**
**  @return  the missile used for burning.
*/
MissileType *MissileBurningBuilding(int percent)
{
	const auto it = ranges::upper_bound(
		std::begin(BurningBuildingFrames),
		std::end(BurningBuildingFrames),
		percent,
		std::less<>{},
		&BurningBuildingFrame::Percent
	);
	if (it != std::begin(BurningBuildingFrames)) {
		return std::prev(it)->Missile;
	}
	return nullptr;
}

/**
**  Save a specific pos.
*/
static void SavePixelPos(CFile &file, const PixelPos &pos)
{
	file.printf("{%d, %d}", pos.x, pos.y);
}


/**
**  Save the state of a missile to file.
**
**  @param file  Output file.
*/
void Missile::SaveMissile(CFile &file) const
{
	file.printf("Missile(\"type\", \"%s\",", this->Type->Ident.c_str());
	file.printf(" \"%s\",", this->Local ? "local" : "global");
	file.printf(" \"pos\", ");
	SavePixelPos(file, this->position);
	file.printf(", \"origin-pos\", ");
	SavePixelPos(file, this->source);
	file.printf(", \"goal\", ");
	SavePixelPos(file, this->destination);
	file.printf(",\n  \"frame\", %d, \"state\", %d, \"anim-wait\", %d, \"wait\", %d, \"delay\", %d,\n ",
				this->SpriteFrame, this->State, this->AnimWait, this->Wait, this->Delay);
	if (this->SourceUnit != nullptr) {
		file.printf(" \"source\", \"%s\",", UnitReference(*this->SourceUnit).c_str());
	}
	if (this->TargetUnit != nullptr) {
		file.printf(" \"target\", \"%s\",", UnitReference(*this->TargetUnit).c_str());
	}
	file.printf(" \"damage\", %d,", this->Damage);
	file.printf(" \"ttl\", %d,", this->TTL);
	if (this->Hidden) {
		file.printf(" \"hidden\", ");
	}
	if (!this->PiercedUnits.empty()) {
		file.printf(" \"pierced\", {");
		for (const CUnit *pierced : this->PiercedUnits) {
			file.printf("\"%s\", ", UnitReference(*pierced).c_str());
		}
		file.printf("},");
	}
	if (this->DestroyMissile) {
		file.printf(" \"destroy\", ");
	}
	file.printf(" \"step\", {%d, %d}", this->CurrentStep, this->TotalStep);

	// Slot filled in during init
	file.printf(")\n");
}

/**
**  Save the state missiles to file.
**
**  @param file  Output file.
*/
void SaveMissiles(CFile &file)
{
	file.printf("\n--- -----------------------------------------\n");
	file.printf("--- MODULE: missiles\n\n");

	for (const auto &missile : GlobalMissiles) {
		missile->SaveMissile(file);
	}
	for (const auto &missile : LocalMissiles) {
		missile->SaveMissile(file);
	}
}

/**
**  Initialize missile type.
*/
void MissileType::Init()
{
	// Resolve impact missiles and sounds.
	this->FiredSound.MapSound();
	this->ImpactSound.MapSound();
	for (auto &mc : this->Impact) {
		mc.MapMissile();
	}
	this->Smoke.MapMissile();
}

/**
**  Initialize missile-types.
*/
void InitMissileTypes()
{
	for (auto &[key, value] : MissileTypes) {
		value->Init();
	}
}

/**
**  Clean up missile-types.
*/
void CleanMissileTypes()
{
	MissileTypes.clear();
}

/**
**  Initialize missiles.
*/
void InitMissiles()
{
}

/**
**  Clean up missiles.
*/
void CleanMissiles()
{
	GlobalMissiles.clear();
	LocalMissiles.clear();
}

template <typename Range>
bool IsPercentIncreasingValid(const Range &range)
{
	bool ret = true;
	if (!range.empty())
	{
		ret = range.front().Percent >= 0
			&& range.back().Percent <= 100;
	}

	return ret && ranges::is_sorted(
		range,
		std::less<>{},
		[](const auto& e) { return e.Percent; });
}

bool IsBurningBuildingFramesValid()
{
	return IsPercentIncreasingValid(BurningBuildingFrames);
}

void FreeBurningBuildingFrames()
{
	BurningBuildingFrames.clear();
}

//@}
