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

#include "hd_synth_graphics.h"
#include "video.h"
#include "../../src/video/hd_classic_hud.h"

#include <SDL_image.h>

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
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

TEST_CASE("hd_atlas byte-exact draw with odd canvas dimensions (cw/2 truncation)")
{
	// Locks the integer-truncation placement rule (dst.x = anchorX - cw/2 + offx, C++
	// truncating division) for an ODD cw/ch, so later pivot work has a documented baseline.
	const int W = 64, H = 64;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("draw_odd");

	const int pw = 16, ph = 16;
	std::vector<uint8_t> page = patternPixels(pw, ph);
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, page));

	// Odd sub-rect and odd untrimmed canvas.
	const uint16_t sx = 1, sy = 2, sw = 5, sh = 3;
	const uint16_t cw = 7, ch = 5; // odd canvas -> cw/2 == 3, ch/2 == 2 (truncated)
	const int16_t offx = 0, offy = 0;
	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("epsilon_0", 0, sx, sy, sw, sh, offx, offy, cw, ch),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	const int frame = g.FrameByName("epsilon_0");
	REQUIRE(frame >= 0);

	const uint8_t sr = 21, sg = 22, sb = 23, sa = 255;
	SDL_SetRenderDrawColor(h.renderer, sr, sg, sb, sa);
	SDL_RenderClear(h.renderer);

	GpuWorldDrawZoom = 1.0f;
	const int anchorX = 31, anchorY = 33;
	g.DrawAtlasFrameClip(frame, anchorX, anchorY);
	SDL_RenderPresent(h.renderer);

	// C++ integer division truncates toward zero: cw/2 == 3 (not 3.5), ch/2 == 2 (not 2.5).
	const int dstX = anchorX - cw / 2 + offx; // 31 - 3 + 0 = 28
	const int dstY = anchorY - ch / 2 + offy; // 33 - 2 + 0 = 31
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

	auto isSentinel = [&](int x, int y) {
		const uint8_t *p = screenPx(x, y);
		return p[0] == sr && p[1] == sg && p[2] == sb && p[3] == sa;
	};
	CHECK(isSentinel(dstX + sw, dstY));  // one column past the right edge
	CHECK(isSentinel(dstX, dstY + sh));  // one row past the bottom edge
	CHECK(isSentinel(dstX - 1, dstY));   // one column before the left edge
	CHECK(isSentinel(dstX, dstY - 1));   // one row above the top edge

	SDL_UnlockSurface(h.screen);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	GpuWorldDrawZoom = 1.0f;
	h.shutdown();
}

TEST_CASE("hd_atlas Load rejects corrupt header counts and structurally invalid records")
{
	SdlHarness h;
	if (!h.init(64, 64)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	auto writePage = [](const fs::path &p) {
		return writePagePng(p, 16, 16, patternPixels(16, 16));
	};

	SUBCASE("frameCount far larger than the file can hold")
	{
		fs::path dir = caseDir("corrupt_count");
		CHECK(writePage(dir / "atlas.p0.png"));
		std::vector<hdatlas::HdFrameRecord> recs = {
			makeRec("alpha_0", 0, 0, 0, 4, 4, 0, 0, 4, 4),
		};
		std::vector<uint8_t> bytes = buildHdAtlas(4096, {"atlas.p0.png"}, recs);
		// Header layout: magic[4](0-3), cap u32(4-7), pageCount u32(8-11), frameCount u32(12-15).
		// Overwrite frameCount with a huge value the file cannot possibly back; the real
		// record bytes stay in place unread.
		bytes[12] = 0xFF; bytes[13] = 0xFF; bytes[14] = 0xFF; bytes[15] = 0x7F;
		writeBytes(dir / "atlas.hdatlas", bytes);
		CAtlasGraphic g;
		CHECK_FALSE(g.Load((dir / "atlas.hdatlas").string()));
		CHECK_FALSE(g.IsLoaded());
	}

	SUBCASE("record references a page beyond pageCount")
	{
		fs::path dir = caseDir("bad_page_ref");
		CHECK(writePage(dir / "atlas.p0.png"));
		std::vector<hdatlas::HdFrameRecord> recs = {
			makeRec("alpha_0", 0, 0, 0, 4, 4, 0, 0, 4, 4),
			makeRec("beta_0", 5 /* only page 0 exists */, 0, 0, 4, 4, 0, 0, 4, 4),
		};
		writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));
		CAtlasGraphic g;
		CHECK_FALSE(g.Load((dir / "atlas.hdatlas").string()));
		CHECK_FALSE(g.IsLoaded());
	}

	SUBCASE("frames present but pageCount == 0")
	{
		fs::path dir = caseDir("frames_no_pages");
		std::vector<hdatlas::HdFrameRecord> recs = {
			makeRec("alpha_0", 0, 0, 0, 4, 4, 0, 0, 4, 4),
		};
		writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {}, recs));
		CAtlasGraphic g;
		CHECK_FALSE(g.Load((dir / "atlas.hdatlas").string()));
		CHECK_FALSE(g.IsLoaded());
	}

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

TEST_CASE("hd_atlas DrawAtlasFrameFull byte-exact placement, no canvas-centre offset")
{
	// Untrimmed frame (offx=offy=0, cw==sw, ch==sh), like a terrain tile, but kept small
	// (8x8) for a fast pixel-exact comparison. DrawAtlasFrameFull must blit it verbatim
	// into the caller-supplied dst rect with no -cw/2 shift (contrast DrawAtlasFrameClip).
	const int W = 64, H = 64;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("full_exact");

	const int pw = 16, ph = 16;
	std::vector<uint8_t> page = patternPixels(pw, ph);
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, page));

	const uint16_t sx = 3, sy = 2, sw = 8, sh = 8;
	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("tile_0", 0, sx, sy, sw, sh, /*offx*/0, /*offy*/0, /*cw*/sw, /*ch*/sh),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	const int frame = g.FrameByName("tile_0");
	REQUIRE(frame >= 0);

	const uint8_t sr = 41, sg = 42, sb = 43, sa = 255;
	SDL_SetRenderDrawColor(h.renderer, sr, sg, sb, sa);
	SDL_RenderClear(h.renderer);

	const int dstX = 20, dstY = 25;
	SDL_Rect dst = {dstX, dstY, sw, sh};
	g.DrawAtlasFrameFull(frame, dst);
	SDL_RenderPresent(h.renderer);

	SDL_LockSurface(h.screen);
	const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
	const int pitch = h.screen->pitch;
	auto screenPx = [&](int x, int y) -> const uint8_t * {
		return sp + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
	};

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

	auto isSentinel = [&](int x, int y) {
		const uint8_t *p = screenPx(x, y);
		return p[0] == sr && p[1] == sg && p[2] == sb && p[3] == sa;
	};
	// No canvas-centre shift: block starts exactly at dst.x/dst.y (were there a -cw/2
	// shift like DrawAtlasFrameClip, the block would start at dstX - sw/2 instead).
	CHECK(isSentinel(dstX - 1, dstY));
	CHECK(isSentinel(dstX, dstY - 1));
	CHECK(isSentinel(dstX + sw, dstY));
	CHECK(isSentinel(dstX, dstY + sh));
	// Were this centred like DrawAtlasFrameClip (dst.x = anchorX - cw/2), the block would
	// start near (dstX - sw/2, dstY - sh/2) instead of at (dstX, dstY); that point is
	// unpainted here, confirming DrawAtlasFrameFull does not apply any canvas-centre shift.
	CHECK(isSentinel(dstX - sw / 2, dstY - sh / 2));

	SDL_UnlockSurface(h.screen);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

TEST_CASE("hd_atlas DrawAtlasFrameFull scales the source into a larger dst rect")
{
	const int W = 64, H = 64;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("full_scaled");

	const int pw = 16, ph = 16;
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, patternPixels(pw, ph)));

	const uint16_t sx = 1, sy = 1, sw = 6, sh = 6;
	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("tile_0", 0, sx, sy, sw, sh, 0, 0, sw, sh),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	const int frame = g.FrameByName("tile_0");
	REQUIRE(frame >= 0);

	const uint8_t sr = 17, sg = 18, sb = 19, sa = 255;
	SDL_SetRenderDrawColor(h.renderer, sr, sg, sb, sa);
	SDL_RenderClear(h.renderer);

	// dst is 2x the frame's native size: proves DrawAtlasFrameFull scales into whatever
	// rect it is given rather than always drawing at native size.
	const int dstX = 10, dstY = 12;
	const int dw = 2 * sw, dh = 2 * sh;
	SDL_Rect dst = {dstX, dstY, dw, dh};
	g.DrawAtlasFrameFull(frame, dst);
	SDL_RenderPresent(h.renderer);

	REQUIRE(dstX + dw < W);
	REQUIRE(dstY + dh < H);

	SDL_LockSurface(h.screen);
	const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
	const int pitch = h.screen->pitch;
	auto isSentinel = [&](int x, int y) {
		const uint8_t *p = sp + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
		return p[0] == sr && p[1] == sg && p[2] == sb && p[3] == sa;
	};

	// All four corners of the (scaled) dst rect must be painted...
	CHECK_FALSE(isSentinel(dstX, dstY));
	CHECK_FALSE(isSentinel(dstX + dw - 1, dstY));
	CHECK_FALSE(isSentinel(dstX, dstY + dh - 1));
	CHECK_FALSE(isSentinel(dstX + dw - 1, dstY + dh - 1));
	// ...and it stops exactly at the dst rect's edges.
	CHECK(isSentinel(dstX + dw, dstY));
	CHECK(isSentinel(dstX, dstY + dh));
	CHECK(isSentinel(dstX - 1, dstY));
	CHECK(isSentinel(dstX, dstY - 1));

	SDL_UnlockSurface(h.screen);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

TEST_CASE("hd_atlas DrawAtlasFrameFull bounds safety: invalid frame indices are no-ops")
{
	const int W = 32, H = 32;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("full_bounds");

	const int pw = 8, ph = 8;
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, patternPixels(pw, ph)));

	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("tile_0", 0, 0, 0, 4, 4, 0, 0, 4, 4),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	REQUIRE(g.NumFrames() == 1);

	const uint8_t sr = 5, sg = 6, sb = 7, sa = 255;
	SDL_SetRenderDrawColor(h.renderer, sr, sg, sb, sa);
	SDL_RenderClear(h.renderer);

	SDL_Rect dst = {2, 2, 4, 4};
	// Negative index, exactly-out-of-range index, and far-out-of-range index: all no-ops.
	g.DrawAtlasFrameFull(-1, dst);
	g.DrawAtlasFrameFull(1, dst);
	g.DrawAtlasFrameFull(9999, dst);
	SDL_RenderPresent(h.renderer);

	SDL_LockSurface(h.screen);
	const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
	const int pitch = h.screen->pitch;
	bool allSentinel = true;
	for (int y = 0; y < H; ++y) {
		for (int x = 0; x < W; ++x) {
			const uint8_t *p = sp + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
			if (!(p[0] == sr && p[1] == sg && p[2] == sb && p[3] == sa)) {
				allSentinel = false;
			}
		}
	}
	CHECK(allSentinel);
	SDL_UnlockSurface(h.screen);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

TEST_CASE("hd_atlas megatile->frame lookup mirrors the P1 map_draw.cpp naming rule")
{
	// Replicates (without pulling in map_draw.cpp/CMap) the 3-line rule documented at
	// src/map/map_draw.cpp EnsureHDTileAtlas(): for megatile index M >= 16, the frame is
	// FrameByName("<era>_" + (M-16)); M < 16 (fog slots) and M beyond the atlas span are -1.
	SdlHarness h;
	if (!h.init(32, 32)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("megatile_map");

	// Synthetic atlas with frames named forest_0..forest_2 (mirrors a real era prefix).
	const int pw = 8, ph = 8;
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, patternPixels(pw, ph)));
	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("forest_0", 0, 0, 0, 2, 2, 0, 0, 2, 2),
		makeRec("forest_1", 0, 2, 0, 2, 2, 0, 0, 2, 2),
		makeRec("forest_2", 0, 4, 0, 2, 2, 0, 0, 2, 2),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));

	const char *era = "forest";
	const int count = 16 + g.NumFrames() + 2; // a couple of megatile slots past the atlas span
	std::vector<int> frameForTile(count, -1);
	for (int m = 16; m < count; ++m) {
		std::string name = era;
		name += '_';
		name += std::to_string(m - 16);
		frameForTile[m] = g.FrameByName(name.c_str());
	}

	// M < 16 (fog slots) are left at -1; the loop never touches them.
	CHECK(frameForTile[0] == -1);
	CHECK(frameForTile[15] == -1);

	// M == 16 -> forest_0, the first real frame.
	REQUIRE(frameForTile[16] >= 0);
	CHECK(frameForTile[16] == g.FrameByName("forest_0"));
	CHECK(g.Frame(frameForTile[16]).sx == 0);

	// M == 17, 18 -> forest_1, forest_2.
	CHECK(frameForTile[17] == g.FrameByName("forest_1"));
	CHECK(frameForTile[18] == g.FrameByName("forest_2"));

	// M beyond the atlas span (forest_3 does not exist) -> -1 (classic-path fallback).
	CHECK(frameForTile[19] == -1);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

// ===========================================================================
// Group 3 — DrawAtlasFrameClipScaled (HD building draw: canvas -> footprint)
// ===========================================================================

TEST_CASE("hd_atlas DrawAtlasFrameClipScaled maps trimmed canvas onto the footprint rect")
{
	// Building-draw case: the frame's untrimmed HD canvas (cw x ch) is scaled to fit the
	// caller's footprint rect (dstW x dstH); the trimmed sub-frame (offx/offy, sw x sh) must
	// land at a proportionally scaled offset/size inside that rect.
	const int W = 320, H = 320;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("scaled_trim");

	const int pw = 64, ph = 64;
	std::vector<uint8_t> page = patternPixels(pw, ph);
	// Overwrite the sub-rect (sx,sy,sw,sh) with a known solid colour so the block is
	// unambiguous against the varying background pattern.
	const uint16_t sx = 8, sy = 8, sw = 25, sh = 25;
	const uint8_t br = 200, bg = 90, bb = 30;
	for (int y = 0; y < sh; ++y) {
		for (int x = 0; x < sw; ++x) {
			uint8_t *p = page.data() + (static_cast<size_t>(sy + y) * pw + (sx + x)) * 4;
			p[0] = br; p[1] = bg; p[2] = bb; p[3] = 255;
		}
	}
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, page));

	// cw=ch=100, sw=sh=50 is the documented example, but the sub-rect must fit inside the
	// page; here cw=ch=100 stays, sw=sh=25 with offx=offy=25 keeps the same trim ratios
	// (offx/cw == 0.25, sw/cw == 0.25) while fitting inside a small synthetic page.
	const uint16_t cw = 100, ch = 100;
	const int16_t offx = 25, offy = 25;
	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("wall_0", 0, sx, sy, sw, sh, offx, offy, cw, ch),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	const int frame = g.FrameByName("wall_0");
	REQUIRE(frame >= 0);

	const uint8_t sr = 51, sg = 52, sb = 53, sa = 255;
	SDL_SetRenderDrawColor(h.renderer, sr, sg, sb, sa);
	SDL_RenderClear(h.renderer);

	// scale = 2x: dstW/cw = 200/100 = 2, dstH/ch = 200/100 = 2.
	const int dstX = 10, dstY = 20, dstW = 200, dstH = 200;
	g.DrawAtlasFrameClipScaled(frame, dstX, dstY, dstW, dstH, /*flip*/false);
	SDL_RenderPresent(h.renderer);

	const int blockX = dstX + offx * 2; // 10 + 50 = 60
	const int blockY = dstY + offy * 2; // 20 + 50 = 70
	const int blockW = sw * 2;          // 50
	const int blockH = sh * 2;          // 50
	REQUIRE(blockX + blockW < W);
	REQUIRE(blockY + blockH < H);

	SDL_LockSurface(h.screen);
	const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
	const int pitch = h.screen->pitch;
	auto screenPx = [&](int x, int y) -> const uint8_t * {
		return sp + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
	};
	auto isBlock = [&](int x, int y) {
		const uint8_t *p = screenPx(x, y);
		return p[0] == br && p[1] == bg && p[2] == bb;
	};
	auto isSentinel = [&](int x, int y) {
		const uint8_t *p = screenPx(x, y);
		return p[0] == sr && p[1] == sg && p[2] == sb && p[3] == sa;
	};

	// The whole scaled block is the solid colour...
	bool blockSolid = true;
	for (int dy = 0; dy < blockH; ++dy) {
		for (int dx = 0; dx < blockW; ++dx) {
			if (!isBlock(blockX + dx, blockY + dy)) {
				blockSolid = false;
			}
		}
	}
	CHECK(blockSolid);

	// ...and the sentinel is untouched just outside its edges.
	CHECK(isSentinel(blockX - 1, blockY));
	CHECK(isSentinel(blockX, blockY - 1));
	CHECK(isSentinel(blockX + blockW, blockY));
	CHECK(isSentinel(blockX, blockY + blockH));

	SDL_UnlockSurface(h.screen);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

TEST_CASE("hd_atlas DrawAtlasFrameClipScaled downscales an oversized HD canvas into a small footprint")
{
	// The real building case: HD canvas (cw/ch) far larger than the on-screen footprint
	// (dstW/dstH), i.e. scale < 1. Confirms the block still lands proportionally and stays
	// fully within the footprint rect (no overflow from truncation/rounding).
	const int W = 128, H = 128;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("scaled_down");

	const int pw = 64, ph = 64;
	std::vector<uint8_t> page = patternPixels(pw, ph);
	const uint16_t sx = 4, sy = 4, sw = 40, sh = 40;
	const uint8_t br = 10, bg = 210, bb = 60;
	for (int y = 0; y < sh; ++y) {
		for (int x = 0; x < sw; ++x) {
			uint8_t *p = page.data() + (static_cast<size_t>(sy + y) * pw + (sx + x)) * 4;
			p[0] = br; p[1] = bg; p[2] = bb; p[3] = 255;
		}
	}
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, page));

	// cw=ch=200 (large HD canvas), sub-frame fills it (offx=offy=0, sw=sh=40 relative to a
	// 200 canvas is a small trimmed region near the origin).
	const uint16_t cw = 200, ch = 200;
	const int16_t offx = 0, offy = 0;
	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("big_0", 0, sx, sy, sw, sh, offx, offy, cw, ch),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	const int frame = g.FrameByName("big_0");
	REQUIRE(frame >= 0);

	const uint8_t sr = 61, sg = 62, sb = 63, sa = 255;
	SDL_SetRenderDrawColor(h.renderer, sr, sg, sb, sa);
	SDL_RenderClear(h.renderer);

	// scale = 0.25: dstW/cw = 50/200 = 0.25.
	const int dstX = 5, dstY = 6, dstW = 50, dstH = 50;
	g.DrawAtlasFrameClipScaled(frame, dstX, dstY, dstW, dstH, /*flip*/false);
	SDL_RenderPresent(h.renderer);

	const int blockX = dstX; // offx*scale == 0
	const int blockY = dstY;
	const int blockW = static_cast<int>(std::lround(sw * 0.25)); // 10
	const int blockH = static_cast<int>(std::lround(sh * 0.25)); // 10
	REQUIRE(blockX + blockW <= dstX + dstW);
	REQUIRE(blockY + blockH <= dstY + dstH);

	SDL_LockSurface(h.screen);
	const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
	const int pitch = h.screen->pitch;
	auto screenPx = [&](int x, int y) -> const uint8_t * {
		return sp + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
	};
	auto isBlock = [&](int x, int y) {
		const uint8_t *p = screenPx(x, y);
		return p[0] == br && p[1] == bg && p[2] == bb;
	};
	auto isSentinel = [&](int x, int y) {
		const uint8_t *p = screenPx(x, y);
		return p[0] == sr && p[1] == sg && p[2] == sb && p[3] == sa;
	};

	// The scaled block's top-left corner is painted with the solid colour...
	CHECK(isBlock(blockX, blockY));
	CHECK(isBlock(blockX + blockW - 1, blockY + blockH - 1));
	// ...and stays inside the footprint: nothing painted at/after the footprint's far edge.
	CHECK(isSentinel(dstX + dstW, dstY));
	CHECK(isSentinel(dstX, dstY + dstH));
	// Nothing painted before the footprint's near edge either.
	CHECK(isSentinel(dstX - 1, dstY));
	CHECK(isSentinel(dstX, dstY - 1));

	SDL_UnlockSurface(h.screen);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

TEST_CASE("hd_atlas DrawAtlasFrameClipScaled tints via colour-mod and resets it afterwards")
{
	const int W = 128, H = 128;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("scaled_tint");

	// A fully white, opaque frame so the tint result is unambiguous.
	const int pw = 32, ph = 32;
	std::vector<uint8_t> page(static_cast<size_t>(pw) * ph * 4, 255);
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, page));

	const uint16_t sx = 0, sy = 0, sw = 16, sh = 16;
	const uint16_t cw = 16, ch = 16;
	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("tint_0", 0, sx, sy, sw, sh, 0, 0, cw, ch),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	const int frame = g.FrameByName("tint_0");
	REQUIRE(frame >= 0);

	SDL_SetRenderDrawColor(h.renderer, 0, 0, 0, 255);
	SDL_RenderClear(h.renderer);

	const int dstX = 10, dstY = 10, dstW = 32, dstH = 32;
	g.DrawAtlasFrameClipScaled(frame, dstX, dstY, dstW, dstH, /*flip*/false,
	                           /*r*/128, /*g*/0, /*b*/0);
	SDL_RenderPresent(h.renderer);

	SDL_LockSurface(h.screen);
	{
		const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
		const int pitch = h.screen->pitch;
		const uint8_t *p = sp + static_cast<size_t>(dstY + dstH / 2) * pitch
		    + static_cast<size_t>(dstX + dstW / 2) * 4;
		CHECK(p[0] == 128);
		CHECK(p[1] == 0);
		CHECK(p[2] == 0);
	}
	SDL_UnlockSurface(h.screen);

	// A subsequent plain (default-colour) draw of the SAME page must NOT be tinted: proves
	// the colour-mod is reset to (255,255,255) after the tinted draw.
	SDL_SetRenderDrawColor(h.renderer, 0, 0, 0, 255);
	SDL_RenderClear(h.renderer);
	g.DrawAtlasFrameClipScaled(frame, dstX, dstY, dstW, dstH, /*flip*/false);
	SDL_RenderPresent(h.renderer);

	SDL_LockSurface(h.screen);
	{
		const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
		const int pitch = h.screen->pitch;
		const uint8_t *p = sp + static_cast<size_t>(dstY + dstH / 2) * pitch
		    + static_cast<size_t>(dstX + dstW / 2) * 4;
		CHECK(p[0] == 255);
		CHECK(p[1] == 255);
		CHECK(p[2] == 255);
	}
	SDL_UnlockSurface(h.screen);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

TEST_CASE("hd_atlas DrawAtlasFrameClipScaled flip=true mirrors the frame horizontally")
{
	const int W = 128, H = 128;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("scaled_flip");

	// A horizontally-asymmetric 4x4 frame: left half red, right half blue, both opaque, so a
	// horizontal mirror swaps which side is which.
	const int pw = 8, ph = 8;
	std::vector<uint8_t> page(static_cast<size_t>(pw) * ph * 4, 0);
	for (int y = 0; y < ph; ++y) {
		for (int x = 0; x < pw; ++x) {
			uint8_t *p = page.data() + (static_cast<size_t>(y) * pw + x) * 4;
			if (x < pw / 2) {
				p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 255; // red on the left
			} else {
				p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255; // blue on the right
			}
		}
	}
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, page));

	const uint16_t sx = 0, sy = 0, sw = 8, sh = 8;
	const uint16_t cw = 8, ch = 8;
	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("flip_0", 0, sx, sy, sw, sh, 0, 0, cw, ch),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	const int frame = g.FrameByName("flip_0");
	REQUIRE(frame >= 0);

	const int dstX = 10, dstY = 10, dstW = 8, dstH = 8; // scale 1x, keep pixel math exact

	auto sampleAt = [&](int x, int y, uint8_t out[4]) {
		SDL_LockSurface(h.screen);
		const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
		const int pitch = h.screen->pitch;
		const uint8_t *p = sp + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
		std::memcpy(out, p, 4);
		SDL_UnlockSurface(h.screen);
	};

	// Unflipped: left quarter (near dstX) is red, right quarter (near dstX+dstW) is blue.
	SDL_SetRenderDrawColor(h.renderer, 0, 0, 0, 255);
	SDL_RenderClear(h.renderer);
	g.DrawAtlasFrameClipScaled(frame, dstX, dstY, dstW, dstH, /*flip*/false);
	SDL_RenderPresent(h.renderer);
	uint8_t left[4], right[4];
	sampleAt(dstX + 1, dstY + dstH / 2, left);
	sampleAt(dstX + dstW - 2, dstY + dstH / 2, right);
	CHECK(left[0] == 255); CHECK(left[2] == 0);   // left is red
	CHECK(right[0] == 0);  CHECK(right[2] == 255); // right is blue

	// Flipped: the colours swap sides.
	SDL_SetRenderDrawColor(h.renderer, 0, 0, 0, 255);
	SDL_RenderClear(h.renderer);
	g.DrawAtlasFrameClipScaled(frame, dstX, dstY, dstW, dstH, /*flip*/true);
	SDL_RenderPresent(h.renderer);
	uint8_t leftF[4], rightF[4];
	sampleAt(dstX + 1, dstY + dstH / 2, leftF);
	sampleAt(dstX + dstW - 2, dstY + dstH / 2, rightF);
	CHECK(leftF[0] == 0);   CHECK(leftF[2] == 255); // left is now blue
	CHECK(rightF[0] == 255); CHECK(rightF[2] == 0);  // right is now red

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

TEST_CASE("hd_atlas DrawAtlasFrameClipScaled bounds safety: invalid frame indices are no-ops")
{
	const int W = 64, H = 64;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}

	fs::path dir = caseDir("scaled_bounds");

	const int pw = 8, ph = 8;
	CHECK(writePagePng(dir / "atlas.p0.png", pw, ph, patternPixels(pw, ph)));

	std::vector<hdatlas::HdFrameRecord> recs = {
		makeRec("tile_0", 0, 0, 0, 4, 4, 0, 0, 4, 4),
	};
	writeBytes(dir / "atlas.hdatlas", buildHdAtlas(4096, {"atlas.p0.png"}, recs));

	CAtlasGraphic g;
	REQUIRE(g.Load((dir / "atlas.hdatlas").string()));
	REQUIRE(g.NumFrames() == 1);

	const uint8_t sr = 15, sg = 16, sb = 17, sa = 255;
	SDL_SetRenderDrawColor(h.renderer, sr, sg, sb, sa);
	SDL_RenderClear(h.renderer);

	// Negative index, exactly-out-of-range index, and far-out-of-range index: all no-ops.
	g.DrawAtlasFrameClipScaled(-1, 2, 2, 20, 20, false);
	g.DrawAtlasFrameClipScaled(1, 2, 2, 20, 20, false);
	g.DrawAtlasFrameClipScaled(9999, 2, 2, 20, 20, false);
	SDL_RenderPresent(h.renderer);

	SDL_LockSurface(h.screen);
	const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
	const int pitch = h.screen->pitch;
	bool allSentinel = true;
	for (int y = 0; y < H; ++y) {
		for (int x = 0; x < W; ++x) {
			const uint8_t *p = sp + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
			if (!(p[0] == sr && p[1] == sg && p[2] == sb && p[3] == sa)) {
				allSentinel = false;
			}
		}
	}
	CHECK(allSentinel);
	SDL_UnlockSurface(h.screen);

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

// ===========================================================================
// Classic view — palette colouring of an indexed page
// ===========================================================================

TEST_CASE("hd_atlas expandPalette6: value x 4, alpha 255, index 0 keyed only on request")
{
	// Synthetic palette: entry i = (i & 63, (i * 3) & 63, 63 - (i & 63)).
	std::vector<uint8_t> ppl(768);
	for (int i = 0; i < 256; ++i) {
		ppl[i * 3 + 0] = static_cast<uint8_t>(i & 63);
		ppl[i * 3 + 1] = static_cast<uint8_t>((i * 3) & 63);
		ppl[i * 3 + 2] = static_cast<uint8_t>(63 - (i & 63));
	}
	ppl[0] = 5; ppl[1] = 6; ppl[2] = 7; // index 0 carries a real colour

	uint8_t tile[256][4];
	hdatlas::expandPalette6(ppl.data(), false, tile);
	bool allTimesFour = true;
	for (int i = 0; i < 256; ++i) {
		for (int c = 0; c < 3; ++c) {
			if (tile[i][c] != ppl[i * 3 + c] * 4) {
				allTimesFour = false;
			}
		}
		if (tile[i][3] != 255) {
			allTimesFour = false;
		}
	}
	CHECK(allTimesFour);
	// Tiles: index 0 is a normal, opaque colour.
	CHECK(tile[0][0] == 20);
	CHECK(tile[0][1] == 24);
	CHECK(tile[0][2] == 28);
	CHECK(tile[0][3] == 255);
	// The brightest 6-bit value is 252, not 255.
	CHECK(tile[63][0] == 252);

	uint8_t sprite[256][4];
	hdatlas::expandPalette6(ppl.data(), true, sprite);
	CHECK(sprite[0][3] == 0);
	CHECK(sprite[0][0] == 0);
	CHECK(std::memcmp(sprite[1], tile[1], 4 * 255) == 0); // every other index is unchanged
}

TEST_CASE("hd_atlas LoadTexturePacker colours an indexed page by the given palette and keeps only the prefix")
{
	const int W = 64, H = 64;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}
	fs::path dir = caseDir("palette_page");

	// 16x8 indexed page: two 8x8 cells. Pixel index = 0..3 by position; the PNG's own palette
	// is all red, so a result coloured by it would be plainly wrong.
	const int pw = 16, ph = 8;
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 8, SDL_PIXELFORMAT_INDEX8);
	REQUIRE(s != nullptr);
	SDL_Color red[256];
	for (int i = 0; i < 256; ++i) {
		red[i] = SDL_Color{255, 0, 0, 255};
	}
	SDL_SetPaletteColors(s->format->palette, red, 0, 256);
	auto indexAt = [](int x, int y) -> uint8_t { return static_cast<uint8_t>((x + y * 3) & 3); };
	SDL_LockSurface(s);
	for (int y = 0; y < ph; ++y) {
		for (int x = 0; x < pw; ++x) {
			static_cast<uint8_t *>(s->pixels)[y * s->pitch + x] = indexAt(x, y);
		}
	}
	SDL_UnlockSurface(s);
	const int rc = IMG_SavePNG(s, (dir / "page.png").string().c_str());
	SDL_FreeSurface(s);
	REQUIRE(rc == 0);
	{
		// The writer may not keep the page indexed in every SDL_image build; then there is nothing to test.
		SDL_Surface *back = IMG_Load((dir / "page.png").string().c_str());
		REQUIRE(back != nullptr);
		const bool indexed = back->format->BytesPerPixel == 1;
		SDL_FreeSurface(back);
		if (!indexed) {
			MESSAGE("SDL_image did not write an indexed PNG - skipping the palette page test");
			h.shutdown();
			return;
		}
	}
	const std::string json =
	    "{\"frames\": {\n"
	    "\"Other/thing_0\": {\"frame\": {\"x\":0,\"y\":0,\"w\":8,\"h\":8}, \"rotated\": false,"
	    " \"spriteSourceSize\": {\"x\":0,\"y\":0,\"w\":8,\"h\":8}, \"sourceSize\": {\"w\":8,\"h\":8}},\n"
	    "\"Tiles/tile_0\": {\"frame\": {\"x\":8,\"y\":0,\"w\":8,\"h\":8}, \"rotated\": false,"
	    " \"spriteSourceSize\": {\"x\":0,\"y\":0,\"w\":8,\"h\":8}, \"sourceSize\": {\"w\":8,\"h\":8}}\n"
	    "}, \"meta\": {\"image\": \"page.png\"}}\n";
	writeBytes(dir / "page.json", std::vector<uint8_t>(json.begin(), json.end()));

	// 6-bit palette: index i -> (10 + i, 20 + i, 30 + i).
	std::vector<uint8_t> ppl(768, 0);
	for (int i = 0; i < 4; ++i) {
		ppl[i * 3 + 0] = static_cast<uint8_t>(10 + i);
		ppl[i * 3 + 1] = static_cast<uint8_t>(20 + i);
		ppl[i * 3 + 2] = static_cast<uint8_t>(30 + i);
	}

	CAtlasGraphic g;
	REQUIRE(g.LoadTexturePacker((dir / "page.json").string(), false, false, ppl.data(), true, "Tiles/tile_"));
	CHECK(g.NumFrames() == 1);
	CHECK(g.FrameByName("Other/thing_0") == -1);
	const int frame = g.FrameByName("Tiles/tile_0");
	REQUIRE(frame >= 0);

	SDL_SetRenderDrawColor(h.renderer, 1, 2, 3, 255);
	SDL_RenderClear(h.renderer);
	const int dstX = 10, dstY = 12;
	SDL_Rect dst = {dstX, dstY, 8, 8};
	g.DrawAtlasFrameFull(frame, dst);
	SDL_RenderPresent(h.renderer);

	SDL_LockSurface(h.screen);
	const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
	bool exact = true;
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			const int idx = indexAt(8 + x, y); // the second cell of the page
			const uint8_t *got = sp + static_cast<size_t>(dstY + y) * h.screen->pitch
			                     + static_cast<size_t>(dstX + x) * 4;
			// Index 0 included: opaque (10, 20, 30) x 4, not a hole.
			if (got[0] != (10 + idx) * 4 || got[1] != (20 + idx) * 4 || got[2] != (30 + idx) * 4
				|| got[3] != 255) {
				exact = false;
			}
		}
	}
	CHECK(exact);
	SDL_UnlockSurface(h.screen);

	// A page that is not indexed cannot be coloured by a palette: the load must fail, not guess.
	fs::path dir2 = caseDir("palette_page_rgba");
	CHECK(writePagePng(dir2 / "page.png", pw, ph, patternPixels(pw, ph)));
	writeBytes(dir2 / "page.json", std::vector<uint8_t>(json.begin(), json.end()));
	CAtlasGraphic g2;
	CHECK_FALSE(g2.LoadTexturePacker((dir2 / "page.json").string(), false, false, ppl.data(), true, "Tiles/tile_"));
	CHECK_FALSE(g2.IsLoaded());

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

// ===========================================================================
// Classic view — fog masks cut from the same indexed page
// ===========================================================================

TEST_CASE("hd_atlas classicFogDither: black where column + row inside the cell is even")
{
	int kept = 0;
	bool alternates = true;
	for (int y = 0; y < 32; ++y) {
		for (int x = 0; x < 32; ++x) {
			kept += hdatlas::classicFogDither(x, y);
			if (x > 0 && hdatlas::classicFogDither(x, y) == hdatlas::classicFogDither(x - 1, y)) {
				alternates = false;
			}
			if (y > 0 && hdatlas::classicFogDither(x, y) == hdatlas::classicFogDither(x, y - 1)) {
				alternates = false;
			}
		}
	}
	CHECK(kept == 512); // half of a 32x32 cell
	CHECK(alternates);
	CHECK(hdatlas::classicFogDither(0, 0));
	CHECK_FALSE(hdatlas::classicFogDither(1, 0));
	// An even cell size keeps the pattern unbroken from one cell to the next.
	CHECK(hdatlas::classicFogDither(31, 5) != hdatlas::classicFogDither(0, 5));
}

TEST_CASE("hd_atlas LoadTexturePacker cuts fog masks keyed, with dithered copies and two full cells")
{
	const int W = 64, H = 64;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}
	fs::path dir = caseDir("palette_fog_masks");

	// 16x8 indexed page: a tile cell (indices 1..3) and a mask cell (index 2 on its left half and on
	// one lone pixel, index 0 elsewhere).
	const int pw = 16, ph = 8;
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 8, SDL_PIXELFORMAT_INDEX8);
	REQUIRE(s != nullptr);
	SDL_Color red[256];
	for (int i = 0; i < 256; ++i) {
		red[i] = SDL_Color{255, 0, 0, 255};
	}
	SDL_SetPaletteColors(s->format->palette, red, 0, 256);
	auto maskAt = [](int x, int y) -> bool { return x < 4 || (x == 6 && y == 2); };
	SDL_LockSurface(s);
	for (int y = 0; y < ph; ++y) {
		for (int x = 0; x < pw; ++x) {
			static_cast<uint8_t *>(s->pixels)[y * s->pitch + x] =
			    x < 8 ? static_cast<uint8_t>(1 + (x + y) % 3) : static_cast<uint8_t>(maskAt(x - 8, y) ? 2 : 0);
		}
	}
	SDL_UnlockSurface(s);
	const int rc = IMG_SavePNG(s, (dir / "page.png").string().c_str());
	SDL_FreeSurface(s);
	REQUIRE(rc == 0);
	{
		SDL_Surface *back = IMG_Load((dir / "page.png").string().c_str());
		REQUIRE(back != nullptr);
		const bool indexed = back->format->BytesPerPixel == 1;
		SDL_FreeSurface(back);
		if (!indexed) {
			MESSAGE("SDL_image did not write an indexed PNG - skipping the fog mask test");
			h.shutdown();
			return;
		}
	}
	const std::string json =
	    "{\"frames\": {\n"
	    "\"Tiles/tile_0\": {\"frame\": {\"x\":0,\"y\":0,\"w\":8,\"h\":8}, \"rotated\": false,"
	    " \"spriteSourceSize\": {\"x\":0,\"y\":0,\"w\":8,\"h\":8}, \"sourceSize\": {\"w\":8,\"h\":8}},\n"
	    "\"Fog/mask_1\": {\"frame\": {\"x\":8,\"y\":0,\"w\":8,\"h\":8}, \"rotated\": false,"
	    " \"spriteSourceSize\": {\"x\":0,\"y\":0,\"w\":8,\"h\":8}, \"sourceSize\": {\"w\":8,\"h\":8}}\n"
	    "}, \"meta\": {\"image\": \"page.png\"}}\n";
	writeBytes(dir / "page.json", std::vector<uint8_t>(json.begin(), json.end()));

	// 6-bit palette: index i -> (10 + i, 20 + i, 30 + i); index 0 carries a colour too.
	std::vector<uint8_t> ppl(768, 0);
	for (int i = 0; i < 4; ++i) {
		ppl[i * 3 + 0] = static_cast<uint8_t>(10 + i);
		ppl[i * 3 + 1] = static_cast<uint8_t>(20 + i);
		ppl[i * 3 + 2] = static_cast<uint8_t>(30 + i);
	}
	const uint8_t ink[3] = {12 * 4, 22 * 4, 32 * 4}; // index 2
	const uint8_t bg[3] = {1, 2, 3};

	// Without a mask prefix the mask frame is not kept (the S0 behaviour).
	{
		CAtlasGraphic plain;
		REQUIRE(plain.LoadTexturePacker((dir / "page.json").string(), false, false, ppl.data(), true, "Tiles/tile_"));
		CHECK(plain.NumFrames() == 1);
		CHECK(plain.FrameByName("Fog/mask_1") == -1);
	}

	CAtlasGraphic g;
	REQUIRE(g.LoadTexturePacker((dir / "page.json").string(), false, false, ppl.data(), true,
	                            "Tiles/tile_", "Fog/mask_"));
	CHECK(g.NumFrames() == 5);
	const int tile = g.FrameByName("Tiles/tile_0");
	const int mask = g.FrameByName("Fog/mask_1");
	const int maskDither = g.FrameByName("Fog/mask_1_dither");
	const int solid = g.FrameByName("Fog/mask_solid");
	const int solidDither = g.FrameByName("Fog/mask_solid_dither");
	REQUIRE(tile >= 0);
	REQUIRE(mask >= 0);
	REQUIRE(maskDither >= 0);
	REQUIRE(solid >= 0);
	REQUIRE(solidDither >= 0);

	// Draw one frame over the background and say, per pixel, whether it must be the masks' colour.
	auto drawnAs = [&](int frame, uint8_t alpha, const std::function<bool(int, int)> &inked) -> bool {
		SDL_SetRenderDrawColor(h.renderer, bg[0], bg[1], bg[2], 255);
		SDL_RenderClear(h.renderer);
		SDL_Rect dst = {10, 12, 8, 8};
		g.DrawAtlasFrameFullAlpha(frame, dst, alpha);
		SDL_RenderPresent(h.renderer);
		SDL_LockSurface(h.screen);
		const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
		bool exact = true;
		for (int y = 0; y < 8; ++y) {
			for (int x = 0; x < 8; ++x) {
				const uint8_t *got = sp + static_cast<size_t>(12 + y) * h.screen->pitch
				                     + static_cast<size_t>(10 + x) * 4;
				const uint8_t *want = inked(x, y) ? ink : bg;
				if (got[0] != want[0] || got[1] != want[1] || got[2] != want[2]) {
					exact = false;
				}
			}
		}
		SDL_UnlockSurface(h.screen);
		return exact;
	};
	// The mask: its own pixels, opaque; index 0 leaves the ground as it is.
	CHECK(drawnAs(mask, 255, [&](int x, int y) { return maskAt(x, y); }));
	// The dithered mask: the mask's pixels on the checkerboard only.
	CHECK(drawnAs(maskDither, 255, [&](int x, int y) { return maskAt(x, y) && hdatlas::classicFogDither(x, y); }));
	// The full cells: all of it, and the checkerboard alone.
	CHECK(drawnAs(solid, 255, [](int, int) { return true; }));
	CHECK(drawnAs(solidDither, 255, [](int x, int y) { return hdatlas::classicFogDither(x, y); }));

	// The tile beside the masks is still opaque and coloured by the palette.
	{
		SDL_SetRenderDrawColor(h.renderer, bg[0], bg[1], bg[2], 255);
		SDL_RenderClear(h.renderer);
		SDL_Rect dst = {10, 12, 8, 8};
		g.DrawAtlasFrameFull(tile, dst);
		SDL_RenderPresent(h.renderer);
		SDL_LockSurface(h.screen);
		const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
		bool exact = true;
		for (int y = 0; y < 8; ++y) {
			for (int x = 0; x < 8; ++x) {
				const int idx = 1 + (x + y) % 3;
				const uint8_t *got = sp + static_cast<size_t>(12 + y) * h.screen->pitch
				                     + static_cast<size_t>(10 + x) * 4;
				if (got[0] != (10 + idx) * 4 || got[1] != (20 + idx) * 4 || got[2] != (30 + idx) * 4) {
					exact = false;
				}
			}
		}
		SDL_UnlockSurface(h.screen);
		CHECK(exact);
	}

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

TEST_CASE("hd_atlas classic sprite rules: frame and mirror, palette run, group names")
{
	// Engine frame -> classic frame: negative = (-f - 1), mirrored.
	bool mirror = true;
	CHECK(hdatlas::classicFrame(0, mirror) == 0);
	CHECK_FALSE(mirror);
	CHECK(hdatlas::classicFrame(12, mirror) == 12);
	CHECK_FALSE(mirror);
	CHECK(hdatlas::classicFrame(-1, mirror) == 0);
	CHECK(mirror);
	CHECK(hdatlas::classicFrame(-3, mirror) == 2); // west = the east column, mirrored
	CHECK(mirror);
	CHECK(hdatlas::classicFrame(-14, mirror) == 13);
	CHECK(mirror);

	// Player colour name -> first index of its four-entry run.
	CHECK(hdatlas::classicRampStart("red") == 208);
	CHECK(hdatlas::classicRampStart("blue") == 212);
	CHECK(hdatlas::classicRampStart("green") == 216);
	CHECK(hdatlas::classicRampStart("violet") == 220);
	CHECK(hdatlas::classicRampStart("orange") == 224);
	CHECK(hdatlas::classicRampStart("black") == 228);
	CHECK(hdatlas::classicRampStart("white") == 232);
	CHECK(hdatlas::classicRampStart("yellow") == 12);
	CHECK(hdatlas::classicRampStart("re") == -1);
	CHECK(hdatlas::classicRampStart("reds") == -1);
	CHECK(hdatlas::classicRampStart("") == -1);
	CHECK(hdatlas::classicRampStart(nullptr) == -1);

	// The run replaces entries 208..211 only.
	uint8_t rgba[256][4];
	for (int i = 0; i < 256; ++i) {
		rgba[i][0] = static_cast<uint8_t>(i);
		rgba[i][1] = static_cast<uint8_t>(255 - i);
		rgba[i][2] = 7;
		rgba[i][3] = 255;
	}
	hdatlas::classicApplyRamp(rgba, 12);
	for (int i = 0; i < 256; ++i) {
		const int from = (i >= 208 && i < 212) ? 12 + (i - 208) : i;
		CHECK(rgba[i][0] == from);
		CHECK(rgba[i][1] == 255 - from);
	}
	hdatlas::classicApplyRamp(rgba, 208); // the art's own run: nothing moves
	CHECK(rgba[208][0] == 12);
	hdatlas::classicApplyRamp(rgba, 254); // would run past the palette: refused
	CHECK(rgba[208][0] == 12);

	// "<group>/indexed_<N>".
	std::string group;
	int n = -1;
	CHECK(hdatlas::classicSplitGroupFrame("Orc/peon.grp/indexed_2", "/indexed_", group, n));
	CHECK(group == "Orc/peon.grp");
	CHECK(n == 2);
	CHECK(hdatlas::classicSplitGroupFrame("Other/death.grp/indexed_34", "/indexed_", group, n));
	CHECK(n == 34);
	CHECK_FALSE(hdatlas::classicSplitGroupFrame("Forest/forest_12", "/indexed_", group, n));
	CHECK_FALSE(hdatlas::classicSplitGroupFrame("Orc/peon.grp/indexed_", "/indexed_", group, n));
	CHECK_FALSE(hdatlas::classicSplitGroupFrame("Orc/peon.grp/indexed_2x", "/indexed_", group, n));
	CHECK_FALSE(hdatlas::classicSplitGroupFrame("/indexed_2", "/indexed_", group, n));

	// HD map entry -> group names to try, in order.
	const std::vector<std::string> orc = hdatlas::classicGroupNames("units_orc_common", "peon");
	REQUIRE(orc.size() == 4);
	CHECK(orc[0] == "Orc/peon.grp");
	CHECK(orc[1] == "Other/peon.grp");
	CHECK(orc[2] == "Monster/peon.grp");
	CHECK(orc[3] == "Human/peon.grp");
	const std::vector<std::string> human = hdatlas::classicGroupNames("units_human_winter", "s_farm");
	REQUIRE(human.size() == 4);
	CHECK(human[0] == "Human/s_farm.grp");
	CHECK(human[3] == "Orc/s_farm.grp");
	const std::vector<std::string> other = hdatlas::classicGroupNames("units_common_forest", "vcircle_0");
	REQUIRE(other.size() == 4);
	CHECK(other[0] == "Other/vcircle.grp"); // a trailing "_<digits>" is not part of the base name
	CHECK(other[1] == "Monster/vcircle.grp");
	CHECK(hdatlas::classicGroupNames("units_common_forest", "x_inv")[0] == "Other/x_inv.grp");
	CHECK(hdatlas::classicGroupNames("units_common_forest", "").empty());
}

TEST_CASE("hd_atlas classicLandSiteGroup: the scaffold group by construction file and tileset")
{
	const std::string summer = "neutral/buildings/land_construction_site.png";
	const std::string winter = "tilesets/winter/neutral/buildings/land_construction_site.png";

	CHECK(std::string(hdatlas::classicLandSiteGroup(summer, "forest")) == "Other/build_1.grp");
	CHECK(std::string(hdatlas::classicLandSiteGroup(winter, "winter")) == "Other/s_build1.grp");
	// The tileset decides, not the folder of the file; the name is compared without regard to case.
	CHECK(std::string(hdatlas::classicLandSiteGroup(summer, "winter")) == "Other/s_build1.grp");
	CHECK(std::string(hdatlas::classicLandSiteGroup("Neutral\\Buildings\\Land_Construction_Site.PNG", "forest"))
	      == "Other/build_1.grp");
	CHECK(std::string(hdatlas::classicLandSiteGroup("land_construction_site.png", "forest")) == "Other/build_1.grp");

	// Tilesets never seen in the original's classic view, and an unknown tileset: no answer.
	CHECK(hdatlas::classicLandSiteGroup(summer, "swamp") == nullptr);
	CHECK(hdatlas::classicLandSiteGroup(summer, "xswamp") == nullptr);
	CHECK(hdatlas::classicLandSiteGroup(summer, "") == nullptr);

	// Constructions with art of their own, and near misses of the name: no answer.
	CHECK(hdatlas::classicLandSiteGroup("human/buildings/shipyard_construction_site.png", "forest") == nullptr);
	CHECK(hdatlas::classicLandSiteGroup("tilesets/summer/neutral/buildings/wall_construction_site.png", "forest")
	      == nullptr);
	CHECK(hdatlas::classicLandSiteGroup("neutral/buildings/island_construction_site.png", "forest") == nullptr);
	CHECK(hdatlas::classicLandSiteGroup("neutral/buildings/land_construction_site.png.bak", "forest") == nullptr);
	CHECK(hdatlas::classicLandSiteGroup("", "forest") == nullptr);

	// The two scaffold stages are frames 0 and 1 as stored; a third frame does not exist.
	bool mirror = true;
	CHECK(hdatlas::classicFrame(0, mirror) < hdatlas::ClassicLandSiteFrames);
	CHECK(hdatlas::classicFrame(1, mirror) < hdatlas::ClassicLandSiteFrames);
	CHECK_FALSE(hdatlas::classicFrame(2, mirror) < hdatlas::ClassicLandSiteFrames);
}

TEST_CASE("hd_atlas classicMinimapSlot: the mapColors sample of a minimap pixel")
{
	auto slots = [](int tiles, int pixels, int count) {
		std::vector<int> out;
		for (int p = 0; p < count; ++p) {
			out.push_back(hdatlas::classicMinimapSlot(p, tiles, pixels));
		}
		return out;
	};
	// Measured on the original, 128-pixel minimap: 32 wide = 4 pixels a tile, all four samples.
	CHECK(slots(32, 128, 9) == std::vector<int>{0, 1, 2, 3, 0, 1, 2, 3, 0});
	// 64 wide = 2 pixels a tile: samples 0 and 1, not 0 and 2.
	CHECK(slots(64, 128, 6) == std::vector<int>{0, 1, 0, 1, 0, 1});
	// 96 wide = 4 pixels for 3 tiles: the first tile of three gets two pixels.
	CHECK(slots(96, 128, 9) == std::vector<int>{0, 1, 0, 0, 0, 1, 0, 0, 0});
	// 128 wide = 1 pixel a tile: sample 0 (derived from the rule, not measured).
	CHECK(slots(128, 128, 5) == std::vector<int>{0, 0, 0, 0, 0});
	for (int p = 0; p < 128; ++p) {
		CHECK(hdatlas::classicMinimapSlot(p, 32, 128) == p % 4);
		CHECK(hdatlas::classicMinimapSlot(p, 64, 128) == p % 2);
		CHECK(hdatlas::classicMinimapSlot(p, 96, 128) == (p % 4 == 1 ? 1 : 0));
	}
	// A larger minimap (not measured): each tile keeps the samples it shows at 128 pixels, stretched.
	CHECK(slots(32, 256, 9) == std::vector<int>{0, 0, 1, 1, 2, 2, 3, 3, 0});
	CHECK(slots(32, 576, 19) == std::vector<int>{0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 0});
	CHECK(slots(64, 576, 10) == std::vector<int>{0, 0, 0, 0, 0, 1, 1, 1, 1, 0});
	CHECK(slots(96, 576, 19) == std::vector<int>{0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	CHECK(slots(96, 576, 22)[21] == 1);
	for (int p = 0; p < 576; ++p) {
		CHECK(hdatlas::classicMinimapSlot(p, 128, 576) == 0);
		const int s = hdatlas::classicMinimapSlot(p, 32, 300);
		CHECK(s >= 0);
		CHECK(s <= 3);
	}
	CHECK(hdatlas::classicMinimapSlot(-1, 32, 128) == 0);
	CHECK(hdatlas::classicMinimapSlot(5, 0, 128) == 0);
	CHECK(hdatlas::classicMinimapSlot(5, 32, 0) == 0);
}

TEST_CASE("hd_atlas ClassicGroup cuts a sprite group keyed, with the player's palette run, nearest")
{
	const int W = 64, H = 64;
	SdlHarness h;
	if (!h.init(W, H)) {
		MESSAGE("SDL renderer unavailable - skipping atlas render tests");
		return;
	}
	fs::path dir = caseDir("palette_sprite_groups");

	// 32x8 indexed page: a tile cell, two cells of a group with team pixels, one cell of a group
	// without. Sprite cell: column 0 = index 208, column 1 = 209, column 2 (upper half) = 5, rest 0.
	const int pw = 32, ph = 8;
	auto unitAt = [](int x, int y) -> uint8_t {
		return static_cast<uint8_t>(x == 0 ? 208 : x == 1 ? 209 : (x == 2 && y < 4) ? 5 : 0);
	};
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 8, SDL_PIXELFORMAT_INDEX8);
	REQUIRE(s != nullptr);
	SDL_Color red[256];
	for (int i = 0; i < 256; ++i) {
		red[i] = SDL_Color{255, 0, 0, 255};
	}
	SDL_SetPaletteColors(s->format->palette, red, 0, 256);
	SDL_LockSurface(s);
	for (int y = 0; y < ph; ++y) {
		for (int x = 0; x < pw; ++x) {
			uint8_t v;
			if (x < 8) {
				v = 3;
			} else if (x < 16) {
				v = unitAt(x - 8, y);
			} else if (x < 24) {
				v = static_cast<uint8_t>(x - 16 < 3 ? 6 : 0);
			} else {
				v = static_cast<uint8_t>(x - 24 < 2 ? 5 : 0);
			}
			static_cast<uint8_t *>(s->pixels)[y * s->pitch + x] = v;
		}
	}
	SDL_UnlockSurface(s);
	const int rc = IMG_SavePNG(s, (dir / "page.png").string().c_str());
	SDL_FreeSurface(s);
	REQUIRE(rc == 0);
	{
		SDL_Surface *back = IMG_Load((dir / "page.png").string().c_str());
		REQUIRE(back != nullptr);
		const bool indexed = back->format->BytesPerPixel == 1;
		SDL_FreeSurface(back);
		if (!indexed) {
			MESSAGE("SDL_image did not write an indexed PNG - skipping the sprite group test");
			h.shutdown();
			return;
		}
	}
	auto cell = [](const char *name, int x) {
		return std::string("\"") + name + "\": {\"frame\": {\"x\":" + std::to_string(x)
		       + ",\"y\":0,\"w\":8,\"h\":8}, \"rotated\": false, \"trimmed\": false,"
		         " \"spriteSourceSize\": {\"x\":0,\"y\":0,\"w\":8,\"h\":8}, \"sourceSize\": {\"w\":8,\"h\":8}}";
	};
	const std::string json = "{\"frames\": {\n" + cell("Tiles/tile_0", 0) + ",\n"
	                         + cell("Orc/unit.grp/indexed_0", 8) + ",\n"
	                         + cell("Orc/unit.grp/indexed_1", 16) + ",\n"
	                         + cell("Other/plain.grp/indexed_0", 24) + "\n"
	                         + "}, \"meta\": {\"image\": \"page.png\"}}\n";
	writeBytes(dir / "page.json", std::vector<uint8_t>(json.begin(), json.end()));

	// 6-bit palette, every entry its own colour.
	std::vector<uint8_t> ppl(768, 0);
	for (int i = 0; i < 256; ++i) {
		ppl[i * 3 + 0] = static_cast<uint8_t>(i % 64);
		ppl[i * 3 + 1] = static_cast<uint8_t>((i * 3 + 1) % 64);
		ppl[i * 3 + 2] = static_cast<uint8_t>((i * 7 + 2) % 64);
	}
	const uint8_t bg[3] = {1, 2, 3};
	auto colourOf = [&](int index, uint8_t out[3]) {
		for (int c = 0; c < 3; ++c) {
			out[c] = static_cast<uint8_t>(ppl[index * 3 + c] * 4);
		}
	};

	// Without a group mark nothing is listed and no page is kept (the behaviour before).
	{
		CAtlasGraphic plain;
		REQUIRE(plain.LoadTexturePacker((dir / "page.json").string(), false, false, ppl.data(), true, "Tiles/tile_"));
		CHECK(plain.NumFrames() == 1);
		CHECK(plain.ClassicSerial() == 0);
		CHECK(plain.ClassicGroupByName("Orc/unit.grp") == -1);
		CHECK(plain.ClassicGroup(0, 208) == nullptr);
	}

	CAtlasGraphic g;
	REQUIRE(g.LoadTexturePacker((dir / "page.json").string(), false, false, ppl.data(), true,
	                            "Tiles/tile_", nullptr, "/indexed_"));
	CHECK(g.NumFrames() == 1); // the sprite cells are not cut at load
	CHECK(g.ClassicSerial() != 0);
	const int unit = g.ClassicGroupByName("orc/UNIT.grp"); // without regard to case
	const int plainGroup = g.ClassicGroupByName("Other/plain.grp");
	REQUIRE(unit >= 0);
	REQUIRE(plainGroup >= 0);
	CHECK(g.ClassicGroupByName("Orc/none.grp") == -1);
	CHECK(g.ClassicGroup(99, 208) == nullptr);
	CHECK(g.ClassicGroup(unit, -1) == nullptr);

	const CAtlasGraphic *black = g.ClassicGroup(unit, 228);
	REQUIRE(black != nullptr);
	CHECK(black->NumFrames() == 2);
	CHECK(g.ClassicGroup(unit, 228) == black);          // cut once
	CHECK(g.ClassicGroup(unit, 208) != black);          // another run, another cut
	// A group without team pixels is one cut for every run.
	CHECK(g.ClassicGroup(plainGroup, 228) == g.ClassicGroup(plainGroup, 212));

	// Draw frame 0 centred on (20, 20) at `scale` screen pixels per classic pixel and compare
	// every screen pixel of the cell with the expected index (0 = the background shows).
	auto drawnAs = [&](const CAtlasGraphic *cells, int scale, bool flip, int run) -> bool {
		SDL_SetRenderDrawColor(h.renderer, bg[0], bg[1], bg[2], 255);
		SDL_RenderClear(h.renderer);
		cells->DrawAtlasFrameCentred(0, 20.0f, 20.0f, static_cast<float>(scale), flip);
		SDL_RenderPresent(h.renderer);
		SDL_LockSurface(h.screen);
		const uint8_t *sp = static_cast<const uint8_t *>(h.screen->pixels);
		const int x0 = 20 - 4 * scale, y0 = 20 - 4 * scale;
		bool exact = true;
		for (int y = 0; y < 8 * scale; ++y) {
			for (int x = 0; x < 8 * scale; ++x) {
				const int cx = flip ? 7 - x / scale : x / scale;
				int index = unitAt(cx, y / scale);
				if (index >= 208 && index < 212) {
					index = run + (index - 208);
				}
				uint8_t want[3] = {bg[0], bg[1], bg[2]};
				if (index) {
					colourOf(index, want);
				}
				const uint8_t *got = sp + static_cast<size_t>(y0 + y) * h.screen->pitch
				                     + static_cast<size_t>(x0 + x) * 4;
				if (got[0] != want[0] || got[1] != want[1] || got[2] != want[2]) {
					exact = false;
				}
			}
		}
		SDL_UnlockSurface(h.screen);
		return exact;
	};
	CHECK(drawnAs(black, 1, false, 228));                     // as stored, black run
	CHECK(drawnAs(black, 1, true, 228));                      // mirrored inside its cell
	CHECK(drawnAs(black, 3, false, 228));                     // whole blocks, no blended pixel
	CHECK(drawnAs(black, 3, true, 228));
	CHECK(drawnAs(g.ClassicGroup(unit, 208), 1, false, 208)); // the art's own run
	CHECK(drawnAs(g.ClassicGroup(unit, 12), 1, false, 12));   // the run that is not beside the others
	CHECK_FALSE(drawnAs(black, 1, false, 208));               // the check does tell runs apart

	// Another page gets another serial, so a remembered group number is not reused for it.
	{
		CAtlasGraphic second;
		REQUIRE(second.LoadTexturePacker((dir / "page.json").string(), false, false, ppl.data(), true,
		                                 "Tiles/tile_", nullptr, "/indexed_"));
		CHECK(second.ClassicSerial() != 0);
		CHECK(second.ClassicSerial() != g.ClassicSerial());
	}

	g.~CAtlasGraphic();
	new (&g) CAtlasGraphic();
	h.shutdown();
}

TEST_CASE("hd_atlas stored canvas: a reduced family is stretched to its rule canvas, others are not")
{
	// 64 px a tile, zoom 1: the sheet density scale.
	const float s = 64.0f / 216.0f;
	// No "canvas" key: the very same value, whatever the frame's canvas.
	CHECK(hdatlas::hdStoredCanvasScale(s, 0, 608) == s);
	CHECK(hdatlas::hdStoredCanvasScale(s, 0, 225) == s);
	CHECK(hdatlas::hdStoredCanvasScale(s, -1, 225) == s);
	// A frame record without a canvas does not divide by zero.
	CHECK(hdatlas::hdStoredCanvasScale(s, 500, 0) == s);
	// Skeleton: body and mask stored at 225, rule canvas 500.
	CHECK(hdatlas::hdStoredCanvasScale(s, 500, 225) == doctest::Approx(s * 2.2222).epsilon(0.0001));
	CHECK(hdatlas::hdStoredCanvasScale(s, 500, 225) * 225 == doctest::Approx(s * 500));
	// Eye of Kilrogg: body stored at 186, mask at the rule canvas 338 -- the mask is not stretched.
	CHECK(hdatlas::hdStoredCanvasScale(s, 338, 186) == doctest::Approx(s * 1.8172).epsilon(0.0001));
	CHECK(hdatlas::hdStoredCanvasScale(s, 338, 338) == doctest::Approx(s));
	// A family already at its rule canvas that carried the key would be drawn as before.
	CHECK(hdatlas::hdStoredCanvasScale(1.5f, 608, 608) == doctest::Approx(1.5f));
}

TEST_CASE("hd_atlas flyer shadow: the Remastered square from the unit's tile")
{
	// 64 px a tile: the blot's centre is +1, +87 from the tile centre (32, 32), side 92.
	hdatlas::FlyerShadowRect r = hdatlas::hdFlyerShadowRect(1, 1, 64, 64, 0, 0, 0, 1.0f);
	CHECK(r.size == 92);
	CHECK(r.x == -13);
	CHECK(r.y == 73);
	CHECK(2 * r.x + r.size == 2 * (32 + 1));
	CHECK(2 * r.y + r.size == 2 * (32 + 87));
	// 32 px a tile: whole pixels of +0.5, +43.5.
	r = hdatlas::hdFlyerShadowRect(1, 1, 32, 32, 0, 0, 0, 1.0f);
	CHECK(r.size == 46);
	CHECK(r.x == -7);
	CHECK(r.y == 37);
	// Zoom 1.5 scales the place and the side alike.
	r = hdatlas::hdFlyerShadowRect(1, 1, 64, 64, 0, 0, 0, 1.5f);
	CHECK(r.size == 138);
	CHECK(r.x == -20);
	CHECK(r.y == 110);
	r = hdatlas::hdFlyerShadowRect(1, 1, 32, 32, 0, 0, 0, 1.5f);
	CHECK(r.size == 69);
	CHECK(r.x == -10);
	CHECK(r.y == 55);
	// Live run 58 (96 px a tile = 32 px a tile at zoom 3): the blot's centre 131 px below the tile
	// centre (measured +126.8 .. +133.4, case means +130.0 .. +131.3) and 1 px right (-1.3 .. +1.6).
	r = hdatlas::hdFlyerShadowRect(1, 1, 32, 32, 0, 0, 0, 3.0f);
	CHECK(r.size == 138);
	CHECK(r.x + r.size / 2 - 48 == 1);
	CHECK(r.y + r.size / 2 - 48 == 131);
	// The type's draw offset moves it; the hover height keeps its sign and factor.
	r = hdatlas::hdFlyerShadowRect(1, 1, 64, 64, 3, -5, 0, 1.0f);
	CHECK(r.x == -10);
	CHECK(r.y == 68);
	CHECK(hdatlas::hdFlyerShadowRect(1, 1, 64, 64, 0, 0, 1, 1.0f).y == 71);
	CHECK(hdatlas::hdFlyerShadowRect(1, 1, 64, 64, 0, 0, -1, 1.0f).y == 75);
	CHECK(hdatlas::hdFlyerShadowRect(1, 1, 64, 64, 0, 0, 1, 1.0f).x == -13);
}

TEST_CASE("hd_atlas flyer shadow: the classic disc's cell from the unit's tile")
{
	CHECK(std::string(hdatlas::ClassicFlyerShadowGroup) == "Other/shadow.grp");
	CHECK(hdatlas::ClassicFlyerShadowFrame == 2);
	// 32 px a tile: the 32 x 32 cell is centred 0, +43 from the tile centre (16, 16), 1:1.
	hdatlas::ClassicFlyerShadowPlace p = hdatlas::classicFlyerShadowPlace(1, 1, 32, 32, 0, 0, 0, 1.0f);
	CHECK(p.cx == 16.0f);
	CHECK(p.cy == 59.0f);
	CHECK(p.scale == 1.0f);
	// 64 px a tile: two screen pixels a classic pixel.
	p = hdatlas::classicFlyerShadowPlace(1, 1, 64, 64, 0, 0, 0, 1.0f);
	CHECK(p.cx == 32.0f);
	CHECK(p.cy == 118.0f);
	CHECK(p.scale == 2.0f);
	p = hdatlas::classicFlyerShadowPlace(1, 1, 64, 64, 0, 0, 0, 1.5f);
	CHECK(p.cx == 48.0f);
	CHECK(p.cy == 177.0f);
	CHECK(p.scale == 3.0f);
	p = hdatlas::classicFlyerShadowPlace(1, 1, 32, 32, 0, 0, 0, 1.5f);
	CHECK(p.cx == 24.0f);
	CHECK(p.cy == 88.5f);
	CHECK(p.scale == 1.5f);
	// Same hover term as the Remastered square.
	CHECK(hdatlas::classicFlyerShadowPlace(1, 1, 64, 64, 0, 0, 1, 1.0f).cy == 116.0f);
	CHECK(hdatlas::classicFlyerShadowPlace(1, 1, 64, 64, 2, 4, 0, 1.0f).cx == 34.0f);
	CHECK(hdatlas::classicFlyerShadowPlace(1, 1, 64, 64, 2, 4, 0, 1.0f).cy == 122.0f);
}

TEST_CASE("hd_atlas classicStep: one more screen pixel a classic pixel every 480 rows of the screen")
{
	// The original: 959 -> 1, 960 -> 2 (at three widths), 1439 -> 2, 1440 -> 3, 1746 -> 3,
	// 1919 -> 3, 1920 -> 4.
	const int heights[] = {479, 480, 720, 959, 960, 1439, 1440, 1746, 1752, 1919, 1920};
	const int steps[] = {1, 1, 1, 1, 2, 2, 3, 3, 3, 3, 4};
	for (int i = 0; i < 11; ++i) {
		CAPTURE(heights[i]);
		CHECK(hdatlas::classicStep(heights[i]) == steps[i]);
	}
	CHECK(hdatlas::classicStep(2400) == 5);
	CHECK(hdatlas::classicStep(0) == 1);
	// The height of the screen, not of the box: 2400 x 1920 has a box 1800 high and 4,
	// 1100 x 960 a box 825 high and 2.
	CHECK(hdatlas::classicBox(2400, 1920).h == 1800);
	CHECK(hdatlas::classicScreen(2400, 1920).s == 4);
	CHECK(hdatlas::classicScreen(2400, 1920).lw == 600);
	CHECK(hdatlas::classicScreen(2400, 1920).lh == 450);
	CHECK(hdatlas::classicBox(1100, 960).h == 825);
	CHECK(hdatlas::classicScreen(1100, 960).s == 2);
}

TEST_CASE("hd_atlas classic screen: the 4:3 box, the step, the logical screen and the rects on it")
{
	using hdatlas::ClassicRect;
	struct Screen {
		int w, h;
		ClassicRect box;
		int s, lw, lh;
		ClassicRect view, mini, menu;   // screen pixels
	};
	const Screen screens[] = {
		// the original's own size: box at client x 171, view at client 347,16, minimap at 195,26
		{1366, 768, {171, 0, 1024, 768}, 1, 1024, 768, {347, 16, 832, 736}, {195, 26, 128, 128}, {217, 2, 60, 20}},
		// o21_14: box x 316..2643, tile 96 px, left column 528 px, minimap inner 384 px
		{2960, 1746, {316, 0, 2328, 1746}, 3, 776, 582, {844, 48, 1752, 1650}, {388, 78, 384, 384}, {454, 6, 180, 60}},
		{2800, 1752, {232, 0, 2336, 1752}, 3, 778, 584, {760, 48, 1758, 1656}, {304, 78, 384, 384}, {370, 6, 180, 60}},
		{1600, 720, {320, 0, 960, 720}, 1, 960, 720, {496, 16, 768, 688}, {344, 26, 128, 128}, {366, 2, 60, 20}},
		// o11_11: map view 352,32 1056 x 1016 in the box
		{1920, 1080, {240, 0, 1440, 1080}, 2, 720, 540, {592, 32, 1056, 1016}, {288, 52, 256, 256}, {332, 4, 120, 40}},
	};
	const auto same = [](const ClassicRect &a, const ClassicRect &b) {
		return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
	};
	for (const Screen &s : screens) {
		CAPTURE(s.w);
		const hdatlas::ClassicScreen c = hdatlas::classicScreen(s.w, s.h);
		CHECK(same(c.box, s.box));
		CHECK(same(hdatlas::classicBox(s.w, s.h), s.box));
		CHECK(c.s == s.s);
		CHECK(c.lw == s.lw);
		CHECK(c.lh == s.lh);
		const ClassicRect lv = hdatlas::classicView(c);
		CHECK(lv.x == 176);
		CHECK(lv.y == 16);
		CHECK(lv.w == s.lw - 192);
		CHECK(lv.h == s.lh - 32);
		const ClassicRect view = hdatlas::classicToScreen(c, lv.x, lv.y, lv.w, lv.h);
		CHECK(same(view, s.view));
		CHECK(same(hdatlas::classicToScreen(c, hdatlas::ClassicMinimapX, hdatlas::ClassicMinimapY,
		                                    hdatlas::ClassicMinimapSize, hdatlas::ClassicMinimapSize), s.mini));
		CHECK(same(hdatlas::classicToScreen(c, hdatlas::ClassicMenuX, hdatlas::ClassicMenuY,
		                                    hdatlas::ClassicMenuW, hdatlas::ClassicMenuH), s.menu));
		// A tile is 32 logical pixels at every size: 32 x s on the screen.
		CHECK(hdatlas::classicToScreen(c, 0, 0, hdatlas::ClassicTilePx, hdatlas::ClassicTilePx).w == 32 * s.s);

		// screen -> logical: outside the logical screen nothing; the corners
		int lx = -1;
		int ly = -1;
		CHECK_FALSE(hdatlas::classicFromScreen(c, c.box.x - 1, 0, lx, ly));
		CHECK_FALSE(hdatlas::classicFromScreen(c, c.box.x + c.lw * c.s, 0, lx, ly));
		CHECK_FALSE(hdatlas::classicFromScreen(c, 0, s.h / 2, lx, ly));
		CHECK_FALSE(hdatlas::classicFromScreen(c, s.w - 1, s.h / 2, lx, ly));
		CHECK_FALSE(hdatlas::classicFromScreen(c, c.box.x, -1, lx, ly));
		CHECK_FALSE(hdatlas::classicFromScreen(c, c.box.x, s.h, lx, ly));
		CHECK(hdatlas::classicFromScreen(c, c.box.x, c.box.y, lx, ly));
		CHECK(lx == 0);
		CHECK(ly == 0);
		CHECK(hdatlas::classicFromScreen(c, c.box.x + c.lw * c.s - 1, c.box.y + c.lh * c.s - 1, lx, ly));
		CHECK(lx == c.lw - 1);
		CHECK(ly == c.lh - 1);
		// Every screen pixel of a logical pixel maps back to it: the first and the last pixel of
		// the first, a middle and the last whole tile of the view.
		for (const int tile : {0, lv.w / 64, lv.w / 32 - 1}) {
			const int cellX = lv.x + tile * 32;
			const ClassicRect r = hdatlas::classicToScreen(c, cellX, lv.y, 32, 32);
			CHECK(hdatlas::classicFromScreen(c, r.x, r.y, lx, ly));
			CHECK(lx == cellX);
			CHECK(ly == lv.y);
			CHECK(hdatlas::classicFromScreen(c, r.x + r.w - 1, r.y + r.h - 1, lx, ly));
			CHECK(lx == cellX + 31);
			CHECK(ly == lv.y + 31);
		}
		// Neighbours meet: the side column ends where the view begins, the view where the right strip begins.
		const ClassicRect side = hdatlas::classicToScreen(c, 0, 0, hdatlas::ClassicViewX, c.lh);
		const ClassicRect right = hdatlas::classicToScreen(c, c.lw - 16, 0, 16, c.lh);
		CHECK(side.x == c.box.x);
		CHECK(side.w == 176 * s.s);
		CHECK(side.x + side.w == view.x);
		CHECK(view.x + view.w == right.x);
		CHECK(right.x + right.w == c.box.x + c.lw * c.s);
		// The logical screen never leaves the box and misses fewer than s of its columns / rows.
		CHECK(c.box.w - c.lw * c.s >= 0);
		CHECK(c.box.w - c.lw * c.s < c.s);
		CHECK(c.box.h - c.lh * c.s >= 0);
		CHECK(c.box.h - c.lh * c.s < c.s);
	}

	// Tiles in view: 2960 x 1746 shows 18.25 x 17.19 tiles of 96 px, 1366 x 768 26 x 23 of 32 px,
	// 1920 x 1080 16.5 x 15.88 of 64 px, 1600 x 720 24 x 21.5 of 32 px.
	const auto tiles = [](int w, int h, bool wide) {
		const ClassicRect v = hdatlas::classicView(hdatlas::classicScreen(w, h));
		return (wide ? v.w : v.h) / (double) hdatlas::ClassicTilePx;
	};
	CHECK(tiles(2960, 1746, true) == doctest::Approx(18.25));
	CHECK(tiles(2960, 1746, false) == doctest::Approx(17.1875));
	CHECK(tiles(1366, 768, true) == doctest::Approx(26.0));
	CHECK(tiles(1366, 768, false) == doctest::Approx(23.0));
	CHECK(tiles(1920, 1080, true) == doctest::Approx(16.5));
	CHECK(tiles(1920, 1080, false) == doctest::Approx(15.875));
	CHECK(tiles(1600, 720, true) == doctest::Approx(24.0));
	CHECK(tiles(1600, 720, false) == doctest::Approx(21.5));
	CHECK(tiles(800, 480, true) == doctest::Approx(14.0));
	CHECK(tiles(2400, 1440, true) == doctest::Approx(14.0));

	// The original's other window sizes (bounding box of the picture): width-limited is centred vertically.
	CHECK(same(hdatlas::classicBox(1000, 800), ClassicRect{0, 25, 1000, 750}));
	CHECK(same(hdatlas::classicBox(1920, 1080), ClassicRect{240, 0, 1440, 1080}));
	CHECK(same(hdatlas::classicBox(800, 480), ClassicRect{80, 0, 640, 480}));
	CHECK(same(hdatlas::classicBox(2400, 1440), ClassicRect{240, 0, 1920, 1440}));
	CHECK(same(hdatlas::classicBox(0, 0), ClassicRect{0, 0, 0, 0}));
}

TEST_CASE("hd_atlas classicHudFramePieces: the frame covers the logical screen outside the map view once")
{
	hdatlas::ClassicHudPiece pieces[hdatlas::ClassicHudPieceMax];
	// 1024 x 768, where the layout was compared with the original pixel for pixel.
	int count = hdatlas::classicHudFramePieces(1024, 768, pieces);
	REQUIRE(count == 12);
	CHECK(std::string(pieces[0].frame) == "TopLeft");
	CHECK(pieces[0].h == 464);
	CHECK(std::string(pieces[1].frame) == "TileableLeft");
	CHECK(pieces[1].y == 464);
	CHECK(pieces[1].h == 176);
	CHECK(std::string(pieces[2].frame) == "TileableLeft");
	CHECK(pieces[2].y == 640);
	CHECK(pieces[2].h == 112);
	CHECK(std::string(pieces[3].frame) == "BottomLeft");
	CHECK(pieces[3].y == 752);
	// 640 x 480: the top piece and the foot fill the column, one tile each of the strips.
	count = hdatlas::classicHudFramePieces(640, 480, pieces);
	REQUIRE(count == 7);
	CHECK(pieces[0].h == 464);
	CHECK(std::string(pieces[1].frame) == "BottomLeft");
	CHECK(pieces[1].y == 464);
	// 776 x 582 (2960 x 1746): the column runs to the bottom, the strips sit on the edges.
	count = hdatlas::classicHudFramePieces(776, 582, pieces);
	REQUIRE(count == 11);
	CHECK(std::string(pieces[1].frame) == "TileableLeft");
	CHECK(pieces[1].h == 102);
	CHECK(pieces[2].y == 566);
	CHECK(std::string(pieces[count - 1].frame) == "BottomRight");
	CHECK(pieces[count - 1].x == 760);
	CHECK(pieces[count - 1].y == 566);
	CHECK(hdatlas::classicHudFramePieces(100, 100, pieces) == 0);

	const auto frameSize = [](const std::string &name, int &w, int &h) {
		w = name == "Top" || name == "Bottom" ? 448 : name == "Right" || name == "TopRight"
		    || name == "BottomRight" ? 16 : 176;
		h = name == "TopLeft" ? 464 : name == "TileableLeft" ? 176 : name == "Right" ? 448 : 16;
	};
	const int sizes[][2] = {{1024, 768}, {776, 582}, {778, 584}, {640, 480}, {960, 720}, {720, 540},
	                        {600, 450}, {480, 360}, {1279, 959}, {333, 250}};
	for (const auto &size : sizes) {
		const int lw = size[0];
		const int lh = size[1];
		CAPTURE(lw);
		count = hdatlas::classicHudFramePieces(lw, lh, pieces);
		REQUIRE(count > 0);
		REQUIRE(count <= hdatlas::ClassicHudPieceMax);
		std::vector<int> hits(lw * lh, 0);
		for (int i = 0; i < count; ++i) {
			const hdatlas::ClassicHudPiece &p = pieces[i];
			int fw = 0;
			int fh = 0;
			frameSize(p.frame, fw, fh);
			CHECK(p.w > 0);
			CHECK(p.h > 0);
			CHECK(p.w <= fw);
			CHECK(p.h <= fh);
			REQUIRE(p.x >= 0);
			REQUIRE(p.y >= 0);
			REQUIRE(p.x + p.w <= lw);
			REQUIRE(p.y + p.h <= lh);
			for (int y = p.y; y < p.y + p.h; ++y) {
				for (int x = p.x; x < p.x + p.w; ++x) {
					++hits[y * lw + x];
				}
			}
		}
		const hdatlas::ClassicRect v = hdatlas::classicView({{0, 0, lw, lh}, 1, lw, lh});
		int wrong = 0;
		for (int y = 0; y < lh; ++y) {
			for (int x = 0; x < lw; ++x) {
				const bool view = x >= v.x && x < v.x + v.w && y >= v.y && y < v.y + v.h;
				wrong += hits[y * lw + x] != (view ? 0 : 1);
			}
		}
		CHECK(wrong == 0);
	}
}

TEST_CASE("hd_atlas tilesBeforeViewEnd: a placement preview past the view's end has no tiles, never a negative count")
{
	using hdatlas::tilesBeforeViewEnd;
	// Wholly inside, cut by the end, on the end, past the end.
	CHECK(tilesBeforeViewEnd(10, 2, 7, 19) == 2);
	CHECK(tilesBeforeViewEnd(24, 2, 7, 19) == 2);
	CHECK(tilesBeforeViewEnd(25, 2, 7, 19) == 1);
	CHECK(tilesBeforeViewEnd(26, 2, 7, 19) == 0);
	// The reported state: a farm (2 x 2) on row 30 of a 32-row map, the view moved by a tap on
	// the minimap to rows 7..25 (classic view, 2960 x 1746: 17.19 tiles down). Was -4.
	CHECK(tilesBeforeViewEnd(30, 2, 7, 19) == 0);
	// Before the view's start the whole footprint is kept (the clip rect cuts the drawing).
	CHECK(tilesBeforeViewEnd(3, 4, 7, 19) == 4);
	CHECK(tilesBeforeViewEnd(0, 0, 0, 0) == 0);
	for (int first = -5; first < 40; ++first) {
		for (int count = 0; count <= 4; ++count) {
			const int n = tilesBeforeViewEnd(first, count, 7, 19);
			CHECK(n >= 0);
			CHECK(n <= count);
		}
	}
}

TEST_CASE("classichud: resource icons about the middle; the hit rect of a command cell at step 1 and 3")
{
	// Gold icon in the original: 250 of 640, 430 of 1000, 463 of 1067, 442 of 1024, 447 of 1035,
	// 263 of 667, 290 of 720, 363 of 867; pitch 75.
	const int widths[] = {640, 1000, 1067, 1024, 1035, 667, 720, 867};
	const int gold[] = {250, 430, 463, 442, 447, 263, 290, 363};
	for (int i = 0; i < 8; ++i) {
		CAPTURE(widths[i]);
		CHECK(classichud::resourceIconX(widths[i], 0) == gold[i]);
		CHECK(classichud::resourceIconX(widths[i], 3) == gold[i] + 225);
	}
	CHECK(classichud::resourceIconX(1024, 1) == 517);
	CHECK(classichud::resourceIconX(1024, 2) == 592);
	// 2960 x 1746: logical 776, gold at 318 = screen 1270, 42 px wide, pitch 225.
	const hdatlas::ClassicScreen big = hdatlas::classicScreen(2960, 1746);
	const hdatlas::ClassicRect icon = hdatlas::classicToScreen(
		big, classichud::resourceIconX(big.lw, 0), classichud::ResourceIconY, classichud::ResourceIconSize,
		classichud::ResourceIconSize);
	CHECK(icon.x == 1270);
	CHECK(icon.y == 0);
	CHECK(icon.w == 42);
	CHECK(hdatlas::classicToScreen(big, classichud::resourceIconX(big.lw, 1), 0, 14, 14).x == 1495);
	CHECK(classichud::tooltipMidY(768) == doctest::Approx(716.0));

	const auto cellAt = [](const hdatlas::ClassicScreen &c, int sx, int sy) {
		int lx = 0;
		int ly = 0;
		return hdatlas::classicFromScreen(c, sx, sy, lx, ly) ? classichud::commandCellAt(lx, ly) : -1;
	};
	// Step 1 (1366 x 768, box at 171): cell 1 is screen 176..229 x 336..381.
	const hdatlas::ClassicScreen one = hdatlas::classicScreen(1366, 768);
	const classichud::Rect first = classichud::commandCell(0);
	hdatlas::ClassicRect r = hdatlas::classicToScreen(one, first.x, first.y, first.w, first.h);
	CHECK(r.x == 176);
	CHECK(r.y == 336);
	CHECK(r.w == 54);
	CHECK(r.h == 46);
	CHECK(cellAt(one, 176, 336) == 0);
	CHECK(cellAt(one, 229, 381) == 0);
	CHECK(cellAt(one, 175, 336) == -1);
	CHECK(cellAt(one, 230, 336) == -1);
	CHECK(cellAt(one, 176, 335) == -1);
	CHECK(cellAt(one, 176, 382) == -1);
	CHECK(cellAt(one, 232, 336) == 1);
	// Step 3 (2960 x 1746, box at 316): cell 1 is screen 331..492 x 1008..1145.
	r = hdatlas::classicToScreen(big, first.x, first.y, first.w, first.h);
	CHECK(r.x == 331);
	CHECK(r.y == 1008);
	CHECK(r.w == 162);
	CHECK(r.h == 138);
	CHECK(cellAt(big, 331, 1008) == 0);
	CHECK(cellAt(big, 492, 1145) == 0);
	CHECK(cellAt(big, 330, 1008) == -1);
	CHECK(cellAt(big, 493, 1008) == -1);
	CHECK(cellAt(big, 331, 1007) == -1);
	CHECK(cellAt(big, 331, 1146) == -1);
	CHECK(cellAt(big, 499, 1008) == 1);
	CHECK(cellAt(big, 331, 1149) == 3);
	CHECK(cellAt(big, 315, 1008) == -1);
	// The last cell ends inside the column at both steps.
	const classichud::Rect last = classichud::commandCell(8);
	r = hdatlas::classicToScreen(big, last.x, last.y, last.w, last.h);
	CHECK(r.x + r.w == 316 + 171 * 3);
	CHECK(r.y + r.h == 476 * 3);
	CHECK(r.y + r.h <= big.box.y + big.lh * big.s);
}

TEST_CASE("classichud: the cells of the command card and of the portrait")
{
	// o11_16: pictures at box 9 / 65 / 121 x 340 / 387 / 434, the cell 4 px larger on every side.
	const int px[3] = {9, 65, 121};
	const int py[3] = {340, 387, 434};
	for (int i = 0; i < 9; ++i) {
		const classichud::Rect cell = classichud::commandCell(i);
		const classichud::Rect pic = classichud::cellPicture(cell);
		CHECK(pic.x == px[i % 3]);
		CHECK(pic.y == py[i / 3]);
		CHECK(pic.w == 46);
		CHECK(pic.h == 38);
		CHECK(cell.w == 54);
		CHECK(cell.h == 46);
		CHECK(classichud::commandCellAt(cell.x, cell.y) == i);
		CHECK(classichud::commandCellAt(cell.x + cell.w - 1, cell.y + cell.h - 1) == i);
	}
	// Between the cells and around the card: nothing.
	CHECK(classichud::commandCellAt(59, 340) == -1);
	CHECK(classichud::commandCellAt(60, 340) == -1);
	CHECK(classichud::commandCellAt(9, 382) == -1);
	CHECK(classichud::commandCellAt(4, 340) == -1);
	CHECK(classichud::commandCellAt(171, 340) == -1);
	CHECK(classichud::commandCellAt(9, 335) == -1);
	CHECK(classichud::commandCellAt(9, 476) == -1);
	// The last cell ends at 170 / 475, inside the side column (176 wide).
	CHECK(classichud::commandCell(8).x + classichud::CellW == 171);
	CHECK(classichud::commandCell(8).y + classichud::CellH == 476);
	// Portrait: picture at 9,169; the bar's fill 50 x 5 at 7,211 inside the bar 54 x 9 at 5,209.
	const classichud::Rect portrait = classichud::cellPicture(classichud::PortraitCell);
	CHECK(portrait.x == 9);
	CHECK(portrait.y == 169);
	CHECK(classichud::HitPointFill.x == classichud::HitPointBar.x + classichud::HitPointSlice);
	CHECK(classichud::HitPointFill.w == classichud::HitPointBar.w - 2 * classichud::HitPointSlice);
	CHECK(classichud::HitPointBar.y == classichud::PortraitCell.y + classichud::CellH - 2);
	// Header and resource row.
	CHECK(classichud::contains(classichud::PauseFace, 108, 2));
	CHECK(classichud::contains(classichud::PauseFace, 167, 21));
	CHECK_FALSE(classichud::contains(classichud::PauseFace, 107, 2));
	CHECK_FALSE(classichud::contains(classichud::PauseFace, 168, 2));
	CHECK(classichud::F5Inset.w * 5 == 180);
	CHECK(classichud::F5Inset.h * 5 == 80);
	CHECK(classichud::resourceIconX(1024, 1) - classichud::resourceIconX(1024, 0) == 75);
	CHECK(classichud::resourceIconX(1024, 3) - classichud::resourceIconX(1024, 2) == 75);
}

TEST_CASE("classichud nineSlice: corners keep their size, the pieces cover the rect once")
{
	classichud::SlicePiece pieces[9];
	const classichud::Rect dst = {5, 336, 54, 46};
	const int count = classichud::nineSlice(16, 16, 7, dst, pieces);
	REQUIRE(count == 9);
	std::vector<int> hits(54 * 46, 0);
	int srcArea = 0;
	for (int i = 0; i < count; ++i) {
		const classichud::SlicePiece &p = pieces[i];
		srcArea += p.src.w * p.src.h;
		CHECK(p.src.x >= 0);
		CHECK(p.src.y >= 0);
		CHECK(p.src.x + p.src.w <= 16);
		CHECK(p.src.y + p.src.h <= 16);
		for (int y = p.dst.y; y < p.dst.y + p.dst.h; ++y) {
			for (int x = p.dst.x; x < p.dst.x + p.dst.w; ++x) {
				++hits[(y - dst.y) * 54 + (x - dst.x)];
			}
		}
	}
	CHECK(srcArea == 16 * 16);
	int wrong = 0;
	for (int h : hits) {
		wrong += h != 1;
	}
	CHECK(wrong == 0);
	// Corners 7 x 7 unscaled; the middle 2 x 2 of the sprite fills the rest.
	CHECK(pieces[0].dst.w == 7);
	CHECK(pieces[0].dst.h == 7);
	CHECK(pieces[8].src.x == 9);
	CHECK(pieces[8].dst.x == 5 + 54 - 7);
	CHECK(pieces[4].src.w == 2);
	CHECK(pieces[4].dst.w == 40);
	CHECK(pieces[4].dst.h == 32);
	// The hit point bar: sprite 6 x 6, border 2, on 54 x 9.
	const int bar = classichud::nineSlice(6, 6, 2, classichud::HitPointBar, pieces);
	REQUIRE(bar == 9);
	CHECK(pieces[4].dst.x == classichud::HitPointFill.x);
	CHECK(pieces[4].dst.y == classichud::HitPointFill.y);
	CHECK(pieces[4].dst.w == classichud::HitPointFill.w);
	CHECK(pieces[4].dst.h == classichud::HitPointFill.h);
	// A rect smaller than two borders has no middle.
	CHECK(classichud::nineSlice(16, 16, 7, classichud::Rect{0, 0, 14, 14}, pieces) == 4);
}

TEST_CASE("classichud hitPointFill: width and colour of the bar")
{
	int colour = -1;
	CHECK(classichud::hitPointFill(30, 30, colour) == 50);   // o11_16: Peasant 30 / 30, 50 px green
	CHECK(colour == 0);
	CHECK(classichud::hitPointFill(1200, 1200, colour) == 50);
	CHECK(classichud::hitPointFill(40, 30, colour) == 50);
	CHECK(classichud::hitPointFill(15, 30, colour) == 25);
	CHECK(colour == 1);
	CHECK(classichud::hitPointFill(14, 30, colour) == 23);
	CHECK(colour == 2);
	CHECK(classichud::hitPointFill(0, 30, colour) == 0);
	CHECK(classichud::hitPointFill(5, 0, colour) == 0);
	// o21_52: a building site at 108 / 400, 13 px of bar_red (columns 7..19).
	CHECK(classichud::hitPointFill(108, 400, colour) == 13);
	CHECK(colour == 2);
	// o30_C04_*: Grunt 37 / 60, 30 px yellow (columns 7..36); Grunt 17 / 60, 14 px red; Peon
	// 16 / 30, 26 px yellow; a2_p03: Peon 7 / 30, 11 px red.
	CHECK(classichud::hitPointFill(37, 60, colour) == 30);
	CHECK(colour == 1);
	CHECK(classichud::hitPointFill(17, 60, colour) == 14);
	CHECK(colour == 2);
	CHECK(classichud::hitPointFill(16, 30, colour) == 26);
	CHECK(colour == 1);
	CHECK(classichud::hitPointFill(7, 30, colour) == 11);
	CHECK(colour == 2);
}

TEST_CASE("classichud tooltipBox: lines, the cost row and the requirement line")
{
	using namespace classichud;
	// One line (o11_55), 640 x 480 and 720 x 540 (o30_C09_*).
	CHECK(tooltipBox(768, 1).fill[0].y == 692);
	CHECK(tooltipBox(768).fill[0].y == 692);
	CHECK(tooltipLineMidY(768, 1, 0, false) == doctest::Approx(tooltipMidY(768)));
	CHECK(tooltipBox(480, 1).fill[0].y == 404);
	CHECK(tooltipBox(480, 1).fill[0].y + tooltipBox(480, 1).fill[0].h - 1 == 451);
	CHECK(tooltipBox(540, 1).fill[0].y == 464);
	// Three lines (o30_C05_hover_build_cell0): top row 662, capitals from 682 / 697 / 712.
	const TooltipBox three = tooltipBox(768, 3);
	CHECK(three.fill[0].y == 662);
	CHECK(three.fill[0].y + three.fill[0].h - 1 == 739);
	CHECK(three.fill[0].x == 195);
	CHECK(three.fill[1].x == 188);
	CHECK(tooltipLineMidY(768, 3, 0, false) == doctest::Approx(686.0));
	CHECK(tooltipLineMidY(768, 3, 1, false) == doctest::Approx(701.0));
	CHECK(tooltipLineMidY(768, 3, 2, false) == doctest::Approx(716.0));
	// Four lines, the last one the requirement (o30_C05_hover_dim_keep): top row 646, the side
	// lines from 653 to 732, capitals from 666 / 681 / 696 / 712.
	const TooltipBox four = tooltipBox(768, 4);
	CHECK(four.fill[0].y == 646);
	CHECK(four.fill[1].y == 653);
	CHECK(four.fill[1].y + four.fill[1].h - 1 == 732);
	CHECK(four.line[3].y == 653);
	CHECK(four.line[3].h == 80);
	CHECK(four.line[6].y == 739);
	CHECK(tooltipLineMidY(768, 4, 0, false) == doctest::Approx(670.0));
	CHECK(tooltipLineMidY(768, 4, 1, false) == doctest::Approx(685.0));
	CHECK(tooltipLineMidY(768, 4, 2, false) == doctest::Approx(700.0));
	CHECK(tooltipLineMidY(768, 4, 3, true) == doctest::Approx(716.0));
	// Four lines without it (o30_C05_hover_arrow_upgrade): the same box, capitals from 711.
	CHECK(tooltipLineMidY(768, 4, 3, false) == doctest::Approx(715.0));
	// The cost row of that box: icons 14 x 14 at x 204 / 270 / 336, rows 678..691, the number
	// 18 px right of its icon.
	const double costMid = tooltipLineMidY(768, 4, 1, false);
	for (int k = 0; k < TooltipCostMax; ++k) {
		const Rect icon = tooltipCostIcon(k, costMid);
		CHECK(icon.x == 204 + 66 * k);
		CHECK(icon.y == 678);
		CHECK(icon.y + icon.h - 1 == 691);
		CHECK(icon.w == 14);
		CHECK(icon.x + TooltipCostTextDX == 222 + 66 * k);
		CHECK(contains(four.fill[1], icon.x, icon.y));
		CHECK(contains(four.fill[1], icon.x + icon.w - 1, icon.y + icon.h - 1));
	}
	CHECK(TooltipX + TooltipTextW <= TooltipBoxX + TooltipBoxW - TooltipBoxCutRight);
	CHECK(TooltipBoxAlpha * 3 == 255);
}

TEST_CASE("classichud messageMidY: the message line above the bottom strip")
{
	using namespace classichud;
	CHECK(messageMidY(768) == doctest::Approx(722.0));
	CHECK(messageMidY(582) == doctest::Approx(536.0));
	CHECK(messageMidY(480) == doctest::Approx(434.0));
	// Inside the map view (it starts at x 176), in the rows of the tooltip box.
	CHECK(MessageX == 176 + 7);
	const TooltipBox box = tooltipBox(768);
	CHECK(contains(box.fill[1], MessageX + 8, (int) messageMidY(768)));
}

TEST_CASE("classichud objectiveMidY: the rows of the objectives block")
{
	using namespace classichud;
	CHECK(objectiveMidY(0) == doctest::Approx(32.0));
	CHECK(objectiveMidY(1) == doctest::Approx(48.0));
	CHECK(objectiveMidY(2) == doctest::Approx(64.0));
	CHECK(ObjectivesX == 185);
}

TEST_CASE("classichud: the cell of the unit in training")
{
	using namespace classichud;
	CHECK(TrainingCell.x == 106);
	CHECK(TrainingCell.x + TrainingCell.w - 1 == 159);
	CHECK(TrainingCell.y == 238);
	CHECK(TrainingCell.y + TrainingCell.h - 1 == 283);
	CHECK(TrainingCell.x >= InfoPanel.x);
	CHECK(TrainingCell.x + TrainingCell.w <= InfoPanel.x + InfoPanel.w);
	CHECK(TrainingCell.y + TrainingCell.h <= CompleteBar.y);
	const Rect picture = cellPicture(TrainingCell);
	CHECK(picture.x == 110);
	CHECK(picture.y == 242);
	CHECK(picture.w == PictureW);
	CHECK(picture.h == PictureH);
	// The label is a stat label of row 1, left of the cell.
	CHECK(TrainingLabelMidY == doctest::Approx(PanelStatMidY + PanelStatPitch));
	CHECK(PanelColonX < TrainingCell.x);
	// o30_C01_human_hall_trains_b: 41 px done (columns 12..52); the mill: 32 px.
	CHECK(completeFill(27, 100) == 41);
	CHECK(completeFill(32, 152) == 32);
}

TEST_CASE("classichud magicFill: the bar in the Magic row")
{
	using namespace classichud;
	// o30_C14_mage_selected: 29 px of 59 at 127 of 255.
	CHECK(magicFill(127, 255) == 29);
	CHECK(magicFill(255, 255) == 59);
	CHECK(magicFill(300, 255) == 59);
	CHECK(magicFill(0, 255) == 0);
	CHECK(magicFill(5, 0) == 0);
	const Rect bar = magicBar(MagicRow);
	CHECK(bar.x == 104);
	CHECK(bar.y == 306);
	CHECK(bar.w == 63);
	CHECK(bar.h == 14);
	const Rect inside = magicFillArea(bar);
	CHECK(inside.x == 106);
	CHECK(inside.y == 308);
	CHECK(inside.w == 59);
	CHECK(inside.h == 10);
	// The bar stands on its stat row, the number on the bar's middle.
	CHECK(bar.y + bar.h / 2.0 == doctest::Approx(PanelStatMidY + PanelStatPitch * MagicRow + 1.0));
	CHECK(MagicTextMidX == doctest::Approx(bar.x + bar.w / 2.0));
	CHECK(bar.x + bar.w <= InfoPanel.x + InfoPanel.w);
}

TEST_CASE("classichud groupCell: the cells of a group and their bars")
{
	using namespace classichud;
	// Up to 9: cells of the portrait's size at x 5 / 61 / 117, y 165 / 220 / 275.
	CHECK(groupCell(0, true).x == PortraitCell.x);
	CHECK(groupCell(0, true).y == PortraitCell.y);
	CHECK(groupCell(0, true).w == 54);
	CHECK(groupCell(0, true).h == 46);
	CHECK(groupCell(4, true).x == 61);
	CHECK(groupCell(4, true).y == 220);
	CHECK(groupCell(8, true).x == 117);
	CHECK(groupCell(8, true).y == 275);
	// The bar of cell 0 is the bar of the single portrait.
	const Rect bar = groupBar(groupCell(0, true));
	CHECK(bar.x == HitPointBar.x);
	CHECK(bar.y == HitPointBar.y);
	CHECK(bar.w == HitPointBar.w);
	CHECK(bar.h == HitPointBar.h);
	const Rect fill = groupFill(groupCell(0, true));
	CHECK(fill.x == HitPointFill.x);
	CHECK(fill.y == HitPointFill.y);
	CHECK(fill.w == HitPointFill.w);
	CHECK(fill.h == HitPointFill.h);
	CHECK(groupFill(groupCell(3, true)).y == 266);
	CHECK(groupFill(groupCell(6, true)).y == 321);
	CHECK(groupFill(groupCell(8, true)).y + HitPointFill.h - 1 == 325);
	CHECK(groupBar(groupCell(8, true)).y + HitPointBar.h <= InfoPanel.y + InfoPanel.h);
	// Taps: the corners, the 2 px between cells, the bar's rows, outside.
	CHECK(groupCellAt(5, 165, true) == 0);
	CHECK(groupCellAt(58, 210, true) == 0);
	CHECK(groupCellAt(59, 170, true) == -1);
	CHECK(groupCellAt(60, 170, true) == -1);
	CHECK(groupCellAt(61, 170, true) == 1);
	CHECK(groupCellAt(170, 320, true) == 8);
	CHECK(groupCellAt(171, 320, true) == -1);
	CHECK(groupCellAt(10, 211, true) == -1);
	CHECK(groupCellAt(10, 219, true) == -1);
	CHECK(groupCellAt(10, 220, true) == 3);
	CHECK(groupCellAt(10, 321, true) == -1);
	CHECK(groupCellAt(4, 170, true) == -1);
	CHECK(groupCellAt(10, 164, true) == -1);
	// 10 to 12: cells 40 wide at x 5 / 47 / 89 / 131.
	CHECK(groupCell(3, false).x == 131);
	CHECK(groupCell(3, false).y == 165);
	CHECK(groupCell(3, false).w == 40);
	CHECK(groupCell(3, false).h == 46);
	CHECK(groupCell(9, false).x == 47);
	CHECK(groupCell(9, false).y == 275);
	CHECK(groupCell(11, false).x + GroupSmallW == InfoPanel.x + InfoPanel.w);
	const Rect small = cellPicture(groupCell(1, false));
	CHECK(small.w == GroupSmallPictureW);
	CHECK(small.h == PictureH);
	CHECK(GroupSmallPictureSrcX == 7);
	CHECK(GroupSmallPictureSrcX + GroupSmallPictureW - 1 == 38);
	// Their bars: fill columns 7..42, 49..84, 91..126, 133..168.
	for (int c = 0; c < 4; ++c) {
		const Rect f = groupFill(groupCell(c, false));
		CHECK(f.x == 7 + 42 * c);
		CHECK(f.x + f.w - 1 == 42 + 42 * c);
		CHECK(groupBar(groupCell(c, false)).w == 40);
	}
	CHECK(groupCellAt(45, 170, false) == -1);
	CHECK(groupCellAt(47, 170, false) == 1);
	CHECK(groupCellAt(170, 320, false) == 11);
	// Fill: the single bar's width for its 50 px, 36 px for a narrow cell.
	int colour = -1;
	int single = -1;
	for (const int hp : {0, 1, 7, 14, 15, 16, 17, 29, 30, 37, 45, 59, 60, 61}) {
		CHECK(groupHitPointFill(hp, 60, 50, colour) == hitPointFill(hp, 60, single));
		CHECK(colour == single);
	}
	CHECK(groupHitPointFill(60, 60, 36, colour) == 36);
	CHECK(colour == 0);
	CHECK(groupHitPointFill(37, 60, 36, colour) == 22);
	CHECK(colour == 1);
	CHECK(groupHitPointFill(5, 0, 36, colour) == 0);
}

TEST_CASE("classichud: passengers of a transport sit in the group rows 1 and 2")
{
	using namespace classichud;
	CHECK(passengerCell(0).x == 5);
	CHECK(passengerCell(0).y == 220);
	CHECK(passengerCell(2).x == 117);
	CHECK(passengerCell(3).x == 5);
	CHECK(passengerCell(3).y == 275);
	CHECK(passengerCell(5).x == 117);
	CHECK(passengerCell(5).y == 275);
	CHECK(groupFill(passengerCell(0)).y == 266);
	CHECK(groupFill(passengerCell(5)).y == 321);
	// The ship's own portrait cell is no passenger.
	CHECK(passengerCellAt(10, 170) == -1);
	CHECK(passengerCellAt(10, 225) == 0);
	CHECK(passengerCellAt(120, 280) == 5);
	CHECK(passengerCellAt(59, 225) == -1);
	CHECK(passengerCellAt(10, 268) == -1);
	CHECK(PassengerMax == 6);
}

TEST_CASE("classichud completeFill: the bar of a building under construction")
{
	using namespace classichud;
	// Inside the info panel, under its texts; the inside is the sprite less its padding of 3.
	CHECK(CompleteBar.x >= InfoPanel.x);
	CHECK(CompleteBar.x + CompleteBar.w <= InfoPanel.x + InfoPanel.w);
	CHECK(CompleteBar.y + CompleteBar.h <= InfoPanel.y + InfoPanel.h);
	CHECK(CompleteFill.x == CompleteBar.x + 3);
	CHECK(CompleteFill.y == CompleteBar.y + 3);
	CHECK(CompleteFill.w == CompleteBar.w - 6);
	CHECK(CompleteFill.h == CompleteBar.h - 6);
	CHECK(CompleteFill.x == 12);
	CHECK(CompleteFill.x + CompleteFill.w - 1 == 163);
	CHECK(completeFill(0, 100) == 0);
	CHECK(completeFill(-5, 100) == 0);
	CHECK(completeFill(5, 0) == 0);
	// o21_52: 40 px done (columns 12..51) = 26.3 % of the 152.
	CHECK(completeFill(2632, 10000) == 40);
	CHECK(completeFill(40, 152) == 40);
	CHECK(completeFill(26, 100) == 39);
	CHECK(completeFill(27, 100) == 41);
	CHECK(completeFill(99, 100) == 150);
	CHECK(completeFill(100, 100) == 152);
	CHECK(completeFill(500, 100) == 152);
	CHECK(completeFill(2000000000, 2000000000) == 152);
	CHECK(completeFill(1999999999, 2000000000) == 151);
	// The words stand on the inside's middle; the Remastered panel's place of them is that place.
	CHECK(CompleteTextMidX == doctest::Approx(CompleteFill.x + CompleteFill.w / 2.0));
	CHECK(CompleteTextMidY == doctest::Approx(CompleteFill.y + CompleteFill.h / 2.0));
	CHECK(panelX(396.0) == doctest::Approx(CompleteTextMidX));
	CHECK(panelMidY(1437.75) == doctest::Approx(CompleteTextMidY));
}

TEST_CASE("classichud tooltipBox: the box under the hint")
{
	using namespace classichud;
	const TooltipBox box = tooltipBox(768);
	// o11_55: columns 195..411 in the rows 692..739, columns 188..419 in the rows 699..732.
	CHECK(box.fill[0].x == 195);
	CHECK(box.fill[0].y == 692);
	CHECK(box.fill[0].x + box.fill[0].w - 1 == 411);
	CHECK(box.fill[0].y + box.fill[0].h - 1 == 739);
	CHECK(box.fill[1].x == 188);
	CHECK(box.fill[1].y == 699);
	CHECK(box.fill[1].x + box.fill[1].w - 1 == 419);
	CHECK(box.fill[1].y + box.fill[1].h - 1 == 732);
	// The line: one pixel thick, every piece on the edge of the black, the outline closed (every
	// corner pixel of the twelve-cornered shape is on two pieces).
	int corners = 0;
	for (int i = 0; i < TooltipBoxLines; ++i) {
		const Rect &l = box.line[i];
		CHECK((l.w == 1 || l.h == 1));
		CHECK((contains(box.fill[0], l.x, l.y) || contains(box.fill[1], l.x, l.y)));
		CHECK((contains(box.fill[0], l.x + l.w - 1, l.y + l.h - 1)
		       || contains(box.fill[1], l.x + l.w - 1, l.y + l.h - 1)));
		for (int j = i + 1; j < TooltipBoxLines; ++j) {
			const Rect &m = box.line[j];
			if (l.x < m.x + m.w && m.x < l.x + l.w && l.y < m.y + m.h && m.y < l.y + l.h) {
				++corners;
			}
		}
	}
	CHECK(corners == 12);
	CHECK(box.line[0].x == 195);
	CHECK(box.line[0].y == 692);
	CHECK(box.line[0].w == 217);
	CHECK(box.line[3].x == 419);
	CHECK(box.line[3].y == 699);
	CHECK(box.line[3].h == 34);
	CHECK(box.line[6].y == 739);
	CHECK(box.line[9].x == 188);
	// The hint inside it: ink from x 204, capital rows 712..719.
	CHECK(contains(box.fill[1], TooltipX, (int) tooltipMidY(768) - 4));
	CHECK(contains(box.fill[1], TooltipX, (int) tooltipMidY(768) + 3));
	// Hung on the bottom at another height.
	const TooltipBox low = tooltipBox(582);
	CHECK(low.fill[0].y == 582 - 76);
	CHECK(low.fill[0].y + low.fill[0].h - 1 == 582 - 29);
	CHECK(low.fill[0].x == box.fill[0].x);
	CHECK(low.line[3].h == box.line[3].h);
}

TEST_CASE("classichud tooltipBoxBlend: run 59 line, fill and outer ring")
{
	using namespace classichud;
	// Measured: line (241, 243, 221) at alpha 0.272..0.277, fill black 0.34 (85 of 255 kept,
	// CLASSIC_CALIB56), ring black 0.15..0.25.
	CHECK(TooltipBoxLineRGB[0] == 241);
	CHECK(TooltipBoxLineRGB[1] == 243);
	CHECK(TooltipBoxLineRGB[2] == 221);
	CHECK(TooltipBoxLineAlpha / 255.0 >= 0.272);
	CHECK(TooltipBoxLineAlpha / 255.0 <= 0.277);
	CHECK(TooltipBoxRingAlpha / 255.0 >= 0.15);
	CHECK(TooltipBoxRingAlpha / 255.0 <= 0.25);
	for (int lines = 1; lines <= 4; ++lines) {
		const TooltipBox outer = tooltipBox(768, lines);
		const TooltipBoxBlend b = tooltipBoxBlend(768, lines);
		std::vector<Rect> all;
		for (const Rect &r : b.inner) { all.push_back(r); }
		for (const Rect &r : b.line) { all.push_back(r); }
		for (const Rect &r : b.ring) { all.push_back(r); }
		// No pixel in two rects (nothing blended twice), none empty.
		for (size_t i = 0; i < all.size(); ++i) {
			CHECK(all[i].w > 0);
			CHECK(all[i].h > 0);
			for (size_t j = i + 1; j < all.size(); ++j) {
				const Rect &l = all[i];
				const Rect &m = all[j];
				CHECK_FALSE((l.x < m.x + m.w && m.x < l.x + l.w && l.y < m.y + m.h && m.y < l.y + l.h));
			}
		}
		// Pixel by pixel over the box and one pixel round it: inner + line = the old box, the
		// line is what the old line covered, the ring is outside the box and touches the line.
		auto in = [](const Rect *rs, int n, int x, int y) {
			for (int i = 0; i < n; ++i) {
				if (contains(rs[i], x, y)) { return true; }
			}
			return false;
		};
		for (int y = outer.fill[0].y - 2; y <= outer.fill[0].y + outer.fill[0].h + 1; ++y) {
			for (int x = outer.fill[1].x - 2; x <= outer.fill[1].x + outer.fill[1].w + 1; ++x) {
				const bool box = in(outer.fill, TooltipBoxFills, x, y);
				const bool oldLine = in(outer.line, TooltipBoxLines, x, y);
				const bool line = in(b.line, TooltipBoxLines, x, y);
				const bool inner = in(b.inner, 3, x, y);
				const bool ring = in(b.ring, TooltipBoxLines, x, y);
				CHECK(line == oldLine);
				CHECK((inner || line) == box);
				bool nearLine = false;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						nearLine = nearLine || in(b.line, TooltipBoxLines, x + dx, y + dy);
					}
				}
				CHECK(ring == (!box && nearLine));
			}
		}
	}
	// One line at 1366 x 768 (logical x = screen x - 171): the ring rows 691 / 740, columns
	// 358 / 591 on the screen.
	const TooltipBoxBlend one = tooltipBoxBlend(768);
	CHECK(one.ring[0].y == 691);
	CHECK(one.ring[6].y == 740);
	CHECK(one.ring[9].x + 171 == 358);
	CHECK(one.ring[3].x + 171 == 591);
	// The blend over a ground g gives the fit's a = 1 - alpha and k = alpha * colour.
	const double a = 1.0 - TooltipBoxLineAlpha / 255.0;
	CHECK(a == doctest::Approx(0.7255).epsilon(0.005));
	CHECK(TooltipBoxLineRGB[0] * (1.0 - a) == doctest::Approx(241 * 0.2745).epsilon(0.01));
}

TEST_CASE("classichud greyOf: the frame under a window is its luma")
{
	using namespace classichud;
	// o21_14 -> o21_40, the orc frame at the same pixels.
	CHECK(greyOf(0x34, 0x20, 0x00) == 0x22);
	CHECK(greyOf(0x50, 0x30, 0x08) == 0x34);
	CHECK(greyOf(0x3c, 0x24, 0x04) == 0x27);
	CHECK(greyOf(0x48, 0x2c, 0x04) == 0x2f);
	// The human frame is grey already and stays (o11_16 / o11_28).
	CHECK(greyOf(0x28, 0x28, 0x28) == 0x28);
	CHECK(greyOf(0x58, 0x58, 0x58) == 0x58);
	CHECK(greyOf(0x34, 0x34, 0x34) == 0x34);
	CHECK(greyOf(0, 0, 0) == 0);
	CHECK(greyOf(255, 255, 255) == 255);
}

TEST_CASE("classichud faceName: the sprite of a button by its state")
{
	using namespace classichud;
	CHECK(faceName("hud_human_small", false) == "hud_human_small");
	CHECK(faceName("hud_human_small", true) == "hud_human_small_active");
	CHECK(faceName("hud_orc_small", true) == "hud_orc_small_active");
	CHECK(faceName("portrait_button", true) == "portrait_button_active");
	CHECK(faceName("portrait_button_highlight", true) == "portrait_button_highlight_active");
	CHECK(faceName("f5", true) == "f5_active");
}

TEST_CASE("hd_atlas classicMinimapSlot: a tile past the sheet's end is refused")
{
	// The colour file: records from megatile 16 on.
	CHECK(hdatlas::classicMinimapRecord(16, 16, 400) == 0);
	CHECK(hdatlas::classicMinimapRecord(415, 16, 400) == 399);
	CHECK(hdatlas::classicMinimapRecord(416, 16, 400) == -1);
	CHECK(hdatlas::classicMinimapRecord(15, 16, 400) == -1);
	CHECK(hdatlas::classicMinimapRecord(0, 16, 400) == -1);
	CHECK(hdatlas::classicMinimapRecord(-1, 16, 400) == -1);
	CHECK(hdatlas::classicMinimapRecord(16, 16, 0) == -1);
	// A tileset sheet of 16 tiles a row and 24 rows: tiles 0..383.
	CHECK(hdatlas::tileSheetHolds(0, 16, 24));
	CHECK(hdatlas::tileSheetHolds(383, 16, 24));
	CHECK_FALSE(hdatlas::tileSheetHolds(384, 16, 24));
	CHECK_FALSE(hdatlas::tileSheetHolds(5000, 16, 24));
	CHECK_FALSE(hdatlas::tileSheetHolds(-1, 16, 24));
	CHECK_FALSE(hdatlas::tileSheetHolds(3, 0, 24));
	CHECK_FALSE(hdatlas::tileSheetHolds(3, 16, 0));
}

TEST_CASE("classichud text: the face for a capital's height and its scale")
{
	using namespace classichud;
	// 2960 x 1746: step 3, a capital 24 px; the HUD faces at step 3 have capitals of 25.5 and 18 px.
	const double caps[2] = {38.25 * 3 / 4.5, 27.0 * 3 / 4.5};
	double scale = 0.0;
	CHECK(capOnScreen(TextCap, 1) == doctest::Approx(8.0));
	CHECK(capOnScreen(TextCap, 2) == doctest::Approx(16.0));
	CHECK(capOnScreen(TextCap, hdatlas::classicStep(1746)) == doctest::Approx(24.0));
	CHECK(pickFace(capOnScreen(TextCap, 3), caps, 2, scale) == 0);
	CHECK(scale * caps[0] == doctest::Approx(24.0));
	CHECK(pickFace(capOnScreen(TextCapSmall, 3), caps, 2, scale) == 1);
	CHECK(scale * caps[1] == doctest::Approx(15.0));
	// 1600 x 720: step 1, a capital 8 px whatever the box's height.
	CHECK(pickFace(capOnScreen(TextCap, hdatlas::classicStep(720)), caps, 2, scale) == 1);
	CHECK(scale * caps[1] == doctest::Approx(8.0));
	// A height nearer to the large face takes it; a tie keeps the first.
	CHECK(pickFace(24.0, caps, 2, scale) == 0);
	CHECK(scale == doctest::Approx(24.0 / 25.5));
	CHECK(pickFace((caps[0] + caps[1]) / 2, caps, 2, scale) == 0);
	// A face that is not loaded is passed over; none at all: -1, drawn at 1.
	const double one[2] = {0.0, 18.0};
	CHECK(pickFace(30.0, one, 2, scale) == 1);
	CHECK(scale == doctest::Approx(30.0 / 18.0));
	const double none[2] = {0.0, 0.0};
	CHECK(pickFace(30.0, none, 2, scale) == -1);
	CHECK(scale == doctest::Approx(1.0));
}

TEST_CASE("classichud text: left, centred and to the colon; the panel's rows")
{
	using namespace classichud;
	CHECK(textLeft(TextAlign::Left, 460.0, 33.0, 0.0) == doctest::Approx(460.0));
	// "Menu" 29 px wide in the face 46..105: 15 px free on the left (o11_13: ink 61..89).
	CHECK(textLeft(TextAlign::Centre, 46 + 60 / 2.0, 29.0, 0.0) == doctest::Approx(61.5));
	CHECK(textLeft(TextAlign::Centre, PauseFace.x + PauseFace.w / 2.0, 28.0, 0.0) == doctest::Approx(124.0));
	// "Armor: 0": the label ends at the colon's column whatever the value's width.
	CHECK(textLeft(TextAlign::Tab, PanelColonX, 60.0, 37.0) == doctest::Approx(63.0));
	CHECK(textLeft(TextAlign::Tab, PanelColonX, 90.0, 37.0) == doctest::Approx(63.0));
	// Resource numbers start 18 px right of each icon.
	CHECK(resourceIconX(1024, 0) + ResourceTextDX == 460);
	CHECK(resourceIconX(1024, 3) + ResourceTextDX == 685);
	// The Remastered panel's places are the measured classic ones.
	CHECK(panelMidY(852.75) == doctest::Approx(PanelNameMidY));
	CHECK(panelMidY(920.25) == doctest::Approx(PanelLevelMidY));
	for (int n = 0; n < 5; ++n) {
		CHECK(panelMidY(1064.25 + 67.5 * n) == doctest::Approx(PanelStatMidY + PanelStatPitch * n));
	}
	CHECK(panelX(450.0) == doctest::Approx(PanelColonX));
	CHECK(panelX(520.0) == doctest::Approx(PanelNameX).epsilon(0.01));
	// Hit point figure: centred on the bar (x 144 units = 32; measured centre 31.5), capitals'
	// middle 1003.5 units = 223.5 (measured 222.5): within a pixel.
	CHECK(panelX(144.0) == doctest::Approx(HitPointBar.x + HitPointBar.w / 2.0));
	CHECK(panelMidY(1003.5) - 222.5 <= 1.0);
}

TEST_CASE("hd_atlas minimapMarkPixels: one pixel more than the unit's tiles cover")
{
	constexpr int fac = 48;
	// 1 pixel a tile (128 map on 128 pixels): a one-tile unit is 2 x 2, whatever its grid step.
	CHECK(hdatlas::minimapMarkPixels(1, 1 * fac, fac) == 2);
	CHECK(hdatlas::minimapMarkPixels(2, 1 * fac, fac) == 3);
	CHECK(hdatlas::minimapMarkPixels(4, 1 * fac, fac) == 5);
	// 4/3 pixels a tile (96 map): still 2.
	CHECK(hdatlas::minimapMarkPixels(1, 64, fac) == 2);
	CHECK(hdatlas::minimapMarkPixels(3, 64, fac) == 5);
	CHECK(hdatlas::minimapMarkPixels(1, 2 * fac, fac) == 3);
	CHECK(hdatlas::minimapMarkPixels(1, 4 * fac, fac) == 5);
}

TEST_CASE("hd_atlas classicMinimapMarkPixels: the measured marks of the original's classic minimap")
{
	constexpr int fac = 48;
	// scale = pixels a tile * fac on the 128-pixel minimap: map 128 -> 48, 96 -> 64, 64 -> 96, 32 -> 192
	// 128 map: every one-tile unit 2 x 2 (o14_03)
	CHECK(hdatlas::classicMinimapMarkPixels(1, 48, fac) == 2);
	// 96 map: Peasant 2 x 2, Gold Mine (3 tiles) 4 x 4
	CHECK(hdatlas::classicMinimapMarkPixels(1, 64, fac) == 2);
	CHECK(hdatlas::classicMinimapMarkPixels(3, 64, fac) == 4);
	// 64 map: Peasant 2 x 2, Gold Mine 6 x 6, Town Hall (4 tiles) 8 x 8
	CHECK(hdatlas::classicMinimapMarkPixels(1, 96, fac) == 2);
	CHECK(hdatlas::classicMinimapMarkPixels(3, 96, fac) == 6);
	CHECK(hdatlas::classicMinimapMarkPixels(4, 96, fac) == 8);
	// 32 map: Peasant 4 x 4, Gold Mine 12 x 12, Town Hall 16 x 16
	CHECK(hdatlas::classicMinimapMarkPixels(1, 192, fac) == 4);
	CHECK(hdatlas::classicMinimapMarkPixels(3, 192, fac) == 12);
	CHECK(hdatlas::classicMinimapMarkPixels(4, 192, fac) == 16);
	// the Remastered view keeps its rule
	CHECK(hdatlas::minimapMarkPixels(1, 192, fac) == 5);
}

TEST_CASE("hd_atlas remasteredMinimapMarkPixels: the original's Remastered minimap marks (run 71)")
{
	constexpr int fac = 48;
	// scale as CMinimap::Create makes it: ceil(surface * fac / n)
	auto scaleFor = [](int surface, int n) { return (surface * fac + n - 1) / n; };
	struct Row { int n; int peasant; int farm; int hall; int mine; };
	// Measured on the original's 128-pixel minimap (ORIG71 "Retry", M).
	const Row rows[] = {
		{32, 4, 8, 16, 12},
		{64, 2, 4, 8, 6},
		{128, 2, 2, 4, 3},
	};
	for (const Row &r : rows) {
		CAPTURE(r.n);
		const int s = scaleFor(128, r.n);
		CHECK(hdatlas::remasteredMinimapMarkPixels(1, s, fac, 128) == r.peasant);
		CHECK(hdatlas::remasteredMinimapMarkPixels(2, s, fac, 128) == r.farm);
		CHECK(hdatlas::remasteredMinimapMarkPixels(4, s, fac, 128) == r.hall);
		CHECK(hdatlas::remasteredMinimapMarkPixels(3, s, fac, 128) == r.mine);
		// A larger surface (3 screen pixels per logical minimap pixel): everything x 3, the
		// one-tile floor too.
		const int s3 = scaleFor(384, r.n);
		CHECK(hdatlas::remasteredMinimapMarkPixels(1, s3, fac, 384) == 3 * r.peasant);
		CHECK(hdatlas::remasteredMinimapMarkPixels(2, s3, fac, 384) == 3 * r.farm);
		CHECK(hdatlas::remasteredMinimapMarkPixels(4, s3, fac, 384) == 3 * r.hall);
	}
	// Footman on 64 (1593 at 34,23): 2 x 2
	CHECK(hdatlas::remasteredMinimapMarkPixels(1, scaleFor(128, 64), fac, 128) == 2);
	// a tiny surface never draws under 2
	CHECK(hdatlas::remasteredMinimapMarkPixels(1, scaleFor(64, 128), fac, 64) == 2);
}

TEST_CASE("hd_atlas classicMinimapViewPixels: the view rectangle of every measured capture")
{
	struct Row {
		int map;   // tiles of the map's side
		int lw;    // logical screen
		int lh;
		int w;     // the rectangle read from the capture, logical pixels
		int h;
	};
	static const Row rows[] = {
		// 32-tile map: o11_13, o11_12 (1366 x 776), o11_11 (1920 x 1080, step 2).
		{32, 1024, 768, 104, 92}, {32, 1035, 776, 104, 92}, {32, 720, 540, 64, 60},
		// 64-tile map: o11_80, o11_83_* (1000 x 800, 1280 x 800, 1366 x 1000, 2560 x 1300, 800 x 480).
		{64, 1024, 768, 52, 46}, {64, 1000, 750, 50, 44}, {64, 1067, 800, 54, 48},
		{64, 666, 500, 28, 28}, {64, 866, 650, 42, 38}, {64, 640, 480, 28, 28},
		// 96-tile map, step 1: o21_01 / o11_40, o21_20, o21_21, o21_22 / o21_28, o21_26.
		{96, 1024, 768, 35, 31}, {96, 1200, 900, 42, 37}, {96, 1267, 950, 45, 38},
		{96, 1279, 959, 45, 38}, {96, 1100, 825, 38, 33},
		// step 2: o21_23 / o21_29, o21_25, o21_27, o21_50.
		{96, 640, 480, 19, 19}, {96, 647, 485, 19, 19}, {96, 550, 413, 15, 15}, {96, 720, 540, 22, 21},
		// step 3: o21_11, o21_10, o21_12, o21_13 / o21_14 / o21_53..55, o21_31.
		{96, 640, 480, 19, 19}, {96, 666, 500, 19, 19}, {96, 711, 533, 22, 21}, {96, 776, 582, 25, 23},
		{96, 800, 600, 26, 23},
		// step 4 and 5: o21_15, o21_16, o21_17.
		{96, 600, 450, 17, 18}, {96, 650, 487, 19, 19}, {96, 480, 360, 13, 14},
	};
	for (const Row &r : rows) {
		const hdatlas::ClassicScreen c = {{0, 0, r.lw, r.lh}, 1, r.lw, r.lh};
		const hdatlas::ClassicRect v = hdatlas::classicView(c);
		CAPTURE(r.map);
		CAPTURE(r.lw);
		CAPTURE(r.lh);
		CHECK(hdatlas::classicMinimapViewPixels(v.w, r.map) == r.w);
		CHECK(hdatlas::classicMinimapViewPixels(v.h, r.map) == r.h);
	}
	// The device: 2960 x 1746, step 3, a 96-tile map: 75 x 69 screen pixels (was 72 x 69).
	const hdatlas::ClassicScreen dev = hdatlas::classicScreen(2960, 1746);
	const hdatlas::ClassicRect view = hdatlas::classicView(dev);
	CHECK(dev.s * hdatlas::classicMinimapViewPixels(view.w, 96) == 75);
	CHECK(dev.s * hdatlas::classicMinimapViewPixels(view.h, 96) == 69);
	// The part of a tile at the view's end does not count; the place of the view neither.
	CHECK(hdatlas::classicMinimapViewPixels(18 * 32, 96) == 25);
	CHECK(hdatlas::classicMinimapViewPixels(18 * 32 + 31, 96) == 25);
	CHECK(hdatlas::classicMinimapViewPixels(19 * 32, 96) == 26);
	// o21_30 (2400 x 1439, step 2, box 1918 wide): 33 x 29, the size of 24 x 21 whole tiles.
	CHECK(hdatlas::classicMinimapViewPixels(24 * 32, 96) == 33);
	CHECK(hdatlas::classicMinimapViewPixels(21 * 32 + 15, 96) == 29);
	// Nothing to show.
	CHECK(hdatlas::classicMinimapViewPixels(0, 96) == 0);
	CHECK(hdatlas::classicMinimapViewPixels(584, 0) == 0);
}

TEST_CASE("classichud: the hotkey letter of a command cell and its place")
{
	using namespace classichud;
	CHECK(hotkeyLetter('m') == "M");
	CHECK(hotkeyLetter('B') == "B");
	CHECK(hotkeyLetter('1') == "1");
	CHECK(hotkeyLetter(0).empty());
	CHECK(hotkeyLetter(27).empty());      // Escape: Cancel has no letter (o21_55)
	CHECK(hotkeyLetter(' ').empty());
	CHECK(hotkeyLetter(1004).empty());    // a key outside ASCII
	// o21_54: "M" in cell 1, ink x 12.33..22.33, rows 343.67..351.67; "S" in cell 2, 69.33..74.67;
	// "V" in cell 8, 69..76, rows 437.67..446.
	CHECK(hotkeyMidX(commandCell(0)) == doctest::Approx(16.5));
	CHECK(hotkeyMidY(commandCell(0)) == doctest::Approx(348.0));
	CHECK(hotkeyMidX(commandCell(0)) - (12.33 + 22.33) / 2 <= 1.0);
	CHECK((69.33 + 74.67) / 2 - hotkeyMidX(commandCell(1)) <= 1.0);
	CHECK(hotkeyMidX(commandCell(1)) == doctest::Approx(72.5));
	CHECK(hotkeyMidX(commandCell(7)) == doctest::Approx((69.0 + 76.0) / 2));
	CHECK(hotkeyMidY(commandCell(7)) == doctest::Approx(442.0));
	// On the device (2960 x 1746, step 3): cell 1's letter about screen 365.5, 1044; 24 px high.
	const hdatlas::ClassicScreen dev = hdatlas::classicScreen(2960, 1746);
	CHECK(dev.box.x + hotkeyMidX(commandCell(0)) * dev.s == doctest::Approx(365.5));
	CHECK(dev.box.y + hotkeyMidY(commandCell(0)) * dev.s == doctest::Approx(1044.0));
	CHECK(capOnScreen(TextCap, dev.s) == doctest::Approx(24.0));
	// The letter is inside the picture's top-left quarter.
	const Rect pic = cellPicture(commandCell(4));
	CHECK(hotkeyMidX(commandCell(4)) - TextCap / 2.0 > pic.x);
	CHECK(hotkeyMidY(commandCell(4)) - TextCap / 2.0 >= pic.y);
	CHECK(hotkeyMidY(commandCell(4)) + TextCap / 2.0 < pic.y + pic.h / 2);
}

TEST_CASE("classichud f5InsetSample: a logical pixel shows the middle pixel of its 5 x 5 block")
{
	using namespace classichud;
	CHECK(F5Inset.w * F5InsetBlock == F5InsetArtW);
	CHECK(F5Inset.h * F5InsetBlock == F5InsetArtH);
	const Rect first = f5InsetSample(0, 0);
	CHECK(first.x == 2);
	CHECK(first.y == 2);
	CHECK(first.w == 1);
	CHECK(first.h == 1);
	const Rect last = f5InsetSample(F5Inset.w - 1, F5Inset.h - 1);
	CHECK(last.x == 177);
	CHECK(last.y == 77);
	for (int ly = 0; ly < F5Inset.h; ++ly) {
		for (int lx = 0; lx < F5Inset.w; ++lx) {
			const Rect r = f5InsetSample(lx, ly);
			CHECK(r.x / F5InsetBlock == lx);
			CHECK(r.y / F5InsetBlock == ly);
			CHECK(r.x % F5InsetBlock == 2);
			CHECK(r.y % F5InsetBlock == 2);
			CHECK(r.x < F5InsetArtW);
			CHECK(r.y < F5InsetArtH);
		}
	}
	// The place a renderer's nearest sample takes at step 1 (the middle of the target pixel).
	CHECK((int) ((7 + 0.5) * F5InsetArtW / F5Inset.w) == f5InsetSample(7, 0).x);
}

TEST_CASE("classichud splitStatLine: the label to the colon, the value from its own column")
{
	using namespace classichud;
	std::string label;
	std::string value;
	CHECK(splitStatLine("Armor~|:~_~_~_~_0", label, value));
	CHECK(label == "Armor~|:");
	CHECK(value == "0");
	CHECK(splitStatLine("Damage~|: 1-5", label, value));
	CHECK(label == "Damage~|:");
	CHECK(value == "1-5");
	CHECK(splitStatLine("Armor~|:~_~_~_~_4~<+2~>", label, value));
	CHECK(value == "4~<+2~>");
	// A heading, a name, a line without a value: not split.
	CHECK_FALSE(splitStatLine("Food Usage~|", label, value));
	CHECK_FALSE(splitStatLine("Peon", label, value));
	CHECK_FALSE(splitStatLine("Gold~|:~_~_~_~_", label, value));
	// The value's column: 4.67 right of the colon's column (ink of "0" from 104.67 at step 3).
	CHECK(PanelValueX - PanelColonX == doctest::Approx(4.67));
	CHECK(textLeft(TextAlign::Left, PanelValueX, 6.0, 0.0) == doctest::Approx(104.67));
	// The hit point figure: 1003.5 units is row 223.0 (measured 222.83, 223.0, 222.5).
	CHECK(panelSmallMidY(1003.5) == doctest::Approx(223.0));
	CHECK(panelSmallMidY(1003.5) - 222.83 < 0.5);
}

TEST_CASE("hd_atlas maskAlpha: the luminance of a team mask pixel")
{
	CHECK(hdatlas::maskAlpha(0, 0, 0) == 0);
	CHECK(hdatlas::maskAlpha(255, 255, 255) == 255);
	CHECK(hdatlas::maskAlpha(255, 0, 0) == 76);
	CHECK(hdatlas::maskAlpha(10, 20, 30) == 18); // 4640 >> 8
}

TEST_CASE("hd_atlas runOrdered: results are handed over in list order, two at most undelivered")
{
	// Nine jobs that end in an order of their own: the long ones first in the list.
	const int sleepMs[9] = {40, 5, 25, 1, 30, 2, 20, 1, 10};
	std::mutex lock;
	std::vector<int> ended;      // order the workers finished in
	std::vector<int> consumed;   // order the caller got them in
	int alive = 0;               // results made and not yet through consume
	int aliveMax = 0;
	const std::thread::id caller = std::this_thread::get_id();
	bool consumeOnCaller = true;
	bool decodeOffCaller = true;
	int idles = 0;

	const int high = hdatlas::runOrdered<int>(
		9, 2, 2, 5,
		[&](int i) {
			{
				std::lock_guard<std::mutex> guard(lock);
				++alive;
				aliveMax = alive > aliveMax ? alive : aliveMax;
				decodeOffCaller = decodeOffCaller && std::this_thread::get_id() != caller;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs[i]));
			std::lock_guard<std::mutex> guard(lock);
			ended.push_back(i);
			return i * 10;
		},
		[&](int i, int result) {
			CHECK(result == i * 10);
			consumeOnCaller = consumeOnCaller && std::this_thread::get_id() == caller;
			std::this_thread::sleep_for(std::chrono::milliseconds(3)); // the upload
			std::lock_guard<std::mutex> guard(lock);
			consumed.push_back(i);
			--alive;
		},
		[&](int) { ++idles; });

	CHECK(consumed == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8});
	CHECK(ended.size() == 9);
	CHECK(ended != consumed); // job 1 ended before job 0
	CHECK(high == 2);
	CHECK(aliveMax == 2);
	CHECK(alive == 0);
	CHECK(consumeOnCaller);
	CHECK(decodeOffCaller);
	CHECK(idles > 0); // 40 ms of waiting for job 0 at 5 ms a call
}

TEST_CASE("hd_atlas runOrdered: one worker, a window of one, nothing to do")
{
	std::vector<int> consumed;
	int alive = 0;
	int aliveMax = 0;
	std::mutex lock;
	const int high = hdatlas::runOrdered<int>(
		4, 1, 1, 100,
		[&](int i) {
			std::lock_guard<std::mutex> guard(lock);
			aliveMax = ++alive > aliveMax ? alive : aliveMax;
			return i;
		},
		[&](int i, int result) {
			std::lock_guard<std::mutex> guard(lock);
			consumed.push_back(result);
			CHECK(result == i);
			--alive;
		},
		[](int) {});
	CHECK(consumed == std::vector<int>{0, 1, 2, 3});
	CHECK(high == 1);
	CHECK(aliveMax == 1);

	int calls = 0;
	CHECK(hdatlas::runOrdered<int>(0, 2, 2, 100, [&](int) { ++calls; return 0; },
	                               [&](int, int) { ++calls; }, [&](int) { ++calls; }) == 0);
	CHECK(calls == 0);
	// More workers than jobs, a window larger than the list.
	consumed.clear();
	CHECK(hdatlas::runOrdered<int>(2, 4, 8, 100, [](int i) { return i + 1; },
	                               [&](int, int result) { consumed.push_back(result); }, [](int) {}) == 2);
	CHECK(consumed == std::vector<int>{1, 2});
}

TEST_CASE("hd_atlas stemIsOfOtherBiome: the pages of another biome, by whole tokens")
{
	CHECK(hdatlas::stemIsOfOtherBiome("units_human_forest_sprites", "winter"));
	CHECK_FALSE(hdatlas::stemIsOfOtherBiome("units_human_forest_masks", "forest"));
	CHECK_FALSE(hdatlas::stemIsOfOtherBiome("units_orc_common_sprites", "winter"));
	CHECK_FALSE(hdatlas::stemIsOfOtherBiome("units_orc_common_masks", "forest"));
	CHECK(hdatlas::stemIsOfOtherBiome("units_common_xswamp_sprites", "swamp"));
	CHECK(hdatlas::stemIsOfOtherBiome("units_common_swamp_sprites", "xswamp"));
	CHECK_FALSE(hdatlas::stemIsOfOtherBiome("units_human_swamp_sprites", "swamp"));
	CHECK_FALSE(hdatlas::stemIsOfOtherBiome("units_common_xswamp_sprites", "xswamp"));
	CHECK(hdatlas::stemIsOfOtherBiome("units_common_winter_sprites", "forest"));
	CHECK_FALSE(hdatlas::stemIsOfOtherBiome("", "forest"));
}

TEST_CASE("hd_atlas stemInBiome: a roster atlas of another tileset is looked for in the map's biome")
{
	// LOAD-CRASH-3473: the Wasteland Town Hall uses the summer file, whose roster atlas is the forest
	// one; on a Wasteland map (effect tileset "swamp") the family is looked for in units_human_swamp.
	CHECK(hdatlas::stemInBiome("units_human_forest", "swamp") == "units_human_swamp");
	CHECK(hdatlas::stemInBiome("units_orc_forest", "swamp") == "units_orc_swamp");
	CHECK(hdatlas::stemInBiome("units_human_winter", "xswamp") == "units_human_xswamp");
	CHECK(hdatlas::stemInBiome("units_common_swamp", "forest") == "units_common_forest");
	// Whole tokens: "swamp" inside "xswamp" is not replaced, and the same biome stays.
	CHECK(hdatlas::stemInBiome("units_human_xswamp", "xswamp") == "units_human_xswamp");
	CHECK(hdatlas::stemInBiome("units_human_swamp", "swamp") == "units_human_swamp");
	// A race's common pages have no biome; an unknown map biome changes nothing.
	CHECK(hdatlas::stemInBiome("units_human_common", "swamp") == "units_human_common");
	CHECK(hdatlas::stemInBiome("units_human_forest", "") == "units_human_forest");
	CHECK(hdatlas::stemInBiome("units_human_forest", "wasteland") == "units_human_forest");
	CHECK(hdatlas::stemInBiome("", "swamp").empty());
}

TEST_CASE("hd_atlas runOrdered: a window of three lets a third page start, never a fourth")
{
	// The preload's list in small: a long page, a short one, a long one ... With a window of two
	// the third job waits for the first to be consumed; with three it starts beside the first.
	const int sleepMs[9] = {40, 5, 25, 1, 30, 2, 20, 1, 10};
	for (const int workers : {2, 3}) {
		std::mutex lock;
		std::vector<int> consumed;
		int alive = 0;
		int aliveMax = 0;
		int running = 0;
		int runningMax = 0;
		const int high = hdatlas::runOrdered<int>(
			9, workers, 3, 5,
			[&](int i) {
				{
					std::lock_guard<std::mutex> guard(lock);
					aliveMax = ++alive > aliveMax ? alive : aliveMax;
					runningMax = ++running > runningMax ? running : runningMax;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs[i]));
				std::lock_guard<std::mutex> guard(lock);
				--running;
				return i * 10;
			},
			[&](int i, int result) {
				CHECK(result == i * 10);
				std::this_thread::sleep_for(std::chrono::milliseconds(3));
				std::lock_guard<std::mutex> guard(lock);
				consumed.push_back(i);
				--alive;
			},
			[](int) {});
		CAPTURE(workers);
		CHECK(consumed == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8});
		CHECK(high == 3);      // job 1 ends while job 0 runs: job 2 is taken with two undelivered
		CHECK(aliveMax == 3);
		CHECK(alive == 0);
		CHECK(runningMax <= workers);
	}
}

TEST_CASE("atlas mask page: one pass gives the bytes of the two passes")
{
	// 16 x 16, every index once. Index 0 is NOT black, another index has its colour, two more
	// share one: the key is the index, not the colour.
	auto makePage = []() {
		SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 16, 16, 8, SDL_PIXELFORMAT_INDEX8);
		REQUIRE(s != nullptr);
		SDL_Color colors[256];
		for (int i = 0; i < 256; ++i) {
			colors[i].r = (Uint8) i;
			colors[i].g = (Uint8) ((i * 7 + 3) & 255);
			colors[i].b = (Uint8) (255 - i);
			colors[i].a = 255;
		}
		colors[0] = SDL_Color{200, 100, 50, 255};
		colors[17] = colors[0];
		colors[40] = colors[41];
		colors[255] = SDL_Color{255, 255, 255, 255};
		colors[1] = SDL_Color{0, 0, 0, 255};
		REQUIRE(SDL_SetPaletteColors(s->format->palette, colors, 0, 256) == 0);
		for (int y = 0; y < 16; ++y) {
			Uint8 *row = static_cast<Uint8 *>(s->pixels) + y * s->pitch;
			for (int x = 0; x < 16; ++x) {
				row[x] = (Uint8) (y * 16 + x);
			}
		}
		return s;
	};
	SDL_Surface *one = AtlasPageToRGBA(makePage(), true, false, true);
	SDL_Surface *two = AtlasPageToRGBA(makePage(), true, false, false);
	REQUIRE(one != nullptr);
	REQUIRE(two != nullptr);
	REQUIRE(one->format->format == SDL_PIXELFORMAT_RGBA32);
	REQUIRE(two->format->format == SDL_PIXELFORMAT_RGBA32);
	REQUIRE(one->w == 16);
	REQUIRE(one->h == 16);
	int differ = 0;
	for (int y = 0; y < 16; ++y) {
		const Uint8 *a = static_cast<const Uint8 *>(one->pixels) + y * one->pitch;
		const Uint8 *b = static_cast<const Uint8 *>(two->pixels) + y * two->pitch;
		differ += std::memcmp(a, b, 16 * 4) != 0;
	}
	CHECK(differ == 0);
	// the numbers themselves: white, alpha = luminance; index 0 clear whatever its colour
	auto at = [one](int index) {
		return static_cast<const Uint8 *>(one->pixels) + (index / 16) * one->pitch + (index % 16) * 4;
	};
	CHECK(at(0)[0] == 255);
	CHECK(at(0)[3] == 0);                                     // the key
	CHECK(at(1)[3] == 0);                                     // black
	CHECK(at(17)[3] == hdatlas::maskAlpha(200, 100, 50));     // the key's colour under another index
	CHECK(at(17)[3] != 0);
	CHECK(at(255)[3] == hdatlas::maskAlpha(255, 255, 255));
	CHECK(at(255)[1] == 255);
	CHECK(at(40)[3] == at(41)[3]);
	SDL_FreeSurface(one);
	SDL_FreeSurface(two);

	// A mask that is not 8-bit goes the old way under either flag.
	auto makeRgba = []() {
		SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 4, 1, 32, SDL_PIXELFORMAT_RGBA32);
		REQUIRE(s != nullptr);
		const Uint8 bytes[16] = {0, 0, 0, 255, 10, 20, 30, 255, 255, 255, 255, 0, 90, 90, 90, 7};
		std::memcpy(s->pixels, bytes, 16);
		return s;
	};
	SDL_Surface *r1 = AtlasPageToRGBA(makeRgba(), true, false, true);
	SDL_Surface *r2 = AtlasPageToRGBA(makeRgba(), true, false, false);
	REQUIRE(r1 != nullptr);
	REQUIRE(r2 != nullptr);
	CHECK(std::memcmp(r1->pixels, r2->pixels, 16) == 0);
	CHECK(static_cast<const Uint8 *>(r1->pixels)[7] == 18);
	SDL_FreeSurface(r1);
	SDL_FreeSurface(r2);
	CHECK(AtlasPageToRGBA(nullptr, true, false) == nullptr);
}

namespace
{

hdiconcache::Key iconKey()
{
	hdiconcache::Key key;
	key.width = 150;
	key.height = 24304;
	key.set = "forest";
	key.sources[0] = {41000, 1700000001};
	key.sources[1] = {9000000, 1700000002};
	key.sources[2] = {39000, 1700000003};
	key.sources[3] = {800000, 1700000004};
	key.pathHash = 0x1234567890abcdefULL;
	return key;
}

} // namespace

TEST_CASE("icon strip cache: the header answers for one key only")
{
	const hdiconcache::Key key = iconKey();
	const uint64_t pixels = (uint64_t) key.width * key.height * 4;
	const uint64_t fileSize = hdiconcache::HEADER_BYTES + pixels;
	const auto header = hdiconcache::encodeHeader(key, true, 0xfeedULL);
	bool team = false;
	uint64_t sum = 0;
	REQUIRE(hdiconcache::checkHeader(header.data(), header.size(), fileSize, key, team, sum));
	CHECK(team);
	CHECK(sum == 0xfeedULL);
	const auto plain = hdiconcache::encodeHeader(key, false, 1);
	REQUIRE(hdiconcache::checkHeader(plain.data(), plain.size(), fileSize, key, team, sum));
	CHECK_FALSE(team);

	auto refused = [&](const hdiconcache::Key &other) {
		bool t = true;
		uint64_t s = 77;
		const bool ok = hdiconcache::checkHeader(header.data(), header.size(), fileSize, other, t, s);
		CHECK(t);       // untouched on a refusal
		CHECK(s == 77);
		return !ok;
	};
	// every source file: another size, another time, gone
	for (int i = 0; i < hdiconcache::SOURCES; ++i) {
		hdiconcache::Key other = key;
		other.sources[i].size += 1;
		CHECK(refused(other));
		other = key;
		other.sources[i].mtime += 1;
		CHECK(refused(other));
		other = key;
		other.sources[i] = {};
		CHECK(refused(other));
	}
	hdiconcache::Key other = key;
	other.set = "ice";
	CHECK(refused(other));
	other = key;
	other.set = "fores";
	CHECK(refused(other));
	other = key;
	other.builder = key.builder + 1;   // a build with another builder
	CHECK(refused(other));
	other = key;
	other.height = key.height + 124;   // another layout
	CHECK(refused(other));
	other = key;
	other.width = 151;
	CHECK(refused(other));
	other = key;
	other.pathHash ^= 1;               // another game folder
	CHECK(refused(other));
	other = key;
	other.set = "";
	CHECK(refused(other));
	other = key;
	other.set = "a_set_name_too_long";
	CHECK(refused(other));

	// the file: cut short, grown, too short for a header
	CHECK_FALSE(hdiconcache::checkHeader(header.data(), header.size(), fileSize - 1, key, team, sum));
	CHECK_FALSE(hdiconcache::checkHeader(header.data(), header.size(), fileSize + 1, key, team, sum));
	CHECK_FALSE(hdiconcache::checkHeader(header.data(), header.size(), hdiconcache::HEADER_BYTES, key, team, sum));
	CHECK_FALSE(hdiconcache::checkHeader(header.data(), header.size() - 1, fileSize, key, team, sum));
	CHECK_FALSE(hdiconcache::checkHeader(nullptr, 0, fileSize, key, team, sum));
	// any one damaged byte of the header
	int accepted = 0;
	for (size_t i = 0; i < header.size(); ++i) {
		auto bad = header;
		bad[i] ^= 0x10;
		accepted += hdiconcache::checkHeader(bad.data(), bad.size(), fileSize, key, team, sum);
	}
	CHECK(accepted == 0);

	CHECK(hdiconcache::fileName("forest") == "hd_iconstrip_forest.bin");
	CHECK(hdiconcache::fileName("xswamp") == "hd_iconstrip_xswamp.bin");
	CHECK(hdiconcache::fileName("").empty());
	CHECK(hdiconcache::fileName("../forest").empty());
	CHECK(hdiconcache::fileName("a/b").empty());
	CHECK(hdiconcache::fileName("Forest").empty());

	// the sum sees a change anywhere, the tail under eight bytes included, and the order
	std::vector<unsigned char> block(1003);
	for (size_t i = 0; i < block.size(); ++i) {
		block[i] = (unsigned char) (i * 31 + 7);
	}
	const uint64_t whole = hdiconcache::payloadSum(block.data(), block.size());
	for (const size_t at : {(size_t) 0, (size_t) 7, (size_t) 8, (size_t) 500, (size_t) 999, (size_t) 1002}) {
		auto bad = block;
		bad[at] ^= 1;
		CHECK(hdiconcache::payloadSum(bad.data(), bad.size()) != whole);
	}
	auto swapped = block;
	std::swap(swapped[16], swapped[24]);
	CHECK(hdiconcache::payloadSum(swapped.data(), swapped.size()) != whole);
	// row by row = in one piece when a row is whole steps (the strip: 600 bytes a row)
	CHECK(hdiconcache::payloadSum(block.data() + 600, 400, hdiconcache::payloadSum(block.data(), 600))
	      == hdiconcache::payloadSum(block.data(), 1000));
}

TEST_CASE("icon strip cache: a written strip is read back byte for byte, a damaged file is not read")
{
	const fs::path dir = caseDir("iconstrip_cache");
	const fs::path file = dir / "sub" / hdiconcache::fileName("forest");
	hdiconcache::Key key = iconKey();
	key.width = 150;
	key.height = 124 * 3;
	SDL_Surface *strip = SDL_CreateRGBSurfaceWithFormat(0, 150, 124 * 3, 32, SDL_PIXELFORMAT_RGBA32);
	REQUIRE(strip != nullptr);
	for (int y = 0; y < strip->h; ++y) {
		Uint8 *row = static_cast<Uint8 *>(strip->pixels) + y * strip->pitch;
		for (int x = 0; x < strip->w * 4; ++x) {
			row[x] = (Uint8) (x * 13 + y * 7 + (x * y) % 5);
		}
	}
	bool team = false;
	CHECK(ReadHDIconStripCache(file, key, team) == nullptr);   // nothing there yet
	REQUIRE(WriteHDIconStripCache(file, key, strip, true));     // makes the directory
	CHECK(fs::file_size(file) == hdiconcache::HEADER_BYTES + (uintmax_t) 150 * 124 * 3 * 4);
	CHECK_FALSE(fs::exists(fs::path(file.string() + ".tmp")));

	SDL_Surface *back = ReadHDIconStripCache(file, key, team);
	REQUIRE(back != nullptr);
	CHECK(team);
	REQUIRE(back->format->format == SDL_PIXELFORMAT_RGBA32);
	REQUIRE(back->w == strip->w);
	REQUIRE(back->h == strip->h);
	int differ = 0;
	for (int y = 0; y < strip->h; ++y) {
		differ += std::memcmp(static_cast<const Uint8 *>(strip->pixels) + y * strip->pitch,
		                      static_cast<const Uint8 *>(back->pixels) + y * back->pitch, 150 * 4) != 0;
	}
	CHECK(differ == 0);
	SDL_FreeSurface(back);

	// another key: the game folder changed
	hdiconcache::Key other = key;
	other.sources[1].mtime += 1;
	team = false;
	CHECK(ReadHDIconStripCache(file, other, team) == nullptr);
	CHECK_FALSE(team);

	// the file's bytes
	std::vector<char> bytes;
	{
		std::ifstream in(file, std::ios::binary);
		bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
	auto put = [&](const std::vector<char> &data) {
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out.write(data.data(), (std::streamsize) data.size());
	};
	// one pixel byte changed: the size and the header hold, the sum does not
	auto bad = bytes;
	bad[hdiconcache::HEADER_BYTES + 150 * 4 * 200 + 3] ^= 0x40;
	put(bad);
	CHECK(ReadHDIconStripCache(file, key, team) == nullptr);
	// cut short
	bad.assign(bytes.begin(), bytes.end() - 600);
	put(bad);
	CHECK(ReadHDIconStripCache(file, key, team) == nullptr);
	bad.assign(bytes.begin(), bytes.begin() + 40);
	put(bad);
	CHECK(ReadHDIconStripCache(file, key, team) == nullptr);
	put({});
	CHECK(ReadHDIconStripCache(file, key, team) == nullptr);
	// a new write replaces the damaged file
	REQUIRE(WriteHDIconStripCache(file, key, strip, false));
	team = true;
	back = ReadHDIconStripCache(file, key, team);
	REQUIRE(back != nullptr);
	CHECK_FALSE(team);
	SDL_FreeSurface(back);

	// a strip of another size than the key says is not written
	hdiconcache::Key wrong = key;
	wrong.height += 124;
	CHECK_FALSE(WriteHDIconStripCache(dir / "other.bin", wrong, strip, true));
	CHECK_FALSE(fs::exists(dir / "other.bin"));
	CHECK_FALSE(WriteHDIconStripCache(dir / "other.bin", key, nullptr, true));
	// a place that cannot be written: false, nothing thrown, nothing left
	const fs::path blocked = dir / "blocked";
	{
		std::ofstream out(blocked);
		out << "x";
	}
	CHECK_FALSE(WriteHDIconStripCache(blocked / "strip.bin", key, strip, true));
	SDL_FreeSurface(strip);
}

TEST_CASE("hd_atlas preloadBaseStems: the atlases every map of a biome loads, local race first")
{
	// PRELOAD-PLAN (work/PRELOAD_IMPL.md): a forest map, an orc player against humans.
	const std::vector<std::string> both = hdatlas::preloadBaseStems("forest", {"orc", "human"});
	REQUIRE(both.size() == 5);
	CHECK(both[0] == "units_common_forest");
	CHECK(both[1] == "units_orc_common");
	CHECK(both[2] == "units_orc_forest");
	CHECK(both[3] == "units_human_common");
	CHECK(both[4] == "units_human_forest");
	// Wasteland maps are the "swamp" biome of the unit atlases; one race gives three stems.
	const std::vector<std::string> one = hdatlas::preloadBaseStems("swamp", {"human"});
	REQUIRE(one.size() == 3);
	CHECK(one[0] == "units_common_swamp");
	CHECK(one[2] == "units_human_swamp");
	// A race without atlases of its own and a repeated race add nothing.
	CHECK(hdatlas::preloadBaseStems("winter", {"neutral", "orc", "orc"}).size() == 3);
	// An unknown biome: nothing (the map's tileset is not known yet).
	CHECK(hdatlas::preloadBaseStems("", {"human"}).empty());
	CHECK(hdatlas::preloadBaseStems("wasteland", {"human"}).empty());
}

TEST_CASE("hd_atlas preloadExtraStems: the atlases the map's types need beyond the held ones")
{
	const std::vector<std::string> have = {"units_common_swamp", "units_human_common", "units_human_swamp"};
	// Types resolve mostly to held stems; a family the swamp atlas lacks stays in the forest one.
	const std::vector<std::string> wanted = {"units_human_common", "", "units_human_swamp",
	                                         "units_human_forest", "units_common_forest",
	                                         "units_human_forest", "units_human_common"};
	const std::vector<std::string> extra = hdatlas::preloadExtraStems(wanted, have);
	REQUIRE(extra.size() == 2);
	CHECK(extra[0] == "units_human_forest");
	CHECK(extra[1] == "units_common_forest");
	CHECK(hdatlas::preloadExtraStems({"units_human_common"}, have).empty());
	CHECK(hdatlas::preloadExtraStems({}, have).empty());
}

TEST_CASE("hd_atlas scriptPlayUnitIdents: the types a map's triggers create during play")
{
	// No trigger: the script's units are all created at load (planned from the map then).
	CHECK(hdatlas::scriptPlayUnitIdents("unit = CreateUnit(\"unit-footman\", 0, {1, 2})\n").empty());
	CHECK(hdatlas::scriptPlayUnitIdents("").empty());
	const std::string text =
		"unit = CreateUnit(\"unit-footman\", 0, {1, 2})\n"           // placed unit: skipped
		"unit = CreateUnit(\"unit-peasant\", 0, {3, 2})\n"
		"AddTrigger(function() return GameCycle > 100 end,\n"
		"  function()\n"
		"    CreateUnit(\"unit-grunt\", 1, {5, 5})\n"                 // created in play
		"    local u = CreateUnit( 'unit-ogre', 1, {6, 5})\n"         // other quote, a space
		"    CreateUnit(\"unit-grunt\", 1, {7, 5})\n"                 // repeat: once
		"    OldCreateUnit(\"unit-dragon\", 1, {8, 5})\n"
		"    CreateUnit(kind, 1, {9, 5})\n"                           // a variable: not readable
		"    CreateUnit(\"\", 1, {9, 5})\n"                           // empty: nothing
		"    return false end)\n";
	const std::vector<std::string> idents = hdatlas::scriptPlayUnitIdents(text);
	REQUIRE(idents.size() == 3);
	CHECK(idents[0] == "unit-grunt");
	CHECK(idents[1] == "unit-ogre");
	CHECK(idents[2] == "unit-dragon");
	// An unclosed literal at the end and one broken by a line end are not read.
	CHECK(hdatlas::scriptPlayUnitIdents("AddTrigger()\nCreateUnit(\"unit-gr").empty());
	CHECK(hdatlas::scriptPlayUnitIdents("AddTrigger()\nCreateUnit(\"unit-a\nb\", 1)").empty());
}

TEST_CASE("hd_atlas pngRgbaBytes: the decoded size of an atlas page from its PNG header")
{
	// units_human_common_sprites.png: 8161 x 7580 -> 247 446 520 bytes of RGBA (236 MiB).
	unsigned char head[24] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
	                          0, 0, 0x1F, 0xE1, 0, 0, 0x1D, 0x9C};
	CHECK(hdatlas::pngRgbaBytes(head, 24) == 8161ull * 7580ull * 4ull);
	CHECK(hdatlas::pngRgbaBytes(head, 23) == 0);
	CHECK(hdatlas::pngRgbaBytes(nullptr, 24) == 0);
	head[1] = 'X';
	CHECK(hdatlas::pngRgbaBytes(head, 24) == 0);
	head[1] = 'P';
	head[12] = 'J';
	CHECK(hdatlas::pngRgbaBytes(head, 24) == 0);
}

TEST_CASE("hd_atlas preload budget: a quarter of the RAM up to the cap; what does not fit waits")
{
	// The cap holds one biome's whole two-race set (measured 1040 - 1046 MiB).
	CHECK(hdatlas::HdPreloadBudgetCapMiB >= 1046);
	CHECK(hdatlas::hdPreloadBudgetMiB(0) == hdatlas::HdPreloadBudgetCapMiB);
	CHECK(hdatlas::hdPreloadBudgetMiB(6 * 1024) == hdatlas::HdPreloadBudgetCapMiB);
	CHECK(hdatlas::hdPreloadBudgetMiB(12 * 1024) == hdatlas::HdPreloadBudgetCapMiB);
	CHECK(hdatlas::hdPreloadBudgetMiB(4 * 1024) == 1024);
	const uint64_t MiB = 1024 * 1024;
	// 1043 MiB held, cap 1100: a 118 MiB building atlas does not fit, a 50 MiB one after it does.
	const std::vector<int> kept = hdatlas::fitPreloadBudget(1043 * MiB, {118 * MiB, 50 * MiB, 10 * MiB},
	                                                        1100 * MiB);
	REQUIRE(kept.size() == 1);
	CHECK(kept[0] == 1);
	// One race in play (559 MiB): two other-biome atlases fit, in order.
	const std::vector<int> two = hdatlas::fitPreloadBudget(559 * MiB, {118 * MiB, 100 * MiB}, 1100 * MiB);
	REQUIRE(two.size() == 2);
	CHECK(two[0] == 0);
	CHECK(two[1] == 1);
	// A page of unknown size is not loaded ahead.
	CHECK(hdatlas::fitPreloadBudget(0, {0}, 1100 * MiB).empty());
}

TEST_CASE("hd_atlas preload decode order: largest page first, equal sizes in list order")
{
	const uint64_t MiB = 1024 * 1024;
	// The winter two-race list as preloadBaseStems gives it (sprites, masks per stem), RGBA MiB.
	const std::vector<uint64_t> bytes = {94 * MiB, 1 * MiB,  236 * MiB, 28 * MiB, 85 * MiB,
	                                     9 * MiB,  232 * MiB, 38 * MiB, 85 * MiB, 4 * MiB};
	const std::vector<int> order = hdatlas::preloadDecodeOrder(bytes);
	const std::vector<int> expected = {2, 6, 0, 4, 8, 7, 3, 5, 9, 1};
	CHECK(order == expected); // 4 and 8 tie: list order kept
	CHECK(hdatlas::preloadDecodeOrder({}).empty());
	const std::vector<int> unknown = hdatlas::preloadDecodeOrder({0, 5, 0});
	CHECK(unknown == std::vector<int>{1, 0, 2});
}

TEST_CASE("hd_atlas preload decode workers: three only on a big device")
{
	CHECK(hdatlas::preloadDecodeWorkers(11 * 1024, 8) == 3); // S8U / S20+
	CHECK(hdatlas::preloadDecodeWorkers(7 * 1024, 6) == 3);
	CHECK(hdatlas::preloadDecodeWorkers(5600, 8) == 2);      // S7+ 6 GB
	CHECK(hdatlas::preloadDecodeWorkers(3 * 1024, 4) == 2);  // emulator
	CHECK(hdatlas::preloadDecodeWorkers(12 * 1024, 4) == 2);
	CHECK(hdatlas::preloadDecodeWorkers(0, 0) == 2);         // unknown
}
