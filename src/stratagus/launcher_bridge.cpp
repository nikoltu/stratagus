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
/**@name launcher_bridge.cpp - Messages between the engine and the activity hosting it. */
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

//@{

#include "stratagus.h"
#include "launcher_bridge.h"

#include <atomic>

#ifdef __ANDROID__
#include <SDL.h>
#include <jni.h>
#endif

/// The command left by the host; 0 = none. Written by the host's thread, taken by the engine's.
static std::atomic<int> PendingCommand{0};

void LauncherPost(int what, int value)
{
#ifdef __ANDROID__
	// SDL reserves the numbers below 0x8000 for itself; everything from there up is handed
	// to the activity's onUnhandledMessage.
	if (what >= 0 && what < 0x8000) {
		SDL_AndroidSendMessage(0x8000 + what, value);
	}
#else
	(void)what;
	(void)value;
#endif
}

int LauncherPollCommand()
{
	return PendingCommand.exchange(0);
}

/// Set by the host before the engine's main function runs; never changes afterwards.
static std::atomic<bool> Hosted{false};

bool LauncherHosted()
{
	return Hosted.load();
}

#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL
Java_net_wargushd_game_WargusHDActivity_nativeLauncherCommand(JNIEnv *, jclass, jint command)
{
	PendingCommand.store(command);
}

extern "C" JNIEXPORT void JNICALL
Java_net_wargushd_game_WargusHDActivity_nativeLauncherHost(JNIEnv *, jclass, jboolean hosted)
{
	Hosted.store(hosted != JNI_FALSE);
}
#endif

//@}
