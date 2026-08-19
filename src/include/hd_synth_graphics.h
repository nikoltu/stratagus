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
/**@name hd_synth_graphics.h - Engine-synthesised HD command-icon graphics. */
//
//      (c) Copyright 2026 by the WargusHD project
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.

#ifndef __HD_SYNTH_GRAPHICS_H__
#define __HD_SYNTH_GRAPHICS_H__

//@{

struct SDL_Surface;

/// WargusHD command-icon cell geometry (IC1). Shipped/preserved values: a 150x124 cell with the
/// HD portrait seated at an 18px inset (-> 114x88 inner), matching icons.lua and the 2026-07 bake.
namespace hdicon
{
constexpr int CELL_W = 150;
constexpr int CELL_H = 124;
constexpr int ICON_INSET = 18;   // px inset of the icon inside the portrait_button frame opening
constexpr int SLOTS = 196;       // classic command-icon slot count (0..195)
constexpr int FRAME_STATES = 6;  // default, hovered, active, highlight, highlight_hovered, highlight_active
} // namespace hdicon

/// Build the BORDERLESS HD command-icon strip natively from the Remastered
/// `Art/hd/HUD/Portrait-face` atlas: 150 x (124*196) RGBA32, slot N == frame `forest_N` seated
/// at (18,18) sized 114x88. Returns a fresh SDL_Surface (caller owns) or nullptr if the native
/// atlas is unavailable (caller then falls back to the on-disk file).
extern SDL_Surface *BuildHDIconStrip();

/// Build the command-cell FRAME strip natively from the Remastered `skins/Modern_HUD_Widgets`
/// atlas: 150 x (124*6) RGBA32, one 9-sliced (border 18) portrait_button state per frame in the
/// order default,hovered,active,highlight,highlight_hovered,highlight_active. Returns a fresh
/// SDL_Surface (caller owns) or nullptr if the native atlas is unavailable.
extern SDL_Surface *BuildHDCommandFrameStrip();

/// Crop a single frame from the Remastered sidebar background atlas `Data/Art/hd/HUD/BG_{race}.json`.
/// @param race "Human" or "Orc"
/// @param frameName "TopLeft" (855x2160) or "TileableLeft" (855x630)
/// Returns a fresh RGBA32 SDL_Surface (caller owns) or nullptr if the atlas is unavailable.
extern SDL_Surface *BuildHDSidebarPanel(const char *race, const char *frameName);

/// Build an F5 menu button 3-frame strip from `Data/skins/Modern_HUD_Widgets.json`:
/// f5, f5_hovered, f5_active, each scaled to cellW x cellH. Total surface = cellW x (cellH*3).
extern SDL_Surface *BuildHDHudF5Strip(int cellW, int cellH);

/// Build a single HUD header button frame from `Data/skins/Modern_HUD_Widgets.json`.
/// @param race "Human" or "Orc"
/// @param pressed false = normal (hud_{race}_small), true = active (hud_{race}_small_active)
/// Surface = cellW x cellH.
extern SDL_Surface *BuildHDHudSmallButton(const char *race, bool pressed, int cellW, int cellH);

//@}

#endif // !__HD_SYNTH_GRAPHICS_H__
