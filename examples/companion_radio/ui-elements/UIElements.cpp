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

// ---- type glyphs (drawn with primitives; unicode can't be used because
// translateUTF8ToBlocks would turn it into a block char) ----

// checkbox: empty when off, filled when on
static void glyphCheckbox(DisplayDriver& d, int x, int y, bool on) {
  d.drawRect(x, y, 9, 9);
  if (on) d.fillRect(x + 2, y + 2, 5, 5);
}
// filled triangle pointing right (vertical base at left, apex at right)
static void glyphTriRight(DisplayDriver& d, int x, int y, int s) {
  for (int r = 0; r < s; r++) {
    int dist = (r < s / 2) ? (s / 2 - r) : (r - s / 2);
    int len = (s / 2 - dist) + 1;
    d.fillRect(x, y + r, len, 1);
  }
}
// filled triangle pointing left (vertical base at right, apex at left)
static void glyphTriLeft(DisplayDriver& d, int x, int y, int s) {
  int base = x + s / 2;
  for (int r = 0; r < s; r++) {
    int dist = (r < s / 2) ? (s / 2 - r) : (r - s / 2);
    int len = (s / 2 - dist) + 1;
    d.fillRect(base - (len - 1), y + r, len, 1);
  }
}
// filled play triangle (8x9, base left, apex right). Drawn as an XBM so the
// e-ink scaler fills the inter-pixel gaps (stacked 1px fillRects would stripe).
static void glyphPlay(DisplayDriver& d, int x, int y) {
  static const uint8_t play_icon[] = {
    0x80, 0xC0, 0xF0, 0xFC, 0xFF, 0xFC, 0xF0, 0xC0, 0x80
  };
  d.drawXbm(x, y, play_icon, 8, 9);
}

void UIElement::draw(DisplayDriver& d, int x, int y, int w, bool focused) const {
  const int h = height();

  // selection = reverse video: fill the whole row, draw contents in the
  // background colour. Compact (no extra padding) and immune to descender clip.
  DisplayDriver::Color fg = DisplayDriver::LIGHT;
  if (focused) {
    d.setColor(DisplayDriver::LIGHT);
    d.fillRect(x, y, w, h);
    fg = DisplayDriver::DARK;
  }

  const int tx = x;            // flush to the left edge, same as the status-bar title
  const int ty = y + UIELEM_PAD;
  const int rightX = x + w - 3;
  d.setTextSize(1);

  char buf[64];

  // 2-row element (e.g. a message): row1 = label, row2 = get_text() body.
  if (rows >= 2) {
    d.setColor(fg);
    d.translateUTF8ToBlocks(buf, label ? label : "", sizeof(buf));
    d.drawTextEllipsized(tx, ty, w - 7, buf);
    const char* l2 = get_text ? get_text(*this) : "";
    d.translateUTF8ToBlocks(buf, l2 ? l2 : "", sizeof(buf));
    d.setColor(fg);
    d.drawTextEllipsized(tx, ty + UIELEM_ROW_H, w - 7, buf);
    return;
  }

  // single-row: caption on the left, a type glyph (+ value) on the right
  int label_right = rightX;
  const int ts = 7;   // triangle size

  switch (kind) {
    case ElemKind::Toggle: {
      bool on = get_bool && get_bool(*this);
      d.setColor(fg);
      glyphCheckbox(d, rightX - 9, ty, on);
      label_right = rightX - 9 - 3;
      break;
    }
    case ElemKind::Action: {
      d.setColor(fg);
      glyphPlay(d, rightX - 8, ty);   // play/▶ = "run this"
      label_right = rightX - 8 - 3;
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
      glyphTriRight(d, rightX - ts, ty + 1, ts);          // ►
      int valx = rightX - ts - 2 - vw;
      d.setColor(fg);
      d.setCursor(valx, ty);
      d.print(vbuf);
      glyphTriLeft(d, valx - 2 - ts, ty + 1, ts);         // ◄
      label_right = valx - 2 - ts - 3;
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
