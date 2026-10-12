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
#include <deque>
#include <mutex>
#include <string>

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

/// The chat lines kept for the host: the last ones, each "rrggbb<tab>name<tab>text" (the colour
/// is "-" for a line without a player), and how many were added in all.
static constexpr size_t ChatLinesKept = 50;
/// Typed lines the engine has not taken yet; more are dropped
static constexpr size_t ChatTypedKept = 16;
static std::mutex ChatLock;
static std::deque<std::string> ChatLines;
static std::deque<std::string> ChatTyped;
static unsigned ChatAdded = 0;

static std::string ChatField(std::string field)
{
	for (char &c : field) {
		if (c == '\t' || c == '\n' || c == '\r') {
			c = ' ';
		}
	}
	return field;
}

void LauncherChatReset()
{
	std::lock_guard<std::mutex> lock(ChatLock);
	ChatLines.clear();
	ChatTyped.clear();
	ChatAdded = 0;
}

void LauncherChatAdd(int rgb, const std::string &name, const std::string &text)
{
	char colour[8] = "-";
	unsigned added;

	if (rgb >= 0) {
		snprintf(colour, sizeof(colour), "%06x", rgb & 0xFFFFFF);
	}
	// The host splits at tabs and line ends; a name or a text holds none of its own.
	const std::string line = std::string(colour) + "\t" + ChatField(name) + "\t" + ChatField(text);
	{
		std::lock_guard<std::mutex> lock(ChatLock);
		if (ChatLines.size() == ChatLinesKept) {
			ChatLines.pop_front();
		}
		ChatLines.push_back(line);
		added = ++ChatAdded;
	}
	LauncherPost(LauncherMsgChatLine, int(added & 0x7FFFFFFF));
}

bool LauncherChatTakeTyped(std::string &text)
{
	std::lock_guard<std::mutex> lock(ChatLock);
	if (ChatTyped.empty()) {
		return false;
	}
	text = ChatTyped.front();
	ChatTyped.pop_front();
	return true;
}

unsigned LauncherChatCount()
{
	std::lock_guard<std::mutex> lock(ChatLock);
	return ChatAdded;
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

/// The host's player has typed a chat line (UTF-8 bytes); the engine takes it in its next frame.
extern "C" JNIEXPORT void JNICALL
Java_net_wargushd_game_WargusHDActivity_nativeChatSend(JNIEnv *env, jclass, jbyteArray text)
{
	if (text == nullptr) {
		return;
	}
	const jsize size = env->GetArrayLength(text);
	if (size <= 0 || size > 1024) {
		return;
	}
	std::string line(size_t(size), '\0');
	env->GetByteArrayRegion(text, 0, size, reinterpret_cast<jbyte *>(&line[0]));
	std::lock_guard<std::mutex> lock(ChatLock);
	if (ChatTyped.size() < ChatTypedKept) {
		ChatTyped.push_back(line);
	}
}

/// The chat lines kept, as UTF-8 bytes: one line each, ended by a line end.
extern "C" JNIEXPORT jbyteArray JNICALL
Java_net_wargushd_game_WargusHDActivity_nativeChatLines(JNIEnv *env, jclass)
{
	std::string all;
	{
		std::lock_guard<std::mutex> lock(ChatLock);
		for (const std::string &line : ChatLines) {
			all += line;
			all += '\n';
		}
	}
	jbyteArray out = env->NewByteArray(jsize(all.size()));
	if (out != nullptr && !all.empty()) {
		env->SetByteArrayRegion(out, 0, jsize(all.size()), reinterpret_cast<const jbyte *>(all.data()));
	}
	return out;
}

extern "C" JNIEXPORT void JNICALL
Java_net_wargushd_game_WargusHDActivity_nativeLauncherHost(JNIEnv *, jclass, jboolean hosted)
{
	Hosted.store(hosted != JNI_FALSE);
}
#endif

//@}
