#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif

// While the display is awake (on external power, or during the on-window on battery), poll
// at least this often so content changes (new packets, stats, messages) show promptly rather
// than at the screen's slow self-refresh cadence (10s on USB / 60s on battery). endFrame()'s
// CRC skips the physical e-ink refresh when nothing actually changed, so between real updates
// this is just cheap buffer redraws. ~500ms ~= one partial refresh, so polling faster is moot.
#ifndef FAST_REFRESH_MS
  #define FAST_REFRESH_MS     500
#endif
// Screens declaring a cadence >= this are treated as static (Help/QR overlays return ~1h):
// left alone so we don't pointlessly re-render / re-encode them every FAST_REFRESH_MS.
#define STATIC_REFRESH_MS     600000

// How long the new-message popup box stays up before it auto-dismisses (any press dismisses it).
#ifndef MSG_POPUP_MS
  #define MSG_POPUP_MS        12000
#endif

// While the display is off, re-draw once this often to keep content current and
// (in Full mode) re-drive every pixel to fight UV fade/ghosting. Between ticks the
// e-ink holds its last image with no driving -- far less UV exposure than the live
// per-second repaint used while awake.
#ifndef EINK_IDLE_REFRESH_MILLIS
  #define EINK_IDLE_REFRESH_MILLIS  60000   // 1 minute
#endif

// While the circle button stays held on a multi-option element, advance to the
// next option this often. Must comfortably exceed one partial e-ink refresh
// (~450ms) so each step is visible on the panel before the next one fires.
#ifndef CYCLE_HOLD_REPEAT_MS
  #define CYCLE_HOLD_REPEAT_MS  750
#endif

// Red "Power" LED heartbeat while on battery, matched to Meshtastic's
// StatusLEDModule: a 1ms blip once per second (~0.1% duty -- a faint alive-tick
// that costs essentially no battery).
#ifndef RED_LED_PERIOD_MILLIS
  #define RED_LED_PERIOD_MILLIS  1000
#endif
#ifndef RED_LED_ON_MILLIS
  #define RED_LED_ON_MILLIS  1
#endif
// Battery voltage treated as "charge complete" for the solid-on red LED while
// externally powered (Meshtastic: charged = 100%). Slightly under 4.20V so the
// solid state is actually reachable under load.
#ifndef RED_LED_FULL_MILLIVOLTS
  #define RED_LED_FULL_MILLIVOLTS  4150
#endif
// Blue status LED flash per received packet (TX lights it for the whole send).
#ifndef BLUE_LED_RX_FLASH_MS
  #define BLUE_LED_RX_FLASH_MS  50
#endif
// Minimum silent gap between per-packet "Pkt Tones" chirps, so a burst reads as
// distinct ticks rather than a continuous tone.
#ifndef PKT_TONE_GAP_MS
  #define PKT_TONE_GAP_MS  45
#endif

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

static int presetIndexForFreq(float mhz);   // defined below, near the off-grid helpers

// Empty ISR: its only job is to make a button edge raise a GPIOTE interrupt, which
// pops the CPU out of the event-wait sleep in the main loop (board.sleep). Without
// it a press isn't a wake source and sits unnoticed until the next BLE/LoRa/timer
// interrupt happens to wake the poller. No work in the ISR = no radio impact.
static void btnWakeISR() {}

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
  user_btn.begin();
  attachInterrupt(digitalPinToInterrupt(PIN_USER_BTN), btnWakeISR, CHANGE);
#endif
  back_btn.begin();   // second button (PIN_BUTTON2)
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON2), btnWakeISR, CHANGE);

  _node_prefs = node_prefs;
  // Seed the remembered off-grid band from the live freq (recovers the choice
  // across reboot while off-grid is on; defaults to the first preset otherwise).
  _offgrid_preset = presetIndexForFreq(_node_prefs->freq);

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
  _qr = new QRScreen();
  pages[PAGE_HOME]      = new HomeScreen(this, node_prefs);
  pages[PAGE_MESH]      = new MeshScreen(this, node_prefs);
  pages[PAGE_RXLOG]     = new RxLogScreen(this, node_prefs);
  pages[PAGE_RADIO]     = new RadioScreen(this, node_prefs);
  pages[PAGE_GPS]       = new GPSScreen(this, node_prefs);
  pages[PAGE_NAV]       = new NavScreen(this, node_prefs);
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

void UITask::showQR() {
  _help_return = curr;            // reuse the overlay return path (any press dismisses)
  _help_return_page = curr_page;
  setCurrScreen(_qr);
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

void UITask::revertFromPopup() {
  _revert_at = 0;
  curr_page = _revert_page;
  setCurrScreen(_revert_screen ? _revert_screen : (UIScreen*)pages[PAGE_HOME]);
  _auto_off = millis() + AUTO_OFF_MILLIS;   // keep the screen lit so the return is visible
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::doAdvert() {
  notify(UIEventType::ack);
  if (the_mesh.advert()) {
    showAlert("Advert sent!");
  } else {
    showAlert("Advert failed..");
  }
}

// ---- periodic self-advert (Mesh page) -------------------------------------
// Preset intervals offered by the "Every" cycle; keep ascending.
static const uint16_t ADVERT_MINS[] = { 5, 15, 30, 60, 120, 360, 720 };
static const int ADVERT_MINS_N = (int)(sizeof(ADVERT_MINS) / sizeof(ADVERT_MINS[0]));
#define ADVERT_MINS_DEFAULT 60

bool UITask::getAutoAdvert() const {
  return _node_prefs && _node_prefs->advert_auto;
}

void UITask::toggleAutoAdvert() {
  if (!_node_prefs) return;
  _node_prefs->advert_auto = _node_prefs->advert_auto ? 0 : 1;
  if (_node_prefs->advert_auto && _node_prefs->advert_interval == 0)
    _node_prefs->advert_interval = ADVERT_MINS_DEFAULT;   // arm with a sane default
  the_mesh.savePrefs();
  the_mesh.rescheduleAutoAdvert();
}

const char* UITask::advertIntervalLabel() const {
  static char buf[8];
  uint16_t m = (_node_prefs && _node_prefs->advert_interval) ? _node_prefs->advert_interval
                                                             : ADVERT_MINS_DEFAULT;
  if (m < 60)           snprintf(buf, sizeof(buf), "%um", (unsigned)m);
  else if (m % 60 == 0) snprintf(buf, sizeof(buf), "%uh", (unsigned)(m / 60));
  else                  snprintf(buf, sizeof(buf), "%uh%02um", (unsigned)(m / 60), (unsigned)(m % 60));
  return buf;
}

void UITask::cycleAdvertInterval() {
  if (!_node_prefs) return;
  uint16_t cur = _node_prefs->advert_interval ? _node_prefs->advert_interval : ADVERT_MINS_DEFAULT;
  int next = 0;                                       // wrap to smallest once past the top
  for (int i = 0; i < ADVERT_MINS_N; i++) {
    if (ADVERT_MINS[i] > cur) { next = i; break; }    // first preset strictly larger than current
  }
  _node_prefs->advert_interval = ADVERT_MINS[next];
  the_mesh.savePrefs();
  the_mesh.rescheduleAutoAdvert();
}

bool UITask::getAdvertLoc() const {
  return _node_prefs && _node_prefs->advert_loc_policy == ADVERT_LOC_SHARE;
}

void UITask::toggleAdvertLoc() {
  if (!_node_prefs) return;
  _node_prefs->advert_loc_policy = (_node_prefs->advert_loc_policy == ADVERT_LOC_SHARE)
      ? ADVERT_LOC_NONE : ADVERT_LOC_SHARE;
  the_mesh.savePrefs();
}

void UITask::forceFullRefresh() {
  if (_display != NULL) {
    _display->fullRefresh();   // full (ghost-clearing) refresh on the next paint
    _next_refresh = 0;         // repaint now so endFrame runs the full update
    if (_display->isOn()) _auto_off = millis() + AUTO_OFF_MILLIS;
  }
}

// Frequency presets (MHz). These match the client-repeat allowed bands
// (repeat_freq_ranges in MyMesh), so off-grid repeat stays valid on each.
static const float FREQ_PRESETS[] = { 433.0f, 869.495f, 918.0f };
static const int   FREQ_PRESET_COUNT = sizeof(FREQ_PRESETS) / sizeof(FREQ_PRESETS[0]);

// index of the preset matching `mhz` (within 0.05 MHz), else 0
static int presetIndexForFreq(float mhz) {
  for (int i = 0; i < FREQ_PRESET_COUNT; i++) {
    float d = mhz - FREQ_PRESETS[i];
    if (d > -0.05f && d < 0.05f) return i;
  }
  return 0;
}

// "Off-grid" mode = client-repeat: this companion node also forwards/relays mesh
// traffic (MyMesh::allowPacketForward), so every node helps carry the mesh.
// Enabling it parks the radio on the selected off-grid preset band (remembering
// the prior freq); disabling it restores that prior freq.
bool UITask::getOffGrid() const {
  return _node_prefs && _node_prefs->client_repeat;
}

void UITask::toggleOffGrid() {
  if (!_node_prefs) return;
  bool turning_on = !_node_prefs->client_repeat;
  _node_prefs->client_repeat = turning_on ? 1 : 0;
  if (turning_on) {
    // remember the current (normal) freq, then move to the chosen off-grid band
    _saved_freq = _node_prefs->freq;
    the_mesh.setRadioFreq(FREQ_PRESETS[_offgrid_preset]);   // retune + persist (also persists client_repeat)
  } else if (_saved_freq > 0) {
    the_mesh.setRadioFreq(_saved_freq);   // restore the pre-off-grid freq + persist
  } else {
    the_mesh.savePrefs();                 // nothing to restore; just persist the flag
  }
  notify(UIEventType::ack);
  showAlert(_node_prefs->client_repeat ? "Off-grid: ON" : "Off-grid: OFF");
  _next_refresh = 0;
}

// The off-grid band is a remembered setting, independent of the live freq, so it
// survives toggling off-grid off (when the radio is on its normal frequency).
int UITask::getFreqPreset() const {
  return _offgrid_preset;
}

void UITask::cycleFreqPreset() {
  if (!_node_prefs) return;
  _offgrid_preset = (_offgrid_preset + 1) % FREQ_PRESET_COUNT;   // remembered regardless of mode
  // Only retune the live radio when off-grid is on; otherwise just remember the
  // choice so a normal node can't be knocked off its configured frequency.
  if (getOffGrid()) {
    the_mesh.setRadioFreq(FREQ_PRESETS[_offgrid_preset]);   // retune + persist
    notify(UIEventType::ack);
  }
  char msg[28];
  snprintf(msg, sizeof(msg), "Off-grid: %.3f MHz", FREQ_PRESETS[_offgrid_preset]);
  showAlert(msg);
  _next_refresh = 0;
}

#if defined(PIN_BUZZER)
// A short per-packet chirp; pitch encodes the payload type so you can hear what
// kind of traffic is passing. Frequencies are spread across a ~2 octave range,
// roughly "lower for chatter, higher for the interesting stuff".
#ifndef PKT_TONE_MS
  #define PKT_TONE_MS  25
#endif
static uint16_t pktToneFreq(uint8_t ptype) {
  switch (ptype) {
    case PAYLOAD_TYPE_ADVERT:    return 523;   // C5  -- background presence beacons
    case PAYLOAD_TYPE_GRP_TXT:   return 784;   // G5  -- channel chatter
    case PAYLOAD_TYPE_GRP_DATA:  return 740;   // F#5
    case PAYLOAD_TYPE_TXT_MSG:   return 1047;  // C6  -- direct messages
    case PAYLOAD_TYPE_ACK:       return 1319;  // E6  -- crisp high tick
    case PAYLOAD_TYPE_PATH:      return 587;   // D5
    case PAYLOAD_TYPE_REQ:       return 880;   // A5
    case PAYLOAD_TYPE_RESPONSE:  return 988;   // B5
    case PAYLOAD_TYPE_ANON_REQ:  return 831;   // G#5
    case PAYLOAD_TYPE_TRACE:     return 659;   // E5
    default:                     return 440;   // A4  -- control/raw/multipart/unknown
  }
}
#endif

// Per-received-packet hook (from MyMesh::logRx). Flashes the blue status LED and,
// in "beep on packets" mode, chirps a short tone whose pitch encodes the packet
// type -- an audible read on mesh traffic. Not a user notification. The beep
// modes are mutually exclusive (packets XOR messages), so no coordination with
// the message-notification sound is needed.
void UITask::onPacketRx(uint8_t ptype) {
#if defined(P_LORA_TX_LED)
  // Blue LED flash (TX lights it for the whole send via the board's
  // onBeforeTransmit/onAfterTransmit hooks). Turned off in userLedHandler().
  digitalWrite(P_LORA_TX_LED, HIGH);
  _blue_led_off_at = millis() + BLUE_LED_RX_FLASH_MS;
#endif

#if defined(PIN_BUZZER)
  if (_node_prefs && _node_prefs->pkt_tones) {   // pkt_tones == "beep on packets"
    // Rate-limit so a burst reads as distinct ticks, not one continuous drone.
    unsigned long now = millis();
    if (now - _last_chirp_at >= (unsigned long)(PKT_TONE_MS + PKT_TONE_GAP_MS)) {
      buzzer.chirp(pktToneFreq(ptype), PKT_TONE_MS);   // no-ops when the buzzer is quiet
      _last_chirp_at = now;
    }
  }
#endif
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
  // Incoming-message sounds are played from newMsg() instead, which knows the
  // hop count (needed for the morse mode). Here we only do the short ack click --
  // suppressed in "beep on packets" mode (the packet chirp covers the ack packet).
  if (t == UIEventType::ack && !(_node_prefs && _node_prefs->pkt_tones)) {
    buzzer.play("ack:d=32,o=8,b=120:c");
  }
#endif

#ifdef PIN_VIBRATION
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}

#if defined(PIN_BUZZER)
// Morse the hop count: the UNITS digit in morse, the TENS in the pitch -- one
// octave per ten hops from a 440Hz base (14 hops = "4" at 880Hz). The octave is
// the interval non-musicians pick out most reliably, and it's unambiguous on a
// square-wave piezo (these discs also get louder toward their ~2-4kHz resonance,
// so the higher digits ring clearer). A direct message (path_len 0xFF) has no
// hop number so it gets one distinct tone instead. dit/dah ~50/150ms keeps a
// digit under ~1s.
void UITask::playMorseHops(uint8_t path_len) {
  const uint16_t DIT = 50, DAH = 150, GAP = 50;
  ToneSeg seq[48];
  uint8_t n = 0;

  // Silent lead-in: right after this returns the UI does a blocking e-ink refresh,
  // during which buzzer.loop() can't run -- without this the first real tone is held
  // for the whole refresh and a leading dit sounds like a dah. The stall lands here.
  seq[n++] = { 0, 30 };

  if (path_len == 0xFF) {                 // direct: no hops to count
    seq[n++] = { 440, 250 };
    buzzer.playTones(seq, n);
    return;
  }

  static const char* const DIGIT[10] = {
    "-----", ".----", "..---", "...--", "....-",
    ".....", "-....", "--...", "---..", "----."
  };
  int hops = path_len & 63;                    // low 6 bits = hop count (top 2 = hash size)
  int tens = hops / 10;
  if (tens > 3) tens = 3;                      // cap at 3520Hz (piezo stays comfortable)
  const uint16_t F = 440 << tens;              // 440 / 880 / 1760 / 3520 Hz

  const char* p = DIGIT[hops % 10];
  for (int i = 0; p[i] && n < 46; i++) {
    if (i) seq[n++] = { 0, GAP };              // intra-character gap
    seq[n++] = { F, (uint16_t)(p[i] == '-' ? DAH : DIT) };
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

void UITask::cycleBeepMode() {
  if (!_node_prefs) return;
  _node_prefs->pkt_tones = _node_prefs->pkt_tones ? 0 : 1;   // messages <-> packets
  the_mesh.savePrefs();
#if defined(PIN_BUZZER)
  if (_node_prefs->pkt_tones) buzzer.chirp(pktToneFreq(PAYLOAD_TYPE_TXT_MSG), PKT_TONE_MS);  // packets preview
  else playMsgSound(3);                                                                      // messages preview
#endif
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
  // Message notification sound -- only in "beep on messages" mode. In "beep on
  // packets" mode this message already chirped as a packet in onPacketRx().
  if (!(_node_prefs && _node_prefs->pkt_tones)) playMsgSound(path_len);

  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();   // light the screen/backlight on a new message (even when app-connected)
  }

  // Show the newest message as a big DOS-style popup box (sender + wrapped preview) drawn
  // over whatever page you're on; it auto-dismisses after MSG_POPUP_MS, any press dismisses it.
  buildMsgPopup(from_name, text);
  _revert_at = millis() + MSG_POPUP_MS;
  _next_refresh = 0;   // draw it now

  if (_display != NULL && _display->isOn()) _auto_off = millis() + AUTO_OFF_MILLIS;
}

// Wrap a new message into _mpop: line 0 = sender, lines 1.. = word-wrapped body to the box width.
void UITask::buildMsgPopup(const char* from, const char* text) {
  const int MAXW = 168;   // inner text width (box ww - borders/pad)
  strncpy(_mpop[0], from ? from : "", sizeof(_mpop[0]) - 1);
  _mpop[0][sizeof(_mpop[0]) - 1] = 0;
  int n = 1;
  char line[sizeof(_mpop[0])]; line[0] = 0;
  const char* p = text ? text : "";
  while (*p && n < MSG_POPUP_MAXLINES) {
    while (*p == ' ') p++;
    const char* w = p;
    while (*p && *p != ' ') p++;
    int wl = (int)(p - w);
    if (wl == 0) break;
    char word[sizeof(_mpop[0])];
    if (wl > (int)sizeof(word) - 1) wl = sizeof(word) - 1;
    memcpy(word, w, wl); word[wl] = 0;
    char trial[sizeof(_mpop[0]) * 2];
    if (line[0] == 0) snprintf(trial, sizeof(trial), "%s", word);
    else              snprintf(trial, sizeof(trial), "%s %s", line, word);
    if (_display && _display->getTextWidth(trial) <= MAXW && strlen(trial) < sizeof(line)) {
      strcpy(line, trial);
    } else {
      if (line[0]) { strncpy(_mpop[n], line, sizeof(_mpop[n]) - 1); _mpop[n][sizeof(_mpop[n]) - 1] = 0; n++; }
      strncpy(line, word, sizeof(line) - 1); line[sizeof(line) - 1] = 0;
    }
  }
  if (line[0] && n < MSG_POPUP_MAXLINES) {
    strncpy(_mpop[n], line, sizeof(_mpop[n]) - 1); _mpop[n][sizeof(_mpop[n]) - 1] = 0; n++;
  }
  _mpop_n = n;
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

#if defined(P_LORA_TX_LED)
  // End of the blue LED's per-packet RX flash (set in notify(packetRx)).
  if (_blue_led_off_at != 0 && millis() >= _blue_led_off_at) {
    digitalWrite(P_LORA_TX_LED, LOW);
    _blue_led_off_at = 0;
  }
#endif

#if defined(LED_POWER)
  // Red "Power" LED, matched to Meshtastic's StatusLEDModule on this board:
  //   charging (external power, battery not full): the charger hardware blinks
  //     it -- release the pin (INPUT) and stay out of the way;
  //   charged (external power, battery full): solid ON;
  //   on battery: 1ms blip once per second (the "LED heartbeat").
  // Meshtastic's <=5%-battery critical fast-blink is unreachable here: the
  // AUTO_SHUTDOWN_MILLIVOLTS cutoff powers the device off well above that.
  unsigned long now = millis();
  if (board.isExternalPowered()) {
    if (_red_batt_at == 0 || now >= _red_batt_at) {   // "full yet?" ADC poll, every 8s
      _red_batt_at = now + 8000;
      _red_full = board.getBattMilliVolts() >= RED_LED_FULL_MILLIVOLTS;
    }
    if (_red_full) {
      if (!_red_led_out || !_red_led_lit) {           // charge complete: solid ON
        pinMode(LED_POWER, OUTPUT);
        digitalWrite(LED_POWER, LED_STATE_ON);
        _red_led_out = true;
        _red_led_lit = true;
      }
    } else if (_red_led_out) {                        // hand the pin back to the charger
      pinMode(LED_POWER, INPUT);
      _red_led_out = false;
      _red_led_lit = false;
    }
  } else {
    _red_full = false;
    _red_batt_at = 0;                           // re-poll promptly on the next USB attach
    if (!_red_led_out) {                        // take the pin for the heartbeat
      pinMode(LED_POWER, OUTPUT);
      digitalWrite(LED_POWER, !LED_STATE_ON);
      _red_led_out = true;
      _red_led_lit = false;
      _red_led_at = now + RED_LED_PERIOD_MILLIS;
    }
    if (!_red_led_lit && now >= _red_led_at) {          // start a blip
      _red_led_lit = true;
      digitalWrite(LED_POWER, LED_STATE_ON);
      _red_led_at = now + RED_LED_ON_MILLIS;
    } else if (_red_led_lit && now >= _red_led_at) {    // end the blip
      _red_led_lit = false;
      digitalWrite(LED_POWER, !LED_STATE_ON);
      _red_led_at = now + RED_LED_PERIOD_MILLIS - RED_LED_ON_MILLIS;
    }
  }
#endif
}

// Low-battery auto power-off (the only shutdown path; manual hibernate was
// removed). The caller has already painted the reason on the e-ink, which holds
// the image with no power, so we just go dark and cut power.
void UITask::shutdown() {
#ifdef PIN_BUZZER
  buzzer.shutdown();
  uint32_t buzzer_timer = millis();
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();
#endif
  if (_display != NULL) _display->turnOff();   // kill the frontlight; the panel keeps the image
  radio_driver.powerOff();
  _board->powerOff();
}

uint32_t UITask::currentEpoch() const {
  return the_mesh.getRTCClock()->getCurrentTime();
}

// A framed pop-up window with a dithered drop shadow -- old DOS/BBS dialog style,
// drawn entirely from glyphs: a single-line box (U+250C..2518) over a cleared white
// interior, with a U+2592 medium-shade shadow offset down-right for depth. Popups use
// the *single* line weight so they read as transient dialogs -- the persistent menu
// pages wear the heavier double-line window chrome (uichrome::rule / bottomBar).
// ww is a multiple of 8 and wh of 16 so the shadow tiling lands exactly; the white
// interior overdraws the overlap, leaving an 8px dithered strip on the right + bottom.
static void drawDosFrame(DisplayDriver& d, int wx, int wy, int ww, int wh, const char* title = nullptr) {
  d.setColor(DisplayDriver::LIGHT);
  for (int sy = wy + 8; sy < wy + 8 + wh; sy += 16)
    for (int sx = wx + 8; sx < wx + 8 + ww; sx += 8) { d.setCursor(sx, sy); d.print("\xE2\x96\x92"); }
  d.setColor(DisplayDriver::DARK);
  d.fillRect(wx, wy, ww, wh);
  d.setColor(DisplayDriver::LIGHT);
  d.setCursor(wx, wy);                     d.print("\xE2\x94\x8C");   // ┌
  d.setCursor(wx + ww - 8, wy);            d.print("\xE2\x94\x90");   // ┐
  d.setCursor(wx, wy + wh - 16);           d.print("\xE2\x94\x94");   // └
  d.setCursor(wx + ww - 8, wy + wh - 16);  d.print("\xE2\x94\x98");   // ┘
  for (int x = wx + 8; x < wx + ww - 8; x += 8) {
    d.setCursor(x, wy);            d.print("\xE2\x94\x80");   // ─
    d.setCursor(x, wy + wh - 16);  d.print("\xE2\x94\x80");   // ─
  }
  for (int y = wy + 16; y < wy + wh - 16; y += 16) {
    d.setCursor(wx, y);            d.print("\xE2\x94\x82");   // │
    d.setCursor(wx + ww - 8, y);   d.print("\xE2\x94\x82");   // │
  }
  // BBS-style title bookended into the top border: ──┤ title ├──
  if (title && title[0]) {
    int tw = d.getTextWidth(title);
    int seg = tw + 32;                              // ┤ + space + title + space + ├
    if (seg <= ww - 16) {                           // fits between the corners
      int segx = (wx + (ww - seg) / 2) & ~7;
      d.setColor(DisplayDriver::DARK);
      d.fillRect(segx, wy, seg, 16);                // erase the ─ under the title
      d.setColor(DisplayDriver::LIGHT);
      d.setCursor(segx, wy);                d.print("\xE2\x94\xA4");   // ┤
      d.setCursor(segx + 16, wy);          d.print(title);
      d.setCursor(segx + 16 + tw + 8, wy); d.print("\xE2\x94\x9C");   // ├
    }
  }
}

// Small centered single-line alert toast.
static void drawDosPopup(DisplayDriver& d, const char* text) {
  d.setTextSize(1);
  int tw = d.getTextWidth(text);
  int ww = ((tw + 32 + 7) / 8) * 8; if (ww > 184) ww = 184;
  const int wh = 48, wx = ((d.width() - ww) / 2) & ~7, wy = 64;
  drawDosFrame(d, wx, wy, ww, wh);
  d.setColor(DisplayDriver::LIGHT);
  d.setCursor(wx + (ww - tw) / 2, wy + 16);
  d.print(text);
}

// Left-aligned new-message box: title line (sender) + wrapped body. Sizes to the message
// -- small for a one-liner, growing to a near-full-screen 184x176 for long ones.
static void drawMsgPopup(DisplayDriver& d, const char* const* lines, int n) {
  d.setTextSize(1);
  if (n < 1) return;
  if (n > 9) n = 9;                                 // max rows that fit (with the shadow)
  const char* title = lines[0];                     // sender -> bookended into the top border
  const int bodyN = n - 1;                          // remaining lines = wrapped body
  int titleW = d.getTextWidth(title);
  int bodyW = 0;
  for (int i = 1; i < n; i++) { int w = d.getTextWidth(lines[i]); if (w > bodyW) bodyW = w; }
  int needT = titleW + 48;                          // ╡_title_╞ (32) + 2 corners (16)
  int needB = bodyW + 16;                           // 2 side borders
  int ww = ((( needT > needB ? needT : needB ) + 7) / 8) * 8;
  if (ww > 184) ww = 184;
  if (ww < 64)  ww = 64;
  const int wh = (bodyN + 2) * 16;                   // title border + body rows + bottom border
  const int wx = ((d.width() - ww) / 2) & ~7;        // centered
  const int wy = ((d.height() - wh) / 2) & ~7;
  drawDosFrame(d, wx, wy, ww, wh, title);
  d.setColor(DisplayDriver::LIGHT);
  for (int i = 0; i < bodyN; i++)
    d.drawTextEllipsized(wx + 8, wy + 16 + i * 16, ww - 16, lines[i + 1]);
}

void UITask::loop() {
#ifdef USE_NATIVE_EINK_UI
  display.pollHibernate();   // deep-sleep the panel once refreshes have gone idle
#endif


  // Per the official M1 manual: circle = function/select, triangle = page turn. ev drives
  // the function/element logic, ev2 the page logic -- so circle feeds ev, triangle feeds ev2.
  int ev  = back_btn.check();    // circle  (back_btn / GPIO39) = function / select
  int ev2 = user_btn.check();    // triangle (user_btn / GPIO42) = page turn
  bool was_on = (_display != NULL && _display->isOn());   // a press that only wakes the screen shouldn't count as interaction

  if (curr == _help || curr == _qr) {
    // ---- help / QR overlay ---- any button press dismisses it back to where we were
    if (ev != BUTTON_EVENT_NONE || ev2 != BUTTON_EVENT_NONE) {
      if (!wakeIfOff()) {
        curr_page = _help_return_page;
        setCurrScreen(_help_return ? _help_return : (UIScreen*)pages[PAGE_HOME]);
      }
    }
  } else if (_revert_at != 0) {
    // ---- new-message popup box ---- drawn over the current page; any press just
    // dismisses it (doesn't also act on the page underneath). A press while the
    // screen is off only wakes it; the box stays up until the next press.
    if (ev != BUTTON_EVENT_NONE || ev2 != BUTTON_EVENT_NONE) {
      if (!wakeIfOff()) { _revert_at = 0; _next_refresh = 0; }
    }
  } else if (curr == _detail) {
    // ---- read view ---- (click + hold only)
    //   circle    click = page down body, then on to the next (older) message
    //   circle    hold  = previous (newer) message  (CLI rescue in first 8s)
    //   triangle  hold  = back to the message list (keeps the drill context)
    // Triangle HOLD is the universal "back" everywhere; a leaf read view has no
    // "next page", so triangle click is intentionally inert here.
    if (ev == BUTTON_EVENT_LONG_PRESS && millis() - ui_started_at < 8000) {
      the_mesh.enterCLIRescue();
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      if (!wakeIfOff()) navMessage(-1);
    } else if (ev != BUTTON_EVENT_NONE) {
      if (!wakeIfOff() && !_detail->scrollDown()) navMessage(+1);
    }
    if (ev2 == BUTTON_EVENT_LONG_PRESS) {
      // return to the message list WITHOUT resetFocus so the drill context
      // (selected conversation/node) is preserved
      if (!wakeIfOff()) { curr_page = PAGE_MESSAGES; setCurrScreen(_messages); }
    }
  } else {
    // ---- circle button (back_btn / GPIO39): ELEMENT / function navigation ----
    //   click = next element (wraps around)
    //   hold  = activate / select focused element   (or CLI rescue in first 8s);
    //           on a multi-option element, keeping it held steps through the
    //           options at CYCLE_HOLD_REPEAT_MS
    if (ev == BUTTON_EVENT_CLICK) {
      if (!wakeIfOff() && onPage()) ((ElementScreen*)curr)->focusNext();
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      if (millis() - ui_started_at < 8000) {   // rescue window after boot
        the_mesh.enterCLIRescue();
      } else if (!wakeIfOff() && onPage()) {
        ((ElementScreen*)curr)->activateFocused();
        if (((ElementScreen*)curr)->focusedIsCycle()) {
          _cycle_repeat_at = millis() + CYCLE_HOLD_REPEAT_MS;   // keep stepping while held
        }
      }
    }

    // ---- triangle button (user_btn / GPIO42): PAGE navigation ----
    //   click = next page (wraps around)
    //   hold  = previous page (back), or pop up one Messages drill level
    if (ev2 == BUTTON_EVENT_CLICK) {
      if (!wakeIfOff() && onPage()) nextPage();
    } else if (ev2 == BUTTON_EVENT_LONG_PRESS) {
      if (!wakeIfOff() && onPage()) {
        if (curr == _messages && !_messages->atTopLevel()) _messages->goBack();
        else prevPage();
      }
    }
  }

  // Auto-repeat: while the circle button stays held on a multi-option element,
  // step to the next option every CYCLE_HOLD_REPEAT_MS. Cancelled the moment the
  // button is released, focus moves, or we leave the page.
  if (_cycle_repeat_at != 0) {
    if (!back_btn.isPressed() || !onPage() || !((ElementScreen*)curr)->focusedIsCycle()) {
      _cycle_repeat_at = 0;
    } else if (millis() >= _cycle_repeat_at) {
      ((ElementScreen*)curr)->activateFocused();
      _cycle_repeat_at = millis() + CYCLE_HOLD_REPEAT_MS;
      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 0;   // repaint so each step is visible
    }
  }

  if (ev != BUTTON_EVENT_NONE || ev2 != BUTTON_EVENT_NONE) {
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;                        // repaint after interaction
    if (was_on) _revert_at = 0;               // real interaction (not a wake) cancels auto-return
  }

  // new-message popup box times out -> clear it; the underlying page stays put
  if (_revert_at != 0 && millis() > _revert_at) {
    _revert_at = 0;
    _next_refresh = 0;   // repaint to erase the box
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
    // Sleep is backlight-only: e-ink retains its image after turnOff(). We kill the
    // frontlight and repaint once (partial) to drop the selection marker, then keep the
    // clock/ages current with a plain partial once a minute. Ghosting is cleared by the
    // in-session swing (every EINK_LIMIT_FASTREFRESH partials, inside NativeEinkDisplay) --
    // no dedicated full/idle refresh here, so the only flash is that occasional swing.
    bool asleep = millis() > _auto_off;
    if (asleep && _display->isOn()) {
      _display->turnOff();
      _idle_refresh_at = millis() + EINK_IDLE_REFRESH_MILLIS;
      _next_refresh = 0;   // partial repaint so the selection marker disappears immediately
    }
    // periodic idle re-draw: keep the clock/ages current with a plain partial
    if (asleep && _idle_refresh_at != 0 && millis() > _idle_refresh_at) {
      _idle_refresh_at = millis() + EINK_IDLE_REFRESH_MILLIS;
      _next_refresh = 0;   // force the (partial) render below
    }
#else
    const bool asleep = false;
#endif

    if (millis() >= _next_refresh && curr) {
      // hide the selection bar while asleep (visual "screen off" hint); it
      // returns on the next render once awake.
      ElementScreen* page = (onPage() && curr != _detail && curr != _help && curr != _qr) ? (ElementScreen*)curr : NULL;
      if (page) page->setFocusVisible(!asleep);

      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (!asleep && millis() < _alert_expiry) {  // DOS/BBS-style alert toast (awake only)
        drawDosPopup(*_display, _alert);
        _next_refresh = _alert_expiry;
      } else if (!asleep && _revert_at != 0) {    // big new-message popup box
        const char* ml[MSG_POPUP_MAXLINES];
        int mn = _mpop_n < MSG_POPUP_MAXLINES ? _mpop_n : MSG_POPUP_MAXLINES;
        for (int i = 0; i < mn; i++) ml[i] = _mpop[i];
        drawMsgPopup(*_display, ml, mn);
        _next_refresh = _revert_at;
      } else if (asleep) {
        _next_refresh = _idle_refresh_at;   // stay dark until the next minute re-draw
      } else {
        // awake: poll fast so content changes show promptly (CRC dedups no-change frames);
        // leave truly-static overlays (Help/QR) on their long cadence.
        if (delay_millis > FAST_REFRESH_MS && delay_millis < STATIC_REFRESH_MS)
          delay_millis = FAST_REFRESH_MS;
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
          if (curr) curr->render(*_display);          // keep the page behind the modal
          const char* ml[2] = { "Low Battery", "Shutting down..." };
          drawMsgPopup(*_display, ml, 2);             // as a popup window (title + body)
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
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled");
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
  showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON");
  _next_refresh = 0;
#endif
}
