#include "UIElements.h"

UIElement makeLabel(const char* label, ElemTextGetFn value, void* ctx) {
  UIElement e;
  e.kind = ElemKind::Label;
  e.label = label;
  e.get_text = value;
  e.ctx = ctx;
  return e;
}

UIElement makeToggle(const char* label, void* ctx, ElemBoolGetFn get, ElemActivateFn toggle) {
  UIElement e;
  e.kind = ElemKind::Toggle;
  e.label = label;
  e.ctx = ctx;
  e.get_bool = get;
  e.on_activate = toggle;
  return e;
}

UIElement makeAction(const char* label, void* ctx, ElemActivateFn act) {
  UIElement e;
  e.kind = ElemKind::Action;
  e.label = label;
  e.ctx = ctx;
  e.on_activate = act;
  return e;
}

UIElement makeCycle(const char* label, void* ctx, const char* const* opts, uint8_t n,
                    ElemOptGetFn get, ElemActivateFn advance) {
  UIElement e;
  e.kind = ElemKind::OptionCycle;
  e.label = label;
  e.ctx = ctx;
  e.option_labels = opts;
  e.option_count = n;
  e.get_opt = get;
  e.on_activate = advance;
  return e;
}

UIElement makeCycleText(const char* label, void* ctx, ElemTextGetFn get_text, ElemActivateFn advance) {
  UIElement e;
  e.kind = ElemKind::OptionCycle;   // draw() uses get_text for the value (no option list)
  e.label = label;
  e.ctx = ctx;
  e.get_text = get_text;
  e.on_activate = advance;
  return e;
}

UIElement makeTwoRow(const char* title, void* ctx, ElemTextGetFn body, ElemActivateFn act) {
  UIElement e;
  e.kind = act ? ElemKind::Action : ElemKind::Label;
  e.label = title;
  e.ctx = ctx;
  e.get_text = body;
  e.on_activate = act;
  e.rows = 2;
  return e;
}

UIElement makeMessageRow(const char* line, void* ctx, ElemTextGetFn time, ElemActivateFn act) {
  UIElement e;
  e.kind = ElemKind::Label;   // Label draw = left text + right value, no glyph
  e.label = line;
  e.ctx = ctx;
  e.get_text = time;          // right-aligned relative time
  e.on_activate = act;        // still actionable (opens the read view)
  e.rows = 1;
  return e;
}

// Type glyphs are now real Unifont symbols (NativeEinkDisplay renders UTF-8), so
// they match the 16px text. UTF-8 bytes written explicitly to avoid source-charset
// surprises.
static const char* const SYM_BOX_OFF = "\xE2\x98\x90";  // U+2610 ☐
static const char* const SYM_BOX_ON  = "\xE2\x98\x91";  // U+2611 ☑
static const char* const SYM_PLAY    = "\xE2\x96\xB6";  // U+25B6 ▶
static const char* const SYM_LEFT    = "\xE2\x97\x80";  // U+25C0 ◀

// draw a symbol whose right edge sits at `right`; returns its left x
static int drawSymRight(DisplayDriver& d, int right, int y, const char* sym) {
  int w = d.getTextWidth(sym);
  d.setCursor(right - w, y);
  d.print(sym);
  return right - w;
}

void UIElement::draw(DisplayDriver& d, int x, int y, int w, bool focused) const {
  // Selection is a full-row reverse-video bar (first-gen iPod style); the caller
  // (ElementScreen::render) paints it at full panel width before this runs, so
  // here we just pick the ink colour and lay out the text within `w`.
  const DisplayDriver::Color fg = focused ? DisplayDriver::DARK : DisplayDriver::LIGHT;
  const int ty = y + UIELEM_PAD;
  d.setTextSize(1);

  const int tx = x;            // content starts at the first column
  const int rightX = x + w;    // values right-align to the content's right column boundary (last col = scrollbar)

  char buf[64];

  // 2-row element (e.g. a message): row1 = label, row2 = get_text() body.
  if (rows >= 2) {
    d.setColor(fg);
    d.translateUTF8ToBlocks(buf, label ? label : "", sizeof(buf));
    d.drawTextEllipsized(tx, ty, w, buf);
    const char* l2 = get_text ? get_text(*this) : "";
    d.translateUTF8ToBlocks(buf, l2 ? l2 : "", sizeof(buf));
    d.setColor(fg);
    d.drawTextEllipsized(tx, ty + UIELEM_ROW_H, w, buf);
    return;
  }

  // single-row: caption on the left, a type glyph (+ value) on the right
  int label_right = rightX;

  switch (kind) {
    case ElemKind::Toggle: {
      bool on = get_bool && get_bool(*this);
      d.setColor(fg);
      label_right = drawSymRight(d, rightX, ty, on ? SYM_BOX_ON : SYM_BOX_OFF) - 3;
      break;
    }
    case ElemKind::Action: {
      d.setColor(fg);
      label_right = drawSymRight(d, rightX, ty, SYM_PLAY) - 3;   // ▶ = "run this"
      break;
    }
    case ElemKind::OptionCycle: {
      const char* val = nullptr;
      if (get_text) {
        val = get_text(*this);
      } else if (option_labels && get_opt) {
        int i = get_opt(*this);
        if (i >= 0 && i < option_count) val = option_labels[i];
      }
      char vbuf[24];
      d.translateUTF8ToBlocks(vbuf, (val && val[0]) ? val : "", sizeof(vbuf));
      int vw = d.getTextWidth(vbuf);
      d.setColor(fg);
      int rax = drawSymRight(d, rightX, ty, SYM_PLAY);     // ▶ on the right
      int valx = rax - 2 - vw;
      d.setCursor(valx, ty);
      d.print(vbuf);
      label_right = drawSymRight(d, valx - 2, ty, SYM_LEFT) - 3;   // ◀ left of value
      break;
    }
    case ElemKind::Label: {
      if (get_text) {
        const char* val = get_text(*this);
        if (val && val[0]) {
          char vbuf[24];
          d.translateUTF8ToBlocks(vbuf, val, sizeof(vbuf));
          int vw = d.getTextWidth(vbuf);
          d.setColor(fg);
          d.drawTextRightAlign(rightX, ty, vbuf);
          label_right = rightX - vw - 3;
        }
      }
      break;
    }
  }

  d.setColor(fg);
  d.translateUTF8ToBlocks(buf, label ? label : "", sizeof(buf));
  int lw = label_right - tx;
  if (lw < 1) lw = 1;
  d.drawTextEllipsized(tx, ty, lw, buf);
}
