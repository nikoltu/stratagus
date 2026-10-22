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
/**@name test_sound.cpp - The test file for sound.cpp. */
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

#include "sound.h"

// A viewport 14 tiles wide on the 64 px tile pans over 13 tiles = 832 px.

TEST_CASE("sound: stereo runs from the viewport's left edge to its right edge")
{
	CHECK(StereoForMapPixel(0, 0, 832) == -128);
	CHECK(StereoForMapPixel(416, 0, 832) == 0);
	CHECK(StereoForMapPixel(832, 0, 832) == 127);
}

TEST_CASE("sound: stereo is relative to the scrolled viewport")
{
	// the same place on the screen pans the same wherever the map is scrolled to
	const int viewX = 40 * 64;
	CHECK(StereoForMapPixel(viewX, viewX, 832) == -128);
	CHECK(StereoForMapPixel(viewX + 416, viewX, 832) == 0);
	CHECK(StereoForMapPixel(viewX + 208, viewX, 832) == StereoForMapPixel(208, 0, 832));
	// left of the viewport stays left, right of it stays right
	CHECK(StereoForMapPixel(viewX - 640, viewX, 832) == -128);
	CHECK(StereoForMapPixel(viewX + 2000, viewX, 832) == 127);
}

TEST_CASE("sound: stereo of a viewport without width is the centre")
{
	CHECK(StereoForMapPixel(100, 0, 0) == 0);
	CHECK(StereoForMapPixel(100, 0, -64) == 0);
}

TEST_CASE("sound: audio focus scales a volume - full, ducked, silent")
{
	CHECK(FocusScaledVolume(128, AudioFocus::Full) == 128);
	CHECK(FocusScaledVolume(128, AudioFocus::Ducked) == 38);
	CHECK(FocusScaledVolume(128, AudioFocus::Silent) == 0);
	CHECK(FocusScaledVolume(255, AudioFocus::Ducked) == 76);
	CHECK(FocusScaledVolume(0, AudioFocus::Ducked) == 0);
	CHECK(FocusScaledVolume(1, AudioFocus::Ducked) == 0);
}

// Options -> Sound switches (OPTIONS-SOUND-SWITCHES-INERT): UnitVoiceEnabled(voice, isBuilding,
// unitSpeech, unitAck, buildingSound).

TEST_CASE("sound switches: all on lets every voice play")
{
	for (EUnitVoice v : {EUnitVoice::Selected, EUnitVoice::Acknowledging, EUnitVoice::Ready,
	                     EUnitVoice::HelpMe, EUnitVoice::Dying, EUnitVoice::WorkCompleted,
	                     EUnitVoice::Building, EUnitVoice::Docking, EUnitVoice::Repairing,
	                     EUnitVoice::Harvesting, EUnitVoice::Attack, EUnitVoice::Build}) {
		CHECK(UnitVoiceEnabled(v, false, true, true, true));
		CHECK(UnitVoiceEnabled(v, true, true, true, true));
	}
}

TEST_CASE("sound switches: Unit Speech off silences a unit's own talk only")
{
	CHECK_FALSE(UnitVoiceEnabled(EUnitVoice::Selected, false, false, true, true));
	CHECK_FALSE(UnitVoiceEnabled(EUnitVoice::Ready, false, false, true, true));
	CHECK_FALSE(UnitVoiceEnabled(EUnitVoice::WorkCompleted, false, false, true, true));
	CHECK(UnitVoiceEnabled(EUnitVoice::Acknowledging, false, false, true, true));
	CHECK(UnitVoiceEnabled(EUnitVoice::Attack, false, false, true, true));
	// a building's selection is not unit speech
	CHECK(UnitVoiceEnabled(EUnitVoice::Selected, true, false, true, true));
}

TEST_CASE("sound switches: Unit Acknowledgements off silences answers to orders only")
{
	for (EUnitVoice v : {EUnitVoice::Acknowledging, EUnitVoice::Attack, EUnitVoice::Build,
	                     EUnitVoice::Repairing, EUnitVoice::Harvesting}) {
		CHECK_FALSE(UnitVoiceEnabled(v, false, true, false, true));
	}
	CHECK(UnitVoiceEnabled(EUnitVoice::Selected, false, true, false, true));
	CHECK(UnitVoiceEnabled(EUnitVoice::Ready, false, true, false, true));
}

TEST_CASE("sound switches: Building Sound off silences buildings, not units")
{
	CHECK_FALSE(UnitVoiceEnabled(EUnitVoice::Selected, true, true, true, false));
	CHECK_FALSE(UnitVoiceEnabled(EUnitVoice::Building, true, true, true, false));
	CHECK_FALSE(UnitVoiceEnabled(EUnitVoice::Ready, true, true, true, false));
	CHECK(UnitVoiceEnabled(EUnitVoice::Selected, false, true, true, false));
	CHECK(UnitVoiceEnabled(EUnitVoice::Ready, false, true, true, false));
}

TEST_CASE("sound switches: alerts and effects are never gated")
{
	for (EUnitVoice v : {EUnitVoice::HelpMe, EUnitVoice::Dying, EUnitVoice::Docking}) {
		CHECK(UnitVoiceEnabled(v, false, false, false, false));
		CHECK(UnitVoiceEnabled(v, true, false, false, false));
	}
}
