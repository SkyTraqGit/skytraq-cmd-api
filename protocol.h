/*
 * protocol.h - SkyTraq binary protocol packet build / parse / checksum
 *
 *   <0xA0,0xA1><PL_hi><PL_lo><Message ID>[Sub ID][...payload...]<CS><0x0D,0x0A>
 *
 * CS = XOR over the payload (Message ID through last body byte, before CS).
 */
#ifndef GNSS_PROTOCOL_H
#define GNSS_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "serial.h"

#define PROTO_SOF1 0xA0
#define PROTO_SOF2 0xA1
#define PROTO_EOF1 0x0D
#define PROTO_EOF2 0x0A

#define PROTO_MAX_PAYLOAD 65535
/* SOF(2) + PL(2) + payload + CS(1) + EOF(2) */
#define PROTO_MAX_PACKET  (2 + 2 + PROTO_MAX_PAYLOAD + 1 + 2)

/* Compute the SkyTraq checksum (XOR of all payload bytes). */
uint8_t proto_checksum(const uint8_t *payload, size_t len);

/* Encode a full frame around a payload (including Message ID and any sub-IDs).
 * Writes to out[]; returns total frame length, or -1 if it doesn't fit. */
int proto_encode(const uint8_t *payload, size_t payload_len,
                 uint8_t *out, size_t out_max);

/* Read a full frame from the serial port with the given timeout (ms).
 * Performs byte-by-byte resync on 0xA0,0xA1, validates checksum and trailer.
 * Returns number of payload bytes copied to payload[] (>=1) on success.
 * Returns 0 on timeout, -1 on protocol error. The full raw frame (SOF..EOF)
 * is also copied to raw[] when raw is non-NULL; *raw_len is updated.
 */
int proto_read_frame(serial_handle_t h, int timeout_ms,
                     uint8_t *payload, size_t payload_max,
                     uint8_t *raw, size_t raw_max, size_t *raw_len);

/* Convenience: send one prebuilt frame. */
int proto_send_frame(serial_handle_t h, const uint8_t *frame, size_t len);

#endif
