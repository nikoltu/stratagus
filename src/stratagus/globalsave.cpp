//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ \
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/// @file globalsave.cpp - Warcraft II: Remastered GlobalSave.json (campaign progress).
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#include "stratagus.h"
#include "globalsave.h"

#include "json.h"
#include "game.h"
#include "parameters.h"

#include <array>
#include <cstdio>
#include <string>

/// The original's four campaign keys, in the order its string table lists them.
static const std::array<const char *, 4> CampaignKeys = {"todOrc", "todHuman", "bdpOrc", "bdpHuman"};

static fs::path GlobalSavePath()
{
	fs::path path = Parameters::Instance.GetUserDirectory();
	if (!GameName.empty()) {
		path /= GameName;
	}
	// Same file name the original client writes into its own user directory.
	path /= "GlobalSave.json";
	return path;
}

enum class ProgressRead { Missing, Read, Damaged };

/// Read the whole file. A missing file is not an error: it means "no progress yet". A file that is
/// there but does not parse is Damaged: its values are unknown, not zero.
static ProgressRead ReadProgressFile(std::array<int, 4> &out)
{
	out = {0, 0, 0, 0};
	FILE *fd = fopen(GlobalSavePath().string().c_str(), "rb");
	if (!fd) {
		return ProgressRead::Missing;
	}
	std::string text;
	char buf[1024];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fd)) > 0) {
		text.append(buf, n);
	}
	fclose(fd);

	content::JsonValue root;
	std::string err;
	if (!content::parseJson(text, root, err)) {
		ErrorPrint("GlobalSave.json is malformed (%s)\n", err.c_str());
		return ProgressRead::Damaged;
	}
	const content::JsonValue *mc = root.find("missionsCompleted");
	if (!mc) {
		return ProgressRead::Read; // valid JSON without the member: no progress recorded yet
	}
	for (size_t i = 0; i < CampaignKeys.size(); ++i) {
		if (const content::JsonValue *v = mc->find(CampaignKeys[i])) {
			out[i] = static_cast<int>(v->asInt(0));
		}
	}
	return ProgressRead::Read;
}

/// Write the four values to a temporary file next to the real one and move it over the real one, so
/// an interrupted write leaves the previous file whole instead of a truncated one (audit EA-419).
static bool WriteProgress(const std::array<int, 4> &values)
{
	const fs::path path = GlobalSavePath();
	fs::path tmp = path;
	tmp += ".tmp";
	FILE *fd = fopen(tmp.string().c_str(), "wb");
	if (!fd) {
		ErrorPrint("Cannot open '%s' for writing\n", tmp.u8string().c_str());
		return false;
	}
	// Same shape the original writes: one object, one member, four integer keys.
	bool ok = fprintf(fd, "{\n\t\"missionsCompleted\": {\n") >= 0;
	for (size_t i = 0; i < CampaignKeys.size(); ++i) {
		ok = ok && fprintf(fd, "\t\t\"%s\": %d%s\n", CampaignKeys[i], values[i],
						   (i + 1 < CampaignKeys.size()) ? "," : "") >= 0;
	}
	ok = ok && fprintf(fd, "\t}\n}\n") >= 0;
	ok = (fflush(fd) == 0) && ok;
	ok = (fclose(fd) == 0) && ok;
	std::error_code ec;
	if (!ok) {
		ErrorPrint("Writing '%s' failed; GlobalSave.json is left as it was\n", tmp.u8string().c_str());
		fs::remove(tmp, ec);
		return false;
	}
	fs::rename(tmp, path, ec);
	if (ec) {
		ErrorPrint("Cannot replace '%s': %s\n", path.u8string().c_str(), ec.message().c_str());
		fs::remove(tmp, ec);
		return false;
	}
	return true;
}

static int KeyIndex(const std::string &campaignKey)
{
	for (size_t i = 0; i < CampaignKeys.size(); ++i) {
		if (campaignKey == CampaignKeys[i]) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

void SetGlobalCampaignProgress(const std::string &campaignKey, int missionsCompleted)
{
	const int idx = KeyIndex(campaignKey);
	if (idx < 0) {
		ErrorPrint("Unknown campaign key '%s' for GlobalSave.json\n", campaignKey.c_str());
		return;
	}
	std::array<int, 4> values{};
	if (ReadProgressFile(values) == ProgressRead::Damaged) {
		// The other three campaigns' values are unknown: writing would reset them to zero. Leave the
		// file for the player (or a later fix) to recover, and record nothing (audit EA-419).
		ErrorPrint("GlobalSave.json could not be read; '%s' progress %d is not recorded\n",
				   campaignKey.c_str(), missionsCompleted);
		return;
	}
	if (missionsCompleted <= values[idx]) {
		return; // monotonic: replaying a mission must not remove progress
	}
	values[idx] = missionsCompleted;
	WriteProgress(values);
}
