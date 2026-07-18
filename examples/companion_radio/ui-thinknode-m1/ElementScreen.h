#pragma once

#include <helpers/ui/UIScreen.h>
#include "UIElements.h"
#include "../NodePrefs.h"

class UITask;

// Time formatting that honors NodePrefs time_format (0=24h,1=12h) + utc_offset_min.
// epoch==0 (clock not yet set) renders as "--:--" / "--". Shared by the status-bar
// clock and the message read-view header.
void uiFormatClock(NodePrefs* p, uint32_t epoch, char* out, size_t n);     // "14:32" / "2:32p"
void uiFormatDateTime(NodePrefs* p, uint32_t epoch, char* out, size_t n);  // "06/22 14:32"

// Status-bar glyphs (8 x ES_ICON_H, MSB-first), shared so the Help screen can
// draw the same icons it explains.
extern const int ES_ICON_H;
extern const uint8_t es_app_icon[];    // companion app connected
extern const uint8_t es_gps_icon[];    // GPS on
extern const uint8_t es_muted_icon[];  // buzzer muted
extern const uint8_t es_bolt_icon[];   // charging

// Shared window chrome so every screen matches -- the centered 12-row grid with a
// double-line ═ rule top & bottom, the page title bookended into a rule, and the
// clock/battery/status cluster embedded in the top rule. Used by ElementScreen pages
// and by the message read view (which isn't an ElementScreen).
namespace uichrome {
  int frameTop();        // y of the top rule (row 0, centered)
  int frameBottom();     // y of the bottom rule (row 11)
  int contentTop();      // y of the first content row (row 1)
  int contentBottom();   // y just past the last content row (== frameBottom)
  // Draw the ═ rule at row `y`, then a left-anchored ╡ title ╞ bookended into it,
  // ellipsized to end by `max_right`. Pass max_right = the status cluster's left x on
  // the top rule (so they never collide), or the panel edge on the bottom rule.
  void rule(DisplayDriver& d, int y, const char* title, int max_right);
  // Same, but a single-line ─ ┤ title ├ rule (the lighter weight, for detail lines).
  void ruleThin(DisplayDriver& d, int y, const char* title, int max_right);
  // Clock/battery/status icons embedded into the right of the top rule; returns the
  // cluster's left x (use as `max_right` for the title).
  int  statusCluster(DisplayDriver& d, UITask* task, NodePrefs* prefs);
  // Bottom ═ rule with the centered page-icon strip embedded (current page reverse-video).
  void bottomBar(DisplayDriver& d, int cur_page, int page_count);
}

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

  // native 200x200 panel on a 16px-row / 8px-column glyph grid (12 rows x 25 cols),
  // vertically centered (uichrome::frameTop/Bottom). Row 0 = top rule (title + status
  // cluster), rows 1-10 = content, row 11 = bottom rule (page icons). See uichrome.
  static const int SPACING = 0;          // gap between elements

  int frameTop()      const { return uichrome::frameTop(); }
  int frameBottom()   const { return uichrome::frameBottom(); }
  int contentTop()    const { return uichrome::contentTop(); }
  int contentBottom() const { return uichrome::contentBottom(); }
  int viewportH()     const { return contentBottom() - contentTop(); }
  int elemTop(int idx) const;            // content-space y of element idx (idx==_count => total height)
  int contentHeight() const { return elemTop(_count); }
  void ensureFocusVisible();             // element-aligned scroll so _focus is fully visible

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

  virtual void resetFocus();        // called when this page becomes current
  void focusNext();         // advance focus to next selectable (wraps)
  void focusPrev();         // move focus to previous selectable (wraps)
  void activateFocused();   // activate the focused element
  // true when the focused element is a multi-option cycle (safe to auto-repeat
  // while the select button is held; Actions/Toggles must never auto-repeat)
  bool focusedIsCycle() const {
    return _focus >= 0 && _focus < _count && _elems[_focus].kind == ElemKind::OptionCycle;
  }
  void setFocusVisible(bool v) { _show_focus = v; }   // hide selection bar while sleeping
};
