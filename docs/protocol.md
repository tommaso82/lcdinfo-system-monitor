# Independently reverse-engineered LCDInfo USB protocol

## Identification and provenance

The device is a vintage LCDInfo USB display by Henri Skippari, using a Crystalfontz CFAG12864B-TMI-V (128x64, KS0108), ATmega16 and FT245BM. Original USB VID/PID is 0403:E300 and the product description is USB Display. The research started with no matching protocol documentation: historical LCDInfo USB plugins used a different command layout despite similar hardware. We reconstructed the commands experimentally, then located a historical specification and tested five more commands. The specification itself is not reproduced here.

All results refer to the tested unit, November 2026. Some conclusions are empirical models, not firmware disassembly. Hexadecimal bytes below are written without 0x unless stated otherwise.

## USB transport

WinUSB interface 0, alternate setting 0; bulk OUT 02 and bulk IN 81, 64-byte maximum packet size. Strip the first two FTDI status bytes from EACH USB IN packet (e.g. 31 60). Status-only packets are not firmware replies. Host transfer success is not a firmware ACK. The device-specific interface GUID is read from the Windows device registry; do not hardcode an individual serial number.

Send opcode and parameters together. Splitting them with delays can cause the firmware to treat data as opcodes. The prefix FE was unnecessary in all compared commands; its exact internal handling is unknown.

## Commands

| Opcode and arguments | Observed function |
|---|---|
| 01 | Model, 20 ASCII bytes: CFAG12864B + ten spaces |
| 02 | Width, one byte 80 |
| 03 | Height, one byte 40 |
| 04 | Clear display; next unpositioned graphics write observed at page 7, x64 |
| 05 pp | Page 0-7 |
| 06 xx | Column 0-127 |
| 07 dd | Write one vertical eight-pixel byte, advance column |
| 08 nn data | Write nn bytes; counts 0-255 accepted by parser, 128-byte aligned page writes verified |
| 09 data | Full 1024-byte frame; empirical 127-column stride defect |
| 0A cc | Internal character, 6-column step observed; wraps from x127 to x64 |
| 0B nn text | String; unreliable parser behavior after string, possible replay of old receive data |
| 0C vv | Temporary contrast |
| 0D vv | Temporary backlight |
| 0E vv | Persistent contrast, verified at power-on |
| 0F vv | Persistent backlight, verified at power-on |
| 10 | Read graphics byte; advances by two columns; x63 unreliable |
| 11 x y | Set pixel, x63 unreliable |
| 12 x y | Clear pixel, x63 unreliable |
| 13 c | Character column; observed x=floor(c/6), not multiplication |
| 14 r | Character row, equivalent to 05 for 0-7 |

No documented or verified readback of brightness/contrast, MCU EEPROM or firmware. Do not probe unknown commands blindly. No persistent settings are touched by the monitor.

## Graphics

Eight pages of 128 columns. Each byte describes eight vertical pixels, bit 0 at top. Frame offset = page*128 + x. A full page is sent as 05 pp 06 00 08 80 followed by 128 bytes. Never cross the page boundary in a block. Repeat for all eight pages. A safe end marker is 03 02 02 03, expected response 40 80 80 40. Only accept the new host framebuffer as synchronized after a valid response.

The native application keeps two 1024-byte buffers and sends changed column spans. Merge gaps of up to six unchanged bytes to avoid an extra six-byte command header; if the cost reaches a full page, send the full page. Unchanged frames require no USB traffic.

## Firmware quirks and evidence

- 09: in visual tests, payload index i mapped to x=i%127, page=(i/127)%8 (integer division). The last eight bytes overwrote page 0, x0-7. x127 was untouched on every page. The cursor at entry did not affect the tested frame. Use 08 instead.
- 07/08/0A: crossing x63/64 works in tested sequences; crossing x127 wraps to x64 on the same page. Explicit addressing avoids this.
- 10: read at explicitly selected position, advances by two columns. At x60-67, a written pattern 81 82 83 84 85 86 87 88 read back as 81 82 83 00 85 86 87 88. x63 can also return spurious values. A dummy read on the KS0108 and controller-selection boundary could explain this; not proven.
- 11/12: x63 corruption is consistent with defective read-modify-write. Full byte writes at x63 work. Maintain a host copy instead.
- 13: observed arguments 60,120,252 decimal select x10,20,42 respectively. Use 06 for direct columns.
- 0B: after 600 harmless 02 queries (all answered), 0B 04 41 42 43 44 displayed ABCD with a small leading mark. The next 03 returned 40 followed by 35 copies of 80 and another 40; the next 03 returned only 40. The same behavior was reported with FE. This supports replay of previous commands or another receive-buffer bug, but identical priming bytes cannot establish their exact origin or a universal replay length. Earlier experiments produced repeated text and apparent hangs, sometimes requiring a physical power cycle. Avoid 0B entirely, especially after persistent writes. Do not assume a 40-byte padding workaround is universally safe.
- 04: explicitly set page/column after clearing.
- 08: parser consumption verified for every count and all possible individual data bytes. This does not establish optical placement beyond page boundaries.

## Performance and recovery

Python-era host/USB benchmarks on the tested device: approximately 90 full frames/s via 08, 96/s in grouped batches; roughly 500 partial updates/s in a particular delta workload. These are not optical refresh rates and not native-app benchmarks. For a system monitor a low sampling rate is sufficient.

If the device stops responding, do not send arbitrary reset or programming commands. Windows PnP restart and logical USB port cycling did not restore the tested stuck state. A physical USB power cycle did. The application only uses identification queries and explicitly positioned graphic writes; it cannot repair a stuck firmware parser.
