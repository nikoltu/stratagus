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
/**@name test_presentation.cpp - The test file for presentation.cpp accessors. */
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

#include "presentation.h"
#include "sha256.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{

// A presentation pack with one normal screen (two controls, one carrying values) plus
// two deliberately malformed screens (controls not an array; a control whose values
// field is not an array) to exercise the accessor shape guards.
const std::string kPack =
	"{\"screens\":["
	  "{\"id\":\"options\",\"layout\":{\"w\":\"640\"},\"controls\":["
	    "{\"id\":\"c0\",\"kind\":\"slider\"},"
	    "{\"id\":\"c1\",\"kind\":\"enum\",\"values\":[\"a\",\"b\",\"c\"]}"
	  "]},"
	  "{\"id\":\"weird\",\"controls\":\"not-an-array\"},"
	  "{\"id\":\"weird2\",\"controls\":[{\"id\":\"x\",\"values\":42}]}"
	"]}";

void writeFile(const fs::path &p, const std::string &s)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << s;
}

fs::path buildCache()
{
	fs::path dir = fs::temp_directory_path() / "stratagus_presentation_test";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const std::string sha = content::Sha256Hex(kPack);
	writeFile(dir / "presentation.json", kPack);
	writeFile(dir / "content-set.json",
		std::string("{\"schema\":\"content-set/1\",\"fidelity_profile\":\"TEST\",\"packs\":[{")
		+ "\"pack\":\"presentation\",\"path\":\"presentation.json\",\"sha256\":\"" + sha + "\"}]}");
	return dir;
}

} // namespace

TEST_CASE("presentation pack loads and reports its structure")
{
	REQUIRE(LoadContentSet(buildCache().string()));
	CHECK(PresentationLoaded());
	CHECK(PresentationScreenCount() == 3);
	CHECK(PresentationControlCount("options") == 2);
	CHECK(PresentationControlValueCount("options", 1) == 3);
	CHECK(PresentationControlValue("options", 1, 2) == "c");
	CHECK(PresentationScreenField("options", "layout.w") == "640");
	CHECK(PresentationControlField("options", 0, "kind") == "slider");
}

TEST_CASE("presentation accessors degrade gracefully on malformed shapes")
{
	REQUIRE(LoadContentSet(buildCache().string()));
	CHECK(PresentationControlCount("weird") == 0);      // controls not an array
	CHECK(PresentationControlValueCount("weird2", 0) == 0); // values not an array
}

TEST_CASE("presentation accessors never crash on adversarial arguments")
{
	REQUIRE(LoadContentSet(buildCache().string()));
	const char *screens[] = { "options", "weird", "weird2", "does-not-exist", "" };
	const int idxs[] = { -1000000, -1, 0, 1, 2, 3, 1000000 };
	for (const char *s : screens) {
		PresentationControlCount(s);
		for (int o : idxs) {
			PresentationControlField(s, o, "id");
			PresentationControlValueCount(s, o);
			for (int v : idxs) {
				PresentationControlValue(s, o, v);
			}
		}
		PresentationScreenField(s, "layout.x");
	}
	for (int i : idxs) {
		PresentationScreenId(i);
	}
	// Valid access is still correct after the adversarial storm.
	CHECK(PresentationScreenId(0) == "options");
	CHECK(PresentationControlField("options", 0, "kind") == "slider");
}
