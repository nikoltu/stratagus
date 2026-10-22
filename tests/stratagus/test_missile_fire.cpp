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
#include <string>
#include <vector>

#include "stratagus.h"
#include "map.h"
#include "missile.h"
#include "missile_cadence.h"
#include "spell/spell_demolish.h"
#include "actions.h"
#include "unit.h"
#include "unittype.h"
#include "viewport.h"

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

TEST_CASE("area damage: Death and Decay and Blizzard on a Lumber Mill as live run 80 A9")
{
	// Live run 80 A9 dd3: two Death and Decay casts on an own Lumber Mill (3 x 3, armour 20); each
	// cast's hits come on its own 24-frame comb. Cast 1 took 3 2 3 3 3 4 2 3 3 4, cast 2 took
	// 1 2 2 1 1 2 1 1 1 1 (hp 349 -> 306). The port's finished 10 meets no armour: a spot whose
	// impact lies one tile off the Mill's centre (the outer zone) takes 1 .. 2, two such spots of
	// one cast 2 .. 4; a spot on the centre tile would take 5 .. 10 (none in the live casts).
	const PixelSize tile(64, 64);
	const PixelPos centre(5 * 64 + 32, 5 * 64 + 32); // the centre of a 3 x 3 building's middle tile
	CHECK(SplashZone(centre, centre, tile) == 0);
	CHECK(SplashZone(centre + PixelDiff(64, 0), centre, tile) == 2);
	CHECK(SplashZone(centre + PixelDiff(64, 64), centre, tile) == 2);
	CHECK(SplashZone(centre + PixelDiff(128, 0), centre, tile) == -1);

	const std::set<int> one = keys(splashValues(10, 2, 0));
	CHECK(one == span(1, 2));
	std::set<int> two;
	for (int a : one) {
		for (int b : one) {
			two.insert(a + b);
		}
	}
	CHECK(two == span(2, 4));
	for (int hit : {3, 2, 3, 3, 3, 4, 2, 3, 3, 4}) {
		CHECK(two.count(hit) == 1);
	}
	for (int hit : {1, 2, 2, 1, 1, 2, 1, 1, 1, 1}) {
		CHECK(one.count(hit) == 1);
	}
	// Blizzard bz1: 1 .. 3 a sampled hit (two spots' 1 .. 2 when they fall in one sample)
	CHECK(keys(splashValues(10, 2, 0)) == span(1, 2));

	// Blizzard bz1: one spot's 11 hits from frame 312 to 574 (10 x 26.2); a cast 269 frames
	CHECK(BlizzardShardPeriod == 26);
	CHECK(10 * BlizzardShardPeriod >= 255);
	CHECK(10 * BlizzardShardPeriod <= 269);
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

namespace {
	// The picture each draw of game cycles 0 .. cycles - 1 shows of a stay missile made in the unit
	// pass of cycle 0 (-1: nothing drawn), as the game cycle runs: missiles, then the draw.
	std::vector<int> stayTimeline(int delay, int sleep, int firstFrameWait, int frames, int cycles)
	{
		MissileType mtype("missile-test-stay");
		mtype.Class = MissileClass::Stay;
		mtype.StartDelay = delay;
		mtype.Sleep = sleep;
		mtype.FirstFrameWait = firstFrameWait;
		mtype.SpriteFrames = frames;
		mtype.NumDirections = 1;

		CleanMissiles();
		MakeLocalMissile(mtype, PixelPos(64, 64), PixelPos(64, 64));
		const CViewport vp;
		std::vector<int> shown;
		for (int cycle = 0; cycle != cycles; ++cycle) {
			MissileActions();
			const std::vector<Missile *> table = FindAndSortMissiles(vp);
			shown.push_back(table.empty() ? -1 : table.front()->SpriteFrame);
		}
		CleanMissiles();
		return shown;
	}

	// the same timeline from the original's numbers: hidden draws, then each picture for its cycles
	std::vector<int> expectedTimeline(int hidden, const std::vector<int> &holds, int cycles)
	{
		std::vector<int> shown(hidden, -1);
		for (int picture = 0; picture != int(holds.size()); ++picture) {
			shown.insert(shown.end(), holds[picture], picture);
		}
		shown.resize(cycles, -1);
		return shown;
	}
} // anonymous namespace

TEST_CASE("stay missile lead-in: Delay and FirstFrameWait")
{
	SUBCASE("cannon flash (type 25): hidden c .. c+10, picture 0 c+11 .. c+14, then 3 each, gone at c+24") {
		const std::vector<int> shown = stayTimeline(12, 3, 1, 4, 30);
		CHECK(shown == expectedTimeline(11, {4, 3, 3, 3}, 30));
		CHECK(shown[23] == 3);
		CHECK(shown[24] == -1);
	}
	SUBCASE("explosion (type 23): hidden c, picture 0 c+1 .. c+4, 15 more x 3, gone at c+50") {
		std::vector<int> holds(16, 3);
		holds[0] = 4;
		const std::vector<int> shown = stayTimeline(2, 3, 1, 16, 56);
		CHECK(shown == expectedTimeline(1, holds, 56));
		CHECK(shown[49] == 15);
		CHECK(shown[50] == -1);
	}
	SUBCASE("impact (type 21): Sleep 2, picture 0 three cycles, gone at c+14") {
		const std::vector<int> shown = stayTimeline(2, 2, 1, 6, 20);
		CHECK(shown == expectedTimeline(1, {3, 2, 2, 2, 2, 2}, 20));
	}
	SUBCASE("engine Delay D hides D - 1 draws; without FirstFrameWait picture 0 lasts Sleep - 1") {
		CHECK(stayTimeline(11, 3, 0, 4, 26) == expectedTimeline(10, {3, 3, 3, 3}, 26));
		CHECK(stayTimeline(0, 3, 0, 4, 16) == expectedTimeline(0, {2, 3, 3, 3}, 16));
	}
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

TEST_CASE("building fire follows the hit points: big under half, small under three quarters")
{
	std::vector<BurningBuildingFrame> frames(3);
	frames[0].Percent = 0;
	frames[0].Missile = dummyMissile(1); // big fire
	frames[1].Percent = 50;
	frames[1].Missile = dummyMissile(2); // small fire
	frames[2].Percent = 75;              // none

	// hp < max / 2 -> 0, hp < 3 * max / 4 -> 1, else 2 (both cut down to whole numbers)
	CHECK(BurningBuildingFrameOf(frames, 199, 400) == 0);
	CHECK(BurningBuildingFrameOf(frames, 200, 400) == 1);
	CHECK(BurningBuildingFrameOf(frames, 299, 400) == 1);
	CHECK(BurningBuildingFrameOf(frames, 300, 400) == 2);
	CHECK(BurningBuildingFrameOf(frames, 400, 400) == 2);
	CHECK(BurningBuildingFrameOf(frames, 1, 400) == 0);
	// odd maxima: guard tower 130 (65 / 97), 1001 (500 / 750)
	CHECK(BurningBuildingFrameOf(frames, 64, 130) == 0);
	CHECK(BurningBuildingFrameOf(frames, 65, 130) == 1);
	CHECK(BurningBuildingFrameOf(frames, 96, 130) == 1);
	CHECK(BurningBuildingFrameOf(frames, 97, 130) == 2); // the percent rule had the small fire here
	CHECK(BurningBuildingFrameOf(frames, 500, 1001) == 1); // the percent rule had the big fire here
	CHECK(BurningBuildingFrameOf(frames, 499, 1001) == 0);
	CHECK(BurningBuildingFrameOf(frames, 750, 1001) == 2);
	CHECK(BurningBuildingFrameOf({}, 1, 400) == -1);
}

TEST_CASE("flame shield word: a built-in variable the cast and the cycle count share")
{
	// spells.lua sets "FlameShield" to 500 on a cast and refuses a cast while it is not 0; the
	// per-cycle count-down (HandleBuffsEachCycle) works on the built-in index.
	CHECK(UnitTypeVar.VariableNameLookup["FlameShield"] == FLAMESHIELD_INDEX);
	CHECK(std::string(UnitTypeVar.VariableNameLookup[FLAMESHIELD_INDEX]) == "FlameShield");
	CHECK(UnitTypeVar.VariableNameLookup["Priority"] == PRIORITY_INDEX);
	CHECK(FLAMESHIELD_INDEX + 1 == NVARALREADYDEFINED);
}

TEST_CASE("runes: laid only on a free map tile")
{
	CHECK(RuneTileTaken(true, false, false));
	CHECK_FALSE(RuneTileTaken(true, true, false));  // a ground unit stands there
	CHECK_FALSE(RuneTileTaken(true, false, true));  // a rune lies there
	CHECK_FALSE(RuneTileTaken(true, true, true));
	CHECK_FALSE(RuneTileTaken(false, false, false)); // off the map
}

namespace {
	// The pictures a catapult rock (15 frames: 3 sizes x 5 directions, NumDirections 9) shows over
	// one flight of `tiles` tiles in the direction (dx, dy), one per missile step, repeats folded.
	std::vector<int> rockFlight(MissileClass cls, int dx, int dy, int tiles)
	{
		Map.Info.MapWidth = 64;
		Map.Info.MapHeight = 64;
		Map.Create();
		MissileType mtype("missile-test-rock");
		mtype.Class = cls;
		mtype.SpriteFrames = 15;
		mtype.NumDirections = 9;
		mtype.Speed = 8;
		mtype.SmokePrecision = 8; // as DefineMissileType sets it (= Speed)
		mtype.Sleep = 1;
		mtype.Range = 0;
		const PixelPos from(32 * 32, 32 * 32);
		const PixelPos to = from + PixelPos(dx * 32 * tiles, dy * 32 * tiles);
		auto missile = Missile::Init(mtype, from, to);
		std::vector<int> shown;
		for (int i = 0; i != 400 && missile->TTL != 0; ++i) {
			missile->Action();
			if (missile->TTL == 0) {
				break;
			}
			if (shown.empty() || shown.back() != missile->SpriteFrame) {
				shown.push_back(missile->SpriteFrame);
			}
		}
		Map.Fields.clear();
		Map.Info.MapWidth = 0;
		Map.Info.MapHeight = 0;
		return shown;
	}
} // anonymous namespace

TEST_CASE("catapult rock (ORIG51 s.6): direction d held, sizes d, d+5, d+10, d+5, d over the flight")
{
	// the 6 live flights: E (A, D) 2, 7, 12, 7, 2; SE (B, C, E, F) 3, 8, 13, 8, 3
	CHECK(rockFlight(MissileClass::Parabolic, 1, 0, 6) == std::vector<int>{2, 7, 12, 7, 2});
	CHECK(rockFlight(MissileClass::Parabolic, 1, 1, 6) == std::vector<int>{3, 8, 13, 8, 3});
	// not seen live (N, NE, S; the west half drawn mirrored): the same rule
	CHECK(rockFlight(MissileClass::Parabolic, 0, -1, 6) == std::vector<int>{0, 5, 10, 5, 0});
	CHECK(rockFlight(MissileClass::Parabolic, 1, -1, 7) == std::vector<int>{1, 6, 11, 6, 1});
	// straight south: frame 4 of its size, drawn mirrored (with NumDirections 9 the south heading falls
	// on the mirrored side of the sheet; the same picture turned over)
	CHECK(rockFlight(MissileClass::Parabolic, 0, 1, 8) == std::vector<int>{-5, -10, -15, -10, -5});
	// mirrored: frame f drawn flipped is -f - 1
	CHECK(rockFlight(MissileClass::Parabolic, -1, 0, 6) == std::vector<int>{-3, -8, -13, -8, -3});
	// without the enhanced effects the rock flies the straight line of the cycle-once class: the same pictures
	CHECK(rockFlight(MissileClass::PointToPointCycleOnce, 1, 0, 6) == std::vector<int>{2, 7, 12, 7, 2});
	CHECK(rockFlight(MissileClass::PointToPointCycleOnce, 1, 1, 6) == std::vector<int>{3, 8, 13, 8, 3});
	CHECK(rockFlight(MissileClass::PointToPointCycleOnce, 0, 1, 8) == std::vector<int>{-5, -10, -15, -10, -5});
	// each size about a fifth of the flight (live: ~2 grabs of 3.2 frames each, flights of 26 - 33 frames)
	std::vector<int> steps;
	{
		Map.Info.MapWidth = 64;
		Map.Info.MapHeight = 64;
		Map.Create();
		MissileType mtype("missile-test-rock");
		mtype.Class = MissileClass::Parabolic;
		mtype.SpriteFrames = 15;
		mtype.NumDirections = 9;
		mtype.Speed = 8;
		mtype.SmokePrecision = 8;
		mtype.Sleep = 1;
		auto missile = Missile::Init(mtype, PixelPos(1024, 1024), PixelPos(1024 + 7 * 32, 1024));
		int last = 99;
		for (int i = 0; i != 400 && missile->TTL != 0; ++i) {
			missile->Action();
			if (missile->TTL == 0) {
				break;
			}
			if (missile->SpriteFrame != last) {
				steps.push_back(0);
				last = missile->SpriteFrame;
			}
			++steps.back();
		}
		Map.Fields.clear();
		Map.Info.MapWidth = 0;
		Map.Info.MapHeight = 0;
	}
	REQUIRE(steps.size() == 5);
	for (int n : steps) {
		CHECK(n >= 4);
		CHECK(n <= 7); // 7 tiles at 8 px a cycle: 28 cycles, 5 or 6 each
	}
}

// Live run 54 item 10 / 14 (ORIG54.md): small fire below 75 %, big fire below 50 % - Farm 400 (none
// at 312, small at 299; big at 193), Town Hall 1200 (small at 899, big at 598), Shipyard 1100 (826
// none, 824 small; 551 small, 543 big). 3 buildings.
TEST_CASE("building fire, run 54: the thresholds of the Farm, the Town Hall and the Shipyard")
{
	std::vector<BurningBuildingFrame> frames(3);
	frames[0].Percent = 0;
	frames[0].Missile = dummyMissile(1); // big fire
	frames[1].Percent = 50;
	frames[1].Missile = dummyMissile(2); // small fire
	frames[2].Percent = 75;              // none

	CHECK(BurningBuildingFrameOf(frames, 312, 400) == 2);
	CHECK(BurningBuildingFrameOf(frames, 300, 400) == 2);
	CHECK(BurningBuildingFrameOf(frames, 299, 400) == 1);
	CHECK(BurningBuildingFrameOf(frames, 193, 400) == 0);
	CHECK(BurningBuildingFrameOf(frames, 900, 1200) == 2);
	CHECK(BurningBuildingFrameOf(frames, 899, 1200) == 1);
	CHECK(BurningBuildingFrameOf(frames, 598, 1200) == 0);
	CHECK(BurningBuildingFrameOf(frames, 826, 1100) == 2);
	CHECK(BurningBuildingFrameOf(frames, 825, 1100) == 2);
	CHECK(BurningBuildingFrameOf(frames, 824, 1100) == 1);
	CHECK(BurningBuildingFrameOf(frames, 551, 1100) == 1);
	CHECK(BurningBuildingFrameOf(frames, 550, 1100) == 1);
	CHECK(BurningBuildingFrameOf(frames, 549, 1100) == 0);
	CHECK(BurningBuildingFrameOf(frames, 543, 1100) == 0);
}

// Live run 54 item 10: the fire picture follows the HP change within one 250 ms grab (3 of 3). At
// 38.5 cycles a second that is 9 cycles; the fire's step is 2 cycles (Sleep), its loop 12 / 20.
TEST_CASE("building fire, run 54: the picture follows the hit points at the next step, not the loop end")
{
	MissileType small("test-small-fire");
	small.size = PixelSize(64, 96);
	small.SpriteFrames = 6;
	small.NumDirections = 1;
	small.Sleep = 2;
	MissileType big("test-big-fire");
	big.size = PixelSize(96, 96);
	big.SpriteFrames = 10;
	big.NumDirections = 1;
	big.Sleep = 2;
	const std::vector<BurningBuildingFrame> saved = BurningBuildingFrames;
	setFrames({{0, &big}, {50, &small}, {75, nullptr}});

	CUnit unit;
	unit.Refs = 1;
	unit.Variable.resize(std::max<size_t>(HP_INDEX + 1, UnitTypeVar.GetNumberVariable()));
	unit.Variable[HP_INDEX].Max = 400;
	unit.Variable[HP_INDEX].Value = 299;
	unit.Orders.push_back(COrder::NewActionStill());
	unit.Burning = 1;
	{
		MissileFire fire;
		fire.Type = &small;
		fire.SourceUnit = &unit;
		fire.Action(); // picture 0 -> 1
		CHECK(fire.Type == &small);
		CHECK(fire.SpriteFrame == 1);

		unit.Variable[HP_INDEX].Value = 199; // under half, in the middle of the small fire's loop
		fire.Action();
		CHECK(fire.Type == &big);   // at the next step (2 cycles), not 4 pictures later
		CHECK(fire.SpriteFrame == 0);
		CHECK(fire.TTL != 0);

		unit.Variable[HP_INDEX].Value = 300; // repaired to 75 %: the fire goes at the next step
		fire.Action();
		CHECK(fire.TTL == 0);
		CHECK(int(unit.Burning) == 0);
		fire.SourceUnit = nullptr;
	}
	unit.Orders.clear();
	BurningBuildingFrames = saved;
}

TEST_CASE("flame shield, run 55: the flames reach units 2 tiles off, not 3; 1 - 2 a hit there")
{
	// ORIG55 item 11: tile offsets from the carrier (distance = max(|dx|, |dy|)). Each flame makes
	// its area hit from its own place on the ring; a unit is hit on a step when its centre lies in
	// the inner (22/32 tile) or outer (42/32 tile) zone of that place.
	const PixelSize tile(32, 32);
	const auto zonesOn = [&](int tx, int ty, int zone) {
		int steps = 0;
		for (int s = 0; s < FlameShieldSteps; ++s) {
			const PixelPos flame(FlameShieldOffset(s, 32, false), FlameShieldOffset(s, 32, true));
			if (SplashZone(flame, PixelPos(tx * 32, ty * 32), tile) == zone) {
				++steps;
			}
		}
		return steps;
	};
	// The live losses over ~495 cycles (cast 1 and 3) and ours: 5 flames, a hit every 16th call,
	// 1 - 2 (mean 1.5) in the outer zone, on the share of the 51 ring steps that reach the unit.
	struct Live {
		int dx, dy, lost;
	};
	const Live hurt[] = {{-2, 0, 44}, {-2, -1, 46}, {-2, 1, 20}, {2, -1, 63}, {-2, 1, 21}, {2, 1, 28}};
	for (const Live &u : hurt) {
		CAPTURE(u.dx);
		CAPTURE(u.dy);
		CHECK(zonesOn(u.dx, u.dy, 0) == 0); // never the inner zone at distance 2
		const int outer = zonesOn(u.dx, u.dy, 2);
		CHECK(outer > 0);
		const double ours = 5.0 * 1.5 * (495.0 / 16.0) * outer / FlameShieldSteps;
		CHECK(ours > u.lost * 0.75);
		CHECK(ours < u.lost * 1.3);
	}
	// distance 3: nothing (live: Footman 1498 at +3, 0 lost 0); ours at no offset of the ring 3 off
	for (int d = -3; d <= 3; ++d) {
		for (const auto &t : {Vec2i(3, d), Vec2i(-3, d), Vec2i(d, 3), Vec2i(d, -3)}) {
			CAPTURE(t.x);
			CAPTURE(t.y);
			CHECK(zonesOn(t.x, t.y, 0) + zonesOn(t.x, t.y, 2) == 0);
		}
	}
	// the outer zone of the flames' 4 is 1 or 2 a hit (live: all 154 loss steps at distance 2)
	CHECK(keys(splashValues(4, 2, 0)) == span(1, 2));
	// the carrier is spared (live 3 of 3): MissileHitsZones passes it as the spared unit
	CHECK_FALSE(SplashTakesUnit(true, false, true));
}

TEST_CASE("Burning building fire: its height above the footprint centre by building size")
{
	// classic px of the 32 px tile (live run 58: farm 24, barracks 20; S2B: hall 26)
	CHECK(BurningFlameLiftClassicPx(2) == 24);
	CHECK(BurningFlameLiftClassicPx(3) == 20);
	CHECK(BurningFlameLiftClassicPx(4) == 26);
	// unmeasured sizes take the nearest measured one
	CHECK(BurningFlameLiftClassicPx(1) == 24);
	CHECK(BurningFlameLiftClassicPx(5) == 26);
	// never the old whole tile
	for (int t = 1; t <= 5; ++t) {
		CHECK(BurningFlameLift(t, 32) < 32);
	}
	// map px for other tile sizes: at 96 screen px a tile the farm's fire is 72 px up (measured
	// -72.3), the barracks' 60 (-59.8), the hall's 78 (-75.8 Remastered, -78 = 26 x 3 classic)
	CHECK(BurningFlameLift(2, 96) == 72);
	CHECK(BurningFlameLift(3, 96) == 60);
	CHECK(BurningFlameLift(4, 96) == 78);
	CHECK(BurningFlameLift(2, 64) == 48);
	CHECK(BurningFlameLift(4, 32) == 26);
}
