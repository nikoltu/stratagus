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
/**@name home_leash.h - The home leash and the call for help of standing units. */
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


#ifndef HOME_LEASH_H
#define HOME_LEASH_H

// The original gives a fighter that the map placed with a non-zero data word a home tile (its
// start tile) and looks at every such unit of a computer slot once in 50 game cycles: one that
// stands idle at "computer reaction range / 2 + 4" tiles or more from home, as max(|dx|, |dy|),
// is sent home by a plain move; nearer than that it stays where its fight ended.
// A hit calls the idle fighters of the struck unit's slot to the attacker: those of a computer
// slot from 13 tiles around the struck unit - one fixed by the map only from 4 tiles -, those of
// the other slots from 2 tiles, ships from 4.

/// Game cycles between two passes of the home leash
constexpr int HomeLeashPassCycles = 50;

/// From this distance to its home tile on, an idle unit of a computer slot walks home
constexpr int HomeLeashLimit(int reactRangeComputer)
{
	return reactRangeComputer / 2 + 4;
}

/// The original's tile distance: the larger of the two axis distances
constexpr int HomeLeashDistance(int x, int y, int homeX, int homeY)
{
	const int dx = x > homeX ? x - homeX : homeX - x;
	const int dy = y > homeY ? y - homeY : homeY - y;
	return dx > dy ? dx : dy;
}

/// Is a unit at (x, y) with this home tile to be sent home?
constexpr bool HomeLeashSendsHome(int x, int y, int homeX, int homeY, int reactRangeComputer)
{
	return HomeLeashDistance(x, y, homeX, homeY) >= HomeLeashLimit(reactRangeComputer);
}

/// Tiles around a struck unit from which its slot's idle fighters are called
constexpr int HelpCallRange(bool computerSlot, bool struckIsShip)
{
	return computerSlot ? 13 : (struckIsShip ? 4 : 2);
}

/// Cycles a building that does not attack remembers its attacker after the hit (run 72: 1-3)
constexpr unsigned long BuildingAttackerHoldCycles = 1;

/// Farthest a unit fixed by the map may stand from the struck one to be called
constexpr int HelpCallFixedRange = 4;

/// Is the tile (x, y) inside the call box of a struck unit at (sx, sy) of sw x sh tiles?
constexpr bool HelpCallInBox(int x, int y, int sx, int sy, int sw, int sh, int range)
{
	return x >= sx - range && x <= sx + sw - 1 + range
	    && y >= sy - range && y <= sy + sh - 1 + range;
}

/// Tile distance of (x, y) to a unit at (sx, sy) of sw x sh tiles: to its nearest edge
constexpr int HelpCallDistance(int x, int y, int sx, int sy, int sw, int sh)
{
	const int dx = x < sx ? sx - x : (x > sx + sw - 1 ? x - (sx + sw - 1) : 0);
	const int dy = y < sy ? sy - y : (y > sy + sh - 1 ? y - (sy + sh - 1) : 0);
	return dx > dy ? dx : dy;
}

/// Is an idle fighter at (x, y) called when a unit of its slot at (sx, sy) is struck?
constexpr bool HelpCallReaches(bool computerSlot, bool struckIsShip, bool fixedByMap,
                               int x, int y, int sx, int sy, int sw, int sh)
{
	if (!HelpCallInBox(x, y, sx, sy, sw, sh, HelpCallRange(computerSlot, struckIsShip))) {
		return false;
	}
	return !(computerSlot && fixedByMap)
	    || HelpCallDistance(x, y, sx, sy, sw, sh) <= HelpCallFixedRange;
}

// What holds a unit of a computer slot to its home tile. A guard (fixed by the map) walks home
// when it stands idle too far away. A unit sent on a wave, a guard called by a struck friend and
// a unit of the last stand are held to the place they were sent to: too far from it and not
// attacking they go back to it attacking on the way, idle they go for the nearest enemy, and
// with no enemy left they wander.

/// CUnit::HomeRole
enum HomeRole : unsigned char {
	HomeRoleNone = 0,
	HomeRoleGuard = 1,     ///< home is the tile the map put the unit on
	HomeRolePoint = 2,     ///< home is the place the unit was sent to
	HomeRoleWanderer = 4   ///< no enemy was left: the unit roams
};

/// The role of a unit read from a saved game: a file written before roles were saved has only
/// the home tile, and a unit with a home tile was a guard then.
constexpr int HomeRoleOnLoad(bool hasHome, bool hasRole, int savedRole)
{
	return hasRole ? savedRole : (hasHome ? HomeRoleGuard : HomeRoleNone);
}

enum class Role2 { Nothing, BackToPoint, NearestEnemy, BecomeWanderer };

/// What the 50-cycle pass does with a unit held to a point.
/// far      : HomeLeashDistance(...) >= HomeLeashLimit(reactRangeComputer)
/// attacking: the unit's current order is an attack
/// idle     : the unit stands with no order
constexpr Role2 Role2Action(bool far, bool attacking, bool idle, bool enemyLeft)
{
	return (far && !attacking) ? Role2::BackToPoint
	     : idle ? (enemyLeft ? Role2::NearestEnemy : Role2::BecomeWanderer)
	     : Role2::Nothing;
}

/// One axis of a wanderer's next point: own - mapSize / 4 + (random % (mapSize / 2)), cut to
/// the map.
constexpr int WanderAxis(int own, int mapSize, unsigned random)
{
	const int half = mapSize / 2;
	const int point = own - mapSize / 4 + (half > 0 ? static_cast<int>(random % static_cast<unsigned>(half)) : 0);
	return point < 0 ? 0 : (point > mapSize - 1 ? mapSize - 1 : point);
}

/// A guard breaks off and walks home, although it fights, where the slot that holds the cell it
/// stands in is another one and stands there with more than 2.5 times the guard's own slot.
/// ownStrength / ownerStrength: the two slots' values of the cell, in the column the unit uses
/// (ships: sea, others: land).
constexpr bool StrengthRetreats(int cellOwner, int ownSlot, int ownStrength, int ownerStrength)
{
	return cellOwner >= 0 && cellOwner != ownSlot && 10 * ownStrength < 4 * ownerStrength;
}

/// Does the 50-cycle pass send a guard home? From the limit on, when it stands idle or breaks off
/// (StrengthRetreats); nearer than the limit it stays, break off or not.
constexpr bool HomeLeashGuardWalksHome(bool idle, bool retreats, int x, int y, int homeX, int homeY,
                                       int reactRangeComputer)
{
	return (idle || retreats) && HomeLeashSendsHome(x, y, homeX, homeY, reactRangeComputer);
}

/// One unit of a computer slot one of whose units a submarine has hit.
struct SubmarineAnswerUnit {
	int x = 0, y = 0;
	bool flyerOrSubmarine = false; ///< the kinds that are sent
	bool busyWithAttacker = false; ///< it attacks something already
	bool isStruck = false;         ///< it is the unit that was hit
};

/// The answer to a submarine: the slot's nearest flyer or submarine to the struck unit at
/// (sx, sy) that attacks nothing yet goes for the attacker, from anywhere on the map. Index of
/// the unit to send (the first one of equals), -1 when there is none.
template <typename Units>
constexpr int SubmarineAnswer(const Units &own, int sx, int sy)
{
	int index = -1;
	int best = 0;
	int i = 0;
	for (const SubmarineAnswerUnit &unit : own) {
		if (unit.flyerOrSubmarine && !unit.busyWithAttacker && !unit.isStruck) {
			const int distance = HomeLeashDistance(unit.x, unit.y, sx, sy);
			if (index < 0 || distance < best) {
				index = i;
				best = distance;
			}
		}
		++i;
	}
	return index;
}

#endif
