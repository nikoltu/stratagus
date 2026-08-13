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
/// @file presentation.h - Lua-facing accessors over the loaded W2PresentationPack.
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#ifndef __CONTENT_PRESENTATION_H__
#define __CONTENT_PRESENTATION_H__

#include <string>

// These are declared in the global namespace so tolua can bind them directly; Wargus Lua reads the
// original-derived presentation pack (screens / typed controls / transitions / layout) by logical id
// instead of hand-authored screen tables (ARC-003: Lua identifies logical assets, not paths).

/// Load the runtime content set from `cacheDir`/content-set.json and index its presentation pack.
/// Idempotent, logs the outcome, and is safe when the cache is absent (queries then report empty).
bool LoadContentSet(const std::string &cacheDir);

bool PresentationLoaded();
/// Fidelity profile of the loaded content set (e.g. "PC_REMASTERED_REFERENCE"), or "" when none.
std::string PresentationFidelityProfile();
/// Oracle schema/provenance of the loaded content set (e.g. "builtin"), or "" when none.
std::string PresentationOracleSchema();
/// Number of packs in the loaded content set (0 when none loaded).
int PresentationPackCount();
int PresentationScreenCount();
/// Total number of typed controls across every screen in the presentation pack (0 when unloaded).
int PresentationControlTotal();
/// Logical screen id at `index` (0-based), or "" when out of range.
std::string PresentationScreenId(int index);
/// A screen-level field: "kind", "parent", "marker", "geometry_status", "runtime_capture", or a
/// layout field via "layout.<name>" (e.g. "layout.scale_formula"). "" when absent.
std::string PresentationScreenField(const std::string &screenId, const std::string &field);

int PresentationControlCount(const std::string &screenId);
/// A control field: "id", "group", "kind", "config_key", "label", "evidence_grade". "" when absent.
std::string PresentationControlField(const std::string &screenId, int order, const std::string &field);

/// Number of value options for an enum/dropdown control (0 for a plain control).
int PresentationControlValueCount(const std::string &screenId, int order);
/// The `index`-th value-option string key of a control, or "" when out of range.
std::string PresentationControlValue(const std::string &screenId, int order, int index);

#endif // __CONTENT_PRESENTATION_H__
