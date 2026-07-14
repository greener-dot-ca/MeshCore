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
  d.setCursor(right - w, 0);
  d.print(blk);
  int left = right - w;

  if (board.isExternalPowered()) {           // charging: ⚡ left of the gauge
    const char* bolt = "\xE2\x9A\xA1";        // U+26A1
    int bw = d.getTextWidth(bolt);
    d.setColor(DisplayDriver::GREEN);
    d.setCursor(left - bw, 0);
    d.print(bolt);
    left -= bw;
  }
  return left;
}

void ElementScreen::drawStatusBar(DisplayDriver& d) {
  d.setTextSize(1);
  const int ty = 0;   // row 0

  // right side: clock (rightmost), then battery (+charge bolt) to its left. Every
  // item is a whole number of 8px columns and the clock's right edge is the panel
  // edge (200), so column-adjacent placement keeps the whole cluster on the grid.
  char clk[12];
  uiFormatClock(_prefs, _task->currentEpoch(), clk, sizeof(clk));
  int cw = d.getTextWidth(clk);
  d.setColor(DisplayDriver::LIGHT);
  d.drawTextRightAlign(d.width(), ty, clk);
  int x = drawBattery(d, d.width() - cw);        // returns left edge of battery (or bolt)

  // status icons (BLE-off / app / GPS / muted) as Unifont symbols, right-justified before battery
  const char* icons[4]; DisplayDriver::Color col[4]; int ni = 0;
  if (_task->isCLIRescue()) {                    // in CLI rescue the companion link is suspended:
    icons[ni] = "\xF0\x9F\x94\xA7"; col[ni] = DisplayDriver::RED; ni++; // 🔧 show that, not a false "connected"
  } else {
    if (!_task->isSerialEnabled()) { icons[ni] = "\xF0\x9F\x93\xB5"; col[ni] = DisplayDriver::RED; ni++; } // 📵 BLE off
    if (_task->hasConnection()) { icons[ni] = "\xF0\x9F\x93\xB1"; col[ni] = DisplayDriver::LIGHT; ni++; } // 📱
  }
  if (_task->getGPSState())   { icons[ni] = "\xF0\x9F\x93\x8D"; col[ni] = DisplayDriver::LIGHT; ni++; } // 📍
#ifdef PIN_BUZZER
  if (_task->isBuzzerQuiet())  { icons[ni] = "\xF0\x9F\x94\x87"; col[ni] = DisplayDriver::RED;  ni++; } // 🔇
#endif
  for (int i = ni - 1; i >= 0; i--) {            // place right-to-left, column-adjacent
    x -= d.getTextWidth(icons[i]);
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

}

// Bottom bar: one glyph per page (in PAGE_* order), the current page shown
// reverse-video. Keep this array in sync with the enum in Pages.h.
static const char* const PAGE_ICONS[] = {
  "\xF0\x9F\x8F\xA0",   // PAGE_HOME       🏠 house
  "\xE2\x9C\x89",       // PAGE_MESSAGES   ✉ envelope
  "\xF0\x9F\x93\xA1",   // PAGE_MESH       📡 antenna
  "\xF0\x9F\x93\xA5",   // PAGE_RXLOG      📥 inbox (RX)
  "\xF0\x9F\x93\xBB",   // PAGE_RADIO      📻 radio
  "\xF0\x9F\x93\x8D",   // PAGE_GPS        📍 pin
  "\xF0\x9F\xA7\xAD",   // PAGE_NAV        🧭 compass
  "\xE2\x92\xB7",       // PAGE_BLUETOOTH  Ⓑ circled-B
  "\xF0\x9F\x94\x94",   // PAGE_BUZZ       🔔 bell
  "\xF0\x9F\x95\x90",   // PAGE_TIME       🕐 clock
  "\xE2\x8F\xBB",       // PAGE_SHUTDOWN   ⏻ power
};

void ElementScreen::drawPageDots(DisplayDriver& d) {
  int n = pageCount();
  if (n <= 1) return;
  const int NICONS = (int)(sizeof(PAGE_ICONS) / sizeof(PAGE_ICONS[0]));
  if (n > NICONS) n = NICONS;
  int cur = pageIndex();
  d.setTextSize(1);
  const int y = USABLE_BOTTOM - 16;            // row 11
  // Column-adjacent (gap 0); each icon is a whole number of columns, so snapping
  // the start to a column boundary keeps every icon on the grid.
  int total = 0;
  for (int i = 0; i < n; i++) total += d.getTextWidth(PAGE_ICONS[i]);
  int x = ((d.width() - total) / 2) & ~(GRID_COL - 1);
  if (x < 0) x = 0;
  for (int i = 0; i < n; i++) {
    const char* g = PAGE_ICONS[i];
    int gw = d.getTextWidth(g);
    if (i == cur) {                             // reverse-video: white glyph on a black cell
      d.setColor(DisplayDriver::LIGHT);
      d.fillRect(x, y, gw, 16);
      d.setColor(DisplayDriver::DARK);
    } else {
      d.setColor(DisplayDriver::LIGHT);
    }
    d.setCursor(x, y);
    d.print(g);
    x += gw;
  }
}

// TUI-style scrollbar: a column of glyphs at the right edge -- light vertical
// bars for the track, full blocks for the thumb (no drawn rectangles).
void ElementScreen::drawScrollbar(DisplayDriver& d) {
  int top = contentTop(), vp = viewportH(), ch = contentHeight();
  if (ch <= vp) return;
  static const char* const TRACK = "\xE2\x94\x82";   // │ U+2502
  static const char* const THUMB = "\xE2\x96\x88";   // █ U+2588
  d.setTextSize(1);
  int gw = d.getTextWidth(TRACK);
  int x = d.width() - gw;
  int cells = vp / 16;                               // 16px glyph rows in the viewport
  if (cells < 1) cells = 1;
  int thumb_cells = cells * vp / ch;
  if (thumb_cells < 1) thumb_cells = 1;
  int max_off = ch - vp;
  int thumb_start = (max_off > 0) ? (_scroll_y * (cells - thumb_cells) + max_off / 2) / max_off : 0;
  d.setColor(DisplayDriver::LIGHT);
  for (int i = 0; i < cells; i++) {
    d.setCursor(x, top + i * 16);
    d.print((i >= thumb_start && i < thumb_start + thumb_cells) ? THUMB : TRACK);
  }
}

int ElementScreen::render(DisplayDriver& d) {
  rebuild();
  if (_focus >= _count) _focus = (_count > 0) ? _focus % _count : -1;

  drawStatusBar(d);

  const int top = contentTop();
  const int vp = viewportH();
  const bool hasSB = contentHeight() > vp;
  // Content uses every column from the first (x=0) to the last-but-one; the final
  // column is always reserved for the scrollbar, so nothing reflows when it
  // appears and the scrollbar never lands on the selection bar.
  const int cw = d.width() - GRID_COL;

  for (int i = 0; i < _count; i++) {
    int et = elemTop(i);
    int eh = _elems[i].height();
    if (et < _scroll_y) continue;            // above viewport
    if (et + eh > _scroll_y + vp) break;      // below viewport (rest won't fit)
    int ey = top + (et - _scroll_y);
    bool foc = _show_focus && i == _focus;
    if (foc) {                                // reverse-video selection bar (content columns)
      d.setColor(DisplayDriver::LIGHT);
      d.fillRect(0, ey, cw, eh);
    }
    _elems[i].draw(d, 0, ey, cw, foc);
  }

  if (hasSB) drawScrollbar(d);
  drawPageDots(d);

  // Wake to re-render on a cadence so time-varying fields (uptime, message
  // relative ages) stay current: faster on USB, slower on battery. endFrame()
  // CRC-gates the panel, so a static page costs only an MCU wake, not a refresh.
  return board.isExternalPowered() ? 10000 : 60000;
}
