# LCDInfo System Monitor

A lightweight native Windows system monitor for the vintage LCDInfo USB graphical LCD designed by Henri Skippari. This project began because the original software and exact firmware protocol were difficult to locate, and the available legacy plugins did not match our device. We independently reconstructed its USB protocol by observing actual transfers, responses and displayed pixels.

## Hardware

- Crystalfontz CFAG12864B-TMI-V, 128 × 64 monochrome graphical LCD, KS0108 controller family.
- ATmega16 microcontroller and FTDI FT245BM USB FIFO.
- Original USB VID:PID: **0403:E300**; product string: **USB Display**.
- Expected identification response: `CFAG12864B`, padded to 20 bytes.

The findings apply to our tested firmware revision; other units may differ. The original hardware and firmware are not distributed here. Credit for the original design goes to Henri Skippari / LCDInfo.

## Native monitor

Target: Windows 7 through 11 (x86 and x64), with native Win32 APIs and WinUSB. CPU, RAM, GPU (when native WDDM counters are available), network receive/transmit rates, uptime and clock. The Big Digits layout is rendered on the host into a 1024-byte monochrome framebuffer. The program is intended to start directly in the notification area, with a right-click Exit menu. No brightness, contrast or EEPROM changes are needed.

Distribution goal: one portable, statically linked executable per architecture, with embedded font and tray icon. No installer, external application DLLs or configuration files. Choose x86 or x64 for your system; the checksum is optional verification metadata. The Windows WinUSB driver binding remains a prerequisite.

Windows 7/8 GPU utilization may be unavailable. A compatible WinUSB driver binding is required; the legacy D2XX binding is not interchangeable. No driver or firmware is installed by this project.

## Experimental power management

This branch is a test build, not a release. On suspend/hibernate, clear the display and close USB. By default there are no backlight changes. Optional command-line argument `--backlight 180` (decimal 0–255) also turns off the backlight in RAM during suspend and restores the explicit value after resume; it does not change the startup brightness. No EEPROM writes or configuration files. Value 0 deliberately restores zero. Invalid arguments are rejected.

The worker stops sampling while suspended, waits for an interruptible two-second settling period after resume, reopens USB and rebuilds the sampler and framebuffer. Failed opens or backlight restoration retry using the existing three-second reconnect interval. The UI waits at most approximately one second for best-effort suspend handling; a busy or unresponsive USB device may not blank before sleep. A marker verifies the firmware parser, not actual light output. No additional thread, periodic polling, or steady-state USB traffic. The added synchronization checks have not been benchmarked. Physical S3/S4 and combined command sequences still require validation.

## Windows driver setup

The original FTDI driver must be replaced with a Microsoft WinUSB binding for this device. Follow the [Windows setup guide](docs/windows-setup.md) for hardware identification, recommended installation, checks and rollback. No firmware or EEPROM changes are necessary.

See [protocol notes](docs/protocol.md) for independently established commands, firmware anomalies and safe workarounds. These notes are not a reproduction of the original manual.

## Status

v1.1.0 retains 500 ms sampling and differential display updates, while reducing network enumeration and GPU counter overhead. On the tested Windows 10 six-logical-processor PC, diagnostic runs showed approximately 65% lower process CPU use (0.35% to 0.12%; different workloads and durations, not a controlled A/B benchmark). The production build contains no profiling or log output. Windows 7–11 remains the compatibility target; older systems are not physically verified. See [releases](https://github.com/tommaso82/lcdinfo-system-monitor/releases). No original driver binaries, firmware, device serial numbers or personal logs are included.
