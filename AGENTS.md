# AGENTS.md

MeshCore. The active work in this checkout is the **ThinkNode M1 native e-ink UI**.

## Important branch

**`ui-thinknode-m1`** is the dev/trunk branch for the M1 native e-ink UI work (on the
`fork` remote). Do work here and merge side branches back into it. `main` is upstream
MeshCore and does **not** contain the M1 native UI.

## The gist of this branch

A companion-radio UI for the ThinkNode M1's **200×200 two-button e-ink** display: native
full-resolution rendering, real Unicode text via a built-in GNU Unifont subset, two-button
navigation (circle scrolls/selects, triangle pages/back), and a live per-packet **RX Log**.

The panel (GDEH0154D67 / SSD1681) is driven through **Meshtastic's pinned GxEPD2 fork**
using the `GxEPD2_154_D67` class. Every visible update uses the crisp **partial** waveform;
ghosting is cleared with a black→white **swing** rather than the OTP full-refresh LUT (that
LUT under-drives unchanged pixels and leaves salt-and-pepper speckle on this panel). See
`examples/companion_radio/ui-thinknode-m1/NativeEinkDisplay.{h,cpp}`.

Full UI overview: **`examples/companion_radio/ui-thinknode-m1/README.md`**.

## How to build & flash

The M1 native-UI firmware is the BLE companion env. From the repo root:

```sh
# build
pio run -e ThinkNode_M1_companion_radio_ble_unifontui

# flash over USB (DFU; upload_protocol = nrfutil, auto-resets the board into its bootloader)
pio run -e ThinkNode_M1_companion_radio_ble_unifontui -t upload
```

If the upload can't find the board, double-tap reset to force the nRF52 bootloader, then
re-run the upload. Build artifacts land in `.pio/build/ThinkNode_M1_companion_radio_ble_unifontui/`
(`firmware.zip` is the DFU package, `firmware.hex` the full image).

### Verifying a flash actually landed

**Do not trust the `[SUCCESS]` summary alone** — it can report success without programming
the board (e.g. when nothing is connected). Confirm two things:

1. **The board is connected first:** `ls /dev/cu.usbmodem*` should list a port (e.g.
   `/dev/cu.usbmodem111201`). No port = not plugged in; stop and ask the user to connect it.
2. **The upload log shows a real DFU:** look for `Forcing reset using 1200bps ... `,
   `Upgrading target ... with DFU package`, and **`Device programmed.`**. A genuine flash on
   this board takes **~38 seconds**; a suspiciously fast (~13s) "success" did **not** actually
   program. Don't pipe the upload through `tail -4` — grep for `Device programmed`/`%`/`error`
   or read the full output so these lines are visible.

Other M1 envs exist (`..._usb`, `..._repeater`, `..._room_server`), but the native e-ink UI
is **`ThinkNode_M1_companion_radio_ble_unifontui`**.
