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
/**@name widgets.h - The widgets headerfile. */
//
//      (c) Copyright 2005-2006 by Fran�ois Beerten and Jimmy Salmon
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

#ifndef __WIDGETS_H__
#define __WIDGETS_H__

#include "font.h"
#include "luacallback.h"

#include <functional>
#include <guisan.hpp>
#include <guisan/sdl.hpp>
#include <guisan/keylistener.hpp>
#include <guisan/mouselistener.hpp>

extern bool GuichanActive;

void initGuichan();
void freeGuichan();
void handleInput(const SDL_Event *event);

/// NG99: true while the guichan widget layer owns input, i.e. a menu is on screen. The touch
/// layer needs it to tell a menu tap from an in-game tap: in a menu the synthetic pointer is not
/// drawn and may be parked off-widget when the finger lifts, in game it is the real cursor and
/// drives build placement.
bool GuichanOwnsInput();

#if USING_TOLUAPP
void addActionListener(gcn::Widget *, gcn::ActionListener *);
void setBackgroundColor(gcn::Widget *, const gcn::Color &);
void setBaseColor(gcn::Widget *, const gcn::Color &color);
void setDirty(gcn::Widget *, bool isDirty);
void setDisabledColor(gcn::Widget *, const gcn::Color &);
void setFont(gcn::Widget *widget, gcn::Font *font);
void setHotKey(gcn::Widget *, const char *key);
void scrollToBottom(gcn::ScrollArea *);
void scrollToTop(gcn::ScrollArea *);
#endif

class LuaActionListener : public gcn::ActionListener, public gcn::KeyListener, public gcn::MouseListener
{
	LuaCallbackImpl callback;
public:
	LuaActionListener(lua_State *lua, lua_Object function);
	~LuaActionListener() override;
	void action(const gcn::ActionEvent &actionEvent) override;
	void keyPressed(gcn::KeyEvent&) override;
	void keyReleased(gcn::KeyEvent&) override;
	void hotKeyPressed(const gcn::Key&) override;
	void hotKeyReleased(const gcn::Key&) override;
	void mouseEntered(gcn::MouseEvent&) override;
	void mouseExited(gcn::MouseEvent&) override;
	void mousePressed(gcn::MouseEvent&) override;
	void mouseReleased(gcn::MouseEvent&) override;
	void mouseClicked(gcn::MouseEvent&) override;
	void mouseWheelMovedUp(gcn::MouseEvent&) override;
	void mouseWheelMovedDown(gcn::MouseEvent&) override;
	void mouseMoved(gcn::MouseEvent&) override;
};

class LambdaActionListener : public gcn::ActionListener
{
	std::function<void(const std::string &)> lambda;
public:
	explicit LambdaActionListener(std::function<void(const std::string &)> l) : lambda(l) {}

	void action(const gcn::ActionEvent &actionEvent) override { lambda(actionEvent.getId()); }
};

class ImageWidget : public gcn::Icon
{
public:
	explicit ImageWidget(gcn::Image *img) : gcn::Icon(img) {} // TODO: Remove (used for Movie in ToLua++)
	explicit ImageWidget(std::shared_ptr<gcn::Image> img) : gcn::Icon(img.get()), img(img) {}

private:
	std::shared_ptr<gcn::Image> img;
};

class ButtonWidget : public gcn::Button
{
public:
	explicit ButtonWidget(const std::string &caption) : Button(caption)
	{
		gcn::Key key = GetHotKey(caption);
		setHotKey(key.getValue());
	}
};

class CImageButton : public gcn::Button
{
public:
	CImageButton();
	explicit CImageButton(const std::string &caption);

	void draw(gcn::Graphics *graphics) override;

	void adjustSize();

	void setNormalImage(std::shared_ptr<gcn::Image> image) { normalImage = image; adjustSize(); }
	void setPressedImage(std::shared_ptr<gcn::Image> image) { pressedImage = image; }
	void setDisabledImage(std::shared_ptr<gcn::Image> image) { disabledImage = image; }
	// Remastered widgets are three-state: Data/skins/skins.json gives every button family a
	// *_hover entry next to its normal and active ones (e.g. fe_campaign button/button_hover/
	// button_active). Without a hover image, hover falls back to the normal art.
	void setHoverImage(std::shared_ptr<gcn::Image> image) { hoverImage = image; }

	std::shared_ptr<gcn::Image> normalImage;
	std::shared_ptr<gcn::Image> pressedImage;
	std::shared_ptr<gcn::Image> disabledImage;
	std::shared_ptr<gcn::Image> hoverImage;
};

class ImageRadioButton : public gcn::RadioButton
{
public:
	ImageRadioButton() = default;
	ImageRadioButton(const std::string &caption, const std::string &group,
					 bool marked);

	void drawBox(gcn::Graphics *graphics) override;
	void draw(gcn::Graphics *graphics) override;
	void mousePressed(gcn::MouseEvent&) override;
	void mouseReleased(gcn::MouseEvent&) override;
	void mouseClicked(gcn::MouseEvent&) override;

	void adjustSize();
	void setUncheckedNormalImage(std::shared_ptr<gcn::Image> image) { uncheckedNormalImage = image; }
	void setUncheckedPressedImage(std::shared_ptr<gcn::Image> image) { uncheckedPressedImage = image; }
	void setUncheckedDisabledImage(std::shared_ptr<gcn::Image> image) { uncheckedDisabledImage = image; }
	void setCheckedNormalImage(std::shared_ptr<gcn::Image> image) { checkedNormalImage = image; }
	void setCheckedPressedImage(std::shared_ptr<gcn::Image> image) { checkedPressedImage = image; }
	void setCheckedDisabledImage(std::shared_ptr<gcn::Image> image) { checkedDisabledImage = image; }

	std::shared_ptr<gcn::Image> uncheckedNormalImage;
	std::shared_ptr<gcn::Image> uncheckedPressedImage;
	std::shared_ptr<gcn::Image> uncheckedDisabledImage;
	std::shared_ptr<gcn::Image> checkedNormalImage;
	std::shared_ptr<gcn::Image> checkedPressedImage;
	std::shared_ptr<gcn::Image> checkedDisabledImage;
	bool mMouseDown = false;
};

class ImageCheckBox : public gcn::CheckBox
{
public:
	ImageCheckBox() = default;
	ImageCheckBox(const std::string &caption, bool marked = false);

	void draw(gcn::Graphics *graphics) override;
	void drawBox(gcn::Graphics *graphics) override;
	void mousePressed(gcn::MouseEvent&) override;
	void mouseReleased(gcn::MouseEvent &) override;
	void mouseClicked(gcn::MouseEvent &) override;

	void adjustSize();
	void setUncheckedNormalImage(std::shared_ptr<gcn::Image> image) { uncheckedNormalImage = image; }
	void setUncheckedPressedImage(std::shared_ptr<gcn::Image> image) { uncheckedPressedImage = image; }
	void setUncheckedDisabledImage(std::shared_ptr<gcn::Image> image) { uncheckedDisabledImage = image; }
	void setCheckedNormalImage(std::shared_ptr<gcn::Image> image) { checkedNormalImage = image; }
	void setCheckedPressedImage(std::shared_ptr<gcn::Image> image) { checkedPressedImage = image; }
	void setCheckedDisabledImage(std::shared_ptr<gcn::Image> image) { checkedDisabledImage = image; }

	std::shared_ptr<gcn::Image> uncheckedNormalImage;
	std::shared_ptr<gcn::Image> uncheckedPressedImage;
	std::shared_ptr<gcn::Image> uncheckedDisabledImage;
	std::shared_ptr<gcn::Image> checkedNormalImage;
	std::shared_ptr<gcn::Image> checkedPressedImage;
	std::shared_ptr<gcn::Image> checkedDisabledImage;
	bool mMouseDown = false;
};

class ImageSlider : public gcn::Slider
{
public:
	explicit ImageSlider(double scaleEnd = 1.0);
	ImageSlider(double scaleStart, double scaleEnd);

	void drawMarker(gcn::Graphics *graphics) override;
	void draw(gcn::Graphics *graphics) override;

	void setMarkerImage(std::shared_ptr<gcn::Image> image);
	void setBackgroundImage(std::shared_ptr<gcn::Image> image);
	void setDisabledBackgroundImage(std::shared_ptr<gcn::Image> image);
	// Remastered slider track. skins.json describes it with COLOURS, not a sprite:
	// widget_slider_black = { color [202,130,28] (the part left of the marker),
	// secondary_color [44,39,33] (the remainder) }; widget_slider_orc/human carry the
	// faction variants. Track thickness is not in the skin data and is measured from the
	// original client's own output (options-sound capture: 9 px at 1440p = 13.5 logical).
	void setTrackColors(int fr, int fg, int fb, int ur, int ug, int ub, int thickness)
	{
		trackFilled = gcn::Color(fr, fg, fb);
		trackUnfilled = gcn::Color(ur, ug, ub);
		trackThickness = thickness;
	}

	std::shared_ptr<gcn::Image> markerImage;
	std::shared_ptr<gcn::Image> backgroundImage;
	std::shared_ptr<gcn::Image> disabledBackgroundImage;
	gcn::Color trackFilled;
	gcn::Color trackUnfilled;
	int trackThickness = 0;
};

class MultiLineLabel : public gcn::Widget
{
public:
	MultiLineLabel() = default;
	explicit MultiLineLabel(const std::string &caption);

	void draw(gcn::Graphics *graphics) override;
	void drawFrame(gcn::Graphics *graphics) override;
	int lineHeight() const;   // NG101: the font's declared line advance, or the cell height

	void setCaption(const std::string &caption);
	const std::string &getCaption() const;
	void setAlignment(gcn::Graphics::Alignment alignment);
	void setAlignment(unsigned int alignment) { setAlignment(static_cast<gcn::Graphics::Alignment>(alignment)); } // TOLUA
	gcn::Graphics::Alignment getAlignment() const;
	void setVerticalAlignment(unsigned int alignment);
	unsigned int getVerticalAlignment() const;
	void setLineWidth(int width);
	int getLineWidth() const;
	void adjustSize();

	enum {
		LEFT = 0,
		CENTER,
		RIGHT,
		TOP,
		BOTTOM
	};

private:
	void wordWrap();

	std::string mCaption;
	std::vector<std::string> mTextRows;
	gcn::Graphics::Alignment mAlignment = gcn::Graphics::Alignment::Left;
	unsigned int mVerticalAlignment = TOP;
	int mLineWidth = 0;
};

class ScrollingWidget : public gcn::ScrollArea
{
public:
	ScrollingWidget(int width, int height);
	void add(gcn::Widget *widget, int x, int y);
	void restart();
	void setSpeed(float speed) { this->speedY = speed; }
	float getSpeed() const { return this->speedY; }
	/// Scroll by the clock instead of by the frame: pixels a second (0 = by the frame, as before).
	void setSpeedPerSecond(float speed) { this->speedPerSecond = speed; }
	/// Stop with the content at this y (<= 0) instead of scrolling it out of sight.
	void setRestY(int y) { this->restY = y; this->hasRest = true; }

private:
	void logic() override;

private:
	gcn::Container container; /// Data container
	float speedY; /// vertical speed of the container (positive number: go up).
	float speedPerSecond = 0.f; /// when > 0: vertical speed in pixels a second, by the clock
	unsigned int lastTicks = 0; /// clock at the last step (0 = not started)
	int restY = 0;              /// where the content comes to rest
	bool hasRest = false;       /// true: stop at restY
	float containerY = 0; /// Y position of the container
	bool finished = false; /// True while scrolling ends.
};

class Windows : public gcn::Window
{
public:
	Windows(const std::string &text, int width, int height);
	void add(gcn::Widget *widget, int x, int y) override;

	void setBackgroundColor(const gcn::Color &color);
	void setBaseColor(const gcn::Color &color);

private:
	void mouseDragged(gcn::MouseEvent &) override;

private:
	gcn::ScrollArea scroll; /// To use scroll bar.
	gcn::Container container; /// data container.
	bool blockwholewindow; /// Manage condition limit of moveable windows. @see mouseMotion.
	/// @todo Method to set this variable. Maybe set the variable static.
};

// Similar to gcn::TextBox but
// - handles clipboard
// - handles UTF-8
// - handles more shortcuts
class CTextBox : public gcn::TextBox
{
public:
	using gcn::TextBox::TextBox;
	void mousePressed(gcn::MouseEvent &) override;
	void keyPressed(gcn::KeyEvent &) override;
};

// Similar to gcn::TextField but
// - handles selection
// - have password mode
// - handles clipboard
// - handles UTF-8
// - handles more shortcuts
class CTextField : public gcn::TextField
{
public:
	CTextField() = default;
	explicit CTextField(const std::string &text) : TextField(text) {}

	void draw(gcn::Graphics *graphics) override;
	void mousePressed(gcn::MouseEvent &) override;
	void mouseDragged(gcn::MouseEvent &) override;
	void keyPressed(gcn::KeyEvent &) override;

	void setPassword(bool flag) { isPassword = flag; }
	void getTextSelectionPositions(unsigned int *first, unsigned int *len) const;

protected:
	int mSelectStart = 0;
	int mSelectEndOffset = 0;
	bool isPassword = false;
};

class ImageTextField : public CTextField
{
public:
	ImageTextField() = default;
	explicit ImageTextField(const std::string &text) : CTextField(text) {}

	void draw(gcn::Graphics *graphics) override;
	void drawFrame(gcn::Graphics *graphics) override;

	void setItemImage(std::shared_ptr<CGraphic> image) { itemImage = image; }

private:
	std::shared_ptr<CGraphic> itemImage;
};

class StringListModel : public gcn::ListModel
{
	std::vector<std::string> list;
public:
	explicit StringListModel(std::vector<std::string> l) : list(l) {}

	int getNumberOfElements() override { return list.size(); }
	std::string getElementAt(int i) override { return list[i]; }

	int getIdxOfElement(std::string_view element);
};

class LuaListModel : public gcn::ListModel
{
	std::vector<std::string> list;
public:
	LuaListModel() = default;

	int getNumberOfElements() override { return list.size(); }
	std::string getElementAt(int i) override { return list[i]; }

	void setList(lua_State *lua, lua_Object *lo);
	int getIdxOfElement(std::string_view element);
};

class ImageListBox : public gcn::ListBox
{
public:
	ImageListBox() = default;
	explicit ImageListBox(gcn::ListModel *listModel);

	void draw(gcn::Graphics *graphics) override;
	void drawFrame(gcn::Graphics *graphics) override;
	void mousePressed(gcn::MouseEvent &) override;
	void mouseMoved(gcn::MouseEvent &) override;    // CF5(b): which row the pointer is over
	void mouseExited(gcn::MouseEvent &) override;
	void mouseDragged(gcn::MouseEvent &) override;  // tracks travel; the container does the scrolling
	void mouseReleased(gcn::MouseEvent &) override; // tap selects / drag suppresses select
	void logic() override;                          // adjustSize()

	// The ROW HEIGHT comes from this image. adjustSize() runs on setList(), i.e. before the art is
	// known, and then sizes the content from the FONT height instead -- so the content stays ~5x too
	// short until the first logic() tick corrects it, and the first frames are drawn clipped to the
	// short content. Re-size here, as soon as the real row art is known.
	void setItemImage(std::shared_ptr<CGraphic> image) { itemImage = image; adjustSize(); }
	// CF5(b): the Remastered skin carries the open-list row plate as THREE frames --
	// dropdown_open_default / _hover / _active -- so the row under the pointer and the row being
	// pressed are real art, not a tint. Unset leaves the single-plate behaviour untouched.
	void setHoverItemImage(std::shared_ptr<CGraphic> image) { hoverItemImage = image; }
	void setActiveItemImage(std::shared_ptr<CGraphic> image) { activeItemImage = image; }
	// Campaign locking: rows with index >= unlockedCount get the grayed row image + padlock icon.
	void setLockedIcon(std::shared_ptr<CGraphic> image) { lockedIcon = image; }
	void setLockedItemImage(std::shared_ptr<CGraphic> image) { lockedItemImage = image; }
	// Selection: the original marks the picked row by drawing every other row dimmed (measured:
	// mission-select-tod-human, brightness ratio 0.51). This is the dimmed variant of the same
	// sprite; when it is not set, rows are not dimmed.
	void setUnselectedItemImage(std::shared_ptr<CGraphic> image) { unselectedItemImage = image; }
	// NG91: the enclosing ImageListBoxWidget tiles the row sprite over its whole viewport, which
	// paints an empty slot for every row the list does NOT have. It needs the real element count to
	// stop at; gcn::ListBox keeps mListModel protected, so expose it here.
	int getNumberOfElements() const
	{
		return mListModel ? mListModel->getNumberOfElements() : 0;
	}
	void setUnlockedCount(int n) { unlockedCount = n; }
	// NG51: row text is CENTRED by default, which is what the mission/save/load lists do. The
	// original's dropdown list is LEFT-aligned with the same inset its closed field uses, so a
	// dropdown sets a pad >= 0 here and gets left-aligned rows.
	void setTextLeftPad(int pad) { textLeftPad = pad; }
	void adjustSize();
	void setSelected(int selected);
	// Public so the owning ImageListBoxWidget can size its scroll-arrow step from the real row art.
	int rowHeight() const;
	// NG94: an explicit row height makes it a layout quantity (logical px through the caller's
	// own scale), and the sprite stretches to it, as the row width does. 0 keeps the height of
	// the item sprite's own pixel size, which does not scale with the display.
	void setRowHeight(int h) { forcedRowHeight = h; adjustSize(); }

private:

	std::shared_ptr<CGraphic> itemImage = nullptr;
	std::shared_ptr<CGraphic> lockedIcon = nullptr;      /// padlock drawn on rows >= unlockedCount
	std::shared_ptr<CGraphic> lockedItemImage = nullptr; /// grayed row image for rows >= unlockedCount
	std::shared_ptr<CGraphic> unselectedItemImage = nullptr; /// dimmed row image for rows != mSelected
	std::shared_ptr<CGraphic> hoverItemImage = nullptr;      /// CF5(b) row under the pointer
	std::shared_ptr<CGraphic> activeItemImage = nullptr;     /// CF5(b) row being pressed
	int mHoverIndex = -1;                                    /// -1 = the pointer is not on a row
	int unlockedCount = -1;
	int forcedRowHeight = 0;   // NG94: 0 = derive from the item sprite                               /// -1 = no locking (all rows unlocked)
	int textLeftPad = -1;   // >= 0 -> left-align row text at this inset (NG51)
	// Selection half of the drag/tap split. The scrolling itself belongs to the enclosing
	// TouchScrollArea, which owns the gesture for every scroll container; this only decides
	// whether the gesture ends as a row selection.
	bool mDragScrolling = false; /// a left press is in progress on the list content
	bool mDidDrag = false;       /// the press moved past the threshold -> suppress the tap select
	int mDragStartY = 0;         /// press Y in scroll-invariant viewport space
	int mDragLastY = 0;          /// previous drag Y, for the plain-ScrollArea parent case

	void setHoverFromY(int y);   /// CF5(b): map a pointer Y to a row index, or -1
};

// Scroll container with finger drag-to-scroll (NG104). gcn::ScrollArea reacts only to its
// arrows, its bar and its marker, so on a touch device everything below the fold of a
// too-tall panel is unreachable. Guisan bubbles every mouse event up the whole parent chain,
// so a container sees the drags that begin on its descendants and can move the content
// without any child widget being involved.
class TouchScrollArea : public gcn::ScrollArea
{
public:
	using gcn::ScrollArea::ScrollArea;

	void mousePressed(gcn::MouseEvent &) override;
	void mouseDragged(gcn::MouseEvent &) override;
	void mouseReleased(gcn::MouseEvent &) override;
	void logic() override;

	/// True once the gesture in progress has travelled far enough to count as a scroll.
	/// Content that acts on release (a list row) uses this to suppress its own activation.
	bool isContentDragged() const { return mDidDrag; }

	// NG130: the Remastered scrollbar art. gcn::ScrollArea draws a plain grey bar, which is what the
	// Preferences column showed while every list next to it used the red skin -- ImageListBoxWidget
	// had these images and a bare ScrollArea had no way to get them. Unset falls back to the base
	// drawing, so a ScrollArea that binds nothing looks exactly as it did.
	void setUpButtonImage(std::shared_ptr<CGraphic> image) { upButtonImage = image; }
	void setDownButtonImage(std::shared_ptr<CGraphic> image) { downButtonImage = image; }
	void setVBarImage(std::shared_ptr<CGraphic> image) { vBarImage = image; }
	void setMarkerImage(std::shared_ptr<CGraphic> image) { markerImage = image; }

	void drawUpButton(gcn::Graphics *) override;
	void drawDownButton(gcn::Graphics *) override;
	void drawVBar(gcn::Graphics *) override;
	void drawVMarker(gcn::Graphics *) override;

	static constexpr int DragThreshold = 6; /// px of travel before a press becomes a scroll
	static constexpr float FlingDecay = 0.88f; /// per-tick momentum decay

private:
	void cancelPendingClick();

private:
	bool mDragging = false;   /// a press is being tracked as a possible content drag
	bool mDidDrag = false;    /// that press has passed DragThreshold
	bool mClickCancelled = false; /// the pending child click was already withdrawn
	int mStartX = 0, mStartY = 0; /// press position, container-relative
	int mLastX = 0, mLastY = 0;   /// previous drag position, container-relative
	float mVelX = 0.f, mVelY = 0.f; /// last per-event delta; seeds the fling
	bool mFling = false;      /// momentum fling active (decays each logic() tick)

	std::shared_ptr<CGraphic> upButtonImage = nullptr;   /// NG130 Remastered scrollbar art
	std::shared_ptr<CGraphic> downButtonImage = nullptr;
	std::shared_ptr<CGraphic> vBarImage = nullptr;
	std::shared_ptr<CGraphic> markerImage = nullptr;
};

class ListBoxWidget : public TouchScrollArea
{
public:
	ListBoxWidget(unsigned int width, unsigned int height);

	void fontChanged() override;

	void setList(lua_State *lua, lua_Object *lo);
	void setSelected(int i);
	int getSelected() const;
	void setBackgroundColor(const gcn::Color &color);
	void addActionListener(gcn::ActionListener *actionListener);

private:
	void adjustSize();

private:
	LuaListModel lualistmodel;
	gcn::ListBox listbox;
};

class ImageListBoxWidget : public ListBoxWidget
{
public:
	ImageListBoxWidget(unsigned int width, unsigned int height);

	void setList(lua_State *lua, lua_Object *lo);
	void setSelected(int i);
	int getSelected() const;
	void setBackgroundColor(const gcn::Color &color);
	// NG108: the row text colour belongs to the inner list box, not to the scroll area around it.
	// Setting it on the wrapper looked like it worked and changed nothing on screen.
	// gcn::Widget::setForegroundColor is NOT virtual, so this cannot be an override and Lua only
	// reaches it because ui.pkg binds it on THIS class -- tolua then dispatches on the concrete
	// type instead of on Widget.
	void setForegroundColor(const gcn::Color &color)
	{
		gcn::Widget::setForegroundColor(color);
		listbox.setForegroundColor(color);
	}
	void addActionListener(gcn::ActionListener *actionListener);
	// Defined out of line (widgets.cpp): setting the row art also fixes the content height, the
	// scrollbar policy and the scroll-arrow step, none of which can be known before the art is.
	void setItemImage(std::shared_ptr<CGraphic> image);
	void setLockedIcon(std::shared_ptr<CGraphic> image) { listbox.setLockedIcon(image); }
	void setLockedItemImage(std::shared_ptr<CGraphic> image) { listbox.setLockedItemImage(image); }
	void setUnselectedItemImage(std::shared_ptr<CGraphic> image) { listbox.setUnselectedItemImage(image); }
	// NG114: the row's hover plate. The list rows are baked from the same atlas family as the
	// dropdown rows, so they take the same treatment CF5(b) gave the dropdown.
	void setHoverItemImage(std::shared_ptr<CGraphic> image) { listbox.setHoverItemImage(image); }
	void setActiveItemImage(std::shared_ptr<CGraphic> image) { listbox.setActiveItemImage(image); }
	void setUnlockedCount(int n) { listbox.setUnlockedCount(n); }
	int getNumberOfElements() const { return listbox.getNumberOfElements(); }   // NG91
	void setRowHeight(int h);                                                   // NG94
	void setUpButtonImage(std::shared_ptr<CGraphic> image) { upButtonImage = image; }
	void setUpPressedButtonImage(std::shared_ptr<CGraphic> image) { upPressedButtonImage = image; }
	void setDownButtonImage(std::shared_ptr<CGraphic> image) { downButtonImage = image; }
	void setDownPressedButtonImage(std::shared_ptr<CGraphic> image)
	{
		downPressedButtonImage = image;
	}
	void setLeftButtonImage(std::shared_ptr<CGraphic> image) { leftButtonImage = image; }
	void setLeftPressedButtonImage(std::shared_ptr<CGraphic> image)
	{
		leftPressedButtonImage = image;
	}
	void setRightButtonImage(std::shared_ptr<CGraphic> image) { rightButtonImage = image; }
	void setRightPressedButtonImage(std::shared_ptr<CGraphic> image)
	{
		rightPressedButtonImage = image;
	}
	void setHBarImage(std::shared_ptr<CGraphic> image)
	{
		hBarButtonImage = image;
		mScrollbarWidth = std::min<int>(image->getWidth(), image->getHeight());
	}
	void setVBarImage(std::shared_ptr<CGraphic> image)
	{
		vBarButtonImage = image;
		mScrollbarWidth = std::min<int>(image->getWidth(), image->getHeight());
	}
	void setMarkerImage(std::shared_ptr<CGraphic> image) { markerImage = image; }
	gcn::Rectangle getVerticalMarkerDimension();
	gcn::Rectangle getHorizontalMarkerDimension();

	void draw(gcn::Graphics *graphics) override;
	void drawFrame(gcn::Graphics *graphics) override;
	void fontChanged() override;

private:
	void drawUpButton(gcn::Graphics *graphics) override;
	void drawDownButton(gcn::Graphics *graphics) override;
	void drawLeftButton(gcn::Graphics *graphics) override;
	void drawRightButton(gcn::Graphics *graphics) override;
	void drawHMarker(gcn::Graphics *graphics) override;
	void drawVMarker(gcn::Graphics *graphics) override;
	void drawHBar(gcn::Graphics *graphics) override;
	void drawVBar(gcn::Graphics *graphics) override;

	void drawUpPressedButton(gcn::Graphics *graphics);
	void drawDownPressedButton(gcn::Graphics *graphics);
	void drawLeftPressedButton(gcn::Graphics *graphics);
	void drawRightPressedButton(gcn::Graphics *graphics);
	void adjustSize();
private:
	std::shared_ptr<CGraphic> itemImage;
	std::shared_ptr<CGraphic> upButtonImage;
	std::shared_ptr<CGraphic> upPressedButtonImage;
	std::shared_ptr<CGraphic> downButtonImage;
	std::shared_ptr<CGraphic> downPressedButtonImage;
	std::shared_ptr<CGraphic> leftButtonImage;
	std::shared_ptr<CGraphic> leftPressedButtonImage;
	std::shared_ptr<CGraphic> rightButtonImage;
	std::shared_ptr<CGraphic> rightPressedButtonImage;
	std::shared_ptr<CGraphic> hBarButtonImage;
	std::shared_ptr<CGraphic> vBarButtonImage;
	std::shared_ptr<CGraphic> markerImage;

	LuaListModel lualistmodel;
	ImageListBox listbox;
};

class DropDownWidget : public gcn::DropDown
{
	DropDownWidget(std::unique_ptr<LuaListModel> listmodel, gcn::ListBox *listBox) :
		gcn::DropDown(listmodel.get(), nullptr, listBox),
		mListModel(std::move(listmodel))
	{}

public:
	explicit DropDownWidget(gcn::ListBox *listBox = nullptr) :
		DropDownWidget(std::make_unique<LuaListModel>(), listBox)
	{}

	void setList(lua_State *lua, lua_Object *lo);
	void setSize(int width, int height);
protected:
	std::unique_ptr<LuaListModel> mListModel;
};

class ImageDropDownWidget : public DropDownWidget
{
private:
	explicit ImageDropDownWidget(std::unique_ptr<ImageListBox> imageListBox) :
		DropDownWidget(imageListBox.get()),
		mImageListBox(std::move(imageListBox))
	{
		this->mListBox->addActionListener(this);
	}

public:
	ImageDropDownWidget() : ImageDropDownWidget(std::make_unique<ImageListBox>()) {}
	void setItemImage(std::shared_ptr<CGraphic> image)
	{
		this->itemImage = image;
		this->mImageListBox->setItemImage(image);
	}
	// CF5(b): the open list's hover and pressed plates. Only the list needs them -- the closed field
	// draws itemImage and has its own closed_hover / closed_active art.
	void setHoverItemImage(std::shared_ptr<CGraphic> image) { mImageListBox->setHoverItemImage(image); }
	void setActiveItemImage(std::shared_ptr<CGraphic> image) { mImageListBox->setActiveItemImage(image); }
	void setDownNormalImage(std::shared_ptr<CGraphic> image) { DownNormalImage = image; }
	void setDownPressedImage(std::shared_ptr<CGraphic> image) { DownPressedImage = image; }

	void draw(gcn::Graphics *graphics) override;
	void drawFrame(gcn::Graphics *graphics) override;
	void drawButton(gcn::Graphics *graphics) override;
	void setList(lua_State *lua, lua_Object *lo);
	void setSize(int width, int height);
	std::string getSelectedItem();
	int setSelectedItem(lua_State *lua, lua_Object *lo);
	void adjustHeight() override;
	void setFont(gcn::Font *font);

private:
	std::shared_ptr<CGraphic> itemImage;
	std::shared_ptr<CGraphic> DownNormalImage;
	std::shared_ptr<CGraphic> DownPressedImage;
	std::unique_ptr<ImageListBox> mImageListBox;
	// Explicit closed-field height set via setSize(). Kept separate so re-listing (which calls
	// adjustHeight) can restore the intended tall beveled-button height instead of collapsing
	// to font height.
	int mFieldHeight = 0;
};

class StatBoxWidget : public gcn::Widget
{
public:
	StatBoxWidget(int width, int height);

	void draw(gcn::Graphics *graphics) override;

	void setCaption(const std::string &s);
	const std::string &getCaption() const;
	void setPercent(const int percent);
	int getPercent() const;

private:
	std::string caption;  /// caption of the widget.
	unsigned int percent; /// percent value of the widget.
};

class MenuScreen : public gcn::Container
{
public:
	MenuScreen();

	void draw(gcn::Graphics *graphics) override;
	void logic() override;

	int run(bool loop = true);
	void stop(int result = 0, bool stopAll = false);
	void stopAll(int result = 0) { stop(result, true); }
	void addLogicCallback(LuaActionListener *listener);
	void setDrawMenusUnder(bool drawUnder) { this->drawUnder = drawUnder; }
	bool getDrawMenusUnder() const { return this->drawUnder; }

private:
	bool runLoop = true;
	int loopResult = 0;
	gcn::Widget *oldtop = nullptr;
	LuaActionListener *logiclistener = nullptr;
	bool drawUnder = false;
};

#endif
