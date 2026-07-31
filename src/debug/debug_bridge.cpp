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

#include "commands.h"
#include "cursor.h"
#include "filesystem.h"
#include "interface.h"
#include "map.h"
#include "parameters.h"
#include "player.h"
#include "settings.h"
#include "tile.h"
#include "ui.h"
#include "unit.h"
#include "unit_manager.h"
#include "unittype.h"
#include "video.h"
#include "viewport.h"

#include <guisan.hpp>
#include <guisan/widgets/label.hpp>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

// Android ships <execinfo.h> in the sysroot but backtrace()/backtrace_symbols_fd() are only
// declared/available at API >= 33; this build targets API 24, so exclude Android (the system's
// debuggerd/tombstone provides the native backtrace on crash regardless — the handler here just
// records signal + frameCounter + gameCycle to a pullable file).
#if __has_include(<execinfo.h>) && !defined(__ANDROID__)
#include <execinfo.h>
#define DBG_HAVE_EXECINFO 1
#endif

// The guichan top widget (current MenuScreen / game / editor container).
extern std::unique_ptr<gcn::Gui> Gui;

/*----------------------------------------------------------------------------
--  State
----------------------------------------------------------------------------*/

static bool DbgInited = false;
static fs::path DbgDir;       /// <UserDirectory>/wdbg
static fs::path DbgCmdFile;   /// wdbg/cmd
static fs::path DbgOutFile;   /// wdbg/out.json
static fs::path DbgOutTmp;    /// wdbg/out.json.tmp
static fs::path DbgStatFile;  /// wdbg/status.json

// The crash handler must be async-signal-safe-ish, so it cannot touch fs::path
// (allocations). Cache the crash file path as a plain C string up front.
static char DbgCrashPath[4096] = {0};

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
						std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
						s += buf;
					} else {
						s += c;
					}
			}
		}
		s += '"';
	}
	// "key":<literal>   (caller supplies formatted number/bool/object)
	void num(const char *key, long v) { key_(key); char b[32]; std::snprintf(b, sizeof(b), "%ld", v); s += b; }
	void dbl(const char *key, double v) { key_(key); char b[48]; std::snprintf(b, sizeof(b), "%g", v); s += b; }
	void boolean(const char *key, bool v) { key_(key); s += v ? "true" : "false"; }
	void str(const char *key, const std::string &v) { key_(key); esc(v); }
	void null(const char *key) { key_(key); s += "null"; }
	void key_(const char *key) { s += '"'; s += key; s += "\":"; }
	void comma() { s += ','; }
};

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
			std::snprintf(cbuf, sizeof(cbuf), "%d,%d,%d", c.R, c.G, c.B);
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
		std::snprintf(cbuf, sizeof(cbuf), "%d,%d,%d,%d", fg.r, fg.g, fg.b, fg.a);
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

// Safe unit lookup by slot id (GetSlotUnit() does no bounds check).
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

	int n = 0;
	for (CUnit *u : Selected) {
		if (u) { SendCommandMove(*u, pos, EFlushMode::On); ++n; }
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
	std::snprintf(cbuf, sizeof(cbuf), "%d,%d,%d", col.R, col.G, col.B);
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

void CrashHandler(int sig)
{
	// Use only low-level, mostly async-signal-safe calls. Best-effort readable dump.
	int fd = open(DbgCrashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		RawWrite(fd, "signal=");
		RawWrite(fd, SignalName(sig));
		RawWrite(fd, " (");
		RawWriteInt(fd, sig);
		RawWrite(fd, ")\nts=");
		RawWriteInt(fd, (long)time(nullptr));
		RawWrite(fd, "\nframeCounter=");
		RawWriteInt(fd, (long)FrameCounter);
		RawWrite(fd, "\ngameCycle=");
		RawWriteInt(fd, (long)GameCycle);
		RawWrite(fd, "\nbacktrace:\n");
#ifdef DBG_HAVE_EXECINFO
		void *frames[64];
		int n = backtrace(frames, 64);
		backtrace_symbols_fd(frames, n, fd);
#else
		RawWrite(fd, "(no execinfo available on this platform)\n");
#endif
		fsync(fd);
		close(fd);
	}
	// Restore default disposition and re-raise so the process still dies normally.
	signal(sig, SIG_DFL);
	raise(sig);
}

void InstallCrashHandlers()
{
	struct sigaction sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sa_handler = CrashHandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
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

} // namespace

/*----------------------------------------------------------------------------
--  Public API
----------------------------------------------------------------------------*/

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

	WriteStatus();
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
}

//@}
