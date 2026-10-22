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
/**@name pud.c - library for convert PUD files to native stratagus format. */
//
//      (c) Copyright 2005-2011 by The Stratagus Team and Pali Rohár
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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <zlib.h>

#include <algorithm>

#include "wargus_endian.h"
#include "pud.h"

namespace wc2pud {


#define VERSION "1.0"

const char *PlayerTypeStrings[] = {
	"",
	"",
	"neutral",
	"nobody",
	"computer",
	"person",
	"rescue-passive",
	"rescue-active"
};

const char *RaceNames[] = {
	"human",
	"orc",
	"neutral"
};

const char *TilesetTypeStrings[] = {
	"summer",
	"winter",
	"wasteland",
	"swamp"
};

// unit script names that correspond to the unit types
const char *UnitScriptNames[] = {
	"unit-footman", "unit-grunt",
	"unit-peasant", "unit-peon",
	"unit-ballista", "unit-catapult",
	"unit-knight", "unit-ogre",
	"unit-archer", "unit-axethrower",
	"unit-mage", "unit-death-knight",
	"unit-paladin", "unit-ogre-mage",
	"unit-dwarves", "unit-goblin-sappers",
	"unit-attack-peasant", "unit-attack-peon",
	"unit-ranger", "unit-berserker",
	"unit-female-hero", "unit-evil-knight",
	"unit-flying-angel", "unit-fad-man",
	"unit-white-mage", "unit-beast-cry",
	"unit-human-oil-tanker", "unit-orc-oil-tanker",
	"unit-human-transport", "unit-orc-transport",
	"unit-human-destroyer", "unit-orc-destroyer",
	"unit-battleship", "unit-ogre-juggernaught",
	"", "unit-fire-breeze",
	"", "",
	"unit-human-submarine", "unit-orc-submarine",
	"unit-balloon", "unit-zeppelin",
	"unit-gryphon-rider", "unit-dragon",
	"unit-knight-rider",
	"unit-eye-of-vision",
	"unit-arthor-literios",
	"unit-quick-blade",
	"",
	"unit-double-head",
	"unit-wise-man",
	"unit-ice-bringer",
	"unit-man-of-light",
	"unit-sharp-axe",
	"",
	"unit-skeleton",
	"unit-daemon",
	"unit-critter",
	"unit-farm", "unit-pig-farm",
	"unit-human-barracks", "unit-orc-barracks",
	"unit-church", "unit-altar-of-storms",
	"unit-human-watch-tower", "unit-orc-watch-tower",
	"unit-stables", "unit-ogre-mound",
	"unit-inventor", "unit-alchemist",
	"unit-gryphon-aviary", "unit-dragon-roost",
	"unit-human-shipyard", "unit-orc-shipyard",
	"unit-town-hall", "unit-great-hall",
	"unit-elven-lumber-mill", "unit-troll-lumber-mill",
	"unit-human-foundry", "unit-orc-foundry",
	"unit-mage-tower", "unit-temple-of-the-damned",
	"unit-human-blacksmith", "unit-orc-blacksmith",
	"unit-human-refinery", "unit-orc-refinery",
	"unit-human-oil-platform", "unit-orc-oil-platform",
	"unit-keep", "unit-stronghold",
	"unit-castle", "unit-fortress",
	"unit-gold-mine",
	"unit-oil-patch",
	"unit-human-start-location", "unit-orc-start-location",
	"unit-human-guard-tower", "unit-orc-guard-tower",
	"unit-human-cannon-tower", "unit-orc-cannon-tower",
	"unit-circle-of-power",
	"unit-dark-portal",
	"unit-runestone",
	"unit-human-wall", "unit-orc-wall"
};
static const size_t UnitScriptNameCount = sizeof(UnitScriptNames) / sizeof(UnitScriptNames[0]);

const char *AiTypeNames[] = {
	"wc2-land-attack",
	"wc2-passive",
	"orc-03",
	"hum-04",
	"orc-04",
	"hum-05",
	"orc-05",
	"hum-06",
	"orc-06",
	"hum-07",
	"orc-07",
	"hum-08",
	"orc-08",
	"hum-09",
	"orc-09",
	"hum-10",
	"orc-10",
	"hum-11",
	"orc-11",
	"hum-12",
	"orc-12",
	"hum-13",
	"orc-13",
	"hum-14-orange",
	"orc-14-blue",
	"wc2-sea-attack",
	"wc2-air-attack",
	"hum-14-red",
	"hum-14-white",
	"hum-14-black",
	"orc-14-green",
	"orc-14-white",
	// expansion scenarios
	"orc-exp-4",
	"orc-exp-5",
	"orc-exp-7a",
	"orc-exp-9",
	"orc-exp-10",
	"orc-exp-12",
	"orc-exp-6a",
	"orc-exp-6b",
	"orc-exp-11a",
	"orc-exp-11b",
	"hum-exp-2a",
	"hum-exp-2b",
	"hum-exp-2c",
	"hum-exp-3a",
	"hum-exp-3b",
	"hum-exp-3c",
	"hum-exp-4a",
	"hum-exp-4b",
	"hum-exp-4c",
	"hum-exp-5a",
	"hum-exp-5b",
	"hum-exp-5c",
	"hum-exp-5d",
	"hum-exp-6a",
	"hum-exp-6b",
	"hum-exp-6c",
	"hum-exp-6d",
	"hum-exp-8a",
	"hum-exp-8b",
	"hum-exp-8c",
	"hum-exp-9a",
	"hum-exp-9b",
	"hum-exp-9c",
	"hum-exp-9d",
	"hum-exp-10a",
	"hum-exp-10b",
	"hum-exp-10c",
	"hum-exp-11a",
	"hum-exp-11b",
	"hum-exp-12a",
	"orc-exp-5b",
	"hum-exp-7a",
	"hum-exp-7b",
	"hum-exp-7c",
	"orc-exp-12a",
	"orc-exp-12b",
	"orc-exp-12c",
	"orc-exp-12d",
	"orc-exp-2",
	"orc-exp-7b",
	"orc-exp-3",
};


PudData::PudData()
{
	memset(Description, 0, sizeof(Description));
	memset(Players, 0, sizeof(Players));
	memset(Races, 0, sizeof(Races));
	Tileset = TilesetTypes(0);
	MapSizeX = 0;
	MapSizeY = 0;
	memset(AiType, 0, sizeof(AiType));
	memset(StartGold, 0, sizeof(StartGold));
	memset(StartLumber, 0, sizeof(StartLumber));
	memset(StartOil, 0, sizeof(StartOil));
	memset(StartX, 0, sizeof(StartX));
	memset(StartY, 0, sizeof(StartY));
}

// Append a printf-formatted line to a std::string.
static void AppendLine(std::string &out, const char *fmt, ...)
{
	char buf[600];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	out += buf;
}

// Build the map PRESENTATION as Lua (DefinePlayerTypes + PresentMap). Sets Map.Info when run. The native
// loader runs the setup phase directly afterwards, so no DefineMapSetup file redirect is emitted here.
void PudData::BuildPresentation(std::string &out) const
{
	const PudData &pdata = *this;
	out += "-- Stratagus Map Presentation (native)\n\n";

	char buf[512];
	strcpy(buf, "DefinePlayerTypes(");
	int playerCount = 0;
	for (int i = 0; i != PLAYERMAX; ++i) {
		sprintf(buf + strlen(buf), "\"%s\",", PlayerTypeStrings[pdata.Players[i]]);
		if (pdata.Players[i] != PlayerNobody && pdata.Players[i] != PlayerNeutral) {
			++playerCount;
		}
	}
	strcpy(&buf[strlen(buf) - 1], ")\n");
	out += buf;

	// The description is the map's own text: a quote or backslash in it must not end the Lua string.
	std::string description;
	for (const char *c = pdata.Description; *c; ++c) {
		if (*c == '"' || *c == '\\') {
			description += '\\';
			description += *c;
		} else if (*c == '\n' || *c == '\r') {
			description += ' ';
		} else {
			description += *c;
		}
	}
	AppendLine(out, "PresentMap(\"%s\", %d, %d, %d, %d)\n", description.c_str(),
		playerCount, pdata.MapSizeX, pdata.MapSizeY, 1);
}

// Build the map SETUP as Lua (players, tileset, terrain tiles, units). Run after Map.Create().
void PudData::BuildSetup(std::string &out) const
{
	const PudData &pdata = *this;
	out += "-- Stratagus Map Setup (native)\n\n";

	for (int i = 0; i != PLAYERMAX - 1; ++i) {
		if (pdata.Players[i] == PlayerNobody) {
			continue;
		}
		AppendLine(out, "SetStartView(%d, %d, %d)\n", i, pdata.StartX[i], pdata.StartY[i]);
		AppendLine(out, "SetPlayerData(%d, \"Resources\", \"gold\", %d)\n", i, pdata.StartGold[i]);
		AppendLine(out, "SetPlayerData(%d, \"Resources\", \"wood\", %d)\n", i, pdata.StartLumber[i]);
		AppendLine(out, "SetPlayerData(%d, \"Resources\", \"oil\", %d)\n", i, pdata.StartOil[i]);
		AppendLine(out, "SetPlayerData(%d, \"RaceName\", \"%s\")\n", i, RaceNames[pdata.Races[i]]);
		if (pdata.Players[i] != PlayerNeutral) {
			// The slot's AI byte as the map gives it: the original game selects its script by this
			// number. A map may carry any byte; one outside the name table keeps the first name.
			const unsigned aiId = static_cast<unsigned char>(pdata.AiType[i]);
			const size_t aiNames = sizeof(AiTypeNames) / sizeof(AiTypeNames[0]);
			AppendLine(out, "SetAiType(%d, \"%s\")\n", i, AiTypeNames[aiId < aiNames ? aiId : 0]);
			AppendLine(out, "if SetWc2AiId then SetWc2AiId(%d, %u) end\n", i, aiId);
		}
	}
	AppendLine(out, "SetPlayerData(%d, \"RaceName\", \"neutral\")\n", PLAYERMAX - 1);
	out += "\n";

	AppendLine(out, "LoadTileModels(\"scripts/tilesets/%s.lua\")\n\n", TilesetTypeStrings[pdata.Tileset]);

	for (int y = 0; y < pdata.MapSizeY; ++y) {
		for (int x = 0; x < pdata.MapSizeX; ++x) {
			const int mapOffset = y * pdata.MapSizeX + x;
			AppendLine(out, "SetTile(%d, %d, %d, %d)\n", pdata.Tiles[mapOffset], x, y, pdata.Value[mapOffset]);
		}
	}

	out += "\nlocal unit\n";

	for (size_t i = 0; i < pdata.Units.size(); ++i) {
		if (pdata.Units[i].Type == UnitHumanStart
			|| pdata.Units[i].Type == UnitOrcStart) {
			continue;
		}
		if (pdata.Units[i].Type < 0 || static_cast<size_t>(pdata.Units[i].Type) >= UnitScriptNameCount
			|| strlen(UnitScriptNames[pdata.Units[i].Type]) <= 1) {
			fprintf(stderr, "Skipping unknown unit type %d for player %d at [%d, %d]\n", pdata.Units[i].Type, pdata.Units[i].Player, pdata.Units[i].X, pdata.Units[i].Y);
			continue;
		}
		AppendLine(out, "unit = CreateUnit(\"%s\", %d, {%d, %d})\n",
			UnitScriptNames[pdata.Units[i].Type], pdata.Units[i].Player,
			pdata.Units[i].X, pdata.Units[i].Y);
		if (pdata.Units[i].Type == UnitHumanOilPlatform || pdata.Units[i].Type == UnitOrcOilPlatform ||
				pdata.Units[i].Type == UnitGoldMine || pdata.Units[i].Type == UnitOilPatch) {
			AppendLine(out, "SetResourcesHeld(unit, %d)\n", pdata.Units[i].Data * 2500);
		} else if (pdata.Units[i].Data != 0) {
			// Any other record with a non-zero data word: the original's unit fixed by the map.
			AppendLine(out, "SetUnitMapFixed(unit)\n");
		}
	}
}

// Parse a .PUD held in memory. Every read stays inside [puddata, puddata + size), and every byte that
// later indexes a fixed table (owner type, side, tileset, unit type, a start location's owner) is
// checked: a map that would index outside them is refused (false) instead of crashing the engine at
// load; the caller reports the map as unreadable. The shipped maps stay inside every range (audit
// EA-216). A declared chunk length that runs past the end of the file is cut to the bytes present,
// as the earlier reader effectively did, so a map it accepted is still accepted.
bool PudData::Parse(const unsigned char *puddata, size_t size)
{
	PudData &pdata = *this;
	char header[5];
	unsigned int tmp;
	size_t pos = 0;

	// Reads one chunk header at `pos`; false when fewer than 8 bytes are left.
	auto readHeader = [&](size_t &len) {
		if (size - pos < 8) {
			return false;
		}
		memcpy(header, puddata + pos, 4);
		header[4] = '\0';
		memcpy(&tmp, puddata + pos + 4, 4);
		len = ConvertLE32(tmp);
		pos += 8;
		return true;
	};

	size_t len = 0;
	if (puddata == nullptr || !readHeader(len) || strcmp(header, "TYPE")) {
		return false;
	}
	// "WAR2 MAP" and its terminator.
	if (size - pos < 9 || memcmp(puddata + pos, "WAR2 MAP", 9) != 0) {
		return false;
	}
	pos += std::min(len, size - pos);

	while (pos < size) {
		if (!readHeader(len)) {
			fprintf(stderr, "PUD: truncated chunk header at byte %u\n", static_cast<unsigned>(pos));
			return false;
		}
		const unsigned char *curp = puddata + pos;
		const size_t avail = size - pos;
		// The bytes a chunk reader needs must be in the file (they may run into the next chunk, as
		// with the earlier reader, but never past the end).
		auto need = [&](size_t n) {
			if (n > avail) {
				fprintf(stderr, "PUD: chunk %s needs %u bytes, %u left\n", header,
						static_cast<unsigned>(n), static_cast<unsigned>(avail));
				return false;
			}
			return true;
		};
		if (!strcmp(header, "VER ")) {
			// nothing useful here, skip it
		} else if (!strcmp(header, "DESC")) {
			const size_t n = std::min({avail, std::min(len, avail), sizeof(pdata.Description) - 1});
			size_t k = 0;
			while (k < n && curp[k] != '\0') {
				pdata.Description[k] = static_cast<char>(curp[k]);
				++k;
			}
			pdata.Description[k] = '\0';
			if (k == 0) {
				strcpy(pdata.Description, "(unnamed)");
			}
		} else if (!strcmp(header, "OWNR")) {
			if (!need(PLAYERMAX)) {
				return false;
			}
			for (int i = 0; i < PLAYERMAX; ++i) {
				if (curp[i] > PlayerRescueActive) {
					fprintf(stderr, "PUD: owner type %d of slot %d is out of range\n", curp[i], i);
					return false;
				}
				pdata.Players[i] = (enum PlayerTypes)curp[i];
			}
		} else if (!strcmp(header, "ERA ") || !strcmp(header, "ERAX")) {
			if (!need(1)) {
				return false;
			}
			if (curp[0] > TilesetSwamp) {
				fprintf(stderr, "PUD: tileset %d is out of range\n", curp[0]);
				return false;
			}
			pdata.Tileset = (enum TilesetTypes)curp[0];
		} else if (!strcmp(header, "DIM ")) {
			if (!need(4)) {
				return false;
			}
			pdata.MapSizeX = curp[0] | (curp[1] << 8);
			pdata.MapSizeY = curp[2] | (curp[3] << 8);
		} else if (!strcmp(header, "UDTA")) {
			// FIXME: todo
		} else if (!strcmp(header, "ALOW")) {
			// no puds seem to use this, so we'll ignore
		} else if (!strcmp(header, "UGRD")) {
			// FIXME: todo
		} else if (!strcmp(header, "SIDE")) {
			if (!need(PLAYERMAX)) {
				return false;
			}
			for (int i = 0; i < PLAYERMAX; ++i) {
				if (curp[i] > RaceNeutral) {
					fprintf(stderr, "PUD: side %d of slot %d is out of range\n", curp[i], i);
					return false;
				}
				pdata.Races[i] = (enum RaceTypes)curp[i];
			}
		} else if (!strcmp(header, "SGLD")) {
			if (!need(PLAYERMAX * 2)) {
				return false;
			}
			for (int i = 0; i < PLAYERMAX; ++i) {
				pdata.StartGold[i] = curp[i * 2] | (curp[i * 2 + 1] << 8);
			}
		} else if (!strcmp(header, "SLBR")) {
			if (!need(PLAYERMAX * 2)) {
				return false;
			}
			for (int i = 0; i < PLAYERMAX; ++i) {
				pdata.StartLumber[i] = curp[i * 2] | (curp[i * 2 + 1] << 8);
			}
		} else if (!strcmp(header, "SOIL")) {
			if (!need(PLAYERMAX * 2)) {
				return false;
			}
			for (int i = 0; i < PLAYERMAX; ++i) {
				pdata.StartOil[i] = curp[i * 2] | (curp[i * 2 + 1] << 8);
			}
		} else if (!strcmp(header, "AIPL")) {
			if (!need(PLAYERMAX)) {
				return false;
			}
			// Any byte is kept: BuildSetup clamps it to the name table when it is used.
			for (int i = 0; i < PLAYERMAX; ++i) {
				pdata.AiType[i] = curp[i];
			}
		} else if (!strcmp(header, "MTXM")) {
			const size_t cells = static_cast<size_t>(pdata.MapSizeX) * pdata.MapSizeY;
			if (cells == 0) {
				fprintf(stderr, "PUD: MTXM before a map size\n");
				return false;
			}
			if (!need(cells * 2)) {
				return false;
			}
			pdata.Tiles.assign(cells, 0);
			pdata.Value.assign(cells, 0);

			for (int j = 0; j < pdata.MapSizeY; ++j) {
				for (int i = 0; i < pdata.MapSizeX; ++i) {
					const size_t at = static_cast<size_t>(j) * pdata.MapSizeX + i;
					int v = curp[at * 2] | (curp[at * 2 + 1] << 8);
					pdata.Tiles[at] = v;
					if ((v & 0xFFF0) == 0x00A0 || (v & 0xFFF0) == 0x00C0 || (v & 0xFF00) == 0x0900) {
						// orc wall
						pdata.Value[at] = 40;
					} else if ((v & 0x00F0) == 0x0090 || (v & 0xFFF0) == 0x00B0 || (v & 0xFF00) == 0x0800) {
						// human wall
						pdata.Value[at] = 40;
					} else {
						pdata.Value[at] = 0;
					}
				}
			}
		} else if (!strcmp(header, "SQM ")) { // movement map
		} else if (!strcmp(header, "OILM")) { // oil map
		} else if (!strcmp(header, "REGM")) { // action map
		} else if (!strcmp(header, "SIGN")) {
		} else if (!strcmp(header, "UNIT")) {
			pdata.Units.resize(std::min(len, avail) / 8);

			for (size_t i = 0; i < pdata.Units.size(); ++i) {
				pdata.Units[i].X = curp[i * 8] | (curp[i * 8 + 1] << 8);
				pdata.Units[i].Y = curp[i * 8 + 2] | (curp[i * 8 + 3] << 8);
				pdata.Units[i].Type = curp[i * 8 + 4];
				pdata.Units[i].Player = curp[i * 8 + 5];
				pdata.Units[i].Data = curp[i * 8 + 6] | (curp[i * 8 + 7] << 8);

				if (pdata.Units[i].Player >= PLAYERMAX) {
					fprintf(stderr, "PUD: unit %u has owner %d, out of range\n",
							static_cast<unsigned>(i), pdata.Units[i].Player);
					return false;
				}
				if (pdata.Units[i].Type == UnitHumanStart || pdata.Units[i].Type == UnitOrcStart) {
					pdata.StartX[pdata.Units[i].Player] = pdata.Units[i].X;
					pdata.StartY[pdata.Units[i].Player] = pdata.Units[i].Y;
				}
			}
		} else {
			printf("unknown section: %s, %u\n", header, static_cast<unsigned>(len));
			return false;
		}
		pos += std::min(len, avail);
	}
	// The setup reads one tile word per cell of the map size.
	const size_t cells = static_cast<size_t>(pdata.MapSizeX) * pdata.MapSizeY;
	if (pdata.Tiles.size() != cells) {
		fprintf(stderr, "PUD: %u tiles for a %d x %d map\n", static_cast<unsigned>(pdata.Tiles.size()),
				pdata.MapSizeX, pdata.MapSizeY);
		return false;
	}
	return true;
}

// Convert pud to native stratagus format
} // namespace wc2pud
