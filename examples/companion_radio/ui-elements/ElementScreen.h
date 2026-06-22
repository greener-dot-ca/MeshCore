#pragma once

#include <helpers/ui/UIScreen.h>
#include "UIElements.h"
#include "../NodePrefs.h"

class UITask;

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

  static const int STATUS_H      = 13;   // top status-bar height
  static const int DOTS_H        = 3;    // bottom page-dot strip
  static const int USABLE_BOTTOM = 126;  // e-ink usable logical height (~2px panel margin)
  static const int SPACING       = 0;    // gap between elements

  int contentTop()    const { return STATUS_H + 1; }
  int contentBottom() const { return USABLE_BOTTOM - DOTS_H; }
  int viewportH()     const { return contentBottom() - contentTop(); }
  int elemTop(int idx) const;            // content-space y of element idx (idx==_count => total height)
  int contentHeight() const { return elemTop(_count); }
  void ensureFocusVisible();             // element-aligned scroll so _focus is fully visible

  int  drawBattery(DisplayDriver& d);    // returns left x consumed (for title ellipsis)
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
