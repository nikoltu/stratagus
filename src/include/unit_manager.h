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
/**@name unit_manager.h - Unit manager header. */
//
//      (c) Copyright 2007 by Jimmy Salmon
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

#ifndef __UNITMANAGER_H__
#define __UNITMANAGER_H__

//@{

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include <list>
#include <memory>
#include <vector>


/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/

class CUnit;
class CFile;
struct lua_State;

class CUnitManager
{
public:
	CUnitManager();
	~CUnitManager();
	void Init();

	CUnit *AllocUnit();
	void ReleaseUnit(CUnit &unit);
	void Save(CFile &file) const;
	void Load(lua_State *Lua);

	// Following is for already allocated Unit (no specific order)
	void Add(CUnit *unit);
	const std::vector<CUnit *> &GetUnits() const { return units; }

	bool empty() const;

	CUnit *lastCreatedUnit();

	// Following is mainly for scripting
	/// The unit in slot `index`. The index must be a slot that exists (asserted); engine code
	/// that holds a known valid slot number uses this one.
	CUnit &GetSlotUnit(int index) const;
	/// The unit in slot `index`, or nullptr when no such slot exists. For numbers that come
	/// from scripts, save files or other outside input. The unit may be a released one.
	CUnit *FindSlotUnit(int index) const;
	bool IsSlotIndex(int index) const { return index >= 0 && static_cast<size_t>(index) < unitSlots.size(); }
	unsigned int GetUsedSlotCount() const;

private:
	std::vector<CUnit *> units;
	std::vector<std::unique_ptr<CUnit>> unitSlots;
	std::list<CUnit *> releasedUnits;
	CUnit *lastCreated = nullptr;
};


/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

extern CUnitManager *UnitManager;   /// Unit manager

//@}

#endif // !__UNITMANAGER_H__
