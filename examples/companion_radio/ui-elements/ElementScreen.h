#pragma once

#include <helpers/ui/UIScreen.h>
#include "UIElements.h"
#include "../NodePrefs.h"

class UITask;

// Time formatting that honors NodePrefs time_format (0=24h,1=12h) + utc_offset_min.
// epoch==0 (clock not yet set) renders as "--:--" / "--". Shared by the status-bar
// clock and the message read-view header.
void uiFormatClock(NodePrefs* p, uint32_t epoch, char* out, size_t n);     // "14:32" / "2:32p"
void uiFormatDateTime(NodePrefs* p, uint32_t epoch, char* out, size_t n);  // "Jun 22 14:32"

// Status-bar glyphs (8 x ES_ICON_H, MSB-first), shared so the Help screen can
// draw the same icons it explains.
extern const int ES_ICON_H;
extern const uint8_t es_app_icon[];    // companion app connected
extern const uint8_t es_gps_icon[];    // GPS on
extern const uint8_t es_muted_icon[];  // buzzer muted
extern const uint8_t es_bolt_icon[];   // charging

// A scrollable, element-based page. Subclasses own a fixed UIElement[] and
// point _elems/_count at it (in their ctor, or in rebuild() for dynamic lists).
// Shared chrome: top status bar, bottom page-dots, right scrollbar + more-arrows.
//
// All drawing is deterministic (depends only on state), and render() returns a
// long delay so the e-ink panel only repaints when a real interaction sets
// _next_refresh — never on a timer. Do NOT add per-frame-changing pixels.
class ElementScreen : public UIScreen {
protected:
  UITask*     _task;
  NodePrefs*  _prefs;
  const char* _title;
  UIElement*  _elems;
  int         _count;
  int         _focus;       // index of focused selectable element, or -1
  int         _scroll_y;    // pixel scroll offset into the content column
  bool        _show_focus;  // draw the selection bar (false = screen is sleeping)

  // native 200x200 panel geometry (NativeEinkDisplay renders 1:1, 16px Unifont)
  static const int STATUS_H      = 20;   // top status-bar height (16px text + separator)
  static const int DOTS_H        = 16;   // bottom page-dot strip (fits a 16px ●/○ glyph)
  static const int USABLE_BOTTOM = 198;  // e-ink usable height (~2px panel margin)
  static const int SPACING       = 0;    // gap between elements

  int contentTop()    const { return STATUS_H + 1; }
  int contentBottom() const { return USABLE_BOTTOM - DOTS_H; }
  int viewportH()     const { return contentBottom() - contentTop(); }
  int elemTop(int idx) const;            // content-space y of element idx (idx==_count => total height)
  int contentHeight() const { return elemTop(_count); }
  void ensureFocusVisible();             // element-aligned scroll so _focus is fully visible

  int  drawBattery(DisplayDriver& d, int right);   // battery ends at x==right; returns its left x
  void drawStatusBar(DisplayDriver& d);
  void drawPageDots(DisplayDriver& d);
  void drawScrollbar(DisplayDriver& d);

  virtual void rebuild() {}              // refresh dynamic element set before render
  virtual int  pageIndex() const = 0;
  virtual int  pageCount() const = 0;

public:
  ElementScreen(UITask* task, NodePrefs* prefs, const char* title)
    : _task(task), _prefs(prefs), _title(title), _elems(nullptr),
      _count(0), _focus(-1), _scroll_y(0), _show_focus(true) {}

  int  render(DisplayDriver& display) override;
  bool handleInput(char c) override;

  void resetFocus();        // called when this page becomes current
  void focusNext();         // advance focus to next selectable (wraps)
  void focusPrev();         // move focus to previous selectable (wraps)
  void activateFocused();   // activate the focused element
  void setFocusVisible(bool v) { _show_focus = v; }   // hide selection bar while sleeping
};
