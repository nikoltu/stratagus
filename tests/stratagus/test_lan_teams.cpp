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
/**@name test_lan_teams.cpp - The LAN room's teams become alliances at a game's start. */
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

#include "stratagus.h"

#include "game.h"
#include "map.h"
#include "netconnect.h"
#include "player.h"
#include "settings.h"

#include <SDL.h>

#include <vector>

extern SDL_Surface *TheScreen;

namespace
{

/**
**  The players' state as CreateGame sees it on a LAN device: the slots of the map, the presets
**  launch.lua's LanPreset wrote (type, team 1..8), the network's player count and this device's
**  slot. Everything it changes is put back.
*/
class TeamWorld
{
public:
	TeamWorld()
	{
		savedThisPlayer = ThisPlayer;
		savedNumPlayers = NumPlayers;
		savedNetPlayers = NetPlayers;
		savedLocal = NetLocalPlayerNumber;
		savedSettings = GameSettings;
		savedColors = PlayerColorsRGB;
		for (int i = 0; i < PlayerMax; ++i) {
			savedMapTypes[i] = Map.Info.PlayerType[i];
		}
		// CPlayer::Init maps its colour through the screen's format: a 1 x 1 surface stands in.
		savedScreen = TheScreen;
		if (TheScreen == nullptr) {
			ownScreen = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
			TheScreen = ownScreen;
		}
		// CPlayer::Init reads each slot's first colour; without Lua the table may be sized but empty.
		PlayerColorsRGB.resize(PlayerMax);
		for (auto &colours : PlayerColorsRGB) {
			if (colours.empty()) {
				colours.push_back(CColor(0, 0, 0));
			}
		}
		CleanPlayers();
		GameSettings.Init();
		for (int i = 0; i < PlayerMax; ++i) {
			Map.Info.PlayerType[i] = PlayerTypes::PlayerNobody;
		}
		Map.Info.PlayerType[PlayerMax - 1] = PlayerTypes::PlayerNeutral;
	}
	~TeamWorld()
	{
		CleanPlayers();
		ThisPlayer = savedThisPlayer;
		NumPlayers = savedNumPlayers;
		NetPlayers = savedNetPlayers;
		NetLocalPlayerNumber = savedLocal;
		GameSettings = savedSettings;
		PlayerColorsRGB = savedColors;
		for (int i = 0; i < PlayerMax; ++i) {
			Map.Info.PlayerType[i] = savedMapTypes[i];
		}
		TheScreen = savedScreen;
		if (ownScreen != nullptr) {
			SDL_FreeSurface(ownScreen);
		}
	}

	/// A seat of the room in slot `slot`: a person or a computer, in team `team` (-1: no team).
	void Seat(int slot, PlayerTypes type, int team)
	{
		GameSettings.Presets[slot].Type = type;
		GameSettings.Presets[slot].Team = team;
	}

	/// The game starts on the device of slot `local` among `people` network players.
	void Start(int people, int local)
	{
		NetPlayers = people;
		NetLocalPlayerNumber = local;
		CreatePresetPlayers();
	}

private:
	SDL_Surface *savedScreen = nullptr;
	SDL_Surface *ownScreen = nullptr;
	CPlayer *savedThisPlayer = nullptr;
	int savedNumPlayers = 0;
	int savedNetPlayers = 0;
	int savedLocal = 0;
	Settings savedSettings;
	std::vector<std::vector<CColor>> savedColors;
	PlayerTypes savedMapTypes[PlayerMax]{};
};

/// a and b on one side: allied both ways, never enemies, each sees what the other sees.
void CheckSameSide(int a, int b)
{
	CAPTURE(a);
	CAPTURE(b);
	CHECK(Players[a].IsAllied(Players[b]));
	CHECK(Players[b].IsAllied(Players[a]));
	CHECK_FALSE(Players[a].IsEnemy(Players[b]));
	CHECK_FALSE(Players[b].IsEnemy(Players[a]));
	CHECK(Players[a].HasSharedVisionWith(Players[b]));
	CHECK(Players[b].HasSharedVisionWith(Players[a]));
	CHECK(Players[a].GetSharedVision().count(b) == 1);
	CHECK(Players[b].GetSharedVision().count(a) == 1);
	CHECK(Players[a].Team == Players[b].Team);
	CHECK(Players[a].IsTeamed(Players[b]));
}

/// a and b on different sides: enemies both ways, not allied, no shared vision.
void CheckOtherSide(int a, int b)
{
	CAPTURE(a);
	CAPTURE(b);
	CHECK(Players[a].IsEnemy(Players[b]));
	CHECK(Players[b].IsEnemy(Players[a]));
	CHECK_FALSE(Players[a].IsAllied(Players[b]));
	CHECK_FALSE(Players[b].IsAllied(Players[a]));
	CHECK_FALSE(Players[a].HasSharedVisionWith(Players[b]));
	CHECK_FALSE(Players[b].HasSharedVisionWith(Players[a]));
	CHECK(Players[a].Team != Players[b].Team);
}

} // namespace

TEST_CASE("lan teams: two people against one person")
{
	TeamWorld world;
	world.Seat(0, PlayerTypes::PlayerPerson, 1);
	world.Seat(1, PlayerTypes::PlayerPerson, 2);
	world.Seat(2, PlayerTypes::PlayerPerson, 1);
	world.Start(3, 2);

	REQUIRE(ThisPlayer == &Players[2]);
	CheckSameSide(0, 2);
	CheckOtherSide(0, 1);
	CheckOtherSide(2, 1);
	// The closed slots and the neutral one are nobody's ally.
	for (int i = 3; i < PlayerMax; ++i) {
		CAPTURE(i);
		CHECK_FALSE(Players[0].IsAllied(Players[i]));
		CHECK_FALSE(Players[i].IsAllied(Players[0]));
		CHECK_FALSE(Players[0].HasSharedVisionWith(Players[i]));
	}
}

TEST_CASE("lan teams: people together against computers")
{
	TeamWorld world;
	world.Seat(0, PlayerTypes::PlayerPerson, 3);
	world.Seat(1, PlayerTypes::PlayerComputer, 5);
	world.Seat(2, PlayerTypes::PlayerPerson, 3);
	world.Seat(3, PlayerTypes::PlayerComputer, 5);
	world.Seat(4, PlayerTypes::PlayerPerson, 3);
	world.Start(3, 0);

	CheckSameSide(0, 2);
	CheckSameSide(0, 4);
	CheckSameSide(2, 4);
	CheckSameSide(1, 3);
	for (int person : {0, 2, 4}) {
		for (int computer : {1, 3}) {
			CheckOtherSide(person, computer);
		}
	}
}

TEST_CASE("lan teams: a computer on the people's side, and computers against each other")
{
	TeamWorld world;
	world.Seat(0, PlayerTypes::PlayerPerson, 1);
	world.Seat(1, PlayerTypes::PlayerPerson, 1);
	world.Seat(2, PlayerTypes::PlayerComputer, 1);
	world.Seat(3, PlayerTypes::PlayerComputer, 2);
	world.Seat(4, PlayerTypes::PlayerComputer, 3);
	world.Start(2, 1);

	REQUIRE(ThisPlayer == &Players[1]);
	CheckSameSide(0, 1);
	CheckSameSide(0, 2);
	CheckSameSide(1, 2);
	// Computers are allied by default (CreatePlayer); different teams make them enemies.
	CheckOtherSide(2, 3);
	CheckOtherSide(2, 4);
	CheckOtherSide(3, 4);
	CheckOtherSide(0, 3);
	CheckOtherSide(1, 4);
}

TEST_CASE("lan teams: every seat its own team is every one against every one")
{
	TeamWorld world;
	world.Seat(0, PlayerTypes::PlayerPerson, 1);
	world.Seat(1, PlayerTypes::PlayerPerson, 2);
	world.Seat(2, PlayerTypes::PlayerComputer, 3);
	world.Seat(3, PlayerTypes::PlayerComputer, 4);
	world.Start(2, 0);

	for (int a = 0; a < 4; ++a) {
		for (int b = a + 1; b < 4; ++b) {
			CheckOtherSide(a, b);
		}
	}
}

TEST_CASE("lan teams: the result is the same whatever the slot order")
{
	// The team of a later slot meets the earlier ones, so a side split over far slots works too.
	TeamWorld world;
	world.Seat(0, PlayerTypes::PlayerComputer, 2);
	world.Seat(5, PlayerTypes::PlayerPerson, 1);
	world.Seat(7, PlayerTypes::PlayerPerson, 1);
	world.Seat(6, PlayerTypes::PlayerComputer, 2);
	world.Start(2, 5);

	REQUIRE(ThisPlayer == &Players[5]);
	CheckSameSide(5, 7);
	CheckSameSide(0, 6);
	CheckOtherSide(5, 0);
	CheckOtherSide(5, 6);
	CheckOtherSide(7, 0);
	CheckOtherSide(7, 6);
}
