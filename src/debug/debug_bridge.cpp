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
/**@name debug_bridge.cpp - In-engine debug bridge (bug catcher + state monitor). */
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

//@{

#include "stratagus.h"

#include "debug_bridge.h"
#include "frame_profiler.h"

#include "commands.h"
#include "cursor.h"
#include "filesystem.h"
#include "group_move.h"
#include "interface.h"
#include "map.h"
#include "net_message.h"
#include "network.h"
#include "parameters.h"
#include "player.h"
#include "script.h"
#include "settings.h"
#include "tile.h"
#include "ui.h"
#include "ui/popup.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"
#include "video.h"
#include "viewport.h"

#include <guisan.hpp>
#include <guisan/widgets/label.hpp>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>
#include <csignal>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <cstdint>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <unwind.h>   // _Unwind_Backtrace: works at Android API24 (backtrace() does not)
#include <dlfcn.h>    // dladdr: module + nearest symbol resolution, available on Android
#endif

#ifdef __ANDROID__
#include <SDL_system.h>
#include <android/log.h>
#define DBG_LOG_TAG "WargusHD"
#endif

// backtrace()/backtrace_symbols_fd() are only available at Android API >= 33 (this build targets
// API 24), so the crash handler uses <unwind.h> _Unwind_Backtrace + dladdr instead (both work at
// API 24) to resolve frames to module + offset + nearest symbol.

// The guichan top widget (current MenuScreen / game / editor container).
extern std::unique_ptr<gcn::Gui> Gui;

/*----------------------------------------------------------------------------
--  State
----------------------------------------------------------------------------*/

static bool DbgInited = false;
/// Whether the command file is read and the debug Lua calls exist. On Android only a build
/// marked debuggable asks for it (the game activity keeps the file "debug_bridge" in the app's files folder); elsewhere
/// the environment variable WARGUSHD_DEBUG_BRIDGE=1 does. Off until DebugBridge_Init has found one of them.
#ifdef WARGUSHD_DEBUG_COMMANDS
static bool DbgCommands = false;
#else
static constexpr bool DbgCommands = false; // a build without the command channel
#endif
static fs::path DbgDir;       /// <UserDirectory>/wdbg
static fs::path DbgCmdFile;   /// wdbg/cmd
static fs::path DbgOutFile;   /// wdbg/out.json
static fs::path DbgOutTmp;    /// wdbg/out.json.tmp
static fs::path DbgStatFile;  /// wdbg/status.json

// The crash handler must be async-signal-safe-ish, so it cannot touch fs::path
// (allocations). Cache the crash file path as a plain C string up front.
static char DbgCrashPath[4096] = {0};

// Watchdog heartbeat (see debug_bridge.h). File scope so the header extern binds here.
std::atomic<const char *> DbgCheckpoint{"init"};
std::atomic<unsigned long> DbgHeartbeat{0};

/*----------------------------------------------------------------------------
--  Frame profiler (see frame_profiler.h)
----------------------------------------------------------------------------*/

// Per-section accumulators, keyed by FrameProfSection. Fixed-size, allocation-free.
static Uint64 FpStart[FP_SECTION_COUNT] = {0};   // begin counter of the open span
static Uint64 FpTotal[FP_SECTION_COUNT] = {0};   // accumulated ticks this window
static long   FpFrames = 0;                      // rendered frames this window
static Uint64 FpFrameTotal = 0;                  // wall-clock ticks across frames
static Uint64 FpLastFrameEnd = 0;                // counter at previous EndFrame

void FrameProfBegin(FrameProfSection s)
{
	if ((unsigned)s < FP_SECTION_COUNT) {
		FpStart[s] = SDL_GetPerformanceCounter();
	}
}

void FrameProfEnd(FrameProfSection s)
{
	if ((unsigned)s < FP_SECTION_COUNT && FpStart[s]) {
		FpTotal[s] += SDL_GetPerformanceCounter() - FpStart[s];
		FpStart[s] = 0;
	}
}

void FrameProfEndFrame()
{
	const Uint64 now = SDL_GetPerformanceCounter();
	if (FpLastFrameEnd) {
		FpFrameTotal += now - FpLastFrameEnd;
	}
	FpLastFrameEnd = now;
	++FpFrames;
}

/*----------------------------------------------------------------------------
--  Tiny JSON string builder
----------------------------------------------------------------------------*/

namespace {

struct Json {
	std::string s;
	void raw(const char *t) { s += t; }
	void raw(const std::string &t) { s += t; }
	void ch(char c) { s += c; }

	void esc(const std::string &in) {
		s += '"';
		for (char c : in) {
			switch (c) {
				case '"':  s += "\\\""; break;
				case '\\': s += "\\\\"; break;
				case '\n': s += "\\n"; break;
				case '\r': s += "\\r"; break;
				case '\t': s += "\\t"; break;
				default:
					if ((unsigned char)c < 0x20) {
						char buf[8];
						snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
						s += buf;
					} else {
						s += c;
					}
			}
		}
		s += '"';
	}
	// "key":<literal>   (caller supplies formatted number/bool/object)
	void num(const char *key, long v) { key_(key); char b[32]; snprintf(b, sizeof(b), "%ld", v); s += b; }
	void dbl(const char *key, double v) { key_(key); char b[48]; snprintf(b, sizeof(b), "%g", v); s += b; }
	void boolean(const char *key, bool v) { key_(key); s += v ? "true" : "false"; }
	void str(const char *key, const std::string &v) { key_(key); esc(v); }
	void null(const char *key) { key_(key); s += "null"; }
	void key_(const char *key) { s += '"'; s += key; s += "\":"; }
	void comma() { s += ','; }
};

// The command channel (the command file, its commands, the status file and the debug Lua
// calls) is compiled only where WARGUSHD_DEBUG_COMMANDS is defined (CMake option of that name,
// OFF for a release build). Without it only the crash records and the watchdog remain.
#ifdef WARGUSHD_DEBUG_COMMANDS

void WriteFileAtomic(const std::string &payload)
{
	FILE *f = std::fopen(DbgOutTmp.string().c_str(), "wb");
	if (!f) {
		return;
	}
	std::fwrite(payload.data(), 1, payload.size(), f);
	std::fclose(f);
	// Rename over the target so a reader never sees a half-written file.
	std::error_code ec;
	fs::rename(DbgOutTmp, DbgOutFile, ec);
	if (ec) {
		// Fallback: some filesystems refuse cross-name rename; copy instead.
		fs::copy_file(DbgOutTmp, DbgOutFile, fs::copy_options::overwrite_existing, ec);
	}
}

/*----------------------------------------------------------------------------
--  Command implementations
----------------------------------------------------------------------------*/

void EmitUnitFields(Json &j, const CUnit &unit)
{
	const CUnitType *type = unit.Type;
	j.ch('{');
	j.num("id", (long)UnitNumber(unit));
	j.comma(); j.str("type", type ? type->Ident : std::string());
	j.comma(); j.num("player", unit.Player ? unit.Player->Index : -1);
	j.comma(); j.num("tileX", unit.tilePos.x);
	j.comma(); j.num("tileY", unit.tilePos.y);

	int screenX = 0, screenY = 0;
	bool haveScreen = false;
	if (UI.MouseViewport) {
		const PixelPos sp = UI.MouseViewport->MapToScreenPixelPos(unit.GetMapPixelPosTopLeft());
		screenX = sp.x;
		screenY = sp.y;
		haveScreen = true;
	}
	j.comma();
	if (haveScreen) { j.num("screenX", screenX); j.comma(); j.num("screenY", screenY); }
	else { j.null("screenX"); j.comma(); j.null("screenY"); }

	int pxW = 0, pxH = 0;
	if (type) { const PixelSize ps = type->GetPixelSize(); pxW = ps.x; pxH = ps.y; }
	j.comma(); j.num("pxW", pxW);
	j.comma(); j.num("pxH", pxH);
	j.comma(); j.num("frame", unit.Frame);
	// Player-colour diagnostic: the colour index the sprite tint path resolves to
	// (GameSettings.Presets[player].PlayerColor) and the RGB it maps to via PlayerColorsRGB[idx][0].
	// This is exactly the ColorMod the GPU team-mask pass applies; compare with the on-screen sprite.
	{
		const int pidx = unit.Player ? unit.Player->Index : -1;
		int cidx = -1;
		if (pidx >= 0 && pidx < PlayerMax) {
			cidx = GameSettings.Presets[pidx].PlayerColor;
		}
		j.comma(); j.num("colorIdx", cidx);
		if (cidx >= 0 && cidx < (int)PlayerColorsRGB.size() && !PlayerColorsRGB[cidx].empty()) {
			char cbuf[32];
			const CColor &c = PlayerColorsRGB[cidx][0];
			snprintf(cbuf, sizeof(cbuf), "%d,%d,%d", c.R, c.G, c.B);
			j.comma(); j.str("colorRGB", cbuf);
		}
	}
	if (type && !type->File.empty()) { j.comma(); j.str("file", type->File); }
	j.ch('}');
}

std::string CmdState()
{
	Json j;
	j.ch('{');

	// screen
	j.key_("screen"); j.ch('{');
	j.num("w", Video.Width); j.comma(); j.num("h", Video.Height);
	j.ch('}');

	// viewport
	j.comma(); j.key_("viewport");
	CViewport *vp = UI.MouseViewport ? UI.MouseViewport : UI.SelectedViewport;
	if (vp) {
		j.ch('{');
		j.dbl("zoom", vp->Zoom);
		j.comma(); j.num("mapX", vp->MapPos.x);
		j.comma(); j.num("mapY", vp->MapPos.y);
		j.comma(); j.num("tileSize", PixelTileSize.x);
		j.comma(); j.num("topLeftX", vp->GetTopLeftPos().x);
		j.comma(); j.num("topLeftY", vp->GetTopLeftPos().y);
		j.comma(); j.num("mapWidth", vp->MapWidth);
		j.comma(); j.num("mapHeight", vp->MapHeight);
		j.ch('}');
	} else {
		j.raw("null");
	}

	// cursor
	j.comma(); j.key_("cursor"); j.ch('{');
	j.num("x", CursorScreenPos.x);
	j.comma(); j.num("y", CursorScreenPos.y);
	bool onMap = UI.MouseViewport && UI.MouseViewport->IsInsideMapArea(CursorScreenPos);
	j.comma(); j.boolean("onMap", onMap);
	j.comma(); j.key_("building");
	if (CursorBuilding) {
		j.ch('{');
		j.str("type", CursorBuilding->Ident);
		if (UI.MouseViewport) {
			const Vec2i t = UI.MouseViewport->ScreenToTilePos(CursorScreenPos);
			j.comma(); j.num("tileX", t.x);
			j.comma(); j.num("tileY", t.y);
		}
		j.ch('}');
	} else {
		j.raw("null");
	}
	j.ch('}'); // cursor

	// selected
	j.comma(); j.key_("selected"); j.ch('[');
	for (size_t i = 0; i < Selected.size(); ++i) {
		if (i) j.comma();
		if (Selected[i]) EmitUnitFields(j, *Selected[i]);
		else j.raw("null");
	}
	j.ch(']');

	// unitCount + visible units
	static const std::vector<CUnit *> emptyUnits;
	const std::vector<CUnit *> &all = UnitManager ? UnitManager->GetUnits() : emptyUnits;
	j.comma(); j.num("unitCount", (long)all.size());

	j.comma(); j.key_("units"); j.ch('[');
	int emitted = 0;
	if (vp) {
		const int minX = vp->MapPos.x;
		const int minY = vp->MapPos.y;
		const int maxX = vp->MapPos.x + vp->MapWidth;
		const int maxY = vp->MapPos.y + vp->MapHeight;
		for (CUnit *u : all) {
			if (emitted >= 200) break;
			if (!u || u->Type == nullptr) continue;
			// only units whose tile falls inside the viewport's visible tile range
			if (u->tilePos.x < minX - 1 || u->tilePos.x > maxX + 1) continue;
			if (u->tilePos.y < minY - 1 || u->tilePos.y > maxY + 1) continue;
			if (emitted) j.comma();
			EmitUnitFields(j, *u);
			++emitted;
		}
	}
	j.ch(']');

	j.ch('}');
	return j.s;
}

void EmitWidgetTree(Json &j, gcn::Widget *w, int depth)
{
	j.ch('{');
	const char *cls = w ? typeid(*w).name() : "null";
	j.str("class", cls ? cls : "");
	if (w) {
		j.comma(); j.num("x", w->getX());
		j.comma(); j.num("y", w->getY());
		j.comma(); j.num("w", w->getWidth());
		j.comma(); j.num("h", w->getHeight());
		j.comma(); j.boolean("visible", w->isVisible());
		if (auto *lbl = dynamic_cast<gcn::Label *>(w)) {
			j.comma(); j.str("text", lbl->getCaption());
		}
		const gcn::Color &fg = w->getForegroundColor();
		char cbuf[48];
		snprintf(cbuf, sizeof(cbuf), "%d,%d,%d,%d", fg.r, fg.g, fg.b, fg.a);
		j.comma(); j.str("fg", cbuf);

		// Recurse into direct children of containers (one level of nesting cap via depth).
		if (auto *cont = dynamic_cast<gcn::Container *>(w)) {
			const std::list<gcn::Widget *> &kids = cont->getChildren();
			j.comma(); j.num("childCount", (long)kids.size());
			if (depth < 3) {
				j.comma(); j.key_("children"); j.ch('[');
				bool first = true;
				for (gcn::Widget *kid : kids) {
					if (!first) j.comma();
					first = false;
					EmitWidgetTree(j, kid, depth + 1);
				}
				j.ch(']');
			}
		}
	}
	j.ch('}');
}

std::string CmdUi()
{
	Json j;
	j.ch('{');
	gcn::Widget *top = Gui ? Gui->getTop() : nullptr;
	// Depth of the guichan top; guichan keeps a single active top widget (the menu stack
	// top is set as Gui->getTop()), so this is 1 when a menu/game screen is active, 0 otherwise.
	j.num("menuStackDepth", top ? 1 : 0);
	j.comma(); j.key_("top");
	if (top) {
		EmitWidgetTree(j, top, 0);
	} else {
		j.raw("null");
	}
	j.ch('}');
	return j.s;
}

std::string CmdAssets()
{
	Json j;
	j.ch('{');
	std::vector<DebugGraphicInfo> gfx;
	DebugBridge_CollectGraphics(gfx);
	j.num("count", (long)gfx.size());
	j.comma(); j.key_("graphics"); j.ch('[');
	int n = 0;
	for (const auto &g : gfx) {
		if (n >= 400) break;
		if (n) j.comma();
		j.ch('{');
		j.str("file", g.file);
		j.comma(); j.num("w", g.width);
		j.comma(); j.num("h", g.height);
		j.comma(); j.num("numFrames", g.numFrames);
		j.ch('}');
		++n;
	}
	j.ch(']');
	j.ch('}');
	return j.s;
}

std::string CmdColor(int x, int y)
{
	Json j;
	j.ch('{');
	j.num("x", x);
	j.comma(); j.num("y", y);
	// GPU-pipeline (Phase 1): the world (tiles/units) now lives on the GPU backbuffer, not in
	// TheScreen (which is only the translucent CPU overlay: HUD/fog/cursor, transparent over the
	// world). Sampling TheScreen->pixels here would miss the world entirely. Read the COMPOSITED
	// pixel back from the renderer instead. Caveat: after SDL_RenderPresent the backbuffer can be
	// undefined on some GLES drivers, so this is best-effort for the debug bridge; it is most
	// reliable when read within the same frame before present.
	if (!TheRenderer || x < 0 || y < 0) {
		j.comma(); j.null("r"); j.comma(); j.null("g"); j.comma(); j.null("b"); j.comma(); j.null("a");
		j.ch('}');
		return j.s;
	}
	uint32_t px = 0;
	SDL_Rect r1 = { x, y, 1, 1 };
	if (SDL_RenderReadPixels(TheRenderer, &r1, SDL_PIXELFORMAT_ARGB8888, &px, (int)sizeof(px)) != 0) {
		j.comma(); j.null("r"); j.comma(); j.null("g"); j.comma(); j.null("b"); j.comma(); j.null("a");
		j.comma(); j.str("err", SDL_GetError());
		j.ch('}');
		return j.s;
	}
	int r = (px & RMASK) >> RSHIFT;
	int g = (px & GMASK) >> GSHIFT;
	int b = (px & BMASK) >> BSHIFT;
	int a = (px & AMASK) >> ASHIFT;
	j.comma(); j.num("r", r);
	j.comma(); j.num("g", g);
	j.comma(); j.num("b", b);
	j.comma(); j.num("a", a);
	j.ch('}');
	return j.s;
}

// Report the per-frame profiler averages over the accumulated window, then RESET so each
// call reports a fresh window. avgFrameMs is measured wall-clock (present-to-present); each
// section value is total_ms/frames. present is a subset of realize (RenderPresent within it).
std::string CmdProf()
{
	const double freq = (double)SDL_GetPerformanceFrequency();
	const long frames = FpFrames;
	const double div = frames > 0 ? (double)frames : 1.0;
	const double toMsPerFrame = (freq > 0.0) ? (1000.0 / freq) / div : 0.0;

	Json j;
	j.ch('{');
	j.num("frames", frames);
	j.comma(); j.dbl("avgFrameMs", (double)FpFrameTotal * toMsPerFrame);
	j.comma(); j.key_("sections"); j.ch('{');
	static const char *names[FP_SECTION_COUNT] = {
		"logic", "minimap", "fog", "updatedisplay", "world", "hud", "realize", "present",
		"beginframe", "ovlclear", "invalidate",
		"hud_fillers", "hud_minimap", "hud_panels", "hud_guichan", "hud_cursor"
	};
	for (int i = 0; i < FP_SECTION_COUNT; ++i) {
		if (i) j.comma();
		j.dbl(names[i], (double)FpTotal[i] * toMsPerFrame);
	}
	j.ch('}');
	j.ch('}');

	// Reset the window.
	for (int i = 0; i < FP_SECTION_COUNT; ++i) { FpTotal[i] = 0; FpStart[i] = 0; }
	FpFrames = 0;
	FpFrameTotal = 0;
	FpLastFrameEnd = 0;
	return j.s;
}

/*----------------------------------------------------------------------------
--  Command injector (drive units via the engine's Send* command API)
----------------------------------------------------------------------------*/

// Split a command line into whitespace-separated tokens (verb included).
std::vector<std::string> Tokenize(const std::string &line)
{
	std::vector<std::string> out;
	const size_t n = line.size();
	size_t i = 0;
	while (i < n) {
		while (i < n && std::isspace((unsigned char)line[i])) ++i;
		const size_t start = i;
		while (i < n && !std::isspace((unsigned char)line[i])) ++i;
		if (i > start) out.push_back(line.substr(start, i - start));
	}
	return out;
}

// Safe unit-type lookup by ident (UnitTypeByIdent() would ExitFatal on a miss).
CUnitType *FindUnitTypeByIdent(const std::string &ident)
{
	for (CUnitType *t : getUnitTypes()) {
		if (t && t->Ident == ident) return t;
	}
	return nullptr;
}

// Safe unit lookup by slot id: only a unit that is in the game (not a released slot).
CUnit *FindUnitById(long id)
{
	if (!UnitManager) return nullptr;
	for (CUnit *u : UnitManager->GetUnits()) {
		if (u && u->Type && (long)UnitNumber(*u) == id) return u;
	}
	return nullptr;
}

// Is a game actually running (so ThisPlayer / SelectedViewport / Map are valid)?
bool InjectorGameReady()
{
	return GameRunning && ThisPlayer != nullptr && UI.SelectedViewport != nullptr;
}

// Emit {"ok":..,"msg":..,"selected":[ids],"unit":id}
std::string InjectorResult(bool ok, const std::string &msg, long unitId = -1)
{
	Json j;
	j.ch('{');
	j.boolean("ok", ok);
	j.comma(); j.str("msg", msg);
	j.comma(); j.key_("selected"); j.ch('[');
	for (size_t i = 0; i < Selected.size(); ++i) {
		if (i) j.comma();
		if (Selected[i]) j.raw(std::to_string((long)UnitNumber(*Selected[i])));
		else j.raw("null");
	}
	j.ch(']');
	j.comma(); j.num("unit", unitId);
	j.ch('}');
	return j.s;
}

std::string CmdSelect(const std::vector<std::string> &tok)
{
	if (!InjectorGameReady()) return InjectorResult(false, "no game");
	if (tok.size() < 2) return InjectorResult(false, "usage: select id|type|at ...");

	const std::string &sub = tok[1];

	if (sub == "id") {
		if (tok.size() < 3) return InjectorResult(false, "usage: select id <N>");
		const long id = std::strtol(tok[2].c_str(), nullptr, 10);
		CUnit *u = FindUnitById(id);
		if (!u) return InjectorResult(false, "unit id not found");
		SelectSingleUnit(*u);
		SelectionChanged();
		return InjectorResult(true, "selected unit by id", id);
	}

	if (sub == "type") {
		if (tok.size() < 3) return InjectorResult(false, "usage: select type <ident> [player <P>]");
		CUnitType *type = FindUnitTypeByIdent(tok[2]);
		if (!type) return InjectorResult(false, "unknown unit type: " + tok[2]);
		int wantPlayer = ThisPlayer->Index;
		if (tok.size() >= 5 && tok[3] == "player") {
			wantPlayer = (int)std::strtol(tok[4].c_str(), nullptr, 10);
		}
		UnSelectAll();
		int count = 0;
		for (CUnit *u : UnitManager->GetUnits()) {
			if (!u || u->Type != type || !u->Player) continue;
			if (u->Player->Index != wantPlayer) continue;
			if (u->Removed || !u->IsAlive()) continue;
			if (SelectUnit(*u)) ++count;
		}
		SelectionChanged();
		return InjectorResult(count > 0, count > 0 ? "selected by type" : "no matching units");
	}

	if (sub == "at") {
		if (tok.size() < 4) return InjectorResult(false, "usage: select at <tileX> <tileY>");
		Vec2i pos(std::strtol(tok[2].c_str(), nullptr, 10), std::strtol(tok[3].c_str(), nullptr, 10));
		if (!Map.Info.IsPointOnMap(pos)) return InjectorResult(false, "tile off map");
		CUnit *found = nullptr;
		for (CUnit *u : UnitManager->GetUnits()) {
			if (!u || !u->Type || u->Removed) continue;
			const Vec2i tl = u->tilePos;
			const Vec2i br(tl.x + u->Type->TileWidth - 1, tl.y + u->Type->TileHeight - 1);
			if (pos.x >= tl.x && pos.x <= br.x && pos.y >= tl.y && pos.y <= br.y) { found = u; break; }
		}
		if (!found) return InjectorResult(false, "no unit at tile");
		SelectSingleUnit(*found);
		SelectionChanged();
		return InjectorResult(true, "selected unit at tile", (long)UnitNumber(*found));
	}

	return InjectorResult(false, "unknown select subcommand: " + sub);
}

std::string CmdBuild(const std::vector<std::string> &tok)
{
	if (!InjectorGameReady()) return InjectorResult(false, "no game");
	if (tok.size() < 4) return InjectorResult(false, "usage: build <ident> <tileX> <tileY>");
	if (Selected.empty() || !Selected[0]) return InjectorResult(false, "no worker selected");

	CUnitType *type = FindUnitTypeByIdent(tok[1]);
	if (!type) return InjectorResult(false, "unknown unit type: " + tok[1]);

	Vec2i pos(std::strtol(tok[2].c_str(), nullptr, 10), std::strtol(tok[3].c_str(), nullptr, 10));
	if (!Map.Info.IsPointOnMap(pos)) return InjectorResult(false, "tile off map");

	CUnit *worker = Selected[0];
	if (!CanBuildUnitType(worker, *type, pos, 0).has_value()) {
		return InjectorResult(false, "worker cannot build " + type->Ident + " at that tile");
	}
	SendCommandBuildBuilding(*worker, pos, *type, EFlushMode::On);
	return InjectorResult(true, "build ordered", (long)UnitNumber(*worker));
}

// Evaluate an arbitrary Lua chunk on the engine's Lua state. A UI-testing lever: lets a menu
// screen be opened directly (e.g. `lua RunCampaignSubmenu("campaigns/...")`) without touch
// navigation, so any window can be reached + screenshotted from the bridge. Debug tool only.
std::string CmdLua(const std::string &code)
{
	if (code.empty()) return InjectorResult(false, "usage: lua <chunk>");
	if (!Lua) return InjectorResult(false, "no lua state");
	const int base = lua_gettop(Lua);
	if (luaL_loadstring(Lua, code.c_str()) != 0 || lua_pcall(Lua, 0, LUA_MULTRET, 0) != 0) {
		const char *err = lua_tostring(Lua, -1);
		std::string e = err ? err : "lua error";
		lua_settop(Lua, base);
		return InjectorResult(false, "lua error: " + e);
	}
	// Report the first return value (if any) as the message, so `lua return X` is inspectable.
	std::string ret = "ok";
	if (lua_gettop(Lua) > base) {
		const char *s = lua_tostring(Lua, base + 1);
		if (s) ret = s;
	}
	lua_settop(Lua, base);
	return InjectorResult(true, ret);
}

// Lua DebugNetQuitPlayer(player): in a single-player game, take a player out the way a player
// of a network game goes out for good - the executed quit command, reason "connection lost".
// To look at the rule for a gone player's units on one device (`lua DebugNetQuitPlayer(1)`).
// It exists only with the debug bridge: registered by DebugBridge_Init, defined nowhere else.
// Returns true if the quit ran.
int CclDebugNetQuitPlayer(lua_State *l)
{
	LuaCheckArgs(l, 1);
	const int player = LuaToNumber(l, 1);

	if (IsNetworkGame() || !InjectorGameReady() || player < 0 || player >= NumPlayers
		|| player == PlayerNumNeutral || player == ThisPlayer->Index
		|| Players[player].Type == PlayerTypes::PlayerNobody || Players[player].Gone) {
		lua_pushboolean(l, 0);
		return 1;
	}
	NetworkExecQuit(player, NetQuitLost);
	lua_pushboolean(l, 1);
	return 1;
}

// Lua DebugNetForceWatchChoice(): in a running network game, show this device the defeat
// banner with its two buttons, "Watch" and "Leave Game", whatever the number of players left
// and although its player is not defeated. For a look at the banner and at the watching state
// with two devices, where a real defeat ends the game and leaves nothing to watch. The scripts'
// own ActionDefeat is called with the global LanWatchForced set; nothing is sent to the others.
// It exists only with the debug bridge. Returns true if the banner was asked for.
int CclDebugNetForceWatchChoice(lua_State *l)
{
	LuaCheckArgs(l, 0);
	if (!IsNetworkGame() || !InjectorGameReady() || NetworkIsWatching()) {
		lua_pushboolean(l, 0);
		return 1;
	}
	lua_pushboolean(l, 1);
	lua_setglobal(l, "LanWatchForced");
	lua_getglobal(l, "ActionDefeat");
	const bool shown = lua_isfunction(l, -1) && lua_pcall(l, 0, 0, 0) == 0;
	if (!shown) {
		lua_pop(l, 1); // what is not a function, or the error
		lua_pushnil(l);
		lua_setglobal(l, "LanWatchForced");
	}
	lua_pushboolean(l, shown);
	return 1;
}

// Lua DebugNetFakeWait(table or nil): make NetWaitState() tell a made-up wait until it is
// cleared, to look at the waiting plate and at the held loading line on one device. Only what
// the scripts are told is made up; nothing of the network code is touched and no cycle stops.
//   DebugNetFakeWait{name = "Thrall", ms = 45000, allowance = 100000}   a player is awaited
//   DebugNetFakeWait{name = "Thrall", removing = true}                  his removal is agreed
//   DebugNetFakeWait{selfout = true, reason = "connection_lost"}        this device is out
//   DebugNetFakeWait{held = true, names = {"Thrall", "Lothar"}}         the game has not begun
//   DebugNetFakeWait(nil)                                               the network's state again
// The times count down in real time. The plate script is read in if the game has not read it.
// It exists only with the debug bridge: registered by DebugBridge_Init, defined nowhere else.
int CclDebugNetFakeWait(lua_State *l)
{
	LuaCheckArgs(l, 1);
	if (!lua_istable(l, 1)) {
		NetworkSetFakeWaitView(nullptr);
		lua_pushboolean(l, 1);
		return 1;
	}
	const auto text = [l](const char *name) {
		std::string value;
		lua_getfield(l, 1, name);
		if (lua_isstring(l, -1)) {
			value = lua_tostring(l, -1);
		}
		lua_pop(l, 1);
		return value;
	};
	const auto number = [l](const char *name, int fallback) {
		lua_getfield(l, 1, name);
		const int value = lua_isnumber(l, -1) ? static_cast<int>(lua_tonumber(l, -1)) : fallback;
		lua_pop(l, 1);
		return value;
	};
	const auto flag = [l](const char *name) {
		lua_getfield(l, 1, name);
		const bool value = lua_toboolean(l, -1) != 0;
		lua_pop(l, 1);
		return value;
	};
	CNetWaitView view;

	view.SelfLoaded = true;
	if (flag("held")) {
		view.Phase = "start";
		view.Held = true;
		const int ms = number("ms", -1);
		lua_getfield(l, 1, "names");
		if (lua_istable(l, -1)) {
			const int count = lua_rawlen(l, -1);
			for (int i = 1; i <= count; ++i) {
				lua_rawgeti(l, -1, i);
				if (lua_isstring(l, -1)) {
					CNetWaitPlayer player;
					player.Player = i;
					player.Name = lua_tostring(l, -1);
					player.State = "loading";
					player.MsLeft = ms;
					view.Players.push_back(player);
				}
				lua_pop(l, 1);
			}
		}
		lua_pop(l, 1);
	} else if (flag("selfout")) {
		view.Phase = "game";
		view.SelfOut = true;
		view.SelfOutReason = text("reason");
		if (view.SelfOutReason.empty()) {
			view.SelfOutReason = "connection_lost";
		}
	} else {
		view.Phase = "game";
		view.Player = number("player", -1);
		view.Name = text("name");
		view.MsLeft = number("ms", 60000);
		view.AllowanceMsLeft = number("allowance", 120000);
		view.Removing = flag("removing");
		CNetWaitPlayer player;
		player.Player = view.Player;
		player.Name = view.Name;
		player.State = view.Removing ? "removing" : "silent";
		player.MsLeft = view.MsLeft;
		view.Players.push_back(player);
	}
	NetworkSetFakeWaitView(&view);
	CclCommand("if (NetWaitFrame == nil) then Load(\"scripts/lan_wait.lua\") end");
	lua_pushboolean(l, 1);
	return 1;
}

std::string CmdTrain(const std::vector<std::string> &tok)
{
	if (!InjectorGameReady()) return InjectorResult(false, "no game");
	if (tok.size() < 2) return InjectorResult(false, "usage: train <ident>");
	if (Selected.empty() || !Selected[0]) return InjectorResult(false, "no building selected");

	CUnitType *type = FindUnitTypeByIdent(tok[1]);
	if (!type) return InjectorResult(false, "unknown unit type: " + tok[1]);

	CUnit *building = Selected[0];
	SendCommandTrainUnit(*building, *type, EFlushMode::On);
	return InjectorResult(true, "train ordered", (long)UnitNumber(*building));
}

std::string CmdMove(const std::vector<std::string> &tok)
{
	if (!InjectorGameReady()) return InjectorResult(false, "no game");
	if (tok.size() < 3) return InjectorResult(false, "usage: move <tileX> <tileY>");
	if (Selected.empty()) return InjectorResult(false, "nothing selected");

	Vec2i pos(std::strtol(tok[1].c_str(), nullptr, 10), std::strtol(tok[2].c_str(), nullptr, 10));
	if (!Map.Info.IsPointOnMap(pos)) return InjectorResult(false, "tile off map");

	// The same goals as a move order given with the pointer.
	const std::vector<Vec2i> goals = GroupMoveGoalsForUnits(Selected, pos);
	int n = 0;
	for (size_t i = 0; i < Selected.size(); ++i) {
		CUnit *u = Selected[i];
		if (u) { SendCommandMove(*u, goals[i], EFlushMode::On); ++n; }
	}
	return InjectorResult(n > 0, "move ordered for " + std::to_string(n) + " unit(s)");
}

std::string CmdStop(const std::vector<std::string> &)
{
	if (!InjectorGameReady()) return InjectorResult(false, "no game");
	if (Selected.empty()) return InjectorResult(false, "nothing selected");
	int n = 0;
	for (CUnit *u : Selected) {
		if (u) { SendCommandStopUnit(*u); ++n; }
	}
	return InjectorResult(n > 0, "stop ordered for " + std::to_string(n) + " unit(s)");
}

// zoom <float> : set the magnification of every in-game map viewport (clamped to [1.0, 4.0]) so the
// operator can exercise the Phase-1 GPU render at Zoom 1/2/4. Mirrors the two-finger pinch gesture.
std::string CmdZoom(const std::vector<std::string> &tok)
{
	Json j;
	if (tok.size() < 2) {
		j.ch('{'); j.boolean("ok", false); j.comma(); j.str("msg", "usage: zoom <1.0..4.0>"); j.ch('}');
		return j.s;
	}
	float z = std::strtof(tok[1].c_str(), nullptr);
	if (z < 1.0f) z = 1.0f;
	else if (z > 4.0f) z = 4.0f;

#ifdef __ANDROID__
	// Re-derives tiles-in-view, scroll clamp, fog and minimap geometry for every viewport.
	SetMapViewportsZoom(z);
#else
	// Desktop test path: replicate the per-viewport re-anchor (SetMapViewportsZoom is Android-only).
	for (unsigned int i = 0; i < UI.NumViewports; ++i) {
		CViewport &vp = UI.Viewports[i];
		vp.Zoom = z;
		vp.Set(vp.MapPos, vp.Offset);
	}
#endif
	j.ch('{'); j.boolean("ok", true); j.comma(); j.dbl("zoom", z); j.ch('}');
	return j.s;
}

// Override a player's colour slot live (GameSettings.Presets[player].PlayerColor). The sprite tint
// and minimap read this every frame, so the change is immediate. Test tooling: verifies the HD
// team-colour recolour works with a non-default colour. Usage: pcolor <player> <colorIdx>.
std::string CmdPColor(const std::vector<std::string> &tok)
{
	if (tok.size() < 3) return InjectorResult(false, "usage: pcolor <player> <colorIdx>");
	const int p = (int)std::strtol(tok[1].c_str(), nullptr, 10);
	const int c = (int)std::strtol(tok[2].c_str(), nullptr, 10);
	if (p < 0 || p >= PlayerMax) return InjectorResult(false, "player out of range");
	if (c < 0 || c >= (int)PlayerColorsRGB.size()) return InjectorResult(false, "colorIdx out of range");
	GameSettings.Presets[p].PlayerColor = c;
	if (p < NumPlayers) {
		Players[p].SetUnitColors(PlayerColorsRGB[c]);
	}
	Json j; j.ch('{'); j.boolean("ok", true); j.comma(); j.num("player", p);
	j.comma(); j.num("colorIdx", c);
	char cbuf[32];
	const CColor &col = PlayerColorsRGB[c][0];
	snprintf(cbuf, sizeof(cbuf), "%d,%d,%d", col.R, col.G, col.B);
	j.comma(); j.str("colorRGB", cbuf); j.ch('}');
	return j.s;
}

std::string CmdUnitTypes(const std::vector<std::string> &tok)
{
	const std::string filter = (tok.size() >= 2) ? tok[1] : std::string();
	Json j;
	j.ch('{');
	j.key_("filter"); j.esc(filter);
	j.comma(); j.key_("types"); j.ch('[');
	int n = 0;
	for (CUnitType *t : getUnitTypes()) {
		if (!t) continue;
		if (!filter.empty() && t->Ident.find(filter) == std::string::npos) continue;
		if (n) j.comma();
		j.ch('{');
		j.str("ident", t->Ident);
		j.comma(); j.str("name", t->Name);
		j.comma(); j.boolean("building", t->Building);
		j.ch('}');
		++n;
	}
	j.ch(']');
	j.comma(); j.num("count", n);
	j.ch('}');
	return j.s;
}

/*----------------------------------------------------------------------------
--  Command tooltips
----------------------------------------------------------------------------*/

// The texts the tooltip panel draws for every shown command button of the selection: the same
// call DrawRemasteredTooltip makes, so a tooltip can be read without pointing at its button.
std::string CmdTips()
{
	Json j;
	j.ch('{');
	j.num("selected", (long)Selected.size());
	j.comma(); j.num("level", CurrentButtonLevel);
	j.comma(); j.key_("buttons"); j.ch('[');
	int n = 0;
	if (!Selected.empty()) {
		for (const ButtonAction &button : CurrentButtons) {
			if (button.Pos == -1) {
				continue;
			}
			bool allowed = true;
			for (const CUnit *unit : Selected) {
				if (!IsButtonAllowed(*unit, button)) {
					allowed = false;
					break;
				}
			}
			std::string title;
			std::string body;
			std::string requirement;
			const int id = GetRemasteredTooltipTexts(button, allowed, title, body, requirement);
			if (n) j.comma();
			j.ch('{');
			j.num("pos", button.Pos);
			j.comma(); j.num("action", (long)button.Action);
			j.comma(); j.str("value", button.ValueStr);
			j.comma(); j.boolean("allowed", allowed);
			j.comma(); j.num("id", id);
			j.comma(); j.str("title", title);
			j.comma(); j.str("body", body);
			j.comma(); j.str("requirement", requirement);
			j.comma(); j.str("hint", button.Hint);
			j.ch('}');
			++n;
		}
	}
	j.ch(']');
	j.comma(); j.num("count", n);
	j.ch('}');
	return j.s;
}

/*----------------------------------------------------------------------------
--  HUD layout dump
----------------------------------------------------------------------------*/

// Emit {"x","y","w","h"} for a CUIButton; size comes from its Style (may be null).
void EmitButtonRect(Json &j, const CUIButton &b, bool withText = false)
{
	j.ch('{');
	j.num("x", b.X);
	j.comma(); j.num("y", b.Y);
	j.comma(); j.num("w", b.Style ? b.Style->Width : 0);
	j.comma(); j.num("h", b.Style ? b.Style->Height : 0);
	if (withText) { j.comma(); j.str("text", b.Text); }
	j.ch('}');
}

void EmitButtonArray(Json &j, const char *key, const std::vector<CUIButton> &v)
{
	j.comma(); j.key_(key); j.ch('[');
	for (size_t i = 0; i < v.size(); ++i) {
		if (i) j.comma();
		EmitButtonRect(j, v[i]);
	}
	j.ch(']');
}

std::string CmdHud()
{
	Json j;
	j.ch('{');

	// infoPanel: CInfoPanel has G + X/Y; W/H come from the graphic (may be null pre-game).
	j.key_("infoPanel"); j.ch('{');
	j.num("x", UI.InfoPanel.X);
	j.comma(); j.num("y", UI.InfoPanel.Y);
	j.comma(); j.num("w", UI.InfoPanel.G ? UI.InfoPanel.G->Width : 0);
	j.comma(); j.num("h", UI.InfoPanel.G ? UI.InfoPanel.G->Height : 0);
	j.ch('}');

	// buttonPanel + its command buttons
	j.comma(); j.key_("buttonPanel"); j.ch('{');
	j.num("x", UI.ButtonPanel.X);
	j.comma(); j.num("y", UI.ButtonPanel.Y);
	j.comma(); j.num("w", UI.ButtonPanel.G ? UI.ButtonPanel.G->Width : 0);
	j.comma(); j.num("h", UI.ButtonPanel.G ? UI.ButtonPanel.G->Height : 0);
	j.comma(); j.key_("buttons"); j.ch('[');
	for (size_t i = 0; i < UI.ButtonPanel.Buttons.size(); ++i) {
		if (i) j.comma();
		EmitButtonRect(j, UI.ButtonPanel.Buttons[i]);
	}
	j.ch(']');
	j.ch('}');

	// minimap
	j.comma(); j.key_("minimap"); j.ch('{');
	j.num("x", UI.Minimap.X);
	j.comma(); j.num("y", UI.Minimap.Y);
	j.comma(); j.num("w", UI.Minimap.W);
	j.comma(); j.num("h", UI.Minimap.H);
	j.ch('}');

	// resources (icon + text anchors)
	j.comma(); j.key_("resources"); j.ch('[');
	bool firstRes = true;
	for (int i = 0; i < MaxResourceInfo; ++i) {
		const CResourceInfo &r = UI.Resources[i];
		// Only emit slots that are actually configured (have an icon or a text pos).
		if (!r.G && r.TextX == -1 && r.TextY == -1 && r.IconX == 0 && r.IconY == 0) continue;
		if (!firstRes) j.comma();
		firstRes = false;
		j.ch('{');
		j.num("index", i);
		j.comma(); j.num("iconX", r.IconX);
		j.comma(); j.num("iconY", r.IconY);
		j.comma(); j.num("iconW", r.IconWidth);
		j.comma(); j.num("textX", r.TextX);
		j.comma(); j.num("textY", r.TextY);
		j.ch('}');
	}
	j.ch(']');

	// selectedButtons + singleSelectedButton
	EmitButtonArray(j, "selectedButtons", UI.SelectedButtons);
	j.comma(); j.key_("singleSelectedButton");
	if (UI.SingleSelectedButton) EmitButtonRect(j, *UI.SingleSelectedButton); else j.raw("null");

	// trainingButtons + singleTrainingButton
	EmitButtonArray(j, "trainingButtons", UI.TrainingButtons);
	j.comma(); j.key_("singleTrainingButton");
	if (UI.SingleTrainingButton) EmitButtonRect(j, *UI.SingleTrainingButton); else j.raw("null");

	// transportingButtons
	EmitButtonArray(j, "transportingButtons", UI.TransportingButtons);

	// upgradingButton / researchingButton
	j.comma(); j.key_("upgradingButton");
	if (UI.UpgradingButton) EmitButtonRect(j, *UI.UpgradingButton); else j.raw("null");
	j.comma(); j.key_("researchingButton");
	if (UI.ResearchingButton) EmitButtonRect(j, *UI.ResearchingButton); else j.raw("null");

	// userButtons (vector<CUIUserButton>, each has a .Button)
	j.comma(); j.key_("userButtons"); j.ch('[');
	for (size_t i = 0; i < UI.UserButtons.size(); ++i) {
		if (i) j.comma();
		EmitButtonRect(j, UI.UserButtons[i].Button, /*withText=*/true);
	}
	j.ch(']');

	// menu buttons
	j.comma(); j.key_("menuButton"); EmitButtonRect(j, UI.MenuButton, true);
	j.comma(); j.key_("networkMenuButton"); EmitButtonRect(j, UI.NetworkMenuButton, true);
	j.comma(); j.key_("networkDiplomacyButton"); EmitButtonRect(j, UI.NetworkDiplomacyButton, true);

	// pieMenu (9 slots, radius-relative offsets)
	j.comma(); j.key_("pieMenu"); j.ch('{');
	j.num("mouseButton", UI.PieMenu.MouseButton);
	j.comma(); j.key_("x"); j.ch('[');
	for (int i = 0; i < 9; ++i) { if (i) j.comma(); j.raw(std::to_string(UI.PieMenu.X[i])); }
	j.ch(']');
	j.comma(); j.key_("y"); j.ch('[');
	for (int i = 0; i < 9; ++i) { if (i) j.comma(); j.raw(std::to_string(UI.PieMenu.Y[i])); }
	j.ch(']');
	j.ch('}');

	// statusLine (has Width + TextX/TextY; no dedicated X/Y)
	j.comma(); j.key_("statusLine"); j.ch('{');
	j.num("x", UI.StatusLine.TextX);
	j.comma(); j.num("y", UI.StatusLine.TextY);
	j.comma(); j.num("w", UI.StatusLine.Width);
	j.ch('}');

	// mapArea
	j.comma(); j.key_("mapArea"); j.ch('{');
	j.num("x", UI.MapArea.X);
	j.comma(); j.num("y", UI.MapArea.Y);
	j.comma(); j.num("ex", UI.MapArea.EndX);
	j.comma(); j.num("ey", UI.MapArea.EndY);
	j.ch('}');

	// selectedViewport
	j.comma(); j.key_("selectedViewport");
	if (UI.SelectedViewport) {
		const CViewport *vp = UI.SelectedViewport;
		j.ch('{');
		j.num("x", vp->GetTopLeftPos().x);
		j.comma(); j.num("y", vp->GetTopLeftPos().y);
		j.comma(); j.num("ex", vp->GetBottomRightPos().x);
		j.comma(); j.num("ey", vp->GetBottomRightPos().y);
		j.comma(); j.num("mapWidth", vp->MapWidth);
		j.comma(); j.num("mapHeight", vp->MapHeight);
		j.ch('}');
	} else {
		j.raw("null");
	}

	j.ch('}');
	return j.s;
}

// Play a finger gesture into the touch layer (video/sdl.cpp). Usage: touch <steps>,
// steps `d X Y`, `m X Y`, `u`, `w MS`, and `d2` / `m2` / `u2` for a second finger; X, Y in
// logical framebuffer pixels (the space of the `hud` and `touchinfo` rects). The script plays
// over the following frames; read the outcome with `touchinfo` or `state` afterwards.
std::string CmdTouch(const std::string &script)
{
	Json j;
	std::string error;
	const bool ok = DebugBridge_TouchScript(script, error);
	j.ch('{');
	j.boolean("ok", ok);
	if (!ok) {
		j.comma(); j.str("msg", error);
	}
	j.ch('}');
	return j.s;
}

// What the touch routes can be checked against: the touch layer's own state, the group
// strip (rect, state and unit count per group), the placement preview, the selection with
// the length of each unit's order queue, and the Shift bit (must be 0 between gestures).
std::string CmdTouchInfo()
{
	Json j;
	j.ch('{');
	j.key_("touch"); j.ch('{'); j.raw(DebugBridge_TouchState()); j.ch('}');
	j.comma(); j.boolean("touchMode", UiTouchMode());
	j.comma(); j.num("keyModifiers", KeyModifiers);
	j.comma(); j.num("cursorState", static_cast<long>(CursorState));
	j.comma(); j.boolean("stripShown", GameRunning && UiTouchGroupStripShown());

	j.comma(); j.key_("groups"); j.ch('[');
	for (int group = 1; group <= 9; ++group) {
		int x = 0, y = 0, w = 0, h = 0;
		const bool fits = GameRunning && UiTouchGroupSlotRect(group, x, y, w, h);
		if (group > 1) j.comma();
		j.ch('{');
		j.num("group", group);
		j.comma(); j.num("count", GameRunning ? static_cast<long>(GetUnitsOfGroup(group).size()) : 0);
		j.comma(); j.num("state", fits ? UiTouchGroupSlotState(group) : 0);
		j.comma(); j.num("x", x);
		j.comma(); j.num("y", y);
		j.comma(); j.num("w", w);
		j.comma(); j.num("h", h);
		j.ch('}');
	}
	j.ch(']');

	j.comma(); j.key_("preview");
	Vec2i tile;
	if (GameRunning && CursorBuilding && UI.SelectedViewport
		&& GetBuildingPreviewTile(*UI.SelectedViewport, tile)) {
		j.ch('{');
		j.str("type", CursorBuilding->Ident);
		j.comma(); j.num("tileX", tile.x);
		j.comma(); j.num("tileY", tile.y);
		j.comma(); j.num("tileW", CursorBuilding->TileWidth);
		j.comma(); j.num("tileH", CursorBuilding->TileHeight);
		j.ch('}');
	} else {
		j.raw("null");
	}
	j.comma(); j.key_("building");
	if (CursorBuilding) {
		j.esc(CursorBuilding->Ident);
	} else {
		j.raw("null");
	}

	j.comma(); j.key_("selected"); j.ch('[');
	for (size_t i = 0; i < Selected.size(); ++i) {
		if (i) j.comma();
		j.ch('{');
		j.num("id", Selected[i] ? static_cast<long>(UnitNumber(*Selected[i])) : -1);
		j.comma(); j.num("orders", Selected[i] ? static_cast<long>(Selected[i]->Orders.size()) : 0);
		j.ch('}');
	}
	j.ch(']');
	j.ch('}');
	return j.s;
}

std::string Dispatch(const std::string &cmdline)
{
	// Trim leading whitespace.
	size_t p = cmdline.find_first_not_of(" \t\r\n");
	if (p == std::string::npos) {
		return "{\"error\":\"empty command\"}";
	}
	std::string line = cmdline.substr(p);
	// First token is the verb.
	size_t sp = line.find_first_of(" \t");
	std::string verb = (sp == std::string::npos) ? line : line.substr(0, sp);
	// strip trailing CR/LF from verb
	while (!verb.empty() && (verb.back() == '\r' || verb.back() == '\n')) verb.pop_back();

	if (verb == "state") return CmdState();
	if (verb == "ui")    return CmdUi();
	if (verb == "assets") return CmdAssets();
	if (verb == "color") {
		int cx = 0, cy = 0;
		if (sp != std::string::npos) {
			std::sscanf(line.c_str() + sp, "%d %d", &cx, &cy);
		}
		return CmdColor(cx, cy);
	}
	if (verb == "ping") {
		return "{\"pong\":true}";
	}
	if (verb == "prof")  return CmdProf();
	// --- Injector: drive units via the engine's Send* command API ---
	if (verb == "select")    return CmdSelect(Tokenize(line));
	if (verb == "build")     return CmdBuild(Tokenize(line));
	if (verb == "train")     return CmdTrain(Tokenize(line));
	if (verb == "move")      return CmdMove(Tokenize(line));
	if (verb == "stop")      return CmdStop(Tokenize(line));
	if (verb == "unittypes") return CmdUnitTypes(Tokenize(line));
	if (verb == "zoom")      return CmdZoom(Tokenize(line));
	if (verb == "pcolor")    return CmdPColor(Tokenize(line));
	// --- HUD layout dump ---
	if (verb == "hud")       return CmdHud();
	// --- command tooltip texts of the selection ---
	if (verb == "tips")      return CmdTips();
	// --- touch routes: play a gesture, read what it did ---
	if (verb == "touch")     return CmdTouch((sp != std::string::npos) ? line.substr(sp + 1) : "");
	if (verb == "touchinfo") return CmdTouchInfo();
	// --- arbitrary Lua eval (UI testing: open any menu without touch navigation) ---
	if (verb == "lua") {
		std::string code = (sp != std::string::npos) ? line.substr(sp + 1) : "";
		return CmdLua(code);
	}
	Json j;
	j.ch('{');
	j.str("error", "unknown command");
	j.comma(); j.str("verb", verb);
	j.ch('}');
	return j.s;
}

void WriteStatus()
{
	Json j;
	j.ch('{');
	j.num("frameCounter", (long)FrameCounter);
	j.comma(); j.num("gameCycle", (long)GameCycle);
	j.comma(); j.boolean("running", GameRunning);
	j.comma(); j.num("ts", (long)time(nullptr));
	j.ch('}');
	FILE *f = std::fopen(DbgStatFile.string().c_str(), "wb");
	if (f) {
		std::fwrite(j.s.data(), 1, j.s.size(), f);
		std::fclose(f);
	}
}

#endif // WARGUSHD_DEBUG_COMMANDS

/*----------------------------------------------------------------------------
--  Crash handler
----------------------------------------------------------------------------*/

const char *SignalName(int sig)
{
	switch (sig) {
		case SIGSEGV: return "SIGSEGV";
		case SIGABRT: return "SIGABRT";
#ifdef SIGBUS
		case SIGBUS:  return "SIGBUS";
#endif
		case SIGFPE:  return "SIGFPE";
		case SIGILL:  return "SIGILL";
		default:      return "SIGNAL";
	}
}

#ifndef _WIN32
// Async-signal-safe integer -> decimal writer.
void RawWriteInt(int fd, long v)
{
	char buf[32];
	int i = sizeof(buf);
	bool neg = v < 0;
	unsigned long uv = neg ? (unsigned long)(-v) : (unsigned long)v;
	if (uv == 0) { buf[--i] = '0'; }
	while (uv && i > 0) { buf[--i] = char('0' + (uv % 10)); uv /= 10; }
	if (neg && i > 0) { buf[--i] = '-'; }
	(void)!write(fd, buf + i, sizeof(buf) - i);
}
void RawWrite(int fd, const char *s) { (void)!write(fd, s, std::strlen(s)); }

// Async-signal-safe "0x..." hex writer.
void RawWriteHex(int fd, unsigned long v)
{
	char buf[2 + sizeof(unsigned long) * 2];
	int i = sizeof(buf);
	static const char hex[] = "0123456789abcdef";
	if (v == 0) { buf[--i] = '0'; }
	while (v && i > 1) { buf[--i] = hex[v & 0xF]; v >>= 4; }
	buf[--i] = 'x';
	buf[--i] = '0';
	(void)!write(fd, buf + i, sizeof(buf) - i);
}

// Per-frame sink for _Unwind_Backtrace. Resolves module + nearest symbol via dladdr and
// writes one line per frame:  #NN 0x<pc> <module>+0x<off> <symbol>+0x<symoff>
struct BtState {
	int fd;
	int count;
	int max;
};

_Unwind_Reason_Code BtCallback(struct _Unwind_Context *ctx, void *arg)
{
	BtState *st = (BtState *)arg;
	if (st->count >= st->max) { return _URC_END_OF_STACK; }
	const uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);
	if (pc == 0) { return _URC_END_OF_STACK; }

	RawWrite(st->fd, "  #");
	RawWriteInt(st->fd, st->count);
	RawWrite(st->fd, " ");
	RawWriteHex(st->fd, (unsigned long)pc);

	Dl_info info;
	std::memset(&info, 0, sizeof(info));
	// dladdr is not strictly async-signal-safe, but the process is already dying and this is
	// the only way to turn a raw PC into a module+symbol offline. Best-effort.
	if (dladdr((void *)pc, &info) && info.dli_fname) {
		RawWrite(st->fd, " ");
		RawWrite(st->fd, info.dli_fname);
		if (info.dli_fbase) {
			RawWrite(st->fd, "+");
			RawWriteHex(st->fd, (unsigned long)(pc - (uintptr_t)info.dli_fbase));
		}
		if (info.dli_sname) {
			RawWrite(st->fd, " ");
			RawWrite(st->fd, info.dli_sname);
			RawWrite(st->fd, "+");
			RawWriteHex(st->fd, (unsigned long)(pc - (uintptr_t)info.dli_saddr));
		}
#ifdef __ANDROID__
		__android_log_print(ANDROID_LOG_ERROR, DBG_LOG_TAG, "  #%d %p %s+0x%lx %s",
			st->count, (void *)pc, info.dli_fname,
			(unsigned long)(pc - (uintptr_t)info.dli_fbase),
			info.dli_sname ? info.dli_sname : "?");
#endif
	}
	RawWrite(st->fd, "\n");
	st->count++;
	return _URC_NO_REASON;
}

// Shared writer: dump a header + unwound frames to fd. Used by the signal handler and by the
// C++ fatal-exception recorder (which is the path that actually reaches CrashExit on Android:
// an uncaught gcn::Exception / std::exception unwinds to stratagusMain and _Exit()s, so NO
// signal fires and the signal handler below never runs -- crash.txt would otherwise stay empty).
void DumpFramesToFd(int fd)
{
	RawWrite(fd, "\nframeCounter=");
	RawWriteInt(fd, (long)FrameCounter);
	RawWrite(fd, "\ngameCycle=");
	RawWriteInt(fd, (long)GameCycle);
	RawWrite(fd, "\nbacktrace:\n");
	BtState st{fd, 0, 48};
	_Unwind_Backtrace(BtCallback, &st);
	if (st.count == 0) {
		RawWrite(fd, "  (unwind produced no frames)\n");
	}
}

void CrashHandler(int sig, siginfo_t *info, void *ucontext)
{
	(void)ucontext;
	// Use only low-level, mostly async-signal-safe calls. Best-effort readable dump.
	int fd = open(DbgCrashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		RawWrite(fd, "signal=");
		RawWrite(fd, SignalName(sig));
		RawWrite(fd, " (");
		RawWriteInt(fd, sig);
		RawWrite(fd, ")\nfaultAddr=");
		RawWriteHex(fd, info ? (unsigned long)(uintptr_t)info->si_addr : 0UL);
		RawWrite(fd, "\nts=");
		RawWriteInt(fd, (long)time(nullptr));
		DumpFramesToFd(fd);
		fsync(fd);
		close(fd);
	}
#ifdef __ANDROID__
	__android_log_print(ANDROID_LOG_ERROR, DBG_LOG_TAG,
		"FATAL %s (%d) faultAddr=%p frame=%ld cycle=%ld -> frames follow (also in %s)",
		SignalName(sig), sig, info ? info->si_addr : nullptr,
		(long)FrameCounter, (long)GameCycle, DbgCrashPath);
#endif
	// Restore default disposition and re-raise so the process still dies normally.
	signal(sig, SIG_DFL);
	raise(sig);
}

void InstallCrashHandlers()
{
	struct sigaction sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = CrashHandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	sigaction(SIGSEGV, &sa, nullptr);
	sigaction(SIGABRT, &sa, nullptr);
#ifdef SIGBUS
	sigaction(SIGBUS, &sa, nullptr);
#endif
	sigaction(SIGFPE, &sa, nullptr);
	sigaction(SIGILL, &sa, nullptr);
}
#else
void InstallCrashHandlers()
{
	// On Windows the engine already has print_backtrace()/SEH elsewhere; the debug
	// bridge crash file is a POSIX-target (Android) feature. No-op here.
}
#endif

/*----------------------------------------------------------------------------
--  Main-loop watchdog thread
----------------------------------------------------------------------------*/

static std::atomic<bool> WatchdogStarted{false};

// Sample the heartbeat once a second. When it stops advancing the main thread is
// wedged (silent hang: no signal, no exception). Log the last checkpoint to logcat
// and rewrite crash.txt with it, so an on-device ANR that Android kills still tells
// us WHERE it stuck. A working modal menu still bumps checkpoints (its loop runs
// UpdateDisplay), so it does not false-trigger.
void WatchdogThreadMain()
{
	unsigned long lastBeat = DbgHeartbeat.load(std::memory_order_relaxed);
	int stalledSec = 0;
	int lastReported = 0;
	for (;;) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		const unsigned long beat = DbgHeartbeat.load(std::memory_order_relaxed);
		if (beat != lastBeat) {
			lastBeat = beat;
			stalledSec = 0;
			lastReported = 0;
			continue;
		}
		// Heartbeat frozen for >= 1s: main loop has not crossed a checkpoint.
		++stalledSec;
		if (stalledSec >= 3 && stalledSec != lastReported) {
			lastReported = stalledSec;
			const char *cp = DbgCheckpoint.load(std::memory_order_relaxed);
			if (!cp) { cp = "?"; }
#ifdef __ANDROID__
			__android_log_print(ANDROID_LOG_ERROR, DBG_LOG_TAG,
				"WATCHDOG: main loop STALLED %ds at checkpoint '%s' (frame=%ld cycle=%ld paused=%d)",
				stalledSec, cp, (long)FrameCounter, (long)GameCycle, (int)GamePaused);
#endif
			if (DbgCrashPath[0] && WatchdogWritesCrashFile(cp)) {
				FILE *f = std::fopen(DbgCrashPath, "wb");
				if (f) {
					std::fprintf(f,
						"signal=none (WATCHDOG STALL)\ncheckpoint=%s\nstalledSec=%d\n"
						"frameCounter=%ld\ngameCycle=%ld\npaused=%d\nts=%ld\n",
						cp, stalledSec, (long)FrameCounter, (long)GameCycle,
						(int)GamePaused, (long)time(nullptr));
					std::fclose(f);
				}
			}
		}
	}
}

} // namespace

/*----------------------------------------------------------------------------
--  Public API
----------------------------------------------------------------------------*/

bool WatchdogWritesCrashFile(const char *label)
{
	if (label == nullptr) {
		return true;
	}
	return std::strncmp(label, "loading", 7) != 0 && std::strncmp(label, "movie:", 6) != 0;
}

void DebugBridge_RecordFatal(const char *kind, const char *what)
{
	// Called from stratagusMain's catch blocks BEFORE CrashExit()/_Exit(). This is the real
	// silent-death path on Android (uncaught C++ exception -> no signal, no tombstone). Record
	// kind + what() + a here-and-now backtrace (catch site / main; the throw site is already
	// unwound, but kind/what -- e.g. gcn::Exception "file:line (func): message" -- pinpoints it).
	if (!kind) { kind = "?"; }
	if (!what) { what = ""; }
#ifdef __ANDROID__
	__android_log_print(ANDROID_LOG_ERROR, DBG_LOG_TAG,
		"FATAL C++ exception [%s]: %s (frame=%ld cycle=%ld)",
		kind, what, (long)FrameCounter, (long)GameCycle);
#endif
#ifndef _WIN32
	if (DbgCrashPath[0]) {
		int fd = open(DbgCrashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) {
			RawWrite(fd, "signal=none (C++ exception)\nkind=");
			RawWrite(fd, kind);
			RawWrite(fd, "\nwhat=");
			RawWrite(fd, what);
			RawWrite(fd, "\nts=");
			RawWriteInt(fd, (long)time(nullptr));
			DumpFramesToFd(fd);
			fsync(fd);
			close(fd);
		}
	}
#else
	if (DbgCrashPath[0]) {
		FILE *f = std::fopen(DbgCrashPath, "wb");
		if (f) {
			std::fprintf(f, "signal=none (C++ exception)\nkind=%s\nwhat=%s\nts=%ld\n"
			                "frameCounter=%ld\ngameCycle=%ld\nbacktrace:\n(win32: see minidump)\n",
			             kind, what, (long)time(nullptr), (long)FrameCounter, (long)GameCycle);
			std::fclose(f);
		}
	}
#endif
}

/// The command channel is on (true for a debug start only).
static int CclDebugBridgeOn(lua_State *l)
{
	lua_pushboolean(l, DbgCommands);
	return 1;
}

void DebugBridge_Init()
{
	if (DbgInited) {
		return;
	}

	fs::path base = Parameters::Instance.GetUserDirectory();
	if (base.empty()) {
		// Fall back to CWD if the user dir was never resolved.
		base = fs::current_path();
	}
	DbgDir = base / "wdbg";

	std::error_code ec;
	fs::create_directories(DbgDir, ec);

	DbgCmdFile  = DbgDir / "cmd";
	DbgOutFile  = DbgDir / "out.json";
	DbgOutTmp   = DbgDir / "out.json.tmp";
	DbgStatFile = DbgDir / "status.json";

	const std::string crash = (DbgDir / "crash.txt").string();
	std::strncpy(DbgCrashPath, crash.c_str(), sizeof(DbgCrashPath) - 1);
	DbgCrashPath[sizeof(DbgCrashPath) - 1] = '\0';

	InstallCrashHandlers();

	DbgInited = true;

#ifndef WARGUSHD_DEBUG_COMMANDS
#ifdef __ANDROID__
	__android_log_print(ANDROID_LOG_INFO, DBG_LOG_TAG, "debug bridge commands not built");
#else
	ErrorPrint("debug bridge commands not built\n");
#endif
#elif defined(__ANDROID__)
	{
		// A file, not an environment variable: where the library runs under binary translation
		// (an x86 emulator) the Java side and this side do not share an environment.
		const char *files = SDL_AndroidGetInternalStoragePath();
		std::error_code markEc;
		DbgCommands = files != nullptr && fs::exists(fs::path(files) / "debug_bridge", markEc);
	}
	__android_log_print(ANDROID_LOG_INFO, DBG_LOG_TAG, "debug bridge commands %s", DbgCommands ? "on" : "off");
#else
	{
		const char *wanted = std::getenv("WARGUSHD_DEBUG_BRIDGE");
		DbgCommands = wanted != nullptr && std::strcmp(wanted, "1") == 0;
	}
	ErrorPrint("debug bridge commands %s\n", DbgCommands ? "on" : "off");
#endif
#ifdef WARGUSHD_DEBUG_COMMANDS
	if (Lua && DbgCommands) {
		lua_register(Lua, "DebugNetQuitPlayer", CclDebugNetQuitPlayer);
		lua_register(Lua, "DebugNetFakeWait", CclDebugNetFakeWait);
		lua_register(Lua, "DebugNetForceWatchChoice", CclDebugNetForceWatchChoice);
	}
#endif
	if (Lua) {
		lua_register(Lua, "DebugBridgeOn", CclDebugBridgeOn); // the in-game Debug row asks
	}

#ifdef WARGUSHD_DEBUG_COMMANDS
	if (DbgCommands) {
		WriteStatus();
	}
#endif

	DebugBridge_StartWatchdog();
}

void DebugBridge_StartWatchdog()
{
	bool expected = false;
	if (!WatchdogStarted.compare_exchange_strong(expected, true)) {
		return; // already running
	}
	std::thread(WatchdogThreadMain).detach();
}

void DebugBridge_Poll()
{
	if (!DbgInited) {
		return;
	}
	// Throttle: only touch the filesystem every ~15 frames.
	if ((FrameCounter % 15) != 0) {
		return;
	}

	if (!DbgCommands) {
		return; // crash records only: no status file, no command read
	}
#ifdef WARGUSHD_DEBUG_COMMANDS
	WriteStatus();

	std::error_code ec;
	if (!fs::exists(DbgCmdFile, ec)) {
		return;
	}

	// Read the whole command file (expected one line).
	std::string cmdline;
	{
		FILE *f = std::fopen(DbgCmdFile.string().c_str(), "rb");
		if (f) {
			char buf[1024];
			size_t r;
			while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) {
				cmdline.append(buf, r);
				if (cmdline.size() > 64 * 1024) break; // sanity cap
			}
			std::fclose(f);
		}
	}
	// Consume the command so it is not re-run next tick.
	fs::remove(DbgCmdFile, ec);

	std::string response;
	try {
		response = Dispatch(cmdline);
	} catch (...) {
		response = "{\"error\":\"exception while dispatching command\"}";
	}
	WriteFileAtomic(response);
#endif // WARGUSHD_DEBUG_COMMANDS
}

//@}
