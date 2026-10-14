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
/**@name test_missile_fire.cpp - The test file for missile_fire.cpp / missile.cpp. */
//
//      (c) Copyright 2024 by Matthias Schwarzott
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

#include <doctest.h>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "stratagus.h"
#include "missile.h"
#include "spell/spell_demolish.h"

namespace {
	MissileType* dummyMissile(uintptr_t m)
	{
		return reinterpret_cast<MissileType*>(0x10000 + m);
	}

	void setFrames(const std::vector<std::pair<int, MissileType*>>& frames)
	{
		const std::size_t size = frames.size();
		BurningBuildingFrames.resize(size);
		for (int i = 0; i < size; ++i) {
			BurningBuildingFrames[i].Percent = frames[i].first;
			BurningBuildingFrames[i].Missile = frames[i].second;
		}
	}

} // anonymous namespace

TEST_CASE("missile_fire")
{
	MissileType *m0 = dummyMissile(0);
	MissileType *m1 = dummyMissile(1);
	MissileType *m2 = dummyMissile(2);
	MissileType *m3 = dummyMissile(3);
	MissileType *m4 = dummyMissile(4);
	MissileType *m5 = dummyMissile(5);
	MissileType *m6 = dummyMissile(6);
	MissileType *m7 = dummyMissile(7);
	MissileType *m8 = dummyMissile(8);
	MissileType *m9 = dummyMissile(9);

	BurningBuildingFrames.clear();

	SUBCASE("size=0") {
		setFrames({});
		CHECK(IsBurningBuildingFramesValid());

		CHECK(MissileBurningBuilding(  0) == nullptr);
	}

	SUBCASE("size=1") {
		setFrames({
			{  0, m0},
		});
		CHECK(IsBurningBuildingFramesValid());

		CHECK(MissileBurningBuilding(  0) == m0);
		CHECK(MissileBurningBuilding(  1) == m0);
		CHECK(MissileBurningBuilding(100) == m0);
	}

	SUBCASE("large steps") {
		setFrames({
			{  0, m0},
			{ 50, m1},
		});
		CHECK(IsBurningBuildingFramesValid());

		SUBCASE("normal") {
			CHECK(MissileBurningBuilding(  0) == m0);
			CHECK(MissileBurningBuilding(  1) == m0);
			CHECK(MissileBurningBuilding( 49) == m0);
			CHECK(MissileBurningBuilding( 50) == m1);
			CHECK(MissileBurningBuilding( 51) == m1);
			CHECK(MissileBurningBuilding(100) == m1);
		}

		SUBCASE("out of bounds") {
			CHECK(MissileBurningBuilding( -1) == nullptr);
			CHECK(MissileBurningBuilding(101) == m1);
		}
	}

	SUBCASE("partially dense")
	{
		setFrames({
			{  0, m0},
			{  1, m1},
			{  2, m2},
			{ 65, m3},
			{ 66, m4},
			{ 67, m5},
			{ 99, m6},
			{100, m7},
		});
		CHECK(IsBurningBuildingFramesValid());

		CHECK(MissileBurningBuilding(  0) == m0);
		CHECK(MissileBurningBuilding(  1) == m1);
		CHECK(MissileBurningBuilding(  2) == m2);
		CHECK(MissileBurningBuilding(  3) == m2);

		CHECK(MissileBurningBuilding( 50) == m2);

		CHECK(MissileBurningBuilding( 64) == m2);
		CHECK(MissileBurningBuilding( 65) == m3);
		CHECK(MissileBurningBuilding( 66) == m4);
		CHECK(MissileBurningBuilding( 67) == m5);
		CHECK(MissileBurningBuilding( 68) == m5);

		CHECK(MissileBurningBuilding( 98) == m5);
		CHECK(MissileBurningBuilding( 99) == m6);
		CHECK(MissileBurningBuilding(100) == m7);
	}

	SUBCASE("repeated values and null")
	{
		setFrames({
			{  0, m8},
			{ 80, nullptr},
			{ 90, m8},
		});
		CHECK(IsBurningBuildingFramesValid());

		CHECK(MissileBurningBuilding( 79) == m8);
		CHECK(MissileBurningBuilding( 80) == nullptr);
		CHECK(MissileBurningBuilding( 89) == nullptr);
		CHECK(MissileBurningBuilding( 90) == m8);
	}

	SUBCASE("first key larger") {
		setFrames({
			{ 10, m5},
		});
		CHECK(IsBurningBuildingFramesValid());

		CHECK(MissileBurningBuilding( 9) == nullptr);
		CHECK(MissileBurningBuilding(10) == m5);
	}

	SUBCASE("duplicate keys")
	{
		setFrames({
			{  0, m0},
			{ 20, m1},
			{ 20, m2},
			{ 60, m3},
		});
		CHECK(IsBurningBuildingFramesValid());

		CHECK(MissileBurningBuilding(19) == m0);
		CHECK(MissileBurningBuilding(20) == m2);
		CHECK(MissileBurningBuilding(21) == m2);
	}

	SUBCASE("wrong key order") {
		setFrames({
			{ 75, m9},
			{ 50, m8},
			{  0, m7},
		});

		CHECK_FALSE(IsBurningBuildingFramesValid());
	}

	SUBCASE("too small key value") {
		setFrames({
			{ -1, m7},
			{100, m8},
		});

		CHECK_FALSE(IsBurningBuildingFramesValid());
	}

	SUBCASE("too large key value") {
		setFrames({
			{  0, m7},
			{101, m8},
		});

		CHECK_FALSE(IsBurningBuildingFramesValid());
	}

	BurningBuildingFrames.clear();
}

namespace {
	// every value HitDamage gives over all draws, with the count of each
	std::map<int, int> hitValues(int basic, int piercing, int armor, bool bloodlust = false)
	{
		std::map<int, int> values;
		unsigned int seed = 0x12345678;
		for (int i = 0; i < 60000; ++i) {
			seed = seed * 1103515245u + 12345u;
			++values[HitDamage(basic, piercing, armor, bloodlust, (seed >> 16) & 0x7FFF)];
		}
		return values;
	}

	std::set<int> keys(const std::map<int, int> &values)
	{
		std::set<int> res;
		for (const auto &[value, count] : values) {
			res.insert(value);
		}
		return res;
	}

	// no value is drawn clearly more or less often than the others
	bool even(const std::map<int, int> &values)
	{
		const int expected = 60000 / static_cast<int>(values.size());
		for (const auto &[value, count] : values) {
			if (count < expected * 9 / 10 || count > expected * 11 / 10) {
				return false;
			}
		}
		return true;
	}
} // anonymous namespace

TEST_CASE("hit damage")
{
	SUBCASE("peasant on a building: basic 3 all taken by armor 20, piercing 2") {
		const auto values = hitValues(3, 2, 20);
		CHECK(keys(values) == std::set<int>{1, 2});
		CHECK(even(values));
	}

	SUBCASE("footman on a building: basic 6 all taken by armor 20, piercing 3") {
		const auto values = hitValues(6, 3, 20);
		CHECK(keys(values) == std::set<int>{2, 3, 4});
		CHECK(even(values));
	}

	SUBCASE("peasant on a footman: basic 3 less armor 2, piercing 2") {
		const auto values = hitValues(3, 2, 2);
		CHECK(keys(values) == std::set<int>{2, 3, 4});
		CHECK(even(values));
	}

	SUBCASE("the armor never makes the basic part negative") {
		CHECK(keys(hitValues(3, 2, 20)) == keys(hitValues(3, 2, 3)));
		CHECK(keys(hitValues(3, 2, 200)) == keys(hitValues(0, 2, 0)));
	}

	SUBCASE("the certain half is rounded up, the draw adds 0 .. that half") {
		CHECK(keys(hitValues(0, 1, 0)) == std::set<int>{1, 2});
		CHECK(keys(hitValues(3, 2, 0)) == std::set<int>{3, 4, 5, 6});
		CHECK(keys(hitValues(6, 3, 2)) == std::set<int>{4, 5, 6, 7, 8});
		CHECK(keys(hitValues(6, 3, 0)) == std::set<int>{5, 6, 7, 8, 9, 10});
		CHECK(keys(hitValues(4, 0, 0)) == std::set<int>{2, 3, 4});
	}

	SUBCASE("each draw gives its own value") {
		CHECK(HitDamage(6, 3, 20, false, 0) == 2);
		CHECK(HitDamage(6, 3, 20, false, 1) == 3);
		CHECK(HitDamage(6, 3, 20, false, 2) == 4);
		CHECK(HitDamage(6, 3, 20, false, 3) == 2);
		CHECK(HitDamage(80, 0, 0, false, 40) == 80);
		CHECK(HitDamage(80, 0, 0, false, 41) == 40);
	}

	SUBCASE("nothing to deal is 0, anything to deal is at least 1") {
		CHECK(keys(hitValues(0, 0, 0)) == std::set<int>{0});
		CHECK(keys(hitValues(80, 0, 80)) == std::set<int>{0});
		CHECK(keys(hitValues(80, 0, 200)) == std::set<int>{0});
		CHECK(hitValues(81, 0, 80).begin()->first == 1);
		CHECK(hitValues(0, 1, 255).begin()->first == 1);
	}

	SUBCASE("bloodlust doubles the basic and the piercing damage before the armor") {
		CHECK(keys(hitValues(6, 3, 2, true)) == keys(hitValues(12, 6, 2)));
		CHECK(keys(hitValues(8, 4, 20, true)) == keys(hitValues(0, 8, 0)));
	}

	SUBCASE("one hit is never above 255") {
		const auto values = hitValues(250, 250, 0);
		CHECK(values.begin()->first == 250);
		CHECK(values.rbegin()->first == 255);
	}
}

namespace {
	// Zone of a unit whose centre is the given number of classic pixels (32 per tile) from the impact.
	int zoneAt(int dx, int dy, int tile = 32)
	{
		return SplashZone(PixelPos(1000, 1000), PixelPos(1000 + dx * tile / 32, 1000 + dy * tile / 32),
						  PixelSize(tile, tile));
	}

	std::map<int, int> splashValues(int raw, int zone, int armor)
	{
		std::map<int, int> values;
		for (unsigned int draw = 0; draw != 60000; ++draw) {
			++values[SplashDamage(raw, zone, armor, draw)];
		}
		return values;
	}

	std::set<int> span(int from, int to)
	{
		std::set<int> res;
		for (int i = from; i <= to; ++i) {
			res.insert(i);
		}
		return res;
	}
} // anonymous namespace

TEST_CASE("splash damage")
{
	SUBCASE("zones: whole up to 22 classic pixels, a quarter up to 42, none farther") {
		CHECK(zoneAt(0, 0) == 0);
		CHECK(zoneAt(21, 0) == 0);
		CHECK(zoneAt(22, 0) == 0);
		CHECK(zoneAt(23, 0) == 2);
		CHECK(zoneAt(41, 0) == 2);
		CHECK(zoneAt(42, 0) == 2);
		CHECK(zoneAt(43, 0) == -1);
	}

	SUBCASE("the larger axis offset decides, on either side") {
		CHECK(zoneAt(22, 22) == 0);
		CHECK(zoneAt(-22, 22) == 0);
		CHECK(zoneAt(0, -23) == 2);
		CHECK(zoneAt(22, 23) == 2);
		CHECK(zoneAt(42, -42) == 2);
		CHECK(zoneAt(0, 43) == -1);
		CHECK(zoneAt(-43, 0) == -1);
		// the next tile straight or diagonal is in the outer zone, the second one is out
		CHECK(zoneAt(32, 0) == 2);
		CHECK(zoneAt(32, 32) == 2);
		CHECK(zoneAt(64, 0) == -1);
	}

	SUBCASE("the zones are fractions of the tile, whatever its size in map pixels") {
		CHECK(zoneAt(22, 0, 64) == 0);
		CHECK(SplashZone(PixelPos(0, 0), PixelPos(44, 0), PixelSize(64, 64)) == 0);
		CHECK(SplashZone(PixelPos(0, 0), PixelPos(45, 0), PixelSize(64, 64)) == 2);
		CHECK(SplashZone(PixelPos(0, 0), PixelPos(84, 0), PixelSize(64, 64)) == 2);
		CHECK(SplashZone(PixelPos(0, 0), PixelPos(85, 0), PixelSize(64, 64)) == -1);
	}

	SUBCASE("the missile carries basic + piercing, doubled by bloodlust, at most 255") {
		CHECK(SplashRawDamage(80, 0, false) == 80);
		CHECK(SplashRawDamage(80 + 15, 0, false) == 95);
		CHECK(SplashRawDamage(50, 3, true) == 106);
		CHECK(SplashRawDamage(130, 0, true) == 255);
	}

	SUBCASE("catapult (80 + 0), inner zone") {
		const auto bare = splashValues(80, 0, 0);
		CHECK(keys(bare) == span(40, 80));
		CHECK(even(bare));
		CHECK(keys(splashValues(80, 0, 4)) == span(38, 76));
	}

	SUBCASE("catapult (80 + 0), outer zone: a quarter first, then the armor") {
		const auto bare = splashValues(80, 2, 0);
		CHECK(keys(bare) == span(10, 20));
		CHECK(even(bare));
		const auto armored = splashValues(80, 2, 4);
		CHECK(keys(armored) == span(8, 16));
		CHECK(even(armored));
		// armor 20 takes the whole quarter; in the inner zone 60 are left
		CHECK(keys(splashValues(80, 2, 20)) == std::set<int>{0});
		CHECK(SplashLeft(80, 2, 20) == 0);
		CHECK(keys(splashValues(80, 0, 20)) == span(30, 60));
	}

	SUBCASE("the armor is taken off the piercing part as well") {
		CHECK(SplashLeft(SplashRawDamage(10, 10, false), 0, 15) == 5);
		CHECK(keys(splashValues(20, 0, 15)) == std::set<int>{3, 4, 5, 6});
	}

	SUBCASE("each draw gives its own value") {
		CHECK(SplashDamage(80, 2, 4, 0) == 8);
		CHECK(SplashDamage(80, 2, 4, 8) == 16);
		CHECK(SplashDamage(80, 2, 4, 9) == 8);
		CHECK(SplashDamage(80, 0, 0, 40) == 80);
	}

	SUBCASE("the shooter is left out; the owner of a unit is not asked") {
		CHECK_FALSE(SplashTakesUnit(true, false, false));
		CHECK_FALSE(SplashTakesUnit(true, false, true));
		// any other unit on the ground, the shooter's own included
		CHECK(SplashTakesUnit(false, false, false));
		CHECK(SplashTakesUnit(false, false, true));
	}

	SUBCASE("flying units only when the missile was fired at a flying unit") {
		CHECK_FALSE(SplashTakesUnit(false, true, false));
		CHECK(SplashTakesUnit(false, true, true));
	}
}

TEST_CASE("area damage of a finished number")
{
	// A missile that carries a finished number meets no armor: the zone, then half + draw.
	SUBCASE("blizzard and death and decay, 10: 5 .. 10 on the tile, 1 .. 2 beside it") {
		CHECK(keys(splashValues(10, 0, 0)) == span(5, 10));
		CHECK(keys(splashValues(10, 2, 0)) == span(1, 2));
	}

	SUBCASE("whirlwind and flame shield, 4: 2 .. 4 and 1 .. 2") {
		CHECK(keys(splashValues(4, 0, 0)) == span(2, 4));
		CHECK(keys(splashValues(4, 2, 0)) == span(1, 2));
	}

	SUBCASE("fireball, 40: 20 .. 40 and 5 .. 10") {
		CHECK(keys(splashValues(40, 0, 0)) == span(20, 40));
		CHECK(keys(splashValues(40, 2, 0)) == span(5, 10));
	}

	SUBCASE("the hit of a flyer, 8 .. 16 against armor 0, is carried on") {
		CHECK(keys(hitValues(0, 16, 0)) == span(8, 16));
		// armor does not take piercing damage: the same against armor 20
		CHECK(keys(hitValues(0, 16, 20)) == span(8, 16));
		CHECK(keys(splashValues(8, 0, 0)) == span(4, 8));
		CHECK(keys(splashValues(16, 0, 0)) == span(8, 16));
		CHECK(keys(splashValues(16, 2, 0)) == span(2, 4));
		CHECK(keys(splashValues(8, 2, 0)) == span(1, 2));
	}

	SUBCASE("flying units are in the area, the shooter is not") {
		CHECK(SplashTakesUnit(false, true, true));
		CHECK(SplashTakesUnit(false, false, true));
		CHECK_FALSE(SplashTakesUnit(true, true, true));
	}
}

TEST_CASE("wall steps")
{
	// a wall stands 20 steps; a hit takes a quarter of its damage, at least one
	CHECK(WallSteps(0) == 1);
	CHECK(WallSteps(1) == 1);
	CHECK(WallSteps(3) == 1);
	CHECK(WallSteps(4) == 1);
	CHECK(WallSteps(7) == 1);
	CHECK(WallSteps(8) == 2);
	CHECK(WallSteps(9) == 2);
	CHECK(WallSteps(10) == 2);
	CHECK(WallSteps(40) == 10);
	CHECK(WallSteps(79) == 19);
	CHECK(WallSteps(80) == 20);
	CHECK(WallSteps(255) == 63);
}

TEST_CASE("death coil shares")
{
	using Shares = std::vector<int>;

	SUBCASE("nothing in the area") {
		CHECK(DeathCoilShares({}, 50).empty());
	}

	SUBCASE("one unit gives its hit points, at most the budget") {
		CHECK(DeathCoilShares({30}, 50) == Shares{30});
		CHECK(DeathCoilShares({50}, 50) == Shares{50});
		CHECK(DeathCoilShares({60}, 50) == Shares{50});
	}

	SUBCASE("the weakest first, whole, the next takes the rest") {
		CHECK(DeathCoilShares({60, 30}, 50) == Shares{20, 30});
		CHECK(DeathCoilShares({40, 30, 5}, 50) == Shares{15, 30, 5});
		CHECK(DeathCoilShares({10, 10, 10, 10, 10, 10}, 50) == Shares{10, 10, 10, 10, 10, 0});
		CHECK(DeathCoilShares({20, 30, 40}, 50) == Shares{20, 30, 0});
	}

	SUBCASE("equal hit points: in the given order") {
		CHECK(DeathCoilShares({30, 30, 30}, 50) == Shares{30, 20, 0});
	}

	SUBCASE("the sum never passes the budget") {
		for (int a = 1; a < 70; a += 7) {
			for (int b = 1; b < 70; b += 5) {
				for (int c = 1; c < 70; c += 3) {
					const Shares shares = DeathCoilShares({a, b, c}, 50);
					CHECK(shares[0] + shares[1] + shares[2] == std::min(a + b + c, 50));
					CHECK(shares[0] <= a);
					CHECK(shares[1] <= b);
					CHECK(shares[2] <= c);
				}
			}
		}
	}
}

TEST_CASE("demolish walls")
{
	// with a damage range of 1 the walls fall on the eight tiles around
	CHECK(DemolishTakesWall(Vec2i(1, 0), 1));
	CHECK(DemolishTakesWall(Vec2i(-1, 1), 1));
	CHECK(DemolishTakesWall(Vec2i(1, -1), 1));
	CHECK_FALSE(DemolishTakesWall(Vec2i(2, 0), 1));
	CHECK_FALSE(DemolishTakesWall(Vec2i(0, -2), 1));
	// without one every tile of the explosion counts
	CHECK(DemolishTakesWall(Vec2i(2, 0), -1));
	CHECK(DemolishTakesWall(Vec2i(3, 0), -1));
}

TEST_CASE("aimed hit")
{
	// lightning / touch of death: the unit fired at takes the hit while it is alive on the map
	CHECK(AimedHitTakes(true, true, false));
	// dead or off the map (boarded, entered a building): the impact tile as before
	CHECK_FALSE(AimedHitTakes(true, false, false));
	// a type without the tag keeps its Range rule
	CHECK_FALSE(AimedHitTakes(false, true, false));
	CHECK_FALSE(AimedHitTakes(true, true, true));
	CHECK_FALSE(MissileType("x").AimedHit);
}
