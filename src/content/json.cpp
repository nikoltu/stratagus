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
/// @file json.cpp - Minimal recursive-descent JSON reader (RFC 8259 subset).
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#include "json.h"

#include <cstdlib>

namespace content
{

static const std::string kEmptyString;
static const JsonArray kEmptyArray;

const JsonValue *JsonValue::find(const std::string &key) const
{
	if (type != JsonType::Object || !object) {
		return nullptr;
	}
	auto it = object->find(key);
	return it == object->end() ? nullptr : &it->second;
}

const std::string &JsonValue::asString(const std::string &fallback) const
{
	return type == JsonType::String ? string : (fallback.empty() ? kEmptyString : fallback);
}

double JsonValue::asNumber(double fallback) const
{
	return type == JsonType::Number ? number : fallback;
}

long JsonValue::asInt(long fallback) const
{
	return type == JsonType::Number ? static_cast<long>(number) : fallback;
}

bool JsonValue::asBool(bool fallback) const
{
	return type == JsonType::Bool ? boolean : fallback;
}

const JsonArray &JsonValue::items() const
{
	return (type == JsonType::Array && array) ? *array : kEmptyArray;
}

namespace
{

/// One-pass recursive-descent parser over a UTF-8 buffer.
class Parser
{
public:
	Parser(const std::string &text) : text_(text) {}

	bool parse(JsonValue &out, std::string &error)
	{
		skipWhitespace();
		if (!parseValue(out)) {
			error = error_.empty() ? "malformed JSON" : error_;
			return false;
		}
		skipWhitespace();
		if (pos_ != text_.size()) {
			error = "trailing characters after JSON value";
			return false;
		}
		return true;
	}

private:
	const std::string &text_;
	size_t pos_ = 0;
	std::string error_;

	bool eof() const { return pos_ >= text_.size(); }
	char peek() const { return eof() ? '\0' : text_[pos_]; }

	void skipWhitespace()
	{
		while (!eof()) {
			const char c = text_[pos_];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				++pos_;
			} else {
				break;
			}
		}
	}

	bool fail(const char *message)
	{
		if (error_.empty()) {
			error_ = message;
		}
		return false;
	}

	bool parseValue(JsonValue &out)
	{
		skipWhitespace();
		if (eof()) {
			return fail("unexpected end of input");
		}
		switch (peek()) {
			case '{': return parseObject(out);
			case '[': return parseArray(out);
			case '"': return parseString(out);
			case 't': case 'f': return parseBool(out);
			case 'n': return parseNull(out);
			default: return parseNumber(out);
		}
	}

	bool match(const char *literal, JsonType type, bool boolValue, JsonValue &out)
	{
		for (size_t i = 0; literal[i]; ++i) {
			if (pos_ + i >= text_.size() || text_[pos_ + i] != literal[i]) {
				return fail("invalid literal");
			}
		}
		pos_ += std::string(literal).size();
		out = JsonValue();
		out.type = type;
		out.boolean = boolValue;
		return true;
	}

	bool parseBool(JsonValue &out)
	{
		return peek() == 't' ? match("true", JsonType::Bool, true, out)
		                     : match("false", JsonType::Bool, false, out);
	}

	bool parseNull(JsonValue &out) { return match("null", JsonType::Null, false, out); }

	bool parseNumber(JsonValue &out)
	{
		const size_t start = pos_;
		if (peek() == '-') {
			++pos_;
		}
		bool anyDigit = false;
		while (!eof()) {
			const char c = text_[pos_];
			if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E') {
				anyDigit = anyDigit || (c >= '0' && c <= '9');
				++pos_;
			} else {
				break;
			}
		}
		if (!anyDigit) {
			return fail("invalid number");
		}
		out = JsonValue();
		out.type = JsonType::Number;
		out.number = std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr);
		return true;
	}

	bool parseString(JsonValue &out)
	{
		std::string value;
		if (!parseRawString(value)) {
			return false;
		}
		out = JsonValue();
		out.type = JsonType::String;
		out.string = std::move(value);
		return true;
	}

	static void appendUtf8(std::string &value, unsigned code)
	{
		if (code <= 0x7f) {
			value.push_back(static_cast<char>(code));
		} else if (code <= 0x7ff) {
			value.push_back(static_cast<char>(0xc0 | (code >> 6)));
			value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		} else {
			value.push_back(static_cast<char>(0xe0 | (code >> 12)));
			value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
			value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		}
	}

	bool parseHex4(unsigned &out)
	{
		if (pos_ + 4 > text_.size()) {
			return fail("truncated \\u escape");
		}
		unsigned value = 0;
		for (int i = 0; i < 4; ++i) {
			const char c = text_[pos_++];
			value <<= 4;
			if (c >= '0' && c <= '9') {
				value |= static_cast<unsigned>(c - '0');
			} else if (c >= 'a' && c <= 'f') {
				value |= static_cast<unsigned>(c - 'a' + 10);
			} else if (c >= 'A' && c <= 'F') {
				value |= static_cast<unsigned>(c - 'A' + 10);
			} else {
				return fail("invalid \\u escape");
			}
		}
		out = value;
		return true;
	}

	bool parseRawString(std::string &value)
	{
		if (peek() != '"') {
			return fail("expected string");
		}
		++pos_;
		while (!eof()) {
			const char c = text_[pos_++];
			if (c == '"') {
				return true;
			}
			if (c == '\\') {
				if (eof()) {
					return fail("truncated escape");
				}
				const char e = text_[pos_++];
				switch (e) {
					case '"': value.push_back('"'); break;
					case '\\': value.push_back('\\'); break;
					case '/': value.push_back('/'); break;
					case 'b': value.push_back('\b'); break;
					case 'f': value.push_back('\f'); break;
					case 'n': value.push_back('\n'); break;
					case 'r': value.push_back('\r'); break;
					case 't': value.push_back('\t'); break;
					case 'u': {
						unsigned code = 0;
						if (!parseHex4(code)) {
							return false;
						}
						if (code >= 0xd800 && code <= 0xdbff && pos_ + 1 < text_.size()
						    && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
							pos_ += 2;
							unsigned low = 0;
							if (!parseHex4(low)) {
								return false;
							}
							code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
							value.push_back(static_cast<char>(0xf0 | (code >> 18)));
							value.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
							value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
							value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
						} else {
							appendUtf8(value, code);
						}
						break;
					}
					default: return fail("invalid escape");
				}
			} else {
				value.push_back(c);
			}
		}
		return fail("unterminated string");
	}

	bool parseArray(JsonValue &out)
	{
		++pos_; // consume '['
		auto items = std::make_shared<JsonArray>();
		skipWhitespace();
		if (peek() == ']') {
			++pos_;
		} else {
			while (true) {
				JsonValue element;
				if (!parseValue(element)) {
					return false;
				}
				items->push_back(std::move(element));
				skipWhitespace();
				const char c = peek();
				if (c == ',') {
					++pos_;
					continue;
				}
				if (c == ']') {
					++pos_;
					break;
				}
				return fail("expected ',' or ']' in array");
			}
		}
		out = JsonValue();
		out.type = JsonType::Array;
		out.array = items;
		return true;
	}

	bool parseObject(JsonValue &out)
	{
		++pos_; // consume '{'
		auto members = std::make_shared<JsonObject>();
		skipWhitespace();
		if (peek() == '}') {
			++pos_;
		} else {
			while (true) {
				skipWhitespace();
				std::string key;
				if (!parseRawString(key)) {
					return false;
				}
				skipWhitespace();
				if (peek() != ':') {
					return fail("expected ':' after object key");
				}
				++pos_;
				JsonValue value;
				if (!parseValue(value)) {
					return false;
				}
				(*members)[key] = std::move(value);
				skipWhitespace();
				const char c = peek();
				if (c == ',') {
					++pos_;
					continue;
				}
				if (c == '}') {
					++pos_;
					break;
				}
				return fail("expected ',' or '}' in object");
			}
		}
		out = JsonValue();
		out.type = JsonType::Object;
		out.object = members;
		return true;
	}
};

} // namespace

bool parseJson(const std::string &text, JsonValue &out, std::string &errorMsg)
{
	Parser parser(text);
	return parser.parse(out, errorMsg);
}

} // namespace content
