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
/**@name popup.h - The Popup header file. */
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

#ifndef __POPUP_H__
#define __POPUP_H__

//@{

#include "color.h"
#include "script.h"
#include "vec2i.h"
#include "video.h"

#include <optional>
#include <string>
#include <vector>

/*----------------------------------------------------------------------------
--  Declarations
----------------------------------------------------------------------------*/

class ButtonAction;
class CFont;
class CPopup;
enum class ButtonCmd;
enum class ECondition;

#define MARGIN_X 4
#define MARGIN_Y 2

class PopupConditionPanel
{
public:
	PopupConditionPanel() = default;
	~PopupConditionPanel() = default;

	bool HasHint = false;         /// check if button has hint.
	bool HasDescription = false;  /// check if button has description.
	bool HasDependencies = false; /// check if button has dependencies or restrictions.
	std::optional<ButtonCmd> ButtonAction; /// action type of button
	std::string ButtonValue;    /// value used in ValueStr field of button

	std::vector<ECondition> BoolFlags; /// array of condition about user flags.
	std::vector<ECondition> Variables; /// array of variable to verify (enable and max > 0)
};

class CPopupContentType
{
public:
	CPopupContentType() = default;
	virtual ~CPopupContentType() = default;

	/// Tell how show the variable Index.
	virtual void Draw(int x, int y, const CPopup &popup, const unsigned int popupWidth, const ButtonAction &button, int *Costs) const = 0;
	/// Get the content's width
	virtual int GetWidth(const ButtonAction &button, int *Costs) const = 0;
	/// Get the content's height
	virtual int GetHeight(const ButtonAction &button, int *Costs) const = 0;

	virtual void Parse(lua_State *l) = 0;

	static std::unique_ptr<CPopupContentType> ParsePopupContent(lua_State *l);

public:
	PixelPos pos{0, 0}; /// position to draw.

	int MarginX = MARGIN_X;  /// Left and right margin width.
	int MarginY = MARGIN_Y;  /// Upper and lower margin height.
	PixelSize minSize{0, 0}; /// Minimal size covered by content type.
	bool Wrap = true;        /// If true, the next content will be placed on the next "line".
protected:
	std::string TextColor;      /// Color used for plain text in content.
	std::string HighlightColor; /// Color used for highlighted letters.
public:
	std::unique_ptr<PopupConditionPanel> Condition; /// Condition to show the content; if nullptr, no condition.
};

enum class EPopupButtonInfo {
	Hint,
	Description,
	Dependencies
};

class CPopupContentTypeButtonInfo : public CPopupContentType
{
public:
	CPopupContentTypeButtonInfo() = default;
	~CPopupContentTypeButtonInfo() override = default;

	void Draw(int x,
	          int y,
	          const CPopup &popup,
	          const unsigned int popupWidth,
	          const ButtonAction &button,
	          int *Costs) const override;

	int GetWidth(const ButtonAction &button, int *Costs) const override;
	int GetHeight(const ButtonAction &button, int *Costs) const override;

	void Parse(lua_State *l) override;

private:
	EPopupButtonInfo InfoType = EPopupButtonInfo::Hint; /// Type of information to show.
	unsigned int MaxWidth = 0; /// Maximum width of multi-lined information.
	CFont *Font = nullptr;     /// Font to use.
};

class CPopupContentTypeText : public CPopupContentType
{
public:
	CPopupContentTypeText() = default;
	~CPopupContentTypeText() override = default;

	void Draw(int x,
	          int y,
	          const CPopup &popup,
	          const unsigned int popupWidth,
	          const ButtonAction &button,
	          int *Costs) const override;

	int GetWidth(const ButtonAction &button, int *Costs) const override;
	int GetHeight(const ButtonAction &button, int *Costs) const override;

	void Parse(lua_State *l) override;

private:
	std::string Text;          /// Text to display
	unsigned int MaxWidth = 0; /// Maximum width of multi-lined text.
	CFont *Font = nullptr;     /// Font to use.
};

class CPopupContentTypeCosts : public CPopupContentType
{
public:
	CPopupContentTypeCosts() = default;
	~CPopupContentTypeCosts() override = default;

	void Draw(int x,
	          int y,
	          const CPopup &popup,
	          const unsigned int popupWidth,
	          const ButtonAction &button,
	          int *Costs) const override;

	int GetWidth(const ButtonAction &button, int *Costs) const override;
	int GetHeight(const ButtonAction &button, int *Costs) const override;

	void Parse(lua_State *l) override;

private:
	CFont *Font = nullptr; /// Font to use.
	bool Centered = false;     /// if true, center the display.
};

class CPopupContentTypeLine : public CPopupContentType
{
public:
	CPopupContentTypeLine() = default;
	~CPopupContentTypeLine() override = default;

	void Draw(int x,
	          int y,
	          const CPopup &popup,
	          const unsigned int popupWidth,
	          const ButtonAction &button,
	          int *Costs) const override;

	int GetWidth(const ButtonAction &button, int *Costs) const override;
	int GetHeight(const ButtonAction &button, int *Costs) const override;

	void Parse(lua_State *l) override;

private:
	IntColor Color = ColorWhite; /// Color used for line.
	unsigned int Width = 0;      /// line height
	unsigned int Height = 1;     /// line height
};

class CPopupContentTypeVariable : public CPopupContentType
{
public:
	CPopupContentTypeVariable() = default;

	void Draw(int x,
	          int y,
	          const CPopup &popup,
	          const unsigned int popupWidth,
	          const ButtonAction &button,
	          int *Costs) const override;

	int GetWidth(const ButtonAction &button, int *Costs) const override;
	int GetHeight(const ButtonAction &button, int *Costs) const override;

	void Parse(lua_State *l) override;

private:
	std::unique_ptr<IStringDesc> Text; /// Text to display.
	CFont *Font = nullptr; /// Font to use.
	bool Centered = false; /// if true, center the display.
	int Index = -1;        /// Index of the variable to show, -1 if not.
};

class CPopup
{
public:
	CPopup() = default;
	~CPopup() = default;

	std::vector<std::unique_ptr<CPopupContentType>> Contents; /// Array of contents to display.
	std::string Ident;                         /// Ident of the popup.
	int MarginX = MARGIN_X;                    /// Left and right margin width.
	int MarginY = MARGIN_Y;                    /// Upper and lower margin height.
	int MinWidth = 0;                          /// Minimal width covered by popup.
	int MinHeight = 0;                         /// Minimal height covered by popup.
	CFont *DefaultFont = nullptr;              /// Default font for content.
	IntColor BackgroundColor = ColorBlue;      /// Color used for popup's background.
	IntColor BorderColor = ColorWhite;         /// Color used for popup's borders.
	/// IG-T1: the Remastered draws its tooltip as a nine-sliced sprite (`hud_tooltip` ->
	/// `tooltip_frame`, 9slice 70, scale 5), not as a flat rectangle, and it puts it at a FIXED
	/// place -- the bottom-left of the map area -- instead of next to the button. When
	/// BackgroundImage is set it replaces the flat fill; when FixedX >= 0 the popup is drawn there.
	std::shared_ptr<CGraphic> BackgroundImage;
	int BackgroundBorder = 0;                  /// nine-slice border of BackgroundImage, in its own pixels
	int FixedX = -1;                           /// LOGICAL x (3840x2160 space), -1 = follow the button
	int FixedY = -1;
	int FixedHeight = 0;                       /// forced LOGICAL height, 0 = size to the contents
	/// HUD-020: true = the original's fixed tooltip layout (DrawRemasteredTooltip); Contents,
	/// Margin, MinWidth, MinHeight and the Fixed* fields are then not used.
	bool RemasteredLayout = false;
};

/// HUD-020: draw the command tooltip the way the original lays it out (Oracle
/// `tooltip_layout_contract`). `costs` is indexed like CUnitStats::Costs plus ManaResCost;
/// `allowed` is false when the button is drawn greyed.
extern void DrawRemasteredTooltip(const CPopup &popup, const ButtonAction &button,
								  const int *costs, bool allowed);

/// The three texts DrawRemasteredTooltip draws for `button`: the title, the body and, for a
/// greyed button (`allowed` false), the requirement line. Returns the id of the string record
/// the button's target resolved to, -1 when it has none (a plain command, or no counterpart).
extern int GetRemasteredTooltipTexts(const ButtonAction &button, bool allowed, std::string &title,
									 std::string &body, std::string &requirement);


//@}

#endif // !__UI_H__
