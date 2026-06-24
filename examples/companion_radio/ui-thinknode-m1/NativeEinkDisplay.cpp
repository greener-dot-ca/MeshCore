#include <Arduino.h>
#include "NativeEinkDisplay.h"
#include "unifont_glyphs.h"

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 4
#endif

// ---- Unifont glyph lookup + UTF-8 ------------------------------------------------

static int uniLookup(uint32_t cp) {            // binary search the sorted codepoint table
  int lo = 0, hi = UNIFONT_GLYPH_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint32_t v = unifont_cp[mid];
    if (v == cp) return mid;
    if (v < cp) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

// Zero-width codepoints (ZWJ/ZWSP/joiners, variation selectors, BOM). Unifont has
// no glyphs for these and no precomposed ZWJ emoji, so we skip them -- a sequence
// like the pirate flag (🏴 ZWJ ☠ VS16) renders as its visible parts, not junk boxes.
static bool isZeroWidth(uint32_t cp) {
  return cp == 0x200D || (cp >= 0x200B && cp <= 0x200F) ||
         (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF;
}

// Decode one UTF-8 codepoint; advance p past it. Stops cleanly on truncation.
static const char* utf8Next(const char* p, uint32_t& cp) {
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) { cp = c; return p; }
  int n; uint32_t v;
  if ((c & 0xE0) == 0xC0)      { n = 1; v = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { n = 2; v = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { n = 3; v = c & 0x07; }
  else { cp = '?'; return p; }                 // stray continuation/invalid byte
  for (int i = 0; i < n; i++) {
    if ((*p & 0xC0) != 0x80) { cp = '?'; return p; }   // truncated
    v = (v << 6) | (*p++ & 0x3F);
  }
  cp = v;
  return p;
}

int NativeEinkDisplay::blitGlyph(uint32_t cp, int x, int y, int scale) {
  int idx = uniLookup(cp);
  if (idx < 0) {                               // missing: hollow box placeholder
    display.drawRect(x, y + 2 * scale, 7 * scale, 11 * scale, _curr_color);
    return 8 * scale;
  }
  int w = unifont_w[idx];                      // 8 or 16
  uint32_t off = unifont_off[idx];
  int rowbytes = w / 8;
  for (int r = 0; r < UNIFONT_ROWS; r++) {
    for (int c = 0; c < w; c++) {
      if (unifont_bits[off + r * rowbytes + (c >> 3)] & (0x80 >> (c & 7))) {
        if (scale == 1) display.drawPixel(x + c, y + r, _curr_color);
        else            display.fillRect(x + c * scale, y + r * scale, scale, scale, _curr_color);
      }
    }
  }
  return w * scale;
}

// ---- lifecycle (mirrors GxEPDDisplay, native resolution) -------------------------

bool NativeEinkDisplay::begin() {
  display.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  SPI1.begin();
  display.init(115200, true, 2, false);
  display.setRotation(DISPLAY_ROTATION);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.fillScreen(GxEPD_WHITE);
  display.display(true);
#if defined(DISP_BACKLIGHT)
  digitalWrite(DISP_BACKLIGHT, LOW);
  pinMode(DISP_BACKLIGHT, OUTPUT);
#endif
  _init = true;
  return true;
}

void NativeEinkDisplay::turnOn() {
  if (!_init) begin();
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, HIGH);
#endif
  _isOn = true;
}

void NativeEinkDisplay::turnOff() {
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, LOW);
#endif
  _isOn = false;
}

void NativeEinkDisplay::clear() {
  display.fillScreen(GxEPD_WHITE);
}

void NativeEinkDisplay::startFrame(Color bkg) {
  display.fillScreen(GxEPD_WHITE);
  _curr_color = GxEPD_BLACK;
  display_crc.reset();
}

// ---- draw ops --------------------------------------------------------------------

void NativeEinkDisplay::setTextSize(int sz) {
  _scale = (sz >= 2) ? 2 : 1;
  display_crc.update<int>(_scale);
}

void NativeEinkDisplay::setColor(Color c) {    // DARK = background (white); others = ink (black)
  _curr_color = (c == DARK) ? GxEPD_WHITE : GxEPD_BLACK;
  display_crc.update<uint16_t>(_curr_color);
}

void NativeEinkDisplay::setCursor(int x, int y) {
  _cx = x; _cy = y;
  display_crc.update<int>(x);
  display_crc.update<int>(y);
}

void NativeEinkDisplay::print(const char* str) {
  display_crc.update<int>(_cx);
  display_crc.update<int>(_cy);
  display_crc.update<uint16_t>(_curr_color);
  display_crc.update<int>(_scale);
  display_crc.update<char>(str, strlen(str));
  const char* p = str; uint32_t cp;
  while (*p) {
    p = utf8Next(p, cp);
    if (isZeroWidth(cp)) continue;
    _cx += blitGlyph(cp, _cx, _cy, _scale);
  }
}

void NativeEinkDisplay::fillRect(int x, int y, int w, int h) {
  display_crc.update<int>(x); display_crc.update<int>(y);
  display_crc.update<int>(w); display_crc.update<int>(h);
  display_crc.update<uint16_t>(_curr_color);
  display.fillRect(x, y, w, h, _curr_color);
}

void NativeEinkDisplay::drawRect(int x, int y, int w, int h) {
  display_crc.update<int>(x); display_crc.update<int>(y);
  display_crc.update<int>(w); display_crc.update<int>(h);
  display_crc.update<uint16_t>(_curr_color);
  display.drawRect(x, y, w, h, _curr_color);
}

// 1:1 blit of an MSB-first bitmap (bit 0x80 = leftmost), matching the project's
// drawXbm convention -- no scaling, so nothing stripes.
void NativeEinkDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display_crc.update<int>(x); display_crc.update<int>(y);
  display_crc.update<uint8_t>(bits, (w * h + 7) / 8);
  display_crc.update<uint16_t>(_curr_color);
  int rowbytes = (w + 7) / 8;
  for (int by = 0; by < h; by++) {
    for (int bx = 0; bx < w; bx++) {
      if (bits[by * rowbytes + (bx >> 3)] & (0x80 >> (bx & 7)))
        display.drawPixel(x + bx, y + by, _curr_color);
    }
  }
}

uint16_t NativeEinkDisplay::getTextWidth(const char* str) {
  int w = 0; const char* p = str; uint32_t cp;
  while (*p) {
    p = utf8Next(p, cp);
    if (isZeroWidth(cp)) continue;
    int idx = uniLookup(cp);
    w += (idx >= 0 ? unifont_w[idx] : 8) * _scale;
  }
  return w;
}

void NativeEinkDisplay::translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
  size_t i = 0;
  for (; src[i] && i < dest_size - 1; i++) dest[i] = src[i];
  dest[i] = 0;
}

// Clear accumulated ghosting by cycling the whole screen black->white using the
// NORMAL partial-update path (which works reliably on this panel), then letting
// the caller repaint content. A true full refresh (display(false)) or a mid-
// session init() leaves this SSD1681 unable to render cleanly afterwards (blank /
// corrupt), so we deliberately stay in partial mode and just exercise every pixel
// through a full black/white swing instead.
void NativeEinkDisplay::fullRefresh() {
  display.fillScreen(GxEPD_BLACK);
  display.display(true);          // whole screen -> black (partial)
  display.fillScreen(GxEPD_WHITE);
  display.display(true);          // whole screen -> white (partial)
  last_display_crc_value = 0;     // force the next render to repaint content
}

void NativeEinkDisplay::endFrame() {
  uint32_t crc = display_crc.finalize();
  if (crc != last_display_crc_value) {
    display.display(true);                      // partial (auto-promoted to full right after init)
    last_display_crc_value = crc;
  }
}
