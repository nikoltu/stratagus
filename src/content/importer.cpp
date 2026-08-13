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

#include <cstdio>
#include <filesystem>
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
	// Pipeline hook: owned source -> validated content set. In-engine generation is not yet
	// implemented; the offline pack compiler currently produces the cache. Report honestly and
	// leave the cache untouched so a subsequent boot re-evaluates cleanly once a set is present.
	fprintf(stderr,
	        "content: first-run import hook reached (source under %s -> cache %s); "
	        "in-engine generation not yet available -- provision the content set to proceed\n",
	        sourceRoot.c_str(), cacheDir.c_str());
	return false;
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
