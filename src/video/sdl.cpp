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
/**@name sdl.cpp - SDL video support. */
//
//      (c) Copyright 1999-2011 by Lutz Sammer, Jimmy Salmon, Nehal Mistry and
//                                 Pali Rohár
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
-- Includes
----------------------------------------------------------------------------*/

#include "stratagus.h"

#include "editor.h"
#include "game.h"
#include "debug_bridge.h"
#include "shaders.h"
#include "frame_profiler.h"
#include "network.h"
#include "online_service.h"
#include "parameters.h"
#include "sound_server.h"
#include "translate.h"
#include "ui.h"
#include "unit.h"
#include "loadingscreen.h"
#include "video.h"
#include "widgets.h"

#include <climits>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <SDL.h>
#include <SDL_syswm.h>

#ifdef USE_BEOS
#include <sys/socket.h>
#endif

#ifdef USE_WIN32
# include <shellapi.h>
#else
# ifdef DEBUG
#  include <signal.h>
# endif
# include <sys/stat.h>
# include <sys/types.h>
#endif

/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
--  Variables
----------------------------------------------------------------------------*/

SDL_Window *TheWindow; /// Internal screen
SDL_Renderer *TheRenderer = nullptr; /// Internal screen
SDL_Texture *TheTexture; /// Internal screen
SDL_Surface *TheScreen; /// Internal screen

static SDL_Rect Rects[100];
static int NumRects;

static std::map<int, std::string> Key2Str;
static std::map<std::string, int> Str2Key;

/// Frame length in ms
static double FrameTicks;

/// Target refresh rate for renderer
int RefreshRate = 0;

const EventCallback *Callbacks;

bool IsSDLWindowVisible = true;

/// Just a counter to cache window data on in other places when the size changes
uint8_t SizeChangeCounter = 0;

static bool dummyRenderer = false;
/// Time of the last SDL event handed to SdlDoEvent (see GreyFrameUnderMenu, live frames).
static Uint32 LastSdlEventTicks = 0;

uint32_t SDL_CUSTOM_KEY_UP;

/**
**  Clean up SDL video resources properly
*/
static void CleanUpVideoSdl()
{
	if (TheRenderer) {
		SDL_DestroyRenderer(TheRenderer);
		TheRenderer = nullptr;
	}

	if (TheWindow) {
		SDL_DestroyWindow(TheWindow);
		TheWindow = nullptr;
	}

	if (Video.blankCursor) {
		Video.blankCursor.reset();
	}

	SDL_StopTextInput();
	SDL_Quit();
}

/*----------------------------------------------------------------------------
--  Sync
----------------------------------------------------------------------------*/

static int GetRefreshRate()
{
	if (!RefreshRate) {
		int displayCount = SDL_GetNumVideoDisplays();
		SDL_DisplayMode mode;
		for (int i = 0; i < displayCount; i++) {
			SDL_GetDesktopDisplayMode(0, &mode);
			if (mode.refresh_rate > RefreshRate) {
				RefreshRate = mode.refresh_rate;
			}
		}
		if (!RefreshRate) {
			RefreshRate = 60;
		}
	}
	return RefreshRate;
}

/**
**  Initialise video sync.
**  Calculate the length of video frame and any simulation skips.
**
**  @see CyclesPerSecond @see SkipCycles @see SkipFrames @see FrameTicks
*/
void SetVideoSync()
{
	int fps = GetRefreshRate();
	int nativeFps = fps;
	if (fps < CyclesPerSecond) {
		fprintf(stdout, "WARNING: Game speed is faster than monitor refresh rate.\n");
		FrameTicks = 1000.0 / CyclesPerSecond;
		SDL_GL_SetSwapInterval(0); // disable vsync, so we can run faster than the refresh
	} else {
		FrameTicks = 1000.0 / fps;
		if (SDL_GL_SetSwapInterval(-1) < 0) { // try to set adaptive vsync
			SDL_GL_SetSwapInterval(1); // if it failed, set vsync
		}
	}
	SkipCycles = (static_cast<double>(fps) / CyclesPerSecond) - 1;

	DebugPrint("native fps: %d, render frame skip: %d, game cycle skip: %f\n",
	           nativeFps,
	           Preference.FrameSkip,
	           SkipCycles);
}

/*----------------------------------------------------------------------------
--  Video
----------------------------------------------------------------------------*/

/**
**  Initialize SDLKey to string map
*/
static void InitKey2Str()
{
	Str2Key[_("esc")] = SDLK_ESCAPE;

	if (!Key2Str.empty()) {
		return;
	}

	Key2Str[SDLK_BACKSPACE] = "backspace";
	Key2Str[SDLK_TAB] = "tab";
	Key2Str[SDLK_CLEAR] = "clear";
	Key2Str[SDLK_RETURN] = "return";
	Key2Str[SDLK_PAUSE] = "pause";
	Key2Str[SDLK_ESCAPE] = "escape";
	Key2Str[SDLK_SPACE] = " ";
	Key2Str[SDLK_EXCLAIM] = "!";
	Key2Str[SDLK_QUOTEDBL] = "\"";
	Key2Str[SDLK_HASH] = "#";
	Key2Str[SDLK_DOLLAR] = "$";
	Key2Str[SDLK_AMPERSAND] = "&";
	Key2Str[SDLK_QUOTE] = "'";
	Key2Str[SDLK_LEFTPAREN] = "(";
	Key2Str[SDLK_RIGHTPAREN] = ")";
	Key2Str[SDLK_ASTERISK] = "*";
	Key2Str[SDLK_PLUS] = "+";
	Key2Str[SDLK_COMMA] = ",";
	Key2Str[SDLK_MINUS] = "-";
	Key2Str[SDLK_PERIOD] = ".";
	Key2Str[SDLK_SLASH] = "/";

	for (int i = SDLK_0; i <= SDLK_9; ++i) {
		Key2Str[i] = std::string(1, static_cast<char>(i));
	}

	Key2Str[SDLK_COLON] = ":";
	Key2Str[SDLK_SEMICOLON] = ";";
	Key2Str[SDLK_LESS] = "<";
	Key2Str[SDLK_EQUALS] = "=";
	Key2Str[SDLK_GREATER] = ">";
	Key2Str[SDLK_QUESTION] = "?";
	Key2Str[SDLK_AT] = "@";
	Key2Str[SDLK_LEFTBRACKET] = "[";
	Key2Str[SDLK_BACKSLASH] = "\\";
	Key2Str[SDLK_RIGHTBRACKET] = "]";
	Key2Str[SDLK_BACKQUOTE] = "`";

	for (int i = SDLK_a; i <= SDLK_z; ++i) {
		Key2Str[i] = std::string(1, static_cast<char>(i));
	}

	Key2Str[SDLK_DELETE] = "delete";

	for (int i = SDLK_KP_0; i <= SDLK_KP_9; ++i) {
		Key2Str[i] = "kp_" + std::to_string(i - SDLK_KP_0);
	}

	Key2Str[SDLK_KP_PERIOD] = "kp_period";
	Key2Str[SDLK_KP_DIVIDE] = "kp_divide";
	Key2Str[SDLK_KP_MULTIPLY] = "kp_multiply";
	Key2Str[SDLK_KP_MINUS] = "kp_minus";
	Key2Str[SDLK_KP_PLUS] = "kp_plus";
	Key2Str[SDLK_KP_ENTER] = "kp_enter";
	Key2Str[SDLK_KP_EQUALS] = "kp_equals";
	Key2Str[SDLK_UP] = "up";
	Key2Str[SDLK_DOWN] = "down";
	Key2Str[SDLK_RIGHT] = "right";
	Key2Str[SDLK_LEFT] = "left";
	Key2Str[SDLK_INSERT] = "insert";
	Key2Str[SDLK_HOME] = "home";
	Key2Str[SDLK_END] = "end";
	Key2Str[SDLK_PAGEUP] = "pageup";
	Key2Str[SDLK_PAGEDOWN] = "pagedown";

	for (int i = SDLK_F1; i <= SDLK_F15; ++i) {
		Key2Str[i] = "f" + std::to_string(i - SDLK_F1 + 1);
		Str2Key["F" + std::to_string(i - SDLK_F1 + 1)] = i;
	}

	Key2Str[SDLK_HELP] = "help";
	Key2Str[SDLK_PRINTSCREEN] = "print";
	Key2Str[SDLK_SYSREQ] = "sysreq";
	Key2Str[SDLK_PAUSE] = "break";
	Key2Str[SDLK_MENU] = "menu";
	Key2Str[SDLK_POWER] = "power";
	//Key2Str[SDLK_EURO] = "euro";
	Key2Str[SDLK_UNDO] = "undo";
}

#ifdef USE_WIN32
enum PROCESS_DPI_AWARENESS {
    PROCESS_DPI_UNAWARE = 0,
    PROCESS_SYSTEM_DPI_AWARE = 1,
    PROCESS_PER_MONITOR_DPI_AWARE = 2
};

static void setDpiAware() {
	HRESULT(WINAPI *SetProcessDpiAwareness)(PROCESS_DPI_AWARENESS dpiAwareness); // Windows 8.1 and later

	if (void* shcoreDLL = SDL_LoadObject("SHCORE.DLL")) {
		SetProcessDpiAwareness = (HRESULT(WINAPI *)(PROCESS_DPI_AWARENESS)) SDL_LoadFunction(shcoreDLL, "SetProcessDpiAwareness");
	} else {
		SetProcessDpiAwareness = nullptr;
	}
	if (SetProcessDpiAwareness) {
		/* Try Windows 8.1+ version */
		HRESULT result = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
		DebugPrint("called SetProcessDpiAwareness: %d", (result == S_OK) ? 1 : 0);
	} else {
		if (void* userDLL = SDL_LoadObject("USER32.DLL")) {
			BOOL(WINAPI *SetProcessDPIAware)(void); // Vista and later
			SetProcessDPIAware = (BOOL(WINAPI *)(void)) SDL_LoadFunction(userDLL, "SetProcessDPIAware");
			if (SetProcessDPIAware) {
				/* Try Vista - Windows 8 version.
				   This has a constant scale factor for all monitors.
				*/
				BOOL success = SetProcessDPIAware();
				DebugPrint("called SetProcessDPIAware: %d", (int)success);
			}
		}
		// In any case, on these old Windows versions we have to do a bit of
		// compatibility hacking. Windows 7 and below don't play well with
		// opengl rendering and (for some odd reason) fullscreen.
		fprintf(stdout, "\n!!! Detected old Windows version - forcing software renderer and windowed mode!!!\n\n");
		SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE);
		VideoForceFullScreen = 1;
		Video.FullScreen = 0;
	}

}
#else
static void setDpiAware() {
}
#endif

/**
**  Initialize the video part for SDL.
*/
void InitVideoSdl()
{
	Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;

	if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
		// Fix tablet input in full-screen mode
		SDL_setenv("SDL_MOUSE_RELATIVE", "0", 1);
#ifdef __ANDROID__
		// Drive the pointer entirely from raw touch/stylus events so the S-Pen and
		// finger share one precise gesture pipeline (tap = left, hold >=0.3s = right,
		// drag = box-select). SDL's automatic touch->mouse synthesis would otherwise
		// fire a duplicate left-button stream underneath us, so switch it off.
		SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
		SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
		// NG76: the game is landscape, like the Windows original -- there is no portrait layout to
		// fall back to. The manifest already says sensorLandscape, but SDL overrides it at runtime
		// (SDLActivity.setOrientation) from the surface it happens to be handed first: when that
		// surface is portrait the app comes up portrait and, after the device rotates, keeps
		// drawing into the old viewport -- pillarboxed world and HUD, briefing text clipped. Naming
		// the orientations SDL is allowed to request keeps it landscape from the first surface on.
		SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
		int res = SDL_Init(
					  SDL_INIT_AUDIO | SDL_INIT_VIDEO |
					  SDL_INIT_EVENTS | SDL_INIT_TIMER);
		if (res < 0) {
			ErrorPrint("Couldn't initialize SDL: %s\n", SDL_GetError());
			exit(1);
		}

		SDL_CUSTOM_KEY_UP = SDL_RegisterEvents(1);
#ifndef __ANDROID__
		SDL_StartTextInput();
#else
		// On Android, SDL_StartTextInput pops up the soft keyboard, which covers the
		// whole game. Keep it off; open it explicitly only when a text field is focused.
		SDL_StopTextInput();
#endif

		// Clean up on exit
		atexit(CleanUpVideoSdl);

		// If debug is enabled, Stratagus disable SDL Parachute.
		// So we need gracefully handle segfaults and aborts.
#if defined(DEBUG) && !defined(USE_WIN32)
		const auto cleanExit = [](int) {
			// Clean SDL
			CleanUpVideoSdl();
			// Reestablish normal behaviour for next abort call
			signal(SIGABRT, SIG_DFL);
			// Generates a core dump
			abort();
		};
		signal(SIGSEGV, +cleanExit);
		signal(SIGABRT, +cleanExit);
#endif
	}

	// Initialize the display

	setDpiAware();

	// Sam said: better for windows.
	/* SDL_HWSURFACE|SDL_HWPALETTE | */
	if (Video.FullScreen) {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	} else {
		flags |= SDL_WINDOW_RESIZABLE;
	}

#ifdef __ANDROID__
	// Render at the device's own aspect ratio instead of a 4:3 640x480 logical size
	// (which letterboxes and squishes the widescreen HD art). Scale the logical size so
	// the shorter axis is ~960px: crisp/HD, correct aspect, UI still a usable size.
	if (!Video.Width || !Video.Height) {
		SDL_Rect bounds;
		if (SDL_GetDisplayBounds(0, &bounds) == 0 && bounds.w > 0 && bounds.h > 0) {
			const int targetShort = 960;
			if (bounds.w >= bounds.h) {
				Video.Height = targetShort;
				Video.Width = static_cast<int>(static_cast<long long>(bounds.w) * targetShort / bounds.h);
			} else {
				Video.Width = targetShort;
				Video.Height = static_cast<int>(static_cast<long long>(bounds.h) * targetShort / bounds.w);
			}
		}
	}
#endif
	if (!Video.Width || !Video.Height) {
		Video.Width = 640;
		Video.Height = 480;
	}
	if (!Video.WindowWidth || !Video.WindowHeight) {
		Video.WindowWidth = Video.Width;
		Video.WindowHeight = Video.Height;
	}
	if (!Video.Depth) {
		Video.Depth = 32;
	}

	const char *win_title = "Stratagus";
	// Set WindowManager Title
	if (!FullGameName.empty()) {
		win_title = FullGameName.c_str();
	} else if (!Parameters::Instance.applicationName.empty()) {
		win_title = Parameters::Instance.applicationName.c_str();
	}

	const char *window_pos = SDL_GetHint("SDL_VIDEO_WINDOW_POS");
	int x = SDL_WINDOWPOS_UNDEFINED;
	int y = SDL_WINDOWPOS_UNDEFINED;
	if (window_pos) {
		std::stringstream ss(window_pos);
		ss >> x;     // X
		ss.ignore(); // skip ","
		ss >> y;     // Y
		printf("[Window Pos] %d,%d\n", x, y);
	}
	TheWindow = SDL_CreateWindow(win_title, x, y,
	                             Video.WindowWidth, Video.WindowHeight, flags);
	if (TheWindow == nullptr) {
		ErrorPrint("Couldn't set %dx%dx%d video mode: %s\n",
		           Video.Width,
		           Video.Height,
		           Video.Depth,
		           SDL_GetError());
		exit(1);
	}
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	int rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
	if (!Parameters::Instance.benchmark) {
		rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
	}
	if (!TheRenderer) {
		TheRenderer = SDL_CreateRenderer(TheWindow, -1, rendererFlags);
	}
	SDL_RendererInfo rendererInfo;
	if (!SDL_GetRendererInfo(TheRenderer, &rendererInfo)) {
		printf("[Renderer] %s\n", rendererInfo.name);
		if (strlen(rendererInfo.name) == 0) {
			dummyRenderer = true;
		}
		if (starts_with(rendererInfo.name, "opengl")) {
			LoadShaderExtensions();
		}
	}
	SDL_SetRenderDrawColor(TheRenderer, 0, 0, 0, 255);
#ifdef __ANDROID__
	// Render at the device's OWN native full-screen resolution — fully adaptive per device
	// (S8U, S7+, …), no invented fixed logical size, no letterbox: the logical framebuffer
	// exactly matches the screen so the whole display is filled 1:1. The UI is authored to
	// scale/anchor to this size. (A whole-framebuffer downscale gives a zoomed map but also
	// doubles the fixed-pixel menu art off-screen, so in-game zoom must be a map-viewport-only
	// scale, not a global logical-size cap.)
	{
		int outW = 0, outH = 0;
		SDL_GetRendererOutputSize(TheRenderer, &outW, &outH);
		// This activity is sensorLandscape, but at startup the window can briefly report a portrait
		// render size before the device orientation settles. The Lua UI, HUD and fonts are all
		// authored against Video.Width/Height at script-load time (which happens right after this),
		// so capturing a transient portrait size here lays the entire UI out portrait for the whole
		// session. Pump events until the render size reports landscape (w >= h) or a ~1s timeout: a
		// genuinely portrait device still proceeds after the timeout, while a landscape tablet gets
		// its final landscape dimensions before any script runs.
		for (int settle = 0; settle < 60 && outH > outW; ++settle) {
			SDL_PumpEvents();
			SDL_Delay(16);
			SDL_GetRendererOutputSize(TheRenderer, &outW, &outH);
		}
		if (outW > 0 && outH > 0) {
			// The window / GPU present target is always the device's native size.
			Video.WindowWidth = outW;
			Video.WindowHeight = outH;
			// Performance lever: the whole frame is CPU-software-composited at Video.Width x
			// Video.Height every frame, which is the dominant cost at full HD (a busy in-game
			// frame can miss the 33ms budget badly on weaker GPUs/CPUs, e.g. the S7+). Render the
			// framebuffer at a fraction of native and let the GPU upscale it on SDL_RenderCopy;
			// the UI stays proportional because it is all authored against Video.Width/Height.
			// Scale is read from <internal>/render_scale.txt (0.30..1.00; default 1.0 = full HD)
			// so it can be A/B tuned per device without a rebuild.
			float renderScale = 1.0f;
			if (const char *base = SDL_AndroidGetInternalStoragePath()) {
				std::string sp = std::string(base) + "/render_scale.txt";
				if (FILE *sf = fopen(sp.c_str(), "r")) {
					float v = 0.0f;
					if (fscanf(sf, "%f", &v) == 1 && v >= 0.30f && v <= 1.0f) {
						renderScale = v;
					}
					fclose(sf);
				}
			}
			Video.Width  = std::max(320, (int)lround(outW * (double)renderScale));
			Video.Height = std::max(240, (int)lround(outH * (double)renderScale));
			fprintf(stdout, "WargusHD: renderScale=%.2f -> framebuffer %dx%d, present %dx%d\n",
			        renderScale, Video.Width, Video.Height, outW, outH);
			// NG95/NG96: the framebuffer size is final here and the Lua UI is laid out right after,
			// so the in-game reference canvas has to exist before any script runs.
			Video.ComputeHudCanvas();
			fprintf(stdout, "WargusHD: hud canvas %dx%d at (%d,%d), scale %.4f\n",
			        Video.HudCanvas.W, Video.HudCanvas.H, Video.HudCanvas.X, Video.HudCanvas.Y,
			        Video.HudCanvas.Scale);
		}
		// World internal-resolution scale (independent of renderScale above): the zoomed in-game
		// world is rendered into a GPU target at this fraction of the viewport and upscaled, while
		// the HUD stays native. Read from <internal>/world_scale.txt (0.25..1.00; default 1.0 = off)
		// so it can be A/B tuned per device without a rebuild.
		if (const char *base = SDL_AndroidGetInternalStoragePath()) {
			std::string wp = std::string(base) + "/world_scale.txt";
			if (FILE *wf = fopen(wp.c_str(), "r")) {
				float v = 0.0f;
				if (fscanf(wf, "%f", &v) == 1 && v > 0.0f && v <= 1.0f) {
					WorldRenderScale = std::max(0.25f, std::min(1.0f, v));
				}
				fclose(wf);
			}
			fprintf(stdout, "WargusHD: worldScale=%.2f\n", WorldRenderScale);
		}
	}
#endif
	Video.ResizeScreen(Video.Width, Video.Height);

// #ifdef USE_WIN32
// 	HWND hwnd = nullptr;
// 	HICON hicon = nullptr;
// 	SDL_SysWMinfo info;
// 	SDL_VERSION(&info.version);

// 	if (SDL_GetWindowWMInfo(TheWindow, &info)) {
// 		hwnd = info.win.window;
// 	}

// 	if (hwnd) {
// 		hicon = ExtractIcon(GetModuleHandle(nullptr), Parameters::Instance.applicationName.c_str(), 0);
// 	}

// 	if (hicon) {
// 		SendMessage(hwnd, (UINT)WM_SETICON, ICON_SMALL, (LPARAM)hicon);
// 		SendMessage(hwnd, (UINT)WM_SETICON, ICON_BIG, (LPARAM)hicon);
// 	}
// #endif

#if !defined(USE_WIN32) && !defined(USE_MAEMO)
		std::string FullGameNameL = FullGameName;
		for (size_t i = 0; i < FullGameNameL.size(); ++i) {
			FullGameNameL[i] = tolower(FullGameNameL[i]);
		}

		std::string ApplicationName = Parameters::Instance.applicationName;
		std::string ApplicationNameL = ApplicationName;
		for (auto& c : ApplicationNameL) {
			c = tolower(c);
		}

		std::vector<fs::path> pixmaps
		{
			fs::path(PIXMAPS) / (FullGameName + ".png"),
			fs::path(PIXMAPS) / (FullGameNameL + ".png"),
			fs::path("/usr/share/pixmaps") / (FullGameName + ".png"),
			fs::path("/usr/share/pixmaps") / (FullGameNameL + ".png"),
			fs::path(PIXMAPS) / (ApplicationName + ".png"),
			fs::path(PIXMAPS) / (ApplicationNameL + ".png"),
			fs::path("/usr/share/pixmaps") / (ApplicationName + ".png"),
			fs::path("/usr/share/pixmaps") / (ApplicationNameL + ".png"),
			fs::path(PIXMAPS) / "Stratagus.png",
			fs::path(PIXMAPS) / "stratagus.png",
			fs::path("/usr/share/pixmaps/Stratagus.png"),
			fs::path("/usr/share/pixmaps/stratagus.png")
		};

		std::shared_ptr<CGraphic> g;
		SDL_Surface *icon = nullptr;

		for (const auto &p : pixmaps) {
			if (fs::exists(p)) {
				g = CGraphic::New(p.u8string());
				g->Load();
				icon = g->getSurface();
				if (icon) { break; }
			}
		}
		if (icon) {
			SDL_SetWindowIcon(TheWindow, icon);
		}
#endif
	Video.FullScreen = (SDL_GetWindowFlags(TheWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
	Video.Depth = TheScreen->format->BitsPerPixel;

	// Must not allow SDL to switch to relative mouse coordinates when going
	// fullscreen. So we don't hide the cursor, but instead set a transparent
	// 1px cursor
	Uint8 emptyCursor[] = {'\0'};
	Video.blankCursor.reset(SDL_CreateCursor(emptyCursor, emptyCursor, 1, 1, 0, 0));
	SDL_SetCursor(Video.blankCursor.get());

	InitKey2Str();

	ColorBlack = Video.MapRGB(TheScreen->format, 0, 0, 0);
	// M666-9: the green of the original's health bars is (52, 112, 4) -- the portrait bar in 50
	// captures of work/agent-orig-scenes (e.g. e01, hi10, b02) and the bar over a unit (k06, k20,
	// k31); it was the classic (48, 100, 4).
	ColorDarkGreen = Video.MapRGB(TheScreen->format, 52, 112, 4);
	ColorLightBlue = Video.MapRGB(TheScreen->format, 52, 113, 166);
	ColorBlue = Video.MapRGB(TheScreen->format, 0, 0, 252);
	ColorOrange = Video.MapRGB(TheScreen->format, 248, 140, 20);
	ColorWhite = Video.MapRGB(TheScreen->format, 252, 248, 240);
	ColorLightGray = Video.MapRGB(TheScreen->format, 192, 192, 192);
	ColorGray = Video.MapRGB(TheScreen->format, 128, 128, 128);
	ColorDarkGray = Video.MapRGB(TheScreen->format, 64, 64, 64);
	ColorRed = Video.MapRGB(TheScreen->format, 252, 0, 0);
	ColorGreen = Video.MapRGB(TheScreen->format, 0, 252, 0);
	ColorYellow = Video.MapRGB(TheScreen->format, 252, 252, 0);

	UI.MouseWarpPos.x = UI.MouseWarpPos.y = -1;
}

/**
**  Invalidate some area
**
**  @param x  screen pixel X position.
**  @param y  screen pixel Y position.
**  @param w  width of rectangle in pixels.
**  @param h  height of rectangle in pixels.
*/
void InvalidateArea(int x, int y, int w, int h)
{
	Assert(NumRects != sizeof(Rects) / sizeof(*Rects));
	Assert(x >= 0 && y >= 0 && x + w <= Video.Width && y + h <= Video.Height);
	Rects[NumRects].x = x;
	Rects[NumRects].y = y;
	Rects[NumRects].w = w;
	Rects[NumRects].h = h;
	++NumRects;
}

/**
**  Invalidate whole window
*/
void Invalidate()
{
	Rects[0].x = 0;
	Rects[0].y = 0;
	Rects[0].w = Video.Width;
	Rects[0].h = Video.Height;
	NumRects = 1;
}


static bool isTextInput(int key) {
	return key >= 32 && key <= 128 && !(KeyModifiers & (ModifierAlt | ModifierControl | ModifierSuper));
}

#ifdef __ANDROID__
// ---------------------------------------------------------------------------
// Touch / S-Pen gesture translation
//
// The S-Pen and finger are both delivered as SDL touch fingers. WC2 needs two
// pointer buttons (left = select/UI, right = move/attack order) but a touch
// screen has none, so we synthesise them from a single-finger gesture:
//
//   * quick tap (released < LONGPRESS, no drift)  -> LEFT click
//   * hold in place >= 0.3 s, then release        -> RIGHT click (order/cancel)
//   * drag beyond DRAG_PX                          -> LEFT press..release (box-select / scroll)
//
// Touch routes for what a keyboard does in the original (owner decision 2026-10-08; the
// original has no touch input, so none of these has an original counterpart):
//
//   * TOUCH-R1  a touch that lands on a slot of the group strip belongs to the strip and never
//               becomes a pointer event: tap = select the group, second tap = centre on it,
//               hold = store the selection; a tap on the free slot stores as well
//               (ui/interface.cpp).
//   * TOUCH-R2  after a hold order on the map, the finger slides on and rests again:
//               each rest is one more RIGHT click, sent with Shift held (a queued order).
//   * TOUCH-R4/R6  a second tap close to the first and soon after it is sent at the first
//               tap's exact point, so that the engine's own double-click test sees it
//               (double tap on a unit = its type; on empty ground = clear the selection).
//   * TOUCH-R5  the same second touch, if it becomes a drag, is a selection box whose
//               release is sent with Shift held (the box is added to the selection).
//   * TOUCH-R7  nothing here: the press / release of a tap and of a drag are told apart in
//               ui/mouse.cpp by the pointer motion between them.
//
// Only the first finger drives the pointer; extra fingers are ignored so a
// stray palm/second touch cannot hijack the gesture. SDL's own touch->mouse
// synthesis is disabled (see InitVideoSdl) so this is the sole pointer source.
// ---------------------------------------------------------------------------
static const Uint32 TOUCH_LONGPRESS_MS = 300;   // hold this long -> right click
static const int    TOUCH_DRAG_PX      = 24;    // drift past this -> drag (left)

static bool         TouchActive    = false;
static SDL_FingerID TouchFinger    = 0;
static Uint32       TouchDownTicks = 0;
static int          TouchDownX     = 0;
static int          TouchDownY     = 0;
static bool         TouchDragging  = false;     // left button held for a drag
// A tap's click is emitted one frame AFTER the motion that positions it. Measured on device: a
// synthesised click is only honoured when the widget layer has consumed the positioning motion in an
// EARLIER gcn::Gui::logic() pass -- and a quick tap puts motion and click in the same pass whenever no
// frame boundary happens to fall between FINGERDOWN and FINGERUP. That made a single tap work or not
// work purely by frame rate (~110 ms/frame: worked; ~410 ms/frame: silently did nothing). A real mouse
// never produces that ordering, so the adapter must not either (rules 37, 40).
static bool         TouchClickPending = false;
static int          TouchClickX       = 0;
static int          TouchClickY       = 0;
static Uint8        TouchClickButton  = 0;
static bool         TouchLongFired = false;     // the hold->right-click already fired mid-hold
static float        Finger1X       = 0.0f;      // last pixel pos of the primary finger
static float        Finger1Y       = 0.0f;

// TOUCH-R4/R5/R6: the second touch of a pair. The time window is the engine's own
// DoubleClickDelay (300 ms, stratagus.lua); the distance is twice the drag threshold.
static const int    TOUCH_PAIR_PX  = 2 * TOUCH_DRAG_PX; // TOUCH_SPEC 3.1: second touch this near the first tap
static bool         TouchTapValid  = false;     // a tap on the map view has just ended
static Uint32       TouchTapTicks  = 0;         // when its finger lifted
static int          TouchTapX      = 0;         // where its click was sent
static int          TouchTapY      = 0;
static bool         TouchPair      = false;     // this touch began as the second of a pair
static bool         TouchAddDrag   = false;     // ... and became the add-to-selection box
static bool         TouchLongArmed = false;     // pair only: pointer moved to the true point, order next frame

// TOUCH-R2: hold, then slide and rest = queued order.
static const Uint32 TOUCH_WAYPOINT_MS     = TOUCH_LONGPRESS_MS; // TOUCH_SPEC 3.1: rest this long = one more order
static const int    TOUCH_WAYPOINT_MIN_PX = 2 * TOUCH_DRAG_PX;  // TOUCH_SPEC 3.1: at least this far from the last order
static bool         TouchChain     = false;     // the fired hold was an order that may be followed
static int          TouchChainX    = 0;         // where the last order of the chain was given
static int          TouchChainY    = 0;
static int          TouchRestX     = 0;         // where the finger rests now
static int          TouchRestY     = 0;
static Uint32       TouchRestTicks = 0;         // since when

// TOUCH-R1: a touch that began on the group strip.
static int          TouchStripSlot     = 0;     // group whose slot holds this touch (0: none)
static bool         TouchStripLost     = false; // the finger left the slot: nothing will fire
static int          TouchStripTapSlot  = 0;     // slot of the last strip tap (0: none)
static Uint32       TouchStripTapTicks = 0;     // when that tap's finger lifted

// ---------------------------------------------------------------------------
// Two-finger camera controls.
//
// When a second finger lands both fingers together drive the map viewport
// instead of the pointer:
//
//   * PAN   - the two-finger centroid drags the map (high sensitivity). This is
//             the dominant gesture and works from the moment the pair is down.
//   * PINCH - changing the finger separation steps the map zoom (deliberately
//             low sensitivity so an ordinary pan does not trip it). A step is
//             only taken once the fingers have settled, the separation has
//             changed by a large fraction, and that change dominates the
//             centroid drift since the last step.
//
// The single-finger pointer gesture is cancelled the instant the second finger
// arrives, and neither finger of the pair may start a new pointer gesture: a
// fresh all-fingers-up is required before single-finger handling resumes.
// ---------------------------------------------------------------------------
static const Uint32 TWO_FINGER_SETTLE_MS   = 90;    // ignore pinch until fingers settle
static const float  PINCH_STEP_RATIO       = 0.35f; // separation must change >=35% to step zoom
// WORLD-ZOOM-RULE, option A: the original has no map zoom -- the tile size is fixed by the screen
// height (32 * k px). With this off the two-finger gesture pans only and the view always shows the
// original's map area.
static const bool   PinchZoomEnabled       = false;
// Discrete zoom stops. Stepping between fixed values avoids the jitter a
// continuous mapping would produce from small finger tremors.
static const float  ZoomSteps[]            = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
static const int    NumZoomSteps           = static_cast<int>(sizeof(ZoomSteps) / sizeof(ZoomSteps[0]));

static bool         TwoFingerActive  = false;   // both fingers driving the camera
static bool         SuppressGestures = false;   // block new single-finger gestures until all up
static int          TouchFingerCount = 0;       // fingers currently down
static bool         TouchPointerParked = false; // pointer moved off every widget after a menu tap
static int          TouchHoverEndFrames = 0;    // frames left until the in-game HUD hover is dropped (0: none due)
static SDL_FingerID Finger2          = 0;
static float        Finger2X         = 0.0f;
static float        Finger2Y         = 0.0f;
static Uint32       TwoFingerStartTicks = 0;
static float        PinchRefDist     = 0.0f;    // separation at the last accepted zoom step
static float        CentroidRefX     = 0.0f;    // centroid at the last accepted zoom step
static float        CentroidRefY     = 0.0f;
static float        LastCentroidX    = 0.0f;    // centroid on the previous motion frame
static float        LastCentroidY    = 0.0f;
static double       PanAccumX        = 0.0;     // fractional map-pixel pan carry
static double       PanAccumY        = 0.0;

/// Map a normalised finger position to window pixels. The mouse-button/motion
/// cases below apply the usual VerticalPixelSize correction, so leave the y
/// unscaled here to stay identical to a real mouse event.
static void TouchToPixel(float nx, float ny, int &px, int &py)
{
	// Map the normalised finger position onto the LOGICAL framebuffer (Video.Width/
	// Height), which is what the engine and menu widgets use — this stays correct when
	// the logical size is a fraction of the window (see the zoom cap in InitVideoSdl).
	px = static_cast<int>(nx * Video.Width + 0.5f);
	py = static_cast<int>(ny * Video.Height + 0.5f);
}

// Re-emit the gesture as ordinary SDL mouse events. Feeding the queue (rather
// than calling InputMouse* directly) is essential: it drives BOTH the in-game
// input path AND the guichan menu widgets, exactly as SDL's own touch->mouse
// synthesis used to - the difference is purely which button and when.
static void PushMouseMotion(int x, int y)
{
	// A motion to a negative position parks the pointer off every widget (see the end of a
	// tap); any other motion means a finger is down again.
	TouchPointerParked = (x < 0 || y < 0);
	SDL_Event e;
	SDL_zero(e);
	e.type = SDL_MOUSEMOTION;
	e.motion.x = x;
	e.motion.y = y;
	SDL_PushEvent(&e);
}

static void PushMouseButton(Uint32 type, Uint8 button, int x, int y)
{
	SDL_Event e;
	SDL_zero(e);
	e.type = type;
	e.button.button = button;
	e.button.state = (type == SDL_MOUSEBUTTONDOWN) ? SDL_PRESSED : SDL_RELEASED;
	e.button.clicks = 1;
	e.button.x = x;
	e.button.y = y;
	SDL_PushEvent(&e);
}

/// TOUCH-R2 / TOUCH-R5: press or release Shift around a synthesised click. The engine reads the
/// queue flag of every order and the add-to-selection flag of a box from KeyModifiers
/// (ui/mouse.cpp, `KeyModifiers & ModifierShift`), which only key events set
/// (HandleKeyModifiersDown / Up); the key events travel in the same queue as the click, so
/// the modifier is held for exactly that click. Same device as the mouse-wheel case below.
static void PushShiftKey(bool down)
{
	SDL_Event e;
	SDL_zero(e);
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.sym = SDLK_LSHIFT;
	SDL_PushEvent(&e);
}

/// The map viewport the camera gestures act on. Prefer the selected viewport,
/// falling back to the first one; null when no in-game viewport exists (e.g. in
/// a menu), in which case the gestures simply suppress the pointer.
static CViewport *ActiveGameViewport()
{
	if (UI.SelectedViewport) {
		return UI.SelectedViewport;
	}
	if (UI.NumViewports > 0) {
		return &UI.Viewports[0];
	}
	return nullptr;
}

/// Is the pointer the game's (not a menu's, not the editor's)? The touch routes exist only there.
static bool TouchInGame()
{
	return GameRunning && !GuichanOwnsInput() && Editor.Running == EditorNotRunning;
}

/// Is a screen point inside the map view?
static bool TouchOnMapView(int x, int y)
{
	const CViewport *vp = ActiveGameViewport();
	return vp && vp->Contains(PixelPos(x, y));
}

/// Next larger discrete zoom stop above z (returns the top stop if already there).
static float NextZoomStep(float z)
{
	for (int i = 0; i < NumZoomSteps; ++i) {
		if (ZoomSteps[i] > z + 0.01f) {
			return ZoomSteps[i];
		}
	}
	return ZoomSteps[NumZoomSteps - 1];
}

/// Next smaller discrete zoom stop below z (returns the bottom stop if already there).
static float PrevZoomStep(float z)
{
	for (int i = NumZoomSteps - 1; i >= 0; --i) {
		if (ZoomSteps[i] < z - 0.01f) {
			return ZoomSteps[i];
		}
	}
	return ZoomSteps[0];
}

/// Set the game viewport top-left to the given map-pixel corner using the public
/// Set() overload (tile (0,0) top-left is map pixel (0,0), so the offset is the
/// absolute map-pixel corner). Set() applies the engine's scroll clamp.
static void SetViewportMapCorner(CViewport *vp, int mapPixelX, int mapPixelY)
{
	vp->Set(Vec2i(0, 0), PixelDiff(mapPixelX, mapPixelY));
}

/// Current map-pixel coordinate of the viewport's top-left corner.
static PixelPos ViewportMapCorner(const CViewport *vp)
{
	// ScreenToMapPixelPos of the on-screen top-left yields exactly the corner.
	return vp->ScreenToMapPixelPos(vp->GetTopLeftPos());
}

/// Begin two-finger mode: cancel any pending single-finger gesture and seed the
/// pan/pinch reference state from the current finger geometry.
static void BeginTwoFinger(SDL_FingerID secondId, float f2x, float f2y)
{
	// Cancel the pending single-finger gesture cleanly. If a left-drag was
	// already injected, release it here so nothing is left held down. If the
	// gesture was still tentative (no button injected), just drop it - no tap
	// or right-click must fire for these fingers.
	if (TouchDragging) {
		PushMouseButton(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT,
		                static_cast<int>(Finger1X + 0.5f),
		                static_cast<int>(Finger1Y + 0.5f));
	}
	TouchDragging = false;
	// The touch routes end with the single-finger gesture: no waypoint, no strip action and
	// no pair is carried into or out of a camera gesture.
	TouchChain    = false;
	TouchPair     = false;
	TouchAddDrag  = false;
	TouchTapValid = false;
	if (TouchStripSlot) {
		TouchStripLost = true;
		UiTouchGroupPress(0);
	}

	TwoFingerActive     = true;
	Finger2             = secondId;
	Finger2X            = f2x;
	Finger2Y            = f2y;
	TwoFingerStartTicks = SDL_GetTicks();

	const float cx = (Finger1X + Finger2X) * 0.5f;
	const float cy = (Finger1Y + Finger2Y) * 0.5f;
	LastCentroidX = CentroidRefX = cx;
	LastCentroidY = CentroidRefY = cy;
	PinchRefDist  = std::hypot(Finger1X - Finger2X, Finger1Y - Finger2Y);
	PanAccumX = PanAccumY = 0.0;
}

/// Process a motion frame while two fingers are down: pan by the centroid delta
/// and, when the separation change dominates, step the zoom.
static void UpdateTwoFinger()
{
	const float cx = (Finger1X + Finger2X) * 0.5f;
	const float cy = (Finger1Y + Finger2Y) * 0.5f;
	const float dist = std::hypot(Finger1X - Finger2X, Finger1Y - Finger2Y);

	CViewport *vp = ActiveGameViewport();

	// --- Pinch zoom (low sensitivity, discrete steps) ----------------------
	const bool settled = (SDL_GetTicks() - TwoFingerStartTicks) >= TWO_FINGER_SETTLE_MS;
	const float distChange    = std::fabs(dist - PinchRefDist);
	const float centroidDrift = std::hypot(cx - CentroidRefX, cy - CentroidRefY);
	bool zoomed = false;

	if (PinchZoomEnabled && vp && settled && PinchRefDist > 1.0f
		&& distChange >= PINCH_STEP_RATIO * PinchRefDist
		&& distChange >= centroidDrift) {
		const float curZoom = vp->Zoom;
		const float newZoom = (dist > PinchRefDist) ? NextZoomStep(curZoom)
		                                            : PrevZoomStep(curZoom);
		if (newZoom != curZoom) {
			// Keep the map point under the centroid fixed across the zoom
			// change: read it before the scale changes, then re-anchor.
			const PixelPos anchor =
				vp->ScreenToMapPixelPos(PixelPos(static_cast<int>(cx + 0.5f),
				                                 static_cast<int>(cy + 0.5f)));
			SetMapViewportsZoom(newZoom);
			const float z = (vp->Zoom > 0.0f) ? vp->Zoom : 1.0f;
			const int cornerX = static_cast<int>(std::lround(
				anchor.x - (cx - vp->GetTopLeftPos().x) / z));
			const int cornerY = static_cast<int>(std::lround(
				anchor.y - (cy - vp->GetTopLeftPos().y) / z));
			SetViewportMapCorner(vp, cornerX, cornerY);
			zoomed = true;
		}
		// Reset the references after any accepted step so the next step needs a
		// fresh large separation change - this is what makes zoom discrete.
		PinchRefDist = dist;
		CentroidRefX = cx;
		CentroidRefY = cy;
	}

	// --- Pan (high sensitivity) --------------------------------------------
	// Skip on the frame a zoom step was applied - that frame already re-centred
	// the view on the centroid, so also folding in a pan would double-count.
	if (vp && !zoomed) {
		const float z = (vp->Zoom > 0.0f) ? vp->Zoom : 1.0f;
		// Map follows the fingers: a rightward/downward finger drag reveals the
		// content to the upper-left, i.e. the map corner moves the opposite way.
		// Divide the on-screen delta by the zoom because the map is upscaled.
		PanAccumX += -static_cast<double>(cx - LastCentroidX) / z;
		PanAccumY += -static_cast<double>(cy - LastCentroidY) / z;
		const int mdx = static_cast<int>(PanAccumX);
		const int mdy = static_cast<int>(PanAccumY);
		if (mdx != 0 || mdy != 0) {
			PanAccumX -= mdx;
			PanAccumY -= mdy;
			const PixelPos corner = ViewportMapCorner(vp);
			SetViewportMapCorner(vp, corner.x + mdx, corner.y + mdy);
		}
	}

	LastCentroidX = cx;
	LastCentroidY = cy;
}

/// Leave two-finger mode when either finger of the pair lifts. The remaining
/// finger must not start a new pointer gesture, so drop pointer tracking and
/// keep single-finger handling suppressed until every finger is up.
static void EndTwoFinger()
{
	TwoFingerActive  = false;
	TouchActive      = false;
	TouchDragging    = false;
	SuppressGestures = true;
}

static void HandleFingerEvent(const SDL_Event &event)
{
	int px, py;
	TouchToPixel(event.tfinger.x, event.tfinger.y, px, py);
	const float fx = event.tfinger.x * Video.Width;
	const float fy = event.tfinger.y * Video.Height;

	switch (event.type) {
		case SDL_FINGERDOWN:
			++TouchFingerCount;
			if (TwoFingerActive) {
				break; // already have our pair; extra fingers are ignored
			}
			if (TouchActive) {
				// Second finger: hand control to the camera gestures.
				BeginTwoFinger(event.tfinger.fingerId, fx, fy);
				break;
			}
			if (SuppressGestures) {
				break; // leftover finger after a camera gesture; wait for all-up
			}
			// First finger of a fresh gesture.
			TouchActive    = true;
			TouchFinger    = event.tfinger.fingerId;
			TouchDragging  = false;
			TouchLongFired = false;
			TouchLongArmed = false;
			TouchPair      = false;
			TouchAddDrag   = false;
			TouchChain     = false;
			TouchStripSlot = 0;
			TouchStripLost = false;
			// The moment the finger touched, not the moment this frame got round to it: after a
			// slow frame the two differ by the frame's length, and a hold would be measured
			// too long by that much.
			TouchDownTicks = event.tfinger.timestamp ? event.tfinger.timestamp : SDL_GetTicks();
			TouchDownX     = px;
			TouchDownY     = py;
			Finger1X       = fx;
			Finger1Y       = fy;
			if (TouchInGame()) {
				// TOUCH-R1: a slot of the group strip takes the touch. No pointer motion is
				// sent, so the cursor, the hover, the edge scroll and a building preview
				// stay as they are.
				TouchStripSlot = UiTouchGroupSlotAt(PixelPos(px, py));
				if (TouchStripSlot) {
					UiTouchGroupPress(TouchStripSlot);
					TouchTapValid = false;
					break;
				}
				// TOUCH-R4/R5/R6: soon after a tap on the map view and close to it.
				TouchPair = TouchTapValid
				            && TouchDownTicks - TouchTapTicks <= static_cast<Uint32>(DoubleClickDelay)
				            && std::abs(px - TouchTapX) <= TOUCH_PAIR_PX
				            && std::abs(py - TouchTapY) <= TOUCH_PAIR_PX;
			}
			TouchTapValid = false;
			// Park the cursor under the finger so hover/tooltips track it, but
			// do not commit to a button yet - the gesture is still ambiguous.
			// The second touch of a pair parks it on the first tap's point instead: the engine
			// drops its double-click state on any change of the pointer position
			// (InputMouseMove), and two taps of a finger never land on the same pixel.
			if (TouchPair) {
				PushMouseMotion(TouchTapX, TouchTapY);
			} else {
				PushMouseMotion(px, py);
			}
			break;

		case SDL_FINGERMOTION:
			if (TwoFingerActive) {
				if (event.tfinger.fingerId == TouchFinger) {
					Finger1X = fx;
					Finger1Y = fy;
				} else if (event.tfinger.fingerId == Finger2) {
					Finger2X = fx;
					Finger2Y = fy;
				} else {
					break; // motion from an ignored extra finger
				}
				UpdateTwoFinger();
				break;
			}
			if (!TouchActive || event.tfinger.fingerId != TouchFinger) {
				break;
			}
			Finger1X = fx;
			Finger1Y = fy;
			if (TouchStripSlot) {
				// TOUCH-R1: sliding off the slot (by more than the drag threshold) drops the
				// touch; it never turns into a drag on the map.
				int sx, sy, sw, sh;
				if (!TouchStripLost
					&& (!UiTouchGroupSlotRect(TouchStripSlot, sx, sy, sw, sh)
						|| px < sx - TOUCH_DRAG_PX || px >= sx + sw + TOUCH_DRAG_PX
						|| py < sy - TOUCH_DRAG_PX || py >= sy + sh + TOUCH_DRAG_PX)) {
					TouchStripLost = true;
					UiTouchGroupPress(0);
				}
				break;
			}
			if (TouchLongFired) {
				if (!TouchChain) {
					break;   // after a mid-hold order fired, ignore drift until release
				}
				// TOUCH-R2: the pointer goes with the finger again, and a move of more than
				// the drag threshold starts a new rest (PollTouchWaypoint).
				PushMouseMotion(px, py);
				if (std::abs(px - TouchRestX) > TOUCH_DRAG_PX
					|| std::abs(py - TouchRestY) > TOUCH_DRAG_PX) {
					TouchRestX     = px;
					TouchRestY     = py;
					TouchRestTicks = SDL_GetTicks();
				}
				break;
			}
			{
				const bool drift = std::abs(px - TouchDownX) > TOUCH_DRAG_PX
				                   || std::abs(py - TouchDownY) > TOUCH_DRAG_PX;
				if (TouchPair && !TouchDragging && !drift) {
					break;   // still a possible second tap: the pointer stays on the first tap's point
				}
				PushMouseMotion(px, py);
				if (!TouchDragging && drift) {
					// Enough drift: this is a drag. Start a left press from the
					// origin so box-select / map-scroll behave like a held button.
					TouchDragging = true;
					// TOUCH-R5: the drag of a pair, begun and ended on the map view while the
					// engine is in pointer mode, adds its box (Shift at the release, below).
					TouchAddDrag = TouchPair && TouchInGame()
					               && TouchOnMapView(TouchTapX, TouchTapY)
					               && TouchOnMapView(TouchDownX, TouchDownY)
					               && UITouchAddBoxAllowed();
					if (TouchAddDrag) {
						// The first tap of the pair, on empty ground, has cleared the selection.
						UITouchRestoreClearedSelection();
					}
					PushMouseButton(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, px, py);
				}
			}
			break;

		case SDL_FINGERUP:
			if (TouchFingerCount > 0) {
				--TouchFingerCount;
			}
			if (TwoFingerActive
				&& (event.tfinger.fingerId == TouchFinger || event.tfinger.fingerId == Finger2)) {
				EndTwoFinger();
				if (TouchFingerCount == 0) {
					SuppressGestures = false;
				}
				break;
			}
			if (!TouchActive || event.tfinger.fingerId != TouchFinger) {
				if (TouchFingerCount == 0) {
					// All fingers up: allow single-finger gestures again.
					SuppressGestures = false;
				}
				break;
			}
			if (TouchStripSlot) {
				// TOUCH-R1: a tap on a slot. The first selects the group; a second one on
				// the same slot within the double-click time centres the view on it. A hold
				// has already stored (PollTouchLongPress); a touch that slid off does nothing.
				UiTouchGroupPress(0);
				if (!TouchLongFired && !TouchStripLost) {
					const Uint32 now = event.tfinger.timestamp ? event.tfinger.timestamp : SDL_GetTicks();
					if (UiTouchGroupSlotState(TouchStripSlot) == 1) {
						// Owner decision 2026-10-08, Q2 = B: the free slot has nothing to
						// recall and nothing to overwrite, so a tap stores the selection
						// there. That tap is not the first of a centring pair.
						UiTouchGroupStore(TouchStripSlot);
						TouchStripTapSlot = 0;
					} else if (TouchStripTapSlot == TouchStripSlot
						&& now - TouchStripTapTicks <= static_cast<Uint32>(DoubleClickDelay)) {
						UiTouchGroupCentre(TouchStripSlot);
						TouchStripTapSlot = 0;
					} else {
						UiTouchGroupRecall(TouchStripSlot);
						TouchStripTapSlot  = TouchStripSlot;
						TouchStripTapTicks = now;
					}
				}
				TouchStripSlot = 0;
			} else if (TouchDragging) {
				if (TouchAddDrag) {
					UITouchRestoreClearedSelection();   // if the clear came after the drag began
					PushShiftKey(true);  // TOUCH-R5: the box is added (UIHandleButtonUp reads Shift)
				}
				PushMouseMotion(px, py);
				PushMouseButton(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, px, py);
				if (TouchAddDrag) {
					PushShiftKey(false);
				}
				// NG99: a drag ends here, not in FlushPendingTouchClick -- it never sets
				// TouchClickPending -- so it needs its own "the finger is gone". Measured: without
				// this, scrolling a list left 5167 hover-coloured pixels parked on the row the
				// finger lifted from, exactly the defect the tap path was just fixed for.
				// The release is pushed immediately here rather than deferred a frame, so the
				// sentinel can follow it directly.
				if (GuichanOwnsInput()) {
					PushMouseMotion(-1, -1);
				}
			} else if (TouchLongFired) {
				// The hold order already fired mid-hold (PollTouchLongPress); the lift
				// just ends the gesture, no extra click. (TOUCH-R2: nor an extra waypoint.)
				PushMouseMotion(px, py);
			} else {
				// Quick release before the hold threshold -> a tap = LEFT click (select).
				// The motion is pushed now; the click itself waits for the next frame
				// (FlushPendingTouchClick) so the widget layer sees move-then-click, as with a mouse.
				// The second tap of a pair is sent at the first tap's point (see SDL_FINGERDOWN).
				const int cx = TouchPair ? TouchTapX : px;
				const int cy = TouchPair ? TouchTapY : py;
				PushMouseMotion(cx, cy);
				TouchClickPending = true;
				TouchClickX       = cx;
				TouchClickY       = cy;
				TouchClickButton  = SDL_BUTTON_LEFT;
				// A tap on the map view may be the first of a pair. A pair is over after its
				// second tap (the engine's double-click state is, too).
				TouchTapValid = !TouchPair && TouchInGame() && TouchOnMapView(cx, cy);
				TouchTapTicks = event.tfinger.timestamp ? event.tfinger.timestamp : SDL_GetTicks();
				TouchTapX     = cx;
				TouchTapY     = cy;
			}
			TouchActive    = false;
			TouchDragging  = false;
			TouchLongFired = false;
			TouchLongArmed = false;
			TouchPair      = false;
			TouchAddDrag   = false;
			TouchChain     = false;
			// IG-010 / HUD-029: the finger is gone. Three frame ends from here the motion pushed
			// above and a tap's click (pushed at the end of this frame, handled in the next one)
			// have both been taken; then the hover is dropped (PollTouchHoverEnd).
			TouchHoverEndFrames = 3;
			if (TouchFingerCount == 0) {
				SuppressGestures = false;
			}
			break;
	}
}

/// TOUCH-R2: after a hold order on the map the finger has slid on and now rests: one more
/// RIGHT click there, with Shift held, so the engine appends the order instead of replacing
/// the queue. A rest is TOUCH_WAYPOINT_MS without a move of more than TOUCH_DRAG_PX; it must
/// be at least TOUCH_WAYPOINT_MIN_PX from the last order, so a finger that simply stays down
/// gives no second order. The engine is asked again every time (selection gone, pointer on
/// the HUD or on a scroll edge, game paused: no order, the chain stays open).
static void PollTouchWaypoint()
{
	if (!TouchChain || SDL_GetTicks() - TouchRestTicks < TOUCH_WAYPOINT_MS) {
		return;
	}
	if (std::abs(TouchRestX - TouchChainX) < TOUCH_WAYPOINT_MIN_PX
		&& std::abs(TouchRestY - TouchChainY) < TOUCH_WAYPOINT_MIN_PX) {
		return;
	}
	if (!TouchInGame() || !UITouchOrderChainAllowed()) {
		return;
	}
	// The order goes to where the finger is (at most TOUCH_DRAG_PX from the rest point).
	const int wx = static_cast<int>(Finger1X + 0.5f);
	const int wy = static_cast<int>(Finger1Y + 0.5f);
	TouchChainX = wx;
	TouchChainY = wy;
	TouchRestX  = wx;
	TouchRestY  = wy;
	PushShiftKey(true);
	PushMouseMotion(wx, wy);
	PushMouseButton(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, wx, wy);
	PushMouseButton(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, wx, wy);
	PushShiftKey(false);
}

// Called once per frame (WaitEventsOneFrame): a stationary press that crosses the hold
// threshold fires its RIGHT click immediately, WHILE the finger is still down, instead of
// waiting for release. A real drag trips TOUCH_DRAG_PX first and never reaches here.
static void PollTouchLongPress()
{
	if (!TouchActive || TouchDragging || TwoFingerActive) {
		return;
	}
	if (TouchLongFired) {
		PollTouchWaypoint();
		return;
	}
	if (SDL_GetTicks() - TouchDownTicks < TOUCH_LONGPRESS_MS) {
		return;
	}
	if (TouchStripSlot) {
		// TOUCH-R1: a hold on a slot stores the selection in its group.
		TouchLongFired = true;
		if (!TouchStripLost) {
			UiTouchGroupStore(TouchStripSlot);
		}
		return;
	}
	if (TouchPair && !TouchLongArmed) {
		// The second touch of a pair has kept the pointer on the first tap's point. It is a
		// hold after all: move the pointer to where the finger is and give the order one
		// frame later, when the engine knows what is under it.
		TouchLongArmed = true;
		PushMouseMotion(TouchDownX, TouchDownY);
		return;
	}
	TouchLongFired = true;
	// TOUCH-R2: asked before the click is sent, for the state the click will meet.
	TouchChain     = TouchInGame() && UITouchOrderChainAllowed();
	TouchChainX    = TouchDownX;
	TouchChainY    = TouchDownY;
	TouchRestX     = static_cast<int>(Finger1X + 0.5f);
	TouchRestY     = static_cast<int>(Finger1Y + 0.5f);
	TouchRestTicks = SDL_GetTicks();
	PushMouseMotion(TouchDownX, TouchDownY);
	PushMouseButton(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT, TouchDownX, TouchDownY);
	PushMouseButton(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT, TouchDownX, TouchDownY);
}

// ---------------------------------------------------------------------------
// Debug bridge: play a finger gesture into the touch layer (verb `touch`). The steps become
// ordinary SDL finger events, so they run through HandleFingerEvent exactly as a finger
// does; `adb shell input` cannot time two touches 100 ms apart, this can (to one frame).
// Nothing here runs unless the bridge starts a script.
// ---------------------------------------------------------------------------
struct TouchScriptStep {
	char   op;      // 'd' down, 'm' move, 'u' up, 'w' wait
	int    finger;  // 1 or 2
	int    x;       // logical framebuffer pixels (Video.Width x Video.Height)
	int    y;
	Uint32 ms;      // 'w' only
};
static std::vector<TouchScriptStep> TouchScript;
static size_t TouchScriptPos       = 0;
static Uint32 TouchScriptWaitStart = 0;
static Uint32 TouchScriptWaitMs    = 0;
static int    TouchScriptX[2]      = {0, 0};
static int    TouchScriptY[2]      = {0, 0};

static void PushScriptFinger(Uint32 type, int finger, int x, int y)
{
	SDL_Event e;
	SDL_zero(e);
	e.type = type;
	e.tfinger.timestamp = SDL_GetTicks();
	e.tfinger.touchId = 0x7000;
	e.tfinger.fingerId = 0x7000 + finger;
	e.tfinger.x = (x + 0.25f) / Video.Width;   // TouchToPixel rounds to the nearest pixel
	e.tfinger.y = (y + 0.25f) / Video.Height;
	e.tfinger.pressure = (type == SDL_FINGERUP) ? 0.0f : 1.0f;
	SDL_PushEvent(&e);
}

/// Called once per frame before the frame's events are read.
static void PollTouchScript()
{
	while (TouchScriptPos < TouchScript.size()) {
		if (SDL_GetTicks() - TouchScriptWaitStart < TouchScriptWaitMs) {
			return;
		}
		TouchScriptWaitMs = 0;
		const TouchScriptStep &step = TouchScript[TouchScriptPos++];
		const int f = step.finger - 1;
		switch (step.op) {
			case 'd':
			case 'm':
				TouchScriptX[f] = step.x;
				TouchScriptY[f] = step.y;
				PushScriptFinger(step.op == 'd' ? SDL_FINGERDOWN : SDL_FINGERMOTION, step.finger, step.x, step.y);
				break;
			case 'u':
				PushScriptFinger(SDL_FINGERUP, step.finger, TouchScriptX[f], TouchScriptY[f]);
				break;
			default: // 'w'
				TouchScriptWaitStart = SDL_GetTicks();
				TouchScriptWaitMs    = step.ms;
				break;
		}
	}
	if (!TouchScript.empty()) {
		TouchScript.clear();
		TouchScriptPos = 0;
	}
}
#endif // __ANDROID__

/**
**  Handle interactive input event.
**
**  @param callbacks  Callback structure for events.
**  @param event      SDL event structure pointer.
*/
#ifdef __ANDROID__
/// Emit a tap's click, one frame after the motion that positioned it. Called from
/// WaitEventsOneFrame immediately after the frame's Gui->logic() pass, so the events land in the
/// NEXT frame's queue and are handled after the move has already been consumed.
static void FlushPendingTouchClick()
{
	if (!TouchClickPending) {
		return;
	}
	TouchClickPending = false;
	PushMouseButton(SDL_MOUSEBUTTONDOWN, TouchClickButton, TouchClickX, TouchClickY);
	PushMouseButton(SDL_MOUSEBUTTONUP, TouchClickButton, TouchClickX, TouchClickY);

	// NG99: the finger is gone, so nothing is under a pointer any more. Without this the synthetic
	// pointer stays parked where the finger lifted and the widget layer keeps treating it as a
	// hovering mouse -- a tapped list row held its hover plate indefinitely, outranking the real
	// selection (ImageListBox::draw checks the hover before the selected/unselected branch).
	// gcn dispatches mouseExited when the widget under the pointer changes, and ImageListBox
	// already clears its hover index there; the signal simply never arrived.
	//
	// AFTER the click, never at SDL_FINGERUP: the click is deliberately deferred a frame so the
	// widget layer sees move-then-click as it would with a mouse, and a motion pushed earlier
	// would make the click land on the sentinel instead of the row.
	//
	// Menus only. In game this pointer is the real cursor -- it is drawn and it drives build
	// placement -- so that path is left alone. In a menu it is not drawn at all, so moving it is
	// invisible.
	if (GuichanOwnsInput()) {
		PushMouseMotion(-1, -1);
	}
}

/// IG-010 / HUD-029: in game the pointer is not parked after a touch (it is the real cursor), so a
/// HUD button stayed "under the cursor" after the finger left: its popup and its lit state stood
/// until the next touch. The original shows a tooltip only while the pointer is on the button.
/// Called once per frame after FlushPendingTouchClick. In a menu the parking above does the work.
static void PollTouchHoverEnd()
{
	if (TouchHoverEndFrames <= 0) {
		return;
	}
	if (TouchActive) {
		TouchHoverEndFrames = 0; // a finger is down again: its own motion sets the hover
		return;
	}
	if (--TouchHoverEndFrames == 0 && !GuichanOwnsInput() && GameRunning
	    && Editor.Running == EditorNotRunning) {
		UIClearButtonHover();
	}
}
#endif // __ANDROID__

/**
**  Is the touch pointer parked, i.e. is no finger on the screen since the last tap in a menu?
**  A parked pointer is nowhere: its reported position must not be taken for a mouse resting
**  at the screen corner. Always false where there is no touch input.
*/
bool IsTouchPointerParked()
{
#ifdef __ANDROID__
	return TouchPointerParked;
#else
	return false;
#endif
}

/**
**  Debug bridge, verb `touch`: start a finger gesture script.
**
**  Steps, separated by blanks: `d X Y` finger down, `m X Y` move, `u` up, `w MS` wait;
**  `d2 X Y`, `m2 X Y`, `u2` are the same for a second finger. X, Y are logical framebuffer
**  pixels. Example, a double tap: `d 1500 800 w 60 u w 120 d 1510 806 w 60 u`.
**
**  @param script  the steps.
**  @param error   set when false is returned.
**
**  @return  true if the script was taken (it plays over the next frames).
*/
bool DebugBridge_TouchScript(const std::string &script, std::string &error)
{
#ifdef __ANDROID__
	if (!TouchScript.empty()) {
		error = "a touch script is still playing";
		return false;
	}
	std::vector<TouchScriptStep> steps;
	std::istringstream in(script);
	std::string op;
	bool down[2] = {false, false};
	while (in >> op) {
		TouchScriptStep step = {op[0], 1, 0, 0, 0};
		if (op == "d2" || op == "m2" || op == "u2") {
			step.finger = 2;
		} else if (op != "d" && op != "m" && op != "u" && op != "w") {
			error = "unknown step '" + op + "'";
			return false;
		}
		const int f = step.finger - 1;
		if (step.op == 'w') {
			long ms = -1;
			if (!(in >> ms) || ms < 0 || ms > 10000) {
				error = "w needs 0..10000 ms";
				return false;
			}
			step.ms = static_cast<Uint32>(ms);
		} else if (step.op == 'u') {
			if (!down[f]) {
				error = "u without d";
				return false;
			}
			down[f] = false;
		} else {
			if (!(in >> step.x >> step.y) || step.x < 0 || step.y < 0
				|| step.x >= Video.Width || step.y >= Video.Height) {
				error = op + " needs X Y inside the screen";
				return false;
			}
			if ((step.op == 'd') == down[f]) {
				error = (step.op == 'd') ? "d while down" : "m without d";
				return false;
			}
			down[f] = true;
		}
		steps.push_back(step);
		if (steps.size() > 256) {
			error = "more than 256 steps";
			return false;
		}
	}
	if (steps.empty() || down[0] || down[1]) {
		error = steps.empty() ? "empty script" : "a finger is left down";
		return false;
	}
	TouchScript          = steps;
	TouchScriptPos       = 0;
	TouchScriptWaitStart = SDL_GetTicks();
	TouchScriptWaitMs    = 0;
	return true;
#else
	(void)script;
	error = "no touch layer in this build";
	return false;
#endif
}

/**
**  Debug bridge, verb `touchinfo`: the touch layer's state and constants as JSON members
**  (no braces), empty where there is no touch layer.
*/
std::string DebugBridge_TouchState()
{
#ifdef __ANDROID__
	std::ostringstream o;
	o << "\"active\":" << (TouchActive ? "true" : "false")
	  << ",\"fingers\":" << TouchFingerCount
	  << ",\"twoFinger\":" << (TwoFingerActive ? "true" : "false")
	  << ",\"dragging\":" << (TouchDragging ? "true" : "false")
	  << ",\"longFired\":" << (TouchLongFired ? "true" : "false")
	  << ",\"pair\":" << (TouchPair ? "true" : "false")
	  << ",\"addDrag\":" << (TouchAddDrag ? "true" : "false")
	  << ",\"chain\":" << (TouchChain ? "true" : "false")
	  << ",\"stripSlot\":" << TouchStripSlot
	  << ",\"tapValid\":" << (TouchTapValid ? "true" : "false")
	  << ",\"scriptStepsLeft\":" << (TouchScript.size() - TouchScriptPos)
	  << ",\"longPressMs\":" << TOUCH_LONGPRESS_MS
	  << ",\"dragPx\":" << TOUCH_DRAG_PX
	  << ",\"pairPx\":" << TOUCH_PAIR_PX
	  << ",\"pairMs\":" << DoubleClickDelay
	  << ",\"waypointMs\":" << TOUCH_WAYPOINT_MS
	  << ",\"waypointMinPx\":" << TOUCH_WAYPOINT_MIN_PX;
	return o.str();
#else
	return std::string();
#endif
}

static void SdlDoEvent(const EventCallback &callbacks, SDL_Event &event)
{
	unsigned int keysym = 0;

	LastSdlEventTicks = SDL_GetTicks();

	switch (event.type) {
#ifdef __ANDROID__
		case SDL_FINGERDOWN:
		case SDL_FINGERMOTION:
		case SDL_FINGERUP:
			HandleFingerEvent(event);
			break;
#endif

		case SDL_MOUSEBUTTONDOWN:
			event.button.y = static_cast<int>(std::floor(event.button.y / Video.VerticalPixelSize + 0.5));
			InputMouseButtonPress(callbacks, SDL_GetTicks(), event.button.button);
			break;

		case SDL_MOUSEBUTTONUP:
			event.button.y = static_cast<int>(std::floor(event.button.y / Video.VerticalPixelSize + 0.5));
			InputMouseButtonRelease(callbacks, SDL_GetTicks(), event.button.button);
			break;

		case SDL_MOUSEMOTION:
			event.motion.y = static_cast<int>(std::floor(event.button.y / Video.VerticalPixelSize + 0.5));
			InputMouseMove(callbacks, SDL_GetTicks(), event.motion.x, event.motion.y);
			break;

		case SDL_MOUSEWHEEL:
			{   // similar to Squeak, we fabricate Ctrl+Alt+PageUp/Down for wheel events
				SDL_Keycode key = event.wheel.y > 0 ? SDLK_PAGEUP : SDLK_PAGEDOWN;
				SDL_Event event;
				SDL_zero(event);
				event.type = SDL_KEYDOWN;
				event.key.keysym.sym = SDLK_LCTRL;
				SDL_PushEvent(&event);
				SDL_zero(event);
				event.type = SDL_KEYDOWN;
				event.key.keysym.sym = SDLK_LALT;
				SDL_PushEvent(&event);
				SDL_zero(event);
				event.type = SDL_KEYDOWN;
				event.key.keysym.sym = key;
				SDL_PushEvent(&event);
				SDL_zero(event);
				event.type = SDL_KEYUP;
				event.key.keysym.sym = key;
				SDL_PushEvent(&event);
				SDL_zero(event);
				event.type = SDL_KEYUP;
				event.key.keysym.sym = SDLK_LALT;
				SDL_PushEvent(&event);
				SDL_zero(event);
				event.type = SDL_KEYUP;
				event.key.keysym.sym = SDLK_LCTRL;
				SDL_PushEvent(&event);
			}
			break;

		case SDL_WINDOWEVENT:
			switch (event.window.event) {
				case SDL_WINDOWEVENT_SIZE_CHANGED:
				SizeChangeCounter++;
				break;

				case SDL_WINDOWEVENT_ENTER:
				case SDL_WINDOWEVENT_LEAVE:
				{
					static bool InMainWindow = true;

					if (InMainWindow && (event.window.event == SDL_WINDOWEVENT_LEAVE)) {
						InputMouseExit(callbacks, SDL_GetTicks());
					}
					InMainWindow = (event.window.event == SDL_WINDOWEVENT_ENTER);
				}
				break;

				case SDL_WINDOWEVENT_FOCUS_GAINED:
				case SDL_WINDOWEVENT_FOCUS_LOST:
				{
				if (!IsNetworkGame() && Preference.PauseOnLeave /*(SDL_GetWindowFlags(TheWindow) & SDL_WINDOW_INPUT_FOCUS)*/) {
					static bool DoTogglePause = false;

					if (IsSDLWindowVisible && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
						IsSDLWindowVisible = false;
						if (!GamePaused) {
							DoTogglePause = !GamePaused;
							GamePaused = true;
						}
					} else if (!IsSDLWindowVisible && (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)) {
						IsSDLWindowVisible = true;
						if (GamePaused && DoTogglePause) {
							DoTogglePause = false;
							GamePaused = false;
						}
					}
				}
				}
				break;
			}
			break;

		case SDL_TEXTINPUT:
		{
			char *text = event.text.text;
			if (isTextInput((uint8_t) text[0])) {
				// we only accept US-ascii chars for now
				char lastKey = text[0];
				InputKeyButtonPress(callbacks, SDL_GetTicks(), lastKey, lastKey);
				// fabricate a keyup event for later
				SDL_Event event;
				SDL_zero(event);
				event.type = SDL_CUSTOM_KEY_UP;
				event.user.code = lastKey;
				SDL_PeepEvents(&event, 1, SDL_ADDEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
			}
			break;
		}

		case SDL_KEYDOWN:
			keysym = event.key.keysym.sym;
			if (!isTextInput(keysym)) {
				// only report non-printing keys here, the characters will be reported with the textinput event
				InputKeyButtonPress(callbacks, SDL_GetTicks(), keysym, keysym < 128 ? keysym : 0);
			}
			break;

		case SDL_KEYUP:
			keysym = event.key.keysym.sym;
			if (!isTextInput(keysym)) {
				// only report non-printing keys here, the characters will be reported with the textinput event
				InputKeyButtonRelease(callbacks, SDL_GetTicks(), keysym, keysym < 128 ? keysym : 0);
			}
			break;

		case SDL_QUIT:
			Exit(0);
			break;

		default:
			if (event.type == SDL_SOUND_FINISHED) {
				HandleSoundEvent(event);
			} else if (event.type == SDL_CUSTOM_KEY_UP) {
				char key = static_cast<char>(event.user.code);
				InputKeyButtonRelease(callbacks, SDL_GetTicks(), key, key);
			}
			break;
	}

	if (&callbacks == GetCallbacks()) {
		handleInput(&event);
	}
}

/**
**  Set the current callbacks
*/
void SetCallbacks(const EventCallback *callbacks)
{
	Callbacks = callbacks;
}

/**
**  Get the current callbacks
*/
const EventCallback *GetCallbacks()
{
	return Callbacks;
}

/**
**  Wait for interactive input event for one frame.
**
**  Handles system events, joystick, keyboard, mouse.
**  Handles the network messages.
**  Handles the sound queue.
**
**  All events available are fetched. Sound and network only if available.
**  Returns if the time for one frame is over.
*/
void WaitEventsOneFrame()
{
	if (dummyRenderer) {
		return;
	}

	// Observe-only debug bridge: cheap throttled poll of the wdbg/ command channel.
	DebugBridge_Poll();
#ifdef __ANDROID__
	PollTouchScript(); // a gesture script started by the bridge; idle otherwise
#endif

	Uint32 ticks = SDL_GetTicks();
	if (ticks > NextFrameTicks) { // We are too slow :(
		++SlowFrameCounter;
	}

	InputMouseTimeout(*GetCallbacks(), ticks);
	InputKeyTimeout(*GetCallbacks(), ticks);
	CursorAnimate(ticks);
	int interrupts = Parameters::Instance.benchmark;

	for (;;) {
		// Time of frame over? This makes the CPU happy. :(
		ticks = SDL_GetTicks();
		if (!interrupts && ticks < NextFrameTicks) {
			SDL_Delay(NextFrameTicks - ticks);
			ticks = SDL_GetTicks();
		}
		while (ticks >= (unsigned long)(NextFrameTicks)) {
			++interrupts;
			NextFrameTicks += FrameTicks;
		}

		SDL_Event event[1];
		const int i = SDL_PollEvent(event);
		if (i) { // Handle SDL event
			SdlDoEvent(*GetCallbacks(), *event);
		}

		// Network
		int s = 0;
		if (IsNetworkGame()) {
			s = NetworkFildes.HasDataToRead(0);
			if (s > 0) {
				if (GetCallbacks()->NetworkEvent) {
					GetCallbacks()->NetworkEvent();
				}
			}
		}

		// Online session
		OnlineContextHandler->doOneStep();

		// No more input and time for frame over: return
		if (!i && s <= 0 && interrupts) {
			break;
		}
	}
	handleInput(nullptr);
#ifdef __ANDROID__
	// Fire a hold order mid-hold, not on release. AFTER this frame's events: polled before
	// them, a quick tap whose lift was already waiting in the queue behind a slow frame
	// (longer than the hold threshold) was taken for a hold and became a right click --
	// the tapped button only lit up. With the lift handled first, a tap stays a tap.
	PollTouchLongPress();
	FlushPendingTouchClick();
	PollTouchHoverEnd();
#endif

	if (SkipGameCycle < 0) {
		SkipGameCycle += SkipCycles;
	} else {
		SkipGameCycle--;
	}
}

/**
**  Realize video memory.
*/

static Uint32 LastTick = 0;

static void RenderBenchmarkOverlay()
{
	int RefreshRate = GetRefreshRate();
	// show a bar representing fps, where the entire bar is the max refresh rate of attached displays
	Uint32 nextTick = SDL_GetTicks();
	Uint32 frameTime = nextTick - LastTick;
	int fps = std::min(RefreshRate, static_cast<int>(frameTime > 0 ? (1000.0 / frameTime) : 0));
	LastTick = nextTick;

	// draw the full bar
	SDL_SetRenderDrawColor(TheRenderer, 255, 0, 0, 255);
	SDL_Rect frame = { Video.Width - 10, 2, 8, RefreshRate };
	SDL_RenderDrawRect(TheRenderer, &frame);

	// draw the inner fps gage
	SDL_SetRenderDrawColor(TheRenderer, 0, 255, 0, 255);
	SDL_Rect bar = { Video.Width - 8, 2 + RefreshRate - fps, 4, fps };
	SDL_RenderFillRect(TheRenderer, &bar);

	SDL_SetRenderDrawColor(TheRenderer, 0, 0, 0, 255);
}

// GPU-pipeline hybrid frame loop (Phase 0).
//
// The old model let RealizeVideoMemory own the whole present: RenderClear -> RenderCopy(overlay)
// -> RenderPresent. That is incompatible with drawing GPU world layers (tiles/units) directly to
// the backbuffer during UpdateDisplay, because the RenderClear here would wipe them.
//
// New model splits it into three ordered stages within a single frame:
//   BeginFrame()  = target the backbuffer + clear it to opaque black. MUST run before any GPU
//                   world draw, i.e. at the top of UpdateDisplay / EditorUpdateDisplay.
//   <GPU world draws happen during UpdateDisplay's DrawMapArea>
//   RealizeVideoMemory() = upload TheScreen -> TheTexture and RenderCopy it as the translucent
//                   overlay ON TOP of the GPU layers (no clear, no present).
//   EndFrame()    = present.
//
// BeginFrame is idempotent per frame (frameBegun guard): callers that reach RealizeVideoMemory
// WITHOUT a preceding UpdateDisplay (title screen, some menu/UI paths) get a lazy clear from the
// BeginFrame() call inside RealizeVideoMemory instead. EndFrame always clears the guard so the
// next frame re-clears.
static bool frameBegun = false;

void BeginFrame()
{
	if (dummyRenderer || !TheRenderer) {
		return;
	}
	if (frameBegun) {
		return;
	}
	frameBegun = true;
	SDL_SetRenderTarget(TheRenderer, nullptr);
	SDL_SetRenderDrawColor(TheRenderer, 0, 0, 0, 255);

	// Clear the whole present target every frame. This is a single GPU clear (async, effectively
	// free) and it is REQUIRED now that the in-game CPU overlay is skipped: with the overlay gone,
	// the world + GPU HUD do not necessarily cover every pixel (letterbox, and any gap the HUD art
	// does not paint), so without a full clear stale content from earlier frames lingers there and
	// re-renders as a ghost. (The earlier letterbox-only optimisation assumed the overlay repainted
	// the whole frame, which is no longer true.)
	SDL_RenderClear(TheRenderer);
}

void EndFrame()
{
	if (dummyRenderer || !TheRenderer) {
		return;
	}
	// WargusHD: don't present into the window surface while backgrounded/unfocused. On
	// Android the surface can be mid-teardown then, which races HWUI and trips a FORTIFY
	// abort (pthread_mutex_lock on a destroyed mutex). The next visible frame presents.
	if (IsSDLWindowVisible) {
		FrameProfBegin(FP_PRESENT);
		SDL_RenderPresent(TheRenderer);
		FrameProfEnd(FP_PRESENT);
	}
	frameBegun = false;
}

/**
**  The game behind an in-game window, in greys.
**
**  The original shows the whole game -- map and HUD -- in greys while one of its in-game
**  windows is up, and does not darken it: fitted on its own captures (the same pixels with
**  and without the Game Menu), grey = 0.2126 R + 0.7152 G + 0.0722 B at factor 1.00.
**
**  The game is drawn by the GPU straight into the backbuffer and there is no shader pass to
**  hang a filter on, so the frame is taken once, when the window comes up (the game stands
**  still under a window), turned to greys on the CPU and kept as a texture; while the window
**  stays up that texture is laid over what the game draws, under the widgets.
**
**  Paused by the header button the original shows the same greys with no window up (captures
**  o33_h1_paused, o49_o1_paused: map, minimap, HUD and resource bar all grey). There the game
**  stands but the HUD is in use -- the caption changes, units can be selected, the map can be
**  scrolled -- so a kept frame would go stale: with `live` the frame is taken again.
**
**  Taking the frame is a full-frame GL readback, a CPU pass and a full-frame texture upload
**  (2960x1848: 20.9 MB each way), far more than a frame's time if done every frame. The game
**  stands while paused, so the picture changes only through input or a script: with `live` the
**  frame is read again while SDL events arrive and for GreyLiveSettleMs after the last one, and
**  otherwise once per GreyLiveIdleMs. Both numbers are frame-rate guards of this port, not
**  values of the original; they have NOT been tuned on a device.
**
**  @param menuUp  true while the greys are wanted; false drops the kept frame.
**  @param live    true: keep the kept frame up to date (no window: the HUD is in use).
*/
static constexpr Uint32 GreyLiveSettleMs = 500;
static constexpr Uint32 GreyLiveIdleMs = 250;

void GreyFrameUnderMenu(bool menuUp, bool live)
{
	static SDL_Texture *grey = nullptr;
	static int greyW = 0, greyH = 0;
	static std::vector<uint8_t> px;   // kept between calls: a live frame is read again and again
	static Uint32 lastRead = 0;
	static bool lastLive = false;
	if (dummyRenderer || !TheRenderer) {
		return;
	}
	if (!menuUp) {
		if (grey) {
			SDL_DestroyTexture(grey);
			grey = nullptr;
		}
		px.clear();
		px.shrink_to_fit();
		lastLive = false;
		return;
	}
	// Nothing is presented while the window is not shown (EndFrame), and its surface may be going
	// away: no readback then. The pause set by leaving the application (PauseOnLeave) lands here.
	if (!IsSDLWindowVisible) {
		return;
	}
	const Uint32 now = SDL_GetTicks();
	const bool refresh = live && (now - LastSdlEventTicks < GreyLiveSettleMs || now - lastRead >= GreyLiveIdleMs);
	// A window that comes up over a paused game changes the HUD once more (the dim "Resume"
	// caption): the frame kept from the live phase is taken again on the change of mode.
	const bool modeChanged = (live != lastLive);
	lastLive = live;
	if (!grey || refresh || modeChanged) {
		SDL_Rect vp;
		float sx = 1.f, sy = 1.f;
		SDL_RenderGetViewport(TheRenderer, &vp);
		SDL_RenderGetScale(TheRenderer, &sx, &sy);
		const int w = static_cast<int>(vp.w * sx + 0.5f);
		const int h = static_cast<int>(vp.h * sy + 0.5f);
		if (w <= 0 || h <= 0) {
			return;
		}
		if (grey && (w != greyW || h != greyH)) {
			SDL_DestroyTexture(grey);
			grey = nullptr;
		}
		px.resize(static_cast<size_t>(w) * h * 4);
		// The rectangle is given, never left to the renderer: with none it reads its own viewport,
		// and when that is larger than w x h here (seen once as a crash in memcpy under
		// SDL_ConvertPixels at a campaign mission's start) it writes past this buffer. A given
		// rectangle is only ever cut down to the viewport.
		const SDL_Rect readRect = {0, 0, w, h};
		if (SDL_RenderReadPixels(TheRenderer, &readRect, SDL_PIXELFORMAT_ABGR8888, px.data(), w * 4) != 0) {
			return;
		}
		lastRead = now;
		for (size_t i = 0; i < px.size(); i += 4) {
			// bytes are R, G, B, A in this format
			const unsigned v = (px[i] * 13933u + px[i + 1] * 46871u + px[i + 2] * 4732u + 32768u) >> 16;
			px[i] = px[i + 1] = px[i + 2] = static_cast<uint8_t>(v);
			px[i + 3] = 255;
		}
		if (!grey) {
			grey = SDL_CreateTexture(TheRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
			if (!grey) {
				return;
			}
			greyW = w;
			greyH = h;
			SDL_SetTextureBlendMode(grey, SDL_BLENDMODE_NONE);
		}
		SDL_UpdateTexture(grey, nullptr, px.data(), w * 4);
	}
	SDL_RenderCopy(TheRenderer, grey, nullptr, nullptr);
}

void RealizeVideoMemory()
{
	++FrameCounter;
	if (dummyRenderer) {
		return;
	}
	if (Preference.FrameSkip && (FrameCounter & Preference.FrameSkip)) {
		OverlayDirtyPrevFrame = OverlayDirty;
		return;
	}
	// Lazy clear for present paths that never went through UpdateDisplay (title/menus). When
	// UpdateDisplay already ran, this is a no-op and the GPU world layers drawn since are kept.
	BeginFrame();
	if (NumRects) {
		// In-game with the HUD composited entirely on the GPU backbuffer, the overlay (TheScreen) is
		// empty (OverlayDirty stayed false through UpdateDisplay). The SDL_UpdateTexture upload and
		// the RenderCopy of that empty overlay are then pure waste, so skip both. Never skip when a
		// shader pass consumes the overlay, in the editor, or off the game path; any menu/tooltip/
		// pie/cursor/paletted-fallback marks OverlayDirty via the instrumented CPU blits and uploads.
		const bool skipOverlay = GameRunning && Editor.Running != EditorEditing
		                         && !IsShaderActive() && !OverlayDirty;
		if (!skipOverlay) {
			//SDL_UpdateWindowSurfaceRects(TheWindow, Rects, NumRects);
			SDL_UpdateTexture(TheTexture, nullptr, TheScreen->pixels, TheScreen->pitch);
			if (!RenderWithShader(TheRenderer, TheWindow, TheTexture)) {
				// No RenderClear here anymore: BeginFrame already cleared the backbuffer and the
				// GPU world layers were drawn on top of that clear. This copy blends the CPU
				// overlay (TheScreen, now alpha-capable) over them.
				//for (int i = 0; i < NumRects; i++)
				//    SDL_UpdateTexture(TheTexture, &Rects[i], TheScreen->pixels, TheScreen->pitch);
				SDL_RenderCopy(TheRenderer, TheTexture, nullptr, nullptr);
			}
		}
		if (Parameters::Instance.benchmark) {
			RenderBenchmarkOverlay();
		}
		NumRects = 0;
	}
	// This frame's overlay dirtiness is now fully known; hand it to next frame's clear decision.
	OverlayDirtyPrevFrame = OverlayDirty;
	EndFrame();
	LoadingFramePresented();
	if (!Preference.HardwareCursor) {
		HideCursor();
	}
}

/**
**  Lock the screen for write access.
*/
void SdlLockScreen()
{
	if (SDL_MUSTLOCK(TheScreen)) {
		SDL_LockSurface(TheScreen);
	}
}

/**
**  Unlock the screen for write access.
*/
void SdlUnlockScreen()
{
	if (SDL_MUSTLOCK(TheScreen)) {
		SDL_UnlockSurface(TheScreen);
	}
}

/**
**  Convert a SDLKey to a string
*/
const char *SdlKey2Str(int key)
{
	return Key2Str[key].c_str();
}

/**
**  Convert a string to SDLKey
*/
int Str2SdlKey(const char *str)
{
	InitKey2Str();

	for (auto &[sdlkey, s] : Key2Str) {
		if (!strcasecmp(str, s.c_str())) {
			return sdlkey;
		}
	}
	for (auto &[s, sdlkey] : Str2Key) {
		if (!strcasecmp(str, s.c_str())) {
			return sdlkey;
		}
	}
	return 0;
}

/**
**  Check if the mouse is grabbed
*/
bool SdlGetGrabMouse()
{
	return SDL_GetWindowGrab(TheWindow);
}

/**
**  Toggle grab mouse.
**
**  @param mode  Wanted mode, 1 grab, -1 not grab, 0 toggle.
*/
void ToggleGrabMouse(int mode)
{
	bool grabbed = SdlGetGrabMouse();

	if (mode <= 0 && grabbed) {
		SDL_SetWindowGrab(TheWindow, SDL_FALSE);
	} else if (mode >= 0 && !grabbed) {
		SDL_SetWindowGrab(TheWindow, SDL_TRUE);
	}
}

/**
**  Toggle full screen mode.
*/
void ToggleFullScreen()
{
	if (!TheWindow) { // don't bother if there's no surface.
		return;
	}
	const Uint32 flags = SDL_GetWindowFlags(TheWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP;
	SDL_SetWindowFullscreen(TheWindow, flags ^ SDL_WINDOW_FULLSCREEN_DESKTOP);

#ifdef USE_WIN32
	Invalidate(); // Update display
#endif
	Video.FullScreen = (flags ^ SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

//@}
