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

// Battery level as a single Block-Elements glyph (▁..█, U+2581..2588): a one-char
// fuel gauge whose right edge sits at `right`. Returns its left x (the bolt's, when
// charging). The eighth-blocks are 3-byte UTF-8 E2 96 (80+level).
int ElementScreen::drawBattery(DisplayDriver& d, int right) {
  uint16_t mv = _task->getBattMilliVolts();
  int pct = ((int)mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int level = (pct * 8 + 50) / 100;          // 0..8 eighths, rounded
  if (level < 1) level = 1;                  // always show at least the 1/8 sliver
  if (level > 8) level = 8;

  char blk[4] = { (char)0xE2, (char)0x96, (char)(0x80 + level), 0 };
  int w = d.getTextWidth(blk);
  d.setColor(DisplayDriver::GREEN);
  d.setCursor(right - w, 2);
  d.print(blk);
  int left = right - w;

  if (board.isExternalPowered()) {           // charging: ⚡ left of the gauge
    const char* bolt = "\xE2\x9A\xA1";        // U+26A1
    int bw = d.getTextWidth(bolt);
    d.setColor(DisplayDriver::GREEN);
    d.setCursor(left - bw, 2);
    d.print(bolt);
    left -= bw;
  }
  return left;
}

void ElementScreen::drawStatusBar(DisplayDriver& d) {
  d.setTextSize(1);
  const int ty = 2;   // 16px glyphs span y=2..18 in the 20px bar

  // right side: clock (rightmost), then battery (+charge bolt) to its left
  char clk[12];
  uiFormatClock(_prefs, _task->currentEpoch(), clk, sizeof(clk));
  int cw = d.getTextWidth(clk);
  d.setColor(DisplayDriver::LIGHT);
  d.drawTextRightAlign(d.width(), ty, clk);
  int x = drawBattery(d, d.width() - cw - 4);   // returns left edge of battery (or bolt)

  // status icons (app / GPS / muted) as Unifont symbols, right-justified before battery
  const char* icons[3]; DisplayDriver::Color col[3]; int ni = 0;
  if (_task->hasConnection()) { icons[ni] = "\xF0\x9F\x93\xB1"; col[ni] = DisplayDriver::LIGHT; ni++; } // 📱
  if (_task->getGPSState())   { icons[ni] = "\xF0\x9F\x93\x8D"; col[ni] = DisplayDriver::LIGHT; ni++; } // 📍
#ifdef PIN_BUZZER
  if (_task->isBuzzerQuiet())  { icons[ni] = "\xF0\x9F\x94\x87"; col[ni] = DisplayDriver::RED;  ni++; } // 🔇
#endif
  for (int i = ni - 1; i >= 0; i--) {            // place right-to-left, flush to the battery
    x -= d.getTextWidth(icons[i]) + 1;
    d.setColor(col[i]);
    d.setCursor(x, ty);
    d.print(icons[i]);
  }

  // left side: title
  char title[24];
  d.translateUTF8ToBlocks(title, _title ? _title : "", sizeof(title));
  d.setColor(DisplayDriver::GREEN);
  d.setCursor(0, ty);
  d.print(title);

  d.setColor(DisplayDriver::LIGHT);
  d.fillRect(0, STATUS_H - 2, d.width(), 1);   // separator
}

void ElementScreen::drawPageDots(DisplayDriver& d) {
  int n = pageCount();
  if (n <= 1) return;
  int cur = pageIndex();
  const char* full = "\xE2\x97\x8F";   // ● U+25CF (current page)
  const char* ring = "\xE2\x97\x8B";   // ○ U+25CB (other pages)
  int gw = d.getTextWidth(full);
  int spacing = gw + 2;
  int x = d.width() / 2 - (spacing * (n - 1) + gw) / 2;
  int y = USABLE_BOTTOM - 16;          // bottom-align the 16px glyph cell
  d.setColor(DisplayDriver::LIGHT);
  for (int i = 0; i < n; i++) {
    d.setCursor(x, y);
    d.print(i == cur ? full : ring);
    x += spacing;
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
