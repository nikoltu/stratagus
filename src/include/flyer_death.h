//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \   __\_  __ \__  \   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name flyer_death.h - The shadow of a dying flyer. */
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


#ifndef FLYER_DEATH_H
#define FLYER_DEATH_H

// A killed flyer does not fall: its Death pictures (explosion, then smoke) are drawn where it
// flew, and its shadow stays on the ground under them for about the first 20 frames of the
// Death, then goes (measured on a killed Dragon in the original). A unit on the ground or the
// sea draws no shadow while it dies, as before.

/// Frames of the Death during which a flyer's shadow is still drawn
constexpr int FlyerDeathShadowFrames = 20;

/// Whether the shadow is drawn while the Death has run for the given number of frames
/// (1 = its first frame is on screen; 0 = killed, the Death not yet begun)
constexpr bool FlyerDeathShadowDrawn(bool airUnit, int deathFrames)
{
	return airUnit && deathFrames <= FlyerDeathShadowFrames;
}

#endif // FLYER_DEATH_H
