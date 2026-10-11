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
/**@name debug_bridge.h - In-engine debug bridge (bug catcher + state monitor). */
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

#ifndef DEBUG_BRIDGE_H
#define DEBUG_BRIDGE_H

//@{

#include <atomic>
#include <string>
#include <vector>

/**
**  The Debug Bridge is an observe-only, file-based command/response channel plus
**  a crash catcher. It lets an external operator (e.g. over `adb ... run-as`)
**  pull engine state and crash backtraces without seeing the screen.
**
**  Files live under `<UserDirectory>/wdbg/`:
**    - cmd          operator writes a one-line command here
**    - out.json     engine writes the JSON result of the last command
**    - status.json  cheap heartbeat, refreshed on every throttled poll
**    - crash.txt    signal + backtrace written by the crash handler
**
**  It must be a no-op on performance when idle: only a throttled `stat` of the
**  command file (every ~15 frames). It never mutates game state.
*/

/// Create the wdbg dir, install crash handlers, write an initial status file.
void DebugBridge_Init();

/// Call once per rendered frame from the main loop. Cheap: mostly a throttled stat.
void DebugBridge_Poll();

/// Record a fatal C++ exception (kind + what()) plus a backtrace to crash.txt and logcat.
/// Call from the top-level catch blocks BEFORE terminating: on Android an uncaught exception
/// unwinds and _Exit()s with no signal, so the signal handler never runs and crash.txt would
/// otherwise be empty. This captures the one path that actually reaches CrashExit.
void DebugBridge_RecordFatal(const char *kind, const char *what);

/// Lightweight description of a loaded CGraphic, filled by the video module so the
/// `assets` command can enumerate the existing (file-scope) graphic registry without
/// exposing its internals here. Implemented in src/video/graphic.cpp.
struct DebugGraphicInfo {
	std::string file;
	int width = 0;
	int height = 0;
	int numFrames = 0;
};
void DebugBridge_CollectGraphics(std::vector<DebugGraphicInfo> &out);

/// Verb `touch`: start a finger gesture script in the touch layer. Implemented in
/// src/video/sdl.cpp (the touch layer is file-scope there). False + error if not taken.
bool DebugBridge_TouchScript(const std::string &script, std::string &error);
/// Verb `touchinfo`: the touch layer's state as JSON members. Implemented in src/video/sdl.cpp.
std::string DebugBridge_TouchState();

/*----------------------------------------------------------------------------
--  Main-loop watchdog
----------------------------------------------------------------------------*/

// A background thread samples a heartbeat that the main loop bumps at labelled
// checkpoints. If the heartbeat stops advancing (main thread wedged) it logs the
// last checkpoint to logcat + crash.txt. This catches a silent hang that raises
// no signal and throws no C++ exception (an ANR that Android just kills), telling
// us WHERE the main thread stuck instead of leaving crash.txt empty.
extern std::atomic<const char *> DbgCheckpoint;   /// last checkpoint label (string literal)
extern std::atomic<unsigned long> DbgHeartbeat;   /// bumped at every checkpoint

inline void DebugBridge_Checkpoint(const char *label)
{
	DbgCheckpoint.store(label, std::memory_order_relaxed);
	DbgHeartbeat.fetch_add(1, std::memory_order_relaxed);
}

#define DBG_CHECKPOINT(label) DebugBridge_Checkpoint(label)

/// Launch the watchdog thread (idempotent). Called from DebugBridge_Init.
void DebugBridge_StartWatchdog();

//@}

#endif // DEBUG_BRIDGE_H
