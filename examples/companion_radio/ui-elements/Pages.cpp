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
static const char* battVoltsText(const UIElement& e) {
  static char b[10]; sprintf(b, "%.2fV", T(e)->getBattMilliVolts() / 1000.0); return b;
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

// ----- GPS getters -----
// Position/sats/alt always show the LAST good fix (cached in sensors.node_*,
// only written while a fix is valid), so the page keeps showing the last known
// location after you go inside or switch GPS off. "Fix" reports live vs not, and
// "Last" shows how long ago the data was valid.
static bool gpsLive(const UIElement& e) {                 // powered on AND a current valid fix
  LocationProvider* l = sensors.getLocationProvider();
  return T(e)->getGPSState() && l && l->isValid();
}
static bool gpsEverFixed() { return sensors.last_fix_ms != 0; }

static const char* gpsFixText(const UIElement& e) {
  return gpsLive(e) ? "Live" : "No";
}
static const char* gpsLastFixText(const UIElement& e) {
  static char b[16];
  if (!gpsEverFixed()) { strcpy(b, "--");  return b; }
  if (gpsLive(e))      { strcpy(b, "now"); return b; }
  unsigned long s = (millis() - sensors.last_fix_ms) / 1000;   // unsigned, wrap-safe
  if (s < 60)        sprintf(b, "%lus ago", s);
  else if (s < 3600) sprintf(b, "%lum ago", s / 60);
  else               sprintf(b, "%luh ago", s / 3600);
  return b;
}
static const char* gpsSatsText(const UIElement&) {
  static char b[12];
  if (gpsEverFixed()) sprintf(b, "%ld", sensors.last_sats); else strcpy(b, "--");
  return b;
}
static const char* gpsLatLonText(const UIElement&) {
  static char b[28];
  if (gpsEverFixed()) sprintf(b, "%.4f,%.4f", sensors.node_lat, sensors.node_lon);
  else strcpy(b, "--");
  return b;
}
static const char* gpsAltText(const UIElement&) {
  static char b[16];
  if (gpsEverFixed()) sprintf(b, "%.1fm", sensors.node_altitude); else strcpy(b, "--");
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
static const char* queueText(const UIElement&) {   // unread-by-app depth / capacity
  static char b[12]; sprintf(b, "%d/%d", the_mesh.getOfflineQueueLen(), OFFLINE_QUEUE_SIZE); return b;
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
static const char* rowBody(const UIElement& e) {
  MessagesScreen::MsgRef* r = (MessagesScreen::MsgRef*)e.ctx;
  return r->scr->previewAt(r->idx);
}
static void rowActivate(const UIElement& e) {   // open the full-message read view
  MessagesScreen::MsgRef* r = (MessagesScreen::MsgRef*)e.ctx;
  r->scr->openDetail(r->idx);
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
  _items[4] = makeLabel("App",    appConnText, task);
  _items[5] = makeLabel("Unread", unreadText,  task);
  _items[6] = makeLabel("Uptime", uptimeText,  task);
  _elems = _items; _count = 7;
}

// ============================================================ MeshScreen
MeshScreen::MeshScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Mesh") {
  _items[0] = makeAction("Send Advert", task, advertCb);
  _items[1] = makeLabel("Contacts", contactsText, task);
  _items[2] = makeLabel("Sent F/D", sentText,     task);
  _items[3] = makeLabel("Recv F/D", recvText,     task);
  _items[4] = makeLabel("Airtime",  airtimeText,  task);
  _items[5] = makeLabel("Noise",    noiseText,    task);
  _items[6] = makeLabel("RSSI",     rssiText,     task);
  _items[7] = makeLabel("SNR",      snrText,      task);
  _items[8] = makeLabel("Queue",    queueText,    task);
  _elems = _items; _count = 9;
}

// ============================================================ GPSScreen
GPSScreen::GPSScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "GPS") {
  _items[0] = makeToggle("GPS",  task, gpsGet, gpsToggle);
  _items[1] = makeLabel("Fix",   gpsFixText,     task);
  _items[2] = makeLabel("Last",  gpsLastFixText, task);
  _items[3] = makeLabel("Sats",  gpsSatsText,    task);
  _items[4] = makeLabel("Pos",   gpsLatLonText,  task);
  _items[5] = makeLabel("Alt",   gpsAltText,     task);
  _elems = _items; _count = 6;
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
    : ElementScreen(task, prefs, "Messages") {
  _elems = _items; _count = 0;
}

// Rebuilt every render straight from the offline queue (the single source of
// truth). _rows is a transient render cache, not a parallel message store.
void MessagesScreen::rebuild() {
  int total = the_mesh.getDisplayMsgCount();
  int n = total > MSG_PAGE_MAX ? MSG_PAGE_MAX : total;
  if (n <= 0) {
    _items[0] = makeLabel("No messages", nullptr, nullptr);
    _elems = _items; _count = 1;
    return;
  }
  MyMesh::MsgView v;
  for (int i = 0; i < n; i++) {            // i==0 is newest
    if (!the_mesh.getDisplayMsg(i, v)) { _rows[i].origin[0] = _rows[i].preview[0] = 0; }
    else {
      if (v.is_direct) snprintf(_rows[i].origin, sizeof(_rows[i].origin), "(D) %s:", v.sender);
      else             snprintf(_rows[i].origin, sizeof(_rows[i].origin), "(%u) %s:", (unsigned)v.hops, v.sender);
      strncpy(_rows[i].preview, v.body, sizeof(_rows[i].preview) - 1);
      _rows[i].preview[sizeof(_rows[i].preview) - 1] = 0;
    }
    _refs[i].scr = this; _refs[i].idx = i;
    _items[i] = makeTwoRow(_rows[i].origin, &_refs[i], rowBody, rowActivate);
  }
  int count = n;
  if (total > n) {     // more buffered for the app than we list on-device
    snprintf(_more, sizeof(_more), "+%d more on app", total - n);
    _items[n] = makeLabel(_more, nullptr, nullptr);
    count = n + 1;
  }
  _elems = _items; _count = count;
}

void MessagesScreen::openDetail(int display_idx) {
  MyMesh::MsgView v;
  if (the_mesh.getDisplayMsg(display_idx, v)) _task->openMessage(v);
}

// ============================================================ MessageDetailScreen
#define WRAP_MAX_LINES 24
#define WRAP_LINE_CAP  48

// Greedy word-wrap of `text` to `max_w` pixels into lines[]. Returns line count.
// Widths are measured on the block-translated candidate (matches how lines draw).
static int wrapText(DisplayDriver& d, int max_w, const char* text,
                    char lines[][WRAP_LINE_CAP], int max_lines) {
  int nl = 0; size_t li = 0;
  lines[0][0] = 0;
  char blocks[WRAP_LINE_CAP * 2];
  const char* p = text;
  while (*p && nl < max_lines) {
    if (*p == '\n') {                                   // explicit break
      lines[nl][li] = 0;
      if (++nl < max_lines) { lines[nl][0] = 0; li = 0; }
      p++; continue;
    }
    const char* ws = p;                                 // next word
    while (*p && *p != ' ' && *p != '\n') p++;
    size_t wlen = p - ws;
    if (wlen >= WRAP_LINE_CAP) wlen = WRAP_LINE_CAP - 1; // clamp a monster word

    bool wrap = false;
    if (li + wlen >= WRAP_LINE_CAP) {
      wrap = (li > 0);
    } else if (li > 0) {
      char cand[WRAP_LINE_CAP];
      memcpy(cand, lines[nl], li); memcpy(cand + li, ws, wlen); cand[li + wlen] = 0;
      d.translateUTF8ToBlocks(blocks, cand, sizeof(blocks));
      if ((int)d.getTextWidth(blocks) > max_w) wrap = true;
    }
    if (wrap) {
      lines[nl][li] = 0;
      if (++nl >= max_lines) break;
      li = 0; lines[nl][0] = 0;
    }
    memcpy(lines[nl] + li, ws, wlen); li += wlen; lines[nl][li] = 0;
    while (*p == ' ') {                                  // keep spaces, not at line start
      if (li > 0 && li < WRAP_LINE_CAP - 1) { lines[nl][li++] = ' '; lines[nl][li] = 0; }
      p++;
    }
  }
  if (li > 0 && nl < max_lines) { lines[nl][li] = 0; nl++; }
  return nl;
}

static const int MD_BODY_TOP = 14;
static const int MD_BOTTOM   = 124;
static int mdLinesPerPage() { return (MD_BOTTOM - MD_BODY_TOP) / UIELEM_ROW_H; }

void MessageDetailScreen::scrollDown() {
  int per = mdLinesPerPage();
  if (_scroll_line + per < _total_lines) _scroll_line += per;
  else _scroll_line = 0;   // wrap back to the top
}

int MessageDetailScreen::render(DisplayDriver& d) {
  d.setTextSize(1);

  // header: sender + (D)/hop count, mirroring the status-bar style
  char origin[40], hdr[48];
  if (_msg.is_direct) snprintf(origin, sizeof(origin), "(D) %s", _msg.sender);
  else                snprintf(origin, sizeof(origin), "(%u) %s", (unsigned)_msg.hops, _msg.sender);
  d.setColor(DisplayDriver::GREEN);
  d.translateUTF8ToBlocks(hdr, origin, sizeof(hdr));
  d.drawTextEllipsized(0, 0, d.width(), hdr);
  d.setColor(DisplayDriver::LIGHT);
  d.fillRect(0, 11, d.width(), 1);

  // body, word-wrapped
  char lines[WRAP_MAX_LINES][WRAP_LINE_CAP];
  char blocks[WRAP_LINE_CAP * 2];
  _total_lines = wrapText(d, d.width() - 4, _msg.body, lines, WRAP_MAX_LINES);
  if (_scroll_line >= _total_lines) _scroll_line = 0;

  const int per = mdLinesPerPage();
  d.setColor(DisplayDriver::LIGHT);
  for (int i = 0; i < per && (_scroll_line + i) < _total_lines; i++) {
    d.translateUTF8ToBlocks(blocks, lines[_scroll_line + i], sizeof(blocks));
    d.setCursor(2, MD_BODY_TOP + i * UIELEM_ROW_H);
    d.print(blocks);
  }
  if (_scroll_line + per < _total_lines) {        // more below: hint at triangle=page-down
    d.drawTextRightAlign(d.width() - 2, MD_BOTTOM - UIELEM_ROW_H, "v");
  }
  return 10000;   // e-ink: repaint only on interaction
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
    _items[0] = makeLabel("Battery",  battPctText,   _task);
    _items[1] = makeLabel("Voltage",  battVoltsText, _task);
    _items[2] = makeLabel("Charging", chargingText,  _task);
    _items[3] = makeAction("Hibernate", this, hibernateCb);
    _elems = _items; _count = 4;
  }
}

void ShutdownScreen::poll() {
  if (_shutdown_init && !_task->isButtonPressed()) _task->shutdown();
}
