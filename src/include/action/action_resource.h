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
/**@name actionresource.h - The actions headerfile. */
//
//      (c) Copyright 1998-2012 by Lutz Sammer and Jimmy Salmon
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

#ifndef __ACTION_RESOURCE_H__
#define __ACTION_RESOURCE_H__

#include "actions.h"

//@{

struct ResourceOrderTestAccess;

class COrder_Resource : public COrder
{
	friend std::unique_ptr<COrder> COrder::NewActionResource(CUnit &harvester, const Vec2i &pos);
	friend std::unique_ptr<COrder> COrder::NewActionResource(CUnit &harvester, CUnit &mine);
	friend std::unique_ptr<COrder> COrder::NewActionReturnGoods(CUnit &harvester, CUnit *depot);
	friend struct ResourceOrderTestAccess;

public:
	COrder_Resource(CUnit &harvester) : COrder(UnitAction::Resource), worker(&harvester) {}

	~COrder_Resource();

	std::unique_ptr<COrder> Clone() const override { return std::make_unique<COrder_Resource>(*this); }

	bool IsValid() const override;

	void Save(CFile &file, const CUnit &unit) const override;
	bool
	ParseSpecificData(lua_State *l, int &j, std::string_view value, const CUnit &unit) override;

	void Execute(CUnit &unit) override;
	PixelPos Show(const CViewport &vp, const PixelPos &lastScreenPos) const override;
	void UpdatePathFinderData(PathFinderInput &input) override;
	bool OnAiHitUnit(CUnit &unit, CUnit *attacker, int /*damage*/) override;
	const Vec2i GetGoalPos() const override;

	int GetCurrentResource() const { return CurrentResource; }
	Vec2i GetHarvestLocation() const;
	bool IsGatheringStarted() const;
	bool IsGatheringFinished() const;
	bool IsGatheringWaiting() const;
	int GetState() const { return State; }
	/// The order is taken off the worker before it ended by itself: a chop not yet complete is lost
	void Interrupt(CUnit &unit);
private:
	int MoveToResource_Terrain(CUnit &unit);
	int MoveToResource_Unit(CUnit &unit);
	int MoveToResource(CUnit &unit);
	void UnitGotoGoal(CUnit &unit, CUnit *const goal, int state);
	int StartGathering(CUnit &unit);
	void LoseResource(CUnit &unit, CUnit &source);
	void ResourceExhausted(CUnit &unit, CUnit &source);
	int GatherResource(CUnit &unit);
	int StopGathering(CUnit &unit);
	int MoveToDepot(CUnit &unit);
	bool WaitInDepot(CUnit &unit);
	void Wc2AiDecideAgain(CUnit &unit);
	void DropResource(CUnit &unit);
	void ResourceGiveUp(CUnit &unit);
	bool FindAnotherResource(CUnit &unit);
	bool ActionResourceInit(CUnit &unit);
private:
	CUnitPtr worker = nullptr; /// unit that own this order.
	unsigned char CurrentResource = 0;
	struct {
		Vec2i Pos{-1, -1}; /// position for terrain resource.
		CUnitPtr Mine = nullptr;
	} Resource;
	CUnitPtr Depot = nullptr;
	int State = 0;
	int TimeToHarvest = 0;       /// how much time until we harvest some more.
	bool DoneHarvesting = false; /// Harvesting done, wait for action to break.
	int Range = 0;
#if 1
	// duplicate of Resource.Pos ?
	Vec2i goalPos{-1, -1};
#endif
};

/// The carry rule of the harvester picture: a held load is shown, except that a terrain harvester
/// (wood) whose Resource order is in its gathering state - at the tree, swinging - is drawn with
/// empty hands. `state` is the order's state, read only when `resourceOrder`.
bool HarvesterShowsLoad(bool terrainHarvester, int held, bool resourceOrder, int state);

/// The wood an interrupted chop loses: a terrain harvester (wood) whose Resource order is taken off
/// it while it is still swinging at its tree, before the load is complete, keeps nothing of what it
/// has cut (live run 36: it walks away empty-handed and needs a full load when sent back). Returns
/// the amount taken off the worker, 0 when the chop is not interrupted or the load is complete.
int InterruptedChopLoss(bool terrainHarvester, int held, int state, bool doneHarvesting);

/// The amount one delivery credits (the original's delivery 0x4B27E0): a fixed sum per trip, not
/// the load scaled - a worker that holds any load (`held` > 0) credits `income`, the player's
/// per-trip amount of that resource; an empty worker credits nothing.
int DeliveredPerTrip(int held, int income);

/// One trip out of a gold mine or an oil platform (the original's take at the entry, 0x4B2AC6):
/// a worker that goes in takes a full load (`capacity`, 100) whenever any stock is left, and the
/// stock loses that full load - never a part load; a remainder below a full load counts as one
/// more trip. The trip that empties the stock collapses the mine at that entry.
struct MineTrip
{
	int load = 0;   /// what the worker carries out
	int stock = 0;  /// what the mine holds after the trip
	bool collapse = false; /// this trip emptied it
};
MineTrip MineTripTake(int stock, int capacity);

/// The builder of a new building and its load (construction end 0x4D6E2F -> delivery 0x4B27E0):
/// a building that is a gold / lumber depot (`depot`) takes the load on the spot - the full trip
/// (`income`) when it stores the load's kind (`storesKind`), nothing otherwise (gold into a lumber
/// mill is lost) - and the builder holds nothing after; any other building leaves the load alone.
struct BuilderLoad
{
	int credited = 0;     /// what the player is credited
	bool emptied = false; /// the builder's load is taken off
};
BuilderLoad BuilderFinishLoad(int held, bool depot, bool storesKind, int income);

/// The original's per-trip amounts (0x4B27E0, read through the per-player building counts
/// 0x89CB80[type]): gold 120 with a finished Castle / Fortress, else 110 with a Keep / Stronghold,
/// else 100; lumber 125 with a Lumber Mill, else 100; oil 125 with a Refinery, else 100.
/// The port gets the same from the hall types' and mills' ImproveProduction (units.lua); these
/// state the rule for the tests.
int GoldPerTrip(int castles, int keeps);
int LumberPerTrip(int lumberMills);
int OilPerTrip(int refineries);

/// A harvest order on the other resource kind, given to a worker that holds a load (live run 43,
/// item 1: a Peasant with gold sent onto a tree), is refused where the worker arrives: it stands
/// there with its load kept as it was, and its next delivery credits that load's own kind. Returns
/// true when the order is refused: a load is held (`held` > 0) of a kind other than `orderKind`.
bool HarvestOrderRefused(int held, int heldKind, int orderKind);

/// The Harvest button is shown only on a worker that holds no load (live run 43, item 1: the card
/// of a Peasant with gold shows Return Goods and no Harvest).
bool HarvestButtonShown(int held);

//@}

#endif // !__ACTION_RESOURCE_H__
