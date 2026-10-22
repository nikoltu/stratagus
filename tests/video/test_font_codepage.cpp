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
/**@name test_font_codepage.cpp - Which sheet slot a character is drawn from. */
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

#include "font.h"

#include <string>
#include <vector>

using Ix = std::vector<int>;

static Ix Ascii(const std::string &s)
{
	return Ix(s.begin(), s.end());
}

TEST_CASE("font codepage: plain ASCII is the same in every page")
{
	for (int page : {437, 866, 1252, FontCodePageHdSheet}) {
		CHECK(FontCodepageIndices("Build 3 Farms ~!", page) == Ascii("Build 3 Farms ~!"));
	}
}

TEST_CASE("font codepage: the HD sheet draws Lithuanian letters and marks from 0x80..0x9A")
{
	const int hd = FontCodePageHdSheet;
	// A a C c E e E. e. I i S s U u U- u- Z z, capital then small
	CHECK(FontCodepageIndices("\xc4\x84\xc4\x85\xc4\x8c\xc4\x8d\xc4\x98\xc4\x99\xc4\x96\xc4\x97\xc4\xae\xc4\xaf"
	                          "\xc5\xa0\xc5\xa1\xc5\xb2\xc5\xb3\xc5\xaa\xc5\xab\xc5\xbd\xc5\xbe", hd)
	      == Ix({0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	             0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91}));
	// en dash, em dash, quotes ' ' , " " low ", ellipsis
	CHECK(FontCodepageIndices("\xe2\x80\x93\xe2\x80\x94\xe2\x80\x98\xe2\x80\x99\xe2\x80\x9a\xe2\x80\x9c"
	                          "\xe2\x80\x9d\xe2\x80\x9e\xe2\x80\xa6", hd)
	      == Ix({0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a}));
	// "Pergale!" with e-dot
	CHECK(FontCodepageIndices("Pergal\xc4\x97!", hd) == Ix({'P', 'e', 'r', 'g', 'a', 'l', 0x87, '!'}));
}

TEST_CASE("font codepage: the HD sheet is Latin-1 above 0xA0 (e acute, a umlaut, n tilde, sharp s)")
{
	CHECK(FontCodepageIndices("\xc3\xa9\xc3\xa4\xc3\xb1\xc3\x9f\xc3\x89\xc2\xa9", FontCodePageHdSheet)
	      == Ix({0xe9, 0xe4, 0xf1, 0xdf, 0xc9, 0xa9}));
}

TEST_CASE("font codepage: CP437 keeps its own slots, and gives Lithuanian letters their ASCII")
{
	// e acute, a umlaut, sharp s in CP437 (the classic .FNT faces)
	CHECK(FontCodepageIndices("\xc3\xa9\xc3\xa4\xc3\x9f", 437) == Ix({0x82, 0x84, 0xe1}));
	// "s" with caron, "z" with caron, ellipsis, low quote: ASCII instead of '?'
	CHECK(FontCodepageIndices("\xc5\xa1\xc5\xbe\xe2\x80\xa6\xe2\x80\x9e", 437) == Ascii("sz...\""));
}

TEST_CASE("font codepage: the HD sheet draws the Polish L-stroke and the euro sign from 0x9B..0x9D")
{
	// capital L-stroke U+0141, small l-stroke U+0142, euro U+20AC: the slots right after the ellipsis
	CHECK(FontCodepageIndices("\xc5\x81\xc5\x82\xe2\x82\xac", FontCodePageHdSheet) == Ix({0x9b, 0x9c, 0x9d}));
	// "5 EUR" and "zl" (z, l-stroke) on the HD sheet
	CHECK(FontCodepageIndices("5\xe2\x82\xac", FontCodePageHdSheet) == Ix({'5', 0x9d}));
	CHECK(FontCodepageIndices("z\xc5\x82", FontCodePageHdSheet) == Ix({'z', 0x9c}));
	// other pages draw the ASCII stand-in, not '?'
	CHECK(FontCodepageIndices("\xc5\x81\xc5\x82\xe2\x82\xac", 437) == Ascii("LlEUR"));
	CHECK(FontCodepageIndices("\xc5\x82\xe2\x82\xac", 1252) == Ascii("lEUR"));
}

TEST_CASE("font codepage: Cyrillic draws its own ASCII stand-in outside code page 866")
{
	// "Mir" in Russian (U+041C U+0438 U+0440): CP866 has the letters
	const char *mir = "\xd0\x9c\xd0\xb8\xd1\x80";
	CHECK(FontCodepageIndices(mir, 866) == Ix({0x8c, 0xa8, 0xe0}));
	// every other page: the transliteration table, not the Latin one
	for (int page : {437, 1252, FontCodePageHdSheet}) {
		CHECK(FontCodepageIndices(mir, page) == Ascii("Mir"));
	}
	// multi-letter stand-ins: Zh (U+0416), shch (U+0449), Io (U+0401)
	CHECK(FontCodepageIndices("\xd0\x96\xd1\x89\xd0\x81", 437) == Ascii("ZhshchIo"));
	// a letter CP866 lacks (Dj, U+0402) draws its stand-in there too
	CHECK(FontCodepageIndices("\xd0\x82", 866) == Ascii("Dj"));
}

TEST_CASE("font codepage: a character no page has stays '?'")
{
	// Polish z-acute U+017A and the trade mark sign U+2122
	CHECK(FontCodepageIndices("\xc5\xba\xe2\x84\xa2", FontCodePageHdSheet) == Ascii("??"));
	CHECK(FontCodepageIndices("\xc5\xba", 437) == Ascii("?"));
}

// ---------------------------------------------------------------------------------------------
// FONT-CJK: glyph map after slot 255, UTF-8 decoding, line breaks without spaces
// ---------------------------------------------------------------------------------------------

/// UTF-8 of a list of codepoints (keeps the test file ASCII).
static std::string U8(std::initializer_list<int> cps)
{
	std::string s;
	for (int cp : cps) {
		if (cp < 0x80) {
			s += char(cp);
		} else if (cp < 0x800) {
			s += char(0xC0 | (cp >> 6));
			s += char(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			s += char(0xE0 | (cp >> 12));
			s += char(0x80 | ((cp >> 6) & 0x3F));
			s += char(0x80 | (cp & 0x3F));
		} else {
			s += char(0xF0 | (cp >> 18));
			s += char(0x80 | ((cp >> 12) & 0x3F));
			s += char(0x80 | ((cp >> 6) & 0x3F));
			s += char(0x80 | (cp & 0x3F));
		}
	}
	return s;
}

/// Every character 10 px wide: lines are measured in characters.
static int TenPx(std::string_view t)
{
	int n = 0;
	for (size_t p = 0; p < t.size();) {
		FontDecodeUTF8(t, p);
		++n;
	}
	return n * 10;
}

/// All lines of `s` at `maxWidth` by FontLineBreak.
static std::vector<std::string> Lines(std::string_view s, unsigned int maxWidth)
{
	std::vector<std::string> out;
	while (!s.empty()) {
		size_t next = 0;
		const size_t len = FontLineBreak(s, maxWidth, TenPx, next);
		out.emplace_back(s.substr(0, len));
		REQUIRE(next > 0);
		s.remove_prefix(next);
	}
	return out;
}

TEST_CASE("font cjk: UTF-8 decoding of 1..4 byte characters, broken and cut-off sequences")
{
	const std::string s = U8({'A', 0xE9, 0x416, 0x6575, 0x1F600});
	size_t p = 0;
	CHECK(FontDecodeUTF8(s, p) == 'A');
	CHECK(FontDecodeUTF8(s, p) == 0xE9);
	CHECK(FontDecodeUTF8(s, p) == 0x416);
	CHECK(FontDecodeUTF8(s, p) == 0x6575);
	CHECK(FontDecodeUTF8(s, p) == 0x1F600);
	CHECK(p == s.size());
	CHECK(FontDecodeUTF8(s, p) == -1);
	// a lone continuation byte, then a good letter: -1 for one byte, then the letter
	const std::string bad = std::string("\x80") + "B";
	p = 0;
	CHECK(FontDecodeUTF8(bad, p) == -1);
	CHECK(p == 1);
	CHECK(FontDecodeUTF8(bad, p) == 'B');
	// a three-byte lead cut off at the end of the text: never reads past it
	const std::string cut = "C\xe6\x95";
	p = 1;
	CHECK(FontDecodeUTF8(cut, p) == -1);
	CHECK(p == 2);
	// a lead followed by ASCII: the ASCII is not swallowed
	const std::string lead = "\xe6Z";
	p = 0;
	CHECK(FontDecodeUTF8(lead, p) == -1);
	CHECK(FontDecodeUTF8(lead, p) == 'Z');
}

TEST_CASE("font cjk: a broken byte draws '?' and the rest of the text is still drawn")
{
	CHECK(FontCodepageIndices(std::string("a\x80" "b"), FontCodePageHdSheet) == Ix({'a', '?', 'b'}));
	CHECK(FontCodepageIndices(std::string("ab\xe6\x95"), FontCodePageHdSheet) == Ix({'a', 'b', '?', '?'}));
}

TEST_CASE("font cjk: the map file gives the slots after 255 in line order")
{
	FontGlyphMap map;
	// Cyrillic "Mi", a kanji, a blank line (keeps its slot), junk (keeps its slot), ASCII (ignored)
	CHECK(ParseFontGlyphMap("1052\r\n1080\n25975\n\nxyz\n65\n12290\n", map) == 4);
	CHECK(map.at(0x41C) == 256);
	CHECK(map.at(0x438) == 257);
	CHECK(map.at(0x6577) == 258);
	CHECK(map.count('A') == 0);
	CHECK(map.at(0x3002) == 262);
	// a repeated character keeps its first slot
	CHECK(ParseFontGlyphMap("1052\n1052\n", map) == 1);
	CHECK(map.at(0x41C) == 256);
	CHECK(ParseFontGlyphMap("", map) == 0);
}

TEST_CASE("font cjk: a mapped character is drawn from its slot; the code page keeps the rest")
{
	FontGlyphMap map;
	ParseFontGlyphMap("1052\n1080\n1088\n25973\n", map);   // M i r (Cyrillic), one kanji
	const std::string mir = U8({0x41C, 0x438, 0x440});
	CHECK(FontCodepageIndices(mir, FontCodePageHdSheet, &map) == Ix({256, 257, 258}));
	// without the map: the old transliteration
	CHECK(FontCodepageIndices(mir, FontCodePageHdSheet) == Ascii("Mir"));
	// ASCII, the ~ codes, Latin-1 and the Lithuanian slots are untouched by a map
	CHECK(FontCodepageIndices("~!" + U8({0xE9, 0x117, 0x6575}), FontCodePageHdSheet, &map)
	      == Ix({'~', '!', 0xE9, 0x87, 259}));
	// a character neither has: '?' as before
	CHECK(FontCodepageIndices(U8({0x6E2F}), FontCodePageHdSheet, &map) == Ix({'?'}));
}

TEST_CASE("font cjk: which characters break, and the kinsoku marks")
{
	CHECK(FontIsCJKBreakChar(0x6575));     // ideograph
	CHECK(FontIsCJKBreakChar(0x3042));     // hiragana a
	CHECK(FontIsCJKBreakChar(0x30AB));     // katakana ka
	CHECK(FontIsCJKBreakChar(0xFF01));     // full-width !
	CHECK_FALSE(FontIsCJKBreakChar('a'));
	CHECK_FALSE(FontIsCJKBreakChar(0x416)); // Cyrillic: spaces
	CHECK_FALSE(FontIsCJKBreakChar(0xD55C)); // Hangul: spaces
	CHECK(FontNoLineStart(0x3002));        // ideographic full stop
	CHECK(FontNoLineStart(0x3001));        // ideographic comma
	CHECK(FontNoLineStart(0x300D));        // closing corner bracket
	CHECK(FontNoLineStart(0x30FC));        // long vowel mark
	CHECK(FontNoLineStart(0x3063));        // small tsu
	CHECK_FALSE(FontNoLineStart(0x6575));
	CHECK(FontNoLineEnd(0x300C));          // opening corner bracket
	CHECK(FontNoLineEnd(0xFF08));          // full-width (
	CHECK_FALSE(FontNoLineEnd(0x3002));
}

TEST_CASE("font cjk: Latin text still breaks at its spaces and newlines")
{
	// "Build three new Farms" at 100 px (10 characters)
	CHECK(Lines("Build three new Farms", 100) == std::vector<std::string>({"Build", "three new", "Farms"}));
	CHECK(Lines("ab\ncd", 0) == std::vector<std::string>({"ab", "cd"}));
	CHECK(Lines("short", 100) == std::vector<std::string>({"short"}));
	// a word wider than the line stands alone and the text goes on
	CHECK(Lines("Supercalifragilistic is long", 100)
	      == std::vector<std::string>({"Supercalifragilistic", "is long"}));
}

TEST_CASE("font cjk: Japanese without spaces breaks between characters, never before a closing mark")
{
	// 8 characters, lines of 3 (35 px): kanji x3, full stop, kanji x4
	const int k = 0x6575;
	const std::string text = U8({k, k, k, 0x3002, k, k, k, k});
	// the full stop may not begin a line: the third kanji goes down with it
	CHECK(Lines(text, 35) == std::vector<std::string>({U8({k, k}), U8({k, 0x3002, k}), U8({k, k, k})}));
	// no break after an opening bracket: the bracket moves to the next line with its word
	const std::string quote = U8({k, k, k, 0x300C, k, k, 0x300D});
	CHECK(Lines(quote, 45) == std::vector<std::string>({U8({k, k, k}), U8({0x300C, k, k, 0x300D})}));
	// Korean breaks at its spaces only
	const int h = 0xD55C;
	CHECK(Lines(U8({h, h, ' ', h, h, h}), 40) == std::vector<std::string>({U8({h, h}), U8({h, h, h})}));
}

TEST_CASE("font cjk: a colour code is never split from its text; Latin inside CJK stays whole")
{
	const int k = 0x6575;
	// "~y~" + 5 kanji at 4 characters: the code (3 characters wide here) stays with the first kanji
	const std::string coloured = "~y~" + U8({k, k, k, k, k});
	const auto lines = Lines(coloured, 40);
	REQUIRE(!lines.empty());
	CHECK(lines[0].rfind("~y~", 0) == 0);
	CHECK(lines[0].size() > 3);
	// "OK" between kanji is not broken inside
	const std::string mixed = U8({k, k, k, 'O', 'K', k});
	for (const auto &l : Lines(mixed, 40)) {
		CHECK(l.find('O') == l.find("OK"));
	}
}

TEST_CASE("font width: a number is measured with its own digits, not with spaces")
{
	// index = character - 32: space 3, ',' 2, '-' 4, digit d -> 10 + d
	std::vector<char> w(96, 1);
	w[0] = 3;
	w[',' - 32] = 2;
	w['-' - 32] = 4;
	for (int d = 0; d <= 9; ++d) {
		w['0' + d - 32] = char(10 + d);
	}
	CHECK(FontNumberWidth(0, w, 0) == 10);
	CHECK(FontNumberWidth(7, w, 1) == 17 + 1);
	CHECK(FontNumberWidth(1234, w, 0) == 11 + 2 + 12 + 13 + 14);       // "1,234"
	CHECK(FontNumberWidth(-50, w, 1) == (4 + 1) + (15 + 1) + (10 + 1)); // "-50"
	CHECK(FontNumberWidth(1000000, w, 0) == 11 + 2 + 10 * 3 + 2 + 10 * 3);
	// wider "8" than "1": the old code measured both as the space
	CHECK(FontNumberWidth(8, w, 0) != FontNumberWidth(1, w, 0));
	// a table too short for the digits measures them as the space; empty table -> 0
	std::vector<char> shortTable(5, 6);
	CHECK(FontNumberWidth(42, shortTable, 0) == 12);
	CHECK(FontNumberWidth(42, std::vector<char>{}, 0) == 0);
}
