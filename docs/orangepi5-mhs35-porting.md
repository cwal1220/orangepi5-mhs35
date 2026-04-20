# Orange Pi 5 MHS35 SPI LCD Porting Log

Date: 2026-04-19
Target board: Orange Pi 5
Remote host: `orangepi@192.168.219.109`
Remote OS: Armbian 26.2.1
Kernel: `6.1.115-vendor-rk35xx`

## Goal

Port a Raspberry Pi-oriented 3.5 inch SPI touch LCD previously driven with `goodtft/LCD-show` `MHS35-show` to Orange Pi 5.

## Findings

- `MHS35-show` is Raspberry Pi-specific and directly edits `/boot/config.txt`, loads Raspberry Pi `dtoverlay=mhs35`, and expects Raspberry Pi packages such as `libraspberrypi-dev`.
- The current Orange Pi 5 image does not expose any `/dev/spidev*` nodes by default.
- The current kernel already contains the required generic drivers:
  - `fb_ili9486`
  - `ads7846`
- Decompiled `mhs35-overlay.dtb` from `goodtft/LCD-show` shows:
  - LCD controller: `ilitek,ili9486`
  - Touch controller: `ti,ads7846`
  - Default LCD settings: `rotate=90`, `fps=30`, `txbuflen=0x8000`
- Orange Pi 5 uses a 26-pin header, not the Raspberry Pi 40-pin layout.

## Orange Pi 5 header mapping used for this port

The header mapping was derived from the active device tree and the Orange Pi `wiringOP` pin tables.

- LCD SPI bus:
  - Physical pin 19: `SPI4_TXD`
  - Physical pin 21: `SPI4_RXD`
  - Physical pin 23: `SPI4_CLK`
  - Physical pin 24: `SPI4_CS1`
- GPIO lines used by the MHS35 module:
  - Physical pin 11: touch IRQ
  - Physical pin 18: LCD DC
  - Physical pin 22: LCD RESET
  - Physical pin 26: touch CS

## RK3588 GPIO mapping used in the overlay

- Physical pin 11 -> `GPIO4_B2`
- Physical pin 18 -> `GPIO1_D2`
- Physical pin 22 -> `GPIO2_D4`
- Physical pin 26 -> `GPIO1_A3`

## SPI routing decision

- Orange Pi 5 header SPI lines match `spi4m0`.
- Physical pin 24 aligns with `spi4m0-cs1`.
- Touch CS on physical pin 26 is not a hardware SPI chip-select pin on Orange Pi 5, so it is handled as a GPIO-backed software chip-select.

## Files created in this workspace

- `docs/orangepi5-mhs35-porting.md`
- `docs/orangepi5-mhs35-overlay.dts`

## Change log

### 2026-04-19

- Started manual port instead of using `MHS35-show` directly.
- Identified the original panel and touch controller from the Raspberry Pi overlay.
- Mapped Orange Pi 5 SPI and GPIO pins needed for the panel.
- Began writing an Orange Pi 5 specific overlay for `spi4`.
- Adjusted the overlay to a pure DTS form because `armbian-add-overlay` does not preprocess `#include` macros.
- Installed the overlay on the Orange Pi with `sudo armbian-add-overlay ~/orangepi5-mhs35-overlay.dts`.
- Confirmed `/boot/armbianEnv.txt` now contains `user_overlays=orangepi5-mhs35-overlay`.
- Rebooted and verified successful driver probe:
  - `/dev/fb1` created
  - `fb_ili9486` loaded
  - `ads7846` loaded
  - input device registered as `ADS7846 Touchscreen`
- Confirmed framebuffer geometry:
  - `fb1`: `480x320`, 16bpp
- Relevant `dmesg` lines after reboot:
  - `ads7846 spi4.0: touchscreen, irq 130`
  - `input: ADS7846 Touchscreen`
  - `graphics fb1: fb_ili9486 frame buffer, 480x320`
- Observed that the board can take a long time to become reachable again after reboot. Allow extra boot time before assuming failure.
- Added a simple userspace mirror utility temporarily during bring-up.
- Installed and enabled the mirror service temporarily on the Orange Pi during bring-up.
- Updated `/boot/armbianEnv.txt` to force a 1080p render source at boot:
  - `extraargs=cma=256M video=HDMI-A-1:1920x1080@60e`
- Rebooted and verified after boot:
  - `fb0`: `1920x1080`, 32bpp
  - `fb1`: `480x320`, 16bpp
- Relevant boot log lines after the 1080p change:
  - `forcing HDMI-A-1 connector on`
  - `Update mode to 1920x1080i60`
  - `Update mode to 1920x1080p60`
- temporary mirror path was validated and later removed

## Direct UI mode

- Removed the temporary mirror path and temporary custom LCD UI path from the workspace.
- Added a direct launcher for `openpilot` UI on `/dev/fb1`:
  - `docs/orangepi5-run-openpilot-lcd.sh`
- The launcher uses:
  - `QT_QPA_PLATFORM=linuxfb:fb=/dev/fb1`
  - `QT_QPA_GENERIC_PLUGINS=evdevtouch:/dev/input/by-path/platform-fecb0000.spi-cs-0-event`
  - `QT_QUICK_BACKEND=software`
  - `LIBGL_ALWAYS_SOFTWARE=1`
- Manual smoke test result:
  - `~/openpilot/selfdrive/ui/ui` started successfully on `fb1`
  - the process was then intentionally terminated after validation
  - no systemd service is required for this mode
- Remote cleanup performed:
  - removed temporary mirror/custom UI service files and binaries
  - removed temporary source/service files from the Orange Pi home directory
- Current runtime state on the Orange Pi:
  - `openpilot` UI is started manually via the launcher script
  - process tree shows `bash -> _ui`
  - `gdm3` was stopped so that only the LCD UI remains visible
- Found a UI scaling issue on the `480x320` LCD:
  - `setMainWindow()` still used the default `1920x1080` layout on Orange Pi
  - the result was clipped or visually broken on the small framebuffer
- Patched `openpilot/selfdrive/ui/qt/qt_window.cc` so that, when `SCALE` is not explicitly set, the UI automatically scales down to fit smaller screens such as `/dev/fb1`.
- Rebuilt `selfdrive/ui/_ui` on the Orange Pi with:
  - `cd ~/openpilot && scons -Q --debug=explain -j8 selfdrive/ui/_ui`
- Verified the rebuild reason included:
  - `selfdrive/ui/qt/qt_window.cc changed`
  - `selfdrive/ui/_ui` relinked after `qt_window.o` and `libqt_widgets.a` were rebuilt
- Reverted the temporary `openpilot` UI scaling patch after confirming it did not solve the clipping well enough on the 3.5 inch panel.
- Rebuilt `selfdrive/ui/_ui` again on the Orange Pi after the revert.
- Rechecked the LCD framebuffer capabilities and confirmed that `fb1` exposes only a single fixed mode:
  - `geometry 480 320 480 320 16`
  - `virtual_size 480,320`
  - `modes U:480x320p-0`
- Conclusion from the framebuffer probe:
  - the SPI LCD is natively fixed at `480x320`
  - this setup can change rotation, userspace scaling strategy, or what content is drawn
  - it cannot become a true higher-resolution panel through a simple mode switch
- Added a small-screen rendering path for `openpilot` UI:
  - the top-level Qt window now uses the actual screen size when the display is smaller than `1920x1080`
  - NanoVG onroad rendering keeps a logical `1920x1080` coordinate space and scales it down to the physical LCD size
  - touch coordinates on the onroad and driver-view paths are mapped back into the original `1920x1080` design coordinate space
- Rebuilt `selfdrive/ui/_ui` on the Orange Pi after the small-screen scaling change.
- Relaunched the UI in a persistent `tmux` session:
  - session name: `lcd_ui`
  - verified runtime process: `./_ui`
- Reboot validation:
  - issued a remote reboot and waited for SSH recovery
  - SSH port reopened about 51 seconds later on this boot
  - after reconnect, relaunched the UI in `tmux` and verified `./_ui` was running again
- Adjusted the camera transform for small screens:
  - `CameraViewWidget::updateFrameMat()` now uses the original logical `1920x1080` design size when computing zoom on reduced LCD sizes
  - this specifically reduces the over-zoomed road camera view seen on the `480x320` panel
- Rebuilt `_ui` again after the camera transform change and relaunched it in `lcd_ui`.

## Verification commands

Run these on the Orange Pi after boot:

```bash
cat /boot/armbianEnv.txt
ls -l /dev/fb*
lsmod | egrep 'fb_ili9486|ads7846'
dmesg | egrep -i 'ili9486|ads7846|fb1'
fbset -fb /dev/fb1 -i
grep -n -A6 -B1 'ADS7846' /proc/bus/input/devices
```

## Current result

- Manual Orange Pi 5 overlay port is working at the kernel-driver level.
- The SPI LCD is exposed as framebuffer `fb1`.
- The resistive touch controller is exposed as `ADS7846 Touchscreen`.
- Display mirroring, Qt/X11 touch calibration, and user-facing app startup behavior are separate follow-up steps if needed.

## Resolution note

- The MHS35 panel is physically `480x320`.
- It cannot become a true native `1920x1080` panel.
- What can be configured instead is:
  - system rendering or primary display mode at `1920x1080`
  - scaled mirroring from that source onto the `480x320` SPI LCD
  - portrait rotation if the panel mounting requires `320x480`

## Follow-up files

- `docs/orangepi5-run-openpilot-lcd.sh`

## 2026-04-19 Direct framebuffer check

- Captured `/dev/fb1` directly from the Orange Pi and converted the raw dump locally to inspect the LCD contents without relying on HDMI or screenshots.
- Verified that the current `linuxfb` Qt session is running with:
  - `QT_ENABLE_HIGHDPI_SCALING=1`
  - `QT_SCALE_FACTOR=0.25`
- Added temporary startup diagnostics in `selfdrive/ui/qt/qt_window.cc` and confirmed the runtime Qt view of the LCD:
  - `screen_size=1920x1280`
  - `window_size=1920x1080`
  - `dpr=0.25`
- This means Qt is already rendering the UI as a scaled logical 1080p surface on the `480x320` panel rather than treating the panel as a raw `480x320` desktop.
- The remaining problem is usability/layout on a very small LCD:
  - the stock openpilot/offroad UI is still designed for a much larger display
  - direct framebuffer captures show that the result is not practical on the 3.5-inch panel even though Qt scaling is active

## 2026-04-19 Revert and separate LCD UI

- Reverted the stock openpilot UI source changes that had been added for LCD scaling experiments.
- Rebuilt the stock `selfdrive/ui/_ui` on the Orange Pi after that revert so the default app path is back to the original layout logic.
- Added a separate LCD-only Qt program instead of modifying the stock UI:
  - source: `openpilot/selfdrive/ui/lcd_compact.cc`
  - binary: `selfdrive/ui/_lcd_compact`
- Added a manual launcher script:
  - local reference copy: `docs/orangepi5-run-lcd-compact.sh`
  - remote copy: `~/orangepi5-run-lcd-compact.sh`
- Verified the new LCD app by:
  - launching it in `tmux` session `lcd_compact`
  - confirming runtime process `./_lcd_compact`
  - capturing `/dev/fb1` directly and checking the framebuffer image
- The separate compact UI is designed directly for the `480x320` SPI LCD instead of trying to force the stock 1080p-oriented UI onto the panel.

## 2026-04-19 Touch verification

- Confirmed the touch input node on the Orange Pi:
  - device: `ADS7846 Touchscreen`
  - event node: `/dev/input/event7`
  - by-path node: `/dev/input/by-path/platform-fecb0000.spi-cs-0-event`
- Ran two direct kernel-level checks with `evtest` while physically pressing the LCD:
  - first capture around `2026-04-19 11:57 KST`
  - second capture around `2026-04-19 11:58 KST`
- Both captures only showed the static device capability header and did not show any `BTN_TOUCH`, `ABS_X`, `ABS_Y`, or `ABS_PRESSURE` events.
- Current conclusion:
  - the LCD display path is working
  - the touch controller is enumerated
  - but actual touch interrupts/data are not reaching the Linux input event stream yet

## 2026-04-19 Deeper touch debugging

- Temporarily unbound `ads7846` and probed candidate GPIO lines directly while physically pressing and long-holding the panel.
- Narrow scans performed:
  - `gpiochip4` lines `8 9 10 11 12`
  - `gpiochip1` lines `4 5 6 7`
- Broad scan performed across many otherwise-unused input lines on:
  - `gpiochip1`
  - `gpiochip2`
  - `gpiochip4`
- Result of all GPIO scans:
  - no sampled candidate line changed state during touch
  - the previously assumed `pendown-gpio = <&gpio4 10 ...>` mapping is therefore not validated by measurement
- Also verified that `sudo gpioget` works normally on the board, so the lack of GPIO transitions is not explained by the userspace tool itself.
- Rebound `spi4.0` to `spidev` temporarily and created `/dev/spidev4.0`.
- Issued direct ADS7846/XPT2046-style SPI read commands for:
  - `X` (`0xD0`)
  - `Y` (`0x90`)
  - `Z1` (`0xB0`)
  - `Z2` (`0xC0`)
- Direct SPI probe result:
  - all raw responses were `[0, 0, 0]`
  - all decoded values remained `0`
- This means the touch path is not only missing Linux input events; in the current wiring/mapping, the touch controller is also not showing any meaningful SPI response.
- After the direct SPI probe, `spi4.0` was rebound back to the `ads7846` driver so the prior runtime state was restored.

## 2026-04-19 Reference check against `goodtft/LCD-show` `MHS35-show`

- Pulled `goodtft/LCD-show` and inspected the actual `MHS35-show` script.
- The script itself is Raspberry Pi-specific:
  - copies `usr/mhs35-overlay.dtb` into `/boot/overlays/`
  - writes Raspberry Pi `/boot/config.txt`
  - enables `dtparam=spi=on`
  - applies `dtoverlay=mhs35:rotate=90`
- Decompiled the original `usr/mhs35-overlay.dtb` on the Orange Pi with `dtc`.
- Relevant original overlay properties from `goodtft`:
  - LCD node:
    - `compatible = "ilitek,ili9486"`
    - `reg = <0>`
    - `spi-max-frequency = <115000000>`
    - `txbuflen = <32768>`
    - `rotate = <90>`
    - `fps = <30>`
    - `buswidth = <8>`
    - `regwidth = <16>`
    - `reset-gpios = <... 25 1>`
    - `dc-gpios = <... 24 0>`
  - Touch node:
    - `compatible = "ti,ads7846"`
    - `reg = <1>`
    - `spi-max-frequency = <2000000>`
    - `interrupts = <17 2>`
    - `pendown-gpio = <... 17 1>`
- Interpretation of the original Raspberry Pi design:
  - LCD control pins use BCM `24` and `25`
  - touch IRQ uses BCM `17`
  - touch is on the second SPI chip-select slot
- This matches the well-known Raspberry Pi 3.5-inch HAT wiring pattern:
  - physical pin `18` for `DC`
  - physical pin `22` for `RESET`
  - physical pin `11` for touch IRQ
  - physical pin `26` for touch CS
- Therefore the current Orange Pi port was already following the same physical-pin intent as `MHS35-show`.
- The unresolved problem is not lack of access to `MHS35-show`; it is that the Orange Pi 5 26-pin header/controller mapping is not a drop-in Raspberry Pi equivalent for the touch side.

## Updated touch conclusion

- Display bring-up remains successful.
- Touch bring-up is currently blocked below the Linux input layer.
- Current evidence points more strongly to one of the following:
  - wrong touch CS mapping
  - wrong touch IRQ mapping
  - physical header/pin incompatibility between the Raspberry Pi LCD module and the Orange Pi 5 26-pin header
  - touch-side wiring or module issue on the LCD itself
- In other words, this no longer looks like only an `ads7846` software tuning problem.

## 2026-04-19 Touch path narrowed down further

- Re-checked the original `goodtft` `mhs35-overlay.dtb` on the Orange Pi with `dtc`.
- Confirmed the Raspberry Pi reference design expects:
  - LCD `DC` on physical pin `18`
  - LCD `RESET` on physical pin `22`
  - touch IRQ on physical pin `11`
  - touch CS on physical pin `26`
- Verified Orange Pi 5 physical pin mapping again using `wiringOP` source tables:
  - physical pin `11` maps to GPIO number `138`
  - physical pin `18` maps to GPIO number `58`
  - physical pin `22` maps to GPIO number `92`
  - physical pin `26` maps to GPIO number `35`
- Temporarily rebound `spi4.0` to `spidev` and ran direct ADS7846 command reads.
- Direct SPI touch sampling showed real data while pressing the panel:
  - idle looked like `X=0, Y=4095, Z1~=2, Z2~=4082`
  - during press, `X/Y/Z` changed to real coordinates and pressure-related values
- This proves:
  - touch SPI chip-select is correct
  - the touch controller itself is responding
  - raw touch coordinates can be read successfully from userspace
- Exported GPIO `138` directly and polled its value during long presses.
- GPIO `138` changed exactly as a valid `PENIRQ` line should:
  - idle: `1`
  - pressed: `0`
  - release: back to `1`
- This proves the current `pendown/IRQ` physical line assumption is also correct.
- Therefore the remaining breakage is now very narrow:
  - `ads7846` kernel driver binds successfully
  - `/dev/input/event8` is created
  - but the driver never increments IRQ `130`
  - `evtest` still shows no `BTN_TOUCH` or `ABS_*` events while pressing
- Tried changing the device-tree interrupt type to level-low (`interrupts = <10 8>`).
- The live device tree after reboot showed the updated value was actually applied.
- Even with that change:
  - `/proc/interrupts` still reported IRQ `130` as `Edge ads7846`
  - interrupt count remained `0`
  - `evtest` still showed no touch events
- Current state after all of the above:
  - raw touch hardware path works
  - kernel `ads7846` input path does not
  - the blocker is now specifically the IRQ/driver integration on this Orange Pi kernel, not the panel wiring assumptions

## 2026-04-20 Userspace touch bridge experiment

- Added a userspace SPI-to-uinput bridge prototype:
  - local source: `docs/orangepi5-touch-uinput.py`
  - local launcher: `docs/orangepi5-run-touch-uinput.sh`
- The bridge does the following:
  - unbinds `ads7846`
  - binds `spi4.0` to `spidev`
  - polls raw ADS7846 coordinates from userspace
  - uses GPIO `138` as the raw pen-down signal
  - creates a virtual input device through `/dev/uinput`
- Reused the original `goodtft` MHS35 rotation-90 calibration reference:
  - `Calibration "3936 227 268 3880"`
  - `SwapAxes "1"`
- Verified on the Orange Pi that:
  - `/dev/uinput` exists
  - the userspace bridge starts successfully as root
  - a virtual input device is created:
    - `Name="OrangePi5 MHS35 Touch"`
    - `Handlers=mouse0 event8`
- Ran `evtest` against the virtual device while physically pressing the LCD.
- Result at this stage:
  - the virtual device exists
  - but the current bridge version did not yet emit visible `BTN_TOUCH` / `ABS_*` events during the verification run
- So the userspace path is partially working:
  - device creation succeeded
  - event injection still needs one more debugging pass

## 2026-04-20 Canonical `ads7846` path re-check

- Returned from the userspace experiment to the canonical kernel path.
- Reinstalled the Orange Pi overlay from `~/orangepi5-mhs35-overlay.dts` and rebooted.
- Confirmed after clean boot:
  - `ADS7846 Touchscreen` is present again in `/proc/bus/input/devices`
  - `spi4.0` is bound to the `ads7846` driver
  - IRQ `130` is assigned as `rockchip_gpio_irq 10 Edge ads7846`
  - the live device tree now shows `interrupts = <10 2>` again
  - the live device tree still shows active-low `pendown-gpio = <... 10 1>`
- Confirmed the GPIO line ownership at runtime through `/sys/kernel/debug/gpio`:
  - `gpio-138 (ads7846_pendown) in hi IRQ`
- Asked for a synchronized touch test and sampled both:
  - the runtime debug view of `gpio-138`
  - the `ads7846` IRQ counter in `/proc/interrupts`
- During that 20-second capture, the log stayed at:
  - `lvl=hi irq=0`
  - throughout the full sample window
- After the capture, IRQ `130` still remained at `0`.
- Interpretation of the current canonical state:
  - the original Raspberry Pi `MHS35` touch design has now been mirrored again on Orange Pi 5
  - the touch controller SPI path had already been proven working earlier
  - the penirq GPIO assumption had also been proven earlier by direct GPIO polling when the kernel driver was not owning the line
  - but with the normal `ads7846` driver bound, the Linux interrupt/input path still does not fire
- This shifts the remaining root-cause suspicion away from the original `goodtft` touch wiring model and toward:
  - interaction between this RK3588 vendor kernel's GPIO IRQ handling and `ads7846`
  - or a runtime behavior difference once the kernel driver owns the line/device

## 2026-04-20 Kernel-level narrowing: `PENIRQ` stays high with driver bound

- Collected additional kernel/runtime state from the live Armbian `6.1.115-vendor-rk35xx` system.
- Confirmed:
  - `CONFIG_TOUCHSCREEN_ADS7846=m`
  - `CONFIG_GPIO_ROCKCHIP=y`
  - `CONFIG_DYNAMIC_DEBUG=y`
  - `CONFIG_DEBUG_FS=y`
- Enabled dynamic debug for `drivers/gpio/gpio-rockchip.c` and captured kernel logs during touch attempts.
- Result:
  - no `rockchip_irq_demux` debug lines appeared at all while pressing the panel
  - IRQ `130` stayed at `0`
- This means the issue is not merely “`ads7846` got the IRQ but ignored it”; the GPIO IRQ demux path itself was not firing.
- Then sampled the runtime GPIO state of `gpio-138` while the `ads7846` driver was still bound.
- Used a 20-second capture at roughly 10 ms intervals and asked for two long presses.
- Result:
  - `gpio-138` stayed `hi` for the full capture
  - there were no `lvl=lo` samples at all
- This is materially different from the earlier raw/manual tests:
  - when the kernel driver was detached and the line was polled directly, `gpio-138` did go low on touch
  - when `spidev` was used, raw ADS7846 coordinates were readable
- So the problem is now narrowed further:
  - after the canonical `ads7846` driver binds, the expected `PENIRQ` low transition is no longer observable at runtime
  - therefore no Rockchip GPIO interrupt is generated
- The strongest current interpretation is:
  - this is not just a generic Rockchip IRQ routing failure
  - it is more specifically a runtime interaction between the bound `ads7846` driver and the controller/line state
  - one plausible class of failure is that the controller ends up left in a state where `PENIRQ` is not re-enabled after SPI transactions

## 2026-04-20 Driver-internal check: `pen_down` never changes

- Continued with the “instrument the canonical path” approach without switching back to a userspace touch path.
- Confirmed the running kernel exposes the `ads7846` internal sysfs state:
  - `/sys/devices/platform/fecb0000.spi/spi_master/spi4/spi4.0/pen_down`
  - `/sys/devices/platform/fecb0000.spi/spi_master/spi4/spi4.0/disable`
- Also confirmed `tracefs`, dynamic debug, and kprobes are available in this kernel.
- A lightweight kprobe attempt did not yet provide useful event lines, but the direct `pen_down` sysfs path did.
- Sampled the driver's own `pen_down` state for 20 seconds while pressing the panel multiple times.
- Result:
  - `pen_down` stayed `0` for the entire capture
  - there were no `pen_down=1` samples at all
- Combined with the earlier findings, the stack now looks like this:
  - raw ADS7846 SPI reads can work in userspace
  - manual GPIO polling can observe penirq transitions when the kernel driver is detached
  - but once the canonical `ads7846` driver is bound, the driver's own pen-down state never becomes active
  - consequently IRQ `130` never increments and no input events are reported
- Practical interpretation:
  - the failure point is now narrowed to the bound `ads7846` driver's runtime interaction with the controller/penirq line
  - the most plausible remaining class of root cause is that after binding/initial transaction flow, the controller is not left in a state that re-enables `PENIRQ`

## 2026-04-20 Debug module instrumentation

- Built a temporary out-of-tree `ads7846.ko` against the live kernel headers for:
  - `6.1.115-vendor-rk35xx`
- Used the exact Armbian kernel package metadata revision as the source reference:
  - git revision `34ab830bda234bd4d5b0e3a46451fe5f89e2cc35`
- Added only minimal debug instrumentation:
  - a new sysfs attribute `raw_pendown` that directly returns `get_pendown_state(ts)`
  - `dev_info()` logs around the probe-time “prime” transaction
- Probe-time logs from the debug module:
  - `probe-before-prime: raw_pendown=0 gpio_level=1 irq_flags=0x2002`
  - `probe-after-prime: raw_pendown=0 gpio_level=1 pendown=0`
- Then sampled both:
  - `/sys/.../raw_pendown`
  - `/sys/.../pen_down`
  for 20 seconds while pressing the panel repeatedly.
- Result:
  - `raw_pendown` stayed `0` throughout the full capture
  - `pen_down` also stayed `0` throughout the full capture
  - there were no `1` samples for either value
- This narrows the canonical failure further:
  - even the driver's direct `get_pendown_state()` view never becomes active after the driver is bound
  - therefore the problem is not limited to input reporting or IRQ thread wakeup
  - it is now specifically in the bound-driver view of the penirq/pindown signal path

## 2026-04-20 Related Armbian forum reference

- Reviewed the Armbian forum post:
  - `OrangePi Zero, mainline kernel, SPI LCD + touchscreen`
- Relevant technical points from that post:
  - the author used `tsc2046`/`ads7846`
  - the author explicitly reported poor cooperation between the LCD controller and touchscreen on a shared SPI bus
  - the workaround in that setup was to place the touchscreen on a separate `spi-gpio` bitbanged bus
  - the overlay there still used the normal `ads7846` DT pattern:
    - `interrupts = <... IRQ_TYPE_EDGE_FALLING>`
    - `pendown-gpio = <...>`
    - low touch SPI speed (`500000`)
- Implication for the current Orange Pi 5 investigation:
  - that post does not point to a different polarity or DT binding mistake in our current overlay
  - instead, it strengthens the suspicion that the problematic part is the shared-bus interaction itself
  - however, unlike the Orange Pi Zero example, the MHS35 module physically shares the touch and LCD SPI data lines on the display board, so the same “move touch to separate software SPI bus” workaround is not directly applicable without hardware rewiring

## 2026-04-20 Low-speed touch SPI retest (`500 kHz`)

- Followed the Armbian forum hint and reduced only the touch controller SPI speed:
  - from `2000000`
  - to `500000`
- Reinstalled the overlay and rebooted.
- Confirmed from the live device tree:
  - `spi-max-frequency = 0x0007a120` (`500000`)
- Re-loaded the debug `ads7846.ko` and re-checked probe-time logs.
- Result remained unchanged:
  - `probe-before-prime: raw_pendown=0 gpio_level=1 irq_flags=0x2002`
  - `probe-after-prime: raw_pendown=0 gpio_level=1 pendown=0`
- Then repeated the synchronized press test while sampling:
  - `raw_pendown`
  - `pen_down`
- Result:
  - both stayed `0` throughout the full capture
  - there were no observed `1` transitions
- Conclusion from the low-speed retest:
  - lowering the touch SPI bus from `2 MHz` to `500 kHz` did not restore pen detection
  - the forum post's “use a much lower speed on touchscreen SPI” idea does not resolve this specific Orange Pi 5 + MHS35 canonical-driver failure

## 2026-04-20 GPIO re-check

- Re-checked the pendown GPIO path from scratch after the `tsc2046 + 500 kHz` tests.
- First captured `gpio-138` while the debug `ads7846` driver was bound:
  - `gpio-138 (ads7846_pendown) in hi IRQ`
  - sampled for 20 seconds at roughly 10 ms intervals during long presses
  - result: no `lo` samples, line stayed high throughout
- Then removed `ads7846`, exported GPIO `138` directly through sysfs, and repeated the same style of capture:
  - sampled for 20 seconds at roughly 10 ms intervals during long presses
  - result: still no `0` samples, line stayed at `1` throughout
- So the earlier historical observation of a visible `1 -> 0 -> 1` transition on GPIO `138` could not be reproduced in this later re-check.
- Updated interpretation:
  - current reproducible evidence shows no pendown-level transition on GPIO `138`, regardless of whether the kernel driver is bound
  - this weakens the earlier “bound vs unbound difference on the GPIO line itself” hypothesis
  - the consistently reproducible facts remain:
    - raw SPI touch responses were observed in earlier direct tests
    - canonical driver path still never sees pendown

## 2026-04-20 Physical pin 11 mapping re-check

- Re-checked the Orange Pi 5 26-pin header mapping from scratch.
- In the local `wiringOP` source for `PI_MODEL_5`:
  - `physToGpio_5[11] = 138`
  - `physNames_5[11] = "CAN1_RX"`
- So `wiringOP` still maps Raspberry-style physical pin `11` to Linux GPIO number `138`.
- Cross-check from the live Orange Pi runtime:
  - `gpio-138` is represented as `pin 138 (gpio4-10)` in `/sys/kernel/debug/pinctrl`
  - our overlay binds the touch pendown line to exactly `gpio4-10`
- Conclusion from this re-check:
  - the current software mapping of physical pin `11 -> gpio138 -> gpio4-10` is internally consistent
  - the remaining uncertainty is no longer “did we mistype the Linux GPIO number for physical pin 11?”
  - instead it is whether the display module is actually presenting touch IRQ on that expected physical header pin in the plugged-in mechanical configuration

## 2026-04-20 26-pin header IRQ candidate scan

- Performed a direct scan of the accessible 26-pin GPIO candidates with the touch driver removed.
- Sampled these GPIO numbers in parallel during long presses:
  - `47 46 54 131 132 138 29 139 28 59 58 49 48 92 50 52 35`
- Result:
  - no GPIO changed state during the capture
  - `131` and `132` stayed at `0`
  - the other sampled lines stayed at `1`
  - there were no transitions at all
- Practical implication:
  - among the GPIOs reachable on the 26-pin header, none currently behaves like a touch IRQ output
  - this further shifts suspicion toward the module/header physical routing rather than the Linux GPIO number mapping itself
- After the scan, GPIO exports were cleaned up and the stock `ads7846` module was restored.

## 2026-04-20 Post-reseat clean-boot re-check

- After physically reseating/reconnecting the LCD module and rebooting, SSH access was restored and the Orange Pi was checked again from a clean boot state.
- Overlay state was present as expected:
  - `/boot/armbianEnv.txt` still contained `user_overlays=orangepi5-mhs35-overlay`
  - `/boot/overlay-user/orangepi5-mhs35-overlay.dtbo` existed
  - `/proc/device-tree/spi@fecb0000` contained both `ads7846@0` and `ili9486@1`
- `ads7846` was not auto-loaded immediately after boot, but after a manual `modprobe ads7846`:
  - `spi4.0` bound to `/sys/bus/spi/drivers/ads7846`
  - input device `ADS7846 Touchscreen` reappeared
  - GPIO debug showed `gpio-138 (ads7846_pendown)` again, in state `hi IRQ`
- A 20-second capture was then run while the user performed two long presses on the touchscreen.
- Captured fields per sample:
  - GPIO level of `gpio-138`
  - driver sysfs `pen_down`
  - `/proc/interrupts` count for `ads7846`
- Result from `/tmp/touchcap.log`:
  - 166 samples collected
  - `gpio=lo` count: `0`
  - `pen=1` count: `0`
  - IRQ count stayed `0` for all samples
- Tail of the log remained uniformly like:
  - `gpio=hi pen=0 irq=0`
- Practical interpretation:
  - after reseating and a fresh boot, the canonical driver path still sees no touch activity at all
  - the clean-boot state does restore the expected binding (`ads7846_pendown` on GPIO138), but touch presses still do not pull the line low and do not increment interrupts

## 2026-04-20 Post-reseat unbound GPIO + raw SPI split test

- From the same clean-boot state, `ads7846` was removed again to separate the GPIO pendown signal path from direct touch-controller SPI responses.
- Direct GPIO test with driver removed:
  - exported GPIO `138`
  - sampled `/sys/class/gpio/gpio138/value` for 20 seconds during two long presses
  - result: `200` samples, `zero_count = 0`, only state observed was `1`
  - tail of the log remained all `1`
- This means that in the current reproducible setup, the expected `TP_IRQ` line still does not visibly go low even with the kernel touch driver removed.
- Then `spi4.0` was temporarily rebound to `spidev` and read directly as an XPT2046/ADS7846-class controller at `500 kHz`.
- Raw SPI result after the physical reseat was strongly positive:
  - idle frames showed the familiar idle pattern like `X=0000 Y=4095 Z1=0000 Z2=4089`
  - while touching, multiple non-idle coordinate/pressure tuples were captured repeatedly, including examples such as:
    - `X=1356 Y=2579 Z1=0396 Z2=2905`
    - `X=1721 Y=2539 Z1=0458 Z2=2984`
    - `X=0961 Y=1746 Z1=0243 Z2=2506`
    - `X=2510 Y=1966 Z1=0628 Z2=2673`
    - `X=1791 Y=2955 Z1=0552 Z2=3165`
- Practical interpretation from this split test:
  - the touch controller is definitely alive on SPI and returns changing coordinates/pressure data under touch
  - but the touchscreen interrupt/pendown path is still not visible as a working GPIO low transition on the expected line
  - therefore the currently reproducible failure is specifically in the `TP_IRQ` / pendown signalling path, not in the touch ADC SPI data path itself
- After the raw test, the board was restored back to the normal kernel-driver state by unbinding `spidev` and reloading `ads7846`.

## 2026-04-20 Polling-based userspace touch path

- Started implementing a fallback touch path that does not depend on `TP_IRQ`.
- Reworked [orangepi5-touch-uinput.py](/Users/chan/Documents/Raspilot/docs/orangepi5-touch-uinput.py) from a GPIO-gated design into a pure SPI polling design.
- New behavior:
  - unbinds `ads7846` and temporarily binds `spi4.0` to `spidev`
  - polls `X/Y/Z1/Z2` directly at `500 kHz`
  - treats touch as active based on sampled raw SPI values rather than GPIO pendown
  - applies a small consecutive-sample filter before press/release events
  - emits `ABS_X`, `ABS_Y`, `ABS_PRESSURE`, and `BTN_TOUCH` through `uinput`
- Rationale:
  - reproducible testing shows the touch ADC data path works
  - reproducible testing does not show a working `TP_IRQ` pendown line
  - so a polling path is currently the most practical way to get working touch input on this module with Orange Pi 5

## 2026-04-20 `ads7846_poll` coordinate calibration

- Added a dedicated out-of-tree polling kernel driver at:
  - `/Users/chan/Documents/Raspilot/kernel_debug/ads7846_poll/ads7846_poll.c`
- The driver was built successfully against the live Orange Pi 5 kernel headers and bound directly to `spi4.0`.
- Initial kernel-level polling verification showed working touch events on `/dev/input/event8`, but they were still in raw 12-bit space.
- Added in-driver coordinate mapping for the MHS35 `rotate=90` orientation:
  - screen X follows raw Y
  - screen Y follows raw X reversed
- First calibrated pass produced event ranges approximately:
  - `X: 31..463`
  - `Y: 44..264`
- Then a raw SPI corner capture was used to tighten the default raw range further.
- Based on the captured corner medians, the driver defaults were adjusted from:
  - `raw x 227..3936`, `raw y 268..3880`
  to:
  - `raw x 280..3760`, `raw y 392..3886`
- After rebuilding and reloading the module, kernel log confirmed:
  - `ads7846_poll spi4.0: polling touchscreen, 10 ms period, 5 samples, z1 on/off 80/40, raw x 280-3760, raw y 392-3886, screen 480x320`
- Current practical state:
  - `spi4.0` is bound to `ads7846_poll`
  - touch events are generated by the kernel driver
  - coordinate scaling is now much closer to the LCD's `480x320` space, with remaining work focused only on fine calibration if needed

## 2026-04-20 Qt touch visualizer

- Added a standalone Qt touch debug app at:
  - `/Users/chan/Documents/Raspilot/openpilot/selfdrive/ui/qt/touch_debug.cc`
- Added a build target in:
  - `/Users/chan/Documents/Raspilot/openpilot/selfdrive/ui/SConscript`
  - target name: `selfdrive/ui/qt/touch_debug`
- Purpose:
  - render a full-screen touch test UI on the 3.5-inch LCD
  - show the latest touch position, press/release state, and recent tap markers directly on screen
  - make it obvious where the current kernel touch driver believes the screen is being pressed
- Built successfully on the Orange Pi 5 with the existing openpilot Qt toolchain.
- Added a small run helper:
  - `/Users/chan/Documents/Raspilot/docs/orangepi5-run-touch-debug.sh`
  - it launches the Qt app on `/dev/fb1` and points Qt at `/dev/input/event8`

## 2026-04-20 Qt calibration collector

- Added a dedicated Qt calibration UI at:
  - `/Users/chan/Documents/Raspilot/openpilot/selfdrive/ui/qt/touch_calibrate.cc`
- Added a build target:
  - `selfdrive/ui/qt/touch_calibrate`
- Purpose:
  - show five target points in sequence
  - collect the observed touch positions reported by the current kernel driver
  - save the captured target/observed pairs to `/tmp/touch_calibration_points.txt`
- Added a launcher helper:
  - `/Users/chan/Documents/Raspilot/docs/orangepi5-run-touch-calibrate.sh`
- Adjusted `ads7846_poll` coordinate mapping again after on-screen Qt validation showed the reported touch position was mirrored horizontally and vertically.
- New mapping direction:
  - screen X follows raw Y reversed
  - screen Y follows raw X (non-reversed)
- Fine-tuned the X-axis feel after on-screen testing showed touches on the right side were landing slightly too far right.
- Adjusted the `ads7846_poll` calibration by relaxing `raw_y_min` from `392` to `360` so the right side maps a little less aggressively.
- Right-edge touch still felt too far right after the previous tweak, so the X-axis mapping was compressed further by lowering `raw_y_min` again from `360` to `300`.
- Updated the Qt calibration collector so that each captured point is auto-saved immediately.
- This removes the need to press the on-screen SAVE button, which was difficult to hit while touch was still miscalibrated.
- Read `/tmp/touch_calibration_points.txt` collected from the Qt calibration UI and fit a simple linear correction on top of the first-pass screen mapping.
- Derived calibration from the five GUI samples:
  - `X' ~= 0.9701 * X + 5.81`
  - `Y' ~= 0.9304 * Y + 9.01`
- Implemented this as a second-stage affine correction in `ads7846_poll` using integer scale/offset terms.
