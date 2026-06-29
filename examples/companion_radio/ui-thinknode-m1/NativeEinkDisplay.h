#pragma once

#include <SPI.h>
#include <Wire.h>

#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <CRC32.h>

#include <helpers/ui/DisplayDriver.h>

// Consecutive partial (fast) refreshes before one is promoted to a true full refresh to
// clear accumulated ghosting -- matches Meshtastic's ThinkNode M1 EINK_LIMIT_FASTREFRESH.
#ifndef EINK_LIMIT_FASTREFRESH
  #define EINK_LIMIT_FASTREFRESH  10
#endif

// Native 200x200 renderer for the ThinkNode M1 e-ink: draws 1:1 in physical pixels
// (no logical-128 upscaler, so chrome is crisp) and uses a packed GNU Unifont subset
// for text (real Unicode/symbol glyphs instead of the `block` fallback). Lives in the
// ui-thinknode-m1 module so upstream src/ stays untouched -- swapped in via the build flag
// DISPLAY_CLASS=NativeEinkDisplay + USE_NATIVE_EINK_UI (see variant target.h).
class NativeEinkDisplay : public DisplayDriver {
  // GDEH0154D67 1.54" 200x200 (SSD1681) -- the M1's actual panel, driven via Meshtastic's
  // GxEPD2 fork (correct class + crisp partial waveform). We keep every visible update on
  // the crisp partial path and clear ghosting with a black->white swing rather than the
  // OTP full-refresh LUT: that LUT writes new content into both RAM banks, so unchanged
  // pixels see "no change" and under-drive, leaving salt-and-pepper speckle.
  GxEPD2_BW<GxEPD2_154_D67, 200> display;
  bool      _init = false;
  bool      _isOn = false;
  uint16_t  _curr_color;        // GxEPD_BLACK / GxEPD_WHITE
  int       _cx = 0, _cy = 0;   // text cursor, native px = top-left of the glyph cell
  int       _scale = 1;         // 1 => 16px Unifont, 2 => 32px
  CRC32     display_crc;        // skip redundant e-ink refreshes (same pattern as GxEPDDisplay)
  uint32_t  last_display_crc_value = 0;
  bool      _clear_pending = false;  // do a ghost-clearing swing at the start of the next frame
  int       _partial_count = 0;      // partial refreshes since the last swing

  void swingClear();   // cycle every pixel black->white to flush ghosting (crisp, no speckle)
  int  blitGlyph(uint32_t cp, int x, int y, int scale);   // returns advance width (px)

public:
  // Meshtastic's GxEPD2 fork takes the SPI bus in the constructor (no selectSPI()); the
  // M1 drives the panel on the variant's secondary bus, SPI1.
  NativeEinkDisplay() : DisplayDriver(200, 200),
    display(GxEPD2_154_D67(DISP_CS, DISP_DC, DISP_RST, DISP_BUSY, SPI1)) {}

  bool begin();

  bool isOn() override { return _isOn; }
  bool isEink() override { return true; }
  void fullRefresh() override;   // request a full refresh on the next endFrame
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  // UTF-8 is rendered directly by print(); pass it through untouched.
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override;
  void endFrame() override;
};
