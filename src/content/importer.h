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
/// @file importer.h - First-run content import: decide + hook (ARC-002).
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#ifndef __CONTENT_IMPORTER_H__
#define __CONTENT_IMPORTER_H__

#include <string>

namespace content
{

/// What the content pipeline should do at boot, decided from the presence of a validated content
/// set versus a source tree to import from. See doc 08 AST-005 / ARC-002 (import + content).
enum class FirstRunAction {
	ContentReady,    ///< a validated content set already loaded; nothing to import
	ImportNeeded,    ///< no usable content set, but an owned source tree is present -> import
	NoContentSource, ///< neither a content set nor a source tree -> run on built-in defaults
};

/// Decide the first-run action. `contentReady` is whether a content set already validated;
/// `sourceRoot` is the directory under which owned source data (raw / derived) would live. This
/// only inspects for presence -- it never reads, mutates, or fabricates any content. Path-agnostic:
/// the caller supplies the root.
FirstRunAction EvaluateFirstRun(bool contentReady, const std::string &sourceRoot);

/// First-run import entry point: owned source under `sourceRoot` -> a validated content set under
/// `cacheDir`. This is the pipeline hook; in-engine generation is not yet implemented (the offline
/// pack compiler currently produces the cache), so this reports honestly and produces nothing on
/// its own. Returns true only when a usable content set was produced. Safe to call in any state.
bool RunFirstRunImport(const std::string &cacheDir, const std::string &sourceRoot);

/// Stable human-readable name of an action, for logging.
const char *ToString(FirstRunAction action);

} // namespace content

#endif // __CONTENT_IMPORTER_H__
