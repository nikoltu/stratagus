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
/// @file presentation.cpp - Global presentation pack index + Lua accessors.
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#include "presentation.h"

#include "content_set.h"

#include <cstdio>
#include <map>
#include <vector>

namespace
{

content::ContentSet g_contentSet;
const content::JsonValue *g_presentation = nullptr;
std::vector<std::string> g_screenOrder;
std::map<std::string, const content::JsonValue *> g_screenById;

const content::JsonValue *screen(const std::string &id)
{
	auto it = g_screenById.find(id);
	return it == g_screenById.end() ? nullptr : it->second;
}

const std::string &fieldString(const content::JsonValue *node, const std::string &key)
{
	static const std::string empty;
	if (!node) {
		return empty;
	}
	const content::JsonValue *value = node->find(key);
	return value ? value->asString() : empty;
}

} // namespace

bool LoadContentSet(const std::string &cacheDir)
{
	g_presentation = nullptr;
	g_screenOrder.clear();
	g_screenById.clear();

	const std::string manifest = cacheDir + "/content-set.json";
	if (!g_contentSet.load(manifest)) {
		fprintf(stderr, "content: no runtime content set at %s (%s)\n",
		        manifest.c_str(), g_contentSet.error().c_str());
		return false;
	}
	const content::ContentPack *pack = g_contentSet.findPack("presentation");
	if (!pack) {
		fprintf(stderr, "content: content set has no presentation pack\n");
		return false;
	}
	g_presentation = &pack->data;
	if (const content::JsonValue *screens = g_presentation->find("screens")) {
		for (const content::JsonValue &s : screens->items()) {
			const std::string id = fieldString(&s, "id");
			if (!id.empty()) {
				g_screenOrder.push_back(id);
				g_screenById[id] = &s;
			}
		}
	}
	fprintf(stderr, "content: loaded content set (%s), presentation pack: %zu screens\n",
	        g_contentSet.fidelityProfile().c_str(), g_screenOrder.size());
	return true;
}

bool PresentationLoaded()
{
	return g_presentation != nullptr;
}

int PresentationScreenCount()
{
	return static_cast<int>(g_screenOrder.size());
}

std::string PresentationScreenId(int index)
{
	if (index < 0 || index >= static_cast<int>(g_screenOrder.size())) {
		return std::string();
	}
	return g_screenOrder[static_cast<size_t>(index)];
}

std::string PresentationScreenField(const std::string &screenId, const std::string &field)
{
	const content::JsonValue *node = screen(screenId);
	if (!node) {
		return std::string();
	}
	if (field.rfind("layout.", 0) == 0) {
		const content::JsonValue *layout = node->find("layout");
		return layout ? fieldString(layout, field.substr(7)) : std::string();
	}
	return fieldString(node, field);
}

int PresentationControlCount(const std::string &screenId)
{
	const content::JsonValue *node = screen(screenId);
	if (!node) {
		return 0;
	}
	const content::JsonValue *controls = node->find("controls");
	return controls ? static_cast<int>(controls->items().size()) : 0;
}

std::string PresentationControlField(const std::string &screenId, int order, const std::string &field)
{
	const content::JsonValue *node = screen(screenId);
	if (!node) {
		return std::string();
	}
	const content::JsonValue *controls = node->find("controls");
	if (!controls || order < 0 || order >= static_cast<int>(controls->items().size())) {
		return std::string();
	}
	return fieldString(&controls->items()[static_cast<size_t>(order)], field);
}
