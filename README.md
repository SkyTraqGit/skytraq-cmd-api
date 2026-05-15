# Skytraq Command API — SkyTraq GNSS Console Utility

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Build](https://github.com/USERNAME/skytraq-cmd-api/actions/workflows/build.yml/badge.svg)](https://github.com/USERNAME/skytraq-cmd-api/actions/workflows/build.yml)
[![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-blue.svg)](#build)
[![Language](https://img.shields.io/badge/language-C99-00599C.svg)](#)

A small cross-platform (Linux + Windows MSYS2 UCRT64 / MinGW) command-line
tool for talking to SkyTraq GNSS receivers using the binary protocol
described in **Application Note AN0037 v1.4.69**.

It can:

* print the field table for any known message (`-h`)
* send a Set/Configure command and report ACK / NACK / Timeout (`-c`)
* send a Query command and pretty-print the decoded response (`-q`)
* save the raw response frame (from `0xA0 0xA1` to `0x0D 0x0A`) to a file (`-o`)
* run a batch of commands from a script file (`-i`)
* talk to any serial port at any integer baud rate (`-p`, `-b`)
* override the default 3000 ms response timeout (`-t`)

---

## Build

### Linux

```bash
make
```

Standard glibc on Linux is enough; arbitrary baud rates are supported via
the `BOTHER`/`termios2` ioctl.

### Windows (MSYS2 UCRT64)

From an MSYS2 UCRT64 shell:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc make
make
```

This produces `gnss_tool.exe`. The executable can be run from any normal
Windows command prompt (it has no DLL dependencies beyond what UCRT64 GCC
links statically by default - if you want a fully redistributable .exe,
add `LDFLAGS="-static"` to the make command).

### Windows (native MinGW-w64) and other Unixes

The same `make` line works. For a fully static Windows build:

```bash
make LDFLAGS="-static"
```

---

## Numeric format

Every numeric command-line argument (Message ID, Sub-ID, field values,
baud rate, timeout) accepts:

| form    | meaning           |
|---------|-------------------|
| `123`   | decimal           |
| `7Bh`   | hex (suffix `h`)  |
| `0x7B`  | hex (C-style)     |

For Sub-ID messages, write `ID/SubID`, e.g. `100/23`, `0x64/0x17`, `64h/17h`.

---

## Usage

### `-h` - show parameter list

```bash
gnss_tool -h 0x02
gnss_tool -h 100/23
gnss_tool -h 4Bh
```

When the message is a query, the response message's field table is printed
too.

### `-c` - Set / Configure

```bash
gnss_tool -p /dev/ttyUSB0 -c 0x09 1 1
                              ID  Type Attributes
```

Field values come in the same order as in the AN0037 field table, **starting
from field 2** (the Message ID is provided automatically). For sub-ID messages
you only give the user fields - the Sub-ID is filled in from the message
definition. Big-endian encoding is applied automatically for fields wider
than one byte.

The tool then waits up to 3000 ms (or `-t MS`) for an ACK or NACK.

### `-q` - Query

```bash
gnss_tool -p COM3 -b 115200 -q 0x02 0
```

Same field convention as `-c`. After ACK, the tool waits for the linked
response message, then prints each field with its decoded value, unit,
and type.

### `-o FILE` - save raw response

```bash
gnss_tool -p /dev/ttyUSB0 -o swver.bin -q 2 1
```

Writes the entire response frame (SOF + length + payload + CS + EOF) to
`swver.bin`. Decoded output still appears on stdout.

### `-i SCRIPT` - batch mode

Each line in the script is a `-c` or `-q` command without the leading
`gnss_tool` and without `-p / -b / -t / -o` (those are taken from the
outer command). Blank lines and lines beginning with `#` are ignored.

```text
# script.txt
-q 0x02 0
-c 0x09 1 1
-q 100/24 0
```

```bash
gnss_tool -p /dev/ttyUSB0 -i script.txt
```

### `-p`, `-b`, `-t`

* `-p PORT` - Linux: `/dev/ttyUSB0`, `/dev/ttyACM0`, etc. Windows: `COM3`,
  `COM10`, … (the `\\.\` prefix is added internally so high COM numbers work).
* `-b BAUD` - any integer baud rate. Default 115200.
* `-t MS`   - response timeout in ms. Default 3000.
* `-r N`    - number of response frames to collect after the ACK
  (default 1). Use `-r N` (N>1) when one query produces several responses;
  use `-r 0` to collect every response until the timeout fires
  ("streaming" mode). With `-o`, every captured frame is appended to the
  output file.

### Streaming responses (`-r 0`)

Some queries cause the receiver to emit several response frames in a
single burst. The classic example is **Get GLONASS Ephemeris (0x5B)**
with slot=0: the receiver dumps **all 24 GLONASS slots as 24 separate
0x90 frames**.

```bash
gnss_tool -p COM28 -b 115200 -t 6000 -r 0 -o glo_all.bin -q 0x5B 0
```

This sends 0x5B with slot=0, prints the ACK, then keeps reading until
no more frames arrive within `-t` ms of the previous one. Each frame
is decoded to stdout and the raw bytes are appended to `glo_all.bin`
so it ends up containing the whole burst in order.

---

## Coverage of the AN0037 message set

The built-in message database covers ~50 of the most commonly used messages:

* System control: System Restart (0x01), Set Factory Defaults (0x04),
  System Reboot (0x64/0x3F)
* Software info: Query Software Version (0x02) / CRC (0x03), and their
  responses (0x80, 0x81)
* Serial / output: Configure Serial Port (0x05), Configure / Query Message
  Type (0x09 / 0x16 → 0x8C)
* Position update rate: Configure / Query (0x0E / 0x10 → 0x86)
* Power: Configure / Query Power Mode (0x0C / 0x15 → 0xB9)
* Masks: DOP (0x2A / 0x2E → 0xAF), Elevation & CNR (0x2B / 0x2F → 0xB0)
* Position pinning: 0x39 / 0x3A → 0xB4, parameters 0x3B
* NMEA talker ID: 0x4B / 0x4F → 0x93
* SBAS / QZSS / SAEE: 0x62/{1..6,80,81,82}, 0x63/{1,2,80}
* 0x64 sub-IDs: boot status (0x01 → 0x80), navigation mode
  (0x17 / 0x18 → 0x8B), GPS time (0x20 → 0x8E), datum (0x27 / 0x28 → 0x92),
  version extension (0x7D → 0xFE)
* ACK (0x83), NACK (0x84)

Messages outside this list can still be **sent** via `-c` / `-q` - the tool
just transmits the bytes you pass on the command line (one byte per
argument) and dumps the response as hex. To get pretty-printing for a
new message, add an entry to `messages.c`. The pattern is straightforward;
copy any existing entry and change the field list.

---

## End-to-end test

`fake_gnss.py` is a Python pseudo-terminal-based fake receiver useful for
testing without real hardware. `run_tests.sh` exercises every command-line
mode against it.

```bash
./run_tests.sh
```

---

## File layout

```
gnss_tool.c   - CLI parsing, dispatch, encoding/decoding glue
protocol.c/h  - frame builder, checksum, frame reader (with resync)
serial.c/h    - cross-platform serial: Win32 DCB / POSIX termios / termios2
messages.c/h  - message database
Makefile      - builds for Linux and MSYS2/MinGW (auto-detects .exe)
fake_gnss.py  - PTY-based fake receiver (Linux/macOS only)
real_tests.py - Markdown-driven test runner for real hardware
test_plan.md  - test plan consumed by real_tests.py
run_tests.sh  - end-to-end test driver against fake_gnss.py
```

---

## Contributing

Bug reports, pull requests, and additions to the message database are very
welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a PR,
and note that this project follows the [Contributor Covenant](CODE_OF_CONDUCT.md).

For security issues, see [SECURITY.md](SECURITY.md) — please do not file
public issues for vulnerabilities.

---

## License

This project is licensed under the MIT License — see [LICENSE](LICENSE) for
the full text.

Copyright © 2026 SkyTraq Technology, Inc. SkyTraq is a trademark of
SkyTraq Technology, Inc.
