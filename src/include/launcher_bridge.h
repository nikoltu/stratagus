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
/**@name launcher_bridge.h - Messages between the engine and the activity hosting it. */
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

#ifndef __LAUNCHER_BRIDGE_H__
#define __LAUNCHER_BRIDGE_H__

//@{

/**
**  Tell the hosting activity something (it handles the message on its own thread).
**  Does nothing where the engine is not hosted by an activity.
**
**  @param what   Message number, 0..0x7FFF (the meanings are the scripts' and the host's).
**  @param value  One number that goes with it.
*/
extern void LauncherPost(int what, int value);

/**
**  Take the command the hosting activity has left for the engine, if any.
**  The host only stores a command; it is acted on when a script asks for it here.
**
**  @return  The command number, or 0 when there is none.
*/
extern int LauncherPollCommand();

/// Message numbers the host understands (LauncherPost's `what`).
enum {
	LauncherMsgLoadingBegin = 1, /// loading has begun; the host shows its loading screen
	LauncherMsgLoadingEnd = 2,   /// the first frame after loading is on screen
	LauncherMsgGameEnded = 4     /// the game's last picture is on screen; the host may copy it
};

/// Command numbers the host leaves for the engine itself (the scripts have their own).
enum {
	LauncherCmdFrameTaken = 9    /// the host has dealt with LauncherMsgGameEnded
};

/**
**  A game has just ended: put its last picture on screen once more, without the banner that
**  ended it, and give the host the time to take a copy for what it shows above the game.
**  Does nothing where the engine is not hosted.
*/
extern void LauncherShowLastGameFrame();

/**
**  Does the hosting activity draw the loading screen?
**  When it does, the engine only reports when loading begins and ends.
*/
extern bool LauncherHosted();

//@}

#endif // !__LAUNCHER_BRIDGE_H__
