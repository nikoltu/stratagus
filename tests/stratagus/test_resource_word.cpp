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
/**@name test_resource_word.cpp - HUD-RESOURCE-WORD: the shown word of a resource. */
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

#include "script.h"
#include "unit.h"
#include "unittype.h"
#include "upgrade_structs.h"

#include <string>
#include <variant>

#include <string_view>

// src/ui/mainscr.cpp
extern std::variant<int, const char *> GetComponent(const CUnit &unit, int index, EnumVariable e, int t);
extern std::string DefaultResourceDisplayNames[MaxCosts];
extern const std::string &ResourceDisplayName(int res);
extern bool SetResourceDisplayName(std::string_view ident, const std::string &text);

namespace
{

/// The Wargus resource identifiers for one test; everything is put back when the test ends.
struct ResourceNamesScope
{
	std::string saved[MaxCosts];
	std::string savedShown[MaxCosts];
	ResourceNamesScope()
	{
		for (int i = 0; i < MaxCosts; ++i) {
			saved[i] = DefaultResourceNames[i];
			savedShown[i] = DefaultResourceDisplayNames[i];
			DefaultResourceNames[i].clear();
			DefaultResourceDisplayNames[i].clear();
		}
		DefaultResourceNames[TimeCost] = "time";
		DefaultResourceNames[GoldCost] = "gold";
		DefaultResourceNames[WoodCost] = "wood";
		DefaultResourceNames[OilCost] = "oil";
	}
	~ResourceNamesScope()
	{
		for (int i = 0; i < MaxCosts; ++i) {
			DefaultResourceNames[i] = saved[i];
			DefaultResourceDisplayNames[i] = savedShown[i];
		}
	}
};

std::string CarryWord(const CUnit &unit)
{
	return std::get<const char *>(GetComponent(unit, CARRYRESOURCE_INDEX, EnumVariable::Name, 0));
}

} // namespace

TEST_CASE("resource word: without a shown word the identifier stays (enUS before the fix)")
{
	ResourceNamesScope scope;
	CHECK(ResourceDisplayName(WoodCost) == "wood");
	CHECK(ResourceDisplayName(GoldCost) == "gold");
	CHECK(ResourceDisplayName(-1).empty());
	CHECK(ResourceDisplayName(MaxCosts).empty());
}

TEST_CASE("resource word: the shown word replaces the identifier on the panel, the identifier stays")
{
	ResourceNamesScope scope;
	// jaJP, the S20+ report: "運搬: 20 wood" -> the game's own word for lumber.
	CHECK(SetResourceDisplayName("wood", "\xe6\x9c\xa8\xe6\x9d\x90"));
	CHECK(SetResourceDisplayName("gold", "Gold"));
	CHECK_FALSE(SetResourceDisplayName("lumber", "Lumber")); // not an identifier
	CHECK_FALSE(SetResourceDisplayName("", "x"));
	CHECK(ResourceDisplayName(WoodCost) == "\xe6\x9c\xa8\xe6\x9d\x90");
	CHECK(DefaultResourceNames[WoodCost] == "wood"); // saves / AI / scripts keep the identifier
	CHECK(ResourceDisplayName(OilCost) == "oil");

	CUnit worker;
	worker.Variable.resize(UnitTypeVar.GetNumberVariable());
	worker.CurrentResource = WoodCost;
	CHECK(CarryWord(worker) == "\xe6\x9c\xa8\xe6\x9d\x90");
	worker.CurrentResource = OilCost;
	CHECK(CarryWord(worker) == "oil");

	// an empty text clears the word again
	CHECK(SetResourceDisplayName("wood", ""));
	worker.CurrentResource = WoodCost;
	CHECK(CarryWord(worker) == "wood");
}
