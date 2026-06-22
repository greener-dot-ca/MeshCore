#include "ElementScreen.h"
#include "UITask.h"
#include <Arduino.h>

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif

// small 8x8 "muted" glyph (same data as ui-new icons.h)
static const uint8_t es_muted_icon[] = {
  0x20, 0x6a, 0xea, 0xe4, 0xe4, 0xea, 0x6a, 0x20
};

int ElementScreen::elemTop(int idx) const {
  int y = 0;
  for (int i = 0; i < idx && i < _count; i++) {
    y += _elems[i].height() + SPACING;
  }
  return y;
}

void ElementScreen::ensureFocusVisible() {
  if (_focus < 0) { _scroll_y = 0; return; }
  // scroll up if focus is above the window
  if (elemTop(_focus) < _scroll_y) { _scroll_y = elemTop(_focus); return; }
  // scroll down: smallest start s<=_focus so [s.._focus] fits the viewport
  int s = _focus;
  int used = _elems[_focus].height();
  while (s > 0) {
    int prevH = _elems[s - 1].height() + SPACING;
    if (used + prevH > viewportH()) break;
    used += prevH;
    s--;
  }
  int target = elemTop(s);
  if (target > _scroll_y) _scroll_y = target;
}

void ElementScreen::resetFocus() {
  rebuild();
  _scroll_y = 0;
  _focus = -1;
  for (int i = 0; i < _count; i++) {
    if (_elems[i].selectable()) { _focus = i; break; }
  }
  ensureFocusVisible();
}

void ElementScreen::focusNext() {
  if (_count <= 0) return;
  int base = _focus;
  for (int k = 1; k <= _count; k++) {
    int idx = (((base + k) % _count) + _count) % _count;
    if (_elems[idx].selectable()) { _focus = idx; ensureFocusVisible(); return; }
  }
}

void ElementScreen::focusPrev() {
  if (_count <= 0) return;
  int base = (_focus < 0) ? 0 : _focus;
  for (int k = 1; k <= _count; k++) {
    int idx = (((base - k) % _count) + _count) % _count;
    if (_elems[idx].selectable()) { _focus = idx; ensureFocusVisible(); return; }
  }
}

void ElementScreen::activateFocused() {
  if (_focus >= 0 && _focus < _count) _elems[_focus].activate();
}

bool ElementScreen::handleInput(char c) {
  if (c == KEY_DOWN || c == KEY_SELECT) { focusNext(); return true; }
  if (c == KEY_ENTER) { activateFocused(); return true; }
  return false;
}

int ElementScreen::drawBattery(DisplayDriver& d) {
  uint16_t mv = _task->getBattMilliVolts();
  int pct = ((int)mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  const int iconW = 24, iconH = 10;
  int iconX = d.width() - iconW - 4;
  d.setColor(DisplayDriver::GREEN);
  d.drawRect(iconX, 0, iconW, iconH);
  d.fillRect(iconX + iconW, iconH / 4, 3, iconH / 2);
  d.fillRect(iconX + 2, 2, pct * (iconW - 4) / 100, iconH - 4);

  int left = iconX;
#ifdef PIN_BUZZER
  if (_task->isBuzzerQuiet()) {
    d.setColor(DisplayDriver::RED);
    d.drawXbm(iconX - 11, 1, es_muted_icon, 8, 8);
    left = iconX - 11;
  }
#endif
  return left;
}

void ElementScreen::drawStatusBar(DisplayDriver& d) {
  d.setTextSize(1);
  int left = drawBattery(d);

  d.setColor(DisplayDriver::GREEN);
  char title[40];
  d.translateUTF8ToBlocks(title, _title ? _title : "", sizeof(title));
  d.drawTextEllipsized(0, 0, left - 2, title);

  d.setColor(DisplayDriver::LIGHT);
  d.fillRect(0, STATUS_H - 2, d.width(), 1);   // separator at y=11
}

void ElementScreen::drawPageDots(DisplayDriver& d) {
  int n = pageCount();
  if (n <= 1) return;
  int cur = pageIndex();
  const int spacing = 8;
  int y = USABLE_BOTTOM - 2;
  int x0 = d.width() / 2 - spacing * (n - 1) / 2;
  d.setColor(DisplayDriver::LIGHT);
  for (int i = 0; i < n; i++) {
    int cx = x0 + i * spacing;
    if (i == cur) d.fillRect(cx - 1, y - 1, 3, 3);
    else          d.fillRect(cx, y, 1, 1);
  }
}

void ElementScreen::drawScrollbar(DisplayDriver& d) {
  int top = contentTop(), vp = viewportH(), ch = contentHeight();
  if (ch <= vp) return;
  int x = d.width() - 2;
  d.setColor(DisplayDriver::LIGHT);
  d.drawRect(x, top, 1, vp);
  int thumb_h = vp * vp / ch;
  if (thumb_h < 4) thumb_h = 4;
  if (thumb_h > vp) thumb_h = vp;
  int denom = ch - vp;
  int thumb_y = top + (denom > 0 ? (_scroll_y * (vp - thumb_h) / denom) : 0);
  d.fillRect(x, thumb_y, 2, thumb_h);
}

int ElementScreen::render(DisplayDriver& d) {
  rebuild();
  if (_focus >= _count) _focus = (_count > 0) ? _focus % _count : -1;

  drawStatusBar(d);

  const int top = contentTop();
  const int vp = viewportH();
  const bool hasSB = contentHeight() > vp;
  const int cw = d.width() - (hasSB ? 4 : 0);

  for (int i = 0; i < _count; i++) {
    int et = elemTop(i);
    int eh = _elems[i].height();
    if (et < _scroll_y) continue;            // above viewport
    if (et + eh > _scroll_y + vp) break;      // below viewport (rest won't fit)
    int ey = top + (et - _scroll_y);
    _elems[i].draw(d, 0, ey, cw, _show_focus && i == _focus);
  }

  if (hasSB) drawScrollbar(d);
  drawPageDots(d);

  return 10000;   // no timer-driven repaint (e-ink: only repaint on interaction)
}
