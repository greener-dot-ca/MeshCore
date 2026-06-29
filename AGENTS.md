# AGENTS.md

MeshCore. The active work in this checkout is the **ThinkNode M1 native e-ink UI**.

## Important branch

**`ui-thinknode-m1`** is the dev/trunk branch for the M1 native e-ink UI work (on the
`fork` remote). Do work here and merge side branches back into it. `main` is upstream
MeshCore and does **not** contain the M1 native UI.

## The gist of this branch

A companion-radio UI for the ThinkNode M1's **200×200 two-button e-ink** display: native
full-resolution rendering, real Unicode text via a built-in GNU Unifont subset, two-button
navigation (triangle scrolls/selects, circle pages/back), and a live per-packet **RX Log**.

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
pio run -e ThinkNode_M1_companion_radio_ble

# flash over USB (DFU; upload_protocol = nrfutil, auto-resets the board into its bootloader)
pio run -e ThinkNode_M1_companion_radio_ble -t upload
```

If the upload can't find the board, double-tap reset to force the nRF52 bootloader, then
re-run the upload. Build artifacts land in `.pio/build/ThinkNode_M1_companion_radio_ble/`
(`firmware.zip` is the DFU package, `firmware.hex` the full image).

Other M1 envs exist (`..._legacy` = old upscaled UI, `..._usb`, `..._repeater`,
`..._room_server`), but the native e-ink UI is **`ThinkNode_M1_companion_radio_ble`**.
