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
/**@name test_hudcanvas.cpp - The test file for the in-game picture and HUD step. */
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

#include <algorithm>

#include "stratagus.h"
#include "video.h"

namespace
{
struct Canvas
{
	int x, y, w, h, step;
};

// The rule is read through the one function that owns it; the screen size is put back after.
Canvas CanvasFor(int width, int height)
{
	const int oldWidth = Video.Width;
	const int oldHeight = Video.Height;
	Video.Width = width;
	Video.Height = height;
	Video.ComputeHudCanvas();
	const Canvas c = {Video.HudCanvas.X, Video.HudCanvas.Y, Video.HudCanvas.W, Video.HudCanvas.H,
	                  Video.HudCanvas.Step};
	const double scale = Video.HudCanvas.Scale;
	CHECK(scale == doctest::Approx(c.step * 480.0 / 2160.0));
	Video.Width = oldWidth;
	Video.Height = oldHeight;
	Video.ComputeHudCanvas();
	return c;
}

void CheckCanvas(int width, int height, const Canvas &want)
{
	const Canvas got = CanvasFor(width, height);
	CAPTURE(width);
	CAPTURE(height);
	CHECK(got.x == want.x);
	CHECK(got.y == want.y);
	CHECK(got.w == want.w);
	CHECK(got.h == want.h);
	CHECK(got.step == want.step);
}
} // namespace

TEST_CASE("hud canvas: a screen between 4:3 and 16:9 is the whole picture at step 3")
{
	// The sizes the layout is fitted to; none of these numbers may change.
	CheckCanvas(2960, 1746, {0, 0, 2960, 1746, 3});
	CheckCanvas(2960, 1848, {0, 0, 2960, 1848, 3});
	CheckCanvas(2800, 1752, {0, 0, 2800, 1752, 3});
}

TEST_CASE("hud canvas: a 16:9 screen is the whole picture")
{
	CheckCanvas(1280, 720, {0, 0, 1280, 720, 1});
	CheckCanvas(1920, 1080, {0, 0, 1920, 1080, 2});
	CheckCanvas(2560, 1440, {0, 0, 2560, 1440, 3});
	CheckCanvas(3840, 2160, {0, 0, 3840, 2160, 4});
}

TEST_CASE("hud canvas: a screen wider than 16:9 is the whole width from the left edge")
{
	// WIDE-MAP-VIEW, an owner's deviation. The original keeps a centred 16:9
	// picture at these client sizes (1280 x 720 at x 220, 1920 x 1080 at x 320, 2560 x 1440 at
	// x 440); here the side bar is at x 0 and the map view runs to the right edge. The step is
	// the original's.
	CheckCanvas(1720, 720, {0, 0, 1720, 720, 1});
	CheckCanvas(2560, 1080, {0, 0, 2560, 1080, 2});
	CheckCanvas(3440, 1440, {0, 0, 3440, 1440, 3});
	// A 20:9 phone, as a whole screen and less its system bars.
	CheckCanvas(1600, 720, {0, 0, 1600, 720, 1});
	CheckCanvas(1551, 675, {0, 0, 1551, 675, 1});
	CheckCanvas(1552, 675, {0, 0, 1552, 675, 1});
}

TEST_CASE("hud canvas: no size up to 16:9 is touched by the wide rule")
{
	// The rule as it stood before the wide branch, for every width of the three heights the
	// layout is fitted to and of a 720-row screen, up to the 16:9 width.
	for (const int height : {675, 720, 1746, 1752, 1848}) {
		const int pw169 = (height * 32 + 9) / 18;
		for (int width = height; width <= pw169; ++width) {
			const int ph = std::min(height, (width * 6 + 4) / 8);
			const Canvas got = CanvasFor(width, height);
			if (got.x != 0 || got.w != width || got.h != ph || got.y != (height - ph) / 2
				|| got.step != std::max(1, height / 480)) {
				CAPTURE(width);
				CAPTURE(height);
				FAIL_CHECK("the canvas of a screen not wider than 16:9 has moved");
			}
		}
	}
}

TEST_CASE("hud canvas: the step is the whole number of 480-row layouts in the height")
{
	CHECK(CanvasFor(1706, 959).step == 1);
	CHECK(CanvasFor(1706, 960).step == 2);
	CHECK(CanvasFor(2560, 1439).step == 2);
	CHECK(CanvasFor(2560, 1440).step == 3);
	CHECK(CanvasFor(3414, 1919).step == 3);
	CHECK(CanvasFor(3414, 1920).step == 4);
	CHECK(CanvasFor(500, 400).step == 1);
}

TEST_CASE("hud canvas: a screen narrower than 4:3 keeps a centred 4:3 picture")
{
	CheckCanvas(1000, 1000, {0, 125, 1000, 750, 2});
	CheckCanvas(600, 900, {0, 225, 600, 450, 1});
}

/*----------------------------------------------------------------------------
--  Command tooltip: the string record of a button's target
----------------------------------------------------------------------------*/

#include "wc2strings.h"

namespace
{
// A table of the game's shape with made-up words: one name on two records of a family, the way
// the game repeats a name for the human / orc pair and for the two levels of an upgrade.
const char *const TipTable = "{"
	"\"unit_72\":\"Dock\",\"unit_73\":\"Dock\","
	"\"unit_72_requirement\":\"needs a hall and a mill\","
	"\"unit_73_requirement\":\"needs a den and a saw\","
	"\"unit_16\":\"Worker\",\"unit_2\":\"Worker\","
	"\"unit_58\":\"Field\","
	"\"upgrade_10\":\"Plates\",\"upgrade_8\":\"Plates\",\"upgrade_9\":\"Plates\""
	"}";

struct TipTableScope
{
	TipTableScope() { REQUIRE(LoadWC2StringsFromText(TipTable)); }
	~TipTableScope()
	{
		for (const char *ident : {"unit-a-dock", "unit-b-dock", "unit-a-worker"}) {
			SetWC2IdForIdent("unit", ident, -1);
		}
		for (const char *ident : {"upgrade-a-plates1", "upgrade-a-plates2", "upgrade-b-plates1"}) {
			SetWC2IdForIdent("upgrade", ident, -1);
		}
		LoadWC2StringsFromText("{}");
	}
};
} // namespace

TEST_CASE("tooltip record: a name on two records gives the first key in text order")
{
	const TipTableScope table;
	CHECK(GetWC2Id("unit", "Dock") == 72);
	CHECK(GetWC2Id("unit", "Worker") == 16);
	CHECK(GetWC2Id("upgrade", "Plates") == 10);
	CHECK(GetWC2Id("unit", "Field") == 58);
	CHECK(GetWC2Id("unit", "Nothing") == -1);
}

TEST_CASE("tooltip record: the ident wins over the display name")
{
	const TipTableScope table;
	SetWC2IdForIdent("unit", "unit-a-worker", 2);
	CHECK(GetWC2IdForIdent("unit", "unit-a-worker") == 2);
	CHECK(ResolveWC2Id("unit", "unit-a-worker", "Worker") == 2);
	// The name alone still gives what it gave.
	CHECK(GetWC2Id("unit", "Worker") == 16);
}

TEST_CASE("tooltip record: one display name with two idents gives two records")
{
	const TipTableScope table;
	SetWC2IdForIdent("unit", "unit-a-dock", 72);
	SetWC2IdForIdent("unit", "unit-b-dock", 73);
	const int a = ResolveWC2Id("unit", "unit-a-dock", "Dock");
	const int b = ResolveWC2Id("unit", "unit-b-dock", "Dock");
	CHECK(a == 72);
	CHECK(b == 73);
	CHECK(GetWC2String("unit_" + std::to_string(a) + "_requirement") == "needs a hall and a mill");
	CHECK(GetWC2String("unit_" + std::to_string(b) + "_requirement") == "needs a den and a saw");

	// Three records under one name, and no name on the objects at all.
	SetWC2IdForIdent("upgrade", "upgrade-a-plates1", 8);
	SetWC2IdForIdent("upgrade", "upgrade-a-plates2", 9);
	SetWC2IdForIdent("upgrade", "upgrade-b-plates1", 10);
	CHECK(GetWC2IdForIdent("upgrade", "upgrade-a-plates1") == 8);
	CHECK(GetWC2IdForIdent("upgrade", "upgrade-a-plates2") == 9);
	CHECK(GetWC2IdForIdent("upgrade", "upgrade-b-plates1") == 10);
}

TEST_CASE("tooltip record: an ident without an entry is looked up by its name as before")
{
	const TipTableScope table;
	CHECK(GetWC2IdForIdent("unit", "unit-b-dock") == -1);
	CHECK(ResolveWC2Id("unit", "unit-b-dock", "Dock") == GetWC2Id("unit", "Dock"));
	CHECK(ResolveWC2Id("unit", "unit-field", "Field") == 58);
	CHECK(ResolveWC2Id("unit", "unit-unknown", "Nothing") == -1);
	CHECK(ResolveWC2Id("unit", "", "Dock") == 72);
	// The families are separate: an entry of one is not seen from another.
	SetWC2IdForIdent("upgrade", "upgrade-a-plates1", 8);
	CHECK(GetWC2IdForIdent("unit", "upgrade-a-plates1") == -1);

	// Taking an entry away brings the name lookup back; an empty ident or family stores nothing.
	SetWC2IdForIdent("unit", "unit-b-dock", 73);
	CHECK(ResolveWC2Id("unit", "unit-b-dock", "Dock") == 73);
	SetWC2IdForIdent("unit", "unit-b-dock", -1);
	CHECK(ResolveWC2Id("unit", "unit-b-dock", "Dock") == 72);
	SetWC2IdForIdent("unit", "", 5);
	SetWC2IdForIdent("", "unit-b-dock", 5);
	CHECK(GetWC2IdForIdent("unit", "") == -1);
	CHECK(GetWC2IdForIdent("", "unit-b-dock") == -1);
}

TEST_CASE("tooltip record: the registry does not depend on the loaded strings")
{
	{
		const TipTableScope table;
		SetWC2IdForIdent("unit", "unit-keeps", 73);
		REQUIRE(LoadWC2StringsFromText("{\"unit_1\":\"Other\"}"));
		CHECK(GetWC2IdForIdent("unit", "unit-keeps") == 73);
		CHECK(GetWC2Id("unit", "Dock") == -1);
		// A text that is not an object leaves the table as it was.
		CHECK_FALSE(LoadWC2StringsFromText("[1,2"));
		CHECK(GetWC2Id("unit", "Other") == 1);
	}
	SetWC2IdForIdent("unit", "unit-keeps", -1);
	CHECK(GetWC2IdForIdent("unit", "unit-keeps") == -1);
	CHECK(GetWC2StringCount() == 0);
}
