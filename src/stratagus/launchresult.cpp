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
/**@name launchresult.cpp - Result record of a game started by a launch request. */
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

#include "stratagus.h"
#include "launchresult.h"

#include "game.h"
#include "map.h"
#include "parameters.h"
#include "tileset.h"
#include "unittype.h"
#include "util.h"

#include <cstdio>
#include <memory>
#include <string>
#include <system_error>

unsigned long LastGameCycle = 0;

static fs::path LaunchResultDirectory()
{
	fs::path path = Parameters::Instance.GetUserDirectory();
	if (!GameName.empty()) {
		path /= GameName;
	}
	path /= "launch";
	return path;
}

/// Replace one file of the launch directory as a whole.
static bool WriteLaunchFile(const std::string &name, const std::string &text)
{
	const fs::path dir = LaunchResultDirectory();
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (ec) {
		ErrorPrint("Launch result: cannot create '%s'\n", dir.string().c_str());
		return false;
	}
	const fs::path target = dir / name;
	const fs::path temporary = dir / (name + ".tmp");

	FILE *fd = fopen(temporary.string().c_str(), "wb");
	if (!fd) {
		ErrorPrint("Launch result: cannot write '%s'\n", temporary.string().c_str());
		return false;
	}
	const size_t written = fwrite(text.data(), 1, text.size(), fd);
	const bool closed = fclose(fd) == 0;
	if (written != text.size() || !closed) {
		ErrorPrint("Launch result: short write to '%s'\n", temporary.string().c_str());
		fs::remove(temporary, ec);
		return false;
	}
	fs::rename(temporary, target, ec);
	if (ec) {
		ErrorPrint("Launch result: cannot replace '%s'\n", target.string().c_str());
		fs::remove(temporary, ec);
		return false;
	}
	return true;
}

bool WriteLaunchResult(const std::string &text)
{
	return WriteLaunchFile("result.json", text);
}

bool WriteLaunchLoading(const std::string &text)
{
	return WriteLaunchFile("loading.txt", text);
}

bool WriteLaunchExport(const std::string &text)
{
	return WriteLaunchFile("export.json", text);
}

bool WriteLaunchValidate(const std::string &text)
{
	return WriteLaunchFile("validate.json", text);
}

/*----------------------------------------------------------------------------
--  The map editor's export and check
----------------------------------------------------------------------------*/

std::string EditorJsonString(const std::string &text)
{
	std::string out = "\"";
	for (const char c : text) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (c == '"') {
			out += "\\\"";
		} else if (c == '\\') {
			out += "\\\\";
		} else if (u < 0x20 || u == 0x7f) {
			char buf[8];
			snprintf(buf, sizeof(buf), "\\u%04x", u);
			out += buf;
		} else {
			out += c;
		}
	}
	out += "\"";
	return out;
}

/// A flag word as a JSON string of hex digits: the word has bits past a double's 53.
static std::string HexString(uint64_t value)
{
	char buf[24];
	snprintf(buf, sizeof(buf), "\"0x%llx\"", static_cast<unsigned long long>(value));
	return buf;
}

std::string EditorContentStamp()
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(FileChecksums));
	return buf;
}

std::string EditorTilesetJson()
{
	const CTileset &tileset = Map.Tileset;
	std::string out = "{\"name\":" + EditorJsonString(tileset.Name);
	out += ",\"file\":" + EditorJsonString(Map.TileModelsFileName.string());
	// A tile names its terrains by their place in this list.
	out += ",\"terrains\":[";
	for (int i = 0; i < tileset.getSolidTerrainCount(); ++i) {
		out += (i ? "," : "") + EditorJsonString(tileset.getTerrainName(i));
	}
	// [index, graphic, flags, base terrain, mixed terrain]; the separators (graphic 0) left out.
	out += "],\"tiles\":[";
	bool first = true;
	for (size_t i = 0; i < tileset.tiles.size(); ++i) {
		const CTile &tile = tileset.tiles[i];
		if (tile.tile == 0) {
			continue;
		}
		out += first ? "[" : ",[";
		first = false;
		out += std::to_string(i) + "," + std::to_string(tile.tile) + "," + HexString(tile.flag)
		       + "," + std::to_string(tile.tileinfo.BaseTerrain) + ","
		       + std::to_string(tile.tileinfo.MixTerrain) + "]";
	}
	out += "]}";
	return out;
}

/// The unit type a building must stand on (an oil platform on its patch), or "".
static std::string OnTopOf(const std::vector<std::unique_ptr<CBuildRestriction>> &rules)
{
	for (const auto &rule : rules) {
		if (const auto *onTop = dynamic_cast<const CBuildRestrictionOnTop *>(rule.get())) {
			return onTop->ParentName;
		}
		if (const auto *all = dynamic_cast<const CBuildRestrictionAnd *>(rule.get())) {
			const std::string name = OnTopOf(all->_or_list);
			if (!name.empty()) {
				return name;
			}
		}
	}
	return "";
}

std::string EditorUnitTypesJson()
{
	std::string out = "[";
	bool first = true;
	for (const CUnitType *type : getUnitTypes()) {
		if (type == nullptr) {
			continue;
		}
		const char *domain = type->MoveType == EMovement::Fly ? "air"
		                   : type->MoveType == EMovement::Naval ? "sea" : "land";
		const bool shore = type->BoolFlag.size() > SHOREBUILDING_INDEX
		                   && type->BoolFlag[SHOREBUILDING_INDEX].value;
		out += first ? "{" : ",{";
		first = false;
		out += "\"ident\":" + EditorJsonString(type->Ident);
		out += ",\"name\":" + EditorJsonString(type->Name);
		out += ",\"w\":" + std::to_string(type->TileWidth);
		out += ",\"h\":" + std::to_string(type->TileHeight);
		out += ",\"grid\":" + std::to_string(type->GridStep);
		out += ",\"domain\":\"" + std::string(domain) + "\"";
		out += std::string(",\"building\":") + (type->Building ? "true" : "false");
		out += std::string(",\"shore\":") + (shore ? "true" : "false");
		out += std::string(",\"neutral\":") + (type->Neutral ? "true" : "false");
		out += ",\"gives\":" + std::to_string(type->GivesResource);
		out += ",\"ontop\":" + EditorJsonString(OnTopOf(type->BuildingRules));
		// The map flags that keep it off a tile (UnitTypeCanBeAt) and those it sets there.
		out += ",\"mask\":" + HexString(type->MovementMask);
		out += ",\"field\":" + HexString(type->FieldFlags);
		out += ",\"icon\":" + EditorJsonString(type->Icon.Name);
		out += "}";
	}
	out += "]";
	return out;
}

static bool HDArtPreload = true;

void SetHDArtPreload(bool on)
{
	HDArtPreload = on;
}

bool HDArtPreloadOn()
{
	return HDArtPreload;
}

static bool EditorChecking = false;
static std::vector<EditorMovedUnit> EditorMoved;

void EditorCheckBegin()
{
	EditorMoved.clear();
	EditorChecking = true;
}

void EditorNoteUnitMoved(const std::string &ident, int player, int askedX, int askedY,
                         int atX, int atY, bool lost)
{
	if (!EditorChecking) {
		return;
	}
	EditorMovedUnit unit;
	unit.ident = ident;
	unit.player = player;
	unit.askedX = askedX;
	unit.askedY = askedY;
	unit.atX = atX;
	unit.atY = atY;
	unit.lost = lost;
	EditorMoved.push_back(unit);
}

std::vector<EditorSeam> FindTileSeams(int width, int height,
                                      const std::vector<uint32_t> &quads,
                                      const std::vector<bool> &mixes)
{
	// The half masks of CEditor::ChangeSurrounding (src/editor/edmap.cpp).
	const uint32_t BH_QUAD_M = 0x0000FFFF; // bottom half
	const uint32_t RH_QUAD_M = 0x00FF00FF; // right half
	std::vector<EditorSeam> seams;
	if (width <= 0 || height <= 0) {
		return seams;
	}
	const size_t count = static_cast<size_t>(width) * height;
	if (quads.size() < count || mixes.size() < count) {
		return seams;
	}
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const size_t i = static_cast<size_t>(y) * width + x;
			if (!mixes[i]) {
				continue;
			}
			// The tile to the right: its left half is this tile's right half.
			if (x + 1 < width && mixes[i + 1]
			    && (quads[i] & RH_QUAD_M) != ((quads[i + 1] >> 8) & RH_QUAD_M)) {
				seams.push_back({x, y, false});
			}
			// The tile below: its top half is this tile's bottom half.
			if (y + 1 < height && mixes[i + width]
			    && (quads[i] & BH_QUAD_M) != ((quads[i + width] >> 16) & BH_QUAD_M)) {
				seams.push_back({x, y, true});
			}
		}
	}
	return seams;
}

std::string EditorCheckJson(const std::string &tileset, int width, int height,
                            const std::vector<EditorSeam> &seams,
                            const std::vector<EditorMovedUnit> &moved, size_t limit)
{
	std::string out = "{\"tileset\":" + EditorJsonString(tileset);
	out += ",\"w\":" + std::to_string(width) + ",\"h\":" + std::to_string(height);
	// [x, y, "right" | "down"]
	out += ",\"seams\":{\"count\":" + std::to_string(seams.size()) + ",\"at\":[";
	for (size_t i = 0; i < seams.size() && i < limit; ++i) {
		out += (i ? ",[" : "[") + std::to_string(seams[i].x) + "," + std::to_string(seams[i].y)
		       + (seams[i].down ? ",\"down\"]" : ",\"right\"]");
	}
	out += "]},\"moved\":{\"count\":" + std::to_string(moved.size()) + ",\"at\":[";
	for (size_t i = 0; i < moved.size() && i < limit; ++i) {
		const EditorMovedUnit &unit = moved[i];
		out += i ? ",{" : "{";
		out += "\"type\":" + EditorJsonString(unit.ident);
		out += ",\"player\":" + std::to_string(unit.player);
		out += ",\"x\":" + std::to_string(unit.askedX) + ",\"y\":" + std::to_string(unit.askedY);
		out += ",\"to_x\":" + std::to_string(unit.atX) + ",\"to_y\":" + std::to_string(unit.atY);
		out += std::string(",\"lost\":") + (unit.lost ? "true" : "false") + "}";
	}
	out += "]}}";
	return out;
}

std::string EditorCheckLoadedMap()
{
	EditorChecking = false;
	const int width = Map.Info.MapWidth;
	const int height = Map.Info.MapHeight;
	std::vector<uint32_t> quads;
	std::vector<bool> mixes;
	const size_t count = (width > 0 && height > 0) ? static_cast<size_t>(width) * height : 0;
	if (count > 0 && Map.Fields.size() >= count) {
		quads.resize(count, 0);
		mixes.resize(count, false);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const CMapField &mf = *Map.Field(Vec2i(x, y));
				const tile_index index = mf.getTileIndex();
				// Left out as the engine editor leaves them out: walls, decorative and
				// non-mixing fields; and what getQuadFromTile has no corners for (the
				// extended tiles, a mixed tile past the slot's fourteenth row).
				if (mf.isAWall() || mf.isFlag(MapFieldDecorative | MapFieldNonMixing)
				    || index >= Map.Tileset.getTileCount() || index >= ExtendedTilesetBeginIdx) {
					continue;
				}
				if (Map.Tileset.tiles[index].tileinfo.MixTerrain != 0 && ((index & 0x00F0) >> 4) > 13) {
					continue;
				}
				const size_t i = static_cast<size_t>(y) * width + x;
				quads[i] = Map.Tileset.getQuadFromTile(index);
				mixes[i] = true;
			}
		}
	}
	const std::string text = EditorCheckJson(Map.Tileset.Name, width, height,
	                                         FindTileSeams(width, height, quads, mixes),
	                                         EditorMoved, 256);
	EditorMoved.clear();
	return text;
}
