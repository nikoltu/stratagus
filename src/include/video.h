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
/**@name video.h - The video headerfile. */
//
//      (c) Copyright 1999-2011 by Lutz Sammer, Nehal Mistry, Jimmy Salmon and
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

#ifndef __VIDEO_H__
#define __VIDEO_H__

//@{

#include "color.h"
#include "filesystem.h"
#include "sdl2_helper.h"
#include "shaders.h"
#include "stratagus.h"
#include "vec2i.h"

#include <SDL.h>
#include <guisan.hpp>
#include <guisan/sdl/sdlimage.hpp>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

class CFont;

/// The SDL screen
extern SDL_Window *TheWindow;
extern SDL_Renderer *TheRenderer;
extern SDL_Surface *TheScreen;
extern SDL_Texture *TheTexture;

// GPU-pipeline (Phase 1): when true, CGraphic::DrawFrameClip[X] / DrawPlayerColorFrameClip[X]
// route to the GPU texture path (RenderCopy to the backbuffer) instead of a CPU blit into
// TheScreen. Set ONLY around the on-screen world draws (map tile pass + unit sprite/shadow
// draws) in the non-zoomed viewport render; false everywhere else (HUD, menus, icons, and the
// zoomed offscreen render), so those keep compositing on the CPU overlay. The individual draw
// functions additionally require mTexture != nullptr and surface == TheScreen before taking the
// GPU path, so paletted graphics and off-screen targets always fall back to the CPU path.
extern bool GpuWorldDrawActive;

// GPU-pipeline (fonts): when true, CFont::DrawChar RenderCopy's each glyph straight to the
// backbuffer (GPU) instead of blitting into the CPU overlay (TheScreen). Set ONLY around the
// in-game HUD text draws in UpdateDisplay; false everywhere else (menus/title/tooltips/guichan)
// so that text keeps the CPU path. Requires TheRenderer and a paletted font (an RGBA glyph
// texture is built per font+colour); non-paletted fonts fall back to the CPU blit.
extern bool GpuFontToBackbuffer;

// GPU-pipeline (HUD sprites): when true, CGraphic::DrawFrameClip[X] / DrawSubClip / DrawSubClipTrans
// route RGBA (mTexture) HUD blits straight to the backbuffer (RenderCopy) at 1:1 screen pixels
// instead of blitting into the CPU overlay (TheScreen). The minimap and unit portrait also take a
// GPU path when this is set. Set ONLY around the in-game HUD draws in UpdateDisplay (together with
// GpuFontToBackbuffer); false everywhere else so menus/title/editor keep the CPU path. Unlike the
// world path this never scales by GpuWorldDrawZoom (HUD is native pixels). Paletted graphics (no
// mTexture) fall back to the CPU blit.
extern bool GpuHudDrawActive;

// GPU-pipeline (Phase 1, zoomed direct render): the magnification the world GPU draws are drawn at.
// When the in-game viewport is zoomed (Android default 2.0), the tile/unit GPU path draws straight
// to the backbuffer at the ZOOMED on-screen size instead of going through the CPU offscreen +
// SoftStretch. The screen POSITION passed to the draw functions is already zoom-correct (tiles are
// placed at their rounded zoomed screen rect; unit sprite anchors come from MapToScreenPixelPos and
// their native centring offsets are scaled by this factor at the draw site), so here only the
// DESTINATION SIZE is multiplied by this zoom. Defaults to 1.0 => identical to the classic Zoom==1
// path (lround(W*1)==W), so nothing changes off the zoomed path.
extern float GpuWorldDrawZoom;

// World internal-resolution scale (0.25..1.0). When < 1.0 the zoomed in-game world (terrain, units,
// missiles, particles, fog) is rendered into a GPU target texture at this fraction of the viewport
// render size and upscaled (linear) onto the on-screen viewport, trading world sharpness for
// fillrate. HUD/fonts/cursor stay native. 1.0 (default) => the direct-to-backbuffer path, unchanged.
// Read once from <internal>/world_scale.txt at video init.
extern float WorldRenderScale;

#define RSHIFT  16
#define GSHIFT  8
#define BSHIFT  0
#define ASHIFT  24
#define RMASK   0x00ff0000
#define GMASK   0x0000ff00
#define BMASK   0x000000ff
#define AMASK   0xff000000

using pixelModifier = uint32_t(*)(const uint32_t, const uint32_t, const uint32_t); // type alias

/// Class for modifiers for custom pixel manipulations
class PixelModifier
{
public:
	/// This one returns srcRGB+A(modulated) only if srcA is present. Otherwise returns dstRGBA.
	/// Used to copy (without alpha modulating) those pixels which has alpha channel values
	static uint32_t CopyWithSrcAlphaKey(const uint32_t srcPixel, const uint32_t dstPixel, const uint32_t reqAlpha)
	{
		uint32_t srcAlpha = (srcPixel >> ASHIFT) & 0xFF;
		if (srcAlpha) {
			srcAlpha = (srcAlpha * reqAlpha) >> 8;
			return (srcPixel - (srcPixel & AMASK)) + (srcAlpha << ASHIFT);
		}
		return dstPixel;
	}
	/// Add more modifiers here
};

class CGraphic : public gcn::SDLImage
{
public:
	struct frame_pos_t {
		short int x = 0;
		short int y = 0;
	};

public:
	CGraphic() : gcn::SDLImage(nullptr, false) {}
	~CGraphic();
	// Draw
	void DrawClip(int x, int y,
				  SDL_Surface *surface = TheScreen) const;
	void DrawSub(int gx, int gy, int w, int h, int x, int y,
				 SDL_Surface *surface = TheScreen) const;

	void DrawSubCustomMod(int gx, int gy, int w, int h, int x, int y,
					      pixelModifier modifier,
						  const uint32_t param,
						  SDL_Surface *surface = TheScreen) const;

	void DrawSubClip(int gx, int gy, int w, int h, int x, int y,
					 SDL_Surface *surface = TheScreen) const;
	void DrawSubTrans(int gx, int gy, int w, int h, int x, int y,
					  unsigned char alpha,
					  SDL_Surface *surface = TheScreen) const;
	void DrawSubClipTrans(int gx, int gy, int w, int h, int x, int y,
						  unsigned char alpha,
						  SDL_Surface *surface = TheScreen) const;

	void DrawSubClipCustomMod(int gx, int gy, int w, int h, int x, int y,
							  pixelModifier modifier,
							  const uint32_t param,
							  SDL_Surface *surface = TheScreen) const;

	// Draw frame
	void DrawFrame(unsigned frame, int x, int y,
				   SDL_Surface *surface = TheScreen) const;
	void DrawFrameClip(unsigned frame, int x, int y,
					   SDL_Surface *surface = TheScreen) const;

	// GPU-pipeline (Phase 1): RenderCopy one frame straight to the backbuffer via mTexture,
	// bypassing the CPU software compositor. DrawFrameClipTexX draws the same frame mirrored
	// (SDL_RenderCopyEx horizontal flip) from the *same* base texture, so no separate flip
	// sheet/texture is needed. Only valid when mTexture != nullptr (RGBA graphics only).
	void DrawFrameClipTex(unsigned frame, int x, int y) const;
	void DrawFrameClipTexX(unsigned frame, int x, int y) const;
	// GPU-pipeline: RenderCopy one frame into an explicit destination rect. Used by the zoomed
	// map-tile path so adjacent tiles derive their size from consecutive rounded grid edges and
	// abut seamlessly at fractional zoom (no 1px gaps). Paletted graphics (no mTexture) fall back
	// to a native-size CPU blit at the rect origin, matching DrawFrameClip's fallback.
	void DrawFrameClipTexDst(unsigned frame, const SDL_Rect &dst) const;
	// GPU-pipeline: RenderCopy the WHOLE image (src == full texture) to the backbuffer at (x,y).
	// Used for static opaque HUD fillers. Only valid when mTexture != nullptr (RGBA graphics only).
	void DrawClipTex(int x, int y) const;
	// Crisp-zoom helper: blit a frame scaled to an explicit destination size (w,h) via
	// SDL_BlitScaled. Used ONLY by the crisp-zoom terrain path; it relies on the target
	// surface's clip_rect for clipping (not the engine CLIP_RECTANGLE), which the crisp
	// render path sets up. Not used at all when EnableCrispZoom is off.
	void DrawFrameClipScaled(unsigned frame, int x, int y, int w, int h,
							 SDL_Surface *surface = TheScreen) const;
	void DrawFrameTrans(unsigned frame, int x, int y, int alpha,
						SDL_Surface *surface = TheScreen) const;
	void DrawFrameClipTrans(unsigned frame, int x, int y, int alpha,
							SDL_Surface *surface = TheScreen) const;

	void DrawFrameClipCustomMod(unsigned frame, int x, int y,
								pixelModifier modifier,
								const uint32_t param,
								SDL_Surface *surface = TheScreen) const;

	// Draw frame flipped horizontally
	void DrawFrameX(unsigned frame, int x, int y,
					SDL_Surface *surface = TheScreen) const;
	void DrawFrameClipX(unsigned frame, int x, int y,
						SDL_Surface *surface = TheScreen) const;
	void DrawFrameTransX(unsigned frame, int x, int y, int alpha,
						 SDL_Surface *surface = TheScreen) const;
	void DrawFrameClipTransX(unsigned frame, int x, int y, int alpha,
							 SDL_Surface *surface = TheScreen) const;

	static std::shared_ptr<CGraphic> New(const std::string &file, const int w = 0, const int h = 0);
	static std::shared_ptr<CGraphic> ForceNew(const std::string &file, const int w = 0, const int h = 0);
	static std::shared_ptr<CGraphic> Get(const std::string &file);

	void Load(bool grayscale = false);
	void Flip();
	void Resize(int w, int h);
	void ResizeKeepRatio(int w, int h);
	void SetOriginalSize();
	void AppendFrames(const sequence_of_images &frames);
	bool TransparentPixel(int x, int y);
	void SetPaletteColor(int idx, int r, int g, int b);
	void MakeShadow(PixelPos offset);

	// minor programmatic editing features
	void OverlayGraphic(CGraphic *other, bool mask = false);

	bool IsLoaded(bool flipped = false) const { return mSurface != nullptr && (!flipped || SurfaceFlip != nullptr); }

	//guichan
	int getWidth() const override { return Width; }
	int getHeight() const override { return Height; }

	void setSurface(SDL_Surface *surface) { mSurface = surface; }

	int GetGraphicWidth() const { return mSurface ? mSurface->w : 0; }
	int GetGraphicHeight() const { return mSurface ? mSurface->h : 0; }

	int GetFrameCountPerRow() const { return mSurface ? mSurface->w / Width : 0; }

private:
	void ExpandFor(const uint16_t numOfFramesToAdd);

public:
	fs::path File;         /// Filename
	std::string HashFile;  /// Filename used in hash
	SDL_Surface *SurfaceFlip = nullptr; /// Flipped surface
	// GPU-pipeline (Phase 1): GPU copy of mSurface, uploaded once at the end of Load(). Created
	// ONLY for paletteless (RGBA) surfaces: 8-bit paletted graphics stay on the CPU path so that
	// palette colour-cycling (animated water/lava tiles) and the classic player-colour palette
	// remap keep working. nullptr => this graphic is never drawn through the GPU path.
	SDL_Texture *mTexture = nullptr;
	std::vector<frame_pos_t> frame_map;
	std::vector<frame_pos_t> frameFlip_map;
	void GenFramesMap();
	int Width = 0;         /// Width of a frame
	int Height = 0;        /// Height of a frame
	int NumFrames = 1;     /// Number of frames
	int OriginWidth = 0;   /// Origin graphic width
	int OriginHeight = 0;  /// Origin graphic height
	bool Resized = false;  /// Image has been resized

	friend class CFont;
};

class CPlayerColorGraphic : public CGraphic
{
public:
	CPlayerColorGraphic() = default;
	~CPlayerColorGraphic();

	// HD (RGBA, paletteless) player-colour support. The classic remap only works on 8-bit
	// palettes; for HD sprites we ship the base with a NEUTRAL grey team region + a matching
	// grey mask (<file>_team.png), and composite the player colour at runtime into a per-player
	// cached surface. TeamMask == nullptr means the classic palette path is used instead.
	SDL_Surface *TeamMask = nullptr;
	SDL_Surface *TeamMaskFlip = nullptr;
	std::map<int, SDL_Surface *> ColorVariants;
	std::map<int, SDL_Surface *> ColorVariantsFlip;
	void LoadTeamMask(const std::string &baseFile);
	SDL_Surface *GetColorVariant(int colorIndex, bool flip);

	// GPU-pipeline (Phase 1): GPU copy of the grey team-colour mask (TeamMask). The player colour
	// is applied at draw time via SDL_SetTextureColorMod on this mask + a BLEND RenderCopy over
	// the base frame, reproducing today's baked look with NO per-player CPU baking. nullptr =>
	// this graphic has no HD team mask, so player-colour draws fall back to the CPU path.
	SDL_Texture *mTextureTeam = nullptr;
	void DrawPlayerColorFrameClipTex(int colorIndex, unsigned frame, int x, int y, bool flip = false);

	void DrawPlayerColorFrameClipX(int colorIndex, unsigned frame, int x, int y,
								   SDL_Surface *surface = TheScreen);
	void DrawPlayerColorFrameClip(int colorIndex, unsigned frame, int x, int y,
								  SDL_Surface *surface = TheScreen);
	// Crisp-zoom helper (see CGraphic::DrawFrameClipScaled). Player-colour variant, scaled
	// to an explicit destination size. Only used on the crisp-zoom path.
	void DrawPlayerColorFrameClipScaled(int colorIndex, unsigned frame, int x, int y,
										int w, int h, SDL_Surface *surface = TheScreen);

	static std::shared_ptr<CPlayerColorGraphic> New(const std::string &file, int w = 0, int h = 0);
	static std::shared_ptr<CPlayerColorGraphic> ForceNew(const std::string &file, int w = 0, int h = 0);
	static std::shared_ptr<CPlayerColorGraphic> Get(const std::string &file);

	std::shared_ptr<CPlayerColorGraphic> Clone(bool grayscale = false) const;
};

#ifdef USE_MNG
#ifdef WIN32
#ifdef HAVE_STDDEF_H
#undef HAVE_STDDEF_H
#endif
#endif
#include <libmng.h>
#ifdef WIN32
#ifndef HAVE_STDDEF_H
#undef HAVE_STDDEF_H
#endif
#endif

class Mng : public gcn::SDLImage
{
public:
	Mng() : gcn::SDLImage(nullptr, true) {}
	~Mng();
	Mng(const Mng &) = delete;
	Mng &operator=(const Mng &) = delete;

	static std::shared_ptr<Mng> New(const std::string &name);

	bool Load();
	void Reset();
	void Draw(int x, int y);

	int getIteration() const { return iteration; }

	//guichan
	SDL_Surface* getSurface() const override;

	friend struct MngWrapper;

public:
	static uint32_t MaxFPS;

private:
	std::string name;
	FILE *fd = nullptr;
	mng_handle handle = nullptr;
	std::vector<unsigned char> buffer;
	unsigned long ticks = 0;
	int iteration = 0;
};
#else
/// empty class for lua scripts
class Mng : public gcn::Image
{
public:
	Mng() {}
	~Mng() {}

	static std::shared_ptr<Mng> New(const std::string &name) { return nullptr; }

	bool Load() { return false; }
	void Reset() {}
	void Draw(int x, int y) {}

	//guichan
	int getWidth() const override { return 0; }
	int getHeight() const override { return 0; }
	void free() override { throw "unimplemented"; }
	gcn::Color getPixel(int x, int y) override { throw "unimplemented"; }
	void putPixel(int x, int y, const gcn::Color &color) override { throw "unimplemented"; }
	void convertToDisplayFormat() override { throw "unimplemented"; }

	static inline uint32_t MaxFPS = 15;
};
#endif

/**
**  Event call back.
**
**  This is placed in the video part, because it depends on the video
**  hardware driver.
*/
struct EventCallback {

	/// Callback for mouse button press
	void (*ButtonPressed)(unsigned buttons) = nullptr;
	/// Callback for mouse button release
	void (*ButtonReleased)(unsigned buttons) = nullptr;
	/// Callback for mouse move
	void (*MouseMoved)(const PixelPos &screenPos) = nullptr;
	/// Callback for mouse exit of game window
	void (*MouseExit)() = nullptr;

	/// Callback for key press
	void (*KeyPressed)(unsigned keycode, unsigned keychar) = nullptr;
	/// Callback for key release
	void (*KeyReleased)(unsigned keycode, unsigned keychar) = nullptr;
	/// Callback for key repeated
	void (*KeyRepeated)(unsigned keycode, unsigned keychar) = nullptr;

	/// Callback for network event
	void (*NetworkEvent)() = nullptr;
};


class CVideo
{
public:
	CVideo() = default;

	void LockScreen();
	void UnlockScreen();

	void ClearScreen();
	bool ResizeScreen(int width, int height);

	void DrawPixelClip(Uint32 color, int x, int y);
	void DrawTransPixelClip(Uint32 color, int x, int y, unsigned char alpha);

	void DrawVLine(Uint32 color, int x, int y, int height);
	void DrawTransVLine(Uint32 color, int x, int y, int height, unsigned char alpha);
	void DrawVLineClip(Uint32 color, int x, int y, int height);
	void DrawTransVLineClip(Uint32 color, int x, int y, int height, unsigned char alpha);

	void DrawHLine(Uint32 color, int x, int y, int width);
	void DrawTransHLine(Uint32 color, int x, int y, int width, unsigned char alpha);
	void DrawHLineClip(Uint32 color, int x, int y, int width);
	void DrawTransHLineClip(Uint32 color, int x, int y, int width, unsigned char alpha);

	void DrawLine(Uint32 color, int sx, int sy, int dx, int dy);
	void DrawTransLine(Uint32 color, int sx, int sy, int dx, int dy, unsigned char alpha);
	void DrawLineClip(Uint32 color, const PixelPos &pos1, const PixelPos &pos2);
	void DrawTransLineClip(Uint32 color, int sx, int sy, int dx, int dy, unsigned char alpha);

	void DrawRectangle(Uint32 color, int x, int y, int w, int h);
	void DrawTransRectangle(Uint32 color, int x, int y, int w, int h, unsigned char alpha);
	void DrawRectangleClip(Uint32 color, int x, int y, int w, int h);
	void DrawTransRectangleClip(Uint32 color, int x, int y, int w, int h, unsigned char alpha);

	void FillRectangle(Uint32 color, int x, int y, int w, int h);
	void FillTransRectangle(Uint32 color, int x, int y, int w, int h, unsigned char alpha);
	void FillRectangleClip(Uint32 color, int x, int y, int w, int h);
	void FillTransRectangleClip(Uint32 color, int x, int y, int w, int h, unsigned char alpha);

	void DrawCircle(Uint32 color, int x, int y, int r);
	void DrawTransCircle(Uint32 color, int x, int y, int r, unsigned char alpha);
	void DrawCircleClip(Uint32 color, int x, int y, int r);
	void DrawTransCircleClip(Uint32 color, int x, int y, int r, unsigned char alpha);

	void DrawEllipseClip(Uint32 color, int x, int y, int rx, int ry);

	void FillCircle(Uint32 color, int x, int y, int radius);
	void FillTransCircle(Uint32 color, int x, int y, int radius, unsigned char alpha);
	void FillCircleClip(Uint32 color, const PixelPos &screenPos, int radius);
	void FillTransCircleClip(Uint32 color, int x, int y, int radius, unsigned char alpha);

	Uint32 MapRGB(SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b) { return SDL_MapRGB(f, r, g, b); }
	Uint32 MapRGB(SDL_PixelFormat *f, const CColor &color)
	{
		return MapRGB(f, color.R, color.G, color.B);
	}
	Uint32 MapRGBA(SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
	{
		return SDL_MapRGBA(f, r, g, b, a);
	}
	Uint32 MapRGBA(SDL_PixelFormat *f, const CColor &color)
	{
		return MapRGBA(f, color.R, color.G, color.B, color.A);
	}
	void GetRGB(Uint32 c, SDL_PixelFormat *f, Uint8 *r, Uint8 *g, Uint8 *b)
	{
		SDL_GetRGB(c, f, r, g, b);
	}
	void GetRGBA(Uint32 c, SDL_PixelFormat *f, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
	{
		SDL_GetRGBA(c, f, r, g, b, a);
	}

	int Width = 0;
	int Height = 0;
	int WindowWidth = 0;
	int WindowHeight = 0;
	double VerticalPixelSize = 1.;
	sdl2::CursorPtr blankCursor;
	int Depth = 0;
	bool FullScreen = false;
};

extern CVideo Video;

/**
**  Target CyclesPerSecond that are simulated. The default is CYCLES_PER_SECOND.
**  @see CYCLES_PER_SECOND
*/
extern int CyclesPerSecond;

extern double SkipCycles;

/// Fullscreen or windowed set from commandline.
extern char VideoForceFullScreen;

/// Next frame ticks
extern double NextFrameTicks;

/// Target refresh rate for renderer
extern int RefreshRate;

/// Counts frames
extern unsigned long FrameCounter;

/// Counts quantity of slow frames
extern unsigned long SlowFrameCounter;

/// Initialize Pixels[] for all players.
/// (bring Players[] in sync with Pixels[])
extern void SetPlayersPalette();

/// register lua function
extern void VideoCclRegister();

/// initialize the image loaders part
extern void InitImageLoaders();

/// de-initialize the image loaders
extern void DeInitImageLoaders();

/// initialize the video part
extern void InitVideo();

/// Initializes video synchronization.
extern void SetVideoSync();

/// Init line draw
extern void InitLineDraw();

/// Simply invalidates whole window or screen.
extern void Invalidate();

/// Invalidates selected area on window or screen. Use for accurate
/// redrawing. in so
extern void InvalidateArea(int x, int y, int w, int h);

/// Set clipping for nearly all vector primitives. Functions which support
/// clipping will be marked Clip. Set the system-wide clipping rectangle.
extern void SetClipping(int left, int top, int right, int bottom);

/// GPU-pipeline hybrid frame loop (Phase 0). BeginFrame targets+clears the backbuffer and MUST
/// run before any GPU world draw (called at the top of UpdateDisplay/EditorUpdateDisplay, and
/// lazily from RealizeVideoMemory for non-UpdateDisplay present paths). EndFrame presents.
extern void BeginFrame();
extern void EndFrame();

/// Realize video memory.
extern void RealizeVideoMemory();

/// Save a screenshot to a PNG file
extern void SaveScreenshotPNG(const char *name);

/// Save a screenshot to a PNG file
extern void SaveMapPNG(const char *name);

/// Set the current callbacks
extern void SetCallbacks(const EventCallback *callbacks);
/// Get the current callbacks
extern const EventCallback *GetCallbacks();

/// Process all system events. Returns if the time for a frame is over
extern void WaitEventsOneFrame();

/// Toggle full screen mode
extern void ToggleFullScreen();

/// Push current clipping.
extern void PushClipping();

/// Pop current clipping.
extern void PopClipping();

/// Returns the ticks in ms since start
extern unsigned long GetTicks();

/// Convert a SDLKey to a string
extern const char *SdlKey2Str(int key);

/// Check if the mouse is grabbed
extern bool SdlGetGrabMouse();
/// Toggle mouse grab mode
extern void ToggleGrabMouse(int mode);

extern EventCallback GameCallbacks;   /// Game callbacks
extern EventCallback EditorCallbacks; /// Editor callbacks

/// Runtime toggle for the experimental "crisp-at-zoom" (A2) map render path.
/// DEFAULT false: when off, the zoom render path is byte-for-byte the classic one
/// (render into a smaller offscreen, then bilinear upscale). When on AND a viewport is
/// zoomed, terrain is rendered at full on-screen size from a higher-res tile graphic so
/// it stays crisp. Toggleable from Lua via SetCrispZoom(bool) for A/B testing.
extern bool EnableCrispZoom;
/// Per-blit up-scale factor consulted by the CGraphic frame-blit fast paths. It is 1.0f
/// everywhere except briefly during the crisp-zoom world render, where the crisp path sets
/// it to the viewport Zoom so unit/missile/particle sprites are scaled up to on-screen size
/// via SDL_BlitScaled. At 1.0f every blit takes exactly the classic code path (no behaviour
/// change), which is what keeps EnableCrispZoom==false byte-identical.
extern float CrispZoomDrawScale;

extern Uint32 ColorBlack;
extern Uint32 ColorDarkGreen;
extern Uint32 ColorLightBlue;
extern Uint32 ColorBlue;
extern Uint32 ColorOrange;
extern Uint32 ColorWhite;
extern Uint32 ColorLightGray;
extern Uint32 ColorGray;
extern Uint32 ColorDarkGray;
extern Uint32 ColorRed;
extern Uint32 ColorGreen;
extern Uint32 ColorYellow;

inline Uint32 IndexToColor(unsigned int index) {
    // FIXME: this only works after video was initialized, so we do it dynamically
    static const Uint32 ColorValues[] = {ColorRed, ColorYellow, ColorGreen, ColorLightGray,
                                         ColorGray, ColorDarkGray, ColorWhite, ColorOrange,
                                         ColorLightBlue, ColorBlue, ColorDarkGreen, ColorBlack};
    return ColorValues[index];
}

static const char *ColorNames[] = {"red", "yellow", "green", "light-gray",
                                   "gray", "dark-gray", "white", "orange",
                                   "light-blue", "blue", "dark-green", "black", nullptr};

inline int GetColorIndexByName(std::string_view colorName) {
	int i = 0;
	while (ColorNames[i] != nullptr) {
		if (colorName == ColorNames[i]) {
			return i;
		}
		++i;
	}
	DebugPrint("Unknown color %s", colorName.data());
	return -1;
}

extern void FreeGraphics();

//
//  Color Cycling stuff
//

extern void VideoPaletteListAdd(SDL_Surface *surface);
extern void VideoPaletteListRemove(SDL_Surface *surface);
extern void ClearAllColorCyclingRange();
extern void AddColorCyclingRange(unsigned int begin, unsigned int end);
extern unsigned int SetColorCycleSpeed(unsigned int speed);
extern void SetColorCycleAll(bool value);
extern void RestoreColorCyclingSurface();

/// Does ColorCycling..
extern void ColorCycle();

/// Blit a surface into another with alpha blending
extern void BlitSurfaceAlphaBlending_32bpp(const SDL_Surface *srcSurface, const SDL_Rect *srcRect,
												 SDL_Surface *dstSurface, const SDL_Rect *dstRect, const bool enableMT = true);

//@}

#endif // !__VIDEO_H__
