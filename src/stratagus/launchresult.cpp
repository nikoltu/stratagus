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
/**@name launchresult.cpp - Result record of a game started by a launch request. */
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

#include "stratagus.h"
#include "launchresult.h"

#include "game.h"
#include "parameters.h"

#include <cstdio>
#include <string>
#include <system_error>

unsigned long LastGameCycle = 0;

static fs::path LaunchResultDirectory()
{
	fs::path path = Parameters::Instance.GetUserDirectory();
	if (!GameName.empty()) {
		path /= GameName;
	}
	path /= "launch";
	return path;
}

/// Replace one file of the launch directory as a whole.
static bool WriteLaunchFile(const std::string &name, const std::string &text)
{
	const fs::path dir = LaunchResultDirectory();
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (ec) {
		ErrorPrint("Launch result: cannot create '%s'\n", dir.string().c_str());
		return false;
	}
	const fs::path target = dir / name;
	const fs::path temporary = dir / (name + ".tmp");

	FILE *fd = fopen(temporary.string().c_str(), "wb");
	if (!fd) {
		ErrorPrint("Launch result: cannot write '%s'\n", temporary.string().c_str());
		return false;
	}
	const size_t written = fwrite(text.data(), 1, text.size(), fd);
	const bool closed = fclose(fd) == 0;
	if (written != text.size() || !closed) {
		ErrorPrint("Launch result: short write to '%s'\n", temporary.string().c_str());
		fs::remove(temporary, ec);
		return false;
	}
	fs::rename(temporary, target, ec);
	if (ec) {
		ErrorPrint("Launch result: cannot replace '%s'\n", target.string().c_str());
		fs::remove(temporary, ec);
		return false;
	}
	return true;
}

bool WriteLaunchResult(const std::string &text)
{
	return WriteLaunchFile("result.json", text);
}

bool WriteLaunchLoading(const std::string &text)
{
	return WriteLaunchFile("loading.txt", text);
}
