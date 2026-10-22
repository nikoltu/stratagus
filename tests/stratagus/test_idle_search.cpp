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
/**@name test_idle_search.cpp - The test file for the idle search range. */
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

#include "actions.h"
#include "home_leash.h"
#include "missile_cadence.h"
#include "settings.h"
#include "unit_find.h"

#include <algorithm>
#include <cstdlib>

// Data/Rez/unitdata.dat: attack range, computer and person reaction range of the record.
TEST_CASE("IdleSearchRange: a computer's unit looks in its computer reaction range")
{
	CHECK(IdleSearchRange(true, 1, 6) == 6); // Grunt
	CHECK(IdleSearchRange(true, 4, 7) == 7); // Axethrower
	CHECK(IdleSearchRange(true, 8, 11) == 11); // Catapult
}

TEST_CASE("IdleSearchRange: a person's idle unit looks in its attack range")
{
	CHECK(IdleSearchRange(false, 4, 7) == 4); // Archer (person column 5 is not read)
	CHECK(IdleSearchRange(false, 1, 6) == 1); // Footman
	CHECK(IdleSearchRange(false, 5, 7) == 5); // Ranger with the range upgrade
	CHECK(IdleSearchRange(false, 8, 11) == 8); // Ballista
}

TEST_CASE("MissileCadence: a flame of the Flame Shield hits on every 16th count")
{
	int hits = 0;
	for (int count = 512; count > 0; --count) {
		hits += FlameShieldHitsOn(count);
	}
	CHECK(hits == 32);
	CHECK(FlameShieldHitsOn(17));
	CHECK_FALSE(FlameShieldHitsOn(16));
	CHECK_FALSE(FlameShieldHitsOn(18));
}

TEST_CASE("MissileCadence: the Whirlwind hits 400 times, six cycles apart")
{
	int hits = 0;
	for (int ttl = WhirlwindLifeCycles; ttl > 0; --ttl) {
		hits += WhirlwindHitsOn(ttl);
	}
	CHECK(WhirlwindLifeCycles == 2400);
	CHECK(hits == 400);
	CHECK(WhirlwindHitsOn(12));
	CHECK_FALSE(WhirlwindHitsOn(9));
}

// Computer reaction range of the record: Grunt 6, Axethrower 7, Catapult 11.
TEST_CASE("HomeLeash: the limit is half the computer reaction range plus four")
{
	CHECK(HomeLeashPassCycles == 50);
	CHECK(HomeLeashLimit(6) == 7); // Grunt
	CHECK(HomeLeashLimit(7) == 7); // Axethrower
	CHECK(HomeLeashLimit(11) == 9); // Catapult
}

TEST_CASE("HomeLeash: a unit walks home from the limit on, by the larger axis distance")
{
	CHECK(HomeLeashDistance(10, 10, 16, 12) == 6);
	CHECK(HomeLeashDistance(10, 10, 7, 18) == 8);
	CHECK_FALSE(HomeLeashSendsHome(16, 16, 10, 10, 6)); // 6 tiles: stays where the fight ended
	CHECK(HomeLeashSendsHome(17, 10, 10, 10, 6)); // 7 tiles
	CHECK(HomeLeashSendsHome(10, 3, 10, 10, 6));
	CHECK_FALSE(HomeLeashSendsHome(10, 10, 10, 10, 6));
}

TEST_CASE("HelpCall: 13 tiles for a computer's unit, 2 for a person's, 4 around a person's ship")
{
	CHECK(HelpCallRange(true, false) == 13);
	CHECK(HelpCallRange(true, true) == 13);
	CHECK(HelpCallRange(false, false) == 2);
	CHECK(HelpCallRange(false, true) == 4);
}

TEST_CASE("HelpCall: the box grows from the struck unit's edges")
{
	CHECK(HelpCallInBox(18, 22, 20, 20, 1, 1, 2));
	CHECK_FALSE(HelpCallInBox(17, 20, 20, 20, 1, 1, 2));
	CHECK(HelpCallInBox(25, 20, 20, 20, 4, 4, 2)); // 4 x 4 building: right edge 23
	CHECK_FALSE(HelpCallInBox(26, 20, 20, 20, 4, 4, 2));
	CHECK(HelpCallDistance(25, 27, 20, 20, 4, 4) == 4);
	CHECK(HelpCallDistance(21, 22, 20, 20, 4, 4) == 0);
}

TEST_CASE("HelpCall: who is called")
{
	// a computer's free unit: anywhere in the 13-tile box
	CHECK(HelpCallReaches(true, false, false, 33, 7, 20, 20, 1, 1));
	CHECK_FALSE(HelpCallReaches(true, false, false, 34, 20, 20, 20, 1, 1));
	// a computer's unit fixed by the map: only within 4 tiles of the struck one
	CHECK(HelpCallReaches(true, false, true, 24, 16, 20, 20, 1, 1));
	CHECK_FALSE(HelpCallReaches(true, false, true, 25, 20, 20, 20, 1, 1));
	CHECK_FALSE(HelpCallReaches(true, false, true, 20, 26, 20, 20, 1, 1));
	// a person's unit: 2 tiles, the map mark does not matter
	CHECK(HelpCallReaches(false, false, true, 22, 18, 20, 20, 1, 1));
	CHECK_FALSE(HelpCallReaches(false, false, false, 23, 20, 20, 20, 1, 1));
	// around a person's ship: 4 tiles
	CHECK(HelpCallReaches(false, true, false, 24, 20, 20, 20, 1, 1));
	CHECK_FALSE(HelpCallReaches(false, true, false, 25, 20, 20, 20, 1, 1));
}

TEST_CASE("MissileCadence: a Death and Decay missile hits 10 times, the first on tick 18, then every 24")
{
	int hits = 0;
	int last = -1;
	for (int tick = 0; tick < 1000; ++tick) {
		if (DeathAndDecayHitsOn(tick)) {
			++hits;
			last = tick;
		}
	}
	CHECK(hits == 10);
	CHECK(last == 234);
	CHECK(DeathAndDecayHitsOn(18));
	CHECK(DeathAndDecayHitsOn(42));
	CHECK_FALSE(DeathAndDecayHitsOn(17));
	CHECK_FALSE(DeathAndDecayHitsOn(26));
	CHECK_FALSE(DeathAndDecayHitsOn(258));
}

TEST_CASE("MissileCadence: the rounds of a Death and Decay missile follow each other without a gap")
{
	// round i of the port's chain starts on tick i * 24 and hits on its own tick 18
	for (int round = 0; round < DeathAndDecayRounds; ++round) {
		CHECK(DeathAndDecayHitsOn(round * DeathAndDecayPeriod + DeathAndDecayFirstHit));
	}
	CHECK(DeathAndDecayPictureOn(0, false) == 1);
	CHECK(DeathAndDecayPictureOn(0, true) == 0);
	CHECK(DeathAndDecayPictureOn(2, true) == 0);
	CHECK(DeathAndDecayPictureOn(3, true) == 2);
	CHECK(DeathAndDecayPictureOn(4, false) == 2);
	CHECK(DeathAndDecayPictureOn(7, false) == 3);
	CHECK(DeathAndDecayPictureOn(9, false) == 4);
	CHECK(DeathAndDecayPictureOn(12, false) == 5);
	CHECK(DeathAndDecayPictureOn(14, false) == 6);
	CHECK(DeathAndDecayPictureOn(17, false) == 7);
	CHECK(DeathAndDecayPictureOn(18, false) == 0);
	CHECK(DeathAndDecayPictureOn(23, false) == 0);
}

// Speed bytes of the original's missile types: lightning, torpedo and the small cannon ball 16,
// rock and bolt 8, the others 12 pixels of the 32 pixel tile a cycle.
TEST_CASE("MissileFlight: a tile takes the original's cycles on any tile size")
{
	for (const int tile : {32, 64}) {
		CHECK(MissileFlightCycles(16, tile, tile) == 2); // lightning, torpedo, cannon ball
		CHECK(MissileFlightCycles(12, tile, tile) == 3); // arrow, axe: 2.67
		CHECK(MissileFlightCycles(8, tile, tile) == 4);  // rock, bolt
		CHECK(MissileFlightCycles(12, tile, 4 * tile) == 11); // an arrow over 4 tiles
		CHECK(MissileFlightCycles(8, tile, 8 * tile) == 32);  // a rock over 8 tiles
	}
	CHECK(MissileFlightStep(12, 32, 100, 100, false) == 12);
	CHECK(MissileFlightStep(12, 64, 200, 200, false) == 24);
	CHECK(MissileFlightStep(16, 64, 0, 0, false) == 32);
	CHECK(MissileFlightStep(0, 64, 64, 64, false) == 0);
}

TEST_CASE("MissileFlight: the longer axis of the line carries the speed")
{
	// a diagonal of 4 tiles of 64: length 362, longer axis 256 -> as many cycles as 4 tiles straight
	const int step = MissileFlightStep(12, 64, 256, 362, false);
	CHECK(step == 33);
	CHECK((362 + step - 1) / step == MissileFlightCycles(12, 64, 256));
	// a line 2 across, 4 along
	CHECK(MissileFlightStep(16, 64, 256, 286, false) == 35);
}

TEST_CASE("MissileFlight: a bouncing missile goes at half speed on its last pixels")
{
	CHECK(MissileSlowDistance == 50);
	CHECK(MissileFlightStep(12, 32, 48, 48, true) == 6);
	CHECK(MissileFlightStep(12, 64, 96, 96, true) == 12);
	// a leg of 48 classic pixels: 8 cycles, the original's count of 8 between two area hits
	CHECK((96 + 12 - 1) / 12 == 8);
}

// The original's work routine: 2 x time steps of 5 frames, the first after 11, the end one step later.
TEST_CASE("ProductionCycles: training, research and upgrade take time x 10 + 11 cycles")
{
	CHECK(WC2_FRAMES_PER_TIME_UNIT == 10);
	CHECK(ProductionCycles(45) == 461);   // Peasant
	CHECK(ProductionCycles(60) == 611);   // Footman
	CHECK(ProductionCycles(200) == 2011); // Arrow 1, Town Hall -> Keep
	CHECK(ProductionCycles(250) == 2511);
	// the engine's count: the first unit at once, the wait after it, then a unit every 10 cycles
	for (const int time : {2, 45, 250}) {
		int cycles = 0;
		int ticks = 0;
		int wait = 0;
		while (true) {
			++cycles;
			if (wait) {
				--wait;
				continue;
			}
			const bool firstUnit = ticks == 0;
			if (++ticks >= time) {
				break;
			}
			wait = WC2_FRAMES_PER_TIME_UNIT - 1 + (firstUnit ? WC2_PRODUCTION_EXTRA_FRAMES : 0);
		}
		CHECK(cycles == time * 10 + 11);
		CHECK(cycles == ProductionCycles(time));
	}
}

TEST_CASE("GameSpeedCycleRate: the nine steps of the original's Game Speed")
{
	const int ms[9] = {80, 60, 53, 46, 40, 33, 26, 20, 13};
	for (int step = 0; step < 9; ++step) {
		CHECK(WC2_GAME_SPEED_FRAME_MS[step] == ms[step]);
	}
	CHECK(GameSpeedCycleRate(0) == doctest::Approx(12.5));
	CHECK(GameSpeedCycleRate(25) == doctest::Approx(1000.0 / 53));
	CHECK(GameSpeedCycleRate(50) == doctest::Approx(25.0));
	CHECK(GameSpeedCycleRate(75) == doctest::Approx(1000.0 / 26)); // the factory setting
	CHECK(GameSpeedCycleRate(100) == doctest::Approx(1000.0 / 13));
	// between two steps: a straight line; outside the track: its ends
	CHECK(GameSpeedCycleRate(81) > GameSpeedCycleRate(75));
	CHECK(GameSpeedCycleRate(87) < 50.0);
	CHECK(GameSpeedCycleRate(88) > 50.0);
	CHECK(GameSpeedCycleRate(-5) == doctest::Approx(12.5));
	CHECK(GameSpeedCycleRate(140) == doctest::Approx(1000.0 / 13));
	for (int percent = 1; percent <= 100; ++percent) {
		CHECK(GameSpeedCycleRate(percent) > GameSpeedCycleRate(percent - 1));
	}
}

TEST_CASE("HomeLeash: a unit held to a place goes back to it, hunts, or becomes a wanderer")
{
	// reaction range 6: the limit is 7 tiles
	const auto far = [](int distance) { return HomeLeashSendsHome(10 + distance, 10, 10, 10, 6); };
	CHECK(Role2Action(far(7), false, false, true) == Role2::BackToPoint); // moving
	CHECK(Role2Action(far(7), true, false, true) == Role2::Nothing);      // attacking: never interrupted
	CHECK(Role2Action(far(6), false, true, true) == Role2::NearestEnemy);
	CHECK(Role2Action(far(9), false, true, true) == Role2::BackToPoint);  // far wins over idle
	CHECK(Role2Action(far(3), false, true, false) == Role2::BecomeWanderer);
	CHECK(Role2Action(far(3), false, false, true) == Role2::Nothing);
}

TEST_CASE("HomeLeash: a wanderer's next point is up to a quarter of the map away, cut to the map")
{
	CHECK(WanderAxis(10, 128, 0) == 0);
	CHECK(WanderAxis(100, 128, 63) == 127);
	CHECK(WanderAxis(64, 128, 5) == 37);
	CHECK(WanderAxis(40, 96, 47) == 63);
	CHECK(WanderAxis(64, 128, 64 + 5) == 37); // the random number by half the map
}

TEST_CASE("HomeLeash: a saved game without a role reads a unit with a home as a guard")
{
	CHECK(HomeRoleOnLoad(true, false, 0) == HomeRoleGuard);
	CHECK(HomeRoleOnLoad(false, false, 0) == HomeRoleNone);
	CHECK(HomeRoleOnLoad(true, true, HomeRolePoint) == HomeRolePoint);
	CHECK(HomeRoleOnLoad(true, true, HomeRoleWanderer) == HomeRoleWanderer);
}

TEST_CASE("HomeLeash: a guard breaks off where another slot holds the cell with over 2.5 times its strength")
{
	CHECK(StrengthRetreats(2, 1, 4, 11));
	CHECK_FALSE(StrengthRetreats(2, 1, 4, 10));
	CHECK_FALSE(StrengthRetreats(1, 1, 0, 50)); // its own slot holds the cell
	CHECK_FALSE(StrengthRetreats(-1, 1, 0, 0));
	CHECK(StrengthRetreats(0, 1, -2, 5));
	CHECK_FALSE(StrengthRetreats(0, 1, 5, -2));
	// with the leash: nearer than the limit it stays, retreat or not; from the limit on it walks
	CHECK_FALSE((StrengthRetreats(2, 1, 4, 11) && HomeLeashSendsHome(16, 10, 10, 10, 6)));
	CHECK((StrengthRetreats(2, 1, 4, 11) && HomeLeashSendsHome(17, 10, 10, 10, 6)));
}

TEST_CASE("HomeLeash: a guard walks home from the limit on when idle or when it breaks off")
{
	// reaction range 6: the limit is 7 tiles
	CHECK_FALSE(HomeLeashGuardWalksHome(false, true, 16, 10, 10, 10, 6)); // 6 tiles: stays, break off or not
	CHECK(HomeLeashGuardWalksHome(false, true, 17, 10, 10, 10, 6));       // 7: walks although it fights
	CHECK(HomeLeashGuardWalksHome(true, false, 17, 10, 10, 10, 6));       // the idle rule as before
	CHECK_FALSE(HomeLeashGuardWalksHome(true, false, 16, 10, 10, 10, 6));
	CHECK_FALSE(HomeLeashGuardWalksHome(false, false, 30, 10, 10, 10, 6)); // busy and not outmatched
}

TEST_CASE("HomeLeash: a submarine is answered by the nearest flyer or submarine that attacks nothing")
{
	// a destroyer, a flying machine, a submarine; a unit at (6,5) is struck
	std::vector<SubmarineAnswerUnit> own = {{5, 5, false, false, false}, {40, 40, true, false, false},
	                                        {20, 5, true, false, false}};
	CHECK(SubmarineAnswer(own, 6, 5) == 2);
	own[2].busyWithAttacker = true;
	CHECK(SubmarineAnswer(own, 6, 5) == 1);
	own[1].flyerOrSubmarine = own[2].flyerOrSubmarine = false;
	CHECK(SubmarineAnswer(own, 6, 5) == -1);
	// the struck unit itself does not answer
	CHECK(SubmarineAnswer(std::vector<SubmarineAnswerUnit>{{6, 5, true, false, true}}, 6, 5) == -1);
	// equal distances: the first
	CHECK(SubmarineAnswer(std::vector<SubmarineAnswerUnit>{{0, 5, true, false, false}, {12, 5, true, false, false}}, 6, 5) == 0);
}

TEST_CASE("AutoCast: the spell vectors of a non-caster and of a caster are consistent")
{
	CHECK_FALSE(AutoCastVectorsInconsistent(0, 0));   // casts nothing: both empty
	CHECK_FALSE(AutoCastVectorsInconsistent(22, 22)); // a caster: one entry per spell in both
	CHECK(AutoCastVectorsInconsistent(22, 0));        // a caster type, a unit without the vector
	CHECK(AutoCastVectorsInconsistent(0, 22));
	CHECK(AutoCastVectorsInconsistent(22, 21));       // sized for another spell table
}

TEST_CASE("MissileCadence: a flame of the Flame Shield goes round a ring of 51 steps, scaled with the tile")
{
	// the ring on the classic tile: closed, clockwise, 3 pixels a step on the faster axis
	int minX = 0, maxX = 0, minY = 0, maxY = 0;
	for (int step = 0; step < FlameShieldSteps; ++step) {
		const int x = FlameShieldOffset(step, 32, false);
		const int y = FlameShieldOffset(step, 32, true);
		const int nextX = FlameShieldOffset(FlameShieldNextStep(step), 32, false);
		const int nextY = FlameShieldOffset(FlameShieldNextStep(step), 32, true);
		const int far = std::max(std::abs(nextX - x), std::abs(nextY - y));
		CHECK(far >= 2); // the last step closes the ring
		CHECK(far <= 3);
		// clockwise about the middle (2, -11): the turn from this point to the next is to the right
		CHECK((x - 2) * (nextY + 11) - (y + 11) * (nextX - 2) > 0);
		minX = std::min(minX, x);
		maxX = std::max(maxX, x);
		minY = std::min(minY, y);
		maxY = std::max(maxY, y);
	}
	CHECK(minX == -28);
	CHECK(maxX == 32);
	CHECK(minY == -34);
	CHECK(maxY == 12);
	// some points: the start just left of the top, the right, the bottom, the left, the last
	CHECK(FlameShieldOffset(0, 32, false) == 1);
	CHECK(FlameShieldOffset(0, 32, true) == -34);
	CHECK(FlameShieldOffset(13, 32, false) == 32);
	CHECK(FlameShieldOffset(13, 32, true) == -11);
	CHECK(FlameShieldOffset(26, 32, false) == 1);
	CHECK(FlameShieldOffset(26, 32, true) == 12);
	CHECK(FlameShieldOffset(38, 32, false) == -28);
	CHECK(FlameShieldOffset(38, 32, true) == -10);
	CHECK(FlameShieldOffset(50, 32, false) == -1);
	CHECK(FlameShieldOffset(50, 32, true) == -34);
	// a tile of 64 pixels: twice as wide
	CHECK(FlameShieldOffset(0, 64, false) == 2);
	CHECK(FlameShieldOffset(0, 64, true) == -68);
	CHECK(FlameShieldOffset(13, 64, false) == 64);
	CHECK(FlameShieldOffset(38, 64, false) == -56);
	CHECK(FlameShieldOffset(26, 64, true) == 24);
	// the step counter: one step a call, 50 is followed by 0
	CHECK(FlameShieldNextStep(0) == 1);
	CHECK(FlameShieldNextStep(49) == 50);
	CHECK(FlameShieldNextStep(50) == 0);
	CHECK(FlameShieldNextStep(51) == 0);
	CHECK(FlameShieldNextStep(-3) == 0);
	CHECK(FlameShieldOffset(51, 32, false) == FlameShieldOffset(0, 32, false));
	CHECK(FlameShieldOffset(-1, 32, true) == FlameShieldOffset(0, 32, true));
	CHECK(FlameShieldRoot(0) == 0);
	CHECK(FlameShieldRoot(15) == 3);
	CHECK(FlameShieldRoot(16) == 4);
	CHECK(FlameShieldRoot(8161368506368LL) == 2856810);
	// a whole turn brings a flame back to its start
	int step = 20;
	for (int call = 0; call < FlameShieldSteps; ++call) {
		step = FlameShieldNextStep(step);
	}
	CHECK(step == 20);
}

TEST_CASE("MissileCadence: a rune flashes for 17 cycles in every 256")
{
	// a life of 2048: phase 1 on the first call, 0 again 255 cycles later
	CHECK(RuneFlashPhase(2047) == 1);
	CHECK(RuneFlashPhase(2048) == 0);
	CHECK(RuneFlashPhase(1792) == 0);
	CHECK(RuneFlashPhase(1791) == 1);
	CHECK(RuneFlashPhase(1776) == 16);
	CHECK(RuneFlashPhase(1775) == 17);
	CHECK(RuneFlashPhase(1793) == 255);
	CHECK(RuneFlashPhase(1) == 255);
	// pictures 3 2 1 0 1 2 3 for 2 3 2 3 2 3 2 cycles, then nothing
	const int pictures[RuneFlashCycles] = {3, 3, 2, 2, 2, 1, 1, 0, 0, 0, 1, 1, 2, 2, 2, 3, 3};
	for (int phase = 0; phase < RuneFlashCycles; ++phase) {
		CHECK(RuneFlashPicture(phase) == pictures[phase]);
	}
	CHECK(RuneFlashPicture(17) == -1);
	CHECK(RuneFlashPicture(128) == -1);
	CHECK(RuneFlashPicture(255) == -1);
	CHECK(RuneFlashPicture(-1) == -1);
	// 17 cycles shown in each period of 256
	int shown = 0;
	for (int ttl = 2047; ttl > 2047 - 256; --ttl) {
		shown += RuneFlashPicture(RuneFlashPhase(ttl)) >= 0 ? 1 : 0;
	}
	CHECK(shown == 17);
}

TEST_CASE("MissileCadence: a unit's own explosion is made 8 classic pixels above its centre")
{
	CHECK(UnitExplosionLift(32) == 8);
	CHECK(UnitExplosionLift(64) == 16);
	CHECK(UnitExplosionLift(0) == 0);
}

TEST_CASE("HitReaction: the attacker is the first candidate, then the box, the attacker not twice")
{
	// DECODE_B8 item 5 (0x495830 with argument 0): the remembered attacker is scored first at any
	// distance, then every unit of the box in list order; the first of equal scores wins.
	CHECK(HitReactionCandidates(7, std::vector<int>{}) == std::vector<int>{7});
	CHECK(HitReactionCandidates(7, std::vector<int>{3, 5}) == std::vector<int>{7, 3, 5});
	CHECK(HitReactionCandidates(7, std::vector<int>{3, 7, 5}) == std::vector<int>{7, 3, 5});
	CHECK(HitReactionCandidates(7, std::vector<int>{7}) == std::vector<int>{7});
}

TEST_CASE("target search: candidates north to south, a tie goes to the northernmost")
{
	// DECODE_B10_ROUND2 item 5: the box is walked by pixel y (stable), and the first of equal
	// scores stays. A candidate here: {pixel y, score, name}.
	struct Candidate {
		int y;
		int score;
		int name;
	};
	std::vector<Candidate> table = {{96, 5, 1}, {32, 5, 2}, {64, 7, 3}, {32, 7, 4}, {64, 0, 5}};
	SortNorthFirst(table, [](const Candidate &c) { return c.y; });
	std::vector<int> names;
	for (const Candidate &c : table) {
		names.push_back(c.name);
	}
	CHECK(names == std::vector<int>{2, 4, 3, 5, 1}); // equal y keep the order given

	const auto score = [](const Candidate &c) { return c.score; };
	auto best = FirstOfBestScore(table.begin(), table.end(), score);
	REQUIRE(best != table.end());
	CHECK(best->name == 4); // 7 at y 32 beats 7 at y 64

	std::vector<Candidate> tie = {{160, 5, 1}, {96, 5, 2}};
	CHECK(FirstOfBestScore(tie.begin(), tie.end(), score)->name == 1); // unsorted: list order
	SortNorthFirst(tie, [](const Candidate &c) { return c.y; });
	CHECK(FirstOfBestScore(tie.begin(), tie.end(), score)->name == 2); // the smaller y wins

	std::vector<Candidate> refused = {{0, 0, 1}, {32, 0, 2}};
	CHECK(FirstOfBestScore(refused.begin(), refused.end(), score) == refused.end());
}

TEST_CASE("order cross: pictures 3 2 1 0 held 6 3 2 1 cycles")
{
	const int pictures[ClickCrossCycles] = {3, 3, 3, 3, 3, 3, 2, 2, 2, 1, 1, 0};
	for (int tick = 0; tick < ClickCrossCycles; ++tick) {
		CHECK(ClickCrossPictureOn(tick) == pictures[tick]);
	}
	CHECK(ClickCrossPictureOn(-1) == -1);
	CHECK(ClickCrossPictureOn(12) == -1);
	CHECK(ClickCrossPictureOn(100) == -1);
	// each picture held as the original's screen shows it: the whole X first
	int held[4] = {};
	for (int tick = 0; tick < ClickCrossCycles; ++tick) {
		++held[ClickCrossPictureOn(tick)];
	}
	CHECK(held[0] == 1);
	CHECK(held[1] == 2);
	CHECK(held[2] == 3);
	CHECK(held[3] == 6);
}
