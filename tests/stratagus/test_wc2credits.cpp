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
/**@name test_wc2credits.cpp - The test file for the credits reader in wc2strings.cpp. */
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

#include "wc2strings.h"

#include <fstream>
#include <string>
#include <vector>

namespace
{

/// A made-up file of the credits file's shape: two groups, the second listing its third party
/// before its roles; a role with creditees, a role with a text, a role without a name.
const char *const SyntheticCredits =
	"\xEF\xBB\xBF{\"xml\": {\"credits\": [{\"group\": ["
	"{\"$\": {\"name\": \"FIRST GROUP\", \"id\": \"g1\"},"
	" \"thirdParty\": [{\"$\": {\"id\": \"t1\", \"name\": \"MAKER\"}}, {\"$\": {\"id\": \"t2\", \"name\": \"STUDIO\"}}]},"
	"{\"$\": {\"name\": \"SECOND GROUP\", \"id\": \"g2\"},"
	" \"thirdParty\": [{\"$\": {\"id\": \"t3\", \"name\": \"PARTNER\"}, \"_\": \"Line one\\nLine two\"}],"
	" \"role\": ["
	"  {\"$\": {\"name\": \"Lead\", \"type\": \"1\", \"id\": \"r1\"},"
	"   \"creditee\": [{\"$\": {\"name\": \"Ann Example\", \"id\": \"c1\"}}, {\"$\": {\"name\": \"Bo \\u00c9xample\", \"id\": \"c2\"}}]},"
	"  {\"$\": {\"name\": \"Team\", \"type\": \"2\", \"id\": \"r2\"}, \"_\": \"Cy, Di, Ed\"},"
	"  {\"$\": {\"type\": \"1\", \"id\": \"r3\"}, \"creditee\": [{\"$\": {\"name\": \"Fay\", \"id\": \"c3\"}}]}"
	" ]},"
	"{\"$\": {\"id\": \"g3\"}}"
	"]}]}}";

std::string Lines(const std::vector<WC2CreditLine> &lines)
{
	std::string out;
	for (const auto &l : lines) {
		out += l.kind + ":" + l.text + "|";
	}
	return out;
}

} // namespace

TEST_CASE("WC2 credits: the file's order, headings, roles, texts and gaps")
{
	std::vector<WC2CreditLine> lines;
	REQUIRE(ParseWC2Credits(SyntheticCredits, lines));
	CHECK(Lines(lines)
		  == "group:FIRST GROUP|gap:|"
			 "entry:MAKER|gap:|entry:STUDIO|gap:|"
			 "group:SECOND GROUP|gap:|"
			 "entry:PARTNER|text:Line one\nLine two|gap:|"
			 "entry:Lead|name:Ann Example|name:Bo \xC3\x89xample|gap:|"
			 "entry:Team|text:Cy, Di, Ed|gap:|"
			 "name:Fay|gap:|");
}

TEST_CASE("WC2 credits: not a credits file")
{
	std::vector<WC2CreditLine> lines;
	CHECK_FALSE(ParseWC2Credits("", lines));
	CHECK_FALSE(ParseWC2Credits("{\"xml\": {\"credits\": [", lines));
	CHECK_FALSE(ParseWC2Credits("[1, 2]", lines));
	CHECK_FALSE(ParseWC2Credits("{\"other\": 1}", lines));
	CHECK_FALSE(ParseWC2Credits("{\"xml\": {\"credits\": [{\"group\": []}]}}", lines));
	CHECK(lines.empty());
}

TEST_CASE("WC2 credits: read from a file, absent file")
{
	const fs::path dir = fs::temp_directory_path() / "stratagus_test_wc2credits";
	fs::create_directories(dir);
	const fs::path file = dir / "credits.json";
	{
		std::ofstream f(file, std::ios::binary);
		f << SyntheticCredits;
	}
	std::vector<WC2CreditLine> lines;
	CHECK(ReadWC2Credits(file.string(), lines));
	CHECK(lines.size() == 20);
	CHECK_FALSE(ReadWC2Credits((dir / "missing.json").string(), lines));
	CHECK(lines.empty());
	fs::remove_all(dir);
}
