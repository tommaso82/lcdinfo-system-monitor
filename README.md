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

Windows 7/8 GPU utilization may be unavailable. A compatible WinUSB driver binding is required; the legacy D2XX binding is not interchangeable. No driver or firmware is installed by this project.

See [protocol notes](docs/protocol.md) for independently established commands, firmware anomalies and safe workarounds. These notes are not a reproduction of the original manual.

## Status

Initial native port under development; Windows and physical-device validation are required before claiming compatibility. No original driver binaries, firmware, device serial numbers or personal logs are included.
