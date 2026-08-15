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
/**@name test_hd_atlas.cpp - The test file for the HD sprite atlas layer. */
//
//      (c) Copyright 2026 by The Stratagus Project
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

#include "video.h"

#include <SDL_image.h>

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Synthetic-data helpers. NO Warcraft II bytes appear anywhere; every atlas,
// page and pixel below is generated in-test.
// ---------------------------------------------------------------------------
namespace
{

// A unique per-case scratch directory under the system temp path.
fs::path caseDir(const char *name)
{
	fs::path d = fs::temp_directory_path() / "stratagus_hd_atlas_test" / name;
	fs::remove_all(d);
	fs::create_directories(d);
	return d;
}

void appendU16(std::vector<uint8_t> &out, uint16_t v)
{
	out.push_back(static_cast<uint8_t>(v & 0xff));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

void appendU32(std::vector<uint8_t> &out, uint32_t v)
{
	out.push_back(static_cast<uint8_t>(v & 0xff));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
	out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
	out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

// Serialise a .hdatlas byte-exactly per the layout documented in hd_atlas.h:
//   magic[4], u32 cap, u32 pageCount, u32 frameCount,
//   per page: u16 nameLen; char name[nameLen],
//   per frame: HdFrameRecord (24 packed bytes, written via its raw struct bytes so
//   the layout matches the engine's bulk fread exactly).
std::vector<uint8_t> buildHdAtlas(uint32_t cap,
                                  const std::vector<std::string> &pageNames,
                                  const std::vector<hdatlas::HdFrameRecord> &recs)
{
	std::vector<uint8_t> out;
	out.insert(out.end(), hdatlas::kMagic, hdatlas::kMagic + 4);
	appendU32(out, cap);
	appendU32(out, static_cast<uint32_t>(pageNames.size()));
	appendU32(out, static_cast<uint32_t>(recs.size()));
	for (const std::string &n : pageNames) {
		appendU16(out, static_cast<uint16_t>(n.size()));
		out.insert(out.end(), n.begin(), n.end());
	}
	for (const hdatlas::HdFrameRecord &r : recs) {
		const auto *p = reinterpret_cast<const uint8_t *>(&r);
		out.insert(out.end(), p, p + sizeof(hdatlas::HdFrameRecord));
	}
	return out;
}

void writeBytes(const fs::path &p, const std::vector<uint8_t> &bytes)
{
	std::ofstream f(p, std::ios::binary);
	f.write(reinterpret_cast<const char *>(bytes.data()),
	        static_cast<std::streamsize>(bytes.size()));
}

// pixels: w*h*4 bytes, byte order R,G,B,A (== SDL_PIXELFORMAT_RGBA32 memory order
// on the little-endian engine targets). Written as a real 8-bit RGBA PNG via the
// same SDL_image the engine page loader decodes with.
bool writePagePng(const fs::path &path, int w, int h, const std::vector<uint8_t> &pixels)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
	if (!s) {
		return false;
	}
	SDL_LockSurface(s);
	for (int y = 0; y < h; ++y) {
		std::memcpy(static_cast<uint8_t *>(s->pixels) + y * s->pitch,
		            pixels.data() + static_cast<size_t>(y) * w * 4,
		            static_cast<size_t>(w) * 4);
	}
	SDL_UnlockSurface(s);
	const int rc = IMG_SavePNG(s, path.string().c_str());
	SDL_FreeSurface(s);
	return rc == 0;
}

// A simple, fully-opaque known pattern so byte-exact draw comparisons are unambiguous
// (opaque alpha means BLENDMODE_BLEND leaves the source pixels untouched over the dst).
std::vector<uint8_t> patternPixels(int w, int h)
{
	std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			uint8_t *p = px.data() + (static_cast<size_t>(y) * w + x) * 4;
			p[0] = static_cast<uint8_t>((x * 8 + 1) & 0xff);   // R
			p[1] = static_cast<uint8_t>((y * 8 + 3) & 0xff);   // G
			p[2] = static_cast<uint8_t>(((x + y) * 4 + 5) & 0xff); // B
			p[3] = 255;                                        // A (opaque)
		}
	}
	return px;
}

hdatlas::HdFrameRecord makeRec(const char *name, uint16_t page,
                               uint16_t sx, uint16_t sy, uint16_t sw, uint16_t sh,
                               int16_t offx, int16_t offy, uint16_t cw, uint16_t ch,
                               uint8_t rotated = 0)
{
	hdatlas::HdFrameRecord r{};
	r.nameHash = hdatlas::fnv1a(name);
	r.page = page;
	r.sx = sx; r.sy = sy; r.sw = sw; r.sh = sh;
	r.offx = offx; r.offy = offy;
	r.cw = cw; r.ch = ch;
	r.rotated = rotated;
	r._pad = 0;
	return r;
}

// Minimal SDL software-renderer harness. init() returns false (no CHECK-fail) if the
// dummy video driver or the software renderer are unavailable, so the suite skips
// cleanly on headless CI rather than failing for environment reasons.
struct SdlHarness {
	SDL_Surface *screen = nullptr;
	SDL_Renderer *renderer = nullptr;
	bool ok = false;

	bool init(int w, int h)
	{
		SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
		if (SDL_Init(SDL_INIT_VIDEO) != 0) {
			return false;
		}
		IMG_Init(IMG_INIT_PNG); // best-effort; static SDL_image often needs no init for PNG
		screen = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
		if (!screen) {
			SDL_Quit();
			return false;
		}
		renderer = SDL_CreateSoftwareRenderer(screen);
		if (!renderer) {
			SDL_FreeSurface(screen);
			screen = nullptr;
			SDL_Quit();
			return false;
		}
		TheRenderer = renderer;
		ok = true;
		return true;
	}

	void shutdown()
	{
		TheRenderer = nullptr;
		if (renderer) {
			SDL_DestroyRenderer(renderer);
			renderer = nullptr;
		}
		if (screen) {
			SDL_FreeSurface(screen);
			screen = nullptr;
		}
		if (ok) {
			SDL_Quit();
		}
		ok = false;
	}
};

} // namespace

// ===========================================================================
// Group 1 — pure, always run (no SDL, no files)
// ===========================================================================

TEST_CASE("hd_atlas fnv1a known vectors, determinism and distinctness")
{
	// Empty string -> the FNV offset basis.
	CHECK(hdatlas::fnv1a("") == 2166136261u);
	// Standard FNV-1a 32-bit vector for "a".
	CHECK(hdatlas::fnv1a("a") == 0xe40c292cu);

	// Determinism: same input -> same hash.
	CHECK(hdatlas::fnv1a("footman_0") == hdatlas::fnv1a("footman_0"));

	// A representative set of synthetic sprite keys must be pairwise distinct.
	const char *names[] = {
		"arrow_0", "arrow_1", "vcircle_0", "footman_0", "footman_1",
		"peasant_0", "grunt_0", "ballista_0", "catapult_0", "knight_0",
	};
	const size_t n = sizeof(names) / sizeof(names[0]);
	for (size_t i = 0; i < n; ++i) {
		for (size_t j = i + 1; j < n; ++j) {
			INFO("collision between ", names[i], " and ", names[j]);
			CHECK(hdatlas::fnv1a(names[i]) != hdatlas::fnv1a(names[j]));
		}
	}
}

TEST_CASE("hd_atlas HdFrameRecord ABI layout invariants")
{
	using hdatlas::HdFrameRecord;
	static_assert(sizeof(HdFrameRecord) == 24, "HdFrameRecord must stay 24 bytes for raw fread");

	CHECK(sizeof(HdFrameRecord) == 24);
	CHECK(offsetof(HdFrameRecord, nameHash) == 0);
	CHECK(offsetof(HdFrameRecord, page) == 4);
	CHECK(offsetof(HdFrameRecord, sx) == 6);
	CHECK(offsetof(HdFrameRecord, sy) == 8);
	CHECK(offsetof(HdFrameRecord, sw) == 10);
	CHECK(offsetof(HdFrameRecord, sh) == 12);
	CHECK(offsetof(HdFrameRecord, offx) == 14);
	CHECK(offsetof(HdFrameRecord, offy) == 16);
	CHECK(offsetof(HdFrameRecord, cw) == 18);
	CHECK(offsetof(HdFrameRecord, ch) == 20);
	CHECK(offsetof(HdFrameRecord, rotated) == 22);
	CHECK(offsetof(HdFrameRecord, _pad) == 23);
}

// ===========================================================================
// Group 2 — SDL integration (guarded; SKIP cleanly if no renderer)
// ===========================================================================

TEST_CASE("hd_atlas Load happy path (trimmed + untrimmed + shared page)")
{
	SdlHarness h;
	if (!h.init(64, 64)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("load_ok");

	// Two pages; frames 0 and 1 share page 0, frame 2 lives on page 1.
	const int pw = 32, ph = 32;
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, patternPixels(pw, ph)));
	CHECK(writePagePng(dir / "atlas.p1.png", pw, ph, patternPixels(pw, ph)));

	std::vector<hdatlas::HdFrameRecord> recs = {
		// trimmed: offx/offy != 0, cw/ch > sw/sh
		makeRec("alpha_0", 0, /*sx*/2, /*sy*/3, /*sw*/6, /*sh*/5, /*offx*/4, /*offy*/7, /*cw*/16, /*ch*/14),
		// untrimmed: offsets 0, canvas == src size (shares page 0)
		makeRec("beta_0", 0, /*sx*/10, /*sy*/12, /*sw*/8, /*sh*/8, /*offx*/0, /*offy*/0, /*cw*/8, /*ch*/8),
		// on the second page
		makeRec("gamma_0", 1, /*sx*/1, /*sy*/1, /*sw*/4, /*sh*/4, /*offx*/-2, /*offy*/1, /*cw*/9, /*ch*/9),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png", "atlas.p1.png"}, recs));

	CAtlasGraphic g;
	CHECK(g.Load((dir / "atlas.hdatlas").string()));
	CHECK(g.IsLoaded());
	CHECK(g.NumFrames() == 3);

	CHECK(g.FrameByName("alpha_0") >= 0);
	CHECK(g.FrameByName("beta_0") >= 0);
	CHECK(g.FrameByName("gamma_0") >= 0);
	CHECK(g.FrameByName("absent") == -1);

	// Every field of every frame must round-trip byte-identically.
	for (size_t i = 0; i < recs.size(); ++i) {
		const hdatlas::HdFrameRecord &w = recs[i];
		const hdatlas::HdFrameRecord &r = g.Frame(static_cast<int>(i));
		INFO("frame ", i);
		CHECK(r.nameHash == w.nameHash);
		CHECK(r.page == w.page);
		CHECK(r.sx == w.sx);
		CHECK(r.sy == w.sy);
		CHECK(r.sw == w.sw);
		CHECK(r.sh == w.sh);
		CHECK(r.offx == w.offx);
		CHECK(r.offy == w.offy);
		CHECK(r.cw == w.cw);
		CHECK(r.ch == w.ch);
		CHECK(r.rotated == w.rotated);
	}

	g.~CAtlasGraphic(); // release textures while the renderer is still alive
	new (&g) CAtlasGraphic();
	h.shutdown();
}

TEST_CASE("hd_atlas Load rejects malformed input")
{
	SdlHarness h;
	if (!h.init(64, 64)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	auto goodRecs = []() {
		return std::vector<hdatlas::HdFrameRecord>{
			makeRec("alpha_0", 0, 0, 0, 4, 4, 0, 0, 4, 4),
			makeRec("beta_0", 0, 4, 4, 4, 4, 0, 0, 4, 4),
			makeRec("gamma_0", 0, 8, 8, 4, 4, 0, 0, 4, 4),
		};
	};

	auto writePage = [](const fs::path &p) {
		return writePagePng(p, 16, 16, patternPixels(16, 16));
	};

	SUBCASE("wrong magic")
	{
		fs::path dir = caseDir("bad_magic");
		CHECK(writePage(dir / "atlas.p0.png"));
		std::vector<uint8_t> bytes = buildHdAtlas(4096, {"atlas.p0.png"}, goodRecs());
		bytes[0] = 'X'; // corrupt the magic
		writeBytes(dir / "atlas.hdatlas", bytes);
		CAtlasGraphic g;
		CHECK_FALSE(g.Load((dir / "atlas.hdatlas").string()));
		CHECK_FALSE(g.IsLoaded());
	}

	SUBCASE("truncated (header claims 3 frames, only 1 present)")
	{
		fs::path dir = caseDir("truncated");
		CHECK(writePage(dir / "atlas.p0.png"));
		std::vector<uint8_t> bytes = buildHdAtlas(4096, {"atlas.p0.png"}, goodRecs());
		// Drop the last two frame records (2 * 24 bytes) but keep frameCount == 3.
		bytes.resize(bytes.size() - 2 * sizeof(hdatlas::HdFrameRecord));
		writeBytes(dir / "atlas.hdatlas", bytes);
		CAtlasGraphic g;
		CHECK_FALSE(g.Load((dir / "atlas.hdatlas").string()));
		CHECK_FALSE(g.IsLoaded());
	}

	SUBCASE("record with rotated == 1")
	{
		fs::path dir = caseDir("rotated");
		CHECK(writePage(dir / "atlas.p0.png"));
		std::vector<hdatlas::HdFrameRecord> recs = goodRecs();
		recs[1].rotated = 1; // P0 forbids rotated frames
		writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));
		CAtlasGraphic g;
		CHECK_FALSE(g.Load((dir / "atlas.hdatlas").string()));
		CHECK_FALSE(g.IsLoaded());
	}

	SUBCASE("missing page PNG")
	{
		fs::path dir = caseDir("missing_page");
		// Deliberately do NOT write atlas.p0.png.
		writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, goodRecs()));
		CAtlasGraphic g;
		CHECK_FALSE(g.Load((dir / "atlas.hdatlas").string()));
		CHECK_FALSE(g.IsLoaded());
	}

	h.shutdown();
}

TEST_CASE("hd_atlas byte-exact trimmed draw (pivot/trim math gate)")
{
	const int W = 64, H = 64;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("draw_exact");

	const int pw = 16, ph = 16;
	std::vector<uint8_t> page = patternPixels(pw, ph);
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, page));

	// Trimmed frame: sub-rect (2,3) 5x4, centred canvas 8x6, trim offset (1,1).
	const uint16_t sx = 2, sy = 3, sw = 5, sh = 4;
	const uint16_t cw = 8, ch = 6;
	const int16_t offx = 1, offy = 1;
	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("delta_0", 0, sx, sy, sw, sh, offx, offy, cw, ch),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	const int frame = g.FrameByName("delta_0");
	REQUIRE(frame >= 0);

	// Clear the screen to a sentinel colour and draw at native zoom.
	const uint8_t sr = 7, sg = 11, sb = 13, sa = 255;
	SDL_SetRenderDrawColor(h.renderer, sr, sg, sb, sa);
	SDL_RenderClear(h.renderer);

	GpuWorldDrawZoom = 1.0f;
	const int anchorX = 30, anchorY = 30;
	g.DrawAtlasFrameClip(frame, anchorX, anchorY);
	SDL_RenderPresent(h.renderer);

	// Expected destination top-left per the documented canvas-centre math.
	const int dstX = anchorX - cw / 2 + offx; // 30 - 4 + 1 = 27
	const int dstY = anchorY - ch / 2 + offy; // 30 - 3 + 1 = 28
	REQUIRE(dstX >= 0);
	REQUIRE(dstY >= 0);
	REQUIRE(dstX + sw < W);
	REQUIRE(dstY + sh < H);

	SDL_LockSurface(h.screen);
	const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
	const int pitch = h.screen->pitch;

	auto screenPx = [&](int x, int y) -> const uint8_t * {
		return sp + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
	};

	// 1) The sw x sh block at (dstX,dstY) must equal the source page sub-rect byte-for-byte.
	bool blockExact = true;
	for (int dy = 0; dy < sh; ++dy) {
		for (int dx = 0; dx < sw; ++dx) {
			const uint8_t *want = page.data()
			    + (static_cast<size_t>(sy + dy) * pw + (sx + dx)) * 4;
			const uint8_t *got = screenPx(dstX + dx, dstY + dy);
			if (std::memcmp(want, got, 4) != 0) {
				blockExact = false;
			}
		}
	}
	CHECK(blockExact);

	// 2) A pixel just outside the block (to the right and below) is still the sentinel:
	//    proves the draw placed the block exactly, with no overdraw.
	auto isSentinel = [&](int x, int y) {
		const uint8_t *p = screenPx(x, y);
		return p[0] == sr && p[1] == sg && p[2] == sb && p[3] == sa;
	};
	CHECK(isSentinel(dstX + sw, dstY));          // one column past the right edge
	CHECK(isSentinel(dstX, dstY + sh));          // one row past the bottom edge
	CHECK(isSentinel(dstX - 1, dstY));           // one column before the left edge
	CHECK(isSentinel(dstX, dstY - 1));           // one row above the top edge

	SDL_UnlockSurface(h.screen);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	GpuWorldDrawZoom = 1.0f;
	h.shutdown();
}

TEST_CASE("hd_atlas zoom=2 scales the destination block size")
{
	const int W = 64, H = 64;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("draw_zoom2");

	const int pw = 16, ph = 16;
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, patternPixels(pw, ph)));

	const uint16_t sx = 2, sy = 3, sw = 5, sh = 4;
	const uint16_t cw = 8, ch = 6;
	const int16_t offx = 1, offy = 1;
	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("delta_0", 0, sx, sy, sw, sh, offx, offy, cw, ch),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	const int frame = g.FrameByName("delta_0");
	REQUIRE(frame >= 0);

	const uint8_t sr = 7, sg = 11, sb = 13, sa = 255;
	SDL_SetRenderDrawColor(h.renderer, sr, sg, sb, sa);
	SDL_RenderClear(h.renderer);

	GpuWorldDrawZoom = 2.0f;
	const int anchorX = 30, anchorY = 30;
	g.DrawAtlasFrameClip(frame, anchorX, anchorY);
	SDL_RenderPresent(h.renderer);

	// Position is unchanged by zoom; only the destination size scales by 2.
	const int dstX = anchorX - cw / 2 + offx;
	const int dstY = anchorY - ch / 2 + offy;
	const int zw = 2 * sw, zh = 2 * sh;
	REQUIRE(dstX + zw < W);
	REQUIRE(dstY + zh < H);

	SDL_LockSurface(h.screen);
	const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
	const int pitch = h.screen->pitch;
	auto isSentinel = [&](int x, int y) {
		const uint8_t *p = sp + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
		return p[0] == sr && p[1] == sg && p[2] == sb && p[3] == sa;
	};

	// The block spans exactly zw x zh: the far corner is painted, one pixel past it is sentinel.
	CHECK_FALSE(isSentinel(dstX, dstY));                 // top-left painted
	CHECK_FALSE(isSentinel(dstX + zw - 1, dstY + zh - 1)); // bottom-right painted
	CHECK(isSentinel(dstX + zw, dstY));                 // one column past the scaled width
	CHECK(isSentinel(dstX, dstY + zh));                 // one row past the scaled height

	SDL_UnlockSurface(h.screen);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	GpuWorldDrawZoom = 1.0f;
	h.shutdown();
}
