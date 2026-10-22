// Classic view, slice 1 (CLASSIC_S1): the hit rect of the HUD's F5 slot in the classic box, as
// ClassicF5SlotContains (src/ui/ui.cpp) reads it: hdatlas::classicFromScreen, then
// classichud::contains(classichud::F5Face). Measured: logical 4,2 40 x 20 (S3B_NOTE, the `f5`
// sprite 108 / 108 pixel-equal); screen 328..447 x 6..65 at 2960 x 1746 (S4_REST_SPEC W1).

#include <doctest.h>
#include "video.h"
#include "../../src/video/hd_classic_hud.h"

namespace
{
bool f5At(const hdatlas::ClassicScreen &c, int sx, int sy)
{
	int lx = 0;
	int ly = 0;
	return hdatlas::classicFromScreen(c, sx, sy, lx, ly) && classichud::contains(classichud::F5Face, lx, ly);
}
}

TEST_CASE("classichud: the F5 slot's hit rect at step 1 and 3")
{
	CHECK(classichud::F5Face.x == 4);
	CHECK(classichud::F5Face.y == 2);
	CHECK(classichud::F5Face.w == 40);
	CHECK(classichud::F5Face.h == 20);
	// The slot and the Menu button do not share a pixel.
	CHECK(classichud::F5Face.x + classichud::F5Face.w <= hdatlas::ClassicMenuX);

	// Step 1 (1366 x 768, box at 171): screen 175..214 x 2..21.
	const hdatlas::ClassicScreen one = hdatlas::classicScreen(1366, 768);
	REQUIRE(one.s == 1);
	REQUIRE(one.box.x == 171);
	CHECK(f5At(one, 175, 2));
	CHECK(f5At(one, 214, 21));
	CHECK(f5At(one, 192, 12)); // the click point of the original's capture (client 192,12)
	CHECK_FALSE(f5At(one, 174, 2));
	CHECK_FALSE(f5At(one, 215, 2));
	CHECK_FALSE(f5At(one, 175, 1));
	CHECK_FALSE(f5At(one, 175, 22));

	// Step 3 (2960 x 1746, box at 316): screen 328..447 x 6..65.
	const hdatlas::ClassicScreen big = hdatlas::classicScreen(2960, 1746);
	REQUIRE(big.s == 3);
	REQUIRE(big.box.x == 316);
	const hdatlas::ClassicRect r = hdatlas::classicToScreen(big, classichud::F5Face.x, classichud::F5Face.y,
	                                                         classichud::F5Face.w, classichud::F5Face.h);
	CHECK(r.x == 328);
	CHECK(r.y == big.box.y + 6);
	CHECK(r.w == 120);
	CHECK(r.h == 60);
	CHECK(f5At(big, 328, big.box.y + 6));
	CHECK(f5At(big, 447, big.box.y + 65));
	CHECK_FALSE(f5At(big, 327, big.box.y + 6));
	CHECK_FALSE(f5At(big, 448, big.box.y + 6));
	CHECK_FALSE(f5At(big, 328, big.box.y + 66));
	// Outside the box nothing is the slot.
	CHECK_FALSE(f5At(big, 10, 10));
}
