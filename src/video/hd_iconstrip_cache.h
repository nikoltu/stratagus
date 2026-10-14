//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name hd_iconstrip_cache.h - The device-side cache file of the built command-icon strip. */
//
//      (c) Copyright 2026 by the WargusHD project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.

#ifndef HD_ICONSTRIP_CACHE_H
#define HD_ICONSTRIP_CACHE_H

//@{

// The icon strip (BuildHDIconStrip) is built from the player's own game art; building it takes
// seconds. The finished pixels are kept in a file in the app's cache directory on the device and
// read back while everything that determines them is unchanged. The file is made at run time from
// the player's files and stays in that directory: it is never packaged and never copied elsewhere.
//
// File = header (HEADER_BYTES, little endian) + the strip's rows, 4 bytes a pixel, R G B A.
// Standard library only (the host tests include this header alone).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace hdiconcache
{

/// Bump with ANY change to BuildHDIconStrip that can change a pixel of the strip (the source
/// frames taken, the scaling, the team key): files of an older builder are then rebuilt.
constexpr uint32_t BUILDER_VERSION = 1;

constexpr uint32_t MAGIC = 0x53494448;  // "HDIS"
constexpr uint32_t FORMAT = 1;          // the layout of this header
constexpr size_t SET_BYTES = 16;
constexpr int SOURCES = 4;              // Portrait-face .json .png, Portrait-mask .json .png
constexpr size_t HEADER_BYTES = 6 * 4 + SET_BYTES + SOURCES * 16 + 4 * 8;

/// A source file as the key sees it. An absent file is {0, 0}.
struct SourceStamp {
	uint64_t size = 0;
	int64_t mtime = 0;
};

/// Everything the strip's pixels depend on.
struct Key {
	uint32_t builder = BUILDER_VERSION;
	uint32_t width = 0;
	uint32_t height = 0;
	std::string set;                 // forest / ice / swamp / xswamp
	SourceStamp sources[SOURCES];
	uint64_t pathHash = 0;           // of the resolved source paths (another game folder, a mod)
};

/// FNV-1a over bytes; for the paths and the header.
inline uint64_t hashBytes(const void *data, size_t len, uint64_t h = 1469598103934665603ULL)
{
	const unsigned char *p = static_cast<const unsigned char *>(data);
	for (size_t i = 0; i < len; ++i) {
		h = (h ^ p[i]) * 1099511628211ULL;
	}
	return h;
}

/// Sum of the pixel block, eight bytes a step (a strip is 14.6 MB; this is read speed).
inline uint64_t payloadSum(const void *data, size_t len, uint64_t h = 1469598103934665603ULL)
{
	const unsigned char *p = static_cast<const unsigned char *>(data);
	size_t i = 0;
	for (; i + 8 <= len; i += 8) {
		uint64_t v = 0;
		for (int b = 0; b < 8; ++b) {
			v |= (uint64_t) p[i + b] << (8 * b);
		}
		h = (h ^ v) * 1099511628211ULL;
		h ^= h >> 29;
	}
	return hashBytes(p + i, len - i, h);
}

/// The cache file's name for a set; empty when the set name cannot be a file name part.
inline std::string fileName(const std::string &set)
{
	if (set.empty() || set.size() >= SET_BYTES) {
		return std::string();
	}
	for (const char c : set) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
		if (!ok) {
			return std::string();
		}
	}
	return "hd_iconstrip_" + set + ".bin";
}

namespace detail
{
inline void put32(unsigned char *&p, uint32_t v)
{
	for (int b = 0; b < 4; ++b) {
		*p++ = (unsigned char) (v >> (8 * b));
	}
}
inline void put64(unsigned char *&p, uint64_t v)
{
	for (int b = 0; b < 8; ++b) {
		*p++ = (unsigned char) (v >> (8 * b));
	}
}
inline uint32_t get32(const unsigned char *&p)
{
	uint32_t v = 0;
	for (int b = 0; b < 4; ++b) {
		v |= (uint32_t) *p++ << (8 * b);
	}
	return v;
}
inline uint64_t get64(const unsigned char *&p)
{
	uint64_t v = 0;
	for (int b = 0; b < 8; ++b) {
		v |= (uint64_t) *p++ << (8 * b);
	}
	return v;
}
} // namespace detail

/// The header of a file holding the strip of `key`: `pixelSum` = payloadSum of its pixel block.
inline std::array<unsigned char, HEADER_BYTES> encodeHeader(const Key &key, bool teamKey, uint64_t pixelSum)
{
	std::array<unsigned char, HEADER_BYTES> out{};
	unsigned char *p = out.data();
	detail::put32(p, MAGIC);
	detail::put32(p, FORMAT);
	detail::put32(p, key.builder);
	detail::put32(p, key.width);
	detail::put32(p, key.height);
	detail::put32(p, teamKey ? 1 : 0);
	std::memcpy(p, key.set.data(), key.set.size() < SET_BYTES ? key.set.size() : SET_BYTES - 1);
	p += SET_BYTES;
	for (const SourceStamp &s : key.sources) {
		detail::put64(p, s.size);
		detail::put64(p, (uint64_t) s.mtime);
	}
	detail::put64(p, key.pathHash);
	detail::put64(p, (uint64_t) key.width * key.height * 4);
	detail::put64(p, pixelSum);
	detail::put64(p, hashBytes(out.data(), HEADER_BYTES - 8));
	return out;
}

/// Is `header` (the first `len` bytes of a file of `fileSize` bytes) the header of an undamaged
/// file for exactly `key`? On true: the team-key flag and the sum the pixel block must have.
inline bool checkHeader(const unsigned char *header, size_t len, uint64_t fileSize, const Key &key,
                        bool &teamKey, uint64_t &pixelSum)
{
	if (!header || len < HEADER_BYTES || key.set.empty() || key.set.size() >= SET_BYTES) {
		return false;
	}
	const unsigned char *p = header + HEADER_BYTES - 8;
	if (detail::get64(p) != hashBytes(header, HEADER_BYTES - 8)) {
		return false; // damaged header
	}
	p = header;
	if (detail::get32(p) != MAGIC || detail::get32(p) != FORMAT || detail::get32(p) != key.builder
		|| detail::get32(p) != key.width || detail::get32(p) != key.height) {
		return false;
	}
	const uint32_t team = detail::get32(p);
	if (team > 1) {
		return false;
	}
	char set[SET_BYTES] = {};
	std::memcpy(set, key.set.data(), key.set.size());
	if (std::memcmp(p, set, SET_BYTES) != 0) {
		return false;
	}
	p += SET_BYTES;
	for (const SourceStamp &s : key.sources) {
		if (detail::get64(p) != s.size || detail::get64(p) != (uint64_t) s.mtime) {
			return false; // the game folder changed
		}
	}
	if (detail::get64(p) != key.pathHash) {
		return false;
	}
	const uint64_t pixels = (uint64_t) key.width * key.height * 4;
	if (detail::get64(p) != pixels || fileSize != HEADER_BYTES + pixels) {
		return false; // cut short, or grown
	}
	pixelSum = detail::get64(p);
	teamKey = team == 1;
	return true;
}

} // namespace hdiconcache

//@}

#endif // HD_ICONSTRIP_CACHE_H
