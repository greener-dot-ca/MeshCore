# ThinkNode M1 native e-ink UI (`ui-thinknode-m1`)

A companion-radio UI for the ThinkNode M1's **200×200 e-ink** display, driven by
the case's **two buttons** (triangle + circle). Every screen is a vertical list
of items: you scroll with one button and act/navigate with the other.

This is the default M1 companion-radio BLE build. The previous UI is still
available as a `*_legacy` build — see [Building](#building).

## Features

- **200×200 native e-ink rendering** — crisp, true-resolution text and chrome (no upscaling).
- **Real Unicode text, symbols & icons** via a built-in GNU Unifont subset.
- **Two-button navigation** — circle scrolls/selects, triangle pages and is a universal back; triple-click triangle for Help.
- **Status bar** — app-linked, GPS, muted, Bluetooth-off, charging, battery and clock indicators.
- **No-flicker e-ink** — repaints only when something actually changed, never on a timer.
- **Pages:**
  - **Home** — node name, Buzzer/Bluetooth/GPS toggles, App/Unread/Uptime.
  - **Messages** — unread-by-app queue grouped into conversations, with a drill-down word-wrapped read view.
  - **Mesh** — `Send Advert` action + traffic stats (contacts, sent/recv, airtime, queue).
  - **RX Log** — live per-packet receive log: type (ADV/MSG/CHAN/ACK/…), signal (RSSI/SNR), sender/channel name when known, and reception time.
  - **Radio** — off-grid (client-repeat) toggle, 433/869/918 MHz preset, LoRa params + live RSSI/SNR/noise.
  - **GPS** — toggle + last-good-fix position/sats/altitude.
  - **Bluetooth** — toggle + pairing pin.
  - **Buzz** — buzzer on/off + notification sound (CTU / Beep / Morse).
  - **Time** — 12/24h format + UTC offset.
  - **Power** — battery %, voltage, charging + Hibernate.
- **Graceful sleep** — after 15 s idle the frontlight turns off and the image is retained; as it sleeps (and once a minute after) the panel is re-driven with a full black/white swing to clear accumulated partial-update ghosting, instead of the constant repaint that drives the panel continuously.
- **Red LED battery heartbeat** — blinks every ~5 s while on battery; when plugged in the charger keeps ownership of the LED.

## What's different from the old UI

Compared with the previous M1 firmware (the `ui-new` build, now
`ThinkNode_M1_companion_radio_ble_legacy`):

- **Crisp, full-resolution display.** Draws natively at the panel's true
  200×200 pixels instead of upscaling a 128-pixel logical image, so text and
  chrome are sharp instead of blocky.
- **Real text, symbols and icons.** A built-in GNU Unifont set renders accents,
  punctuation, currency, arrows and pictographs — where the old UI showed a
  generic `█` block for anything beyond plain ASCII.
- **No e-ink flicker.** The screen redraws only when you press a button (and even
  then only if something actually changed), instead of the old timer-driven
  repaint every 1–5 seconds. Easier on the eyes and the battery.
- **Two-button navigation.** Circle moves/selects within a screen; triangle flips
  between pages and is a universal "back" (details below).
- **Organized Messages.** Unread messages are grouped into conversations — one
  per channel and per direct contact, with a count and a last-message time —
  instead of one flat list. Drill into a conversation to read its messages.
- **At-a-glance status bar.** App-linked, GPS, muted, charging, battery and clock
  indicators across the top, plus a red 📵 when Bluetooth is turned off.
- **Sleeps gracefully.** After ~15s idle the frontlight turns off but the image
  stays on the e-ink (it's retained); while asleep it re-draws only once a minute
  (instead of every second), and any button press wakes it without also acting on
  the screen.

## Using it

### Buttons

Two physical buttons, named after the symbols on the case: the **triangle**
button and the **circle** button.

| Button       | Click               | Long press (hold)       | Double click             |
|--------------|---------------------|-------------------------|--------------------------|
| **Circle**   | next item (down)    | previous item (up)¹     | select / open the item   |
| **Triangle** | next page           | previous page / back²   | jump to Home page        |

Triple-click the triangle button on any page to pop up the **Help** overlay (icon
legend + this button guide); any press dismisses it.

¹ In the first 8 s after boot, holding the circle button instead enters CLI rescue.

² **Hold the triangle button is always "back".** On a normal page it steps to the
previous page. Inside Messages it pops up one level (message list →
conversations) before resuming page navigation. In the read view it returns to
the message list.

Any button press while the display is asleep only wakes it.

### Pages

Pages are shown one at a time, in a fixed order shown by the **page-dot strip**
at the bottom (one dot per page, the current one larger). When a page is taller
than the screen, a **scrollbar** and up/down **more-arrows** appear on the right;
selection moves item-to-item and the view follows it.

1. **Home** — node name, then `Buzzer` / `Bluetooth` / `GPS` toggles, then `App`
   (connected?), `Unread` count, `Uptime`.
2. **Messages** — the unread messages (see below).
3. **Mesh** — `Send Advert` action, then traffic stats: `Contacts`, `Sent F/D`,
   `Recv F/D`, `Airtime`, `Queue`.
4. **RX Log** — live per-packet receive log (newest first): packet type
   (`ADV`/`MSG`/`CHAN`/`ACK`/`PATH`/…), signal (`-rssi/+snr`), and the sender or
   channel name when the decode resolves one (otherwise `?`), with the reception
   clock time right-aligned. Scrolls; rebuilt from the in-RAM `rx_log` ring.
5. **Radio** — RF config + link. The `Off-grid` (client-repeat) toggle and the
   `Off grid freq` preset (`433`/`869`/`918` MHz) are the editable controls; then
   the tuned LoRa params `Freq` (MHz), `BW` (kHz), `SF`, `CR`, `TX` (read-only,
   set via the app), and live readings `Noise`, `RSSI`, `SNR`.
6. **GPS** — `GPS` toggle, then `Fix` (live?), `Last` (age of last good fix),
   `Sats`, `Pos` (lat,lon), `Alt`. Position/sats/alt show the *last good fix* so
   the page stays useful after losing signal or switching GPS off.
7. **Bluetooth** — `Bluetooth` toggle, `App` (connected?), `Pin`.
8. **Buzz** — `Buzzer` toggle and notification `Sound` (CTU / Beep / Morse).
9. **Time** — clock `Format` (12/24h) and `UTC +/-` offset.
10. **Power** — `Battery` %, `Voltage`, `Charging`, and a `Hibernate` action.

A boot **Splash** screen (logo + version + build date) shows for ~3 s first.

### Messages

A live view of the offline **"unread-by-app" queue** — messages received over the
mesh that the companion phone app hasn't pulled yet — organized in two levels
(circle double-click descends, hold triangle goes back up):

- **Conversations** — one row per channel *and* per direct contact, with a
  message count and the last message's relative time (`#general (5)`,
  `Alice (3)`).
- **Messages** — every message in the chosen conversation, newest first. Channel
  rows show the sender (`Alice: hey all`); direct rows show just the text.
  Selecting one opens a full-screen, word-wrapped **read view** (top line is the
  `#channel sender` breadcrumb; circle pages down long messages, hold triangle
  returns to the list).

This queue is the *only* on-device message store: there's no manual dismiss and
no saved history. Counts and previews reflect what hasn't been synced yet and
clear once the phone app pulls them — most useful when running the device
standalone, where the queue acts as a rolling buffer. Empty state shows
"No messages".

The status-bar icons are explained in the in-device Help overlay (triple-click
triangle).

---

## Implementation

It shares the same `UITask` surface as the other modules (`begin()` + the
`AbstractUITask` virtuals), so `main.cpp` / `MyMesh` need zero changes, and it
lives entirely in this directory.

### Design goals

- **Two buttons, no chords beyond click/long/double.** No touch, no encoder.
- **e-ink friendly.** The panel does a slow full refresh and CRC-gates redraws,
  so the UI only repaints in response to a real interaction — never on a timer.
  No animation, no "Ns ago" ticking in a draw path, no per-frame-changing pixels.
  `render()` returns a long delay; an interaction sets `_next_refresh = 0` to
  force the single repaint.
- **No heap, no `std::function`.** Elements are plain structs holding C function
  pointers plus a `void* ctx`; all live state lives in the bound context, so the
  element itself is effectively const.

### Elements

Each screen owns a fixed array of `UIElement`s. **Every** element is focusable
(so you can scroll past a tall page to reach an actionable element at the bottom);
only some are *actionable*. The focused element draws a selection bar; actionable
ones carry an extra indicator.

| Kind          | Activation does           |
|---------------|---------------------------|
| `Label`       | — (read-only status)      |
| `Toggle`      | flips an on/off state     |
| `Action`      | runs a one-shot action    |
| `OptionCycle` | advances to next option   |

A one-row message variant (`makeMessageRow`) is used in Messages: a left-aligned
line with a right-aligned relative time supplied via a `get_text` callback. A
two-row read-only variant (`makeTwoRow`) is also available (row 1 title, row 2
body).

### Architecture

| File                       | Responsibility |
|----------------------------|----------------|
| `UIElements.{h,cpp}`       | `UIElement` struct, `ElemKind`, the `make*` factories, per-element draw. |
| `ElementScreen.{h,cpp}`    | Scrollable element-list base: status bar, page-dots, scrollbar, focus/scroll logic, input routing. |
| `Pages.{h,cpp}`            | Concrete screens (Splash, Home, Messages drill-down, Mesh, RX Log, Radio, GPS, Bluetooth, Buzz, Time, Power) and their element getters/callbacks. |
| `UITask.{h,cpp}`           | `AbstractUITask` implementation: owns the pages, button dispatch, refresh timing, LED/buzzer, message intake, shutdown. |
| `NativeEinkDisplay.{h,cpp}`| `DisplayDriver` that draws the GxEPD2 panel 1:1 at 200×200 (no scale), with a CRC dirty-check so a static frame costs no panel refresh, plus a UTF-8-aware Unifont blitter. |
| `unifont_glyphs.h`         | Generated packed GNU Unifont subset (sorted codepoint index + 1-bit glyph blob); see `tools/gen_unifont.py`. |
| `icons.h`                  | XBM bitmaps (logo, status-bar glyphs). |

Buttons: triangle = btn1 / user_btn / GPIO42; circle = btn2 / back_btn / GPIO39
(active-low + internal pull-up).

Chrome geometry is in native physical pixels on the 200×200 panel (no upscaler):
a top status bar (battery + title) and bottom page-dot strip, with row height
fitting the 16px Unifont cell plus padding (`UIELEM_ROW_H`).

### Text rendering (GNU Unifont)

All text is drawn with a packed **GNU Unifont** subset (1-bit, 8×16 / 16×16
bitmaps) by `NativeEinkDisplay`, giving real Unicode/symbol coverage instead of
collapsing non-ASCII to a `█` block. A host generator, `tools/gen_unifont.py`,
filters the Unifont `.hex` files to a chosen set of codepoint ranges into
`unifont_glyphs.h` (a sorted, binary-searchable codepoint index + a 1-bit glyph
blob); a UTF-8-aware blitter does per-codepoint lookup and blits at native
physical resolution. Toggles (☐/☑), cycle arrows (◀▶) and page dots (●/○) are
Unifont glyphs.

Caveats: the curated subset is Latin + punctuation/symbols/arrows (CJK/Hangul are
omitted — the full BMP would be ~1.5–2 MB). Emoji (Plane 1, 4-byte UTF-8) are
crude **monochrome** 16×16 line-art, not color. A dynamic battery bar stays a
drawn widget (a static 🔋 glyph would lose the charge level).

### Building

The default M1 companion-radio BLE env (`DISPLAY_CLASS=NativeEinkDisplay` +
`USE_NATIVE_EINK_UI`):

```
pio run -e ThinkNode_M1_companion_radio_ble          # flash: add -t upload
```

The legacy `ui-new` GxEPD-upscaler UI remains available as
`ThinkNode_M1_companion_radio_ble_legacy`.

Because the display is e-ink, `turnOff()` only kills the frontlight (the image is
retained), so `AUTO_OFF_MILLIS = 15000` is a 15s frontlight timeout, not a screen
clear; `KEEP_DISPLAY_ON_USB` keeps the panel lit while charging. While asleep the
panel is left static and only re-drawn every `EINK_IDLE_REFRESH_MILLIS` (1 min); as
it sleeps and on each idle tick it is re-driven with a full black/white swing
(`fullRefresh()`) to clear accumulated partial-update ghosting.

The mechanism: `main.cpp` includes `"UITask.h"` through the per-board
`-I examples/companion_radio/<module>` include path and globs
`<…/<module>/*.cpp>`, so swapping the module path swaps the whole UI with no
shared-code edits.

### Future work: Nav page

A `PAGE_NAV` that points you toward a target (bearing + distance), GPS-only.

What's already available:
- **Course/speed** from the GPS: `MicroNMEA::getCourse()` (track angle, m°) and
  `getSpeed()` — not yet surfaced by `MicroNMEALocationProvider` (needs accessors).
- **A target location**: `ContactInfo.gps_lat/gps_lon` (6-dp), populated from
  adverts that share location — so "navigate to a contact" is mesh-native.
  A manually-marked waypoint ("mark here, guide me back") is a possible v2.

The hard constraint — **the M1 has no compass/magnetometer** (only GPS + RTC on
I²C; see `ThinkNodeM1SensorManager`). So the only heading is **course-over-ground**,
which is valid *only while moving*:
- **Moving:** known travel heading → draw a **relative turn-arrow** toward the
  target (target bearing − course). COG is jittery below walking speed at the
  GPS's 1 Hz update, so gate the arrow on a minimum speed.
- **Stopped:** no heading → show **absolute bearing** as text only
  ("Brg 045° NE, 1.24 km") and a "move to get heading" hint; a turn-arrow would
  be meaningless.

Rendering: don't rotate a bitmap (stripes/ugly on the e-ink scaler) — ship 8
(or 16) precomputed arrow glyphs (N/NE/…/NW) and snap to the nearest octant,
reusing the `es_gps_icon` nav-arrow style.

Proposed v1: navigate to a selected contact's last shared location; always show
distance + absolute bearing + speed; show the relative arrow only when moving.
