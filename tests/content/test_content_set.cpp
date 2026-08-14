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
/**@name test_content_set.cpp - The test file for content_set.cpp. */
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

#include "content_set.h"
#include "sha256.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{

const std::string kPackBody = "{\"schema\":\"pack/1\",\"k\":1}";

void writeFile(const fs::path &p, const std::string &s)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << s;
}

// A unique per-case scratch directory under the system temp path.
fs::path caseDir(const char *name)
{
	fs::path d = fs::temp_directory_path() / "stratagus_content_test" / name;
	fs::remove_all(d);
	fs::create_directories(d);
	return d;
}

std::string manifest(const std::string &packPathField, const std::string &sha)
{
	return std::string("{\"schema\":\"content-set/1\",\"packs\":[{")
	     + "\"pack\":\"p1\",\"path\":\"" + packPathField + "\","
	     + "\"sha256\":\"" + sha + "\"}]}";
}

} // namespace

TEST_CASE("content set loads a valid manifest")
{
	fs::path dir = caseDir("ok");
	const std::string sha = content::Sha256Hex(kPackBody);
	writeFile(dir / "pack1.json", kPackBody);
	writeFile(dir / "content-set.json", manifest("pack1.json", sha));

	content::ContentSet cs;
	CHECK(cs.load((dir / "content-set.json").string()));
	CHECK(cs.loaded());
	CHECK(cs.packs().size() == 1);
	CHECK(cs.findPack("p1") != nullptr);
}

TEST_CASE("content set rejects malformed manifests")
{
	const std::string sha = content::Sha256Hex(kPackBody);
	auto reject = [&](const char *label, const std::string &manifestText, bool writePack) {
		fs::path dir = caseDir(label);
		if (writePack) {
			writeFile(dir / "pack1.json", kPackBody);
		}
		writeFile(dir / "content-set.json", manifestText);
		content::ContentSet cs;
		INFO(label);
		CHECK_FALSE(cs.load((dir / "content-set.json").string()));
		CHECK_FALSE(cs.loaded());
		CHECK_FALSE(cs.error().empty());
	};

	reject("badjson", "{ not json", true);
	reject("wrongschema", "{\"schema\":\"content-set/2\",\"packs\":[]}", true);
	reject("nopacks", "{\"schema\":\"content-set/1\"}", true);
	reject("nosha", "{\"schema\":\"content-set/1\",\"packs\":[{\"pack\":\"p1\",\"path\":\"pack1.json\"}]}", true);
	reject("dup", std::string("{\"schema\":\"content-set/1\",\"packs\":[")
	              + "{\"pack\":\"p1\",\"path\":\"pack1.json\",\"sha256\":\"" + sha + "\"},"
	              + "{\"pack\":\"p1\",\"path\":\"pack1.json\",\"sha256\":\"" + sha + "\"}]}", true);
	reject("shamismatch", manifest("pack1.json", std::string(64, '0')), true);
	reject("nofile", manifest("pack1.json", sha), false);
}

// Regression: a pack path must not reach outside the content root (absolute path or
// a parent-directory traversal). std::filesystem's `root / path` would otherwise read
// the escaping file.
TEST_CASE("content set rejects pack paths that escape the content root")
{
	const std::string sha = content::Sha256Hex(kPackBody);

	SUBCASE("absolute path")
	{
		fs::path dir = caseDir("abs");
		fs::path outside = dir.parent_path() / "outside_abs.json";
		writeFile(outside, kPackBody);
		std::string absStr = outside.string();
		for (char &c : absStr) {
			if (c == '\\') {
				c = '/';
			}
		}
		writeFile(dir / "content-set.json", manifest(absStr, sha));
		content::ContentSet cs;
		CHECK_FALSE(cs.load((dir / "content-set.json").string()));
	}

	SUBCASE("parent traversal")
	{
		fs::path dir = caseDir("trav");
		writeFile(dir.parent_path() / "outside_rel.json", kPackBody);
		writeFile(dir / "content-set.json", manifest("../outside_rel.json", sha));
		content::ContentSet cs;
		CHECK_FALSE(cs.load((dir / "content-set.json").string()));
	}
}
