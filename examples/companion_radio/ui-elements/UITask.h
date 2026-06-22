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
  NodePrefs* _node_prefs;
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

  UIScreen*            splash;
  ElementScreen*       pages[PAGE_COUNT];
  int                  curr_page;
  UIScreen*            curr;
  MessagesScreen*      _messages;
  MessageDetailScreen* _detail;

  // a new message pops up in the read view for ~10s, then returns to _revert_*
  UIScreen*            _revert_screen = NULL;
  int                  _revert_page = 0;
  unsigned long        _revert_at = 0;   // 0 = no pending auto-return

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

public:
  UITask(mesh::MainBoard* board, BaseSerialInterface* serial)
      : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
    curr_page = 0;
    _msgcount = 0;
    _messages = NULL;
    _detail = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen();
  void openMessageAt(int idx);   // show the read view for list message `idx`
  void navMessage(int delta);    // step to another message while in the read view
  void showAlert(const char* text, int duration_millis);
  int  getMsgCount() const { return _msgcount; }
  uint32_t currentEpoch() const;   // device clock (UTC epoch secs, 0 if unset)
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;

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

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
