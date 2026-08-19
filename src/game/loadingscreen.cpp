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
/**@name loadingscreen.cpp - The map loading screen. */
//
//      (c) Copyright 2026 by the WargusHD project
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

//----------------------------------------------------------------------------
//  Includes
//----------------------------------------------------------------------------

#include "stratagus.h"

#include "loadingscreen.h"

#include "iolib.h"
#include "video.h"

#include <SDL.h>

#include <cmath>
#include <memory>

//----------------------------------------------------------------------------
//  Variables
//----------------------------------------------------------------------------

// The Remastered loading art. `loadscreen.png` is a full-screen still; `Animation.png` is a
// TexturePacker atlas (1166x728) holding a 4-frame looping spinner (each frame 581x362). The
// atlas is not exported as a .hdatlas, so it is loaded as a plain CGraphic and the four frames
// are blitted by their known source rects (Animation.json).
static const char *const LoadingBackgroundFile = "Data/Backgrounds/loadscreen.png";
static const char *const LoadingAnimationFile = "Data/Art/hd/loading/Animation.png";

// Frame count and per-frame source rects of the animation atlas (from Animation.json), in the
// atlas's NATIVE pixels. Scaled by AnimScale once the atlas is resized to its on-screen size.
static const int AnimFrameCount = 4;
static const int AnimNativeW = 581;
static const int AnimNativeH = 362;
static const SDL_Rect AnimFrameSrc[AnimFrameCount] = {
	{ 1, 1, AnimNativeW, AnimNativeH },     // "1"
	{ 1, 365, AnimNativeW, AnimNativeH },   // "2"
	{ 584, 1, AnimNativeW, AnimNativeH },   // "3"
	{ 584, 365, AnimNativeW, AnimNativeH }, // "4"
};

// One animation step every this many milliseconds (~6.7 fps), driven by wall-clock ticks so it
// advances even though the load work blocks the game loop.
static const uint32_t AnimFrameIntervalMs = 150;

static bool LoadingActive = false;

static std::shared_ptr<CGraphic> LoadingBackground; // letterboxed to the screen
static std::shared_ptr<CGraphic> LoadingAnimation;  // atlas, resized to its on-screen size

// Background placement (letterbox: fill height, centre horizontally).
static int BackgroundX = 0;

// Animation placement + the factor the atlas was resized by (native src rects * AnimScale).
static double AnimScale = 1.0;
static int AnimOriginX = 0; // top-left of the drawn frame
static int AnimOriginY = 0;

static uint32_t LoadingStartTicks = 0;

//----------------------------------------------------------------------------
//  Functions
//----------------------------------------------------------------------------

/**
**  Paint the background still + the current animation frame and present. Assumes
**  LoadingActive; draws through the CPU overlay (TheScreen) then RealizeVideoMemory
**  uploads and presents, exactly like the title-screen path.
*/
static void PaintLoadingScreen()
{
	if (LoadingBackground) {
		LoadingBackground->DrawClip(BackgroundX, 0);
	}

	if (LoadingAnimation) {
		const uint32_t elapsed = SDL_GetTicks() - LoadingStartTicks;
		const int idx = (int)((elapsed / AnimFrameIntervalMs) % AnimFrameCount);
		const SDL_Rect &s = AnimFrameSrc[idx];
		// Scale the native src rect to the resized atlas surface.
		const int sx = (int)std::lround(s.x * AnimScale);
		const int sy = (int)std::lround(s.y * AnimScale);
		const int sw = (int)std::lround(s.w * AnimScale);
		const int sh = (int)std::lround(s.h * AnimScale);
		LoadingAnimation->DrawSubClip(sx, sy, sw, sh, AnimOriginX, AnimOriginY);
	}

	Invalidate();
	RealizeVideoMemory();
}

void ShowLoadingScreen()
{
	if (LoadingActive) {
		return;
	}

	// No loading art available -> keep the historical blank load (cheap no-op, never a crash).
	if (!CanAccessFile(LibraryFileName(LoadingBackgroundFile).c_str())) {
		return;
	}

	LoadingBackground = CGraphic::New(LoadingBackgroundFile);
	LoadingBackground->Load();

	// Letterbox: fill the screen height, keep aspect, centre horizontally. Any horizontal
	// remainder is left as black bars (BeginFrame clears the target to black each frame).
	const int nativeW = LoadingBackground->Width;
	const int nativeH = LoadingBackground->Height;
	if (nativeW > 0 && nativeH > 0) {
		const double scale = (double)Video.Height / (double)nativeH;
		int newW = (int)std::lround(nativeW * scale);
		if (newW < 1) {
			newW = 1;
		}
		LoadingBackground->Resize(newW, Video.Height);
		BackgroundX = (Video.Width - newW) / 2;
	} else {
		BackgroundX = 0;
	}

	// The animation is optional: a missing atlas still yields a static loading screen.
	if (CanAccessFile(LibraryFileName(LoadingAnimationFile).c_str())) {
		LoadingAnimation = CGraphic::New(LoadingAnimationFile);
		LoadingAnimation->Load();

		// Size one frame to ~22% of the screen height, keeping the atlas aspect, then place it
		// in the BOTTOM-RIGHT corner: the Remastered client anchors the working peasant/peon
		// there (owner-observed reference), not centre-bottom. The corner inset reuses the
		// bottom margin on both axes; no original layout rect is available for the exact inset.
		const int targetFrameH = (int)std::lround(Video.Height * 0.22);
		AnimScale = (AnimNativeH > 0) ? ((double)targetFrameH / (double)AnimNativeH) : 1.0;
		const int atlasW = LoadingAnimation->Width;
		const int atlasH = LoadingAnimation->Height;
		if (atlasW > 0 && atlasH > 0) {
			LoadingAnimation->Resize((int)std::lround(atlasW * AnimScale),
			                         (int)std::lround(atlasH * AnimScale));
		}
		const int frameW = (int)std::lround(AnimNativeW * AnimScale);
		const int bottomMargin = (int)std::lround(Video.Height * 0.06);
		AnimOriginX = Video.Width - frameW - bottomMargin;
		if (AnimOriginX < 0) {
			AnimOriginX = 0;
		}
		AnimOriginY = Video.Height - targetFrameH - bottomMargin;
		if (AnimOriginY < 0) {
			AnimOriginY = 0;
		}
	}

	LoadingStartTicks = SDL_GetTicks();
	LoadingActive = true;

	PaintLoadingScreen();
}

void UpdateLoadingScreen()
{
	if (!LoadingActive) {
		return;
	}
	PaintLoadingScreen();
}

bool IsLoadingScreenActive()
{
	return LoadingActive;
}

void HideLoadingScreen()
{
	if (!LoadingActive && !LoadingBackground && !LoadingAnimation) {
		return;
	}
	LoadingActive = false;
	LoadingBackground.reset();
	LoadingAnimation.reset();
}
