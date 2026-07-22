#include "Pages.h"
#include "UITask.h"
#include "../MyMesh.h"
#include "target.h"
#include "icons.h"
#include <Arduino.h>
#include <string.h>
#include <math.h>     // great-circle math for the Nav page
#include <RTClib.h>   // DateTime for the message read-view timestamp
#include <qrcode.h>   // ricmoo/QRCode -- Home-screen self-advert QR

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
  t->showAlert(t->isSerialEnabled() ? "BLE: ON" : "BLE: OFF");
}
static void bleDisconnectCb(const UIElement& e) {   // drop the link so another device can pair/connect
  UITask* t = T(e);
  t->disconnectSerial();
  t->showAlert(t->hasConnection() ? "Disconnecting.." : "Not connected");
}
static bool buzzerGet(const UIElement& e)    { return !T(e)->isBuzzerQuiet(); }
static void buzzerToggle(const UIElement& e) { T(e)->toggleBuzzer(); }
static const char* const buzzModeOpts[] = { "CTU", "Beep", "Morse" };
static int  buzzModeGet(const UIElement& e)  { return T(e)->getBuzzerMode(); }
static void buzzModeNext(const UIElement& e) { T(e)->setBuzzerMode(T(e)->getBuzzerMode() + 1); }
static const char* const beepOnOpts[] = { "Msgs", "Pkts" };
static int  beepModeGet(const UIElement& e)  { return T(e)->getBeepMode(); }
static void beepModeNext(const UIElement& e) { T(e)->cycleBeepMode(); }

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
static bool autoAdvGet(const UIElement& e)        { return T(e)->getAutoAdvert(); }
static void autoAdvToggle(const UIElement& e)     { T(e)->toggleAutoAdvert(); }
static const char* advIntervalText(const UIElement& e) { return T(e)->advertIntervalLabel(); }
static void advIntervalNext(const UIElement& e)   { T(e)->cycleAdvertInterval(); }
static bool advLocGet(const UIElement& e)         { return T(e)->getAdvertLoc(); }
static void advLocToggle(const UIElement& e)      { T(e)->toggleAdvertLoc(); }
static void helpCb(const UIElement& e)      { T(e)->showHelp(); }
static void shareQRCb(const UIElement& e)   { T(e)->showQR(); }   // Home name -> self-advert QR
static bool offGridGet(const UIElement& e)    { return T(e)->getOffGrid(); }
static void offGridToggle(const UIElement& e) { T(e)->toggleOffGrid(); }
static const char* const freqOpts[] = { "433", "869", "918" };
// Read-only: the off-grid band is locked to the node's operating band (the RF front-end is
// tuned for one band and can't be probed), so we show it rather than let it be mis-set.
static const char* freqBandText(const UIElement& e) {
  int i = T(e)->getFreqPreset();
  return (i >= 0 && i < (int)(sizeof(freqOpts) / sizeof(freqOpts[0]))) ? freqOpts[i] : "?";
}

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
static const char* rxRowTime(const UIElement& e) {    // right-aligned age ("5m")
  RxLogScreen::RxRef* r = (RxLogScreen::RxRef*)e.ctx;
  return r->scr->timeAt(r->idx);
}
static void rxRowNoop(const UIElement&) {}            // rows scroll but don't drill down

// Fixed 2-char type codes so the log renders as aligned columns (Unifont is monospace).
static const char* rxTypeLabel(uint8_t ptype) {
  switch (ptype) {
    case PAYLOAD_TYPE_ADVERT:    return "AD";
    case PAYLOAD_TYPE_TXT_MSG:   return "MS";
    case PAYLOAD_TYPE_GRP_TXT:   return "CH";
    case PAYLOAD_TYPE_ACK:       return "AK";
    case PAYLOAD_TYPE_PATH:      return "PA";
    case PAYLOAD_TYPE_TRACE:     return "TR";
    case PAYLOAD_TYPE_REQ:       return "RQ";
    case PAYLOAD_TYPE_RESPONSE:  return "RS";
    case PAYLOAD_TYPE_ANON_REQ:  return "AR";
    case PAYLOAD_TYPE_GRP_DATA:  return "GD";
    case PAYLOAD_TYPE_CONTROL:   return "CT";
    case PAYLOAD_TYPE_MULTIPART: return "MP";
    case PAYLOAD_TYPE_RAW_CUSTOM:return "RW";
    default:                     return "??";
  }
}

// Compact relative age of an epoch timestamp ("now"/"5m"/"3h"/"2d"), into `out`.
// Empty when the timestamp or the device clock is unusable.
static void relTime(char* out, size_t cap, uint32_t ts) {
  out[0] = 0;
  uint32_t now = the_mesh.getRTCClock()->getCurrentTime();
  if (ts == 0 || now < ts) return;             // unknown, or clock not yet synced
  uint32_t s = now - ts;
  if      (s < 60)    strncpy(out, "0m", cap);
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
  _items[0] = makeAction(prefs->node_name, task, shareQRCb);       // node name -> QR of your key
  _items[1] = makeAction("Send Advert", task, advertCb);
  _items[2] = makeLabel("Messages", unreadText, task);
  _items[3] = makeLabel("Uptime",   uptimeText,  task);
  _items[4] = makeLabel("Queue",    queueText,   task);
  _items[5] = makeAction("Help", task, helpCb);   // gesture + icon legend
  _elems = _items; _count = 6;     // BT/conn now shown as a status-bar icon
}

// ============================================================ MeshScreen
// Mesh-protocol traffic only; RF config + link readings moved to the Radio page.
MeshScreen::MeshScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Mesh") {
  _items[0]  = makeAction("Send Advert", task, advertCb);
  _items[1]  = makeToggle("Auto advert", task, autoAdvGet, autoAdvToggle);
  _items[2]  = makeCycleText("Every", task, advIntervalText, advIntervalNext);
  _items[3]  = makeToggle("Share loc", task, advLocGet, advLocToggle);
  _items[4]  = makeLabel("Contacts", contactsText, task);
  _items[5]  = makeLabel("Sent F/D", sentText,     task);
  _items[6]  = makeLabel("Recv F/D", recvText,     task);
  _items[7]  = makeLabel("Airtime",  airtimeText,  task);
  _items[8]  = makeLabel("Queue",    queueText,    task);
  _elems = _items; _count = 9;
}

// ============================================================ RadioScreen
// RF config + link readings. The Off-grid client-repeat toggle is the only editable
// control; the off-grid band is band-locked (read-only) to the node's operating band,
// and the LoRa params below are read-only mirrors of the app-side config.
RadioScreen::RadioScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Radio") {
  _items[0] = makeToggle("Off-grid", task, offGridGet, offGridToggle);   // client-repeat / relay
  _items[1] = makeLabel("Off grid freq", freqBandText, task);   // band-locked to the operating band
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
  // No software GPS toggle -- the M1's physical switch controls the receiver; this page is
  // read-only fix info (the 📍 status-bar icon still reflects on/off state).
  _items[0] = makeLabel("Fix",   gpsFixText,     task);
  _items[1] = makeLabel("Last",  gpsLastFixText, task);
  _items[2] = makeLabel("Sats",  gpsSatsText,    task);
  _items[3] = makeLabel("Pos",   gpsLatLonText,  task);
  _items[4] = makeLabel("Alt",   gpsAltText,     task);
  _elems = _items; _count = 5;
}

// ============================================================ NavScreen
// Favourite contacts (starred in the app: flags bit 0) that advertised a location
// are the waypoints (MeshCore has no separate waypoint protocol). gps_lat/lon are
// millionths of a degree.

static bool isWaypoint(const ContactInfo& ci) {
  return (ci.flags & 0x01) && !(ci.gps_lat == 0 && ci.gps_lon == 0);
}
static bool nthWaypoint(int n, ContactInfo& out) {
  int num = the_mesh.getNumContacts();
  for (int i = 0; i < num; i++) {
    if (!the_mesh.getContactByIdx(i, out)) continue;
    if (!isWaypoint(out)) continue;
    if (n-- == 0) return true;
  }
  return false;
}
static int numWaypoints() {
  ContactInfo ci;
  int num = the_mesh.getNumContacts(), n = 0;
  for (int i = 0; i < num; i++) {
    if (the_mesh.getContactByIdx(i, ci) && isWaypoint(ci)) n++;
  }
  return n;
}

// Haversine distance + initial true bearing -- the same great-circle formulas
// Meshtastic's GeoCoord uses (R = 6371 km).
static void greatCircle(double lat1, double lon1, double lat2, double lon2,
                        double& dist_m, double& brg_deg) {
  const double D2R = 0.017453292519943295;
  double p1 = lat1 * D2R, p2 = lat2 * D2R, dl = (lon2 - lon1) * D2R;
  double sa = sin((p2 - p1) / 2), so = sin(dl / 2);
  double h = sa * sa + cos(p1) * cos(p2) * so * so;
  dist_m = 2.0 * 6371000.0 * asin(sqrt(h));
  double y = sin(dl) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  brg_deg = fmod(atan2(y, x) / D2R + 360.0, 360.0);
}

static NavScreen* NV(const UIElement& e) { return (NavScreen*)e.ctx; }
static const char* navTargetText(const UIElement& e) { return NV(e)->targetName(); }
static void navNextCb(const UIElement& e) { NV(e)->nextTarget(); }

static const char* navDistText(const UIElement& e) {
  static char b[14];
  double d, brg;
  if (!NV(e)->targetVector(d, brg)) { strcpy(b, "--"); return b; }
  if (d < 1000)        sprintf(b, "%dm", (int)(d + 0.5));
  else if (d < 10000)  sprintf(b, "%.2fkm", d / 1000.0);
  else if (d < 100000) sprintf(b, "%.1fkm", d / 1000.0);
  else                 sprintf(b, "%.0fkm", d / 1000.0);
  return b;
}
static const char* navBrgText(const UIElement& e) {
  static char b[12];
  double d, brg;
  if (!NV(e)->targetVector(d, brg)) { strcpy(b, "--"); return b; }
  static const char* const pts[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  sprintf(b, "%d\xC2\xB0 %s", (int)(brg + 0.5) % 360, pts[((int)(brg / 45.0 + 0.5)) % 8]);
  return b;
}

// Below this ground speed the GPS course is unreliable, so we stay north-up and
// hide the ETA (it would blow up toward infinity as speed -> 0).
static const double NAV_MOVE_KMH = 1.5;

static const char* navSpeedText(const UIElement& e) {
  static char b[12];
  double kmh, course;
  if (!NV(e)->motion(kmh, course)) { strcpy(b, "--"); return b; }
  sprintf(b, "%.1fkm/h", kmh);
  return b;
}
static const char* navEtaText(const UIElement& e) {
  static char b[10];
  double kmh, course, dist, brg;
  if (!NV(e)->motion(kmh, course) || kmh < NAV_MOVE_KMH || !NV(e)->targetVector(dist, brg)) {
    strcpy(b, "--"); return b;
  }
  double secs = dist / (kmh / 3.6);                 // km/h -> m/s
  if      (secs < 60)    sprintf(b, "%ds", (int)(secs + 0.5));
  else if (secs < 3600)  sprintf(b, "%dm", (int)(secs / 60 + 0.5));
  else if (secs < 86400) sprintf(b, "%.1fh", secs / 3600.0);
  else                   strcpy(b, ">1d");
  return b;
}

const char* NavScreen::targetName() {
  static char b[26];
  ContactInfo ci;
  int n = numWaypoints();
  if (n == 0) { strcpy(b, "--"); return b; }
  if (_target >= n) _target %= n;   // contact list shrank since last render
  nthWaypoint(_target, ci);
  strncpy(b, ci.name, sizeof(b) - 1);
  b[sizeof(b) - 1] = 0;
  return b;
}

void NavScreen::nextTarget() {
  int n = numWaypoints();
  if (n > 0) _target = (_target + 1) % n;
}

bool NavScreen::targetVector(double& dist_m, double& brg_deg) {
  if (!gpsEverFixed()) return false;      // navigate from the last good fix
  ContactInfo ci;
  int n = numWaypoints();
  if (n == 0) return false;
  if (_target >= n) _target %= n;
  if (!nthWaypoint(_target, ci)) return false;
  greatCircle(sensors.node_lat, sensors.node_lon,
              ci.gps_lat / 1.0e6, ci.gps_lon / 1.0e6, dist_m, brg_deg);
  return true;
}

// Live ground speed (km/h) + course (deg) from the GPS. Returns false with no live
// valid fix or no speed field; course_deg is negative when the fix carries no course.
bool NavScreen::motion(double& speed_kmh, double& course_deg) {
  LocationProvider* l = sensors.getLocationProvider();
  if (!l || !l->isValid()) return false;
  long sp = l->getSpeed();                 // thousandths of a knot (negative if absent)
  if (sp < 0) return false;
  speed_kmh = sp * 0.001852;               // (knots/1000) * 1.852 km/h per knot
  long co = l->getCourse();                // thousandths of a degree
  course_deg = (co < 0) ? -1.0 : (co / 1000.0);
  return true;
}

NavScreen::NavScreen(UITask* task, NodePrefs* prefs) : ElementScreen(task, prefs, "Nav") {
  _items[0] = makeCycleText("Target", this, navTargetText, navNextCb);
  _items[1] = makeLabel("Dist",  navDistText,  this);
  _items[2] = makeLabel("Brg",   navBrgText,   this);
  _items[3] = makeLabel("Speed", navSpeedText, this);
  _items[4] = makeLabel("ETA",   navEtaText,   this);
  _elems = _items; _count = 5;
}

int NavScreen::render(DisplayDriver& d) {
  int ret = ElementScreen::render(d);   // status bar, rows, page dots

  // Compass rose in the band under the rows, all font glyphs at the page's regular
  // 16px size: a ring of cardinal letters around a centre arrow that points at the
  // target. When moving (GPS course available above NAV_MOVE_KMH) the rose is
  // heading-up -- "up" is the way you're travelling, the arrow shows which way to
  // turn, and the N/E/S/W ring rotates to keep pointing at true north. Stationary
  // (or no course) it falls back to north-up. "?" when there's no fix or waypoint.
  static const char* const arrows[8] = {
    "\xE2\x86\x91", "\xE2\x86\x97", "\xE2\x86\x92", "\xE2\x86\x98",   // ↑ ↗ → ↘
    "\xE2\x86\x93", "\xE2\x86\x99", "\xE2\x86\x90", "\xE2\x86\x96"    // ↓ ↙ ← ↖
  };
  // ring offsets per 45° sector (0=up), elliptical to match text metrics (24px wide, 16 tall)
  static const int RX[8] = { 0, 17, 24, 17, 0, -17, -24, -17 };
  static const int RY[8] = { -16, -11, 0, 11, 16, 11, 0, -11 };

  double dist, brg;
  bool have = targetVector(dist, brg);

  double kmh, course;
  bool heading_up = motion(kmh, course) && kmh >= NAV_MOVE_KMH && course >= 0;

  double brg_shown = (heading_up && have) ? fmod(brg - course + 360.0, 360.0) : brg;
  const char* mark = have ? arrows[((int)(brg_shown / 45.0 + 0.5)) % 8] : "?";

  // where true north sits on the ring: top when north-up, rotated by -course when heading-up
  double north_at = heading_up ? fmod(360.0 - course, 360.0) : 0.0;
  int nsec = ((int)(north_at / 45.0 + 0.5)) % 8;

  const int cx = d.width() / 2;
  const int rows_end = contentTop() + _count * (UIELEM_ROW_H + 2 * UIELEM_PAD);
  const int cy = (rows_end + contentBottom()) / 2;   // center of the free band
  const int by = cy - 8;                             // text baseline of the middle ring row

  d.setColor(DisplayDriver::LIGHT);
  d.setTextSize(1);
  d.drawTextCentered(cx, by, mark);                  // centre arrow
  static const char* const card[4] = { "N", "E", "S", "W" };
  for (int k = 0; k < 4; k++) {
    int s = (nsec + 2 * k) % 8;                       // N, then +90° each for E/S/W
    d.drawTextCentered(cx + RX[s], by + RY[s], card[k]);
  }
  return ret;
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
  _items[0] = makeToggle("Buzzer", task, buzzerGet, buzzerToggle);          // master enable
  _items[1] = makeCycle("Beep On", task, beepOnOpts, 2, beepModeGet, beepModeNext);      // messages XOR packets
  _items[2] = makeCycle("Msg Sound", task, buzzModeOpts, 3, buzzModeGet, buzzModeNext);  // style (Beep On = Msgs)
  _elems = _items; _count = 3;
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
    case L_MSGS: {
      // depth chevron (option C): "‹ #general" -- the ‹ marks that there's a parent
      // (triangle-hold pops back up to the conversation list).
      const char* pfx = (_sel_is_channel && _sel_conv[0] != '#') ? "#" : "";
      snprintf(_crumb, sizeof(_crumb), "\xE2\x80\xB9 %s%s", pfx, _sel_conv);  // ‹
      _title = _crumb;
      rebuildMessages();
      break;
    }
    default:
      _title = "Msgs";
      rebuildConversations();
      break;
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

// The body fills the content rows; the last content row (just above the bottom icon rule)
// is the compact date/route meta line.
static int mdMetaY()        { return uichrome::contentBottom() - UIELEM_ROW_H; }
static int mdBodyTop()      { return uichrome::contentTop(); }
static int mdLinesPerPage() { return (mdMetaY() - mdBodyTop()) / UIELEM_ROW_H; }

bool MessageDetailScreen::scrollDown() {
  int per = mdLinesPerPage();
  if (_scroll_line + per < _total_lines) { _scroll_line += per; return true; }
  return false;   // at the end; caller advances to the next message
}

int MessageDetailScreen::render(DisplayDriver& d) {
  d.setTextSize(1);

  // Fits the overall UI chrome: the title is just the sender's node name (the ‹ marks the
  // depth), the page-icon bar sits in the bottom rule (like every screen), the word-
  // wrapped body fills the content rows, and a compact date/route single-line rule sits
  // just above the icon bar.
  //
  // For a channel message the sender's node name is embedded in the body as a "Name: "
  // prefix (_msg.sender holds the *channel* name); use just that node name as the title
  // and strip the prefix off the body (keeps the body clean so ASCII art renders). Direct
  // messages already have the node name in _msg.sender and a clean body.
  const char* body = _msg.body;
  char hdr[40];
  if (_msg.is_channel) {
    const char* sep = strstr(_msg.body, ": ");
    if (sep) {
      int n = (int)(sep - _msg.body);
      if (n > (int)sizeof(hdr) - 1) n = sizeof(hdr) - 1;
      memcpy(hdr, _msg.body, n); hdr[n] = 0;           // node name (the "who" before ": ")
      body = sep + 2;                                   // body after the "Name: " prefix
    } else {
      snprintf(hdr, sizeof(hdr), "%s", _msg.sender);    // fall back to the channel name
    }
  } else {
    snprintf(hdr, sizeof(hdr), "%s", _msg.sender);      // DM: the sender node
  }
  char title[48];
  snprintf(title, sizeof(title), "\xE2\x80\xB9 %s", hdr);   // ‹ node name

  // compact meta line: "mm/dd HH:MM │ <route>" (date honors the Time page format + offset);
  // fields separated by a line-drawing │ (U+2502)
  char date[24], sub[48];
  _task->formatDateTime(_msg.timestamp, date, sizeof(date));
  if (_msg.is_direct) {
    snprintf(sub, sizeof(sub), "%s \xE2\x94\x82 Direct", date);      // │
  } else {
    snprintf(sub, sizeof(sub), "%s \xE2\x94\x82 %uh", date, (unsigned)_msg.hops);
  }

  // chrome: status cluster + top-rule title, page icons in the bottom rule
  int cluster_left = uichrome::statusCluster(d, _task, _task->nodePrefs());
  uichrome::rule(d, uichrome::frameTop(), title, cluster_left);
  uichrome::bottomBar(d, PAGE_MESSAGES, PAGE_COUNT);

  // date/route as a single-line ─┤ ... ├─ rule, just above the icon bar
  uichrome::ruleThin(d, mdMetaY(), sub, d.width());

  // body from the top of the content, word-wrapped; reserve the last column for the thumb.
  const int cw = d.width() - GRID_COL;
  char lines[WRAP_MAX_LINES][WRAP_LINE_CAP];
  char body_glyphs[MAX_FRAME_SIZE];
  d.translateUTF8ToBlocks(body_glyphs, body, sizeof(body_glyphs));   // cleaned body (no "Name: " prefix)
  _total_lines = wrapText(d, cw, body_glyphs, lines, WRAP_MAX_LINES);
  if (_scroll_line >= _total_lines) _scroll_line = 0;

  const int top = mdBodyTop();
  const int per = mdLinesPerPage();
  d.setColor(DisplayDriver::LIGHT);
  for (int i = 0; i < per && (_scroll_line + i) < _total_lines; i++) {
    d.setCursor(0, top + i * UIELEM_ROW_H);            // first column
    d.print(lines[_scroll_line + i]);                  // already glyph-translated
  }

  // trackless glyph thumb (body position) in the last column when the body overflows
  if (_total_lines > per) {
    static const char* const THUMB = "\xE2\x96\x88";   // █
    int sx = d.width() - GRID_COL;
    int thumb_cells = per * per / _total_lines;  if (thumb_cells < 1) thumb_cells = 1;
    int max_line = _total_lines - per;
    int thumb_start = max_line > 0 ? (_scroll_line * (per - thumb_cells) + max_line / 2) / max_line : 0;
    d.setColor(DisplayDriver::LIGHT);
    for (int i = thumb_start; i < thumb_start + thumb_cells && i < per; i++) {
      d.setCursor(sx, top + i * UIELEM_ROW_H);
      d.print(THUMB);
    }
  }
  return 10000;   // e-ink: repaint only on interaction
}

// ============================================================ HelpScreen
int HelpScreen::render(DisplayDriver& d) {
  d.setTextSize(1);
  d.setColor(DisplayDriver::GREEN);
  d.setCursor(0, 0);                                          // row 0
  d.print("Help");

  // ---- status-bar icon legend (rows 1-5): icon in the first column, label at col 2 ----
  const char* icons[]  = { "\xF0\x9F\x93\xB1", "\xF0\x9F\x93\xB5", "\xF0\x9F\x93\x8D", "\xF0\x9F\x94\x87", "\xE2\x9A\xA1" }; // 📱📵📍🔇⚡
  const char* labels[] = { "App linked", "BLE off", "GPS on", "Muted", "Charging" };
  d.setColor(DisplayDriver::LIGHT);
  for (int i = 0; i < 5; i++) {
    int y = (1 + i) * UIELEM_ROW_H;
    d.setCursor(0, y);            d.print(icons[i]);
    d.setCursor(2 * GRID_COL, y); d.print(labels[i]);
  }

  // ---- button guide (rows 7-10): ● moves/selects, ▲ pages/backs out ----
  static const char* const gicon[] = { "\xE2\x97\x8F", "\xE2\x97\x8F", "\xE2\x96\xB2", "\xE2\x96\xB2" }; // ● ● ▲ ▲
  static const char* const gtext[] = { "click: next item", "hold: select/open",
                                       "click: next page", "hold: back / prev" };
  for (int i = 0; i < 4; i++) {
    int y = (7 + i) * UIELEM_ROW_H;
    d.setCursor(0, y);            d.print(gicon[i]);
    d.setCursor(2 * GRID_COL, y); d.print(gtext[i]);
  }

  return 3600000;   // static: only repaints on interaction (which dismisses it)
}

// ============================================================ QRScreen
// Full-screen QR of our "meshcore://contact/add?..." URL: another MeshCore user scans
// it (phone app) to add us as a contact. Drawn black-on-white; the frame is already
// cleared white by startFrame, and the centering margin doubles as the quiet zone.
int QRScreen::render(DisplayDriver& d) {
  d.setTextSize(1);
  char uri[224];
  int n = the_mesh.selfShareURI(uri, sizeof(uri));
  if (n <= 0) {
    d.setColor(DisplayDriver::LIGHT);
    d.drawTextCentered(d.width() / 2, d.height() / 2 - 8, "No identity");
    return 3600000;
  }
  // smallest version whose byte-mode / ECC-LOW capacity holds the URL
  static const int cap_L[] = { 0, 17, 32, 53, 78, 106, 134, 154, 192, 232, 271, 321 };
  int len = (int)strlen(uri), ver = 11;
  for (int v = 1; v <= 11; v++) if (cap_L[v] >= len) { ver = v; break; }

  static uint8_t qrbuf[512];   // >= qrcode_getBufferSize(11) (=466), the largest version we emit
  QRCode qr;
  qrcode_initText(&qr, qrbuf, ver, ECC_LOW, uri);

  const int mods = qr.size;                        // modules per side
  const int MARGIN = 16;                           // white quiet-zone margin (px)
  int px = (d.height() - 2 * MARGIN) / mods;
  if (px < 1) px = 1;
  int dim = px * mods;
  int ox = (d.width() - dim) / 2, oy = (d.height() - dim) / 2;

  d.setColor(DisplayDriver::LIGHT);                // black modules
  for (int y = 0; y < mods; y++)
    for (int x = 0; x < mods; x++)
      if (qrcode_getModule(&qr, x, y))
        d.fillRect(ox + x * px, oy + y * px, px, px);

  return 3600000;   // static until a press dismisses it
}

// ============================================================ ShutdownScreen
// Power info page (manual hibernate was removed -- pointless on this board, since
// waking is a cold boot and it wouldn't reach a genuinely low-power state).
ShutdownScreen::ShutdownScreen(UITask* task, NodePrefs* prefs)
    : ElementScreen(task, prefs, "Pwr") {
  _elems = _items; _count = 0;
}

void ShutdownScreen::rebuild() {
  _items[0] = makeLabel("Battery",  battPctText,   _task);
  _items[1] = makeLabel("Voltage",  battVoltsText, _task);
  _items[2] = makeLabel("Charging", chargingText,  _task);
  _elems = _items; _count = 3;
}

// ============================================================ AdvertsScreen
RxLogScreen::RxLogScreen(UITask* task, NodePrefs* prefs)
    : ElementScreen(task, prefs, "RX Log") {
  _elems = _items; _count = 0;
}

// Rebuilt every render from MyMesh's rx_log ring (newest first). Each row is a set
// of fixed-width columns (Unifont is monospace, so printf padding lines them up):
// "TY -rss/+sn LH  Hh" with the relative age right-aligned (re-rendered each
// refresh, so the ages stay current). LH = the last hop: the path hash of the node
// whose transmission we actually heard.
//
// Only FLOOD packets carry this: each relay appends its own hash to the end of the
// path, so the last hop_bytes of via[] is the node that just keyed up. Printed as one
// contiguous fingerprint -- "FE" on a 1-byte network, "EEEEFE" on a 3-byte one. A
// 0-hop flood is heard straight from the origin: its key/channel byte. DIRECT-routed
// packets do NOT record the transmitter (their path is a forward route consumed hop by
// hop, and the sender already popped itself), so the emitter is unknown -> "--". Also
// "--" when the type carries no identity at all (ACK etc). The hops column is the path
// length.
void RxLogScreen::rebuild() {
  static RxLogEntry rx[RX_LOG_SIZE];                   // transient scratch (single-threaded)
  int got = the_mesh.getRxLog(rx, RX_LOG_SIZE);
  const int via_w = 6;   // always pad the last-hop to 3 bytes (6 hex), right-justified, so the
                         // hop-count column stays fixed regardless of what's in view
  int n = 0;
  for (int i = 0; i < got; i++) {
    const RxLogEntry& e = rx[i];
    int hb = e.hop_bytes ? e.hop_bytes : 1;                   // path hash size: bytes per hop
    int nb = (e.via_len < hb) ? e.via_len : hb;               // the last hop = its whole hash
    char via[12];
    if (e.flood && nb > 0) {
      char* p = via;                                          // flood relay: last hash = node we heard
      for (int k = e.via_len - nb; k < e.via_len; k++) p += sprintf(p, "%02X", e.via[k]);
    } else if (e.flood && e.key0 >= 0) {
      sprintf(via, "%02X", (unsigned)e.key0);                 // 0-hop flood: heard the origin directly
    } else {
      strcpy(via, "--");                                      // direct route (emitter not in packet), or no identity
    }
    snprintf(_rows[n].line, sizeof(_rows[n].line), "%s %4d/%+3d %*s %u",
             rxTypeLabel(e.ptype), (int)e.rssi, (int)e.snr, via_w, via, (unsigned)e.hops);
    relTime(_rows[n].time, sizeof(_rows[n].time), e.timestamp);
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
