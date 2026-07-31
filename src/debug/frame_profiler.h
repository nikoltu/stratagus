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
/**@name frame_profiler.h - Per-frame wall-clock section profiler. */
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

#ifndef FRAME_PROFILER_H
#define FRAME_PROFILER_H

//@{

/**
**  Tiny allocation-free wall-clock profiler (SDL_GetPerformanceCounter) for the
**  major per-frame phases. Accumulates total ms per named section plus a global
**  rendered-frame count; the `prof` debug-bridge command reports averages over the
**  window and resets it. Overhead is a couple of QueryPerformanceCounter calls per
**  section per frame; no logging on the hot path.
*/

enum FrameProfSection {
	FP_LOGIC = 0,     /// RunOneGameCycle (accumulates over the 0..N cycles per frame)
	FP_MINIMAP,       /// UI.Minimap.Update()
	FP_FOG,           /// FogOfWar->Update()
	FP_UPDATEDISPLAY, /// UpdateDisplay() (GPU world draw + CPU HUD overlay)
	FP_WORLD,         /// DrawMapArea() — GPU world layers (tiles/units/fog) [subset of updatedisplay]
	FP_HUD,           /// HUD/minimap/panels/guichan/cursor CPU draw [subset of updatedisplay]
	FP_REALIZE,       /// RealizeVideoMemory() (upload + RenderCopy + present)
	FP_PRESENT,       /// SDL_RenderPresent inside EndFrame() (subset of realize)
	FP_SECTION_COUNT
};

/// Start timing a section.
void FrameProfBegin(FrameProfSection section);
/// Stop timing a section, adding the elapsed time to its accumulator.
void FrameProfEnd(FrameProfSection section);
/// Mark the end of one rendered frame (advance the per-frame divisor).
void FrameProfEndFrame();

/// RAII helper: times the enclosing scope into the named section.
struct FrameProfScope {
	FrameProfSection sec;
	explicit FrameProfScope(FrameProfSection s) : sec(s) { FrameProfBegin(s); }
	~FrameProfScope() { FrameProfEnd(sec); }
};

//@}

#endif // FRAME_PROFILER_H
