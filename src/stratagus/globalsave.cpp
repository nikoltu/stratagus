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

/// Read the whole file. Missing file is not an error: it means "no progress yet".
static bool ReadProgress(std::array<int, 4> &out)
{
	out = {0, 0, 0, 0};
	FILE *fd = fopen(GlobalSavePath().string().c_str(), "rb");
	if (!fd) {
		return false;
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
		ErrorPrint("GlobalSave.json is malformed (%s); treating progress as empty\n", err.c_str());
		return false;
	}
	const content::JsonValue *mc = root.find("missionsCompleted");
	if (!mc) {
		return false;
	}
	for (size_t i = 0; i < CampaignKeys.size(); ++i) {
		if (const content::JsonValue *v = mc->find(CampaignKeys[i])) {
			out[i] = static_cast<int>(v->asInt(0));
		}
	}
	return true;
}

static void WriteProgress(const std::array<int, 4> &values)
{
	const fs::path path = GlobalSavePath();
	FILE *fd = fopen(path.string().c_str(), "w");
	if (!fd) {
		ErrorPrint("Cannot open '%s' for writing\n", path.u8string().c_str());
		return;
	}
	// Same shape the original writes: one object, one member, four integer keys.
	fprintf(fd, "{\n\t\"missionsCompleted\": {\n");
	for (size_t i = 0; i < CampaignKeys.size(); ++i) {
		fprintf(fd, "\t\t\"%s\": %d%s\n", CampaignKeys[i], values[i],
				(i + 1 < CampaignKeys.size()) ? "," : "");
	}
	fprintf(fd, "\t}\n}\n");
	fclose(fd);
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

int GetGlobalCampaignProgress(const std::string &campaignKey)
{
	const int idx = KeyIndex(campaignKey);
	if (idx < 0) {
		return 0;
	}
	std::array<int, 4> values{};
	ReadProgress(values);
	return values[idx];
}

void SetGlobalCampaignProgress(const std::string &campaignKey, int missionsCompleted)
{
	const int idx = KeyIndex(campaignKey);
	if (idx < 0) {
		ErrorPrint("Unknown campaign key '%s' for GlobalSave.json\n", campaignKey.c_str());
		return;
	}
	std::array<int, 4> values{};
	ReadProgress(values);
	if (missionsCompleted <= values[idx]) {
		return; // monotonic: replaying a mission must not remove progress
	}
	values[idx] = missionsCompleted;
	WriteProgress(values);
}
