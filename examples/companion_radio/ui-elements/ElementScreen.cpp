#include "ElementScreen.h"
#include "UITask.h"
#include "target.h"   // board.isExternalPowered()
#include <Arduino.h>
#include <RTClib.h>   // DateTime for the status-bar clock

// ---- shared time formatting (honors time_format + utc_offset_min) ----
static void fmtTimePart(NodePrefs* p, const DateTime& dt, char* out, size_t n) {
  int h = dt.hour(), m = dt.minute();
  if (p && p->time_format == 1) {            // 12-hour
    int h12 = h % 12; if (h12 == 0) h12 = 12;
    snprintf(out, n, "%d:%02d%c", h12, m, h < 12 ? 'a' : 'p');
  } else {                                   // 24-hour
    snprintf(out, n, "%02d:%02d", h, m);
  }
}

void uiFormatClock(NodePrefs* p, uint32_t epoch, char* out, size_t n) {
  if (epoch == 0) { strncpy(out, "--:--", n); out[n - 1] = 0; return; }
  uint32_t local = (uint32_t)((int64_t)epoch + (int64_t)(p ? p->utc_offset_min : 0) * 60);
  DateTime dt(local);
  fmtTimePart(p, dt, out, n);
}

void uiFormatDateTime(NodePrefs* p, uint32_t epoch, char* out, size_t n) {
  if (epoch == 0) { strncpy(out, "--", n); out[n - 1] = 0; return; }
  static const char* const mon[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
  uint32_t local = (uint32_t)((int64_t)epoch + (int64_t)(p ? p->utc_offset_min : 0) * 60);
  DateTime dt(local);
  int mo = dt.month();
  char t[12];
  fmtTimePart(p, dt, t, sizeof(t));
  snprintf(out, n, "%s %d %s", (mo >= 1 && mo <= 12) ? mon[mo - 1] : "?", dt.day(), t);
}

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif

// 8x9 status glyphs, sized to the size-1 font cap height so they read as the same
// height as the title text. Bit 0x80 = leftmost pixel (MSB-first, matching drawXbm).
// Drawn full-bleed/bold for high contrast on e-ink; tweak a row by editing its bits.
const int ES_ICON_H = 9;                        // glyph height (≈ font caps)
const uint8_t es_muted_icon[] = {               // speaker + small X (muted)
  0b00010000,
  0b00110000,
  0b01110000,
  0b11110101,
  0b11110010,
  0b11110101,
  0b01110000,
  0b00110000,
  0b00010000,
};
const uint8_t es_app_icon[] = {                 // smartphone (companion app connected)
  0b01111110,
  0b01000010,
  0b01000010,
  0b01000010,
  0b01000010,
  0b01000010,
  0b01000010,
  0b01011010,   // home button
  0b01111110,
};
const uint8_t es_gps_icon[] = {                 // navigation arrow (locator cursor)
  0b00011000,
  0b00011000,
  0b00111100,
  0b00111100,
  0b01111110,
  0b01111110,
  0b11100111,
  0b11000011,
  0b00000000,
};
const uint8_t es_bolt_icon[] = {                // charging lightning bolt
  0b00001110,
  0b00011100,
  0b00111000,
  0b01111110,
  0b00001110,
  0b00011100,
  0b00111000,
  0b00110000,
  0b01100000,
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

// Draws a small battery whose terminal nub ends at x==`right`; returns its left x
// (or the bolt's left x when charging).
int ElementScreen::drawBattery(DisplayDriver& d, int right) {
  uint16_t mv = _task->getBattMilliVolts();
  int pct = ((int)mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  const int iconW = 14, iconH = ES_ICON_H, y = 1;
  int x = right - 2 - iconW;            // 2px for the terminal nub at `right`
  d.setColor(DisplayDriver::GREEN);
  d.drawRect(x, y, iconW, iconH);
  d.fillRect(x + iconW, y + iconH / 4, 2, iconH / 2);          // terminal nub
  d.fillRect(x + 2, y + 2, pct * (iconW - 4) / 100, iconH - 4); // fill

  int left = x;
  if (board.isExternalPowered()) {     // charging: lightning bolt left of battery
    d.drawXbm(x - 9, y, es_bolt_icon, 8, ES_ICON_H);
    left = x - 9;
  }
  return left;
}

void ElementScreen::drawStatusBar(DisplayDriver& d) {
  d.setTextSize(1);

  // right side: clock (rightmost), then battery (+charge bolt) to its left
  char clk[12];
  uiFormatClock(_prefs, _task->currentEpoch(), clk, sizeof(clk));
  int cw = d.getTextWidth(clk);
  d.setColor(DisplayDriver::LIGHT);
  d.drawTextRightAlign(d.width(), 0, clk);
  int batt_left = drawBattery(d, d.width() - cw - 3);

  // status icons (app connected / GPS / muted): right-justified just left of the battery
  const uint8_t* bm[3]; DisplayDriver::Color col[3]; int ni = 0;
  if (_task->hasConnection()) { bm[ni] = es_app_icon;   col[ni] = DisplayDriver::LIGHT; ni++; }
  if (_task->getGPSState())   { bm[ni] = es_gps_icon;   col[ni] = DisplayDriver::LIGHT; ni++; }
#ifdef PIN_BUZZER
  if (_task->isBuzzerQuiet())  { bm[ni] = es_muted_icon; col[ni] = DisplayDriver::RED;  ni++; }
#endif
  int ix = batt_left - 1 - 9 * ni;        // 8px glyph + 1px gap each, flush to the battery
  for (int i = 0; i < ni; i++) { d.setColor(col[i]); d.drawXbm(ix, 1, bm[i], 8, ES_ICON_H); ix += 9; }

  // left side: title
  char title[24];
  d.translateUTF8ToBlocks(title, _title ? _title : "", sizeof(title));
  d.setColor(DisplayDriver::GREEN);
  d.setCursor(0, 0);
  d.print(title);

  d.setColor(DisplayDriver::LIGHT);
  d.fillRect(0, STATUS_H - 2, d.width(), 1);   // separator at y=11
}

void ElementScreen::drawPageDots(DisplayDriver& d) {
  int n = pageCount();
  if (n <= 1) return;
  int cur = pageIndex();
  const int spacing = 8;
  int cy = USABLE_BOTTOM - 3;
  int x0 = d.width() / 2 - spacing * (n - 1) / 2;
  d.setColor(DisplayDriver::LIGHT);
  for (int i = 0; i < n; i++) {
    int cx = x0 + i * spacing;
    if (i == cur) d.fillRect(cx - 2, cy - 2, 5, 5);   // current page: a bit larger
    else          d.fillRect(cx - 1, cy - 1, 3, 3);   // other pages: same base size
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
  const int cw = d.width() - 4;   // always reserve the scrollbar gutter so content doesn't reflow

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

  // Wake to re-render on a cadence so time-varying fields (uptime, message
  // relative ages) stay current: faster on USB, slower on battery. endFrame()
  // CRC-gates the panel, so a static page costs only an MCU wake, not a refresh.
  return board.isExternalPowered() ? 10000 : 60000;
}
