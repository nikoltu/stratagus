//     _________ __                 __
//    /   _____//  |_____________ _/  |______     ____  __ __  ______
//    \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ______
//    /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ \
//   /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//           \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/// @file importer.cpp - First-run content import: decide + hook (ARC-002).
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#include "importer.h"

#include "content_set.h"
#include "sha256.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace content
{

namespace
{

// Directory names, relative to the source root, under which an owned source tree (original raw or
// a derived intermediate) would live. Presence of any non-empty candidate signals a source to
// import from. Kept as data so new source layouts can be recognised without touching the logic.
const char *const kSourceCandidates[] = { "raw", "wc2", "data" };

// True if `dir` exists and holds at least one entry -- i.e. there is something to import.
bool nonEmptyDir(const std::filesystem::path &dir)
{
	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec)) {
		return false;
	}
	std::filesystem::directory_iterator it(dir, ec);
	if (ec) {
		return false;
	}
	return it != std::filesystem::end(it);
}

// True if any known source candidate under `sourceRoot` is present and non-empty.
bool sourcePresent(const std::string &sourceRoot)
{
	if (sourceRoot.empty()) {
		return false;
	}
	const std::filesystem::path root(sourceRoot);
	for (const char *name : kSourceCandidates) {
		if (nonEmptyDir(root / name)) {
			return true;
		}
	}
	return false;
}

// The engine's built-in minimal presentation pack: a small set of option screens whose controls are
// genuinely backed by engine settings (speed and volume sliders). This is a documented last-resort
// default, NOT original-derived data; it exists so a first run can produce a bootable content set.
const char *const kBuiltinPresentation =
	"{\n"
	"  \"generator\": \"engine-builtin-minimal/1\",\n"
	"  \"screens\": [\n"
	"    {\n"
	"      \"id\": \"options_speeds\",\n"
	"      \"title\": \"Speed Settings\",\n"
	"      \"controls\": [\n"
	"        { \"id\": \"game_speed\", \"kind\": \"slider\" },\n"
	"        { \"id\": \"mouse_scroll\", \"kind\": \"slider\" },\n"
	"        { \"id\": \"key_scroll\", \"kind\": \"slider\" }\n"
	"      ]\n"
	"    },\n"
	"    {\n"
	"      \"id\": \"options_sound\",\n"
	"      \"title\": \"Sound\",\n"
	"      \"controls\": [\n"
	"        { \"id\": \"music_volume\", \"kind\": \"slider\" },\n"
	"        { \"id\": \"sfx_volume\", \"kind\": \"slider\" }\n"
	"      ]\n"
	"    }\n"
	"  ]\n"
	"}\n";

// Write text to path, creating parent directories. Returns false on any I/O error.
bool writeFile(const std::filesystem::path &path, const std::string &text)
{
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		return false;
	}
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
	return static_cast<bool>(out);
}

// Emit a minimal but schema-valid content set (manifest + presentation pack) into `cacheDir`.
// The manifest references the pack by its exact SHA-256 so a subsequent load self-verifies.
bool writeBuiltinContentSet(const std::string &cacheDir)
{
	const std::filesystem::path root(cacheDir);
	const std::string packText = kBuiltinPresentation;
	if (!writeFile(root / "oracle" / "presentation.json", packText)) {
		return false;
	}

	const std::string sha = Sha256Hex(packText);
	std::string manifest;
	manifest += "{\n";
	manifest += "  \"schema\": \"content-set/1\",\n";
	manifest += "  \"fidelity_profile\": \"ENGINE_BUILTIN_MINIMAL\",\n";
	manifest += "  \"oracle_schema\": \"builtin\",\n";
	manifest += "  \"packs\": [\n";
	manifest += "    {\n";
	manifest += "      \"pack\": \"presentation\",\n";
	manifest += "      \"path\": \"oracle/presentation.json\",\n";
	manifest += "      \"schema\": \"presentation/1\",\n";
	manifest += "      \"sha256\": \"" + sha + "\",\n";
	manifest += "      \"bytes\": " + std::to_string(packText.size()) + "\n";
	manifest += "    }\n";
	manifest += "  ]\n";
	manifest += "}\n";

	return writeFile(root / "content-set.json", manifest);
}

} // namespace

FirstRunAction EvaluateFirstRun(bool contentReady, const std::string &sourceRoot)
{
	if (contentReady) {
		return FirstRunAction::ContentReady;
	}
	if (sourcePresent(sourceRoot)) {
		return FirstRunAction::ImportNeeded;
	}
	return FirstRunAction::NoContentSource;
}

bool RunFirstRunImport(const std::string &cacheDir, const std::string &sourceRoot)
{
	// Pipeline hook: with no validated set present, bootstrap a minimal but schema-valid content
	// set from the engine's built-in default so the next boot is content-ready. Fuller packs
	// (original-derived) are produced by the offline compiler and override this when present.
	if (!writeBuiltinContentSet(cacheDir)) {
		fprintf(stderr, "content: first-run import could not write a content set to %s\n", cacheDir.c_str());
		return false;
	}
	// Self-verify the emitted set: it must load cleanly (schema, per-pack SHA-256, JSON) before we
	// rely on it. This turns a botched emit into a clear failure now rather than a bad boot later.
	ContentSet check;
	if (!check.load(cacheDir + "/content-set.json")) {
		fprintf(stderr, "content: first-run import wrote an invalid content set to %s (%s)\n",
		        cacheDir.c_str(), check.error().c_str());
		return false;
	}
	fprintf(stderr,
	        "content: first-run import wrote a built-in minimal content set to %s "
	        "(%zu pack(s), verified; source under %s); it will load on the next start\n",
	        cacheDir.c_str(), check.packs().size(), sourceRoot.c_str());
	return true;
}

const char *ToString(FirstRunAction action)
{
	switch (action) {
		case FirstRunAction::ContentReady:    return "content-ready";
		case FirstRunAction::ImportNeeded:    return "import-needed";
		case FirstRunAction::NoContentSource: return "no-content-source";
	}
	return "unknown";
}

} // namespace content
