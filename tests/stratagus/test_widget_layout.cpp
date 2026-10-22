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
#include "video.h"
#include "widgets.h"
#include "nineslice.h"

#include <guisan/focushandler.hpp>
#include <guisan/keyevent.hpp>
#include <guisan/mouseevent.hpp>
#include <algorithm>
#include <cmath>
#include <memory>

TEST_CASE("ImageTextField centres the text line in the field")
{
	// Remastered save window: field 119 high (save-menu-3440x1440.png)
	CHECK(ImageTextFieldTextY(119, 31) == 44);
	CHECK(ImageTextFieldTextY(64, 31) == 16);
	// the line never starts above the field
	CHECK(ImageTextFieldTextY(10, 31) == 0);
	CHECK(ImageTextFieldTextY(31, 31) == 0);
}

TEST_CASE("ImageTextField: text inset and cursor as the original's Save name field (ORIG46)")
{
	// 9 px inside the black edge at a 40 px field (1366 x 768), 7 at 33 (1100), 11 at 46 (1800)
	CHECK(ImageTextFieldTextX(40) == 9);
	CHECK(ImageTextFieldTextX(33) == 7);
	CHECK(ImageTextFieldTextX(46) == 10);
	CHECK(ImageTextFieldTextX(2) == 1);
	// the bar: about 2/3 of the field, centred, a small gap after the text
	const ImageTextFieldCaret c = ImageTextFieldCaretAt(40, 50);
	CHECK(c.height == 27);
	CHECK(c.top == 6);
	CHECK(c.x == 52);
	CHECK(ImageTextFieldCaretAt(33, 0).height == 22);
	CHECK(ImageTextFieldCaretAt(1, 0).height == 1);
}

TEST_CASE("ImageTextField: a tap places the caret under the glyph drawn there (text inset)")
{
	// the pointer -> text x mapping is the draw's origin turned round
	CHECK(ImageTextFieldTextOffsetAt(40, 0, 9) == 0);
	CHECK(ImageTextFieldTextOffsetAt(40, 0, 30) == 21);
	CHECK(ImageTextFieldTextOffsetAt(40, 12, 30) == 33); // scrolled text
	CHECK(ImageTextFieldTextOffsetAt(40, 0, 3) == 0);    // on the frame, before the text

	ImageTextField field("abcdef");
	field.setSize(300, 40);
	gcn::Font *font = field.getFont();
	const int inset = ImageTextFieldTextX(40);
	const auto press = [&](int x) {
		gcn::MouseEvent e(&field, &field, false, false, false, false, gcn::MouseEvent::Pressed,
		                  gcn::MouseEvent::Left, x, 20, 1);
		field.mousePressed(e);
	};
	// one pixel into the third glyph as drawn: the caret goes before it
	press(inset + font->getWidth("ab") + 1);
	CHECK(field.getCaretPosition() == 2);
	// the same x read from the widget's edge (as before) would have counted the inset as text
	CHECK(font->getStringIndexAt("abcdef", inset + font->getWidth("ab") + 1) > 2);
	// a tap on the frame left of the text puts the caret at the start
	press(inset / 2);
	CHECK(field.getCaretPosition() == 0);
	// past the text: at its end
	press(inset + font->getWidth("abcdef") + 20);
	CHECK(field.getCaretPosition() == 6);

	// a drag from the second glyph to the fifth selects glyphs 1..4 with the same origin
	press(inset + font->getWidth("a") + 1);
	gcn::MouseEvent drag(&field, &field, false, false, false, false, gcn::MouseEvent::Dragged,
	                     gcn::MouseEvent::Left, inset + font->getWidth("abcd") + 1, 20, 1);
	field.mouseDragged(drag);
	unsigned first = 0;
	unsigned len = 0;
	field.getTextSelectionPositions(&first, &len);
	CHECK(first == 1);
	CHECK(len == 3);
}

TEST_CASE("ImageTextField art: nine-sliced at the drawing's scale (skins.json widget_edit)")
{
	// the baked field art 1008 x 64, "9slice" [32, 20, 32, 15]; the Save name field 1346 x 119
	// logical drawn at 720 / 2160 (S20+, LEN(1346) = 449, LEN(119) = 40)
	NineSlicePatch p[9];
	const SDL_Rect dest = {100, 50, 449, 40};
	ComputeNineSliceScaled(1008, 64, 32, 20, 32, 15, 720.0 / 2160.0, dest, p);
	// the frame at 1/3 of its sprite size: top 7, bottom 5, sides 11
	CHECK(p[0].dst.w == 11);
	CHECK(p[0].dst.h == 7);
	CHECK(p[8].dst.h == 5);
	CHECK(p[8].dst.w == 11);
	// the inside takes the rest: 28 of the 40 rows (the whole-art stretch gave 40 * 29 / 64 = 18)
	CHECK(p[4].dst.h == 28);
	CHECK(p[4].dst.w == 449 - 22);
	CHECK(p[4].src.x == 32);
	CHECK(p[4].src.y == 20);
	CHECK(p[4].src.h == 29);
	// the patches tile the field exactly
	CHECK(p[0].dst.x == 100);
	CHECK(p[0].dst.y == 50);
	CHECK(p[8].dst.x + p[8].dst.w == 549);
	CHECK(p[8].dst.y + p[8].dst.h == 90);
	int area = 0;
	for (const NineSlicePatch &q : p) {
		area += q.dst.w * q.dst.h;
	}
	CHECK(area == 449 * 40);
	// at 768 rows (the original's 1366 x 768 measurement): frame 7 / 5 px of a 42 px field
	ComputeNineSliceScaled(1008, 64, 32, 20, 32, 15, 768.0 / 2160.0, SDL_Rect{0, 0, 479, 42}, p);
	CHECK(p[0].dst.h == 7);
	CHECK(p[6].dst.h == 5);
	// scale 1 at the art's own height is the art itself vertically
	ComputeNineSliceScaled(1008, 64, 32, 20, 32, 15, 1.0, SDL_Rect{0, 0, 1008, 64}, p);
	CHECK(p[4].dst.h == 29);
	CHECK(p[4].dst.w == 944);
	// a field smaller than its frame: the frame shrinks to fit, nothing outside
	ComputeNineSliceScaled(1008, 64, 32, 20, 32, 15, 1.0, SDL_Rect{0, 0, 40, 20}, p);
	CHECK(p[0].dst.h + p[6].dst.h == 20);
	CHECK(p[0].dst.w + p[2].dst.w == 40);
	CHECK(p[4].dst.h == 0);
}

TEST_CASE("ImageTextField art: the sprite at its own size keeps its whole middle (textinput_normal 252 x 126)")
{
	// Modern_Widgets_<race>.json textinput_normal 252 x 126, "9slice" [32, 20, 32, 15]; the Save
	// name field 449 x 40 at 720 / 2160 (S20+)
	NineSlicePatch p[9];
	const SDL_Rect dest = {100, 50, 449, 40};
	ComputeNineSliceScaled(252, 126, 32, 20, 32, 15, 720.0 / 2160.0, dest, p);
	// the frame as with the 1008 x 64 picture: top 7, bottom 5, sides 11; inside 28 rows
	CHECK(p[0].dst.w == 11);
	CHECK(p[0].dst.h == 7);
	CHECK(p[8].dst.w == 11);
	CHECK(p[8].dst.h == 5);
	CHECK(p[4].dst.h == 28);
	CHECK(p[4].dst.w == 449 - 22);
	// the inside is drawn from the sprite's whole 91-row middle (the 1008 x 64 picture had 29)
	CHECK(p[4].src.x == 32);
	CHECK(p[4].src.y == 20);
	CHECK(p[4].src.w == 252 - 64);
	CHECK(p[4].src.h == 91);
	// the bottom corner is the sprite's own last 15 rows
	CHECK(p[8].src.y == 111);
	CHECK(p[8].src.h == 15);
	int area = 0;
	for (const NineSlicePatch &q : p) {
		area += q.dst.w * q.dst.h;
	}
	CHECK(area == 449 * 40);
	// at the logical size (scale 1, 2160 rows: LEN(119) = 119) the sprite's middle is drawn at
	// 84 of 91 rows, not stretched from 29
	ComputeNineSliceScaled(252, 126, 32, 20, 32, 15, 1.0, SDL_Rect{0, 0, 1346, 119}, p);
	CHECK(p[0].dst.h == 20);
	CHECK(p[6].dst.h == 15);
	CHECK(p[4].dst.h == 84);
	CHECK(p[4].src.h == 91);
}

TEST_CASE("text field: the soft keyboard's letters beyond Latin-1 are typed in as UTF-8")
{
	CHECK(TextFieldTakesCharacter('a'));
	CHECK(TextFieldTakesCharacter(' '));
	CHECK(TextFieldTakesCharacter(0xE9));  // e-acute
	CHECK(TextFieldTakesCharacter(0x105)); // a-ogonek
	CHECK(TextFieldTakesCharacter(0x17E)); // z-caron
	CHECK_FALSE(TextFieldTakesCharacter('\t'));
	CHECK_FALSE(TextFieldTakesCharacter(127));
	CHECK_FALSE(TextFieldTakesCharacter(0x85));
	CHECK_FALSE(TextFieldTakesCharacter(0xD800));
	CHECK_FALSE(TextFieldTakesCharacter(gcn::Key::Left)); // the special keys are negative
	CHECK_FALSE(TextFieldTakesCharacter(gcn::Key::Backspace));

	CTextField field("game");
	field.setCaretPosition(4);
	const auto type = [&](int value) {
		gcn::KeyEvent event(&field, &field, false, false, false, false, gcn::KeyEvent::Pressed, false,
		                    gcn::Key(value));
		field.keyPressed(event);
	};
	type(0x105);
	type(0x17E);
	CHECK(field.getText() == "game\xC4\x85\xC5\xBE");
	type(gcn::Key::Backspace); // one letter, both of its bytes
	CHECK(field.getText() == "game\xC4\x85");
}

TEST_CASE("text field: only a tapped, focused field in the window on top wants the keyboard")
{
	gcn::FocusHandler focus;
	gcn::Container top;
	top._setFocusHandler(&focus);
	gcn::Container other;
	auto field = std::make_unique<CTextField>("game.sav");
	top.add(field.get());
	field->setSize(100, 20);

	// focused by the window's script only (the Save window opens so): no keyboard
	field->requestFocus();
	CHECK(TextFieldWantingKeyboard(&top) == nullptr);

	const unsigned taps = TextFieldTapCount();
	gcn::MouseEvent tap(field.get(), field.get(), false, false, false, false, gcn::MouseEvent::Pressed,
	                    gcn::MouseEvent::Left, 5, 5, 1);
	field->mousePressed(tap);
	CHECK(TextFieldTapCount() == taps + 1);
	CHECK(TextFieldWantingKeyboard(&top) == field.get());
	// not when another window is on top, nor with no window
	CHECK(TextFieldWantingKeyboard(&other) == nullptr);
	CHECK(TextFieldWantingKeyboard(nullptr) == nullptr);
	// not while hidden or read-only
	top.setVisible(false);
	CHECK(TextFieldWantingKeyboard(&top) == nullptr);
	top.setVisible(true);
	field->setEditable(false);
	CHECK(TextFieldWantingKeyboard(&top) == nullptr);
	field->setEditable(true);
	CHECK(TextFieldWantingKeyboard(&top) == field.get());
	// not once the focus is gone
	focus.focusNone();
	CHECK(TextFieldWantingKeyboard(&top) == nullptr);
	field->requestFocus();
	CHECK(TextFieldWantingKeyboard(&top) == field.get());
	// a field that goes away is forgotten
	top.remove(field.get());
	field.reset();
	CHECK(TextFieldWantingKeyboard(&top) == nullptr);
}

TEST_CASE("ImageTextField art: the focused art only while the field has keyboard focus (textinput_active)")
{
	// the choice of picture
	CHECK(ImageTextFieldUsesFocusedArt(true, true));
	CHECK_FALSE(ImageTextFieldUsesFocusedArt(true, false));
	CHECK_FALSE(ImageTextFieldUsesFocusedArt(false, true));
	CHECK_FALSE(ImageTextFieldUsesFocusedArt(false, false));

	// the focus the draw asks for: a script's requestFocus (the Save window opens so) focuses the
	// field without raising the keyboard; a tap keeps the keyboard flow of CTextField
	gcn::FocusHandler focus;
	gcn::Container top;
	top._setFocusHandler(&focus);
	auto field = std::make_unique<ImageTextField>("game.sav");
	top.add(field.get());
	field->setSize(449, 40);
	CHECK_FALSE(field->isFocused());
	field->requestFocus();
	CHECK(field->isFocused());
	CHECK(TextFieldWantingKeyboard(&top) == nullptr);
	const unsigned taps = TextFieldTapCount();
	gcn::MouseEvent tap(field.get(), field.get(), false, false, false, false, gcn::MouseEvent::Pressed,
	                    gcn::MouseEvent::Left, 20, 20, 1);
	field->mousePressed(tap);
	CHECK(TextFieldTapCount() == taps + 1);
	CHECK(TextFieldWantingKeyboard(&top) == field.get());
	CHECK(field->isFocused());
	focus.focusNone();
	CHECK_FALSE(field->isFocused());
	CHECK(TextFieldWantingKeyboard(&top) == nullptr);
	top.remove(field.get());
}

namespace
{
/// Device-pixel picture of a BLQ55 line in the Save name field: the field LEN(119) high
/// (save.lua, RM.FIT() = H / 2160), the face's cell scaled as CFont::Load does it (lround of the
/// authored cell times the uniform scale), the line placed by ImageTextFieldTextY. Ink rows are the
/// FontBaker bake's (launcher capture baked_blq55*.png): `remaster` cell 102, H rows 29..72, x
/// rows 42..73; `remaster_small` cell 88, H rows 25..61, x rows 36..62.
struct SaveFieldLine
{
	int fieldH;
	int lastInkRow; ///< last ink row of a capital, from the field's top row
	double capH;    ///< cap height in device px
	double xH;      ///< x-height in device px
};

SaveFieldLine SaveFieldLineAt(int w, int h, int cell, int capTop, int capBottom, int xTop)
{
	const double s = std::min(w / 3840.0, h / 2160.0);
	SaveFieldLine l;
	l.fieldH = (int) std::floor(119.0 * h / 2160.0 + 0.5);
	const int fontH = std::max(1, (int) std::lround(cell * s));
	l.lastInkRow = ImageTextFieldTextY(l.fieldH, fontH) + (int) std::floor((capBottom + 0.5) * s);
	l.capH = (capBottom - capTop + 1) * s;
	l.xH = (capBottom - xTop + 1) * s;
	return l;
}
} // namespace

TEST_CASE("ImageTextField: the Save field's `remaster` face sits and measures as the original's (ORIG47)")
{
	// ORIG47 glyph boxes, rows from the field's first black row (188 at 1366 x 768, 221 at
	// 1800 x 900) to its last (230 / 270): capital ink ends on row 29 / 33, i.e. 13 / 16 rows above
	// the field's last row; cap height 15 / 18 px, x-height 12 / 13 px.
	struct Orig { int w, h, lastInkFromTop, lastInkFromBottom, cap, x; };
	for (const Orig o : {Orig{1366, 768, 29, 13, 15, 12}, Orig{1800, 900, 33, 16, 18, 13}}) {
		const SaveFieldLine big = SaveFieldLineAt(o.w, o.h, 102, 29, 72, 42);
		CAPTURE(o.w);
		CHECK(std::abs(big.lastInkRow - o.lastInkFromTop) <= 1);
		CHECK(std::abs((big.fieldH - 1 - big.lastInkRow) - o.lastInkFromBottom) <= 1);
		CHECK(std::abs(big.capH - o.cap) <= 1.0);
		CHECK(std::abs(big.xH - o.x) <= 1.0);
		// the list face it replaces is two to three px short of the original's capitals
		const SaveFieldLine small = SaveFieldLineAt(o.w, o.h, 88, 25, 61, 36);
		CHECK(o.cap - small.capH > 1.5);
		CHECK(o.x - small.xH > 1.5);
	}
}
