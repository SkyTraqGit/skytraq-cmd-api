# GNSS Receiver Test Plan (sample)

This file is consumed by `real_tests.py`. Anything outside ` ```test ` fenced
code blocks is ignored, so feel free to use this file as documentation too.

Run it with:

    python3 real_tests.py test_plan.md -p COM3
    python3 real_tests.py test_plan.md -p /dev/ttyUSB0 -b 115200

For a parse-only check that doesn't touch hardware:

    python3 real_tests.py test_plan.md --dry-run

## Test ordering

The plan runs **writes first, then reads**. Every Set / Configure command
goes out before any Query, so the later queries can confirm what was
just written:

  1. Section 1: Set / Configure tests (SRAM only - power-cycle restores)
  2. Section 2: Negative test (deliberate NACK)
  3. Section 3: Read-only queries that double as write verification
  4. Section 4: Streaming ephemeris dumps (read-only, hardware state independent)
  5. Section 5: `-o` save-to-file feature check
  6. Section 6: Help / `-h` output (no port required)

All writes use the Attributes byte = `0` (SRAM only). A power cycle
restores the previous configuration. Never use `1` here unless you
really do want to write to FLASH.

The longer `delay_ms` after each configure helps avoid back-to-back
command collisions on slow firmware.

---

## 1. Configuration writes (SRAM only)

These run first so the queries in section 3 can read back what we wrote.

### 1a. Simple single-byte configures

```test
name: Set Output Message Type = NMEA, SRAM only
type: configure
msgid: 0x09
fields: 1 0
delay_ms: 500
```

```test
name: Set Power Mode = Normal, SRAM only
type: configure
msgid: 0x0C
fields: 0 0
delay_ms: 500
```

```test
name: Set Position Update Rate = 1 Hz, SRAM only
type: configure
msgid: 0x0E
fields: 1 0
delay_ms: 500
```

```test
name: Set NMEA Talker ID = GN, SRAM only
type: configure
msgid: 0x4B
fields: 1 0
delay_ms: 500
```

### 1b. Mask configurations

Both DOP and Elev/CNR get a single non-default value written. The
queries in section 3 read these values back to verify.

```test
name: Set DOP Mask custom (0x2A)
type: configure
msgid: 0x2A
fields: 1 30 31 32 0
delay_ms: 500
```

```test
name: Set Elev/CNR Mask = 15 deg / 30 dBHz (0x2B)
type: configure
msgid: 0x2B
fields: 1 15 30 0
delay_ms: 500
```

### 1c. Position pinning

```test
name: Disable position pinning (0x39)
type: configure
msgid: 0x39
fields: 2 0
delay_ms: 500
```

```test
name: Set position pinning parameters (0x3B)
type: configure
msgid: 0x3B
fields: 2 10 8 45 500 0
delay_ms: 500
```

### 1d. 1PPS configuration (timing receivers only)

```test
name: Set 1PPS cable delay = 0 (0x45)
type: configure
msgid: 0x45
fields: 0 0
delay_ms: 500
```

```test
name: Set 1PPS Timing PVT mode @ Taipei (0x54)
type: configure
msgid: 0x54
fields: 0 2000 30 24.7848 121.0087 100 0
delay_ms: 500
```

```test
name: Set 1PPS Output Mode (0x55)
type: configure
msgid: 0x55
fields: 1 0 0
delay_ms: 500
```

### 1e. Constellation / SBAS configuration

```test
name: Configure SBAS (0x62/0x01)
type: configure
msgid: 0x62/0x01
fields: 1 1 8 1 3 7 0
delay_ms: 500
```

```test
name: Configure QZSS (0x62/0x03)
type: configure
msgid: 0x62/0x03
fields: 1 3 0
delay_ms: 500
```

The next two test the same `0x62/0x05` message in its 27-byte and 30-byte
forms. Pass/Fail of the 30-byte form reveals firmware SouthPAN PRN support:
old firmware NACKs the 30-byte form (= FAIL because of `nack_is: fail`),
new firmware ACKs both (= PASS / PASS).

```test
name: Configure SBAS Advanced without SouthPAN (27-byte form)
type: configure
msgid: 0x62/0x05
fields: 1 2 8 1 2 127 838587h 7b8800h 890000h 7f8084h 7d8c8dh 828f90h 0
delay_ms: 500
```

```test
name: Configure SBAS Advanced with SouthPAN (30-byte form)
type: configure
msgid: 0x62/0x05
fields: 1 2 8 1 2 127 838587h 7b8800h 890000h 7f8084h 7d8c8dh 828f90h 7a0000 0
nack_is: fail
delay_ms: 500
```

### 1f. 0x64-family sub-id configurations

```test
name: Configure Extended NMEA Intervals (0x64/0x02)
type: configure
msgid: 0x64/0x02
fields: 1 1 3 1 1 1 1 0 0 0 0 0 0
delay_ms: 500
```

```test
name: Configure Interference Detection = OFF (0x64/0x06)
type: configure
msgid: 0x64/0x06
fields: 0 0
delay_ms: 500
```

```test
name: Configure Position Fix Nav Mask = 3D/3D (0x64/0x11)
type: configure
msgid: 0x64/0x11
fields: 0 0 0
delay_ms: 500
```

---

## 2. Negative test (NACK is the PASS verdict)

A position update rate of 99 Hz is invalid. We expect the receiver to
NACK; setting `nack_is: pass` makes that the success case. This test
should leave the receiver in the same state as section 1's
`Set Position Update Rate = 1 Hz` because the receiver should reject
the bad value without applying it.

```test
name: Invalid Position Rate must NACK
type: configure
msgid: 0x0E
fields: 99 0
nack_is: pass
expect: NACK received
delay_ms: 500
```

---

## 3. Read-only queries (verify what we wrote)

If any of these time out, the cable / port / baud rate is wrong (or
the writes never actually went through). The interesting thing in
this section is that, with the writes from section 1 having landed,
each query's response should now reflect those values.

### 3a. Sanity / version

```test
name: Query Software Version
type: query
msgid: 0x02
fields: 0
delay_ms: 100
```

```test
name: Query Software CRC
type: query
msgid: 0x03
fields: 1
delay_ms: 100
```

### 3b. Confirm the simple writes from section 1a

```test
name: Query Position Update Rate (should be 1 Hz)
type: query
msgid: 0x10
delay_ms: 100
```

```test
name: Query Power Mode (should be Normal)
type: query
msgid: 0x15
delay_ms: 100
```

```test
name: Query Output Message Type (should be NMEA)
type: query
msgid: 0x16
delay_ms: 100
```

```test
name: Query NMEA Talker ID (should be GN)
type: query
msgid: 0x4F
delay_ms: 100
```

### 3c. Confirm masks from section 1b (custom values)

```test
name: Query DOP Mask (should be 30/31/32)
type: query
msgid: 0x2E
delay_ms: 100
```

```test
name: Query Elevation/CNR Mask (should be 15 / 30)
type: query
msgid: 0x2F
delay_ms: 100
```

### 3d. 0x64-family read-only queries

```test
name: Query Boot Status
type: query
msgid: 0x64/0x01
delay_ms: 100
```

```test
name: Query GNSS Navigation Mode
type: query
msgid: 100/24
delay_ms: 100
```

```test
name: Query GPS Time
type: query
msgid: 0x64/0x20
delay_ms: 100
```

```test
name: Query Datum Index
type: query
msgid: 0x64/0x28
delay_ms: 100
```

---

## 4. Streaming responses (repeat) - ephemeris dumps

Several ephemeris-dump queries cause the receiver to emit one response
frame per satellite as a continuous burst. With `repeat: 0` we collect
every frame until the inter-frame timeout fires; with `outfile:` set,
all frames are appended to a single binary file.

| Query           | Response (per SV)  | Family                |
|-----------------|--------------------|-----------------------|
| `0x30`          | `0xB1`  (87 B)     | GPS                   |
| `0x5B`          | `0x90`  (43 B)     | GLONASS (slot 1..24)  |
| `0x67/0x02`     | `0x67/0x80` (87 or 126 B) | Beidou (MEO/IGSO or GEO) |
| `0x6E/0x02`     | `0x6E/0x80` (85 B) | Galileo               |
| `0x6F/0x04`     | `0x6F/0x81` (77 B) | IRNSS / NavIC         |

```test
name: Get all GPS ephemeris (0x30)
type: query
msgid: 0x30
fields: 0
repeat: 0
timeout_ms: 1000
outfile: gps_ephemeris.bin
expect: Collected [0-9]+ response
delay_ms: 200
```

```test
name: Get all GLONASS ephemeris (0x5B)
type: query
msgid: 0x5B
fields: 0
repeat: 0
timeout_ms: 1000
outfile: glo_ephemeris.bin
expect: Collected [0-9]+ response
delay_ms: 200
```

```test
name: Get all Beidou ephemeris (0x67/0x02)
type: query
msgid: 0x67/0x02
fields: 0
repeat: 0
timeout_ms: 1000
outfile: bds_ephemeris.bin
expect: Collected [0-9]+ response
delay_ms: 200
```

```test
name: Get all Galileo ephemeris (0x6E/0x02)
type: query
msgid: 0x6E/0x02
fields: 0
repeat: 0
timeout_ms: 1000
outfile: gal_ephemeris.bin
expect: Collected [0-9]+ response
delay_ms: 200
```

```test
name: Get all IRNSS / NavIC ephemeris (0x6F/0x04)
type: query
msgid: 0x6F/0x04
fields: 0
repeat: 0
timeout_ms: 1000
outfile: irnss_ephemeris.bin
expect: Collected [0-9]+ response
delay_ms: 200
```

```test
name: Get a single GLONASS slot (slot 1)
type: query
msgid: 0x5B
fields: 1
repeat: 1
timeout_ms: 3000
delay_ms: 100
```

---

## 5. Saving raw responses to disk

Use `outfile:` to capture the raw response frame
(0xA0 0xA1 ... 0x0D 0x0A) as a binary file.

```test
name: Save Software Version raw frame
type: query
msgid: 0x02
fields: 0
outfile: ver_raw.bin
expect: saved [0-9]+ raw bytes
delay_ms: 200
```

---

## 6. Help output (no port required)

The `help` test type runs `gnss_tool -h` and verifies the field table
prints. Useful for sanity-checking the message database itself.

```test
name: Help for QUERY SOFTWARE VERSION
type: help
msgid: 0x02
expect: Software Type
```

```test
name: Help for CONFIGURE GNSS NAVIGATION MODE
type: help
msgid: 100/23
expect: Navigation mode
```
