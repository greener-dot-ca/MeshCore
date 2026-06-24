#pragma once

#include <SPI.h>
#include <Wire.h>

#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <CRC32.h>

#include <helpers/ui/DisplayDriver.h>

// Native 200x200 renderer for the ThinkNode M1 e-ink: draws 1:1 in physical pixels
// (no logical-128 upscaler, so chrome is crisp) and uses a packed GNU Unifont subset
// for text (real Unicode/symbol glyphs instead of the `block` fallback). Lives in the
// ui-elements module so upstream src/ stays untouched -- swapped in via the build flag
// DISPLAY_CLASS=NativeEinkDisplay + USE_NATIVE_EINK_UI (see variant target.h).
class NativeEinkDisplay : public DisplayDriver {
  GxEPD2_BW<GxEPD2_150_BN, 200> display;
  bool      _init = false;
  bool      _isOn = false;
  uint16_t  _curr_color;        // GxEPD_BLACK / GxEPD_WHITE
  int       _cx = 0, _cy = 0;   // text cursor, native px = top-left of the glyph cell
  int       _scale = 1;         // 1 => 16px Unifont, 2 => 32px
  CRC32     display_crc;        // skip redundant e-ink refreshes (same pattern as GxEPDDisplay)
  uint32_t  last_display_crc_value = 0;

  int  blitGlyph(uint32_t cp, int x, int y, int scale);   // returns advance width (px)

public:
  NativeEinkDisplay() : DisplayDriver(200, 200),
    display(GxEPD2_150_BN(DISP_CS, DISP_DC, DISP_RST, DISP_BUSY)) {}

  bool begin();

  bool isOn() override { return _isOn; }
  bool isEink() override { return true; }
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
