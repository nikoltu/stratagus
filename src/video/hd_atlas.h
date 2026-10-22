//     _________ __                 __
//    /   _____//  |_____________ _/  |______     ____  __ __  ______
//    \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//    /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ \
//   /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//           \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/// @file hd_atlas.h - Shared data model for HD sprite atlases (.hdatlas).
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

// This header is shared by the host importer tool and the engine consumer. It
// must stay free of engine/SDL/Qt dependencies: only <cstdint> and the C++
// standard library are permitted so it compiles in any context.

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hdatlas
{

/// One sprite frame within a split page. Fixed layout, little-endian on target.
struct HdFrameRecord {
	uint32_t nameHash;       // FNV-1a (32-bit) of the TexturePacker key, e.g. "arrow_0"
	uint16_t page;           // which split PNG page (0-based)
	uint16_t sx, sy, sw, sh; // src rect on that page (page-local, post-split)
	int16_t  offx, offy;     // spriteSourceSize.x / .y (trim offset into the untrimmed canvas)
	uint16_t cw, ch;         // sourceSize.w / .h (untrimmed canvas dimensions)
	uint8_t  rotated;        // TexturePacker 'rotated'; MUST be 0 for P0 (assert on import)
	uint8_t  _pad;
};

// Binary file .hdatlas layout (all little-endian):
//   char     magic[4]      = {'H','D','A','1'}
//   uint32_t cap           (the GPU page-size cap used at import)
//   uint32_t pageCount
//   uint32_t frameCount
//   for each page: uint16_t nameLen; char name[nameLen]  (PNG filename, no path, e.g. "foo.p0.png")
//   for each frame: HdFrameRecord (packed, in the order below)
//
// Record ordering is deterministic: frames are written sorted by their name
// using std::string operator< so the output is byte-reproducible regardless of
// the JSON object's map/iteration order.

/// Four-byte magic identifying an .hdatlas stream.
inline constexpr char kMagic[4] = { 'H', 'D', 'A', '1' };

/// 32-bit FNV-1a hash of a NUL-terminated string. Offset basis 2166136261,
/// prime 16777619. Shared by tool, engine and tests so keys hash identically.
inline uint32_t fnv1a(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; ++s) {
		h ^= static_cast<uint8_t>(*s);
		h *= 16777619u;
	}
	return h;
}

/// Classic view: expand a 256-entry palette of 6-bit R,G,B triplets (the layout of the game's
/// .ppl files) into R,G,B,A bytes. Each value is multiplied by 4; alpha is 255. With keyIndex0
/// index 0 becomes fully transparent (sprites, masks); without it index 0 is a normal colour (tiles).
inline void expandPalette6(const uint8_t *ppl, bool keyIndex0, uint8_t out[256][4])
{
	for (int i = 0; i < 256; ++i) {
		for (int c = 0; c < 3; ++c) {
			out[i][c] = static_cast<uint8_t>((ppl[i * 3 + c] & 0x3F) * 4);
		}
		out[i][3] = 255;
	}
	if (keyIndex0) {
		out[0][0] = out[0][1] = out[0][2] = out[0][3] = 0;
	}
}

/// Classic view: the one-pixel checkerboard of explored-but-unseen ground. True for the pixels of
/// a fog cell that stay black: those whose column + row (counted inside the cell) is even. The
/// classic fog masks end in the same checkerboard, so a dithered mask and its neighbours join.
inline bool classicFogDither(int x, int y)
{
	return ((x + y) & 1) == 0;
}

/// Classic view: the palette indices of a sprite's team-colour pixels (bright to dark).
constexpr int ClassicTeamFirst = 208;
constexpr int ClassicTeamCount = 4;

/// Classic view: engine frame number -> frame of the classic group. A negative engine frame f is
/// frame (-f - 1) mirrored left-right inside its cell (the west headings).
inline int classicFrame(int engineFrame, bool &mirror)
{
	mirror = engineFrame < 0;
	return mirror ? -engineFrame - 1 : engineFrame;
}

/// Classic view: first palette index of the four-entry run that holds a player colour, by the
/// colour's name, or -1 for a name without a run. Seven runs follow each other from index 208;
/// yellow is the run 12..15.
inline int classicRampStart(const char *colourName)
{
	static const struct { const char *name; int start; } runs[] = {
		{"red", 208}, {"blue", 212}, {"green", 216}, {"violet", 220},
		{"orange", 224}, {"black", 228}, {"white", 232}, {"yellow", 12},
	};
	if (colourName) {
		for (const auto &run : runs) {
			const char *a = run.name;
			const char *b = colourName;
			while (*a && *a == *b) {
				++a;
				++b;
			}
			if (!*a && !*b) {
				return run.start;
			}
		}
	}
	return -1;
}

/// Classic view: move the team-colour run of an expanded palette onto the run that starts at
/// rampStart, so a sprite cut through `rgba` shows that player's colour. Other entries stay.
inline void classicApplyRamp(uint8_t rgba[256][4], int rampStart)
{
	if (rampStart < 0 || rampStart + ClassicTeamCount > 256 || rampStart == ClassicTeamFirst) {
		return;
	}
	uint8_t run[ClassicTeamCount][4];
	for (int i = 0; i < ClassicTeamCount; ++i) {
		for (int c = 0; c < 4; ++c) {
			run[i][c] = rgba[rampStart + i][c];
		}
	}
	for (int i = 0; i < ClassicTeamCount; ++i) {
		for (int c = 0; c < 4; ++c) {
			rgba[ClassicTeamFirst + i][c] = run[i][c];
		}
	}
}

/// Classic view: "<group>/indexed_<N>" -> group and N. `mark` is the text between the two
/// ("/indexed_"). False when the name has no mark or no number after it.
inline bool classicSplitGroupFrame(const std::string &name, const char *mark, std::string &group, int &n)
{
	const size_t at = name.rfind(mark);
	if (at == std::string::npos || at == 0) {
		return false;
	}
	size_t i = at + std::string(mark).size();
	if (i >= name.size()) {
		return false;
	}
	int value = 0;
	for (; i < name.size(); ++i) {
		if (name[i] < '0' || name[i] > '9' || value > 100000) {
			return false;
		}
		value = value * 10 + (name[i] - '0');
	}
	group = name.substr(0, at);
	n = value;
	return true;
}

/// Classic view: the classic group of the common land construction site (the scaffold stages of a
/// building under construction), or nullptr. `constructionFile` is the construction's own sprite
/// file, `tileset` the map's effect tileset ("forest", "winter", "swamp", "xswamp"). Only the two
/// tilesets seen in the original's classic view answer; a construction with art of its own
/// (shipyard, foundry, refinery, oil platform) and the other tilesets give nullptr.
inline const char *classicLandSiteGroup(const std::string &constructionFile, const std::string &tileset)
{
	static const char site[] = "land_construction_site.png";
	const size_t n = sizeof(site) - 1;
	if (constructionFile.size() < n) {
		return nullptr;
	}
	const size_t at = constructionFile.size() - n;
	for (size_t i = 0; i < n; ++i) {
		char c = constructionFile[at + i];
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
		if (c != site[i]) {
			return nullptr;
		}
	}
	if (at > 0 && constructionFile[at - 1] != '/' && constructionFile[at - 1] != '\\') {
		return nullptr;
	}
	if (tileset == "forest") {
		return "Other/build_1.grp";
	}
	if (tileset == "winter") {
		return "Other/s_build1.grp";
	}
	return nullptr;
}

/// Classic view: number of scaffold stages in the land construction site group.
constexpr int ClassicLandSiteFrames = 2;

/// Classic view: which of a megatile's four minimap samples along one axis (column or row of its
/// mapColors record) minimap pixel `p` shows, for a map `tiles` wide on a minimap `pixels` wide.
/// On the original's 128-pixel minimap the first pixel of a tile shows sample 0, the next one
/// sample 1, and so on (measured at 4, 2 and 4/3 pixels a tile). A larger minimap shows the same
/// samples of each tile, stretched over the tile's pixels.
inline int classicMinimapSlot(int p, int tiles, int pixels)
{
	constexpr int Original = 128;
	if (p < 0 || tiles <= 0 || pixels <= 0) {
		return 0;
	}
	const int tile = p * tiles / pixels;
	const int first = (tile * pixels + tiles - 1) / tiles;
	const int length = ((tile + 1) * pixels + tiles - 1) / tiles - first;
	int shown = ((tile + 1) * Original + tiles - 1) / tiles - (tile * Original + tiles - 1) / tiles;
	shown = shown < 1 ? 1 : shown > 4 ? 4 : shown;
	if (p <= first || length <= 0) {
		return 0;
	}
	const int slot = (p - first) * shown / length;
	return slot >= shown ? shown - 1 : slot;
}

/// Classic view: the record of a megatile in a colour file of `count` records whose first one
/// is the megatile `base`, or -1 when the file has none for it.
constexpr int classicMinimapRecord(int megatile, int base, int count)
{
	return megatile >= base && megatile - base < count ? megatile - base : -1;
}

/// A tileset sheet of `rows` rows of `pitch` tiles holds the tile: a map that names a tile past
/// the sheet's end must not be sampled there.
constexpr bool tileSheetHolds(int tile, int pitch, int rows)
{
	return tile >= 0 && pitch > 0 && tile / pitch < rows;
}

/// Classic view: the classic group names to try for a sprite, from its entry in the HD sprite map
/// (atlas stem and frame prefix). The group's base name is the prefix without a trailing "_<digits>";
/// the folder is Human or Orc when the atlas stem says so, then Other, Monster, Human, Orc.
inline std::vector<std::string> classicGroupNames(const std::string &hdAtlas, const std::string &hdPrefix)
{
	std::string base = hdPrefix;
	size_t digits = base.size();
	while (digits > 0 && base[digits - 1] >= '0' && base[digits - 1] <= '9') {
		--digits;
	}
	if (digits < base.size() && digits > 0 && base[digits - 1] == '_') {
		base.erase(digits - 1);
	}
	std::vector<std::string> names;
	if (base.empty()) {
		return names;
	}
	const char *first = hdAtlas.find("human") != std::string::npos ? "Human"
	                    : hdAtlas.find("orc") != std::string::npos ? "Orc" : nullptr;
	if (first) {
		names.push_back(std::string(first) + "/" + base + ".grp");
	}
	for (const char *folder : {"Other", "Monster", "Human", "Orc"}) {
		if (!first || std::string(first) != folder) {
			names.push_back(std::string(folder) + "/" + base + ".grp");
		}
	}
	return names;
}

/// HD-ATLAS-CACHE-GROWS: the unit atlas `stem` (units_<race|common>_<group>_<sprites|masks>) belongs
/// to a biome other than `biome`. Whole tokens are compared, so "swamp" is not found in "xswamp";
/// a race's common pages carry no biome token and belong to every biome.
inline bool stemIsOfOtherBiome(const std::string &stem, const std::string &biome)
{
	size_t begin = 0;
	while (begin <= stem.size()) {
		size_t end = stem.find('_', begin);
		if (end == std::string::npos) {
			end = stem.size();
		}
		const std::string token = stem.substr(begin, end - begin);
		if ((token == "forest" || token == "winter" || token == "swamp" || token == "xswamp")
			&& token != biome) {
			return true;
		}
		begin = end + 1;
	}
	return false;
}

/// LOAD-CRASH-3473 (work/LOAD_CRASH_3473.md): the unit atlas stem `stem` with its biome token
/// replaced by `biome`. The scripts give some units of one tileset the file of another (Wasteland
/// Town Hall: tilesets/summer/human/buildings/town_hall.png), so the roster entry names the forest
/// atlas; the original holds the same family in the map's own biome atlas (units_human_swamp has
/// thall_0). A stem without a biome token (a race's common pages), an unknown `biome` or a stem
/// already of `biome` comes back unchanged.
inline std::string stemInBiome(const std::string &stem, const std::string &biome)
{
	if (biome != "forest" && biome != "winter" && biome != "swamp" && biome != "xswamp") {
		return stem;
	}
	size_t begin = 0;
	while (begin <= stem.size()) {
		size_t end = stem.find('_', begin);
		if (end == std::string::npos) {
			end = stem.size();
		}
		const std::string token = stem.substr(begin, end - begin);
		if (token == "forest" || token == "winter" || token == "swamp" || token == "xswamp") {
			return token == biome ? stem : stem.substr(0, begin) + biome + stem.substr(end);
		}
		begin = end + 1;
	}
	return stem;
}

/// PRELOAD-PLAN (STUTTER-FIRST-DRAW-LOAD): the unit atlas stems (each a _sprites and a _masks page)
/// every map of `biome` loads behind the loading screen, in load order: the biome's common atlas
/// (neutral buildings, corpses, scaffold, effects, shadows), then per race in `races` order (the
/// local player's race first) the race's unit atlas and its building atlas of the biome. A race
/// other than human / orc has no atlas of its own and adds nothing; an unknown biome gives none.
inline std::vector<std::string> preloadBaseStems(const std::string &biome, const std::vector<std::string> &races)
{
	std::vector<std::string> stems;
	if (biome != "forest" && biome != "winter" && biome != "swamp" && biome != "xswamp") {
		return stems;
	}
	stems.push_back("units_common_" + biome);
	for (const std::string &race : races) {
		if (race != "human" && race != "orc") {
			continue;
		}
		for (const std::string &group : {std::string("common"), biome}) {
			const std::string stem = "units_" + race + "_" + group;
			bool seen = false;
			for (const std::string &s : stems) {
				seen = seen || s == stem;
			}
			if (!seen) {
				stems.push_back(stem);
			}
		}
	}
	return stems;
}

/// PRELOAD-PLAN: the stems of `wanted` (the atlases the map's unit types resolve to, in order, with
/// repeats and empty entries) that are not in `have`, once each, in first-seen order.
inline std::vector<std::string> preloadExtraStems(const std::vector<std::string> &wanted,
                                                  const std::vector<std::string> &have)
{
	std::vector<std::string> extra;
	for (const std::string &stem : wanted) {
		if (stem.empty()) {
			continue;
		}
		bool seen = false;
		for (const std::string &s : have) {
			seen = seen || s == stem;
		}
		for (const std::string &s : extra) {
			seen = seen || s == stem;
		}
		if (!seen) {
			extra.push_back(stem);
		}
	}
	return extra;
}

/// PRELOAD-SCRIPT-UNITS (S20+ 3.4.84: an atlas of a race no player has, read mid-game, 1.9 s): the
/// unit type idents a map's own script creates DURING play, from the text of its setup file. A file
/// without "AddTrigger" creates nothing after the load (its units are on the map when the preload
/// runs, and planned from there). Otherwise every CreateUnit / OldCreateUnit call whose first
/// argument is a quoted literal counts, except the map's placed units: lines that begin with
/// "unit = CreateUnit(" (the shape the editor and the PUD converter write; those run at load, where
/// wc2.lua may still convert the race). Once each, in first-seen order; an ident given by a variable
/// cannot be read and is left to its first draw.
inline std::vector<std::string> scriptPlayUnitIdents(const std::string &text)
{
	std::vector<std::string> idents;
	if (text.find("AddTrigger") == std::string::npos) {
		return idents;
	}
	static const std::string call = "CreateUnit(";
	static const std::string placed = "unit = CreateUnit(";
	for (size_t at = text.find(call); at != std::string::npos; at = text.find(call, at + call.size())) {
		const size_t lineStart = text.rfind('\n', at) == std::string::npos ? 0 : text.rfind('\n', at) + 1;
		if (text.compare(lineStart, placed.size(), placed) == 0) {
			continue;
		}
		size_t p = at + call.size();
		while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) {
			++p;
		}
		if (p >= text.size() || (text[p] != '"' && text[p] != '\'')) {
			continue;
		}
		const char quote = text[p];
		const size_t end = text.find(quote, p + 1);
		if (end == std::string::npos || end == p + 1 || text.find('\n', p + 1) < end) {
			continue;
		}
		const std::string ident = text.substr(p + 1, end - p - 1);
		if (std::find(idents.begin(), idents.end(), ident) == idents.end()) {
			idents.push_back(ident);
		}
	}
	return idents;
}

/// PRELOAD-BUDGET: decoded RGBA bytes of a PNG page from its first bytes (signature + IHDR: width
/// at 16, height at 20, big-endian); 0 when `head` is not a PNG header. Every atlas page is
/// uploaded as RGBA32 (or re-packed into RGBA32 pages of the same total), so this is its cost.
inline uint64_t pngRgbaBytes(const unsigned char *head, size_t n)
{
	static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	if (!head || n < 24) {
		return 0;
	}
	for (int i = 0; i < 8; ++i) {
		if (head[i] != sig[i]) {
			return 0;
		}
	}
	if (head[12] != 'I' || head[13] != 'H' || head[14] != 'D' || head[15] != 'R') {
		return 0;
	}
	const auto be32 = [head](int at) {
		return ((uint64_t) head[at] << 24) | ((uint64_t) head[at + 1] << 16)
		       | ((uint64_t) head[at + 2] << 8) | (uint64_t) head[at + 3];
	};
	return be32(16) * be32(20) * 4;
}

/// PRELOAD-BUDGET: the cap for the unit / effect atlases resident after the preload, in MiB of
/// RGBA. Measured pages (Data/Art/hd/unit, 2026-10-21): one biome's whole set for both races is
/// 1040 - 1046 MiB (common <biome> 94, human common 235 + 113, orc common 232 + 151, human <biome>
/// 118 - 120, orc <biome> 97 - 100). That set was already resident in every two-race mission
/// before this plan, only loaded later, so the cap leaves it whole and adds about 54 MiB for the
/// rest; the base set is not cut by it (each of its pages is drawn in a two-race map anyway).
constexpr uint64_t HdPreloadBudgetCapMiB = 1100;

/// The budget for a device with `systemRamMiB` of RAM (0 = unknown): a quarter of the RAM, never
/// more than the cap. 6 GB and more (every tablet of the owner) -> the cap; a 4 GB device 1024.
inline uint64_t hdPreloadBudgetMiB(uint64_t systemRamMiB)
{
	if (systemRamMiB == 0) {
		return HdPreloadBudgetCapMiB;
	}
	const uint64_t quarter = systemRamMiB / 4;
	return quarter < HdPreloadBudgetCapMiB ? quarter : HdPreloadBudgetCapMiB;
}

/// The indices of `bytes` (candidates in priority order) that are loaded when `resident` bytes
/// are held already and `budget` is the cap: each candidate that still fits is taken, one that
/// does not is left out (it loads at its first draw, as before) and a later, smaller one is
/// still tried. A candidate of unknown size (0) is left out: what cannot be measured is not
/// loaded ahead.
inline std::vector<int> fitPreloadBudget(uint64_t resident, const std::vector<uint64_t> &bytes, uint64_t budget)
{
	std::vector<int> kept;
	uint64_t total = resident;
	for (int i = 0; i < (int) bytes.size(); ++i) {
		if (bytes[i] == 0 || total + bytes[i] > budget) {
			continue;
		}
		total += bytes[i];
		kept.push_back(i);
	}
	return kept;
}

/// LOAD-TIME (work/LOAD_TIME_IMPL.md): the order the preload decodes its pages in - the largest
/// first (`bytes` descending, equal sizes in list order). runOrdered hands the pages over in list
/// order, so a big page behind small ones kept the other worker idle while the window was full of
/// small finished pages (host: two workers took 0.9 of the serial time). Largest first, the two
/// common pages (about 235 MiB each) start together and the small ones fill in after them.
inline std::vector<int> preloadDecodeOrder(const std::vector<uint64_t> &bytes)
{
	std::vector<int> order(bytes.size());
	for (size_t i = 0; i < order.size(); ++i) {
		order[i] = (int) i;
	}
	std::stable_sort(order.begin(), order.end(), [&bytes](int a, int b) { return bytes[a] > bytes[b]; });
	return order;
}

/// LOAD-TIME: decode threads (and the window of decoded pages, the same number) for a device with
/// `systemRamMiB` of RAM (0 = unknown) and `cores` CPUs. Each page in flight is up to about
/// 236 MiB of RGBA beside the resident set, so three only from 7 GiB and 6 cores (S8U, S20+);
/// everything else keeps the two it had (S7+ 6 GB, the 3 GB emulator).
inline int preloadDecodeWorkers(uint64_t systemRamMiB, int cores)
{
	return (systemRamMiB >= 7 * 1024 && cores >= 6) ? 3 : 2;
}

/// Flyer shadow. Both views: the place is measured from the centre of the unit's tile on the
/// original (EVIDENCE-AIR-001: captures o14_30, o14_58 classic, o14_72 Remastered; Gnomish Flying
/// Machine and Gryphon Rider). The measures are in 1/64 of a tile = 1/2 classic pixel.
constexpr int FlyerShadowUnit = 64;
/// Remastered view: side of the square the shadow frame is drawn into (EVIDENCE-NG82-010).
constexpr int HdFlyerShadowSize = 92;
/// Remastered view: centre of the blot from the tile centre, +0.5, +43.5 classic px. Live run 58
/// at 96 px a tile (4 cases, 28 grabs: Gryphon Rider and Flying Machine, facings S and E): dx
/// -1.3 .. +1.6, dy +126.8 .. +133.4 screen px, case means +130.0 .. +131.3 = 43.5 classic px;
/// o14_72 (tile 64) had read +44.5. The same +43.5 as the classic disc's ink centre.
constexpr int HdFlyerShadowCentreX = 1;
constexpr int HdFlyerShadowCentreY = 87;
/// Classic view: the group and the frame of the 23 x 23 disc (o14_30 and o14_58: 208 of 208 drawn
/// pixels equal), and the centre of its 32 x 32 cell from the tile centre: 0, +43 classic px. The
/// ink sits at 5..27 of the cell, so its own centre is +0.5, +43.5.
constexpr const char *ClassicFlyerShadowGroup = "Other/shadow.grp";
constexpr int ClassicFlyerShadowFrame = 2;
constexpr int ClassicFlyerShadowCentreY = 86;

struct FlyerShadowRect {
	int x;     // left, from the unit's draw position (top-left of its footprint), screen pixels
	int y;     // top
	int size;  // side
};

/// Remastered view: the square of the flyer shadow. `tilesW` x `tilesH` is the unit's footprint,
/// `tilePxX` x `tilePxY` a tile in native pixels, (`offX`, `offY`) the type's draw offset, `zDisp`
/// the hover height, `zoom` the world zoom.
inline FlyerShadowRect hdFlyerShadowRect(int tilesW, int tilesH, int tilePxX, int tilePxY,
                                         int offX, int offY, int zDisp, float zoom)
{
	// The centre is kept in half pixels until the end: at 32 px a tile the +43.5 would otherwise
	// lose its half pixel before the zoom (1.5 screen px at 96 a tile).
	const float unit = (float) FlyerShadowUnit;
	const int size = (int) std::lround(HdFlyerShadowSize * tilePxX / unit * zoom);
	const float cx = (tilesW * tilePxX / 2.0f + offX + HdFlyerShadowCentreX * tilePxX / unit) * zoom;
	const float cy = (tilesH * tilePxY / 2.0f + offY + HdFlyerShadowCentreY * tilePxY / unit - 2 * zDisp)
	                 * zoom;
	return {(int) std::lround(cx - size / 2.0f), (int) std::lround(cy - size / 2.0f), size};
}

/// SKELETON-SIZE: a family whose sheet is stored reduced (Skeleton, canvas 225 where the rule
/// 6.75 * cell + 122 gives 500; Eye of Kilrogg body, 186 for 338) names its rule canvas in
/// hd_units_map.json ("canvas"). The frame's own canvas is stretched to it: `s` is the sheet
/// density scale, `canvas` the rule canvas (0 = no entry), `frameCanvasW` the frame's stored canvas
/// width. No entry gives `s` itself, untouched.
inline float hdStoredCanvasScale(float s, int canvas, int frameCanvasW)
{
	if (canvas <= 0 || frameCanvasW <= 0) {
		return s;
	}
	return s * (float) canvas / (float) frameCanvasW;
}

struct ClassicFlyerShadowPlace {
	float cx;     // centre of the cell, from the unit's draw position, screen pixels
	float cy;
	float scale;  // screen pixels a classic pixel
};

/// Classic view: where the cell of the shadow disc is drawn; arguments as hdFlyerShadowRect.
inline ClassicFlyerShadowPlace classicFlyerShadowPlace(int tilesW, int tilesH, int tilePxX, int tilePxY,
                                                       int offX, int offY, int zDisp, float zoom)
{
	constexpr float ClassicTile = 32.0f;
	return {(tilesW * tilePxX / 2.0f + offX) * zoom,
	        (tilesH * tilePxY / 2.0f + offY
	         + ClassicFlyerShadowCentreY * tilePxY / (float) FlyerShadowUnit - 2 * zDisp) * zoom,
	        tilePxX * zoom / ClassicTile};
}

/// Minimap: pixels along one axis of the mark of a unit `tiles` wide, at `scale` / `fac` pixels a
/// tile, from the pixel of its own tile. One more than its tiles cover: a one-tile unit on a
/// 1-pixel-a-tile minimap is 2 x 2 (the original's 128 map: ships and flyers o15_12, o15_46, o14_58;
/// ground units o14_03).
inline int minimapMarkPixels(int tiles, int scale, int fac)
{
	return tiles * scale / fac + 1;
}

/// Classic view (the minimap of 128 logical pixels): the mark is the pixels the unit's tiles
/// cover, rounded down, and never under 2. Measured in the original's classic view: a one-tile
/// unit 2 x 2 on a 128 map (o14_03), 2 x 2 on 96 and 64, 4 x 4 on 32; Town Hall 8 x 8 on 64,
/// 16 x 16 on 32; Gold Mine 4 x 4 on 96, 6 x 6 on 64, 12 x 12 on 32.
inline int classicMinimapMarkPixels(int tiles, int scale, int fac)
{
	const int covered = tiles * scale / fac;
	return covered < 2 ? 2 : covered;
}

/// Remastered view: the minimap mark of a unit `tiles` wide, in surface pixels, on a minimap
/// surface `surfacePx` pixels across (its shorter side) at `scale` / `fac` pixels a tile.
/// Measured in the original's Remastered view (live run 71, ORIG71 "Retry", M; minimap 128 px):
/// the mark is the footprint x 4 / 2 / 1 px a tile on 32 / 64 / 128 maps (Farm 8 / 4 / 2, Town
/// Hall 16 / 8 / 4, Gold Mine 12 / 6 / 3), and a one-tile unit is never under 2 x 2 (Peasant
/// 4 / 2 / 2, Footman 2 on 64). The 2 is in the original's 128-pixel minimap's pixels, so on our
/// surface it is 2 x surfacePx / 128 (rounded), never under 2.
inline int remasteredMinimapMarkPixels(int tiles, int scale, int fac, int surfacePx)
{
	const int covered = tiles * scale / fac;
	int floorPx = (2 * surfacePx + 64) / 128;
	if (floorPx < 2) {
		floorPx = 2;
	}
	return covered < floorPx ? floorPx : covered;
}

// Classic view: the picture of the original is a 4:3 box fitted into the window and centred. In
// it the client draws a logical screen of box / s pixels, every logical pixel as s x s screen
// pixels, s = max(1, height of the window / 480) (22 window sizes: steps at 959 / 960, 1439 /
// 1440, 1919 / 1920 whatever the width; 2960 x 1746 gives s 3 and a logical screen of 776 x 582).
// The HUD parts have fixed logical sizes and places; the map view is what is left and shows 32
// logical pixels a tile. All numbers below are logical pixels (read at a 1366 x 768 client, where
// the logical screen is 1024 x 768 and s is 1: captures o11_13, o11_40, box at client x 171).
constexpr int ClassicStepHeight = 480;
constexpr int ClassicTilePx = 32;
// The left column and the frame strips: the map view is logical 176,16 .. Lw - 16, Lh - 16.
constexpr int ClassicViewX = 176;
constexpr int ClassicViewY = 16;
constexpr int ClassicFrameStrip = 16;
// Minimap, inner area: 24..151 x 26..153 (cuts o11_95_minimap_*; the line frame around it at
// 23 / 152 x 25 / 154 is part of the frame art).
constexpr int ClassicMinimapX = 24;
constexpr int ClassicMinimapY = 26;
constexpr int ClassicMinimapSize = 128;
// The Menu button's face (Classic_HUD_Widgets "hud_<race>_small", 60 x 20): found in o11_13 at
// 46,2 (1024 of its 1200 pixels equal; the rest is its caption).
constexpr int ClassicMenuX = 46;
constexpr int ClassicMenuY = 2;
constexpr int ClassicMenuW = 60;
constexpr int ClassicMenuH = 20;

struct ClassicRect {
	int x;
	int y;
	int w;
	int h;
};

/// Classic view: the box on a screen. The original fits the 4:3 box into its window, centred on
/// both axes (1000 x 800 gives 1000 x 750 with 25 black rows above and below). An odd remainder
/// puts the extra black column / row on the right / bottom.
inline ClassicRect classicBox(int screenW, int screenH)
{
	if (screenW <= 0 || screenH <= 0) {
		return {0, 0, 0, 0};
	}
	int w;
	int h;
	if ((long long) screenW * 3 >= (long long) screenH * 4) {
		h = screenH;
		w = (screenH * 8 + 3) / 6;   // round(H * 4 / 3)
		w = w > screenW ? screenW : w;
	} else {
		w = screenW;
		h = (screenW * 6 + 4) / 8;   // round(W * 3 / 4)
		h = h > screenH ? screenH : h;
	}
	return {(screenW - w) / 2, (screenH - h) / 2, w, h};
}

/// Classic view: screen pixels a logical pixel, from the height of the screen alone (not of the
/// box: 2400 x 1920 has a box 1800 high and 4; 1100 x 960 a box 825 high and 2).
inline int classicStep(int screenH)
{
	const int s = screenH / ClassicStepHeight;
	return s < 1 ? 1 : s;
}

struct ClassicScreen {
	ClassicRect box;  // the 4:3 box, screen pixels
	int s;            // screen pixels a logical pixel
	int lw;           // the logical screen
	int lh;
};

/// Classic view: the box, the step and the logical screen of a screen. The logical screen is the
/// whole pixels of box / s, drawn from the box's top-left; where the box is not a multiple of s
/// the last columns / rows of the box (fewer than s) stay black.
inline ClassicScreen classicScreen(int screenW, int screenH)
{
	const ClassicRect box = classicBox(screenW, screenH);
	const int s = classicStep(screenH);
	return {box, s, box.w / s, box.h / s};
}

/// Classic view: the screen rect of a logical rect.
inline ClassicRect classicToScreen(const ClassicScreen &c, int lx, int ly, int lw, int lh)
{
	return {c.box.x + lx * c.s, c.box.y + ly * c.s, lw * c.s, lh * c.s};
}

/// Classic view: the logical pixel under a screen pixel. False when the pixel is outside the
/// logical screen.
inline bool classicFromScreen(const ClassicScreen &c, int sx, int sy, int &lx, int &ly)
{
	if (c.s <= 0 || sx < c.box.x || sy < c.box.y || sx >= c.box.x + c.lw * c.s
		|| sy >= c.box.y + c.lh * c.s) {
		return false;
	}
	lx = (sx - c.box.x) / c.s;
	ly = (sy - c.box.y) / c.s;
	return true;
}

/// Classic view: the map view, logical pixels: what the left column and the three strips leave.
/// A larger logical screen shows more tiles, never a larger tile.
inline ClassicRect classicView(const ClassicScreen &c)
{
	const int w = c.lw - ClassicViewX - ClassicFrameStrip;
	const int h = c.lh - ClassicViewY - ClassicFrameStrip;
	return {ClassicViewX, ClassicViewY, w > 0 ? w : 0, h > 0 ? h : 0};
}

/// Classic view: the side of the minimap's view rectangle, logical pixels, for a view `viewLogical`
/// logical pixels long on a map whose longer side is `mapTiles` tiles. The whole tiles of the view
/// times the minimap's pixels a tile, rounded down; one more where a tile is not a whole number
/// of pixels. Read from the original at 22 logical sizes and three maps, the same at every place
/// of the view:
///   32-tile map (4 px a tile):  26 x 23 tiles 104 x 92 (o11_13, o11_12), 16 x 15 tiles 64 x 60 (o11_11)
///   64-tile map (2 px a tile):  26 x 23 52 x 46 (o11_80), 25 x 22 50 x 44, 27 x 24 54 x 48,
///                               21 x 19 42 x 38, 14 x 14 28 x 28 (o11_83_*)
///   96-tile map (4 / 3 px):     26 x 23 35 x 31 (o21_01, o11_40..68), 18 x 17 25 x 23 (o21_14,
///                               o21_53..55), 14 19, 16 x 15 22 x 21, 12 x 13 17 x 18, 9 x 10
///                               13 x 14, 31 x 27 42 x 37, 33 x 28 45 x 38 (o21_10..31, o21_50)
/// A fit to these three maps, not a decoded rule; a 128-tile map is in no classic capture.
inline int classicMinimapViewPixels(int viewLogical, int mapTiles)
{
	if (viewLogical <= 0 || mapTiles <= 0) {
		return 0;
	}
	const int tiles = viewLogical / ClassicTilePx;
	return tiles * ClassicMinimapSize / mapTiles + (ClassicMinimapSize % mapTiles != 0 ? 1 : 0);
}

/// How many of the `count` tiles that start at tile `first` lie before the end of a view of
/// `viewCount` tiles from `viewFirst`. Never negative: a run that starts past the view's end
/// has none (a countdown loop over a negative count would not end).
inline int tilesBeforeViewEnd(int first, int count, int viewFirst, int viewCount)
{
	const int left = viewFirst + viewCount - first;
	const int n = count < left ? count : left;
	return n > 0 ? n : 0;
}

struct ClassicHudPiece {
	const char *frame;  // frame of Art/classic/HUD/BG_<Race>
	int x;              // place in the logical screen
	int y;
	int w;              // part of the frame that is drawn, from the frame's top-left
	int h;
};

constexpr int ClassicHudPieceMax = 32;

/// Classic view: the frame of a logical screen lw x lh from the pieces of BG_<Race> (TopLeft
/// 176 x 464, TileableLeft 176 x 176, BottomLeft 176 x 16, Top / Bottom 448 x 16, Right 16 x 448,
/// TopRight / BottomRight 16 x 16). Laid out this way at 1024 x 768 every frame pixel outside the
/// widgets equals the original's (o11_13, o11_40: side column below the top piece 50688 of 50688,
/// bottom bar 13312 of 13312, right bar 11776 of 11776). A tiled piece is repeated from its own
/// top-left and cut at the end; so it is at other logical sizes too (o21_14 at 776 x 582 and
/// o21_50 at 720 x 540, orc: column below the top piece, foot, bottom and right strips and both
/// corners equal in every pixel, the top strip outside the resource row).
/// The top piece is TopLeftAlt (the same column with the info panel's frame) while a unit is
/// selected, TopLeft while nothing is (run 56 U2: 2 selected / 3 unselected captures).
/// Returns the number of pieces written (12 at 1024 x 768).
inline int classicHudFramePieces(int lw, int lh, ClassicHudPiece out[ClassicHudPieceMax],
                                 bool unitSelected = false)
{
	constexpr int Side = ClassicViewX;                    // 176
	constexpr int Bar = ClassicFrameStrip;                // 16
	constexpr int SideTopH = 464;
	constexpr int SideTile = 176;
	constexpr int BarTile = 448;
	if (lw < Side + Bar || lh < 2 * Bar) {
		return 0;
	}
	const int right = lw - Bar;
	const int bottom = lh - Bar;
	int n = 0;
	out[n++] = {unitSelected ? "TopLeftAlt" : "TopLeft", 0, 0, Side, bottom < SideTopH ? bottom : SideTopH};
	for (int y = SideTopH; y < bottom && n < ClassicHudPieceMax - 6;) {
		const int h = bottom - y < SideTile ? bottom - y : SideTile;
		out[n++] = {"TileableLeft", 0, y, Side, h};
		y += h;
	}
	out[n++] = {"BottomLeft", 0, bottom, Side, Bar};
	for (int x = Side; x < right && n < ClassicHudPieceMax - 4;) {
		const int w = right - x < BarTile ? right - x : BarTile;
		out[n++] = {"Top", x, 0, w, Bar};
		out[n++] = {"Bottom", x, bottom, w, Bar};
		x += w;
	}
	for (int y = Bar; y < bottom && n < ClassicHudPieceMax - 2;) {
		const int h = bottom - y < BarTile ? bottom - y : BarTile;
		out[n++] = {"Right", right, y, Bar, h};
		y += h;
	}
	out[n++] = {"TopRight", right, 0, Bar, Bar};
	out[n++] = {"BottomRight", right, bottom, Bar, Bar};
	return n;
}

/// Alpha of a team mask pixel: its luminance (the mask page is white with this alpha).
inline uint8_t maskAlpha(unsigned r, unsigned g, unsigned b)
{
	return (uint8_t) ((r * 77 + g * 150 + b * 29) >> 8);
}

/// Runs decode(i) for i = 0 .. count-1 on `workers` threads and hands every result to
/// consume(i, result) on the calling thread, in the order of i whatever order the work ends in.
/// A worker does not start i while `window` results are started and not yet consumed (the one
/// being consumed counts until consume returns), so at most `window` results exist at a time.
/// While the caller waits for a result idle(i) is called on it every `idleMs`. decode must not
/// throw. All workers are joined before the function returns. Returns the largest number of
/// results that were started and not yet consumed.
template <typename Result, typename DecodeFn, typename ConsumeFn, typename IdleFn>
int runOrdered(int count, int workers, int window, int idleMs, DecodeFn decode, ConsumeFn consume,
               IdleFn idle)
{
	struct Shared {
		std::mutex lock;
		std::condition_variable changed;
		std::vector<Result> slot;
		std::vector<char> done;
		int count = 0;
		int next = 0;      // the first index no worker has taken
		int consumed = 0;  // results the caller is through with
		int highWater = 0;
	} shared;
	if (count <= 0) {
		return 0;
	}
	if (workers < 1) { workers = 1; }
	if (window < 1) { window = 1; }
	shared.slot.resize((size_t) count);
	shared.done.assign((size_t) count, 0);
	shared.count = count;

	auto work = [&shared, &decode, window]() {
		for (;;) {
			int i;
			{
				std::unique_lock<std::mutex> guard(shared.lock);
				shared.changed.wait(guard, [&shared, window]() {
					return shared.next >= shared.count || shared.next - shared.consumed < window;
				});
				if (shared.next >= shared.count) {
					return;
				}
				i = shared.next++;
				if (shared.next - shared.consumed > shared.highWater) {
					shared.highWater = shared.next - shared.consumed;
				}
			}
			Result result = decode(i);
			{
				std::lock_guard<std::mutex> guard(shared.lock);
				shared.slot[(size_t) i] = std::move(result);
				shared.done[(size_t) i] = 1;
			}
			shared.changed.notify_all();
		}
	};
	// Joined on every way out, a throwing consume included.
	struct Joiner {
		Shared &shared;
		std::vector<std::thread> threads;
		~Joiner()
		{
			{
				std::lock_guard<std::mutex> guard(shared.lock);
				shared.next = shared.count;
			}
			shared.changed.notify_all();
			for (std::thread &t : threads) {
				if (t.joinable()) { t.join(); }
			}
		}
	} joiner{shared, {}};
	for (int w = 0; w < workers && w < count; ++w) {
		joiner.threads.emplace_back(work);
	}

	for (int i = 0; i < count; ++i) {
		Result result;
		{
			std::unique_lock<std::mutex> guard(shared.lock);
			while (!shared.changed.wait_for(guard, std::chrono::milliseconds(idleMs),
			                                [&shared, i]() { return shared.done[(size_t) i] != 0; })) {
				guard.unlock();
				idle(i);
				guard.lock();
			}
			result = std::move(shared.slot[(size_t) i]);
		}
		consume(i, std::move(result));
		{
			std::lock_guard<std::mutex> guard(shared.lock);
			++shared.consumed;
		}
		shared.changed.notify_all();
	}
	return shared.highWater;
}

} // namespace hdatlas
