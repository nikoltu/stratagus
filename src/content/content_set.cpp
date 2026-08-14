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
/// @file content_set.cpp - Content-set manifest loader with SHA-256 pack validation.
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#include "content_set.h"

#include "sha256.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace content
{

namespace
{

bool readFile(const std::filesystem::path &path, std::string &out)
{
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		return false;
	}
	std::ostringstream buffer;
	buffer << stream.rdbuf();
	out = buffer.str();
	return true;
}

} // namespace

bool ContentSet::fail(const std::string &message)
{
	error_ = message;
	loaded_ = false;
	packs_.clear();
	return false;
}

const ContentPack *ContentSet::findPack(const std::string &name) const
{
	for (const ContentPack &pack : packs_) {
		if (pack.name == name) {
			return &pack;
		}
	}
	return nullptr;
}

bool ContentSet::load(const std::string &contentSetJsonPath)
{
	loaded_ = false;
	error_.clear();
	packs_.clear();

	const std::filesystem::path manifestPath(contentSetJsonPath);
	const std::filesystem::path root = manifestPath.parent_path();

	std::string manifestText;
	if (!readFile(manifestPath, manifestText)) {
		return fail("cannot open content set: " + contentSetJsonPath);
	}

	JsonValue manifest;
	std::string jsonError;
	if (!parseJson(manifestText, manifest, jsonError) || !manifest.isObject()) {
		return fail("content-set.json is not valid JSON: " + jsonError);
	}

	const JsonValue *schema = manifest.find("schema");
	if (!schema || schema->asString() != "content-set/1") {
		return fail("unsupported content-set schema");
	}
	if (const JsonValue *profile = manifest.find("fidelity_profile")) {
		fidelityProfile_ = profile->asString();
	}
	if (const JsonValue *oracle = manifest.find("oracle_schema")) {
		oracleSchema_ = oracle->asString();
	}

	const JsonValue *packList = manifest.find("packs");
	if (!packList || !packList->isArray()) {
		return fail("content-set.json has no packs array");
	}

	for (const JsonValue &entry : packList->items()) {
		if (!entry.isObject()) {
			return fail("pack entry is not an object");
		}
		ContentPack pack;
		if (const JsonValue *v = entry.find("pack")) pack.name = v->asString();
		if (const JsonValue *v = entry.find("path")) pack.path = v->asString();
		if (const JsonValue *v = entry.find("schema")) pack.schema = v->asString();
		if (const JsonValue *v = entry.find("sha256")) pack.sha256 = v->asString();
		if (const JsonValue *v = entry.find("bytes")) pack.bytes = v->asInt();
		if (pack.name.empty() || pack.path.empty() || pack.sha256.empty()) {
			return fail("pack entry missing name/path/sha256");
		}
		// A duplicate name would silently shadow the earlier pack (findPack returns the first); reject
		// it so a malformed manifest is a clear error rather than an ambiguous lookup.
		if (findPack(pack.name)) {
			return fail("duplicate pack name '" + pack.name + "'");
		}
		// The path is defined as cache-relative; a manifest must not reach outside the content root.
		// std::filesystem's `root / path` silently yields `path` when it is absolute, and `..`
		// segments walk out of the directory, so reject anything that is not a pure relative path.
		const std::filesystem::path packRel(pack.path);
		if (packRel.is_absolute() || packRel.has_root_name() || packRel.has_root_directory()) {
			return fail("pack '" + pack.name + "' path is not relative: " + pack.path);
		}
		for (const std::filesystem::path &part : packRel) {
			if (part == "..") {
				return fail("pack '" + pack.name + "' path escapes the content root: " + pack.path);
			}
		}

		std::string packText;
		if (!readFile(root / pack.path, packText)) {
			return fail("cannot open pack '" + pack.name + "' at " + pack.path);
		}
		// Cheap integrity signal, separate from the SHA-256 gate: flag a declared-size mismatch. A
		// declared size of 0 is treated as "unspecified". This warns only; SHA-256 remains the gate.
		if (pack.bytes > 0 && static_cast<long>(packText.size()) != pack.bytes) {
			fprintf(stderr, "content: pack '%s' declares %ld bytes but the file is %zu bytes\n",
			        pack.name.c_str(), pack.bytes, packText.size());
		}
		if (Sha256Hex(packText) != pack.sha256) {
			return fail("pack '" + pack.name + "' failed SHA-256 verification");
		}
		std::string packJsonError;
		if (!parseJson(packText, pack.data, packJsonError)) {
			return fail("pack '" + pack.name + "' is not valid JSON: " + packJsonError);
		}
		pack.valid = true;
		packs_.push_back(std::move(pack));
	}

	loaded_ = true;
	return true;
}

} // namespace content
