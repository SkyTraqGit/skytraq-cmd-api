/*
 * protocol.c - SkyTraq binary protocol packet build / parse / checksum
 */
#include "protocol.h"
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
  #include <sys/time.h>
  #include <time.h>
#endif

uint8_t proto_checksum(const uint8_t *payload, size_t len) {
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++) cs ^= payload[i];
    return cs;
}

int proto_encode(const uint8_t *payload, size_t payload_len,
                 uint8_t *out, size_t out_max) {
    if (payload_len == 0 || payload_len > PROTO_MAX_PAYLOAD) return -1;
    size_t need = 2 + 2 + payload_len + 1 + 2;
    if (need > out_max) return -1;

    size_t i = 0;
    out[i++] = PROTO_SOF1;
    out[i++] = PROTO_SOF2;
    out[i++] = (uint8_t)((payload_len >> 8) & 0xFF);
    out[i++] = (uint8_t)(payload_len & 0xFF);
    memcpy(out + i, payload, payload_len);
    i += payload_len;
    out[i++] = proto_checksum(payload, payload_len);
    out[i++] = PROTO_EOF1;
    out[i++] = PROTO_EOF2;
    return (int)i;
}

int proto_send_frame(serial_handle_t h, const uint8_t *frame, size_t len) {
    return serial_write(h, frame, len);
}

/* Read one byte with the remaining-time deadline. Returns 1 on success, 0 on timeout, -1 on error. */
static int read_one_byte(serial_handle_t h, int timeout_ms, uint8_t *out) {
    int n = serial_read_timeout(h, out, 1, timeout_ms);
    if (n < 0) return -1;
    if (n == 0) return 0;
    return 1;
}

int proto_read_frame(serial_handle_t h, int timeout_ms,
                     uint8_t *payload, size_t payload_max,
                     uint8_t *raw, size_t raw_max, size_t *raw_len) {
    /* We track total elapsed time across multiple per-byte reads.
     * Since serial_read_timeout uses an internal deadline, we approximate
     * by passing the remaining time on each call. */
#ifdef _WIN32
    DWORD t0 = GetTickCount();
    #define ELAPSED() ((int)(GetTickCount() - t0))
#else
    struct timeval t0; gettimeofday(&t0, NULL);
    #define ELAPSED() ({ struct timeval _t; gettimeofday(&_t, NULL); \
        (int)(((_t.tv_sec - t0.tv_sec) * 1000) + ((_t.tv_usec - t0.tv_usec) / 1000)); })
#endif

    size_t rlen = 0;
    /* Hunt for SOF1 0xA0 followed by SOF2 0xA1. */
    uint8_t b = 0, prev = 0;
    int seen_sof1 = 0;
    for (;;) {
        int rem = timeout_ms - ELAPSED();
        if (rem <= 0) return 0;
        int r = read_one_byte(h, rem, &b);
        if (r < 0) return -1;
        if (r == 0) return 0;
        if (seen_sof1 && b == PROTO_SOF2) {
            if (raw && rlen + 2 <= raw_max) { raw[rlen++] = PROTO_SOF1; raw[rlen++] = PROTO_SOF2; }
            break;
        }
        seen_sof1 = (b == PROTO_SOF1) ? 1 : 0;
        prev = b;
    }
    (void)prev;

    /* Read 2-byte payload length (big-endian). */
    uint8_t plh, pll;
    int rem = timeout_ms - ELAPSED();
    if (rem <= 0) return 0;
    if (read_one_byte(h, rem, &plh) <= 0) return 0;
    rem = timeout_ms - ELAPSED();
    if (rem <= 0) return 0;
    if (read_one_byte(h, rem, &pll) <= 0) return 0;
    if (raw && rlen + 2 <= raw_max) { raw[rlen++] = plh; raw[rlen++] = pll; }

    size_t plen = ((size_t)plh << 8) | pll;
    if (plen == 0 || plen > PROTO_MAX_PAYLOAD || plen > payload_max) {
        fprintf(stderr, "proto: bogus payload length %zu\n", plen);
        return -1;
    }

    /* Read payload bytes one at a time so we always honour the deadline. */
    for (size_t i = 0; i < plen; i++) {
        rem = timeout_ms - ELAPSED();
        if (rem <= 0) return 0;
        if (read_one_byte(h, rem, &payload[i]) <= 0) return 0;
        if (raw && rlen < raw_max) raw[rlen++] = payload[i];
    }

    /* Checksum + EOF. */
    uint8_t cs, eof1, eof2;
    rem = timeout_ms - ELAPSED();
    if (rem <= 0) return 0;
    if (read_one_byte(h, rem, &cs) <= 0) return 0;
    if (raw && rlen < raw_max) raw[rlen++] = cs;
    rem = timeout_ms - ELAPSED();
    if (rem <= 0) return 0;
    if (read_one_byte(h, rem, &eof1) <= 0) return 0;
    if (raw && rlen < raw_max) raw[rlen++] = eof1;
    rem = timeout_ms - ELAPSED();
    if (rem <= 0) return 0;
    if (read_one_byte(h, rem, &eof2) <= 0) return 0;
    if (raw && rlen < raw_max) raw[rlen++] = eof2;

    if (eof1 != PROTO_EOF1 || eof2 != PROTO_EOF2) {
        fprintf(stderr, "proto: bad EOF %02X %02X\n", eof1, eof2);
        return -1;
    }
    uint8_t calc = proto_checksum(payload, plen);
    if (calc != cs) {
        fprintf(stderr, "proto: checksum mismatch (got 0x%02X, expected 0x%02X)\n", cs, calc);
        return -1;
    }
    if (raw_len) *raw_len = rlen;
    return (int)plen;

#undef ELAPSED
}
