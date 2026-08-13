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
/// @file sha256.h - Compact SHA-256 for content-pack integrity validation.
//
//      (c) Copyright 2026 by The Stratagus Project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.

#ifndef __CONTENT_SHA256_H__
#define __CONTENT_SHA256_H__

#include <cstddef>
#include <cstdint>
#include <string>

namespace content
{

/// Lowercase hex SHA-256 of a byte buffer.
std::string Sha256Hex(const uint8_t *data, size_t length);
std::string Sha256Hex(const std::string &data);

} // namespace content

#endif // __CONTENT_SHA256_H__
