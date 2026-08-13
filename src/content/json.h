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
/// @file json.h - Minimal self-contained JSON reader for the content layer.
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#ifndef __CONTENT_JSON_H__
#define __CONTENT_JSON_H__

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace content
{

class JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

enum class JsonType { Null, Bool, Number, String, Array, Object };

/// A parsed JSON node. Small tagged value; arrays/objects are shared so copies are cheap.
class JsonValue
{
public:
	JsonType type = JsonType::Null;
	bool boolean = false;
	double number = 0.0;
	std::string string;
	std::shared_ptr<JsonArray> array;
	std::shared_ptr<JsonObject> object;

	bool isNull() const { return type == JsonType::Null; }
	bool isObject() const { return type == JsonType::Object; }
	bool isArray() const { return type == JsonType::Array; }
	bool isString() const { return type == JsonType::String; }
	bool isNumber() const { return type == JsonType::Number; }
	bool isBool() const { return type == JsonType::Bool; }

	/// Object member by key, or nullptr when absent / not an object.
	const JsonValue *find(const std::string &key) const;
	/// String value, or `fallback` when this is not a string.
	const std::string &asString(const std::string &fallback = std::string()) const;
	double asNumber(double fallback = 0.0) const;
	long asInt(long fallback = 0) const;
	bool asBool(bool fallback = false) const;
	/// Array items (empty vector when this is not an array).
	const JsonArray &items() const;
};

/// Parse a UTF-8 JSON document. Returns false and fills `errorMsg` on malformed input.
bool parseJson(const std::string &text, JsonValue &out, std::string &errorMsg);

} // namespace content

#endif // __CONTENT_JSON_H__
