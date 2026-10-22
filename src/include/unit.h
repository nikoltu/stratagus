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
/**@name unit.h - The unit headerfile. */
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

#ifndef __UNIT_H__
#define __UNIT_H__

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "actions.h"
#include "idle_bob.h"
#include "pathfinder.h"
#include "settings.h"

#ifndef __UNITTYPE_H__
#include "unittype.h"
#endif

#ifndef __PLAYER_H__
#include "player.h"
#endif

#include "vec2i.h"

#include <optional>
#include <vector>

/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/

class CAnimation;
class CBuildRestrictionOnTop;
class CConstructionFrame;
class CFile;
class Missile;
class CMapField;
class CPlayer;
class CUnit;
class CUnitColors;
class CUnitPtr;
class CUnitStats;
class CUnitType;
class CUpgrade;
class CVariable;
class CViewport;
class PathFinderData;
class SpellType;
enum class UnitAction : char;
struct lua_State;

using COrderPtr = COrder *;

/*
** Configuration of the small (unit) AI.
** sPPP PPdd dddd dddd 0000 0000 0hhh hhhh
** s... .ppp pppp p... .... .... .... ....
** s... ...I .... .... iiii i... .... ....
** s... .... .... c... .... .... .... ....
*/
#define PRIORITY_FACTOR   0x00080000   /// p
#define HEALTH_FACTOR     0x00000001   /// h (0..100)%
#define DISTANCE_FACTOR   0x00010000   /// d (0..1023)
#define INRANGE_FACTOR    0x00008000   /// i (0..31)
#define INRANGE_BONUS     0x01000000   /// I
#define CANATTACK_BONUS   0x00080000   /// c
#define AIPRIORITY_BONUS  0x04000000   /// P (0..31)

/*
** Same for alternate (simplified) implementation of the small (unit) AI.
** sAT0 0000  0ppp pppp  pddd dddd  dhhh hhhh
*/
#define AT_ATTACKED_BY_FACTOR 0x40000000 /// A (attacker is under attack by target)
#define AT_THREAT_FACTOR      0x20000000 /// T
#define AT_PRIORITY_OFFSET    15         /// p (0..255)
#define AT_DISTANCE_OFFSET    7          /// d (0..255)
#define AT_PRIORITY_MASK_HI   0xFFFF8000 /// Mask for checking only priority (without distance part)

#define AT_FARAWAY_REDUCE_OFFSET 14      /// Priority reduce offset for far away targets (AT_THREAT_FACTOR must be preserved if present)

/// Called whenever the selected unit was updated
extern void SelectedUnitChanged();

/// Returns the map distance between to unittype as locations
extern int MapDistanceBetweenTypes(const CUnitType &src, const Vec2i &pos1,
								   const CUnitType &dst, const Vec2i &pos2);
/// Returns the attack-range distance between two unittypes as locations
extern int AttackDistanceBetweenTypes(const CUnitType &src, const Vec2i &pos1,
									  const CUnitType &dst, const Vec2i &pos2);

/**
**  Unit/Missile headings.
**          N
**  NW              NE
**  W                E
**  SW              SE
**          S
*/
enum _directions_ {
	LookingN  = 0 * 32,      /// Unit looking north
	LookingNE = 1 * 32,      /// Unit looking north east
	LookingE  = 2 * 32,      /// Unit looking east
	LookingSE = 3 * 32,      /// Unit looking south east
	LookingS  = 4 * 32,      /// Unit looking south
	LookingSW = 5 * 32,      /// Unit looking south west
	LookingW  = 6 * 32,      /// Unit looking west
	LookingNW = 7 * 32       /// Unit looking north west
};

#define NextDirection 32        /// Next direction N->NE->E...
#define UnitNotSeen 0x7fffffff  /// Unit not seen, used by CUnit::SeenFrame

/// The big unit structure
class CUnit
{
public:
	CUnit() { Init(); }
	~CUnit() = default;

	void Init();

	COrder *CurrentOrder() const { return Orders[0].get(); }

	UnitAction CurrentAction() const;

	bool IsIdle() const;

	void ClearAction();

	/// Increase a unit's reference count
	void RefsIncrease();
	/// Decrease a unit's reference count
	void RefsDecrease();

	/// Initialize unit structure with default values
	void Init(const CUnitType &type);
	/// Assign unit to player
	void AssignToPlayer(CPlayer &player);

	/// Draw a single unit
	void Draw(const CViewport &vp) const;
	/// Place a unit on map
	void Place(const Vec2i &pos);

	/// Move unit to tile(pos). (Do special stuff : vision, cachelist, pathfinding)
	void MoveToXY(const Vec2i &pos);
	/// Add a unit inside a container. Only deal with list stuff.
	void AddInContainer(CUnit &host);
	/// Change owner of unit
	void ChangeOwner(CPlayer &newplayer);

	/// Remove unit from map/groups/...
	void Remove(CUnit *host);

	void AssignWorkerToMine(CUnit &mine);
	void DeAssignWorkerFromMine(CUnit &mine);

	/// Release a unit
	void Release(bool final = false);

	bool RestoreOrder();
	/// Does the saved order hold a remembered patrol or attack-move (kept while the unit stands)?
	bool SavedOrderIsMemory() const;
	bool CanStoreOrder(COrder *order);

	// Cowards and invisible units don't attack unless ordered.
	bool IsAggressive() const
	{
		return (Type->BoolFlag[CANATTACK_INDEX].value && !Type->BoolFlag[COWARD_INDEX].value
				&& Variable[INVISIBLE_INDEX].Value == 0);
	}

	/// Returns true, if unit is directly seen by an allied unit.
	bool IsVisible(const CPlayer &player) const;

	bool IsInvisible(const CPlayer &player) const
	{
		return (&player != Player && Variable[INVISIBLE_INDEX].Value > 0
				&& !Player->HasSharedVisionWith(player));
	}

	/**
	**  Returns true if unit is alive.
	**  Another unit can interact only with alive map units.
	**
	**  @return        True if alive, false otherwise.
	*/
	bool IsAlive() const;

	/**
	**  Returns true if unit is alive and on the map.
	**  Another unit can interact only with alive map units.
	**
	**  @return        True if alive, false otherwise.
	*/
	bool IsAliveOnMap() const { return !Removed && IsAlive(); }

	/**
	**  Returns true, if unit is visible as an action goal for a player on the map.
	**
	**  @param player  Player to check for.
	**
	**  @return        True if visible, false otherwise.
	*/
	bool IsVisibleAsGoal(const CPlayer &player) const
	{
		// Invisibility
		if (IsInvisible(player)) {
			return false;
		}
		// Don't attack revealers
		if (this->Type->BoolFlag[REVEALER_INDEX].value) {
			return false;
		}
		if ((player.Type == PlayerTypes::PlayerComputer && !this->Type->BoolFlag[PERMANENTCLOAK_INDEX].value)
			|| IsVisible(player) || IsVisibleOnRadar(player)) {
			return IsAliveOnMap();
		} else {
			return Type->BoolFlag[VISIBLEUNDERFOG_INDEX].value
				   && (Seen.ByPlayer & (1 << player.Index))
				   && !(Seen.Destroyed & (1 << player.Index));
		}
	}

	/**
	**  Returns true, if unit is visible for this player on the map.
	**  The unit has to be out of fog of war and alive
	**
	**  @param player  Player to check for.
	**
	**  @return        True if visible, false otherwise.
	*/
	bool IsVisibleOnMap(const CPlayer &player) const
	{
		return IsAliveOnMap() && !IsInvisible(player) && IsVisible(player);
	}

	/// Returns true if unit is visible on minimap. Only for ThisPlayer.
	bool IsVisibleOnMinimap() const;

	// Returns true if unit is visible under radar (By player, or by shared vision)
	bool IsVisibleOnRadar(const CPlayer &pradar) const;

	/// Returns true if unit is visible in a viewport. Only for ThisPlayer.
	bool IsVisibleInViewport(const CViewport &vp) const;

	bool IsEnemy(const CPlayer &player) const;
	bool IsEnemy(const CUnit &unit) const;
	bool IsAllied(const CPlayer &player) const;
	bool IsAllied(const CUnit &unit) const;
	bool IsTeamed(const CPlayer &player) const;
	bool IsTeamed(const CUnit &unit) const;

	bool IsUnusable(bool ignore_built_state = false) const;

	/**
	 **  Returns the map distance between this unit and dst units.
	 **
	 **  @param dst  Distance to this unit.
	 **
	 **  @return     The distance between in tiles.
	 */
	int MapDistanceTo(const CUnit &dst) const
	{
		return MapDistanceBetweenTypes(*Type, tilePos, *dst.Type, dst.tilePos);
	}

	int MapDistanceTo(const Vec2i &pos) const;

	/// The distance an attack range is compared with: the larger axis gap to dst
	int AttackDistanceTo(const CUnit &dst) const
	{
		return AttackDistanceBetweenTypes(*Type, tilePos, *dst.Type, dst.tilePos);
	}

	/// The distance a spell's cast range is compared with: the larger of the x and y tile gaps
	/// to dst (the original's range test 0x4C2140, both tile offsets <= range)
	int SpellDistanceTo(const CUnit &dst) const
	{
		return AttackDistanceBetweenTypes(*Type, tilePos, *dst.Type, dst.tilePos);
	}

	/// The same for a tile goal
	int SpellDistanceTo(const Vec2i &pos) const
	{
		return ChebyshevEdgeDistance(tilePos, Vec2i(Type->TileWidth, Type->TileHeight), pos, Vec2i(1, 1));
	}

	/**
	**  Test if unit can move.
	**  For the moment only check for move animation.
	**
	**  @return true if unit can move.
	*/
	bool CanMove() const { return Type->CanMove(); }

	int GetDrawLevel() const;

	bool IsAttackRanged(CUnit *goal, const Vec2i &goalPos);

	PixelPos GetMapPixelPosTopLeft() const;
	PixelPos GetMapPixelPosCenter() const;
	/// Draw-only position, smoothly interpolated between the previous and current game
	/// cycle by the fraction of a cycle elapsed. Removes the discrete 6px/cycle stepping
	/// (and idle-frame freeze) that reads as jagged movement on HD tiles. Logic must keep
	/// using GetMapPixelPosTopLeft(); only the render path uses this.
	PixelPos GetInterpolatedMapPixelPosTopLeft() const;

public:
	class CUnitManagerData
	{
		friend class CUnitManager;
	public:
		CUnitManagerData() = default;

		int GetUnitId() const { return slot; }
	private:
		int slot = -1;     /// index in UnitManager::unitSlots
		int unitSlot = -1; /// index in UnitManager::units
	};
public:
	// @note int is faster than shorts
	unsigned int     Refs = 0;         /// Reference counter
	unsigned int     ReleaseCycle = 0; /// When this unit could be recycled
	CUnitManagerData UnitManagerData;
	size_t PlayerSlot = 0;  /// index in Player->Units

	std::vector<CUnit *> InsideUnits; /// Units inside.
	CUnit *Container = nullptr;     /// Pointer to the unit containing it (or 0)
	int    BoardCount = 0;    /// Number of units transported inside.

	struct {
		std::vector<CUnit *> AssignedWorkers; /// assigned workers to this resource.
		int Active = 0; /// how many units are harvesting from the resource.
	} Resource; /// Resource still

	Vec2i tilePos{-1, -1}; /// Map position

	unsigned int Offset = -1;/// Map position as flat index offset (x + y * w)

	const CUnitType *Type = nullptr; /// Pointer to unit-type (peon,...)
	CPlayer    *Player = nullptr; /// Owner of this unit
	CUnitStats *Stats = nullptr;  /// Current unit stats
	int CurrentSightRange = 0;    /// Unit's Current Sight Range

	// Pathfinding stuff:
	std::unique_ptr<PathFinderData> pathFinderData;

	// DISPLAY:
	int Frame = 0;             /// Image frame: <0 is mirrored
	int Colors = -1;       /// custom colors
	bool IndividualUpgrades[UpgradeMax]{}; /// individual upgrades which the unit has

	// short: a two-tile step starts 2 * PixelTileSize away, which a signed char cannot hold at 64 px per tile
	short IX = 0;               /// X image displacement to map position
	short IY = 0;               /// Y image displacement to map position
	int LastPixelX = 0;         /// absolute on-map pixel pos at the previous game cycle
	int LastPixelY = 0;         /// (render-side movement interpolation; not gameplay state)
	int LastFrame = 0;          /// unit.Frame at the previous game cycle (render-side sprite tween)
	IdleBobState IdleBob;       /// a flyer's idle bob (idle_bob.h; drawing only, not saved)
	unsigned char Direction = 0; //: 8; /// angle (0-255) unit looking
	unsigned char CurrentResource = 0;
	int ResourcesHeld = 0;      /// Resources Held by a unit

	unsigned char DamagedType = 0; /// Index of damage type of unit which damaged this unit
	unsigned long Attacked = 0;    /// gamecycle unit was last attacked
	unsigned long Summoned = 0;    /// GameCycle unit was summoned using spells
	unsigned Blink : 3;          /// Let selection rectangle blink
	unsigned Moving : 2;         /// The unit is moving
	unsigned ReCast : 1;         /// Recast again next cycle
	unsigned AutoRepair : 1;     /// True if unit tries to repair on still action.

	unsigned Burning : 1;        /// unit is burning
	unsigned Destroyed : 1;      /// unit is destroyed pending reference
	unsigned Released : 1;       /// unit is released
	unsigned Removed : 1;        /// unit is removed (not on map)
	unsigned Selected : 1;       /// unit is selected

	unsigned Constructed : 1;    /// Unit is in construction
	unsigned Active : 1;         /// Unit is active for AI
	unsigned Boarded : 1;        /// Unit is on board a transporter.
	unsigned CacheLock : 1;      /// Unit is on lock by unitcache operations.

	unsigned Waiting : 1;        /// Unit is waiting and playing its still animation
	unsigned MineLow : 1;        /// This mine got a notification about its resources being low

	unsigned ZDisplaced : 1;     /// The IY displacement of this unit is simulating a "height" displacement (for flyers). This is useful to draw shadows appropriately

	unsigned JustMoved : 3;      /// The unit last moved of its own accord this amount of cycles of standing still ago

	unsigned TeamSelected = 0;  /// unit is selected by a team member.
	CPlayer *RescuedFrom = nullptr;        /// The original owner of a rescued unit.
	/// The original's per-unit "can be taken over" mark, where a mission start action changed it:
	/// 0 as the owner's type says (a unit of a rescue player can be taken over), 1 never, 2 always.
	unsigned char TakeOver = 0;
	/// The original's "fixed by the map" mark: the map placed the unit with a non-zero data word.
	unsigned char MapFixed = 0;
	/// The original's home tile of a fighter fixed by the map: the tile the map put it on.
	/// x < 0: the unit has no home and is never sent back to one.
	Vec2i HomePos{-1, -1};
	/// What holds the unit to HomePos (home_leash.h, HomeRole): 0 nothing, 1 a guard on its map
	/// tile, 2 held to the place it was sent to, 4 a wanderer.
	unsigned char HomeRole = 0;
	/// The original's remembered attacker (0x495C90 [victim+0x54]): set by a hit when the unit
	/// remembers no living attacker; while it lives, further hits call no friends. Ref-counted.
	CUnitPtr LastAttacker;
	/// GameCycle of the hit that stored LastAttacker. A building that does not attack forgets
	/// the attacker one cycle after the hit (run 72: cleared 1-3 frames after every farm hit). Not
	/// saved: a loaded mark counts as old.
	unsigned long LastAttackerCycle = 0;
	/// nullptr if the unit was not rescued.
	/* Seen stuff. */
	int VisCount[PlayerMax]{}; /// Unit visibility counts
	struct _seen_stuff_ {
		_seen_stuff_() = default;
		std::size_t CFrame = -1;            /// Seen construction frame
		int         Frame = 0;              /// last seen frame/stage of buildings
		const CUnitType *Type = nullptr;    /// Pointer to last seen unit-type
		Vec2i tilePos{-1, -1};              /// Last unit->tilePos Seen
		short IX = 0;                       /// Seen X image displacement to map position
		short IY = 0;                       /// seen Y image displacement to map position
		unsigned    Constructed : 1;        /// Unit seen construction
		unsigned    State : 3;              /// Unit seen build/upgrade state
		unsigned    Destroyed : PlayerMax;  /// Unit seen destroyed or not
		unsigned    ByPlayer : PlayerMax;   /// Track unit seen by player
	} Seen;

	std::vector<CVariable> Variable; /// array of User Defined variables.

	unsigned long TTL = 0;  /// time to live

	unsigned int GroupId = 0;       /// unit belongs to this group id
	unsigned int LastGroup = 0;     /// unit belongs to this last group

	unsigned int Wait = 0;          /// action counter
	int Threshold = 0;              /// The counter while ai unit couldn't change target.
	int UnderAttack = 0;            /// The counter while small ai can ignore non aggressive targets if searching attacker.

	struct _unit_anim_ {
		const std::vector<std::unique_ptr<CAnimation>>* CurrAnim = nullptr;  /// CurrAnim
		std::size_t Anim = 0; /// Anim
		short Wait = 0;                  /// Wait time
		signed char Rotate = 0;          /// Rotation target and direction
		bool Unbreakable = false;        /// Unbreakable
	} Anim, WaitBackup;


	std::vector<std::unique_ptr<COrder>> Orders; /// orders to process
	std::unique_ptr<COrder> SavedOrder;    /// order to continue after current
	std::unique_ptr<COrder> NewOrder;      /// order for new trained units
	std::unique_ptr<COrder> CriticalOrder; /// order to do as possible in breakable animation.

	std::vector<bool> AutoCastSpell;      /// spells to auto cast
	std::vector<int> SpellCoolDownTimers; /// how much time unit need to wait before spell will be ready

	CUnit *Goal = nullptr; /// Generic/Teleporter goal pointer
};

/**
**  Returns unit number (unique to this unit)
*/
#define UnitNumber(unit) ((unit).UnitManagerData.GetUnitId())

// Classic view (video.h, ui.h): Preference.ClassicView reads and sets it.
extern bool IsClassicView();
extern bool RequestClassicView(bool on);

/**
**  User preference.
*/
class CPreference
{
public:
	CPreference() = default;

	bool ShowSightRange = false;       /// Show sight range.
	bool ShowReactionRange = false;    /// Show reaction range.
	bool ShowAttackRange = false;      /// Show attack range.
	bool ShowMessages = true;          /// Show messages.
	bool ShowNoSelectionStats = true;  /// Show stats when no selection is active
	bool BigScreen = false;            /// If true, shows the big screen(without panels)
	bool PauseOnLeave = true;          /// If true, game pauses when cursor is gone
	bool GrayscaleIcons = false;       /// Use grayscaled icons for unavailable units, upgrades, etc
	bool IconsShift = false;           /// Shift icons slightly when you press on them
	bool StereoSound = true;           /// Enables/disables stereo sound effects
	bool MineNotifications = false;    /// Show mine is running low/depleted messages
	bool DeselectInMine = false;       /// Deselect peasants in mines
	bool NoStatusLineTooltips = false; /// Don't show messages on status line
	bool HardwareCursor = false;       /// If true, uses the hardware to draw the cursor. Shaders do no longer apply to the cursor, but this way it's decoupled from the game refresh rate
	bool SelectionRectangleIndicatesDamage = false; /// If true, the selection rectangle interpolates color to indicate damage
	bool FormationMovement = true; /// If true, player controlled units stay in formation
	bool SmoothUnitAnimation = true; /// Cross-fade walk-animation poses between logic cycles (render-only)
	bool HoldFrameUnderWindow = true; /// PERF-A3a: paused single player with a window up: the game under it is not drawn again
	bool AnimatedWater = true; /// HD water layer on (owner decision 87, Options -> Preferences "Animated Water"); drawing only
	bool UnitSpeech = true;    /// Options -> Sound "Unit Speech": units' selection / ready / job-done voices (UnitVoiceEnabled)
	bool UnitAck = true;       /// Options -> Sound "Unit Acknowledgements": units' answers to orders (UnitVoiceEnabled)
	bool BuildingSound = true; /// Options -> Sound "Building Sound": buildings' selection / construction / ready sounds (UnitVoiceEnabled)

	int FrameSkip = 0;          /// Mask used to skip rendering frames (useful for slow renderers that keep up with the game logic, but not the rendering to screen like e.g. original Raspberry Pi)

	int ShowOrders = 0;         /// How many second show orders of unit on map.
	int ShowNameDelay = 0;      /// How many cycles need to wait until unit's name popup will appear.
	int ShowNameTime = 0;       /// How many cycles need to show unit's name popup.
	int HealthBarMode = 0;      /// Health-bar visibility: 0 = always, 1 = never, 2 = only when not full.
	int HealthBarLocation = 0;  /// Health-bar placement: 0 = above the unit, 1 = below the unit's box.
	int AutosaveMinutes = 5;    /// Autosave the game every X minutes; autosave is disabled if the value is 0
	std::shared_ptr<CGraphic> IconFrameG;
	std::shared_ptr<CGraphic> PressedIconFrameG;

	// these are "preferences" in the sense that the user wants to set these
	// persistently across launches for single player games. However, they are
	// relevant for network sync and replays, so the actually relevant storage
	// is in GameSettings, and we just have properties for the lua code here and
	// to initialize GameSettings to the user preferred defaults
	void InitializeSettingsFromPreferences(Settings &s) {
		s.AiExplores = AiExplores;
		s.SimplifiedAutoTargeting = SimplifiedAutoTargeting;
		s.AiChecksDependencies = AiChecksDependencies;
		s.AllyDepositsAllowed = AllyDepositsAllowed;
	}
private:
	bool AiExplores = true;
	bool SimplifiedAutoTargeting = false;
	bool AiChecksDependencies = false;
	bool AllyDepositsAllowed = false;

#if USING_TOLUAPP
public:
	bool get_AiExplores() const { return AiExplores; }
	void set_AiExplores(bool v) {
		AiExplores = v;
		GameSettings.AiExplores = v;
	}
	bool get_SimplifiedAutoTargeting() const { return SimplifiedAutoTargeting; }
	void set_SimplifiedAutoTargeting(bool v) {
		SimplifiedAutoTargeting = v;
		GameSettings.SimplifiedAutoTargeting = v;
	}
	bool get_AiChecksDependencies() const { return AiChecksDependencies; }
	void set_AiChecksDependencies(bool v) {
		AiChecksDependencies = v;
		GameSettings.AiChecksDependencies = v;
	}
	bool get_AllyDepositsAllowed() const { return AllyDepositsAllowed; }
	void set_AllyDepositsAllowed(bool v) {
		AllyDepositsAllowed = v;
		GameSettings.AllyDepositsAllowed = v;
	}
	// Classic view: presentation only, never read by the simulation, not in a saved game, not
	// in a network message, not written with the preferences (the original keeps the choice for
	// the session only: ORIG11 section 7, U3). One state, kept by the video module.
	bool get_ClassicView() const { return IsClassicView(); }
	void set_ClassicView(bool v) { RequestClassicView(v); }
#endif
};

extern CPreference Preference;

/*----------------------------------------------------------------------------
-- Variables
----------------------------------------------------------------------------*/

// in unit_draw.c
/// @todo could be moved into the user interface ?
extern unsigned long ShowOrdersCount;   /// Show orders for some time
extern unsigned long ShowNameDelay;     /// Delay to show unit's name
extern unsigned long ShowNameTime;      /// Show unit's name for some time
extern bool EnableTrainingQueue;               /// Config: training queues enabled
extern bool EnableBuildingCapture;             /// Config: building capture enabled
extern bool RevealAttacker;                    /// Config: reveal attacker enabled
extern int ResourcesMultiBuildersMultiplier;   /// Config: spend resources for building with multiple workers
extern const CViewport *CurrentViewport; /// CurrentViewport
extern void DrawUnitSelection(const CViewport &vp, const CUnit &unit);
extern void (*DrawSelection)(IntColor, int, int, int, int);

extern unsigned int MaxSelectable;    /// How many units could be selected
extern std::vector<CUnit *> Selected; /// currently selected units

/*----------------------------------------------------------------------------
-- Functions
----------------------------------------------------------------------------*/

/// Mark the field with the FieldFlags.
void MarkUnitFieldFlags(const CUnit &unit);
/// Unmark the field with the FieldFlags.
void UnmarkUnitFieldFlags(const CUnit &unit);
/// Update unit->CurrentSightRange.
void UpdateUnitSightRange(CUnit &unit);
/// Create a new unit
extern CUnit *MakeUnit(const CUnitType &type, CPlayer *player);
/// Create a new unit and place on map
extern CUnit *MakeUnitAndPlace(const Vec2i &pos, const CUnitType &type, CPlayer *player);
/// Find the nearest position at which unit can be placed.
void FindNearestDrop(const CUnitType &type, const Vec2i &goalPos, Vec2i &resPos, int heading);
/// Handle the loss of a unit (food,...)
extern void UnitLost(CUnit &unit);
/// Remove the Orders of a Unit
extern void UnitClearOrders(CUnit &unit);
/// Take a unit being destroyed out of the selections and the cursor
extern void LeaveSelectionOnRelease(CUnit &unit);
/// @todo more docu
extern void UpdateForNewUnit(const CUnit &unit, int upgrade);

extern CUnit *GetFirstContainer(const CUnit &unit);
/// After a saved game is read: mark the sight of a removed unit inside a container (FOW-SAVE)
extern void MapMarkInsideUnitSightAfterLoad(CUnit &unit);

/// Call when an Unit goes under fog.
extern void UnitGoesUnderFog(CUnit &unit, const CPlayer &player);
/// Call when an Unit goes out of fog.
extern void UnitGoesOutOfFog(CUnit &unit, const CPlayer &player);
/// LOAD-CRASH-3473: a unit known under fog with no remembered look gets one after a load
extern void UnitFillMissingSeenAfterLoad(CUnit &unit);

/// Does a recount for VisCount
extern void UnitCountSeen(CUnit &unit);

/// Check for rescue each second
extern void RescueUnits();
/// The part of the rescue condition that needs no map: may a unit with this mark (CUnit::TakeOver)
/// and an owner of this type be taken over at all ...
extern bool UnitCanBeTakenOver(int mark, PlayerTypes owner);
/// ... and may a unit of a player of this type take it over when it stands next to it.
extern bool UnitCanTakeOver(PlayerTypes owner, bool building, bool canAttack);

/// Mark a unit as placed by the map with a non-zero data word; a fighter gets its tile as home
extern void SetUnitMapFixed(CUnit &unit);
/// Is the unit held to its home tile: a homed unit of a computer player that is in no force
extern bool UnitHeldByHome(const CUnit &unit);
/// The original's 50-cycle pass: idle units of computer players that stand too far from home walk home
extern void UnitsHomeLeashPass(const std::vector<CUnit *> &units);
/// The unit's remembered attacker while it lives; a dead one is forgotten here (nullptr then)
extern CUnit *RememberedAttacker(CUnit &unit);
/// A hit's memory gate: false (nothing stored) while a living attacker is remembered, else store
extern bool RememberAttacker(CUnit &victim, CUnit &attacker);
/// The " \"remembered-attacker\", \"Uxxxx\"," part of a saved unit ("" when none is remembered)
extern std::string RememberedAttackerText(const CUnit &unit);

/// Convert direction (dx,dy) to heading (0-255)
extern int DirectionToHeading(const Vec2i &dir);
/// Convert direction (dx,dy) to heading (0-255)
extern int DirectionToHeading(const PixelDiff &dir);

///Correct directions for placed wall.
extern void CorrectWallDirections(CUnit &unit);
/// Correct the surrounding walls.
extern void CorrectWallNeighBours(CUnit &unit);

/// Update frame from heading
extern void UnitUpdateHeading(CUnit &unit);
/// Heading and frame from delta direction
extern void UnitHeadingFromDeltaXY(CUnit &unit, const Vec2i &delta);


/// @todo more docu
extern void DropOutOnSide(CUnit &unit, int heading, const CUnit *container);
/// @todo more docu
extern void DropOutNearest(CUnit &unit, const Vec2i &goalPos, const CUnit *container);

/// How a unit that moves on a grid leaves a building
enum class EGridExit {
	Resource, /// a harvester with its load
	Depot,    /// a harvester that has delivered
	Trained   /// a new unit
};
/// The tile on which a unit that moves on a grid comes out of a 3x3 building, if that tile is free
extern std::optional<Vec2i> FindGridExitTile(const CUnit &unit, const CUnit &container, EGridExit kind);

/// Drop out all units in the unit
extern void DropOutAll(const CUnit &unit);

/// Return the rule used to build this building.
extern CBuildRestrictionOnTop *OnTopDetails(const CUnit &unit, const CUnitType *parent);
/// @todo more docu
extern std::optional<CUnit *>
CanBuildHere(const CUnit *unit, const CUnitType &type, const Vec2i &pos);
/// @todo more docu
extern bool CanBuildOn(const Vec2i &pos, int mask);
/// FIXME: more docu
extern std::optional<CUnit *>
CanBuildUnitType(const CUnit *unit, const CUnitType &type, const Vec2i &pos, int real);
/// Get the suitable animation frame depends of unit's damaged type.
extern int ExtraDeathIndex(std::string_view death);

/// Get unit under cursor
extern CUnit *UnitOnScreen(int x, int y);

/// Let a unit die
extern void LetUnitDie(CUnit &unit, bool suicide = false);
/// Destroy all units inside another unit
extern void DestroyAllInside(CUnit &source);
/// Calculate some value to measure the unit's priority for AI
extern int ThreatCalculate(const CUnit &unit, const CUnit &dest);
extern int TargetPriorityCalculate(const CUnit &attacker, const CUnit &dest);

/// Is target within reaction range of this unit?
extern bool InReactRange(const CUnit &unit, const CUnit &target);
/// Is target within attack range of this unit?
extern bool InAttackRange(const CUnit &unit, const CUnit &target);
/// Is tile within attack range of this unit?
extern bool InAttackRange(const CUnit &unit, const Vec2i &tilePos);
/// Return randomly selected position in direction (to/from) dirUnit from srcPos
extern Vec2i GetRndPosInDirection(const Vec2i &srcPos, const CUnit &dirUnit, const bool dirFrom, const int minRange, const int devRadius, const int rangeDev = 3);
/// Return randomly selected position in direction (to/from) dirPos from srcPos
extern Vec2i GetRndPosInDirection(const Vec2i &srcPos, const Vec2i &dirPos, const bool dirFrom, const int minRange, const int devRadius, const int rangeDev = 3);


/// Hit unit with damage, if destroyed give attacker the points
extern void HitUnit(CUnit *attacker, CUnit &target, int damage, const Missile *missile = nullptr);

/// Calculate the distance from current view point to coordinate
extern int ViewPointDistance(const Vec2i &pos);
/// Calculate the distance from current view point to unit
extern int ViewPointDistanceToUnit(const CUnit &dest);

/// Can this unit-type attack the other (destination)
extern bool CanTarget(const CUnitType &type, const CUnitType &dest);
/// Can transporter transport the other unit
extern bool CanTransport(const CUnit &transporter, const CUnit &unit);

/// Generate a unit reference, a printable unique string for unit
extern std::string UnitReference(const CUnit &unit);

/// save unit-structure
extern void SaveUnit(const CUnit &unit, CFile &file);
/// The unit's small counters as saved-game text: each only when set (format 2)
extern std::string UnitSmallFieldsText(int underAttack, int justMoved, bool zDisplaced, int colors);

/// Initialize unit module
extern void InitUnits();
/// Clean unit module
extern void CleanUnits();

// in unit_draw.c
//--------------------
/// Draw nothing around unit
extern void DrawSelectionNone(IntColor, int, int, int, int);
/// Draw circle around unit
extern void DrawSelectionCircle(IntColor, int, int, int, int);
/// Draw circle filled with alpha around unit
extern void DrawSelectionCircleWithTrans(IntColor, int, int, int, int);
/// Draw rectangle around unit
extern void DrawSelectionRectangle(IntColor, int, int, int, int);
/// Draw rectangle filled with alpha around unit
extern void DrawSelectionRectangleWithTrans(IntColor, int, int, int, int);
/// Draw corners around unit
extern void DrawSelectionCorners(IntColor, int, int, int, int);
/// Draw ellipse around unit
extern void (*DrawSelectionEllipse(float factor))(IntColor, int, int, int, int);

/// Register CCL decorations features
extern void DecorationCclRegister();
/// Load the decorations (health,mana) of units
extern void LoadDecorations();
/// Clean the decorations (health,mana) of units
extern void CleanDecorations();

/// Draw unit's shadow
extern void DrawShadow(const CUnitType &type, int frame, const PixelPos &screenPos, char zDisplacement = 0);
/// Collect all units visible on map in viewport
extern std::vector<CUnit *> FindAndSortUnits(const CViewport &);

/// Show a unit's orders.
extern void ShowOrder(const CUnit &unit);

// in groups.c

/// Save groups
extern void SaveGroups(CFile &file);
/// Cleanup groups
extern void CleanGroups();
/// Get the array of units of a particular group
extern const std::vector<CUnit *> &GetUnitsOfGroup(int num);

/// Remove all units from a group
extern void ClearGroup(int num);
/// Add the array of units to the group
extern void AddToGroup(CUnit **units, unsigned int nunits, int num);
/// Set the contents of a particular group with an array of units
extern void SetGroup(CUnit **units, unsigned int nunits, int num);
/// Remove a unit from a group
extern void RemoveUnitFromGroups(CUnit &unit);
/// Register CCL group features
extern void GroupCclRegister();
extern bool IsGroupTainted(int num);
/// The group of `bits` (bit n = group n) that was given units last by the stamps (higher = later); -1 if none
extern int LastGroupOfBits(unsigned int bits, const unsigned long *stamps, int count);
/// The group the unit was put in last (owner decision 71: its slot is marked); -1 if it is in none
extern int GetLastGroupOfUnit(const CUnit &unit);

// in selection.c

/// Check if unit is the currently only selected
extern bool IsOnlySelected(const CUnit &unit);

///  Save selection to restore after.
extern void SaveSelection();
///  Restore selection.
extern void RestoreSelection();
/// Clear current selection
extern void UnSelectAll();
/// Take destroyed / order-less units out of the selection (draw guard); returns how many
extern int DropUnreadableSelectedUnits();
/// Changed TeamUnit Selection
extern void ChangeTeamSelectedUnits(CPlayer &player, const std::vector<CUnit *> &units);
/// Add a unit to selection
extern bool SelectUnit(CUnit &unit);
/// Select one unit as selection
extern void SelectSingleUnit(CUnit &unit);
/// Remove a unit from selection
extern void UnSelectUnit(CUnit &unit);
/// Add a unit to selected if not already selected, remove it otherwise
extern int ToggleSelectUnit(CUnit &unit);
/// Select units from the same type (if selectable by rectangle)
extern int SelectUnitsByType(CUnit &base);
/// Toggle units from the same type (if selectable by rectangle)
extern int ToggleUnitsByType(CUnit &base);
/// Select the units belonging to a particular group
extern int SelectGroup(int group_number,
                       EGroupSelectionMode mode = EGroupSelectionMode::SelectableByRectangleOnly);
/// Add the units from the same group as the one in parameter
extern int AddGroupFromUnitToSelection(CUnit &unit);
/// Select the units from the same group as the one in parameter
extern int SelectGroupFromUnit(CUnit &unit);
/// Select the units in the selection rectangle
extern int SelectUnitsInRectangle(const PixelPos &corner_topleft, const PixelPos &corner_bottomright);
/// Select ground units in the selection rectangle
extern int SelectGroundUnitsInRectangle(const PixelPos &corner_topleft, const PixelPos &corner_bottomright);
/// Select flying units in the selection rectangle
extern int SelectAirUnitsInRectangle(const PixelPos &corner_topleft, const PixelPos &corner_bottomright);
/// Add the units in the selection rectangle to the current selection
extern int AddSelectedUnitsInRectangle(const PixelPos &corner_topleft, const PixelPos &corner_bottomright);
/// Add ground units in the selection rectangle to the current selection
extern int AddSelectedGroundUnitsInRectangle(const PixelPos &corner_topleft, const PixelPos &corner_bottomright);
/// Add flying units in the selection rectangle to the current selection
extern int AddSelectedAirUnitsInRectangle(const PixelPos &corner_topleft, const PixelPos &corner_bottomright);

/// Save current selection state
extern void SaveSelections(CFile &file);
/// Clean up selections
extern void CleanSelections();
/// Register CCL selection features
extern void SelectionCclRegister();

// in ccl_unit.c

/// register CCL units features
extern void UnitCclRegister();

//@}

#endif // !__UNIT_H__
