# Orange Pi 5 MHS35

Standalone bring-up repository for the GoodTFT/Waveshare-style `MHS-3.5inch RPi Display`
on Orange Pi 5.

This repo keeps the Orange Pi specific pieces out of `openpilot`:

- display overlay for the ILI9486 SPI LCD
- polling kernel driver for the XPT2046/ADS7846 touch controller
- standalone Qt touch debug and calibration tools
- helper scripts for building and running the tools
- porting notes collected during bring-up

## Layout

- `overlay/`
  - Orange Pi 5 user overlay for the LCD + touch wiring
- `kernel/ads7846_poll/`
  - out-of-tree polling touchscreen driver
- `qt/touch_debug/`
  - standalone Qt app that shows where touches land
- `qt/touch_calibrate/`
  - standalone Qt app that collects target/observed calibration pairs
- `scripts/`
  - helper scripts to build, load, and run the tools
- `samples/`
  - archived reference code that was previously tried inside `openpilot`
- `docs/`
  - working notes and investigation history

## Prerequisites

Tested environment:

- Orange Pi 5
- Armbian `26.2.1`
- kernel `6.1.115-vendor-rk35xx`
- Qt 5 with `qmake`
- kernel headers installed for the running kernel

The LCD panel is brought up as `/dev/fb1`. Touch is expected to appear as an
input device named `ADS7846 Poll Touchscreen` after `ads7846_poll` is loaded.

## Quick start

1. Compile and install the overlay:

   ```bash
   dtc -@ -I dts -O dtb -o orangepi5-mhs35-overlay.dtbo overlay/orangepi5-mhs35-overlay.dts
   sudo install -D -m 0644 orangepi5-mhs35-overlay.dtbo /boot/overlay-user/orangepi5-mhs35-overlay.dtbo
   if grep -q '^user_overlays=' /boot/armbianEnv.txt; then
     sudo sed -i'' '/^user_overlays=/ s/$/ orangepi5-mhs35-overlay/' /boot/armbianEnv.txt
   else
     echo 'user_overlays=orangepi5-mhs35-overlay' | sudo tee -a /boot/armbianEnv.txt
   fi
   sudo reboot
   ```

2. After reboot, build and load the polling touch driver:

   ```bash
   ./scripts/build-kernel-module.sh
   sudo ./scripts/load-ads7846-poll.sh
   ```

3. Build the Qt tools and launch the touch visualizer:

   ```bash
   ./scripts/build-qt-tools.sh
   ./scripts/run-touch-debug.sh
   ```

If the run scripts cannot auto-detect the correct device, override them:

```bash
FB_DEVICE=/dev/fb1 TOUCH_DEVICE=/dev/input/event8 ./scripts/run-touch-debug.sh
```

## Qt tools

Build both Qt tools:

```bash
./scripts/build-qt-tools.sh
```

Run the touch visualizer on the SPI LCD:

```bash
./scripts/run-touch-debug.sh
```

Run the calibration collector:

```bash
./scripts/run-touch-calibrate.sh
```

By default the run scripts:

- use framebuffer `/dev/fb1`
- auto-detect the touchscreen input event node by name
- fall back to a `/dev/input/by-path/*spi*event` node if needed

Override them manually if needed:

```bash
FB_DEVICE=/dev/fb1 TOUCH_DEVICE=/dev/input/event8 ./scripts/run-touch-debug.sh
```

## Kernel driver

Build the polling module:

```bash
./scripts/build-kernel-module.sh
```

Load and bind it to `spi4.0`:

```bash
sudo ./scripts/load-ads7846-poll.sh
```

This driver exists because the touch controller answers on SPI, but the
`TP_IRQ`/pendown path does not behave reliably enough for the stock
`ads7846` interrupt-driven driver on this Orange Pi 5 setup.

`load-ads7846-poll.sh` unbinds the stock driver, binds `spi4.0` to
`ads7846_poll`, and then clears `driver_override` again so future rebinds are
not pinned accidentally.

## Overlay

The overlay source is in:

```text
overlay/orangepi5-mhs35-overlay.dts
```

Compile it with:

```bash
dtc -@ -I dts -O dtb -o orangepi5-mhs35-overlay.dtbo overlay/orangepi5-mhs35-overlay.dts
```

Then install it to `/boot/overlay-user/` and enable it from `/boot/armbianEnv.txt`.

The touch node also carries the current polling and calibration values used by
`ads7846_poll`, including:

- raw min/max ranges
- polling thresholds
- final X/Y calibration coefficients

That means recalibration can be done by editing the DTS instead of recompiling
the driver.

## Notes

The detailed bring-up log is in:

```text
docs/orangepi5-mhs35-porting.md
```
