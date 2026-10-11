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

/// WargusHD command-icon cell geometry (IC1, M666-2).
/// The picture strip keeps the 150x124 cell scripts/icons.lua names; the picture fills the cell.
/// The frame is the game's own widget, drawn by its own skin record (Data/skins/skins.json, id
/// `modern_portrait_button`): sprite `portrait_button` 80x80, "9slice" [25,25,25,25], "scale" 5
/// (sprite px per base unit of the 640x480 layout), "padding" [4,4] (base units from the widget's
/// edge to the picture). The sprite's first 5 px are empty (alpha 0 at x 0..4 of its row 40, then
/// the ring at 5..20), so the visible cell is the widget less 1 unit a side.
/// Measured at k = 3 (work/agent-orig-scenes/captures e01, hi10; work/agent-fix-hudrest/tools/
/// m2_cells.py): visible cell 156 x 132 px = 52 x 44 units, ring 9 px, picture 138 x 114 px =
/// 46 x 38 units at (9, 9).
namespace hdicon
{
constexpr int CELL_W = 150;       // picture strip cell, scripts/icons.lua ("ui/icons_hd.png", 150, 124)
constexpr int CELL_H = 124;
constexpr int SLOTS = 196;        // classic command-icon slot count (0..195)
constexpr int FRAME_STATES = 6;   // default, hovered, active, highlight, highlight_hovered, highlight_active
constexpr int FRAME_PX = 80;      // Modern_HUD_Widgets.json: every portrait_button* frame is 80 x 80
constexpr int FRAME_SLICE = 25;   // skins.json modern_portrait_button "9slice"
constexpr int FRAME_SCALE = 5;    // skins.json modern_portrait_button "scale"
constexpr int FRAME_MARGIN_U = 1; // empty rim of the sprite, 5 px = 1 base unit
constexpr int PICTURE_PAD_U = 4;  // skins.json modern_portrait_button "padding"
constexpr int CELL_UNITS_W = 52;  // visible cell in base units (156 px at k = 3, capture e01)
} // namespace hdicon

/// Build the BORDERLESS HD command-icon strip natively from the Remastered
/// `Art/hd/HUD/Portrait-face` atlas: 150 x (124*196) RGBA32, slot N == frame `forest_N` filling
/// its cell. Where `Art/hd/HUD/Portrait-mask` has `forest_N_team`, the team region is written as
/// the key (v, 0, v), v = the mask's grey, for CPlayerColorGraphic::GetHudFrameColorTexture.
/// Returns a fresh SDL_Surface (caller owns) or nullptr if the native atlas is unavailable (caller
/// then falls back to the on-disk file).
extern SDL_Surface *BuildHDIconStrip(const char *set = "forest");

/// True when the last BuildHDIconStrip() wrote the team key from the mask atlas.
extern bool HDIconStripHasTeamKey();

/// Build the command-cell FRAME strip natively from the Remastered `skins/Modern_HUD_Widgets`
/// atlas: 80 x (80*6) RGBA32, the six portrait_button sprites as they are, in the order
/// default,hovered,active,highlight,highlight_hovered,highlight_active. Returns a fresh
/// SDL_Surface (caller owns) or nullptr if the native atlas is unavailable.
extern SDL_Surface *BuildHDCommandFrameStrip();

/// Crop a single frame from the Remastered sidebar background atlas `Data/Art/hd/HUD/BG_{race}.json`.
/// @param race "Human" or "Orc"
/// @param frameName "TopLeft" (855x2160) or "TileableLeft" (855x630)
/// Returns a fresh RGBA32 SDL_Surface (caller owns) or nullptr if the atlas is unavailable.
extern SDL_Surface *BuildHDSidebarPanel(const char *race, const char *frameName);

/// Build an F5 menu button 3-frame strip from `Data/skins/Modern_HUD_Widgets.json`:
/// f5, f5_hovered, f5_active, each scaled to cellW x cellH. Total surface = cellW x (cellH*3).
extern SDL_Surface *BuildHDHudF5Strip(const char *race, int cellW, int cellH);

/// Build a single HUD header button frame from `Data/skins/Modern_HUD_Widgets.json`.
/// @param race "Human" or "Orc"
/// @param pressed false = normal (hud_{race}_small), true = active (hud_{race}_small_active)
/// Surface = cellW x cellH.
extern SDL_Surface *BuildHDHudSmallButton(const char *race, bool pressed, int cellW, int cellH);

/// Build the completion bar's frame (sprite `complete_bar`, 790x100, of
/// `Data/skins/Modern_HUD_Widgets.json`) scaled to w x h. The original draws it at one fifth of its
/// size per base unit, i.e. unstretched, so a plain scale keeps its proportions.
extern SDL_Surface *BuildHDHudCompleteBar(int w, int h);

//@}

#endif // !__HD_SYNTH_GRAPHICS_H__
