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
/**@name statusline.cpp - The statusline. */
//
//      (c) Copyright 2012 by Joris Dauphin
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

/*----------------------------------------------------------------------------
--  Includes
----------------------------------------------------------------------------*/

#include "stratagus.h"

#include "ui/statusline.h"

#include "font.h"
#include "interface.h"
#include "ui.h"
#include "video.h"

/**
**  Draw status line.
*/
void CStatusLine::Draw()
{
	if (!this->StatusLine.empty()) {
		PushClipping();
		SetClipping(this->TextX, this->TextY,
					this->TextX + this->Width - 1, Video.Height - 1);
		CLabel(*this->Font).DrawClip(this->TextX, this->TextY, this->StatusLine);
		PopClipping();
	}
}

/**
**  Change status line to new text.
**
**  @param status  New status line information.
*/
void CStatusLine::Set(const std::string &status)
{
	if (KeyState != EKeyState::Input) {
		// NG37: the text and the cost icons are ONE message, and the costs used to outlive the text
		// that owned them -- DrawCosts() paints at a fixed TextX + 268 whatever the current text is,
		// so a transient notice ("Autosave") appeared beside, or on top of, the resource icons left
		// over from the last button hint. Whoever sets the text owns the costs: clear them here and
		// let the one caller that has costs (UpdateStatusLineForButton) re-set them immediately
		// afterwards, which is exactly what it already does.
		this->ClearCosts();
		this->StatusLine = status;
	}
}

/**
**  Set costs in status line.
**
**  @param mana   Mana costs.
**  @param food   Food costs.
**  @param costs  Resource costs, nullptr pointer if all are zero.
*/
void CStatusLine::SetCosts(int mana, int food, const int *costs)
{
	if (costs) {
		memcpy(Costs, costs, MaxCosts * sizeof(*costs));
	} else {
		ranges::fill(Costs, 0);
	}
	Costs[ManaResCost] = mana;
	Costs[FoodCost] = food;
}

/**
**  Clear costs in status line.
*/
void CStatusLine::ClearCosts()
{
	SetCosts(0, 0, nullptr);
}

/**
**  Draw costs in status line.
**
**  @todo FIXME : make DrawCosts more configurable.
**  @todo FIXME : 'time' resource should be shown too.
**
**  @internal MaxCost == FoodCost.
*/
void CStatusLine::DrawCosts()
{
	// NG81: 268 and the 20 px icon advance below were both sized for the classic small-font
	// layout. Used unscaled in the HD HUD they overprint: remaster_small measures "BUILD FARM"
	// at 226 so it clears, but "BUILD BARRACKS" at 315 and "BUILD BASIC STRUCTURE" at 454 run
	// under their own icons, and a resource icon measures 87 px against the 20 the amount was
	// offset by, so each amount was drawn inside its own icon.
	//
	// Keep 268 as a MINIMUM so every hint that already fits is placed exactly as before, and
	// advance past an icon by its real width the way the maintained copy of this same routine
	// does (CPopupContentTypeCosts::Draw, ui/popup.cpp) -- IconWidth when the UI declares one,
	// the graphic's own width otherwise, plus this function's existing 5 px gap.
	const int textWidth = this->Font ? this->Font->Width(this->StatusLine) : 0;
	int x = UI.StatusLine.TextX + std::max(268, textWidth + 5);
	// COSTFONT: the amounts belong to the hint beside them, so they take its face. They used to
	// draw in the classic bitmap game font, which renders them visibly smaller than the text they
	// annotate. The original cannot be doing that: the Oracle's `fonts` table (E1) has exactly ONE
	// enUS typeface, Data/font/BLQ55.ttf, at three sizes -- there is no second face for it to use.
	// Our own resource bar already draws the equivalent numbers in remaster_small
	// (human/ui_pandora.lua). Fall back to the game font if the UI never set one.
	CLabel label(this->Font ? *this->Font : GetGameFont());
	if (this->Costs[ManaResCost]) {
		const CGraphic *manaG = UI.Resources[ManaResCost].G.get();
		manaG->DrawFrameClip(UI.Resources[ManaResCost].IconFrame, x, UI.StatusLine.TextY);

		const int manaOffset = UI.Resources[ManaResCost].IconWidth;
		x += (manaOffset != -1 ? manaOffset : manaG->Width) + 5;
		x += label.Draw(x, UI.StatusLine.TextY, this->Costs[ManaResCost]);
	}

	for (unsigned int i = 1; i <= MaxCosts; ++i) {
		if (this->Costs[i]) {
			x += 5;
			if (const CGraphic *G = UI.Resources[i].G.get()) {
				G->DrawFrameClip(UI.Resources[i].IconFrame,
					x, UI.StatusLine.TextY);
				const int xOffset = UI.Resources[i].IconWidth;
				x += (xOffset != -1 ? xOffset : G->Width) + 5;
			}
			x += label.Draw(x, UI.StatusLine.TextY, this->Costs[i]);
			if (x > Video.Width - 60) {
				break;
			}
		}
	}
}

/**
**  Clear status line.
*/
void CStatusLine::Clear()
{
	if (KeyState != EKeyState::Input) {
		// NG37: same ownership rule as Set() -- clearing the message clears its costs.
		this->ClearCosts();
		this->StatusLine.clear();
	}
}

//@}
