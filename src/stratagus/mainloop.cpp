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
/**@name mainloop.cpp - The main game loop. */
//
//      (c) Copyright 1998-2006 by Lutz Sammer and Jimmy Salmon
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

//----------------------------------------------------------------------------
//  Includes
//----------------------------------------------------------------------------

#include "online_service.h"
#include "stratagus.h"

#include "actions.h"
#include "editor.h"
#include "fow.h"
#include "game.h"
#include "map.h"
#include "missile.h"
#include "network.h"
#include "particle.h"
#include "replay.h"
#include "results.h"
#include "sound.h"
#include "translate.h"
#include "trigger.h"
#include "ui.h"
#include "unit.h"
#include "video.h"
#include "parameters.h"
#include "frame_profiler.h"

#include <algorithm>

#ifdef HAVE_COZ_PROFILER
# include <coz.h>
#endif

void DrawGuichanWidgets();


enum CallPeriod { cEvery2nd   = 0b1,
				  cEvery4th   = 0b11,
				  cEvery8th   = 0b111,
				  cEvery16th  = 0b1111,
				  cEvery32nd  = 0b11111,
				  cEvery64th  = 0b111111,
				  cEvery128th = 0b1111111,
				  cEvery256th = 0b11111111 };

//----------------------------------------------------------------------------
// Variables
//----------------------------------------------------------------------------

/// variable set when we are scrolling via keyboard
int KeyScrollState = ScrollNone;

/// variable set when we are scrolling via mouse
int MouseScrollState = ScrollNone;

EventCallback GameCallbacks;   /// Game callbacks
EventCallback EditorCallbacks; /// Editor callbacks

//----------------------------------------------------------------------------
// Functions
//----------------------------------------------------------------------------

/**
**  Handle scrolling area.
**
**  @param state  Scroll direction/state.
**  @param fast   Flag scroll faster.
**
**  @todo  Support dynamic acceleration of scroll speed.
**  @todo  If the scroll key is longer pressed the area is scrolled faster.
*/
void DoScrollArea(int state, bool fast, bool isKeyboard)
{
	CViewport *vp;
	int stepx;
	int stepy;
	static int remx = 0; // FIXME: docu
	static int remy = 0; // FIXME: docu

	int speed = isKeyboard ? UI.KeyScrollSpeed : UI.MouseScrollSpeed;

	if (state == ScrollNone) {
		return;
	}

	vp = UI.SelectedViewport;

	if (fast) {
		stepx = (int)(speed * vp->MapWidth / 2 * PixelTileSize.x * CYCLES_PER_SECOND / 4);
		stepy = (int)(speed * vp->MapHeight / 2 * PixelTileSize.y * CYCLES_PER_SECOND / 4);
	} else {// dynamic: let these variables increase up to fast..
		// FIXME: pixels per second should be configurable
		stepx = (int)(speed * PixelTileSize.x * CYCLES_PER_SECOND / 4);
		stepy = (int)(speed * PixelTileSize.y * CYCLES_PER_SECOND / 4);
	}
	if ((state & (ScrollLeft | ScrollRight)) && (state & (ScrollLeft | ScrollRight)) != (ScrollLeft | ScrollRight)) {
		stepx = stepx * 3;
		remx += stepx - (stepx / 100) * 100;
		stepx /= 100;
		if (remx > 100) {
			++stepx;
			remx -= 100;
		}
	} else {
		stepx = 0;
	}
	if ((state & (ScrollUp | ScrollDown)) && (state & (ScrollUp | ScrollDown)) != (ScrollUp | ScrollDown)) {
		stepy = stepy * 3;
		remy += stepy - (stepy / 100) * 100;
		stepy /= 100;
		if (remy > 100) {
			++stepy;
			remy -= 100;
		}
	} else {
		stepy = 0;
	}

	if (state & ScrollUp) {
		stepy = -stepy;
	}
	if (state & ScrollLeft) {
		stepx = -stepx;
	}
	const PixelDiff offset(stepx, stepy);

	vp->Set(vp->MapPos, vp->Offset + offset);

	// This recalulates some values
	HandleMouseMove(CursorScreenPos);
}

/**
**  Draw map area
*/
void DrawMapArea(const fieldHighlightChecker highlightChecker = nullptr)
{
	// Draw all of the viewports
	for (CViewport *vp = UI.Viewports; vp < UI.Viewports + UI.NumViewports; ++vp) {
		// Center viewport on tracked unit
		if (vp->Unit) {
			if (vp->Unit->Destroyed || vp->Unit->CurrentAction() == UnitAction::Die) {
				vp->Unit = nullptr;
			} else {
				vp->Center(vp->Unit->GetMapPixelPosCenter());
			}
		}
		vp->Draw(highlightChecker);
	}
}

/**
**  Display update.
**
**  This functions updates everything on screen. The map, the gui, the
**  cursors.
*/
void UpdateDisplay()
{
	// GPU-pipeline hybrid (Phase 0): open the frame BEFORE any world drawing. This targets and
	// clears the GPU backbuffer to opaque black; the GPU world layers (tiles/units) are then
	// drawn on top of that clear during DrawMapArea, and the CPU overlay (TheScreen) is copied
	// over them in RealizeVideoMemory. Idempotent per frame.
	FrameProfBegin(FP_BEGINFRAME);
	BeginFrame();
	FrameProfEnd(FP_BEGINFRAME);

	if (GameRunning || Editor.Running == EditorEditing) {
		// Clear the CPU overlay to fully TRANSPARENT (was opaque black). The GPU world shows
		// through wherever the overlay is untouched; only opaque CPU layers (HUD/fog/cursor)
		// occlude it. 0 == transparent in ARGB8888. (Prevents empty spaces in the UI.)
		FrameProfBegin(FP_OVLCLEAR);
		{
			// "Keep transparent" region = the actual viewport RENDER rect (where the GPU world is
			// drawn), NOT UI.MapArea: the resource bar and other HUD elements sit inside MapArea but
			// outside the viewport, so basing the border on MapArea leaves them uncleared (ghosting).
			// Union the viewport rects (usually one) and clamp to the overlay.
			const int w = TheScreen->w;
			const int h = TheScreen->h;
			int mapX = w, mapY = h, mapEndX = 0, mapEndY = 0;
			for (std::size_t i = 0; i != UI.NumViewports; ++i) {
				const PixelPos &tl = UI.Viewports[i].TopLeftPos;
				const PixelPos &br = UI.Viewports[i].BottomRightPos;
				mapX = std::min(mapX, tl.x);
				mapY = std::min(mapY, tl.y);
				mapEndX = std::max(mapEndX, br.x + 1);
				mapEndY = std::max(mapEndY, br.y + 1);
			}
			mapX = std::max(0, std::min(mapX, w));
			mapY = std::max(0, std::min(mapY, h));
			mapEndX = std::max(0, std::min(mapEndX, w));
			mapEndY = std::max(0, std::min(mapEndY, h));

			const bool validMapArea = UI.NumViewports > 0 && mapEndX > mapX && mapEndY > mapY;
			// Only the in-game HUD path benefits. Editor/big-map draw freely -> full clear.
			const bool regionClear =
				GameRunning && Editor.Running != EditorEditing && !BigMapMode && validMapArea;

			static bool mapDirtyLastFrame = true; // force first in-game frame to clear interior

			if (regionClear) {
				// Border frame: up to 4 rects outside the map interior. Covers all persistent
				// HUD dynamic content (minimap, portrait/InfoPanel, resources, status line,
				// button panel, menu buttons).
				if (mapY > 0) {
					SDL_Rect r{0, 0, w, mapY};
					SDL_FillRect(TheScreen, &r, 0);
				}
				if (mapEndY < h) {
					SDL_Rect r{0, mapEndY, w, h - mapEndY};
					SDL_FillRect(TheScreen, &r, 0);
				}
				if (mapX > 0) {
					SDL_Rect r{0, mapY, mapX, mapEndY - mapY};
					SDL_FillRect(TheScreen, &r, 0);
				}
				if (mapEndX < w) {
					SDL_Rect r{mapEndX, mapY, w - mapEndX, mapEndY - mapY};
					SDL_FillRect(TheScreen, &r, 0);
				}

				// The resource bar (gold/lumber/oil/food/score) overlays the TOP of the map
				// viewport and its digits change, so its band must be cleared every frame even
				// though it sits inside the interior. A fixed generous band covers it cheaply.
				{
					const int band = std::min(170, mapEndY - mapY);
					if (band > 0) {
						SDL_Rect r{mapX, mapY, mapEndX - mapX, band};
						SDL_FillRect(TheScreen, &r, 0);
					}
				}

				// Map interior holds transient overlay content only when messages are shown or
				// a drag-select box is active. Clear it this frame if either is true now or was
				// last frame (so the frame after the content vanishes erases it once).
				const bool messagesVisible = AreMessagesShown();
				const bool cursorRectangle = CursorState == CursorStates::Rectangle;
				if (messagesVisible || cursorRectangle || mapDirtyLastFrame) {
					SDL_Rect r{mapX, mapY, mapEndX - mapX, mapEndY - mapY};
					SDL_FillRect(TheScreen, &r, 0);
				}
				mapDirtyLastFrame = messagesVisible || cursorRectangle;
			} else {
				// Menus/title, editor, big-map, or invalid MapArea: original full clear.
				SDL_FillRect(TheScreen, nullptr, 0);
				mapDirtyLastFrame = true;
			}
		}
		FrameProfEnd(FP_OVLCLEAR);
		FrameProfBegin(FP_WORLD);
		DrawMapArea();
		FrameProfEnd(FP_WORLD);
		FrameProfBegin(FP_HUD);
		// Draw the in-game HUD text (messages, resources, info panel, status line, button
		// labels) straight to the GPU backbuffer instead of into the CPU overlay. The fillers
		// are already GPU, so the text lands over them. Reset before DrawGuichanWidgets so
		// menu/tooltip text keeps the CPU path.
		GpuFontToBackbuffer = true;
		// HUD sprites (icons, minimap, portrait) also route to the GPU backbuffer while true.
		GpuHudDrawActive = true;
		// TODO: for e.g. environmental effects, we want to push to the renderer here with appropriate shaders set,
		// then do the rest.
		DrawMessages();

		if (CursorState == CursorStates::Rectangle) {
			FrameProfBegin(FP_HUD_CURSOR);
			DrawCursor();
			FrameProfEnd(FP_HUD_CURSOR);
		}

		if ((Preference.BigScreen && !BigMapMode) || (!Preference.BigScreen && BigMapMode)) {
			UiToggleBigMap();
		}

		if (!BigMapMode) {
			FrameProfBegin(FP_HUD_FILLERS);
			for (auto &filler : UI.Fillers) {
				// GPU: static opaque HUD fillers RenderCopy straight to the backbuffer (under the
				// CPU overlay, which is transparent here so the dynamic HUD composites on top). RGBA
				// fillers carry mTexture; paletted ones (mTexture == null) keep the CPU DrawSubClip.
				if (TheRenderer && filler.G->mTexture) {
					filler.G->DrawClipTex(filler.X, filler.Y);
				} else {
					filler.G->DrawSubClip(0, 0, filler.G->Width, filler.G->Height, filler.X, filler.Y);
				}
			}
			FrameProfEnd(FP_HUD_FILLERS);
			FrameProfBegin(FP_HUD_PANELS);
			DrawMenuButtonArea();
			DrawUserDefinedButtons();
			FrameProfEnd(FP_HUD_PANELS);

			FrameProfBegin(FP_HUD_MINIMAP);
			UI.Minimap.Draw();
			for (std::size_t i = 0; i != UI.NumViewports; ++i) {
				UI.Minimap.DrawViewportArea(UI.Viewports[i],
				                            UI.SelectedViewport == &UI.Viewports[i] ? 255 : 128);
			}
			FrameProfEnd(FP_HUD_MINIMAP);

			FrameProfBegin(FP_HUD_PANELS);
			UI.InfoPanel.Draw();
			DrawResources();
			UI.StatusLine.Draw();
			UI.StatusLine.DrawCosts();
			UI.ButtonPanel.Draw();
			FrameProfEnd(FP_HUD_PANELS);
		}

		DrawTimer();
	}

	// End of the in-game HUD text: guichan widgets/menus/tooltips below draw into the CPU
	// overlay, so keep them on the CPU font path.
	GpuFontToBackbuffer = false;
	GpuHudDrawActive = false;

	FrameProfBegin(FP_HUD_GUICHAN);
	DrawPieMenu(); // draw pie menu only if needed

	DrawGuichanWidgets();
	FrameProfEnd(FP_HUD_GUICHAN);

	if (CursorState != CursorStates::Rectangle) {
		FrameProfBegin(FP_HUD_CURSOR);
		DrawCursor();
		FrameProfEnd(FP_HUD_CURSOR);
	}

	FrameProfEnd(FP_HUD);

	//
	// Update changes to display.
	//
	FrameProfBegin(FP_INVALIDATE);
	Invalidate();
	FrameProfEnd(FP_INVALIDATE);
}

static void InitGameCallbacks()
{
	GameCallbacks.ButtonPressed = HandleButtonDown;
	GameCallbacks.ButtonReleased = HandleButtonUp;
	GameCallbacks.MouseMoved = HandleMouseMove;
	GameCallbacks.MouseExit = HandleMouseExit;
	GameCallbacks.KeyPressed = HandleKeyDown;
	GameCallbacks.KeyReleased = HandleKeyUp;
	GameCallbacks.KeyRepeated = HandleKeyRepeat;
	GameCallbacks.NetworkEvent = NetworkEvent;
}

// Real-time accumulator (ms) toward the next logic cycle. Drives both the fixed-timestep
// logic pacing (SingleGameLoop) and the render-side movement interpolation fraction, so the
// simulation runs a stable CYCLES_PER_SECOND regardless of render frame rate.
static double LogicAccumulatorMs = 0.0;

/// Fraction (0..1) of a game-cycle's real-time duration elapsed toward the next cycle.
/// Used only by the draw path to interpolate unit positions between cycles.
double GetGameCycleFraction()
{
	if (GamePaused) {
		return 0.0;
	}
	const double period = 1000.0 / (double)CYCLES_PER_SECOND;
	const double f = LogicAccumulatorMs / period;
	return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

// Advance the simulation by exactly one cycle. The caller (SingleGameLoop) decides WHEN and
// HOW MANY TIMES to run it via the fixed-timestep accumulator, so this no longer gates on
// GamePaused / NetworkInSync / SkipGameCycle itself.
static void RunOneGameCycle()
{
	FrameProfScope _prof(FP_LOGIC);
	// Can't find a better place.
	// FIXME: We need find better place!
	SaveGameLoading = false;

	SinglePlayerReplayEachCycle();
	++GameCycle;
	MultiPlayerReplayEachCycle();
	NetworkCommands(); // Get network commands
	TriggersEachCycle();// handle triggers
	UnitActions();      // handle units
	MissileActions();   // handle missiles
	PlayersEachCycle(); // handle players
	UpdateTimer();      // update game timer


	//
	// Work todo each second.
	// Split into different frames, to reduce cpu time.
	// Increment mana of magic units.
	// Update mini-map.
	// Update map fog of war.
	// Call AI.
	// Check game goals.
	// Check rescue of units.
	//
	switch (GameCycle % CYCLES_PER_SECOND) {
		case 0: // At cycle 0, start all ai players...
			if (GameCycle == 0) {
				for (int player = 0; player < NumPlayers; ++player) {
					PlayersEachSecond(player);
				}
			}
			break;
		case 1:
			break;
		case 2:
			break;
		case 3: // minimap update
			UI.Minimap.UpdateCache = true;
			break;
		case 4:
			break;
		case 5: // forest grow
			Map.RegenerateForest();
			break;
		case 6: // overtaking units
			RescueUnits();
			break;
		default: {
			// FIXME: assume that NumPlayers < (CYCLES_PER_SECOND - 7)
			int player = (GameCycle % CYCLES_PER_SECOND) - 7;
			Assert(player >= 0);
			if (player < NumPlayers) {
				PlayersEachSecond(player);
			}
		}
	}

	if (Preference.AutosaveMinutes != 0 && !IsNetworkGame() && !IsReplayGame() && GameCycle > 0 && (GameCycle % (CYCLES_PER_SECOND * 60 * Preference.AutosaveMinutes)) == 0) { // autosave every X minutes (default is 5), if the option is enabled
	//Wyrmgus end
		UI.StatusLine.Set(_("Autosave"));
		SaveGame("autosave.sav");
	}
}

// Per-rendered-frame housekeeping plus input/frame pacing. Runs once per rendered frame,
// independent of how many logic cycles advanced this frame.
static void PerFrameHousekeeping()
{
	UpdateMessages();     // update messages
	ParticleManager.update(); // handle particles

	if (FastForwardCycle <= GameCycle || !(GameCycle & CallPeriod::cEvery256th)) {
		WaitEventsOneFrame();
	}

	if (!NetworkInSync) {
		NetworkRecover(); // recover network
	}

#ifdef HAVE_COZ_PROFILER
	COZ_PROGRESS_NAMED("GameLogicLoop")
#endif
}

static void DisplayLoop()
{
	/* update only if viewmode changed */
	CheckViewportMode();

	/*
	 *	update only if Update flag is set
	 *	FIXME: still not secure
	 */
	if (UI.Minimap.UpdateCache) {
		FrameProfBegin(FP_MINIMAP);
		UI.Minimap.Update();
		FrameProfEnd(FP_MINIMAP);
		UI.Minimap.UpdateCache = false;
	}

	//
	// Map scrolling
	//
	DoScrollArea(MouseScrollState | KeyScrollState, (KeyModifiers & ModifierControl) != 0, MouseScrollState == 0 && KeyScrollState > 0);

	ColorCycle();

	if (FastForwardCycle <= GameCycle || GameCycle <= 10 || !(GameCycle & CallPeriod::cEvery256th)) {
		//FIXME: this might be better placed somewhere at front of the
		// program, as we now still have a game on the background and
		// need to go through the game-menu or supply a map file

		FrameProfBegin(FP_FOG);
		FogOfWar->Update(FastForwardCycle > GameCycle);
		FrameProfEnd(FP_FOG);

		FrameProfBegin(FP_UPDATEDISPLAY);
		UpdateDisplay();
		FrameProfEnd(FP_UPDATEDISPLAY);

		FrameProfBegin(FP_REALIZE);
		RealizeVideoMemory();
		FrameProfEnd(FP_REALIZE);
	}

	// One rendered frame elapsed (whether or not it drew this iteration).
	FrameProfEndFrame();
}

static void SingleGameLoop()
{
	const double period = 1000.0 / (double)CYCLES_PER_SECOND;
	long lastTicks = (long)SDL_GetTicks();
	while (GameRunning) {
		// Replay fast-forward / seek: advance cycles at max speed with no time throttle.
		// DisplayLoop and WaitEventsOneFrame are internally gated to every 256th cycle here.
		if (FastForwardCycle > GameCycle && !GamePaused && NetworkInSync) {
			RunOneGameCycle();
			DisplayLoop();
			PerFrameHousekeeping();
			lastTicks = (long)SDL_GetTicks();
			LogicAccumulatorMs = 0.0;
			continue;
		}

		const long now = (long)SDL_GetTicks();
		double delta = (double)(now - lastTicks);
		lastTicks = now;
		if (delta < 0.0) delta = 0.0;
		if (delta > 250.0) delta = 250.0;   // clamp after a stall to avoid a spiral of death

		if (!GamePaused && NetworkInSync) {
			LogicAccumulatorMs += delta;
			int guard = 0;
			// Run as many fixed-size logic cycles as real time has accrued (catch-up),
			// bounded so a long stall can't lock the loop. The remainder in the accumulator
			// is the interpolation fraction the draw path reads via GetGameCycleFraction().
			while (LogicAccumulatorMs >= period && guard < 10) {
				RunOneGameCycle();
				LogicAccumulatorMs -= period;
				++guard;
				if (FastForwardCycle > GameCycle) { break; }
			}
		} else {
			LogicAccumulatorMs = 0.0;   // paused / out of sync: no interpolation drift
		}

		DisplayLoop();            // renders with GetGameCycleFraction() = accumulator/period
		PerFrameHousekeeping();   // input + frame pacing (WaitEventsOneFrame)
	}
}

/**
**  Game main loop.
**
**  Unit actions.
**  Missile actions.
**  Players (AI).
**  Cyclic events (color cycle,...)
**  Display update.
**  Input/Network/Sound.
*/
void GameMainLoop(bool clean)
{
	const EventCallback *old_callbacks;

	InitGameCallbacks();

	old_callbacks = GetCallbacks();
	SetCallbacks(&GameCallbacks);

	SetVideoSync();
	GameCursor = UI.Point.Cursor;
	if (clean) {
		GameCycle = 0;
	}
	GameRunning = true;

	CParticleManager::init();

	CclCommand("if (GameStarting ~= nil) then GameStarting() end");

	long ticks = SDL_GetTicks();

	MultiPlayerReplayEachCycle();

	SingleGameLoop();

	//
	// Game over
	//
	if (ThisPlayer && IsNetworkGame()) {
		OnlineContextHandler->reportGameResult();
	}

	if (GameResult == GameExit) {
		Exit(0);
		return;
	}

	NetworkQuitGame();
	EndReplayLog();

	if (Parameters::Instance.benchmark) {
		ticks = SDL_GetTicks() - ticks;
		double fps = FrameCounter * 1000.0 / ticks;
		ErrorPrint("BENCHMARK RESULT: %f fps, %f cps (%ldms for %ldframes in %ldcycles)\n",
		           fps,
		           GameCycle * 1000.0 / ticks,
		           ticks,
		           FrameCounter,
		           GameCycle);
	}

	GameCycle = 0;
	CParticleManager::exit();
	FlagRevealMap = MapRevealModes::cHidden;
	ReplayRevealMap = 0;
	GamePaused = false;
	GodMode = false;

	SetCallbacks(old_callbacks);
}

//@}
