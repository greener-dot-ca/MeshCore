#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif


void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
  back_btn.begin();   // second button (PIN_BUTTON2)

  _node_prefs = node_prefs;

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.startup();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  _messages = new MessagesScreen(this, node_prefs);
  _detail = new MessageDetailScreen(this);
  _help = new HelpScreen();
  pages[PAGE_HOME]      = new HomeScreen(this, node_prefs);
  pages[PAGE_MESH]      = new MeshScreen(this, node_prefs);
  pages[PAGE_RADIO]     = new RadioScreen(this, node_prefs);
  pages[PAGE_GPS]       = new GPSScreen(this, node_prefs);
  pages[PAGE_BLUETOOTH] = new BluetoothScreen(this, node_prefs);
  pages[PAGE_BUZZ]      = new BuzzScreen(this, node_prefs);
  pages[PAGE_TIME]      = new TimeScreen(this, node_prefs);
  pages[PAGE_MESSAGES]  = _messages;
  pages[PAGE_SHUTDOWN]  = new ShutdownScreen(this, node_prefs);

  curr_page = PAGE_HOME;
  setCurrScreen(splash);
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 100;
}

void UITask::gotoHomeScreen() {
  curr_page = PAGE_HOME;
  pages[PAGE_HOME]->resetFocus();
  setCurrScreen(pages[PAGE_HOME]);
}

void UITask::showHelp() {
  _help_return = curr;            // where a dismiss press returns to
  _help_return_page = curr_page;
  setCurrScreen(_help);
}

void UITask::openMessageAt(int idx) {
  int total = the_mesh.getDisplayMsgCount();
  if (total <= 0) return;
  if (idx < 0) idx = 0;
  if (idx >= total) idx = total - 1;
  MyMesh::MsgView v;
  if (the_mesh.getDisplayMsg(idx, v)) {
    _detail->setMessage(v, idx, total);
    setCurrScreen(_detail);
  }
}

void UITask::navMessage(int delta) {   // wraps around the list ends
  int total = the_mesh.getDisplayMsgCount();
  if (total <= 0) return;
  int idx = (_detail->index() + delta % total + total) % total;
  openMessageAt(idx);
}

void UITask::nextPage() {
  curr_page = (curr_page + 1) % PAGE_COUNT;
  pages[curr_page]->resetFocus();
  setCurrScreen(pages[curr_page]);
}

void UITask::prevPage() {
  curr_page = (curr_page + PAGE_COUNT - 1) % PAGE_COUNT;
  pages[curr_page]->resetFocus();
  setCurrScreen(pages[curr_page]);
}

bool UITask::wakeIfOff() {
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 0;
    return true;
  }
  return false;
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::doAdvert() {
  notify(UIEventType::ack);
  if (the_mesh.advert()) {
    showAlert("Advert sent!", 1000);
  } else {
    showAlert("Advert failed..", 1000);
  }
}

void UITask::forceFullRefresh() {
  if (_display != NULL) {
    _display->fullRefresh();   // full (ghost-clearing) refresh on the next paint
    _next_refresh = 0;         // repaint now so endFrame runs the full update
    if (_display->isOn()) _auto_off = millis() + AUTO_OFF_MILLIS;
  }
}

// "Off-grid" mode = client-repeat: this companion node also forwards/relays mesh
// traffic (MyMesh::allowPacketForward), so every node helps carry the mesh. Only
// flips the flag + persists -- it does NOT touch radio params (freq/sf/bw).
bool UITask::getOffGrid() const {
  return _node_prefs && _node_prefs->client_repeat;
}

void UITask::toggleOffGrid() {
  if (!_node_prefs) return;
  _node_prefs->client_repeat = _node_prefs->client_repeat ? 0 : 1;
  the_mesh.savePrefs();
  notify(UIEventType::ack);
  showAlert(_node_prefs->client_repeat ? "Off-grid: ON" : "Off-grid: OFF", 800);
  _next_refresh = 0;
}

// Frequency presets (MHz). These match the client-repeat allowed bands
// (repeat_freq_ranges in MyMesh), so off-grid repeat stays valid on each.
static const float FREQ_PRESETS[] = { 433.0f, 869.495f, 918.0f };
static const int   FREQ_PRESET_COUNT = sizeof(FREQ_PRESETS) / sizeof(FREQ_PRESETS[0]);

int UITask::getFreqPreset() const {
  if (!_node_prefs) return 0;
  for (int i = 0; i < FREQ_PRESET_COUNT; i++) {
    float d = _node_prefs->freq - FREQ_PRESETS[i];
    if (d > -0.05f && d < 0.05f) return i;
  }
  return 0;   // current freq isn't a preset; show the first
}

void UITask::cycleFreqPreset() {
  if (!_node_prefs) return;
  // The preset only governs the off-grid (client-repeat) band -- don't retune the
  // live radio unless off-grid is on, so browsing this control on a normal node
  // can't knock it off its configured frequency.
  if (!getOffGrid()) {
    showAlert("Enable Off-grid first", 1000);
    _next_refresh = 0;
    return;
  }
  int idx = (getFreqPreset() + 1) % FREQ_PRESET_COUNT;
  the_mesh.setRadioFreq(FREQ_PRESETS[idx]);   // retune + persist
  notify(UIEventType::ack);
  char msg[24];
  snprintf(msg, sizeof(msg), "Freq: %.3f MHz", FREQ_PRESETS[idx]);
  showAlert(msg, 1000);
  _next_refresh = 0;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
  // Incoming-message sounds are played from newMsg() instead, which knows the
  // hop count (needed for the morse mode). Here we only do the short ack click.
  if (t == UIEventType::ack) buzzer.play("ack:d=32,o=8,b=120:c");
#endif

#ifdef PIN_VIBRATION
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}

#if defined(PIN_BUZZER)
// Morse the hop count at 440Hz; a direct message (path_len 0xFF) has no hop
// number so it gets one distinct tone instead. dit/dah ~50/150ms keeps a single
// digit (hops are 0-7 in practice) under ~1s.
void UITask::playMorseHops(uint8_t path_len) {
  const uint16_t F = 440;
  const uint16_t DIT = 50, DAH = 150, GAP = 50, DGAP = 150;
  ToneSeg seq[48];
  uint8_t n = 0;

  // Silent lead-in: right after this returns the UI does a blocking e-ink refresh,
  // during which buzzer.loop() can't run -- without this the first real tone is held
  // for the whole refresh and a leading dit sounds like a dah. The stall lands here.
  seq[n++] = { 0, 30 };

  if (path_len == 0xFF) {                 // direct: no hops to count
    seq[n++] = { F, 250 };
    buzzer.playTones(seq, n);
    return;
  }

  static const char* const DIGIT[10] = {
    "-----", ".----", "..---", "...--", "....-",
    ".....", "-....", "--...", "---..", "----."
  };
  char num[5];
  int hops = path_len & 63;                    // low 6 bits = hop count (top 2 = hash size)
  snprintf(num, sizeof(num), "%d", hops);

  for (int d = 0; num[d] && n < 46; d++) {
    if (d) seq[n++] = { 0, DGAP };            // gap between digits
    const char* p = DIGIT[num[d] - '0'];
    for (int i = 0; p[i] && n < 46; i++) {
      if (i) seq[n++] = { 0, GAP };           // intra-character gap
      seq[n++] = { F, (uint16_t)(p[i] == '-' ? DAH : DIT) };
    }
  }
  buzzer.playTones(seq, n);
}
#endif

void UITask::playMsgSound(uint8_t path_len) {
#if defined(PIN_BUZZER)
  switch (_node_prefs ? _node_prefs->buzzer_mode : 0) {
    case 1:   // simple beep (leading 8p absorbs the post-call e-ink refresh stall)
      buzzer.play("beep:d=8,o=5,b=200:8p,a");
      break;
    case 2:   // morse the hop count
      playMorseHops(path_len);
      break;
    case 0:   // Cisco IP-phone ring: F6 beep-pairs then a fast B6<->B5 warble
    default:  // (o=5 so bare 'b' = B5 warbles against the explicit b6)
      buzzer.play("Cisco:d=32,o=5,b=300:"
                  "8p,"   // leading rest absorbs the post-call e-ink refresh stall (else the first beep stretches)
                  "f6,p,f6,4p,p,f6,p,f6,2p,p,b6,p,b6,p,b6,p,b6,p,b,p,b,p,b,p,b,p,b,p,b,p,b,p,b");
      break;
  }
#endif
}

void UITask::setBuzzerMode(uint8_t m) {
  if (!_node_prefs) return;
  _node_prefs->buzzer_mode = m % 3;
  the_mesh.savePrefs();
  playMsgSound(3);   // preview: morse mode demos hop count "3"
}

void UITask::setTimeFormat(uint8_t f) {
  if (!_node_prefs) return;
  _node_prefs->time_format = f & 1;
  the_mesh.savePrefs();
}

void UITask::setUtcOffsetMin(int16_t m) {
  if (!_node_prefs) return;
  _node_prefs->utc_offset_min = m;
  the_mesh.savePrefs();
}

void UITask::formatClock(char* out, size_t n) const {
  uiFormatClock(_node_prefs, currentEpoch(), out, n);
}

void UITask::formatDateTime(uint32_t epoch, char* out, size_t n) const {
  uiFormatDateTime(_node_prefs, epoch, out, n);
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  // the app drained the queue; if the list is showing, repaint to mirror it
  if (curr == _messages) _next_refresh = 0;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  playMsgSound(path_len);   // configured notification sound (morse mode uses the hops)

  if (_display != NULL && !_display->isOn() && !hasConnection()) {
    _display->turnOn();
  }

  // pop the newest message into the read view, then auto-return after ~10s.
  // remember where we were (only on the first of a burst); a detail/splash
  // screen falls back to the Messages list.
  if (_revert_at == 0) {
    if (onPage() && curr != _detail) { _revert_screen = curr; _revert_page = curr_page; }
    else                             { _revert_screen = pages[PAGE_MESSAGES]; _revert_page = PAGE_MESSAGES; }
  }
  openMessageAt(0);   // 0 = newest (the message that just arrived); sets curr=_detail
  _revert_at = millis() + 10000;

  if (_display != NULL && _display->isOn()) _auto_off = millis() + AUTO_OFF_MILLIS;
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::shutdown(bool restart) {
#ifdef PIN_BUZZER
  buzzer.shutdown();
  uint32_t buzzer_timer = millis();
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();
#endif

  if (restart) {
    _board->reboot();
  } else {
    _display->turnOff();
    radio_driver.powerOff();
    _board->powerOff();
  }
}

uint32_t UITask::currentEpoch() const {
  return the_mesh.getRTCClock()->getCurrentTime();
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  int ev  = user_btn.check();
  int ev2 = back_btn.check();
  bool was_on = (_display != NULL && _display->isOn());   // a press that only wakes the screen shouldn't count as interaction

  if (curr == _help) {
    // ---- help overlay ---- any button press dismisses it back to where we were
    if (ev != BUTTON_EVENT_NONE || ev2 != BUTTON_EVENT_NONE) {
      if (!wakeIfOff()) {
        curr_page = _help_return_page;
        setCurrScreen(_help_return ? _help_return : (UIScreen*)pages[PAGE_HOME]);
      }
    }
  } else if (curr == _detail) {
    // ---- read view ----
    //   triangle  click       = page down body, then on to the next (older) message
    //   triangle  hold        = previous (newer) message  (CLI rescue in first 8s)
    //   circle    hold        = back to the message list (keeps the drill context)
    //   circle    double      = Home
    if (ev == BUTTON_EVENT_LONG_PRESS && millis() - ui_started_at < 8000) {
      the_mesh.enterCLIRescue();
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      if (!wakeIfOff()) navMessage(-1);
    } else if (ev != BUTTON_EVENT_NONE) {
      if (!wakeIfOff() && !_detail->scrollDown()) navMessage(+1);
    }
    if (ev2 == BUTTON_EVENT_DOUBLE_CLICK) {
      if (!wakeIfOff()) gotoHomeScreen();
    } else if (ev2 == BUTTON_EVENT_TRIPLE_CLICK) {
      if (!wakeIfOff()) showHelp();
    } else if (ev2 == BUTTON_EVENT_LONG_PRESS) {
      // return to the message list WITHOUT resetFocus so the drill context
      // (selected conversation/node) is preserved
      if (!wakeIfOff()) { curr_page = PAGE_MESSAGES; setCurrScreen(_messages); }
    }
  } else {
    // ---- triangle button (user_btn / GPIO42): ELEMENT navigation ----
    //   click        = next element (down)
    //   long press   = previous element (up)   (or CLI rescue in first 8s)
    //   double-click = activate / select focused element
    if (ev == BUTTON_EVENT_CLICK) {
      if (!wakeIfOff() && onPage()) ((ElementScreen*)curr)->focusNext();
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      if (millis() - ui_started_at < 8000) {   // rescue window after boot
        the_mesh.enterCLIRescue();
      } else if (!wakeIfOff() && onPage()) {
        ((ElementScreen*)curr)->focusPrev();
      }
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      if (!wakeIfOff() && onPage()) ((ElementScreen*)curr)->activateFocused();
    }

    // ---- circle button (back_btn / GPIO39): PAGE navigation ----
    //   click        = next page
    //   long press   = previous page (back), or pop up one Messages drill level
    //   double-click = go to home page
    if (ev2 == BUTTON_EVENT_CLICK) {
      if (!wakeIfOff() && onPage()) nextPage();
    } else if (ev2 == BUTTON_EVENT_LONG_PRESS) {
      if (!wakeIfOff() && onPage()) {
        if (curr == _messages && !_messages->atTopLevel()) _messages->goBack();
        else prevPage();
      }
    } else if (ev2 == BUTTON_EVENT_DOUBLE_CLICK) {
      if (!wakeIfOff() && onPage()) gotoHomeScreen();
    } else if (ev2 == BUTTON_EVENT_TRIPLE_CLICK) {
      if (!wakeIfOff() && onPage()) showHelp();
    }
  }

  if (ev != BUTTON_EVENT_NONE || ev2 != BUTTON_EVENT_NONE) {
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;                        // repaint after interaction
    if (was_on) _revert_at = 0;               // real interaction (not a wake) cancels auto-return
  }

  // auto-return from a popped-up new message to where the user was
  if (_revert_at != 0 && millis() > _revert_at) {
    _revert_at = 0;
    curr_page = _revert_page;
    setCurrScreen(_revert_screen ? _revert_screen : (UIScreen*)pages[PAGE_HOME]);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // keep the screen lit so the return is visible
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying()) buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL) {
#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
    if (board.isExternalPowered()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
    }
#endif
    // Sleep is backlight-only: e-ink retains its image after turnOff(), so we
    // kill just the frontlight here and keep refreshing the content below on the
    // timer (so uptime / message ages stay current while asleep).
    bool asleep = millis() > _auto_off;
    if (asleep && _display->isOn()) {
      _display->turnOff();
      _next_refresh = 0;   // repaint now so the selection bar disappears immediately
    }
#else
    const bool asleep = false;
#endif

    if (millis() >= _next_refresh && curr) {
      // hide the selection bar while asleep (visual "screen off" hint); it
      // returns on the next render once awake.
      ElementScreen* page = (onPage() && curr != _detail && curr != _help) ? (ElementScreen*)curr : NULL;
      if (page) page->setFocusVisible(!asleep);

      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (!asleep && millis() < _alert_expiry) {  // render alert popup (awake only)
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(p, y, _display->width() - p * 2, y);
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(p, y, _display->width() - p * 2, y);
        _display->drawTextCentered(_display->width() / 2, y + p * 3, _alert);
        _next_refresh = _alert_expiry;
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
      if (page) page->setFocusVisible(true);
    }
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {
      if (!board.isExternalPowered()) {
        if (_display != NULL) {
          _display->startFrame();
          _display->setTextSize(2);
          _display->setColor(DisplayDriver::RED);
          _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
          _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
          _display->endFrame();
          if (_display->isEink() == false) { delay(3000); }
        }
        shutdown();
      }
    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  }
  return false;
}

void UITask::toggleGPS() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::toggleBuzzer() {
#ifdef PIN_BUZZER
  if (buzzer.isQuiet()) {
    buzzer.quiet(false);
    notify(UIEventType::ack);
  } else {
    buzzer.quiet(true);
  }
  _node_prefs->buzzer_quiet = buzzer.isQuiet();
  the_mesh.savePrefs();
  showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
  _next_refresh = 0;
#endif
}
