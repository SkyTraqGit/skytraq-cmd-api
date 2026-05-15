#!/usr/bin/env python3
"""
fake_gnss.py - simulate a SkyTraq receiver on a PTY for end-to-end tests
"""
import os
import pty
import select
import struct
import sys
import time

def cs(payload: bytes) -> int:
    c = 0
    for b in payload:
        c ^= b
    return c

def frame(payload: bytes) -> bytes:
    pl = len(payload)
    return bytes([0xA0, 0xA1, (pl >> 8) & 0xFF, pl & 0xFF]) + payload + bytes([cs(payload), 0x0D, 0x0A])

def ack(req_id: int, sub_id: int = None) -> bytes:
    if sub_id is None:
        return frame(bytes([0x83, req_id]))
    return frame(bytes([0x83, req_id, sub_id]))

def nack(req_id: int, sub_id: int = None) -> bytes:
    if sub_id is None:
        return frame(bytes([0x84, req_id]))
    return frame(bytes([0x84, req_id, sub_id]))

def read_one_frame(fd):
    """Blocking-ish: read from fd until one full SkyTraq frame is parsed.
    Returns (payload bytes,) or None on timeout."""
    state = 'sof1'
    buf = bytearray()
    plen = 0
    payload = bytearray()
    deadline = time.time() + 5.0
    while time.time() < deadline:
        r,_,_ = select.select([fd], [], [], 0.1)
        if not r: continue
        try:
            chunk = os.read(fd, 256)
        except OSError:
            return None
        for b in chunk:
            if state == 'sof1':
                if b == 0xA0: state = 'sof2'
            elif state == 'sof2':
                if b == 0xA1: state = 'plh'
                else: state = 'sof1'
            elif state == 'plh':
                plen = b << 8; state = 'pll'
            elif state == 'pll':
                plen |= b
                payload = bytearray(); state = 'pl'
                if plen == 0: state = 'sof1'  # bogus
                left = plen
            elif state == 'pl':
                payload.append(b)
                left -= 1
                if left == 0: state = 'cs'
            elif state == 'cs':
                got_cs = b
                state = 'eof1'
            elif state == 'eof1':
                if b == 0x0D: state = 'eof2'
                else: state = 'sof1'
            elif state == 'eof2':
                if b == 0x0A and got_cs == cs(bytes(payload)):
                    return bytes(payload)
                state = 'sof1'
    return None

def respond(master_fd, payload: bytes):
    if not payload: return
    mid = payload[0]
    sid = payload[1] if len(payload) >= 2 else None
    print(f"[fake] got request: ID=0x{mid:02X}" + (f" SubID=0x{sid:02X}" if sid is not None else "") +
          f" payload={' '.join(f'{b:02X}' for b in payload)}", flush=True)

    if mid == 0x02:  # QUERY SOFTWARE VERSION
        os.write(master_fd, ack(0x02))
        # response 0x80 SOFTWARE VERSION
        body = bytes([0x80, 0x01,
                      0x00,0x01,0x01,0x01,
                      0x00,0x01,0x03,0x0E,
                      0x00,0x07,0x01,0x12])
        os.write(master_fd, frame(body))
    elif mid == 0x03:  # QUERY SOFTWARE CRC
        os.write(master_fd, ack(0x03))
        body = bytes([0x81, 0x01, 0x98, 0x76])
        os.write(master_fd, frame(body))
    elif mid == 0x10:  # QUERY POSITION UPDATE RATE
        os.write(master_fd, ack(0x10))
        os.write(master_fd, frame(bytes([0x86, 10])))    # 10 Hz
    elif mid == 0x16:  # QUERY MESSAGE TYPE
        os.write(master_fd, ack(0x16))
        os.write(master_fd, frame(bytes([0x8C, 1])))     # NMEA
    elif mid == 0x4F:  # QUERY NMEA TALKER ID
        os.write(master_fd, ack(0x4F))
        os.write(master_fd, frame(bytes([0x93, 0x01])))  # GN
    elif mid == 0x09:  # CONFIGURE MESSAGE TYPE - send back ACK
        os.write(master_fd, ack(0x09))
    elif mid == 0x0E:  # CONFIGURE POSITION RATE
        # let's NACK an out-of-range value, ACK otherwise
        if len(payload) >= 2 and payload[1] not in (1,2,4,5,8,10,20,25,40,50):
            os.write(master_fd, nack(0x0E))
        else:
            os.write(master_fd, ack(0x0E))
    elif mid == 0x4B:  # CONFIGURE NMEA TALKER ID
        os.write(master_fd, ack(0x4B))
    elif mid in (0x39, 0x3B,
                 0x45,         # 1PPS cable delay
                 0x54,         # 1PPS timing
                 0x55,         # 1PPS output mode
                 0x2A, 0x2B):  # masks
        os.write(master_fd, ack(mid))
    elif mid == 0x62 and sid in (0x01, 0x03, 0x05):
        os.write(master_fd, ack(0x62, sid))
    elif mid == 0x64 and sid in (0x02, 0x06, 0x11):
        os.write(master_fd, ack(0x64, sid))
    elif mid == 0x30:  # GET GPS EPHEMERIS
        # SV=0 -> dump 16 GPS SVs as 0xB1 (87-byte payload) frames.
        os.write(master_fd, ack(0x30))
        sv_req = payload[1] if len(payload) >= 2 else 0
        svs = range(1, 17) if sv_req == 0 else [sv_req]
        for sv in svs:
            body = bytes([0xB1,
                          (sv >> 8) & 0xFF, sv & 0xFF,    # SV ID (UINT16 BE)
                          0x00])                          # Reserved
            # 83 bytes of subframe blob with deterministic content.
            for k in range(83):
                body += bytes([(sv * 11 + k) & 0xFF])
            assert len(body) == 87
            os.write(master_fd, frame(body))
    elif mid == 0x5B:  # GET GLONASS EPHEMERIS
        # Slot=0 -> dump all 24 slots as 24 separate 0x90 frames.
        # Otherwise, dump just the requested slot.
        os.write(master_fd, ack(0x5B))
        slot_req = payload[1] if len(payload) >= 2 else 0
        slots = range(1, 25) if slot_req == 0 else [slot_req]
        for slot in slots:
            # 43-byte payload: ID, slot, K-number, then 4 x 10-byte ephemeris strings.
            k_signed = slot % 13 - 6              # in [-6..+6]
            k_byte   = k_signed & 0xFF            # two's complement in 1 byte
            body = bytes([0x90, slot & 0xFF, k_byte])
            # Fill the 40 ephemeris bytes with a deterministic pattern so we can verify.
            for k in range(40):
                body += bytes([(slot * 7 + k) & 0xFF])
            assert len(body) == 43
            os.write(master_fd, frame(body))
    elif mid == 0x67 and sid == 0x02:  # GET BEIDOU EPHEMERIS
        os.write(master_fd, ack(0x67, 0x02))
        sv_req = payload[2] if len(payload) >= 3 else 0
        svs = range(1, 11) if sv_req == 0 else [sv_req]
        for sv in svs:
            sat_type = 0 if sv <= 3 else 1   # first 3 are GEO, rest MEO/IGSO
            header = bytes([0x67, 0x80,
                            (sv >> 8) & 0xFF, sv & 0xFF,    # SV ID
                            sat_type,                       # Type
                            0x01])                          # Valid
            body_len = 126 if sat_type == 0 else 87
            body = header + bytes((sv * 13 + k) & 0xFF for k in range(body_len - 6))
            assert len(body) == body_len
            os.write(master_fd, frame(body))
    elif mid == 0x6E and sid == 0x02:  # GET GALILEO EPHEMERIS
        os.write(master_fd, ack(0x6E, 0x02))
        sv_req = payload[2] if len(payload) >= 3 else 0
        svs = range(1, 13) if sv_req == 0 else [sv_req]
        for sv in svs:
            body = bytes([0x6E, 0x80,
                          (sv >> 8) & 0xFF, sv & 0xFF,
                          0x01])                            # Valid
            body += bytes((sv * 17 + k) & 0xFF for k in range(80))
            assert len(body) == 85
            os.write(master_fd, frame(body))
    elif mid == 0x6F and sid == 0x04:  # GET IRNSS EPHEMERIS
        os.write(master_fd, ack(0x6F, 0x04))
        sv_req = payload[2] if len(payload) >= 3 else 0
        svs = range(1, 8) if sv_req == 0 else [sv_req]
        for sv in svs:
            body = bytes([0x6F, 0x81,
                          (sv >> 8) & 0xFF, sv & 0xFF,
                          0x01])
            body += bytes((sv * 19 + k) & 0xFF for k in range(72))
            assert len(body) == 77
            os.write(master_fd, frame(body))
    elif mid == 0x64 and sid == 0x18:  # QUERY GNSS NAVIGATION MODE
        os.write(master_fd, ack(0x64, 0x18))
        os.write(master_fd, frame(bytes([0x64, 0x8B, 2])))  # Car
    elif mid == 0x64 and sid == 0x17:  # CONFIGURE NAV MODE
        os.write(master_fd, ack(0x64, 0x17))
    elif mid == 0x64 and sid == 0x20:  # QUERY GPS TIME
        os.write(master_fd, ack(0x64, 0x20))
        body = bytes([0x64, 0x8E,
                      18,                       # default leap
                      0x09, 0x46,               # week 2374
                      0x00, 0x01, 0xE2, 0x40,   # TOW = 0x0001E240 = 123456 (1234.56 s)
                      18,                       # current leap
                      1])                       # validation = from SV
        os.write(master_fd, frame(body))
    else:
        # Unknown request - NACK it
        os.write(master_fd, nack(mid, sid))

def serve():
    master, slave = pty.openpty()
    pty_name = os.ttyname(slave)
    print(pty_name, flush=True)  # print path to stdout for caller to read
    sys.stdout.flush()
    try:
        while True:
            payload = read_one_frame(master)
            if payload is None:
                continue
            respond(master, payload)
    except KeyboardInterrupt:
        pass
    finally:
        os.close(master); os.close(slave)

if __name__ == '__main__':
    serve()
