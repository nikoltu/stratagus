//     _________ __                 __
//    /   _____//  |_____________ _/  |______     ____  __ __  ______
//    \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//    /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ \
//   /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//           \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/// @file content_set.h - Load + validate a versioned runtime content set (packs).
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#ifndef __CONTENT_CONTENT_SET_H__
#define __CONTENT_CONTENT_SET_H__

#include "json.h"

#include <string>
#include <vector>

namespace content
{

/// One pack listed in a content-set manifest, with its validated payload.
struct ContentPack {
	std::string name;
	std::string path;   ///< cache-relative path to the pack file
	std::string schema;
	std::string sha256; ///< declared content hash
	long bytes = 0;
	JsonValue data;     ///< parsed pack payload (populated once validated)
	bool valid = false; ///< bytes hashed to the declared sha256 and parsed cleanly
};

/// Loads `content-set.json`, verifies the manifest schema and each pack's SHA-256 against the
/// on-disk bytes, and parses every valid pack. The shipped engine consumes a validated set (or
/// runs the importer); it never assumes a developer staging tree. See doc 08 AST-005 / ARC-002.
class ContentSet
{
public:
	/// Load + validate. Returns true only when the manifest and every declared pack verify.
	bool load(const std::string &contentSetJsonPath);

	bool loaded() const { return loaded_; }
	const std::string &error() const { return error_; }
	const std::string &fidelityProfile() const { return fidelityProfile_; }
	const std::string &oracleSchema() const { return oracleSchema_; }
	const std::vector<ContentPack> &packs() const { return packs_; }
	/// Pack by logical name, or nullptr.
	const ContentPack *findPack(const std::string &name) const;

private:
	bool fail(const std::string &message);

	bool loaded_ = false;
	std::string error_;
	std::string fidelityProfile_;
	std::string oracleSchema_;
	std::vector<ContentPack> packs_;
};

} // namespace content

#endif // __CONTENT_CONTENT_SET_H__
