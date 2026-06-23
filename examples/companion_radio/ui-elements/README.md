# Element-based UI for tiny screens (`ui-elements`)

A companion-radio UI for small, slow displays (the ThinkNode M1's 128×128 e-ink
in particular) driven by just **two buttons**. Every page is a vertical list of
*selectable elements*; you scroll through elements with one button and flip
between pages with the other.

It is a **drop-in alternative to `ui-new`**: same `UITask` surface
(`begin()` + the `AbstractUITask` virtuals), so `main.cpp` / `MyMesh` need zero
changes. It lives entirely in this directory and is selected per-board via the
build env (see *Building*), leaving the ~30 boards that share `ui-new`
byte-for-byte untouched.

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

A two-row read-only variant (`makeTwoRow`) is used for message previews: row 1 is
a title (origin), row 2 is the body, supplied dynamically via a `get_text`
callback.

### Buttons

Two physical buttons, named after the symbols on the case: the **triangle**
button (btn1 / user_btn / GPIO42) and the **circle** button (btn2 / back_btn /
GPIO39, active-low + internal pull-up).

| Button       | Click               | Long press (hold)       | Double click             |
|--------------|---------------------|-------------------------|--------------------------|
| **Triangle** (elements) | next element (down) | previous element (up)¹  | activate focused element |
| **Circle** (pages)      | next page           | previous page (back)    | jump to Home page        |

¹ In the first 8 s after boot a hold of the triangle button instead enters CLI rescue.

**Hold the circle button is always "back"** — on a page it steps to the previous
page; in the message read view it returns to the list.

Any button press while the display is asleep only wakes it (it does not also act
on the current screen).

## Pages

Page order is the circle-button navigation order and the page-dot order
(`Pages.h: enum { PAGE_HOME … }`):

1. **Home** — node name, then `Buzzer` / `Bluetooth` / `GPS` toggles, then `App`
   (connected?), `Unread` count, `Uptime`.
2. **Messages** — a live view of the offline **"unread-by-app" queue** (messages
   received over the mesh that the companion phone app hasn't pulled yet): one
   scrollable two-row preview per message, newest first, listing the newest
   `MSG_PAGE_MAX` (32). Activating a message (triangle double-click) opens a
   full-screen **word-wrapped read view** (hold circle = back to list, triangle =
   page down for long messages). There is **no manual dismiss** — messages clear
   automatically when the app syncs them. Empty state shows "No messages".
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

| File                    | Responsibility |
|-------------------------|----------------|
| `UIElements.{h,cpp}`    | `UIElement` struct, `ElemKind`, the `make*` factories, per-element draw. |
| `ElementScreen.{h,cpp}` | Scrollable element-list base: status bar, page-dots, scrollbar, focus/scroll logic, input routing. |
| `Pages.{h,cpp}`         | Concrete screens (Splash, Home, Messages, Mesh, GPS, Bluetooth, Power) and their element getters/callbacks. |
| `UITask.{h,cpp}`        | `AbstractUITask` implementation: owns the pages, button dispatch, refresh timing, LED/buzzer, message intake, shutdown. |
| `icons.h`               | XBM bitmaps (logo, status-bar glyphs). |

### Chrome geometry (`ElementScreen`)
- Top status bar `STATUS_H = 13` px (battery + title).
- Bottom page-dot strip `DOTS_H = 3` px.
- Usable logical height capped at `USABLE_BOTTOM = 126` (the e-ink panel's
  128×128 logical surface maps onto a 200×200 physical panel; a couple of pixels
  are unusable margin).

## Building

Opt-in PlatformIO env (BLE-only for now), with `AUTO_OFF_MILLIS = 0` because the
display is e-ink:

```
pio run -e ThinkNode_M1_companion_radio_ble_elements
```

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

### Unicode glyphs via GNU Unifont

Today all text uses `FreeSans9pt7b` (ASCII `0x20–0x7E` only), and
`translateUTF8ToBlocks` (`DisplayDriver.h`) collapses every non-ASCII char to a
single `█`. So accents, symbols, and emoji can't render at all.

GNU Unifont (1-bit, 8×16 / 16×16 bitmaps) could replace this. Sizing vs the ~272 KB
free flash on the elements build:
- **Full BMP (incl. CJK/Hangul): won't fit** (~1.5–2 MB).
- **Western (Latin-1 + Ext-A + punctuation/symbols): ~10–20 KB** — fits easily.
- **+ Greek/Cyrillic: ~30–50 KB**; + curated emoji: ~+15–60 KB. Still fits.

Per-glyph: 8×16 = 16 B, 16×16 = 32 B, + ~6 B index — vs ~11 B/glyph for FreeSans.
Unifont is slightly taller (16 px vs ~13 px caps) and monospace (~25 chars/line vs
~18–21).

Two approaches: **(a)** keep FreeSans for ASCII and use Unifont only as a fallback
for the chars it lacks (smallest, preserves the current look); or **(b)** render the
whole UI — element text, status bar, icons, the `█` blocks — in Unifont monospace for
full coverage.

Implementation sketch: a host generator (`tools/gen_unifont.py`) filters the Unifont
`.hex` to the chosen ranges into a packed `unifont_glyphs.h` (sorted codepoint index +
1-bit blob); a UTF-8-aware blitter in `GxEPDDisplay` (behind a `UI_FONT_UNIFONT` flag,
so OLED variants are untouched) does per-codepoint lookup and blits glyphs at native
physical resolution. Caveats: emoji live in Plane 1 (`unifont_upper`, 4-byte UTF-8),
and Unifont emoji are crude **monochrome** 16×16 line-art — small icons, not color
emoji. A dynamic battery bar would also have to stay a drawn widget (a static 🔋 glyph
loses the charge level).
