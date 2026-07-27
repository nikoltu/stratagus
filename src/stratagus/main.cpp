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
/**@name main.cpp - The main file. */
//
//      (c) Copyright 2013 by Joris Dauphin
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

#include <SDL.h>
#include <cstdio>
#include <cstdlib>

#ifdef __ANDROID__
#include <android/log.h>
#include <pthread.h>
#include <string>
#include <unistd.h>

// Android has no console, so anything the engine writes to stdout/stderr is lost.
// Redirect both to logcat (tag "WargusHD") by piping them through a reader thread
// that forwards each line via __android_log_write. This is the only window into
// the engine's own diagnostics on device.
static int androidStdioPipe[2];

static void *androidStdioReader(void *)
{
	std::string line;
	char c;
	while (read(androidStdioPipe[0], &c, 1) == 1) {
		if (c == '\n') {
			__android_log_write(ANDROID_LOG_INFO, "WargusHD", line.c_str());
			line.clear();
		} else if (c != '\r') {
			line.push_back(c);
		}
	}
	if (!line.empty()) {
		__android_log_write(ANDROID_LOG_INFO, "WargusHD", line.c_str());
	}
	return nullptr;
}

static void redirectStdioToLogcat()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	if (pipe(androidStdioPipe) != 0) {
		return;
	}
	dup2(androidStdioPipe[1], STDOUT_FILENO);
	dup2(androidStdioPipe[1], STDERR_FILENO);
	pthread_t thread;
	if (pthread_create(&thread, nullptr, androidStdioReader, nullptr) == 0) {
		pthread_detach(thread);
	}
}
#endif

int main(int argc, char **argv)
{
#ifdef __ANDROID__
	redirectStdioToLogcat();
#endif
	if (std::getenv("STRATAGUS_UNBUFFERED_STDIO")) {
		setvbuf(stdout, nullptr, _IONBF, 0);
		setvbuf(stderr, nullptr, _IONBF, 0);
	}
	return stratagusMain(argc, argv);
}

//@}
