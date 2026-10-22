// Classic view, calibration against run 56 of the original (agent-orig-56/ORIG56.md U1, U2, U4;
// note work/CLASSIC_CALIB56.md). Pure checks of hd_atlas.h: the 4:3 box and the step at the 12
// measured client sizes, the tablets, the side column's top piece, the player colour runs.

#include <doctest.h>
#include <cstdlib>
#include <string>
#include "video.h"

namespace
{
struct Measured {
	int w, h;            // client
	int x, y, bw, bh;    // classic box, client pixels (exact extent of the non-black pixels)
	int s;               // HUD + world scale
};

// ORIG56 U1, edges re-read pixel-exact from the captures (any channel > 0).
const Measured Run56[] = {
	{1024, 768, 0, 0, 1024, 768, 1},    {1280, 720, 160, 0, 960, 720, 1},
	{1366, 768, 171, 0, 1024, 768, 1},  {1366, 800, 150, 0, 1067, 800, 1},
	{1800, 780, 380, 0, 1040, 780, 1},  {1366, 900, 83, 0, 1200, 900, 1},
	{1366, 960, 43, 0, 1280, 960, 2},   {1024, 1000, 0, 116, 1024, 768, 2},
	{1920, 1080, 240, 0, 1440, 1080, 2}, {1300, 1100, 0, 62, 1300, 977, 2},
	{1600, 1200, 0, 0, 1600, 1200, 2},  {2560, 1600, 213, 0, 2135, 1600, 3},
};
}

TEST_CASE("classic calib56: the step is floor(client height / 480), width never counts")
{
	for (const Measured &m : Run56) {
		CAPTURE(m.w);
		CAPTURE(m.h);
		CHECK(hdatlas::classicStep(m.h) == m.s);
		CHECK(hdatlas::classicScreen(m.w, m.h).s == m.s);
	}
	// 1024 x 1000: the box is only 768 high, the step is 2 all the same.
	CHECK(hdatlas::classicScreen(1024, 1000).box.h == 768);
	CHECK(hdatlas::classicScreen(1024, 1000).s == 2);
}

TEST_CASE("classic calib56: the box is the largest centred 4:3 rect, within 2 px of every edge")
{
	for (const Measured &m : Run56) {
		CAPTURE(m.w);
		CAPTURE(m.h);
		const hdatlas::ClassicRect b = hdatlas::classicBox(m.w, m.h);
		CHECK(std::abs(b.x - m.x) <= 1);
		CHECK(std::abs(b.y - m.y) <= 1);
		CHECK(std::abs((b.x + b.w) - (m.x + m.bw)) <= 2);
		CHECK(std::abs((b.y + b.h) - (m.y + m.bh)) <= 2);
		// Fits, and one side fills the client.
		CHECK(b.x >= 0);
		CHECK(b.y >= 0);
		CHECK(b.x + b.w <= m.w);
		CHECK(b.y + b.h <= m.h);
		CHECK((b.w == m.w || b.h == m.h));
	}
	// Where 4:3 is a whole number of pixels the box is exact (9 of 12).
	CHECK(hdatlas::classicBox(1280, 720).x == 160);
	CHECK(hdatlas::classicBox(1024, 1000).y == 116);
	CHECK(hdatlas::classicBox(1800, 780).x == 380);
	CHECK(hdatlas::classicBox(1366, 960).x == 43);
}

TEST_CASE("classic calib56: the tablets (S8U, S7+) get step 3")
{
	// S8U full panel 2960 x 1848, and the 2960 x 1746 surface of the device runs.
	const hdatlas::ClassicScreen s8 = hdatlas::classicScreen(2960, 1848);
	CHECK(s8.s == 3);
	CHECK(s8.box.x == 248);
	CHECK(s8.box.y == 0);
	CHECK(s8.box.w == 2464);
	CHECK(s8.box.h == 1848);
	CHECK(s8.lw == 821);
	CHECK(s8.lh == 616);
	const hdatlas::ClassicScreen s8bar = hdatlas::classicScreen(2960, 1746);
	CHECK(s8bar.s == 3);
	CHECK(s8bar.box.x == 316);
	CHECK(s8bar.box.w == 2328);
	CHECK(s8bar.lw == 776);
	CHECK(s8bar.lh == 582);
	// S7+ panel 2800 x 1752.
	const hdatlas::ClassicScreen s7 = hdatlas::classicScreen(2800, 1752);
	CHECK(s7.s == 3);
	CHECK(s7.box.x == 232);
	CHECK(s7.box.w == 2336);
	CHECK(s7.box.h == 1752);
	CHECK(s7.lw == 778);
	CHECK(s7.lh == 584);
}

TEST_CASE("classic calib56: side column TopLeft / TopLeftAlt + TileableLeft + BottomLeft")
{
	hdatlas::ClassicHudPiece p[hdatlas::ClassicHudPieceMax];
	// 1366 x 900 at step 1: logical 1200 x 900.
	const int n = hdatlas::classicHudFramePieces(1200, 900, p);
	REQUIRE(n > 3);
	CHECK(std::string(p[0].frame) == "TopLeft");
	CHECK(p[0].h == 464);
	// TileableLeft from row 464, cut at the foot; BottomLeft in the last 16 rows.
	int y = 464;
	int i = 1;
	for (; std::string(p[i].frame) == "TileableLeft"; ++i) {
		CHECK(p[i].y == y);
		y += p[i].h;
	}
	CHECK(y == 900 - 16);
	CHECK(std::string(p[i].frame) == "BottomLeft");
	CHECK(p[i].y == 900 - 16);

	// A unit selected: the same layout, the top piece is TopLeftAlt.
	hdatlas::ClassicHudPiece q[hdatlas::ClassicHudPieceMax];
	REQUIRE(hdatlas::classicHudFramePieces(1200, 900, q, true) == n);
	CHECK(std::string(q[0].frame) == "TopLeftAlt");
	CHECK(q[0].h == p[0].h);
	for (int k = 1; k < n; ++k) {
		CHECK(std::string(q[k].frame) == p[k].frame);
	}
}

TEST_CASE("classic calib56: player colour runs, 4 entries each from 208")
{
	// Measured live: blue 212..215, green 216..219, violet 220..223 (the atlas carries red 208..211).
	CHECK(hdatlas::classicRampStart("blue") == 212);
	CHECK(hdatlas::classicRampStart("green") == 216);
	CHECK(hdatlas::classicRampStart("violet") == 220);
	// The rule they fit: run = 208 + 4 * colour slot (red, blue, green, violet, orange, black, white).
	const char *const order[] = {"red", "blue", "green", "violet", "orange", "black", "white"};
	for (int i = 0; i < 7; ++i) {
		CHECK(hdatlas::classicRampStart(order[i]) == 208 + 4 * i);
	}
	// Yellow is not in 208..235: UNKNOWN live, 12..15 from TEAM_COLOURS.md.
	CHECK(hdatlas::classicRampStart("yellow") == 12);
}
