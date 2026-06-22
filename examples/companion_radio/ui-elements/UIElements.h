#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <stdint.h>

// Per-element layout (logical px). One element occupies `rows` text rows
// plus a small padding above/below for the selection border.
#define UIELEM_ROW_H  11
#define UIELEM_PAD     2

enum class ElemKind : uint8_t { Label, Toggle, Action, OptionCycle };

struct UIElement;

// All callbacks are plain C function pointers (no std::function / no heap).
// State lives in the bound `ctx`, never in the element, so everything is const.
typedef void        (*ElemActivateFn)(const UIElement& e);  // Action: do it; Toggle: flip; Cycle: advance
typedef bool        (*ElemBoolGetFn) (const UIElement& e);  // Toggle: current on/off state
typedef const char* (*ElemTextGetFn) (const UIElement& e);  // dynamic value text (row2 body for 2-row elements)
typedef int         (*ElemOptGetFn)  (const UIElement& e);  // OptionCycle: current index

// A single self-describing UI element. Screens own a fixed array of these.
struct UIElement {
  ElemKind           kind          = ElemKind::Label;
  const char*        label         = nullptr;   // static caption (left), or row1 text for 2-row elements
  void*              ctx           = nullptr;   // bound live-data / UITask pointer
  ElemActivateFn     on_activate   = nullptr;   // NULL => not actionable
  ElemBoolGetFn      get_bool      = nullptr;   // Toggle only
  ElemTextGetFn      get_text      = nullptr;   // dynamic value (single row) / row2 body (2-row)
  ElemOptGetFn       get_opt       = nullptr;   // OptionCycle only
  const char* const* option_labels = nullptr;   // OptionCycle option names
  uint8_t            option_count  = 0;
  uint8_t            rows          = 1;         // text rows (messages use 2)

  // every element is focusable (so btn2 can scroll through a tall page and
  // reach the actionable element at the bottom); only some are actionable.
  bool selectable() const { return true; }
  bool actionable() const { return on_activate != nullptr; }
  int  height() const { return rows * UIELEM_ROW_H + 2 * UIELEM_PAD; }
  void draw(DisplayDriver& d, int x, int y, int w, bool focused) const;
  void activate() const { if (on_activate) on_activate(*this); }
};

// Terse construction helpers (default member initializers cover the rest).
UIElement makeLabel (const char* label, ElemTextGetFn value = nullptr, void* ctx = nullptr);
UIElement makeToggle(const char* label, void* ctx, ElemBoolGetFn get, ElemActivateFn toggle);
UIElement makeAction(const char* label, void* ctx, ElemActivateFn act);
UIElement makeCycle (const char* label, void* ctx, const char* const* opts, uint8_t n,
                     ElemOptGetFn get, ElemActivateFn advance);
// A 2-row read-only element: row1 = title (stable buffer), row2 = body via get_text.
UIElement makeTwoRow(const char* title, void* ctx, ElemTextGetFn body, ElemActivateFn act = nullptr);
// A 1-row message row: `line` left (ellipsized), `time` right-aligned via get_text,
// activatable to open the read view. Uses the Label draw path (no glyph).
UIElement makeMessageRow(const char* line, void* ctx, ElemTextGetFn time, ElemActivateFn act);
