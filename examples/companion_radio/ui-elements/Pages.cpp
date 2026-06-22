#include "Pages.h"
#include "UITask.h"
#include "../MyMesh.h"
#include "target.h"
#include "icons.h"
#include <Arduino.h>
#include <string.h>

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif

// ------------------------------------------------------------------ helpers
static UITask* T(const UIElement& e) { return (UITask*)e.ctx; }

// ----- Home / shared status getters -----
static const char* battPctText(const UIElement& e) {
  static char b[8];
  int mv = T(e)->getBattMilliVolts();
  int pct = (mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  sprintf(b, "%d%%", pct);
  return b;
}
static const char* uptimeText(const UIElement&) {
  static char b[16];
  unsigned long s = millis() / 1000;
  int d = s / 86400; s %= 86400;
  int h = s / 3600;  s %= 3600;
  int m = s / 60;
  if (d > 0)      sprintf(b, "%dd %dh", d, h);
  else if (h > 0) sprintf(b, "%dh %dm", h, m);
  else            sprintf(b, "%dm", m);
  return b;
}
static const char* chargingText(const UIElement&) {
  return board.isExternalPowered() ? "Yes" : "No";
}
static const char* appConnText(const UIElement& e)  { return T(e)->hasConnection() ? "Connected" : "--"; }
static const char* unreadText(const UIElement& e) {
  static char b[8]; sprintf(b, "%d", T(e)->getMsgCount()); return b;
}

// ----- GPS getters (read the global location provider) -----
static const char* gpsFixText(const UIElement&) {
  LocationProvider* l = sensors.getLocationProvider();
  return (l && l->isValid()) ? "Yes" : "No";
}
static const char* gpsSatsText(const UIElement&) {
  static char b[12]; LocationProvider* l = sensors.getLocationProvider();
  if (l) sprintf(b, "%ld", l->satellitesCount()); else strcpy(b, "--");
  return b;
}
static const char* gpsLatLonText(const UIElement&) {
  static char b[28]; LocationProvider* l = sensors.getLocationProvider();
  if (l && l->isValid())
    sprintf(b, "%.4f,%.4f", l->getLatitude() / 1000000.0, l->getLongitude() / 1000000.0);
  else strcpy(b, "--");
  return b;
}
static const char* gpsAltText(const UIElement&) {
  static char b[16]; LocationProvider* l = sensors.getLocationProvider();
  if (l && l->isValid()) sprintf(b, "%.1fm", l->getAltitude() / 1000.0); else strcpy(b, "--");
  return b;
}

// ----- Mesh stats getters -----
static const char* contactsText(const UIElement&) {
  static char b[8]; sprintf(b, "%d", the_mesh.getNumContacts()); return b;
}
static const char* sentText(const UIElement&) {
  static char b[20]; sprintf(b, "%lu/%lu",
    (unsigned long)the_mesh.getNumSentFlood(), (unsigned long)the_mesh.getNumSentDirect());
  return b;
}
static const char* recvText(const UIElement&) {
  static char b[20]; sprintf(b, "%lu/%lu",
    (unsigned long)the_mesh.getNumRecvFlood(), (unsigned long)the_mesh.getNumRecvDirect());
  return b;
}
static const char* airtimeText(const UIElement&) {
  static char b[16]; sprintf(b, "%lus", (unsigned long)(the_mesh.getTotalAirTime() / 1000)); return b;
}
static const char* noiseText(const UIElement&) {
  static char b[12]; sprintf(b, "%d", radio_driver.getNoiseFloor()); return b;
}
static const char* rssiText(const UIElement&) {
  static char b[12]; sprintf(b, "%d", (int)radio_driver.getLastRSSI()); return b;
}
static const char* snrText(const UIElement&) {
  static char b[12]; sprintf(b, "%d", (int)radio_driver.getLastSNR()); return b;
}

// ----- Bluetooth getters -----
static const char* blePinText(const UIElement&) {
  static char b[12]; uint32_t pin = the_mesh.getBLEPin();
  if (pin) sprintf(b, "%u", (unsigned)pin); else strcpy(b, "--");
  return b;
}

// ----- toggle / action callbacks -----
static bool gpsGet(const UIElement& e)    { return T(e)->getGPSState(); }
static void gpsToggle(const UIElement& e) { T(e)->toggleGPS(); }
static bool bleGet(const UIElement& e)    { return T(e)->isSerialEnabled(); }
static void bleToggle(const UIElement& e) {
  UITask* t = T(e);
  if (t->isSerialEnabled()) t->disableSerial(); else t->enableSerial();
  t->showAlert(t->isSerialEnabled() ? "BLE: ON" : "BLE: OFF", 800);
}
static bool buzzerGet(const UIElement& e)    { return !T(e)->isBuzzerQuiet(); }
static void buzzerToggle(const UIElement& e) { T(e)->toggleBuzzer(); }
static void advertCb(const UIElement& e)    { T(e)->doAdvert(); }
static void hibernateCb(const UIElement& e) { ((ShutdownScreen*)e.ctx)->initShutdown(); }

// ----- message element callbacks -----
static const char* msgBody(const UIElement& e) {
  return ((MessagesScreen::MsgRef*)e.ctx)->e->msg;
}
static void msgActivate(const UIElement& e) {
  MessagesScreen::MsgRef* r = (MessagesScreen::MsgRef*)e.ctx;
  r->scr->removeEntry(r->e);
}

// ============================================================ SplashScreen
SplashScreen::SplashScreen(UITask* task) : _task(task) {
  const char *ver = FIRMWARE_VERSION;
  const char *dash = strchr(ver, '-');
  int len = dash ? dash - ver : strlen(ver);
  if (len >= (int)sizeof(_version_info)) len = sizeof(_version_info) - 1;
  memcpy(_version_info, ver, len);
  _version_info[len] = 0;
  dismiss_after = millis() + 3000;
}

int SplashScreen::render(DisplayDriver& display) {
  display.setColor(DisplayDriver::BLUE);
  int logoWidth = 128;
  display.drawXbm((display.width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

  display.setColor(DisplayDriver::LIGHT);
  display.setTextSize(1);
  const char* website = "https://meshcore.io";
  uint16_t ww = display.getTextWidth(website);
  display.setCursor((display.width() - ww) / 2, 22);
  display.print(website);

  display.drawTextCentered(display.width() / 2, 35, _version_info);
  display.drawTextCentered(display.width() / 2, 48, FIRMWARE_BUILD_DATE);
  return 1000;
}

void SplashScreen::poll() {
  if (millis() >= dismiss_after) _task->gotoHomeScreen();
}

// ============================================================ HomeScreen
HomeScreen::HomeScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Home") {
  _items[0] = makeLabel(prefs->node_name, nullptr, task);          // node name
  _items[1] = makeToggle("Buzzer",    task, buzzerGet, buzzerToggle);
  _items[2] = makeToggle("Bluetooth", task, bleGet,    bleToggle);
  _items[3] = makeToggle("GPS",       task, gpsGet,    gpsToggle);
  _items[4] = makeAction("Send Advert", task, advertCb);
  _items[5] = makeLabel("App",    appConnText, task);
  _items[6] = makeLabel("Unread", unreadText,  task);
  _items[7] = makeLabel("Uptime", uptimeText,  task);
  _elems = _items; _count = 8;
}

// ============================================================ MeshScreen
MeshScreen::MeshScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Mesh") {
  _items[0] = makeLabel("Contacts", contactsText, task);
  _items[1] = makeLabel("Sent F/D", sentText,     task);
  _items[2] = makeLabel("Recv F/D", recvText,     task);
  _items[3] = makeLabel("Airtime",  airtimeText,  task);
  _items[4] = makeLabel("Noise",    noiseText,    task);
  _items[5] = makeLabel("RSSI",     rssiText,     task);
  _items[6] = makeLabel("SNR",      snrText,      task);
  _items[7] = makeAction("Send Advert", task, advertCb);
  _elems = _items; _count = 8;
}

// ============================================================ GPSScreen
GPSScreen::GPSScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "GPS") {
  _items[0] = makeToggle("GPS", task, gpsGet, gpsToggle);
  _items[1] = makeLabel("Fix",  gpsFixText,    task);
  _items[2] = makeLabel("Sats", gpsSatsText,   task);
  _items[3] = makeLabel("Pos",  gpsLatLonText, task);
  _items[4] = makeLabel("Alt",  gpsAltText,    task);
  _elems = _items; _count = 5;
}

// ============================================================ BluetoothScreen
BluetoothScreen::BluetoothScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Bluetooth") {
  _items[0] = makeToggle("Bluetooth", task, bleGet, bleToggle);
  _items[1] = makeLabel("App", appConnText, task);
  _items[2] = makeLabel("Pin", blePinText,  task);
  _elems = _items; _count = 3;
}

// ============================================================ MessagesScreen
MessagesScreen::MessagesScreen(UITask* task, NodePrefs* prefs)
    : ElementScreen(task, prefs, "Messages"), _num(0) {
  _elems = _items; _count = 0;
}

void MessagesScreen::addPreview(uint8_t path_len, const char* from_name, const char* msg) {
  if (_num >= MAX_UNREAD_MSGS) {                 // drop oldest
    for (int i = 1; i < _num; i++) _msgs[i - 1] = _msgs[i];
    _num--;
  }
  MsgEntry* p = &_msgs[_num++];
  p->timestamp = 0;
  if (path_len == 0xFF) snprintf(p->origin, sizeof(p->origin), "(D) %s:", from_name);
  else                  snprintf(p->origin, sizeof(p->origin), "(%u) %s:", (unsigned)(path_len & 63), from_name);  // low 6 bits = hop count (Packet::getPathHashCount)
  strncpy(p->msg, msg, sizeof(p->msg) - 1);
  p->msg[sizeof(p->msg) - 1] = 0;
}

void MessagesScreen::removeEntry(MsgEntry* e) {
  int idx = (int)(e - _msgs);
  if (idx < 0 || idx >= _num) return;
  for (int i = idx + 1; i < _num; i++) _msgs[i - 1] = _msgs[i];
  _num--;
}

void MessagesScreen::rebuild() {
  if (_num <= 0) {
    _items[0] = makeLabel("No messages", nullptr, nullptr);
    _elems = _items; _count = 1;
    return;
  }
  for (int i = 0; i < _num; i++) {
    int storage = _num - 1 - i;               // newest first
    _refs[i].scr = this;
    _refs[i].e = &_msgs[storage];
    _items[i] = makeTwoRow(_msgs[storage].origin, &_refs[i], msgBody, msgActivate);
  }
  _elems = _items; _count = _num;
}

// ============================================================ ShutdownScreen
ShutdownScreen::ShutdownScreen(UITask* task, NodePrefs* prefs)
    : ElementScreen(task, prefs, "Power"), _shutdown_init(false) {
  _elems = _items; _count = 0;
}

void ShutdownScreen::rebuild() {
  if (_shutdown_init) {
    _items[0] = makeLabel("Hibernating...", nullptr, nullptr);
    _elems = _items; _count = 1;
  } else {
    _items[0] = makeLabel("Battery",  battPctText,  _task);
    _items[1] = makeLabel("Charging", chargingText, _task);
    _items[2] = makeAction("Hibernate", this, hibernateCb);
    _elems = _items; _count = 3;
  }
}

void ShutdownScreen::poll() {
  if (_shutdown_init && !_task->isButtonPressed()) _task->shutdown();
}
