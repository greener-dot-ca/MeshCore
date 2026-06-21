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

void UIElement::draw(DisplayDriver& d, int x, int y, int w, bool focused) const {
  const int h = height();

  // selection chrome: full border when focused; a subtle left tick marks an
  // actionable (but unfocused) element so it reads as interactive.
  if (focused) {
    d.setColor(DisplayDriver::LIGHT);
    d.drawRect(x, y, w, h);
  } else if (actionable()) {
    d.setColor(DisplayDriver::LIGHT);
    d.fillRect(x, y + 2, 2, h - 4);
  }

  const int tx = x + 5;
  const int ty = y + UIELEM_PAD;
  d.setColor(DisplayDriver::LIGHT);
  d.setTextSize(1);

  char buf[64];

  // 2-row element (e.g. a message): row1 = label, row2 = get_text() body.
  if (rows >= 2) {
    d.translateUTF8ToBlocks(buf, label ? label : "", sizeof(buf));
    d.drawTextEllipsized(tx, ty, w - 7, buf);
    const char* l2 = get_text ? get_text(*this) : "";
    d.translateUTF8ToBlocks(buf, l2 ? l2 : "", sizeof(buf));
    d.drawTextEllipsized(tx, ty + UIELEM_ROW_H, w - 7, buf);
    return;
  }

  // single-row: caption on the left, value/indicator on the right
  const char* val = nullptr;
  switch (kind) {
    case ElemKind::Toggle:
      val = (get_bool && get_bool(*this)) ? "ON" : "OFF";
      break;
    case ElemKind::Action:
      val = ">";
      break;
    case ElemKind::OptionCycle:
      if (get_text) {
        val = get_text(*this);
      } else if (option_labels && get_opt) {
        int i = get_opt(*this);
        if (i >= 0 && i < option_count) val = option_labels[i];
      }
      break;
    case ElemKind::Label:
      if (get_text) val = get_text(*this);
      break;
  }

  int valw = 0;
  if (val && val[0]) {
    char vbuf[24];
    d.translateUTF8ToBlocks(vbuf, val, sizeof(vbuf));
    valw = d.getTextWidth(vbuf);
    d.drawTextRightAlign(x + w - 3, ty, vbuf);
  }

  d.translateUTF8ToBlocks(buf, label ? label : "", sizeof(buf));
  int lw = (w - 7) - valw - 3;
  if (lw < 1) lw = 1;
  d.drawTextEllipsized(tx, ty, lw, buf);
}
