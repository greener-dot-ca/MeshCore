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
  pages[PAGE_HOME]      = new HomeScreen(this, node_prefs);
  pages[PAGE_MESH]      = new MeshScreen(this, node_prefs);
  pages[PAGE_GPS]       = new GPSScreen(this, node_prefs);
  pages[PAGE_BLUETOOTH] = new BluetoothScreen(this, node_prefs);
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

void UITask::openMessage(const MyMesh::MsgView& m) {
  _detail->setMessage(m);
  setCurrScreen(_detail);
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

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
  switch (t) {
    case UIEventType::contactMessage:
      buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
      break;
    case UIEventType::channelMessage:
      buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
      break;
    case UIEventType::ack:
      buzzer.play("ack:d=32,o=8,b=120:c");
      break;
    case UIEventType::roomMessage:
    case UIEventType::newContactMessage:
    case UIEventType::none:
    default:
      break;
  }
#endif

#ifdef PIN_VIBRATION
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  // the app drained the queue; if the list is showing, repaint to mirror it
  if (curr == _messages) _next_refresh = 0;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

  // the message is already in the offline queue (single source of truth);
  // just surface the Messages page, which rebuilds from the queue.
  curr_page = PAGE_MESSAGES;
  pages[PAGE_MESSAGES]->resetFocus();
  setCurrScreen(pages[PAGE_MESSAGES]);

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 100;
    }
  }
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

  if (curr == _detail) {
    // ---- read view ----
    //   triangle  any press   = page down (wraps to top)
    //   circle    hold        = back to Messages list (a click won't exit)
    //   circle    double      = Home
    if (ev == BUTTON_EVENT_LONG_PRESS && millis() - ui_started_at < 8000) {
      the_mesh.enterCLIRescue();
    } else if (ev != BUTTON_EVENT_NONE) {
      if (!wakeIfOff()) _detail->scrollDown();
    }
    if (ev2 == BUTTON_EVENT_DOUBLE_CLICK) {
      if (!wakeIfOff()) gotoHomeScreen();
    } else if (ev2 == BUTTON_EVENT_LONG_PRESS) {
      if (!wakeIfOff()) { curr_page = PAGE_MESSAGES; _messages->resetFocus(); setCurrScreen(_messages); }
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
    //   long press   = previous page (back)
    //   double-click = go to home page
    if (ev2 == BUTTON_EVENT_CLICK) {
      if (!wakeIfOff() && onPage()) nextPage();
    } else if (ev2 == BUTTON_EVENT_LONG_PRESS) {
      if (!wakeIfOff() && onPage()) prevPage();
    } else if (ev2 == BUTTON_EVENT_DOUBLE_CLICK) {
      if (!wakeIfOff() && onPage()) gotoHomeScreen();
    }
  }

  if (ev != BUTTON_EVENT_NONE || ev2 != BUTTON_EVENT_NONE) {
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;                        // repaint after interaction
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying()) buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
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
    }
#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
    if (board.isExternalPowered()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
    }
#endif
    if (millis() > _auto_off) {
      // e-ink keeps its image after turnOff(), so repaint once with the
      // selection bar hidden as a visual hint that the screen is asleep.
      if (onPage() && curr != _detail) {
        ElementScreen* s = (ElementScreen*)curr;
        s->setFocusVisible(false);
        _display->startFrame();
        s->render(*_display);
        _display->endFrame();
        s->setFocusVisible(true);   // restore so the bar returns on wake
      }
      _display->turnOff();
    }
#endif
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
