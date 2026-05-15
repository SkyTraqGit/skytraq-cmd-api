# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-05-15

Initial public release.

### Added

- `gnss_tool` command-line utility for SkyTraq GNSS receivers, implementing
  the binary protocol described in **Application Note AN0037 v1.4.69**.
- Operation modes:
  - `-h` — print the field table for any known message (no port needed).
  - `-c` — send a Set/Configure command and report ACK / NACK / Timeout.
  - `-q` — send a Query command and pretty-print the decoded response.
  - `-i` — batch-execute commands from a script file.
  - `-o` — save the raw response frame to a file.
  - `-r` — collect N response frames after the ACK; `-r 0` streams until
    the timeout fires (useful for multi-frame replies such as 0x5B with
    slot=0, which returns 24 GLONASS slots).
- Cross-platform serial backend:
  - Linux: POSIX `termios`, with arbitrary baud rates via `termios2`/`BOTHER`
    when the kernel supports it.
  - Windows (MSYS2 UCRT64 / native MinGW): Win32 `CreateFile` + DCB +
    overlapped I/O. High COM numbers (`COM10`+) handled via the `\\.\` prefix.
- Numeric argument parsing accepts decimal, `0x`-prefixed hex, and `h`-suffixed
  hex. Sub-ID messages use the `ID/SubID` syntax (e.g. `100/23`, `0x64/0x17`).
- Built-in message database covering ~50 of the most commonly used AN0037
  messages, including system control, software/CRC version, serial/output
  configuration, position update rate, power mode, DOP and elevation/CNR
  masks, position pinning, NMEA talker ID, SBAS/QZSS/SAEE, the 0x64 sub-ID
  family (boot status, navigation mode, GPS time, datum, version extension),
  and ACK/NACK.
- Messages outside the curated database can still be sent byte-by-byte; the
  response is dumped as hex.
- Test harness:
  - `fake_gnss.py` — a PTY-based fake receiver for hardware-free testing on
    Linux and macOS.
  - `run_tests.sh` — end-to-end test driver exercising every CLI mode against
    the fake receiver.
  - `real_tests.py` + `test_plan.md` — Markdown-driven test runner for real
    hardware on Linux or Windows (MSYS2 UCRT64).

[1.0.0]: https://github.com/USERNAME/skytraq-cmd-api/releases/tag/v1.0.0
