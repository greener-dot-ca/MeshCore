# ThinkNode M1 native e-ink UI (`ui-thinknode-m1`)

A companion-radio UI for the ThinkNode M1's **200×200 e-ink** display, driven by
just **two buttons**. Every page is a vertical list of *selectable elements*; you
scroll through elements with one button and flip between pages with the other.

It renders **natively at the panel's full 200×200 resolution** (no logical-128
upscaler, so chrome is crisp) and draws text with a packed **GNU Unifont** subset
for real Unicode/symbol coverage. It is the **default M1 companion-radio BLE
build** (`ThinkNode_M1_companion_radio_ble`); the older `ui-new` GxEPD-upscaler UI
is kept as `ThinkNode_M1_companion_radio_ble_legacy`. It shares the same `UITask`
surface (`begin()` + the `AbstractUITask` virtuals), so `main.cpp` / `MyMesh` need
zero changes, and lives entirely in this directory.

## Design goals

- **Two buttons, no chords beyond click/long/double.** No touch, no encoder.
- **e-ink friendly.** The panel does a slow full refresh and CRC-gates redraws,
  so the UI must only repaint in response to a real interaction — never on a
  timer. There is no animation, no "Ns ago" ticking in a draw path, no
  per-frame-changing pixels. `render()` returns a long delay; an interaction
  sets `_next_refresh = 0` to force the single repaint.
- **No heap, no `std::function`.** Elements are plain structs holding C function
  pointers plus a `void* ctx`; all live state lives in the bound context, so the
  element itself is effectively const.

## Interaction model

### Pages
Pages are arranged in a fixed order and shown one at a time. The bottom of the
screen carries a **page-dot strip** — one dot per page, the current page drawn
larger.

A page's content can be taller than the viewport. When it is, a **scrollbar**
runs down the right edge and **more-arrows** indicate content above/below.
Scrolling is element-aligned: focus moves element-to-element and the viewport
follows the focused element (`ensureFocusVisible`).

### Elements
Each element is one of:

| Kind          | Selectable | Actionable | Activation does           |
|---------------|------------|------------|---------------------------|
| `Label`       | yes        | no         | — (read-only status)      |
| `Toggle`      | yes        | yes        | flips an on/off state     |
| `Action`      | yes        | yes        | runs a one-shot action    |
| `OptionCycle` | yes        | yes        | advances to next option   |

**Every** element is focusable (so you can always scroll past a tall page to
reach an actionable element at the bottom); only some are *actionable*. The
focused element draws a selection bar/border; actionable elements carry an extra
indicator so it's clear they can be acted upon.

A one-row message variant (`makeMessageRow`) is used in the Messages drill-down:
a left-aligned line (channel/contact/node + count, or a message body) with a
right-aligned relative time supplied via a `get_text` callback. A two-row
read-only variant (`makeTwoRow`) is also available (row 1 title, row 2 body).

### Buttons

Two physical buttons, named after the symbols on the case: the **triangle**
button (btn1 / user_btn / GPIO42) and the **circle** button (btn2 / back_btn /
GPIO39, active-low + internal pull-up).

| Button       | Click               | Long press (hold)       | Double click             |
|--------------|---------------------|-------------------------|--------------------------|
| **Triangle** (elements) | next element (down) | previous element (up)¹  | activate focused element |
| **Circle** (pages)      | next page           | previous page / back²   | jump to Home page        |

¹ In the first 8 s after boot a hold of the triangle button instead enters CLI rescue.

² **Hold the circle button is always "back".** On a normal page it steps to the
previous page. Inside the Messages drill-down it pops up exactly one level
(messages → nodes → conversations) before resuming page navigation. In the
message read view it returns to the message list (keeping the drill context).

Any button press while the display is asleep only wakes it (it does not also act
on the current screen).

## Pages

Page order is the circle-button navigation order and the page-dot order
(`Pages.h: enum { PAGE_HOME … }`):

1. **Home** — node name, then `Buzzer` / `Bluetooth` / `GPS` toggles, then `App`
   (connected?), `Unread` count, `Uptime`.
2. **Messages** — a live, grouped view of the offline **"unread-by-app" queue**
   (messages received over the mesh that the companion phone app hasn't pulled
   yet), drilled in two levels (triangle double-click descends, hold circle
   ascends):
   - **Conversations** — one row per channel *and* per DM contact, with a message
     count and the last message's relative time (`#general (5)`, `Alice (3)`).
   - **Messages** — every message in the selected conversation, newest first.
     Channel rows are node-prefixed (`Alice: hey all`, since the sender name is
     embedded in the channel body); DM rows are the clean body. Activating one
     opens a full-screen **word-wrapped read view** whose top line is the
     breadcrumb `#channel node` (triangle = page down long messages, hold circle =
     back to the list).

   The queue is the *only* on-device store: there is **no manual dismiss** and no
   persistent history — counts/last reflect queued-but-unsynced messages and clear
   when the app syncs (meaningful for standalone use, where the queue is a
   `OFFLINE_QUEUE_SIZE` rolling buffer). Empty state shows "No messages".
3. **Mesh** — `Send Advert` action, then stats: `Contacts`, `Sent F/D`,
   `Recv F/D`, `Airtime`, `Noise`, `RSSI`, `SNR`, `Queue` (unread/queue capacity).
4. **GPS** — `GPS` toggle, then `Fix` (live?), `Last` (age of last good fix),
   `Sats`, `Pos` (lat,lon), `Alt`. Position/sats/alt show the *last good fix* so
   the page stays useful after losing signal or switching GPS off.
5. **Bluetooth** — `Bluetooth` toggle, `App` (connected?), `Pin`.
6. **Power** — `Battery` %, `Voltage`, `Charging`, and a `Hibernate` action.

A boot **Splash** screen (logo + version + build date, mirroring `ui-new`)
shows for ~3 s before Home.

## Code map

| File                       | Responsibility |
|----------------------------|----------------|
| `UIElements.{h,cpp}`       | `UIElement` struct, `ElemKind`, the `make*` factories, per-element draw. |
| `ElementScreen.{h,cpp}`    | Scrollable element-list base: status bar, page-dots, scrollbar, focus/scroll logic, input routing. |
| `Pages.{h,cpp}`            | Concrete screens (Splash, Home, Messages drill-down, Mesh, GPS, Bluetooth, Time, Power) and their element getters/callbacks. |
| `UITask.{h,cpp}`           | `AbstractUITask` implementation: owns the pages, button dispatch, refresh timing, LED/buzzer, message intake, shutdown. |
| `NativeEinkDisplay.{h,cpp}`| `DisplayDriver` that draws the GxEPD2 panel 1:1 at 200×200 (no scale), with a CRC dirty-check so a static frame costs no panel refresh, plus a UTF-8-aware Unifont blitter. |
| `unifont_glyphs.h`         | Generated packed GNU Unifont subset (sorted codepoint index + 1-bit glyph blob); see `tools/gen_unifont.py`. |
| `icons.h`                  | XBM bitmaps (logo, status-bar glyphs). |

### Chrome geometry (`ElementScreen`)
Geometry is in native physical pixels on the 200×200 panel (no upscaler):
- Top status bar (battery + title) and bottom page-dot strip.
- Row height fits the 16px Unifont cell plus padding (`UIELEM_ROW_H`).

## Building

The default M1 companion-radio BLE env (`DISPLAY_CLASS=NativeEinkDisplay` +
`USE_NATIVE_EINK_UI`):

```
pio run -e ThinkNode_M1_companion_radio_ble          # flash: add -t upload
```

The legacy `ui-new` GxEPD-upscaler UI remains available as
`ThinkNode_M1_companion_radio_ble_legacy`.

Because the display is e-ink, `turnOff()` only kills the frontlight (the image is
retained), so `AUTO_OFF_MILLIS = 10000` is a 10s frontlight timeout, not a screen
clear; `KEEP_DISPLAY_ON_USB` keeps the panel lit while charging.

The mechanism: `main.cpp` includes `"UITask.h"` through the per-board
`-I examples/companion_radio/<module>` include path and globs
`<…/<module>/*.cpp>`, so swapping the module path swaps the whole UI with no
shared-code edits.

## Future work

### Nav page

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

## Text rendering (GNU Unifont)

All text is drawn with a packed **GNU Unifont** subset (1-bit, 8×16 / 16×16
bitmaps) by `NativeEinkDisplay`, giving real Unicode/symbol coverage instead of
collapsing non-ASCII to a `█` block. A host generator,
`tools/gen_unifont.py`, filters the Unifont `.hex` files to a chosen set of
codepoint ranges into `unifont_glyphs.h` (a sorted, binary-searchable codepoint
index + a 1-bit glyph blob); a UTF-8-aware blitter does per-codepoint lookup and
blits at native physical resolution. Toggles (☐/☑), cycle arrows (◀▶) and page
dots (●/○) are Unifont glyphs.

Caveats: the curated subset is Latin + punctuation/symbols/arrows (CJK/Hangul are
omitted — the full BMP would be ~1.5–2 MB). Emoji (Plane 1, 4-byte UTF-8) are
crude **monochrome** 16×16 line-art, not color. A dynamic battery bar stays a
drawn widget (a static 🔋 glyph would lose the charge level).
