# Orange Pi 5 MHS35

Bring-up repository for the GoodTFT/Waveshare-style `MHS-3.5inch RPi Display`
on Orange Pi 5.

This repo now keeps only the Orange Pi specific LCD and touch pieces:

- Orange Pi 5 display overlay for the ILI9486 SPI LCD
- polling kernel driver for the XPT2046/ADS7846 touch controller
- standalone Qt touch debug and calibration tools
- helper scripts for building, loading, and auto-starting the touch driver
- framebuffer benchmark and porting notes

The actual product UI is intended to live in the `openpilot` repository.

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
  - helper scripts to build, load, and auto-start the driver and tools
- `systemd/`
  - reference unit for touch-driver autoload at boot
- `bench/`
  - direct framebuffer benchmark for checking full-screen update throughput
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

3. Build the Qt touch tools:

   ```bash
   ./scripts/build-qt-tools.sh
   ```

4. Launch the touch visualizer or calibration tool:

   ```bash
   ./scripts/run-touch-debug.sh
   ./scripts/run-touch-calibrate.sh
   ```

Optional: build and run the framebuffer benchmark that hammers `/dev/fb1` with
full-screen pattern changes and reports the achieved loop rate:

```bash
./scripts/build-fb-bench.sh
./scripts/run-fb-bench.sh
```

If the run scripts cannot auto-detect the correct device, override them:

```bash
FB_DEVICE=/dev/fb1 TOUCH_DEVICE=/dev/input/event8 ./scripts/run-touch-debug.sh
```

## Touch tools

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

On this Orange Pi 5 setup, the Qt `linuxfb` path often needs elevated access to
the active tty/framebuffer. The Qt launchers therefore default to re-execing
themselves through `sudo -E`. Set `TOUCH_UI_USE_SUDO=0` if you already launch
from a privileged tty or want to debug the non-root path explicitly.

`run-touch-debug.sh` and `run-touch-calibrate.sh` try to auto-load
`ads7846_poll` before they fail, but they still stop if no touchscreen event
node becomes available because those apps require active touch input.

By default the run scripts:

- use framebuffer `/dev/fb1`
- auto-detect the touchscreen input event node by name
- fall back to a `/dev/input/by-path/*spi*event` node if needed

## Kernel driver

Build the polling module:

```bash
./scripts/build-kernel-module.sh
```

Load and bind it to `spi4.0`:

```bash
sudo ./scripts/load-ads7846-poll.sh
```

`load-ads7846-poll.sh` prefers the locally built module from
`kernel/ads7846_poll/ads7846_poll.ko`, but it can also use an installed copy at
`/lib/modules/$(uname -r)/extra/ads7846_poll.ko`.

## Boot autoload

To make the touch driver come up automatically after boot:

```bash
./scripts/build-kernel-module.sh
sudo ./scripts/install-touch-autoload.sh
```

This installs:

- `ads7846_poll.ko` into `/lib/modules/$(uname -r)/extra/`
- a reusable loader at `/usr/local/sbin/orangepi5-load-ads7846-poll.sh`
- a systemd unit at `/etc/systemd/system/orangepi5-ads7846-poll.service`

It then enables and starts the service immediately.

You can inspect it with:

```bash
systemctl status orangepi5-ads7846-poll.service
```

The reference unit file shipped in this repo lives at:

```text
systemd/orangepi5-ads7846-poll.service
```

## Notes

- The LCD panel is fixed at `480x320` through the current `fb_ili9486` path.
- The polling driver is used because the stock IRQ-based `ads7846` path did not
  produce reliable touch events on this Orange Pi 5 setup.
- Porting history and experiment notes are kept in:

```text
docs/orangepi5-mhs35-porting.md
```
