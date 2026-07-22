#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "Pages.h"

// Element-based, two-button companion UI (see README.md). Drop-in replacement
// for ui-new's UITask: same constructor / begin() / AbstractUITask virtuals.
class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  unsigned long _idle_refresh_at = 0;   // next idle re-draw while the screen is off (0 = none scheduled)
  NodePrefs* _node_prefs;
  float _saved_freq;       // freq before off-grid was enabled, restored on disable (0 = none)
  char _alert[80];
  unsigned long _alert_expiry;
  static const int MSG_POPUP_MAXLINES = 9;   // near-full-screen box: sender + up to 8 wrapped lines
  char _mpop[MSG_POPUP_MAXLINES][28];
  int  _mpop_n = 0;
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif
#if defined(LED_POWER)
  unsigned long _red_led_at = 0;   // next red-LED heartbeat edge (battery only)
  bool _red_led_lit = false;       // red LED currently lit (blip window / solid full)
  bool _red_led_out = false;       // pin held as OUTPUT (released to INPUT while charging)
  unsigned long _red_batt_at = 0;  // next "battery full?" ADC poll while on external power
  bool _red_full = false;          // last poll said charge complete (solid red)
  unsigned long _blue_led_off_at = 0;  // end of the blue LED's per-packet RX flash (0 = idle)
  unsigned long _last_chirp_at = 0;    // last packet chirp (rate-limit for distinct ticks)
#endif

  UIScreen*            splash;
  ElementScreen*       pages[PAGE_COUNT];
  int                  curr_page;
  UIScreen*            curr;
  MessagesScreen*      _messages;
  MessageDetailScreen* _detail;
  HelpScreen*          _help;
  QRScreen*            _qr;

  // triple-click pops up the help overlay; any press returns here
  UIScreen*            _help_return = NULL;
  int                  _help_return_page = 0;

  // a new message pops up in the read view for ~10s, then returns to _revert_*
  UIScreen*            _revert_screen = NULL;
  int                  _revert_page = 0;
  unsigned long        _revert_at = 0;   // 0 = no pending auto-return
  unsigned long        _cycle_repeat_at = 0;   // 0 = no held-cycle auto-repeat pending

  void userLedHandler();
  void playMsgSound(uint8_t path_len);   // play the configured notification sound
#if defined(PIN_BUZZER)
  void playMorseHops(uint8_t path_len);  // morse the hop count at 440Hz
#endif
  void setCurrScreen(UIScreen* c);
  void nextPage();
  void prevPage();
  bool onPage() const { return curr != NULL && curr != splash; }
  bool wakeIfOff();   // a button press while the display is off only wakes it
  void revertFromPopup();   // return from a new-message popup to where the user was

public:
  UITask(mesh::MainBoard* board, BaseSerialInterface* serial)
      : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    _saved_freq = 0;
    curr = NULL;
    curr_page = 0;
    _msgcount = 0;
    _messages = NULL;
    _detail = NULL;
    _help = NULL;
    _qr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen();
  void showHelp();               // pop up the help overlay (remembers where to return)
  void showQR();                 // pop up the self-advert QR overlay (share your key)
  void openMessageAt(int idx);   // show the read view for list message `idx`
  void navMessage(int delta);    // step to another message while in the read view
  void showAlert(const char* text, int duration_millis = 1500);
  void buildMsgPopup(const char* from, const char* text);   // sender + wrapped preview into _mpop
  int  getMsgCount() const { return _msgcount; }
  uint32_t currentEpoch() const;   // device clock (UTC epoch secs, 0 if unset)
  bool hasDisplay() const { return _display != NULL; }
  NodePrefs* nodePrefs() const { return _node_prefs; }   // for shared chrome (clock format)

  bool isBuzzerQuiet() {
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  // actions invoked by page elements
  void toggleBuzzer();
  uint8_t getBuzzerMode() const { return _node_prefs ? _node_prefs->buzzer_mode : 0; }
  void setBuzzerMode(uint8_t m);   // persist + play a preview
  // Beep target (mutually exclusive): 0 = beep on messages, 1 = beep on packets.
  uint8_t getBeepMode() const { return (_node_prefs && _node_prefs->pkt_tones) ? 1 : 0; }
  void cycleBeepMode();            // flip messages <-> packets (persist + preview)


  // displayed-time settings (Time page)
  uint8_t getTimeFormat() const { return _node_prefs ? _node_prefs->time_format : 0; }
  void setTimeFormat(uint8_t f);                          // persist
  int16_t getUtcOffsetMin() const { return _node_prefs ? _node_prefs->utc_offset_min : 0; }
  void setUtcOffsetMin(int16_t m);                        // persist
  void formatClock(char* out, size_t n) const;           // current local time, formatted
  void formatDateTime(uint32_t epoch, char* out, size_t n) const;
  bool getGPSState();
  void toggleGPS();
  void doAdvert();
  // ---- periodic self-advert (Mesh page) ----
  bool getAutoAdvert() const;              // is auto-advert enabled?
  void toggleAutoAdvert();                 // enable/disable (persist + rearm timer)
  const char* advertIntervalLabel() const; // current interval as "15m"/"1h" etc.
  void cycleAdvertInterval();              // step to the next preset interval (persist + rearm)
  bool getAdvertLoc() const;               // is location shared in adverts?
  void toggleAdvertLoc();                  // flip advert_loc_policy (persist)
  void forceFullRefresh();                 // full e-ink refresh to clear partial-update ghosting
  bool getOffGrid() const;                 // "off-grid" = client-repeat (this node also relays)
  void toggleOffGrid();
  int  getFreqPreset() const;              // 433/869/918 preset band-locked to the operating freq (read-only)

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void onPacketRx(uint8_t ptype) override;   // RX activity LED + optional per-type tone
  void dumpScreenshot(Print& out, int fmt) override { if (_display) _display->screenshot(out, fmt); }
  void loop() override;

  void shutdown();   // low-battery auto power-off
};
