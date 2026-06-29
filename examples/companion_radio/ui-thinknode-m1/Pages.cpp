#include "Pages.h"
#include "UITask.h"
#include "../MyMesh.h"
#include "target.h"
#include "icons.h"
#include <Arduino.h>
#include <string.h>
#include <RTClib.h>   // DateTime for the message read-view timestamp

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
static const char* unreadText(const UIElement&) {   // readable text messages (matches the Messages list)
  static char b[8]; sprintf(b, "%d", the_mesh.getDisplayMsgCount()); return b;
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
static const char* gpsSatsText(const UIElement& e) {
  static char b[12];
  LocationProvider* l = sensors.getLocationProvider();
  if (T(e)->getGPSState() && l) sprintf(b, "%ld", l->satellitesCount());  // live count (shows acquisition before a fix)
  else if (gpsEverFixed())      sprintf(b, "%ld", sensors.last_sats);     // GPS off: count at last fix
  else                          strcpy(b, "--");
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
static void bleDisconnectCb(const UIElement& e) {   // drop the link so another device can pair/connect
  UITask* t = T(e);
  t->disconnectSerial();
  t->showAlert(t->hasConnection() ? "Disconnecting.." : "Not connected", 800);
}
static bool buzzerGet(const UIElement& e)    { return !T(e)->isBuzzerQuiet(); }
static void buzzerToggle(const UIElement& e) { T(e)->toggleBuzzer(); }
static const char* const buzzModeOpts[] = { "CTU", "Beep", "Morse" };
static int  buzzModeGet(const UIElement& e)  { return T(e)->getBuzzerMode(); }
static void buzzModeNext(const UIElement& e) { T(e)->setBuzzerMode(T(e)->getBuzzerMode() + 1); }

// ----- time page callbacks -----
static const char* const timeFmtOpts[] = { "24h", "12h" };
static int  timeFmtGet(const UIElement& e)  { return T(e)->getTimeFormat(); }
static void timeFmtNext(const UIElement& e) { T(e)->setTimeFormat((T(e)->getTimeFormat() + 1) & 1); }
static const char* clockText(const UIElement& e) {
  static char b[12]; T(e)->formatClock(b, sizeof(b)); return b;
}
// UTC offset is split into sign / hours / half-hour controls -- far fewer clicks
// than cycling one combined -12..+14 selector. (Sign at offset 0 is a no-op since
// +0 == -0; set the hours first, then the sign.)
static const char* const utcSignOpts[] = { "+", "-" };
static int  utcSignGet(const UIElement& e)  { return T(e)->getUtcOffsetMin() < 0 ? 1 : 0; }
static void utcSignNext(const UIElement& e) { T(e)->setUtcOffsetMin((int16_t)(-T(e)->getUtcOffsetMin())); }

static const char* utcHoursText(const UIElement& e) {    // magnitude hours: "0".."14"
  static char b[4];
  int m = T(e)->getUtcOffsetMin();
  snprintf(b, sizeof(b), "%d", (m < 0 ? -m : m) / 60);
  return b;
}
static void utcHoursNext(const UIElement& e) {           // step hours 0..14, keep sign + the +30 flag
  int m = T(e)->getUtcOffsetMin();
  int sign = m < 0 ? -1 : 1;
  int mag  = m < 0 ? -m : m;
  int h = mag / 60 + 1;  if (h > 14) h = 0;
  int half = (mag % 60) ? 30 : 0;
  T(e)->setUtcOffsetMin((int16_t)(sign * (h * 60 + half)));
}
static bool utcHalfGet(const UIElement& e)    { return (T(e)->getUtcOffsetMin() % 60) != 0; }
static void utcHalfToggle(const UIElement& e) {          // flip the 30-min part, keeping sign + hours
  int m = T(e)->getUtcOffsetMin();
  int sign = m < 0 ? -1 : 1;
  int mag  = m < 0 ? -m : m;
  mag = (mag / 60) * 60 + ((mag % 60) ? 0 : 30);
  T(e)->setUtcOffsetMin((int16_t)(sign * mag));
}
static void advertCb(const UIElement& e)    { T(e)->doAdvert(); }
static void fullRefreshCb(const UIElement& e) { T(e)->forceFullRefresh(); }
static bool offGridGet(const UIElement& e)    { return T(e)->getOffGrid(); }
static void offGridToggle(const UIElement& e) { T(e)->toggleOffGrid(); }
static const char* const freqOpts[] = { "433", "869", "918" };
static int  freqGet(const UIElement& e)  { return T(e)->getFreqPreset(); }
static void freqNext(const UIElement& e) { T(e)->cycleFreqPreset(); }
static void hibernateCb(const UIElement& e) { ((ShutdownScreen*)e.ctx)->initShutdown(); }

// ----- Radio settings getters (read-only; the actual tuned LoRa params, set via
// the app -- the on-device controls are the Off-grid toggle + freq preset above) -----
static const char* freqText(const UIElement&) {   // tuned centre frequency, MHz
  static char b[12]; sprintf(b, "%g", the_mesh.getNodePrefs()->freq); return b;
}
static const char* bwText(const UIElement&) {      // bandwidth, kHz
  static char b[12]; sprintf(b, "%g", the_mesh.getNodePrefs()->bw); return b;
}
static const char* sfText(const UIElement&) {      // spreading factor
  static char b[6]; sprintf(b, "%d", the_mesh.getNodePrefs()->sf); return b;
}
static const char* crText(const UIElement&) {      // coding rate (4/N)
  static char b[6]; sprintf(b, "%d", the_mesh.getNodePrefs()->cr); return b;
}
static const char* txText(const UIElement&) {      // transmit power
  static char b[10]; sprintf(b, "%ddBm", the_mesh.getNodePrefs()->tx_power_dbm); return b;
}

// ----- message element callbacks -----
static const char* rowTime(const UIElement& e) {   // right-aligned relative time
  MessagesScreen::MsgRef* r = (MessagesScreen::MsgRef*)e.ctx;
  return r->scr->timeAt(r->idx);
}
static void rowActivate(const UIElement& e) {   // open the full-message read view
  MessagesScreen::MsgRef* r = (MessagesScreen::MsgRef*)e.ctx;
  r->scr->openDetail(r->idx);
}
static void convActivate(const UIElement& e) {  // drill into a conversation
  MessagesScreen::MsgRef* r = (MessagesScreen::MsgRef*)e.ctx;
  r->scr->selectConversation(r->idx);
}

// ----- RX log row callbacks + payload-type label -----
static const char* rxRowTime(const UIElement& e) {    // right-aligned HH:MM
  RxLogScreen::RxRef* r = (RxLogScreen::RxRef*)e.ctx;
  return r->scr->timeAt(r->idx);
}
static void rxRowNoop(const UIElement&) {}            // rows scroll but don't drill down

static const char* rxTypeLabel(uint8_t ptype) {
  switch (ptype) {
    case PAYLOAD_TYPE_ADVERT:    return "ADV";
    case PAYLOAD_TYPE_TXT_MSG:   return "MSG";
    case PAYLOAD_TYPE_GRP_TXT:   return "CHAN";
    case PAYLOAD_TYPE_ACK:       return "ACK";
    case PAYLOAD_TYPE_PATH:      return "PATH";
    case PAYLOAD_TYPE_TRACE:     return "TRACE";
    case PAYLOAD_TYPE_REQ:       return "REQ";
    case PAYLOAD_TYPE_RESPONSE:  return "RESP";
    case PAYLOAD_TYPE_ANON_REQ:  return "AREQ";
    case PAYLOAD_TYPE_GRP_DATA:  return "GDAT";
    case PAYLOAD_TYPE_CONTROL:   return "CTRL";
    case PAYLOAD_TYPE_MULTIPART: return "MPRT";
    case PAYLOAD_TYPE_RAW_CUSTOM:return "RAW";
    default:                     return "?";
  }
}

// Compact relative age of an epoch timestamp ("now"/"5m"/"3h"/"2d"), into `out`.
// Empty when the timestamp or the device clock is unusable.
static void relTime(char* out, size_t cap, uint32_t ts) {
  out[0] = 0;
  uint32_t now = the_mesh.getRTCClock()->getCurrentTime();
  if (ts == 0 || now < ts) return;             // unknown, or clock not yet synced
  uint32_t s = now - ts;
  if      (s < 60)    strncpy(out, "now", cap);
  else if (s < 3600)  snprintf(out, cap, "%lum", (unsigned long)(s / 60));
  else if (s < 86400) snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
  else                snprintf(out, cap, "%lud", (unsigned long)(s / 86400));
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
  _items[1] = makeAction("Send Advert", task, advertCb);
  _items[2] = makeLabel("Messages", unreadText, task);
  _items[3] = makeLabel("Uptime",   uptimeText,  task);
  _items[4] = makeLabel("Queue",    queueText,   task);
  _items[5] = makeAction("Full Refresh", task, fullRefreshCb);    // clear e-ink ghosting
  _elems = _items; _count = 6;     // BT/conn now shown as a status-bar icon
}

// ============================================================ MeshScreen
// Mesh-protocol traffic only; RF config + link readings moved to the Radio page.
MeshScreen::MeshScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Mesh") {
  _items[0]  = makeAction("Send Advert", task, advertCb);
  _items[1]  = makeLabel("Contacts", contactsText, task);
  _items[2]  = makeLabel("Sent F/D", sentText,     task);
  _items[3]  = makeLabel("Recv F/D", recvText,     task);
  _items[4]  = makeLabel("Airtime",  airtimeText,  task);
  _items[5]  = makeLabel("Queue",    queueText,    task);
  _elems = _items; _count = 6;
}

// ============================================================ RadioScreen
// RF config + link readings. The Off-grid client-repeat toggle and the 433/869/918
// MHz preset (moved off the Mesh page) are the only editable controls; the LoRa
// params below are read-only mirrors of the app-side config.
RadioScreen::RadioScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Radio") {
  _items[0] = makeToggle("Off-grid", task, offGridGet, offGridToggle);   // client-repeat / relay
  _items[1] = makeCycle("Off grid freq", task, freqOpts, 3, freqGet, freqNext);  // 433/869/918 MHz preset
  _items[2] = makeLabel("Freq", freqText, task);
  _items[3] = makeLabel("BW",   bwText,   task);
  _items[4] = makeLabel("SF",   sfText,   task);
  _items[5] = makeLabel("CR",   crText,   task);
  _items[6] = makeLabel("TX",   txText,   task);
  _items[7] = makeLabel("Noise", noiseText, task);
  _items[8] = makeLabel("RSSI",  rssiText,  task);
  _items[9] = makeLabel("SNR",   snrText,   task);
  _elems = _items; _count = 10;
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
BluetoothScreen::BluetoothScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "BT") {
  _items[0] = makeToggle("Bluetooth", task, bleGet, bleToggle);
  _items[1] = makeLabel("App", appConnText, task);
  _items[2] = makeLabel("Pin", blePinText,  task);
  _items[3] = makeAction("Disconnect", task, bleDisconnectCb);   // drop link -> switch devices
  _elems = _items; _count = 4;
}

// ============================================================ BuzzScreen
BuzzScreen::BuzzScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Buzz") {
  _items[0] = makeToggle("Buzzer", task, buzzerGet, buzzerToggle);
  _items[1] = makeCycle("Sound", task, buzzModeOpts, 3, buzzModeGet, buzzModeNext);
  _elems = _items; _count = 2;
}

// ============================================================ TimeScreen
TimeScreen::TimeScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Time") {
  _items[0] = makeLabel("Time", clockText, task);                       // current local time
  _items[1] = makeCycle("Format", task, timeFmtOpts, 2, timeFmtGet, timeFmtNext);
  _items[2] = makeCycle("UTC +/-", task, utcSignOpts, 2, utcSignGet, utcSignNext);
  _items[3] = makeCycleText("Hours", task, utcHoursText, utcHoursNext);   // magnitude 0-14
  _items[4] = makeToggle("+30 min", task, utcHalfGet, utcHalfToggle);     // half-hour offset
  _elems = _items; _count = 5;
}

// ============================================================ MessagesScreen
MessagesScreen::MessagesScreen(UITask* task, NodePrefs* prefs)
    : ElementScreen(task, prefs, "Msgs") {
  _elems = _items; _count = 0;
}

// Both levels are rebuilt every render straight from the offline queue (the
// single source of truth); _rows/_key_* are a transient render cache, not a
// parallel store. The queue holds up to OFFLINE_QUEUE_SIZE; grouping scans it all
// so counts are accurate (cheap relative to the e-ink refresh cadence).
void MessagesScreen::rebuild() {
  switch (_level) {
    case L_MSGS:  rebuildMessages(); break;
    default:      rebuildConversations(); break;
  }
}

// L_CONV: one row per (channel | DM contact) with a message count + last time.
void MessagesScreen::rebuildConversations() {
  int total = the_mesh.getDisplayMsgCount();
  int groups = 0, counts[MSG_PAGE_MAX];
  MyMesh::MsgView v;
  for (int i = 0; i < total; i++) {               // i==0 is newest
    if (!the_mesh.getDisplayMsg(i, v)) continue;
    int g = -1;
    for (int k = 0; k < groups; k++)
      if (_key_chan[k] == v.is_channel && strcmp(_key_name[k], v.sender) == 0) { g = k; break; }
    if (g < 0) {
      if (groups >= MSG_PAGE_MAX) continue;        // out of row slots
      g = groups++;
      _key_chan[g] = v.is_channel;
      snprintf(_key_name[g], sizeof(_key_name[g]), "%s", v.sender);
      // last heard: local receive time (reliable), fall back to sender's clock
      relTime(_rows[g].time, sizeof(_rows[g].time), v.rx_timestamp ? v.rx_timestamp : v.timestamp);
      counts[g] = 0;
    }
    counts[g]++;
  }
  if (groups == 0) {
    _items[0] = makeLabel("No messages", nullptr, nullptr);
    _elems = _items; _count = 1;
    return;
  }
  for (int g = 0; g < groups; g++) {
    const char* hash = (_key_chan[g] && _key_name[g][0] != '#') ? "#" : "";
    snprintf(_rows[g].line, sizeof(_rows[g].line), "%s%s (%d)", hash, _key_name[g], counts[g]);
    _refs[g].scr = this; _refs[g].idx = g;
    _items[g] = makeMessageRow(_rows[g].line, &_refs[g], rowTime, convActivate);
  }
  _elems = _items; _count = groups;
}

// L_MSGS: every message in the selected conversation, newest first. Channel
// bodies already carry the "Node: " sender prefix, so the row is just the body
// ("Alice: hey all"); DM bodies are clean.
void MessagesScreen::rebuildMessages() {
  int total = the_mesh.getDisplayMsgCount();
  int n = 0, matched = 0;
  MyMesh::MsgView v;
  for (int i = 0; i < total; i++) {
    if (!the_mesh.getDisplayMsg(i, v)) continue;
    if (v.is_channel != _sel_is_channel || strcmp(v.sender, _sel_conv) != 0) continue;
    matched++;
    if (n >= MSG_PAGE_MAX) continue;               // keep counting for the overflow label
    snprintf(_rows[n].line, sizeof(_rows[n].line), "%s", v.body);
    // last heard: local receive time (reliable), fall back to sender's clock
    relTime(_rows[n].time, sizeof(_rows[n].time), v.rx_timestamp ? v.rx_timestamp : v.timestamp);
    _refs[n].scr = this; _refs[n].idx = i;         // absolute display index for openDetail
    _items[n] = makeMessageRow(_rows[n].line, &_refs[n], rowTime, rowActivate);
    n++;
  }
  if (n == 0) {                                    // conversation emptied (synced)
    _items[0] = makeLabel("No messages", nullptr, nullptr);
    _elems = _items; _count = 1;
    return;
  }
  int count = n;
  if (matched > n) {
    snprintf(_more, sizeof(_more), "+%d more on app", matched - n);
    _items[n] = makeLabel(_more, nullptr, nullptr);
    count = n + 1;
  }
  _elems = _items; _count = count;
}

void MessagesScreen::selectConversation(int row) {
  if (row < 0 || row >= MSG_PAGE_MAX) return;
  _sel_is_channel = _key_chan[row];
  snprintf(_sel_conv, sizeof(_sel_conv), "%s", _key_name[row]);
  _level = L_MSGS;
  ElementScreen::resetFocus();                     // rebuild new level + focus first row
}

void MessagesScreen::goBack() {                    // message list -> conversation list
  _level = L_CONV;
  ElementScreen::resetFocus();
}

void MessagesScreen::resetFocus() {                // re-entering the page starts at the top
  _level = L_CONV;
  _sel_conv[0] = 0;
  ElementScreen::resetFocus();
}

void MessagesScreen::openDetail(int display_idx) {
  _task->openMessageAt(display_idx);
}

// ============================================================ MessageDetailScreen
#define WRAP_MAX_LINES 24
#define WRAP_LINE_CAP  48

static int utf8CharLen(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

// Column-wrap `g` (UTF-8) to `max_w` pixels into lines[]. Returns line count.
// Preserves all whitespace and honors explicit '\n' -- it does NOT reflow words
// or collapse spaces -- so ASCII art keeps its exact layout. Long lines hard-wrap
// at the right edge. Advances whole codepoints so multibyte glyphs never split.
static int wrapText(DisplayDriver& d, int max_w, const char* g,
                    char lines[][WRAP_LINE_CAP], int max_lines) {
  int nl = 0, li = 0;
  lines[0][0] = 0;
  char cand[WRAP_LINE_CAP + 4];
  const char* p = g;
  while (*p && nl < max_lines) {
    if (*p == '\n') {                                   // explicit break (preserved)
      lines[nl][li] = 0;
      if (++nl < max_lines) { li = 0; lines[nl][0] = 0; }
      p++; continue;
    }
    int cl = utf8CharLen((unsigned char)*p);            // bytes in this codepoint
    for (int k = 1; k < cl; k++) if (!p[k]) { cl = 1; break; }   // guard truncation

    bool overflow = (li + cl >= WRAP_LINE_CAP);
    if (!overflow) {
      memcpy(cand, lines[nl], li);
      memcpy(cand + li, p, cl);
      cand[li + cl] = 0;
      if ((int)d.getTextWidth(cand) > max_w) overflow = true;
    }
    if (overflow && li > 0) {                           // wrap before this glyph
      lines[nl][li] = 0;
      if (++nl >= max_lines) break;
      li = 0; lines[nl][0] = 0;
    }
    memcpy(lines[nl] + li, p, cl);
    li += cl; lines[nl][li] = 0;
    p += cl;
  }
  if (li > 0 && nl < max_lines) { lines[nl][li] = 0; nl++; }
  return nl;
}

static const int MD_BODY_TOP = 40;   // below the 2-line 16px header (name + date/time)
static const int MD_BOTTOM   = 196;
static int mdLinesPerPage() { return (MD_BOTTOM - MD_BODY_TOP) / UIELEM_ROW_H; }

bool MessageDetailScreen::scrollDown() {
  int per = mdLinesPerPage();
  if (_scroll_line + per < _total_lines) { _scroll_line += per; return true; }
  return false;   // at the end; caller advances to the next message
}

int MessageDetailScreen::render(DisplayDriver& d) {
  d.setTextSize(1);

  // header line 1: breadcrumb. For a channel message the sender's node name is
  // embedded in the body as a "Name: " prefix (out.sender holds the *channel*
  // name); lift it onto the header as "#channel node" (channel first) so the body
  // stays clean -- which lets ASCII art render. Direct messages already have the
  // node name in out.sender and a clean body.
  const char* body = _msg.body;
  char hdr[56];
  if (_msg.is_channel) {
    const char* chan = _msg.sender;
    const char* sep = strstr(_msg.body, ": ");
    if (sep) {
      char who[28]; int n = (int)(sep - _msg.body);
      if (n > (int)sizeof(who) - 1) n = sizeof(who) - 1;
      memcpy(who, _msg.body, n); who[n] = 0;
      snprintf(hdr, sizeof(hdr), "%s%s %s", chan[0] == '#' ? "" : "#", chan, who);
      body = sep + 2;                                   // body after the "Name: " prefix
    } else {
      snprintf(hdr, sizeof(hdr), "%s%s", chan[0] == '#' ? "" : "#", chan);
    }
  } else {
    snprintf(hdr, sizeof(hdr), "%s", _msg.sender);
  }
  char hb[64];
  d.setColor(DisplayDriver::GREEN);
  d.translateUTF8ToBlocks(hb, hdr, sizeof(hb));
  d.drawTextEllipsized(0, 2, d.width(), hb);

  // header line 2: "<date> - <route>" (date honors the Time page format + offset)
  char date[24], when[40];
  _task->formatDateTime(_msg.timestamp, date, sizeof(date));
  if (_msg.is_direct) {
    snprintf(when, sizeof(when), "%s - Direct", date);
  } else {
    // "date - <hops>h - <bytes-per-repeater-hash>b" (hash size = top 2 bits of path_len)
    unsigned hash_bytes = (_msg.path_len >> 6) + 1;
    snprintf(when, sizeof(when), "%s - %uh - %ub", date, (unsigned)_msg.hops, hash_bytes);
  }
  d.setColor(DisplayDriver::LIGHT);
  d.setCursor(0, 20);
  d.print(when);
  d.fillRect(0, 38, d.width(), 1);

  // body, word-wrapped (reserve a right gutter for the list scrollbar). Translate
  // to display glyphs once so wrap widths match drawing and breaks are UTF-8 safe.
  const int gutter = 4;
  char lines[WRAP_MAX_LINES][WRAP_LINE_CAP];
  char body_glyphs[MAX_FRAME_SIZE];
  d.translateUTF8ToBlocks(body_glyphs, body, sizeof(body_glyphs));   // cleaned body (no "Name: " prefix)
  _total_lines = wrapText(d, d.width() - 4 - gutter, body_glyphs, lines, WRAP_MAX_LINES);
  if (_scroll_line >= _total_lines) _scroll_line = 0;

  const int per = mdLinesPerPage();
  d.setColor(DisplayDriver::LIGHT);
  for (int i = 0; i < per && (_scroll_line + i) < _total_lines; i++) {
    d.setCursor(2, MD_BODY_TOP + i * UIELEM_ROW_H);
    d.print(lines[_scroll_line + i]);   // already glyph-translated
  }
  if (_scroll_line + per < _total_lines) {        // more body below: circle pages down
    d.drawTextRightAlign(d.width() - gutter - 1, MD_BOTTOM - UIELEM_ROW_H, "v");
  }

  // list scrollbar: this message's position in the list (newest at top); circle
  // pages through the body then on to the next message.
  if (_total > 1) {
    int x = d.width() - 2;
    int top = MD_BODY_TOP, h = MD_BOTTOM - MD_BODY_TOP;
    d.setColor(DisplayDriver::LIGHT);
    d.drawRect(x, top, 1, h);
    int thumb_h = h / _total;
    if (thumb_h < 4) thumb_h = 4;
    if (thumb_h > h) thumb_h = h;
    int thumb_y = top + _idx * (h - thumb_h) / (_total - 1);
    d.fillRect(x, thumb_y, 2, thumb_h);
  }
  return 10000;   // e-ink: repaint only on interaction
}

// ============================================================ HelpScreen
int HelpScreen::render(DisplayDriver& d) {
  d.setTextSize(1);
  d.setColor(DisplayDriver::GREEN);
  d.setCursor(0, 2);
  d.print("Help");
  d.setColor(DisplayDriver::LIGHT);
  d.fillRect(0, 20, d.width(), 1);

  // ---- status-bar icon legend (the same Unifont glyphs the bar uses) ----
  const char* icons[]  = { "\xF0\x9F\x93\xB1", "\xF0\x9F\x93\xB5", "\xF0\x9F\x93\x8D", "\xF0\x9F\x94\x87", "\xE2\x9A\xA1" }; // 📱📵📍🔇⚡
  const char* labels[] = { "App linked", "BLE off", "GPS on", "Muted", "Charging" };
  int y = 24;
  for (int i = 0; i < 5; i++) {
    d.setCursor(2, y);  d.print(icons[i]);
    d.setCursor(22, y); d.print(labels[i]);
    y += 18;
  }

  // ---- two buttons: ● moves/selects within a screen, ▲ navigates pages + always backs out ----
  d.fillRect(0, y, d.width(), 1);                             // separator
  y += 5;
  d.setCursor(2, y);       d.print("\xE2\x97\x8F");           // ● (circle = function/select)
  d.setCursor(22, y);      d.print("click next  hold prev");
  d.setCursor(22, y + 18); d.print("2x: select / open");
  y += 40;
  d.setCursor(2, y);       d.print("\xE2\x96\xB2");           // ▲ (triangle = page)
  d.setCursor(22, y);      d.print("click page  hold BACK");
  d.setCursor(22, y + 18); d.print("2x home  3x help");

  return 3600000;   // static: only repaints on interaction (which dismisses it)
}

// ============================================================ ShutdownScreen
ShutdownScreen::ShutdownScreen(UITask* task, NodePrefs* prefs)
    : ElementScreen(task, prefs, "Pwr"), _shutdown_init(false) {
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

// ============================================================ AdvertsScreen
RxLogScreen::RxLogScreen(UITask* task, NodePrefs* prefs)
    : ElementScreen(task, prefs, "RX Log") {
  _elems = _items; _count = 0;
}

// Rebuilt every render from MyMesh's rx_log ring (newest first). Each row:
// "<TYPE> <rssi>/<+snr> <name>" with the reception clock time right-aligned.
void RxLogScreen::rebuild() {
  static RxLogEntry rx[RX_LOG_SIZE];                   // transient scratch (single-threaded)
  int got = the_mesh.getRxLog(rx, RX_LOG_SIZE);
  int n = 0;
  for (int i = 0; i < got; i++) {
    const RxLogEntry& e = rx[i];
    snprintf(_rows[n].line, sizeof(_rows[n].line), "%s %d/%+d %s",
             rxTypeLabel(e.ptype), (int)e.rssi, (int)e.snr, e.name[0] ? e.name : "?");
    if (e.timestamp) uiFormatClock(_prefs, e.timestamp, _rows[n].time, sizeof(_rows[n].time));
    else             _rows[n].time[0] = 0;
    _refs[n].scr = this; _refs[n].idx = n;
    _items[n] = makeMessageRow(_rows[n].line, &_refs[n], rxRowTime, rxRowNoop);
    n++;
  }
  if (n == 0) {
    _items[0] = makeLabel("No packets yet", nullptr, nullptr);
    _elems = _items; _count = 1;
    return;
  }
  _elems = _items; _count = n;
}
