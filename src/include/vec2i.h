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
/**@name vec2i.h - Vec2i headerfile. */
//
//      (c) Copyright 2010 by Joris Dauphin
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//      02111-1307, USA.
//

#ifndef VEC2I_H
#define VEC2I_H

#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <tuple>

//@{

template <typename T>
class Vec2T
{
public:
	Vec2T() = default;
	Vec2T(T x, T y) : x(x), y(y) {}
	explicit Vec2T(std::tuple<T, T> t) : x(std::get<0>(t)), y(std::get<1>(t)) {}

public:
	T x{};
	T y{};
};


template <typename T>
inline bool operator == (const Vec2T<T> &lhs, const Vec2T<T> &rhs)
{
	return lhs.x == rhs.x && lhs.y == rhs.y;
}

template <typename T>
inline bool operator != (const Vec2T<T> &lhs, const Vec2T<T> &rhs)
{
	return !(lhs == rhs);
}

template <typename T>
inline bool operator < (const Vec2T<T> &lhs, const Vec2T<T> &rhs)
{
	return lhs.x < rhs.x && lhs.y < rhs.y;
}

template <typename T>
inline bool operator > (const Vec2T<T> &lhs, const Vec2T<T> &rhs)
{
	return lhs.x > rhs.x && lhs.y > rhs.y;
}

template <typename T>
inline const Vec2T<T> &operator += (Vec2T<T> &lhs, const Vec2T<T> &rhs)
{
	lhs.x += rhs.x;
	lhs.y += rhs.y;
	return lhs;
}

template <typename T>
inline const Vec2T<T> &operator -= (Vec2T<T> &lhs, const Vec2T<T> &rhs)
{
	lhs.x -= rhs.x;
	lhs.y -= rhs.y;
	return lhs;
}

template <typename T>
inline const Vec2T<T> &operator *= (Vec2T<T> &lhs, int rhs)
{
	lhs.x *= rhs;
	lhs.y *= rhs;
	return lhs;
}

template <typename T>
inline const Vec2T<T> &operator /= (Vec2T<T> &lhs, int rhs)
{
	lhs.x /= rhs;
	lhs.y /= rhs;
	return lhs;
}

template <typename T>
inline Vec2T<T> operator + (const Vec2T<T> &lhs, const Vec2T<T> &rhs)
{
	Vec2T<T> res(lhs);

	res += rhs;
	return res;
}

template <typename T>
inline Vec2T<T> operator - (const Vec2T<T> &lhs, const Vec2T<T> &rhs)
{
	Vec2T<T> res(lhs);

	res -= rhs;
	return res;
}

template <typename T>
inline Vec2T<T> operator * (const Vec2T<T> &lhs, int rhs)
{
	Vec2T<T> res(lhs);

	res *= rhs;
	return res;
}

template <typename T>
inline Vec2T<T> operator * (int lhs, const Vec2T<T> &rhs)
{
	Vec2T<T> res(rhs);

	res *= lhs;
	return res;
}

template <typename T>
inline Vec2T<T> operator / (const Vec2T<T> &lhs, int rhs)
{
	Vec2T<T> res(lhs);

	res /= rhs;
	return res;
}

template <typename T>
inline int SquareDistance(const Vec2T<T> &pos1, const Vec2T<T> &pos2)
{
	const Vec2T<T> diff = pos2 - pos1;

	return diff.x * diff.x + diff.y * diff.y;
}

template <typename T>
inline int Distance(const Vec2T<T> &pos1, const Vec2T<T> &pos2)
{
	return isqrt(SquareDistance(pos1, pos2));
}

using Vec2i = Vec2T<short int>;
using PixelPos = Vec2T<int>;
using PixelDiff = Vec2T<int>;
using PixelSize = Vec2T<int>;
using PixelPrecise = Vec2T<double>;
using TilePos = Vec2T<int16_t>;

/// The larger of the two axis gaps between the nearest tiles of two footprints
/// (pos = top-left tile, size in tiles); 0 when they overlap.
inline int ChebyshevEdgeDistance(const Vec2i &pos1, const Vec2i &size1,
								 const Vec2i &pos2, const Vec2i &size2)
{
	const int dx = std::max({0, pos2.x - (pos1.x + size1.x - 1), pos1.x - (pos2.x + size2.x - 1)});
	const int dy = std::max({0, pos2.y - (pos1.y + size1.y - 1), pos1.y - (pos2.y + size2.y - 1)});

	return std::max(dx, dy);
}

/// The point `dest` moved away from `from` along the same line until its larger axis offset is
/// `minRange` tiles; `dest` itself when it is that far already, when there is no minimum and
/// when the two points are the same (no direction).
inline PixelPos PushOutToRange(const PixelPos &from, const PixelPos &dest, int minRange,
							   const PixelPos &tileSize)
{
	const PixelDiff diff = dest - from;
	// the axis offsets in tiles, both over the common denominator tileSize.x * tileSize.y
	const int reach = std::max(std::abs(diff.x) * tileSize.y, std::abs(diff.y) * tileSize.x);
	const int limit = minRange * tileSize.x * tileSize.y;

	if (reach == 0 || reach >= limit) {
		return dest;
	}
	return PixelPos(from.x + diff.x * limit / reach, from.y + diff.y * limit / reach);
}

//@}

#endif // !VEC2I_H
