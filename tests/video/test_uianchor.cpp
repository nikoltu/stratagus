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
/**@name test_uianchor.cpp - The test file for uianchor.cpp. */
//
//      (c) Copyright 2026 by The Stratagus Project
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

#include <doctest.h>

#include "uianchor.h"
#include "ui.h"
#include "ui/popup.h"

static bool rectEq(const SDL_Rect &a, const SDL_Rect &b)
{
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

TEST_CASE("anchor scale identity at 1:1 with container == reference")
{
	const int refW = 640, refH = 480;
	SDL_Rect container{ 0, 0, refW, refH };
	const AnchorX axs[] = { AnchorX::Left, AnchorX::Center, AnchorX::Right };
	const AnchorY ays[] = { AnchorY::Top, AnchorY::Center, AnchorY::Bottom };
	const SDL_Rect elems[] = {
		{ 0, 0, 1, 1 }, { 37, 15, 5, 33 }, { 320, 240, 200, 64 }, { 639, 479, 1, 1 },
	};
	for (AnchorX ax : axs)
		for (AnchorY ay : ays)
			for (const SDL_Rect &el : elems) {
				SDL_Rect r = AnchorScaleRect(el, refW, refH, container, ax, ay, 1, 1);
				CHECK(rectEq(r, el));
			}
}

TEST_CASE("right/bottom anchor preserves the scaled far margin")
{
	const int refW = 640, refH = 480;
	SDL_Rect el{ 500, 400, 100, 60 };            // right margin 40, bottom margin 20
	const int rightMargin = refW - el.x - el.w;  // 40
	const int bottomMargin = refH - el.y - el.h; // 20
	const int s = 2;
	SDL_Rect c{ 10, 20, 1280, 960 };
	SDL_Rect r = AnchorScaleRect(el, refW, refH, c, AnchorX::Right, AnchorY::Bottom, s, 1);
	CHECK((c.x + c.w) - (r.x + r.w) == rightMargin * s);
	CHECK((c.y + c.h) - (r.y + r.h) == bottomMargin * s);
	CHECK(r.w == el.w * s);
	CHECK(r.h == el.h * s);
}

TEST_CASE("left/top anchor preserves the scaled near margin")
{
	SDL_Rect el{ 30, 15, 80, 40 };
	SDL_Rect c{ 100, 200, 1280, 960 };
	SDL_Rect r = AnchorScaleRect(el, 640, 480, c, AnchorX::Left, AnchorY::Top, 2, 1);
	CHECK(r.x - c.x == el.x * 2);
	CHECK(r.y - c.y == el.y * 2);
}

TEST_CASE("centered element stays centered as the container grows")
{
	SDL_Rect el{ 270, 210, 100, 60 }; // centred in 640x480
	const SDL_Rect cs[] = { { 0, 0, 640, 480 }, { 0, 0, 800, 600 }, { 5, 7, 1920, 1080 } };
	const int scales[][2] = { { 1, 1 }, { 2, 1 }, { 3, 2 } };
	for (const SDL_Rect &c : cs)
		for (const auto &sc : scales) {
			SDL_Rect r = AnchorScaleRect(el, 640, 480, c, AnchorX::Center, AnchorY::Center, sc[0], sc[1]);
			const int leftGap = r.x - c.x;
			const int rightGap = (c.x + c.w) - (r.x + r.w);
			const int topGap = r.y - c.y;
			const int botGap = (c.y + c.h) - (r.y + r.h);
			CHECK(leftGap >= 0);
			CHECK(rightGap >= 0);
			CHECK(topGap >= 0);
			CHECK(botGap >= 0);
			CHECK(leftGap - rightGap >= -1);
			CHECK(leftGap - rightGap <= 1);
			CHECK(topGap - botGap >= -1);
			CHECK(topGap - botGap <= 1);
		}
}

TEST_CASE("anchor scale rounds sizes and survives degenerate input")
{
	SDL_Rect el{ 10, 10, 7, 3 };
	SDL_Rect r = AnchorScaleRect(el, 640, 480, SDL_Rect{ 0, 0, 640, 480 }, AnchorX::Left, AnchorY::Top, 3, 2);
	CHECK(r.w == 11); // 10.5 -> 11
	CHECK(r.h == 5);  // 4.5 -> 5
	// bad scale / zero reference must not divide by zero.
	AnchorScaleRect(SDL_Rect{ 0, 0, 0, 0 }, 0, 0, SDL_Rect{ 0, 0, 0, 0 }, AnchorX::Center, AnchorY::Center, -5, 0);
	AnchorScaleRect(SDL_Rect{ 0, 0, 0, 0 }, -3, -3, SDL_Rect{ 0, 0, 0, 0 }, AnchorX::Right, AnchorY::Bottom, 1, -2);
}

TEST_CASE("popup_unknown_ident_is_skipped")
{
	// "Show button popups" off: the scripts define no popups, the buttons keep their ident.
	auto saved = std::move(UI.ButtonPopups);
	UI.ButtonPopups.clear();
	CHECK(FindPopupByIdent("popup-x") == nullptr);

	auto popup = std::make_unique<CPopup>();
	popup->Ident = "popup-a";
	CPopup *expected = popup.get();
	UI.ButtonPopups.push_back(std::move(popup));
	CHECK(FindPopupByIdent("popup-a") == expected);

	UI.ButtonPopups = std::move(saved);
}
