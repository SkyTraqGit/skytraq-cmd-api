/*
 * gnss_tool.c - SkyTraq GNSS Receiver Console Tool (AN0037 v1.4.69)
 *
 * Cross-platform: Windows (MSYS2 UCRT64 / native MinGW) and Linux.
 *
 * Synopsis:
 *   gnss_tool -h MessageID
 *   gnss_tool -p PORT [-b BAUD] [-t MS] [-o FILE]  -c MessageID Field2 Field3 ...
 *   gnss_tool -p PORT [-b BAUD] [-t MS] [-o FILE]  -q MessageID Field2 Field3 ...
 *   gnss_tool -p PORT [-b BAUD] [-t MS] [-o FILE]  -i SCRIPT_FILE
 *
 * MessageID may be given as:
 *   decimal:           5
 *   hex with 'h':      4Bh
 *   C-style hex:       0x4B
 *   ID/SubID:          100/23   100/17h   0x64/0x17   64h/0x17
 *
 * Field values use the same numeric conventions (decimal default, trailing
 * 'h' or leading '0x' for hex). Their byte size is taken from the message's
 * field table; fields that are larger than 8 bits are encoded big-endian
 * (per AN0037).
 *
 * Files written with -o contain the complete raw response frame
 * (0xA0 0xA1 ... 0x0D 0x0A).
 *
 * Script files for -i are plain text; one command per line. Each line is
 * a -c or -q invocation without the leading "gnss_tool":
 *     -c 0x09 1 1
 *     -q 0x02 0
 * Blank lines and lines starting with '#' are ignored.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>

#include "serial.h"
#include "protocol.h"
#include "messages.h"

/* ----------- numeric parsing (decimal default, "h" suffix or "0x" => hex) - */
static int parse_uint(const char *s, unsigned long *out) {
    if (!s || !*s) return -1;
    char buf[64];
    size_t n = strlen(s);
    if (n >= sizeof(buf)) return -1;
    strcpy(buf, s);
    int hex = 0;
    if (n >= 2 && (buf[0]=='0') && (buf[1]=='x' || buf[1]=='X')) {
        hex = 1;
    } else if (n >= 1 && (buf[n-1]=='h' || buf[n-1]=='H')) {
        hex = 1;
        buf[n-1] = '\0';
    }
    char *end = NULL;
    unsigned long v = strtoul(buf, &end, hex ? 16 : 10);
    if (!end || *end != '\0') return -1;
    *out = v;
    return 0;
}

/* Parse a "MsgID" or "ID/SubID" string. */
static int parse_msg_id(const char *s, uint8_t *id, int *has_subid, uint8_t *subid) {
    char buf[64];
    if (strlen(s) >= sizeof(buf)) return -1;
    strcpy(buf, s);
    char *slash = strchr(buf, '/');
    *has_subid = 0;
    *subid = 0;
    if (slash) {
        *slash = '\0';
        unsigned long v;
        if (parse_uint(buf, &v) != 0 || v > 0xFF) return -1;
        *id = (uint8_t)v;
        if (parse_uint(slash + 1, &v) != 0 || v > 0xFF) return -1;
        *subid = (uint8_t)v;
        *has_subid = 1;
    } else {
        unsigned long v;
        if (parse_uint(buf, &v) != 0 || v > 0xFF) return -1;
        *id = (uint8_t)v;
    }
    return 0;
}

/* ----------- pretty hex dump --------------------------------------------- */
static void dump_hex(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        printf("%02X%c", p[i], ((i+1)%16==0 || i+1==n) ? '\n' : ' ');
    }
}

/* ----------- -h: print help/field list ----------------------------------- */
static int cmd_help(const char *idstr) {
    uint8_t id = 0, subid = 0;
    int hassub = 0;
    if (parse_msg_id(idstr, &id, &hassub, &subid) != 0) {
        fprintf(stderr, "Invalid message id: %s\n", idstr);
        return 1;
    }
    const msg_def_t *m = NULL;
    if (hassub) {
        m = msg_lookup(id, 1, subid);
    } else {
        /* Try no-subid form first; if not found, list all matching subids. */
        m = msg_lookup(id, 0, 0);
        if (!m) {
            int found_any = 0;
            for (int i = 0; ; i++) {
                const msg_def_t *e = msg_iter(i);
                if (!e) break;
                if (e->id != id) continue;
                if (!found_any) {
                    printf("Message ID 0x%02X (%u) — multiple sub-ID entries:\n",
                           id, id);
                    found_any = 1;
                }
                printf("  0x%02X/0x%02X  %s\n", e->id, e->sub_id, e->name);
            }
            if (found_any) return 0;
        }
    }
    if (!m) {
        if (hassub)
            fprintf(stderr, "Unknown message ID 0x%02X / Sub-ID 0x%02X\n", id, subid);
        else
            fprintf(stderr, "Unknown message ID 0x%02X\n", id);
        return 1;
    }

    /* Print header. */
    printf("=========================================================\n");
    if (m->has_subid)
        printf("  %s   (ID 0x%02X / Sub-ID 0x%02X  =  %u/%u)\n",
               m->name, m->id, m->sub_id, m->id, m->sub_id);
    else
        printf("  %s   (ID 0x%02X  =  %u)\n", m->name, m->id, m->id);
    printf("  %s\n", m->summary);
    const char *kind = m->kind==MSG_INPUT_SET ? "Input  - Set / Configure"
                     : m->kind==MSG_INPUT_QUERY ? "Input  - Query"
                     : "Output / Response";
    printf("  Kind: %s\n", kind);
    if (m->kind == MSG_INPUT_QUERY && m->resp_id >= 0) {
        if (m->resp_subid_used)
            printf("  Response: 0x%02X/0x%02X\n", m->resp_id, m->resp_subid);
        else
            printf("  Response: 0x%02X\n", m->resp_id);
    } else if (m->kind == MSG_INPUT_SET) {
        printf("  Response: ACK (0x83) on success, NACK (0x84) on failure\n");
    }
    printf("---------------------------------------------------------\n");
    printf("  %-7s %-32s %-7s %-12s %s\n", "Field", "Name", "Type", "Unit", "Description");
    printf("  %-7s %-32s %-7s %-12s %s\n", "-----", "----", "----", "----", "-----------");
    static const char *TNAME[] = { "UINT8","UINT16","UINT32","SINT8","SINT16","SINT32","SPFP","DPFP","BYTES" };
    for (int i = 0; i < m->n_fields; i++) {
        const msg_field_t *f = &m->fields[i];
        /* Insert the optional trailing field one slot before the final
         * (Attributes) entry so the help table reflects on-wire order. */
        if (m->optional_trailing_field && i == m->n_fields - 1) {
            const msg_field_t *of = m->optional_trailing_field;
            printf("  %-7s %-32s %-7s %-12s %s\n",
                   of->range, of->name, TNAME[of->type],
                   of->unit && *of->unit ? of->unit : "-",
                   of->desc ? of->desc : "");
        }
        printf("  %-7s %-32s %-7s %-12s %s\n",
               f->range, f->name, TNAME[f->type],
               f->unit && *f->unit ? f->unit : "-",
               f->desc ? f->desc : "");
    }
    if (m->optional_trailing_field) {
        printf("  (the line marked '(opt)' is only encoded when you supply\n"
               "   one extra value; omit it to send the shorter form.)\n");
    }
    printf("=========================================================\n");

    /* When a query, also describe the response fields if known. */
    if (m->kind == MSG_INPUT_QUERY && m->resp_id >= 0) {
        const msg_def_t *r = msg_lookup((uint8_t)m->resp_id,
                                         m->resp_subid_used,
                                         (uint8_t)m->resp_subid);
        if (r) {
            printf("\nResponse Message:  %s\n", r->name);
            printf("---------------------------------------------------------\n");
            printf("  %-7s %-32s %-7s %-12s %s\n", "Field", "Name", "Type", "Unit", "Description");
            for (int i = 0; i < r->n_fields; i++) {
                const msg_field_t *f = &r->fields[i];
                printf("  %-7s %-32s %-7s %-12s %s\n",
                       f->range, f->name, TNAME[f->type],
                       f->unit && *f->unit ? f->unit : "-",
                       f->desc ? f->desc : "");
            }
        }
    }
    return 0;
}

/* ----------- packet building --------------------------------------------- */

/* Parse a floating-point literal accepted by strtod. Returns 0 on success. */
static int parse_double(const char *s, double *out) {
    if (!s || !*s) return -1;
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) return -1;
    /* Allow trailing 'f' or 'F' (treat as float literal) */
    if (end && (*end == 'f' || *end == 'F')) end++;
    if (end && *end != '\0') return -1;
    *out = v;
    return 0;
}

/* Parse a multi-byte BYTES field value. Two accepted forms:
 *   "AB"           - exactly one byte, interpreted as decimal or
 *                    hex (with 'h' suffix or '0x' prefix). The user
 *                    must supply enough byte arguments separately when
 *                    the field is wider than 1 byte.
 *   "ABCDEFh"      - hex digit string, encodes ceil(N/2) bytes. The
 *                    most-significant nibbles come first; an odd number
 *                    of digits gets a leading 0 nibble. Useful for PRN
 *                    triplets like "838587h" -> 83 85 87. (BYTES field
 *                    only.)
 *   "0xABCDEF"     - same as above with C-style prefix.
 *
 * Output: writes up to `field_size` bytes into out[]; returns the number
 * of bytes actually written, or -1 on parse error. The caller verifies
 * the byte count matches the field width.
 */
static int parse_bytes_field(const char *s, uint8_t *out, int field_size) {
    if (!s || !*s) return -1;
    /* Detect hex form. */
    const char *digits = s;
    int hex = 0;
    char buf[256];
    size_t n = strlen(s);
    if (n + 1 > sizeof(buf)) return -1;
    if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        digits = s + 2;
        n -= 2;
        hex = 1;
    } else if (n >= 1 && (s[n-1] == 'h' || s[n-1] == 'H')) {
        memcpy(buf, s, n - 1);
        buf[n - 1] = '\0';
        digits = buf;
        n -= 1;
        hex = 1;
    } else {
        /* No explicit marker: if the token contains any hex letter (a-f/A-F)
         * AND the field is wider than 1 byte, treat as implicit hex. This
         * lets users write "7a0000" as a 3-byte BYTES value without needing
         * an "h" suffix. Pure-digit tokens still go through the decimal
         * path (single byte). */
        if (field_size > 1) {
            for (size_t i = 0; i < n; i++) {
                char c = s[i];
                if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                    hex = 1;
                    break;
                }
            }
        }
    }
    if (!hex) {
        /* Plain decimal: must fit one byte. */
        unsigned long v;
        if (parse_uint(s, &v) != 0 || v > 0xFF) return -1;
        if (field_size < 1) return -1;
        out[0] = (uint8_t)v;
        return 1;
    }
    /* Validate digits and pack into bytes (MSB first). */
    if (n == 0) return -1;
    for (size_t i = 0; i < n; i++) {
        if (!isxdigit((unsigned char)digits[i])) return -1;
    }
    int bytes = (int)((n + 1) / 2);  /* round up */
    if (bytes > field_size) {
        fprintf(stderr, "BYTES literal '%s' decodes to %d bytes but field expects %d\n",
                s, bytes, field_size);
        return -1;
    }
    /* If odd nibble count, the first byte takes the single leading nibble. */
    int idx = 0;
    size_t i = 0;
    if (n & 1) {
        char c = digits[0];
        unsigned int v = (c >= 'a') ? (c - 'a' + 10)
                       : (c >= 'A') ? (c - 'A' + 10)
                                    : (c - '0');
        out[idx++] = (uint8_t)v;
        i = 1;
    }
    for (; i + 1 < n; i += 2) {
        unsigned int hi, lo;
        char c1 = digits[i], c2 = digits[i+1];
        hi = (c1 >= 'a') ? (c1 - 'a' + 10) : (c1 >= 'A') ? (c1 - 'A' + 10) : (c1 - '0');
        lo = (c2 >= 'a') ? (c2 - 'a' + 10) : (c2 >= 'A') ? (c2 - 'A' + 10) : (c2 - '0');
        out[idx++] = (uint8_t)((hi << 4) | lo);
    }
    return idx;
}

/* Encode an integer value into the payload at offset, big-endian. */
static int put_field(uint8_t *payload, size_t *off, size_t cap,
                     field_type_t t, int size, unsigned long long val) {
    int sz = (t == FT_BYTES) ? size : field_type_size(t);
    if (*off + (size_t)sz > cap) return -1;
    for (int i = sz - 1; i >= 0; i--) {
        payload[*off + i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    *off += sz;
    return 0;
}

/* Encode raw bytes into payload at offset (used for BYTES fields). */
static int put_bytes(uint8_t *payload, size_t *off, size_t cap,
                     const uint8_t *src, int n) {
    if (*off + (size_t)n > cap) return -1;
    memcpy(payload + *off, src, (size_t)n);
    *off += (size_t)n;
    return 0;
}

/* Encode a single field value (text -> bytes) per its declared type. */
static int encode_one_field(const msg_field_t *f,
                            const char *text,
                            uint8_t *payload, size_t *off, size_t cap) {
    switch (f->type) {
        case FT_U8: case FT_U16: case FT_U32:
        case FT_S8: case FT_S16: case FT_S32: {
            unsigned long v;
            if (parse_uint(text, &v) != 0) {
                fprintf(stderr, "Invalid integer '%s' for field '%s'\n", text, f->name);
                return -1;
            }
            return put_field(payload, off, cap, f->type, f->size, v);
        }
        case FT_SPFP: {
            double dv;
            if (parse_double(text, &dv) != 0) {
                fprintf(stderr, "Invalid float '%s' for field '%s'\n", text, f->name);
                return -1;
            }
            float fv = (float)dv;
            uint32_t bits;
            memcpy(&bits, &fv, 4);
            return put_field(payload, off, cap, FT_U32, 0, bits);
        }
        case FT_DPFP: {
            double dv;
            if (parse_double(text, &dv) != 0) {
                fprintf(stderr, "Invalid float '%s' for field '%s'\n", text, f->name);
                return -1;
            }
            uint64_t bits;
            memcpy(&bits, &dv, 8);
            /* put_field uses unsigned long long, fine on all platforms. */
            int sz = 8;
            if (*off + (size_t)sz > cap) return -1;
            for (int i = sz - 1; i >= 0; i--) {
                payload[*off + i] = (uint8_t)(bits & 0xFF);
                bits >>= 8;
            }
            *off += sz;
            return 0;
        }
        case FT_BYTES: {
            uint8_t buf[256];
            int sz = f->size;
            if (sz <= 0 || sz > (int)sizeof(buf)) {
                fprintf(stderr, "BYTES field '%s' has invalid size %d\n", f->name, sz);
                return -1;
            }
            int got = parse_bytes_field(text, buf, sz);
            if (got < 0) {
                fprintf(stderr, "Invalid BYTES value '%s' for field '%s'\n", text, f->name);
                return -1;
            }
            if (got != sz) {
                fprintf(stderr, "Field '%s' wants %d byte(s), value '%s' supplied %d\n",
                        f->name, sz, text, got);
                return -1;
            }
            return put_bytes(payload, off, cap, buf, got);
        }
    }
    return -1;
}

/* Build a payload for a known message from CLI argv values.
 * argv[0] is the message ID string (skipped here; already parsed).
 * argv[1..] are field values starting from field #2 (since field #1 is ID).
 * For sub-id messages, the user may either supply or omit the sub-ID byte;
 * we always derive it from the message definition so they don't have to.
 */
static int build_payload_from_def(const msg_def_t *m,
                                  int n_user_fields,
                                  char **user_fields,
                                  uint8_t *payload, size_t cap, size_t *out_len) {
    size_t off = 0;
    /* Field #1: Message ID */
    if (off >= cap) return -1;
    payload[off++] = m->id;
    /* Field #2: Sub-ID if applicable */
    int next_field_idx = 1;            /* index into m->fields */
    if (m->has_subid) {
        if (off >= cap) return -1;
        payload[off++] = m->sub_id;
        next_field_idx = 2;
    }
    /* Now user-supplied fields fill from m->fields[next_field_idx] onward. */
    int needed = m->n_fields - next_field_idx;
    int has_optional = (m->optional_trailing_field != NULL);

    /* If the message has an optional trailing field, the user may supply
     * either `needed` values (use the base layout) or `needed + 1` values
     * (the extra is encoded with the optional field, inserted just before
     * the LAST entry in fields[] - which is the Attributes byte by
     * convention). */
    int use_optional = 0;
    if (n_user_fields == needed + 1 && has_optional) {
        use_optional = 1;
    } else if (n_user_fields < needed) {
        fprintf(stderr, "Not enough fields: %s requires %d more value(s) after ID%s.\n",
                m->name, needed, m->has_subid ? " and Sub-ID" : "");
        if (has_optional)
            fprintf(stderr, "(Or %d values to also include optional '%s'.)\n",
                    needed + 1, m->optional_trailing_field->name);
        if (m->has_subid)
            fprintf(stderr, "Use 'gnss_tool -h 0x%02X/0x%02X' to see the field list.\n",
                    m->id, m->sub_id);
        else
            fprintf(stderr, "Use 'gnss_tool -h 0x%02X' to see the field list.\n", m->id);
        return -1;
    } else if (n_user_fields > needed && !has_optional) {
        fprintf(stderr, "Warning: %d extra value(s) ignored.\n", n_user_fields - needed);
    } else if (n_user_fields > needed + 1 && has_optional) {
        fprintf(stderr, "Warning: %d extra value(s) ignored.\n",
                n_user_fields - (needed + 1));
    }

    /* Encode all base fields except the last; if use_optional, slip the
     * optional field in before the last; then encode the last (Attributes). */
    int last = needed - 1;        /* index into the user_fields[] array */
    for (int i = 0; i < last; i++) {
        const msg_field_t *f = &m->fields[next_field_idx + i];
        if (encode_one_field(f, user_fields[i], payload, &off, cap) != 0) {
            return -1;
        }
    }
    if (use_optional) {
        /* user_fields[last] is the optional value (e.g. SouthPAN PRN). */
        if (encode_one_field(m->optional_trailing_field, user_fields[last],
                             payload, &off, cap) != 0) {
            return -1;
        }
        /* The final user_fields[last+1] is the Attributes byte. */
        const msg_field_t *f = &m->fields[next_field_idx + last];
        if (encode_one_field(f, user_fields[last + 1], payload, &off, cap) != 0) {
            return -1;
        }
    } else {
        /* Standard path: encode the last (Attributes) field with user_fields[last]. */
        if (last >= 0) {
            const msg_field_t *f = &m->fields[next_field_idx + last];
            if (encode_one_field(f, user_fields[last], payload, &off, cap) != 0) {
                return -1;
            }
        }
    }
    *out_len = off;
    return 0;
}

/* For unknown-to-DB messages: payload[0]=ID, then each user value is
 * encoded as a single byte. Sub-id is supported via the ID/SubID syntax,
 * which puts both bytes at the front. */
static int build_payload_raw(uint8_t id, int has_subid, uint8_t sub_id,
                             int n_user_fields, char **user_fields,
                             uint8_t *payload, size_t cap, size_t *out_len) {
    size_t off = 0;
    if (off >= cap) return -1;
    payload[off++] = id;
    if (has_subid) {
        if (off >= cap) return -1;
        payload[off++] = sub_id;
    }
    for (int i = 0; i < n_user_fields; i++) {
        unsigned long v;
        if (parse_uint(user_fields[i], &v) != 0) {
            fprintf(stderr, "Invalid value '%s'\n", user_fields[i]);
            return -1;
        }
        if (v > 0xFF) {
            fprintf(stderr, "Field '%s' = 0x%lX > 0xFF, but message is unknown to DB.\n"
                            "Pass each byte separately, or look up the message in the manual.\n",
                            user_fields[i], v);
            return -1;
        }
        payload[off++] = (uint8_t)(v & 0xFF);
        if (off >= cap) return -1;
    }
    *out_len = off;
    return 0;
}

/* ----------- response decoding ------------------------------------------- */
/* Reads a big-endian value of given byte size as unsigned. */
static unsigned long long read_be_u(const uint8_t *p, int sz) {
    unsigned long long v = 0;
    for (int i = 0; i < sz; i++) v = (v << 8) | p[i];
    return v;
}
static long long read_be_s(const uint8_t *p, int sz) {
    unsigned long long u = read_be_u(p, sz);
    /* Sign-extend. */
    unsigned long long sign = 1ULL << (sz*8 - 1);
    if (u & sign) {
        unsigned long long mask = (sz == 8) ? ~0ULL : ((1ULL << (sz*8)) - 1);
        return (long long)(u | ~mask);
    }
    return (long long)u;
}

static void decode_and_print(const uint8_t *payload, size_t plen) {
    if (plen < 1) {
        printf("(empty payload)\n");
        return;
    }
    uint8_t id = payload[0];
    const msg_def_t *m = NULL;
    /* Try with sub-id first if there's enough payload. */
    if (plen >= 2) {
        m = msg_lookup(id, 1, payload[1]);
    }
    if (!m) m = msg_lookup(id, 0, 0);

    if (!m) {
        /* ACK / NACK with sub-id form may not be in the table - handle generic ACK/NACK */
        if (id == 0x83 || id == 0x84) {
            const char *label = id == 0x83 ? "ACK" : "NACK";
            if (plen >= 3) {
                printf("%s for request 0x%02X/0x%02X\n", label, payload[1], payload[2]);
            } else if (plen >= 2) {
                printf("%s for request 0x%02X\n", label, payload[1]);
            } else {
                printf("%s (no body)\n", label);
            }
            return;
        }
        printf("Unknown message 0x%02X (not in DB) — payload (%zu bytes):\n", id, plen);
        dump_hex(payload, plen);
        return;
    }

    /* Print header. */
    if (m->has_subid)
        printf("=== %s (ID 0x%02X / Sub-ID 0x%02X) ===\n", m->name, m->id, m->sub_id);
    else
        printf("=== %s (ID 0x%02X) ===\n", m->name, m->id);

    /* Walk fields, computing offset within payload. The first field is
     * always Message ID at offset 0. */
    size_t off = 0;
    static const char *TNAME[] = { "UINT8","UINT16","UINT32","SINT8","SINT16","SINT32","SPFP","DPFP","BYTES" };
    for (int i = 0; i < m->n_fields; i++) {
        const msg_field_t *f = &m->fields[i];
        int sz = (f->type == FT_BYTES) ? (f->size > 0 ? f->size : (int)(plen - off))
                                       : field_type_size(f->type);
        if (off + (size_t)sz > plen) {
            /* For optional trailing fields like ACK Sub-ID, just stop printing. */
            break;
        }
        printf("  Field %-6s %-30s = ", f->range, f->name);
        switch (f->type) {
            case FT_U8: case FT_U16: case FT_U32: {
                unsigned long long u = read_be_u(payload + off, sz);
                printf("%llu (0x%llX)", u, u);
                break;
            }
            case FT_S8: case FT_S16: case FT_S32: {
                long long s = read_be_s(payload + off, sz);
                printf("%lld", s);
                break;
            }
            case FT_SPFP: {
                uint32_t u = (uint32_t)read_be_u(payload + off, 4);
                float fv;
                memcpy(&fv, &u, 4);
                printf("%g", (double)fv);
                break;
            }
            case FT_DPFP: {
                uint64_t u = read_be_u(payload + off, 8);
                double dv;
                memcpy(&dv, &u, 8);
                printf("%g", dv);
                break;
            }
            case FT_BYTES: {
                int has_print = 1;
                for (int k = 0; k < sz; k++) {
                    unsigned char c = payload[off+k];
                    if (c != 0 && (c < 0x20 || c > 0x7E) && c != '\r' && c != '\n' && c != '\t') {
                        has_print = 0; break;
                    }
                }
                if (has_print) {
                    printf("\"");
                    for (int k = 0; k < sz; k++) {
                        unsigned char c = payload[off+k];
                        if (c == 0) break;
                        putchar(c);
                    }
                    printf("\"");
                } else {
                    printf("[");
                    for (int k = 0; k < sz; k++) printf("%s%02X", k?" ":"", payload[off+k]);
                    printf("]");
                }
                break;
            }
        }
        if (f->unit && *f->unit) printf(" %s", f->unit);
        printf("   <%s>\n", TNAME[f->type]);
        off += (size_t)sz;
    }
    if (off < plen) {
        printf("  +%zu trailing bytes: ", plen - off);
        dump_hex(payload + off, plen - off);
    }
}

/* ----------- write raw frame to file ------------------------------------- */
/* mode_w  = 1 -> truncate, 0 -> append (used for multi-response captures). */
static int save_raw_frame(const char *path, const uint8_t *raw, size_t n, int mode_w) {
    FILE *f = fopen(path, mode_w ? "wb" : "ab");
    if (!f) {
        fprintf(stderr, "Cannot open '%s' for %s\n", path, mode_w ? "writing" : "appending");
        return -1;
    }
    if (fwrite(raw, 1, n, f) != n) {
        fprintf(stderr, "Short write to '%s'\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    printf("[%s %zu raw bytes %s %s]\n",
           mode_w ? "saved" : "appended", n,
           mode_w ? "to" : "to", path);
    return 0;
}

/* Truncate the outfile to zero bytes (called once before a multi-response
 * capture so subsequent appends accumulate). */
static int truncate_outfile(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Cannot open '%s' for writing\n", path);
        return -1;
    }
    fclose(f);
    return 0;
}

/* ----------- send a request, wait for ACK + (optional) reply ------------- */
typedef struct {
    serial_handle_t h;
    int             timeout_ms;
    const char     *outfile;     /* for -q only */
    int             repeat;      /* 1 = single response (default);
                                  * N>1 = collect up to N response frames;
                                  * 0   = collect every response until timeout */
} ctx_t;

/* Send a prebuilt frame and wait for ACK/NACK referencing our id (possibly
 * with sub-id). For queries also waits for the linked response.
 *
 * cx->repeat controls how many response frames to collect after the ACK:
 *   1  = exactly one response (default, original behaviour)
 *   N>1 = up to N response frames; stops on count or on timeout
 *   0  = collect everything until timeout (used for streaming responses
 *        like 0x5B with slot=0, which dumps all 24 GLONASS slots).
 *
 * Returns: 0 if ACK was received and (for queries) at least one response
 *            (or, when repeat==0, ACK + timeout-bounded collection succeeded).
 *          1 if NACK was received.
 *          2 if timeout while still waiting for ACK or first response.
 *         -1 on protocol error.
 */
static int send_and_handle(ctx_t *cx,
                           const uint8_t *frame, size_t frame_len,
                           uint8_t req_id, int has_subid, uint8_t sub_id,
                           int wait_response,
                           int resp_id, int resp_subid_used, int resp_subid) {
    serial_flush_input(cx->h);
    if (serial_write(cx->h, frame, frame_len) < 0) {
        fprintf(stderr, "serial_write failed\n");
        return -1;
    }
    printf(">> Sent %zu bytes: ", frame_len);
    dump_hex(frame, frame_len);

    /* If we're going to capture to a file and we may collect multiple frames,
     * truncate the file up front so subsequent appends accumulate cleanly. */
    if (cx->outfile && wait_response && cx->repeat != 1) {
        if (truncate_outfile(cx->outfile) != 0) return -1;
    }

    int got_ack = 0;
    int responses_collected = 0;
    int target = cx->repeat;          /* 1, N, or 0 (=unlimited) */
    /* "done with responses" predicate */
    #define RESP_DONE() ( !wait_response \
                        || (target == 0 ? 0 \
                                        : responses_collected >= target) )

    /* Loop: read frames until (we have ACK) AND (response goal met OR timeout). */
    while (!got_ack || !RESP_DONE()) {
        uint8_t pl[PROTO_MAX_PAYLOAD];
        uint8_t raw[PROTO_MAX_PACKET];
        size_t  raw_len = 0;
        int n = proto_read_frame(cx->h, cx->timeout_ms,
                                 pl, sizeof(pl),
                                 raw, sizeof(raw), &raw_len);
        if (n == 0) {
            /* Timeout. The interpretation depends on what we already have: */
            if (!got_ack) {
                fprintf(stderr, "Timeout (%d ms) waiting for ACK/NACK.\n",
                        cx->timeout_ms);
                return 2;
            }
            if (target == 0) {
                /* Streaming mode: no more frames within the inter-frame
                 * window. This is the expected end-of-burst signal. */
                printf("<< End of response stream.\n"
                       "<< Collected %d response frame(s).\n",
                       responses_collected);
                return responses_collected > 0 ? 0 : 2;
            }
            if (responses_collected == 0) {
                fprintf(stderr, "Timeout (%d ms) waiting for response message.\n",
                        cx->timeout_ms);
                return 2;
            }
            /* Bounded repeat: timeout before we got all N. Report what we got. */
            fprintf(stderr,
                "Stopped after %d/%d response(s) (no more frames within %d ms).\n",
                responses_collected, target, cx->timeout_ms);
            printf("<< Collected %d response frame(s).\n", responses_collected);
            return responses_collected > 0 ? 0 : 2;
        }
        if (n < 0) return -1;

        /* ACK ? */
        if (pl[0] == 0x83 && !got_ack) {
            int matches = 0;
            if (n >= 2 && pl[1] == req_id) {
                if (!has_subid) matches = 1;
                else if (n >= 3 && pl[2] == sub_id) matches = 1;
            }
            if (matches) {
                printf("<< ACK received (request 0x%02X", req_id);
                if (has_subid) printf("/0x%02X", sub_id);
                printf(").\n");
                got_ack = 1;
                continue;
            }
        }
        if (pl[0] == 0x84) {
            int matches = 0;
            if (n >= 2 && pl[1] == req_id) {
                if (!has_subid) matches = 1;
                else if (n >= 3 && pl[2] == sub_id) matches = 1;
            }
            if (matches) {
                printf("<< NACK received (request 0x%02X", req_id);
                if (has_subid) printf("/0x%02X", sub_id);
                printf(" rejected).\n");
                return 1;
            }
        }
        /* Maybe it's our awaited response. */
        if (wait_response && resp_id >= 0) {
            int matches = 0;
            if (pl[0] == (uint8_t)resp_id) {
                if (!resp_subid_used) matches = 1;
                else if (n >= 2 && pl[1] == (uint8_t)resp_subid) matches = 1;
            }
            if (matches) {
                responses_collected++;
                if (cx->repeat == 1) {
                    printf("<< Response received (%d bytes payload):\n", n);
                } else {
                    printf("<< Response #%d received (%d bytes payload):\n",
                           responses_collected, n);
                }
                dump_hex(pl, (size_t)n);
                printf("\n");
                decode_and_print(pl, (size_t)n);
                if (cx->outfile && raw_len > 0) {
                    /* Truncate on the FIRST write when repeat==1; otherwise
                     * append (we already pre-truncated above). */
                    int trunc = (cx->repeat == 1);
                    save_raw_frame(cx->outfile, raw, raw_len, trunc);
                }
                continue;
            }
        }
        /* Otherwise it's some other (probably asynchronous) message - print it
         * for transparency and keep waiting. */
        printf("<< Other message received while waiting (ID 0x%02X, %d bytes payload):\n",
               pl[0], n);
        dump_hex(pl, (size_t)n);
    }
    if (cx->repeat != 1 && wait_response) {
        printf("<< Collected %d response frame(s).\n", responses_collected);
    }
    return 0;
    #undef RESP_DONE
}

/* ----------- run a single -c or -q command -------------------------------- */
static int run_cq(ctx_t *cx, int is_query, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Missing message ID\n");
        return -1;
    }
    uint8_t id = 0, subid = 0;
    int has_subid = 0;
    if (parse_msg_id(argv[0], &id, &has_subid, &subid) != 0) {
        fprintf(stderr, "Invalid message id: %s\n", argv[0]);
        return -1;
    }

    /* Look up. If query and the user typed the primary id without a sub-id,
     * but the table only has sub-id forms, give a friendly error. */
    const msg_def_t *m = msg_lookup(id, has_subid, subid);

    uint8_t payload[PROTO_MAX_PAYLOAD];
    size_t  plen = 0;
    if (m) {
        if (build_payload_from_def(m, argc - 1, argv + 1,
                                   payload, sizeof(payload), &plen) != 0)
            return -1;
        /* Reflect actual sub-id presence from definition. */
        has_subid = m->has_subid;
        subid     = m->sub_id;
    } else {
        if (has_subid)
            fprintf(stderr, "Note: message 0x%02X/0x%02X is not in the DB; sending as raw bytes.\n",
                    id, subid);
        else
            fprintf(stderr, "Note: message 0x%02X is not in the DB; sending as raw bytes.\n", id);
        if (build_payload_raw(id, has_subid, subid,
                              argc - 1, argv + 1,
                              payload, sizeof(payload), &plen) != 0)
            return -1;
    }

    uint8_t frame[PROTO_MAX_PACKET];
    int flen = proto_encode(payload, plen, frame, sizeof(frame));
    if (flen < 0) {
        fprintf(stderr, "proto_encode failed\n");
        return -1;
    }

    int wait_resp = 0, resp_id = -1, resp_subid_used = 0, resp_subid = 0;
    if (is_query) {
        wait_resp = 1;
        if (m && m->resp_id >= 0) {
            resp_id = m->resp_id;
            resp_subid_used = m->resp_subid_used;
            resp_subid = m->resp_subid;
        } else {
            /* Unknown query: still wait for ACK, but treat any non-ACK as the response. */
            resp_id = -1;
        }
    }

    int rc = send_and_handle(cx, frame, (size_t)flen,
                             id, has_subid, subid,
                             wait_resp,
                             resp_id, resp_subid_used, resp_subid);
    return rc;
}

/* ----------- script-file batch (-i) -------------------------------------- */
static int tokenise(char *line, char **argv, int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        argv[n++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) { *p++ = '\0'; }
    }
    return n;
}

static int run_script(ctx_t *cx, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Cannot open script '%s'\n", path);
        return -1;
    }
    char line[1024];
    int line_no = 0, errs = 0;
    while (fgets(line, sizeof(line), f)) {
        line_no++;
        /* strip newline */
        size_t L = strlen(line);
        while (L > 0 && (line[L-1]=='\r'||line[L-1]=='\n')) line[--L]=0;
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;
        char *argv_[32];
        int argc_ = tokenise(p, argv_, 32);
        if (argc_ < 2) {
            fprintf(stderr, "[%s:%d] empty/short command, skipping\n", path, line_no);
            continue;
        }
        printf("\n--- script line %d: %s\n", line_no, p);
        if (strcmp(argv_[0], "-c") == 0) {
            if (run_cq(cx, 0, argc_-1, argv_+1) < 0) errs++;
        } else if (strcmp(argv_[0], "-q") == 0) {
            if (run_cq(cx, 1, argc_-1, argv_+1) < 0) errs++;
        } else {
            fprintf(stderr, "[%s:%d] expected -c or -q, got '%s'\n", path, line_no, argv_[0]);
            errs++;
        }
    }
    fclose(f);
    return errs;
}

/* ----------- usage ------------------------------------------------------- */
static void usage(const char *prog) {
    fprintf(stderr,
"GNSS receiver console tool (SkyTraq AN0037 v1.4.69)\n"
"\n"
"Usage:\n"
"  %s -h MessageID\n"
"      Print the parameter list (Name / Type / Unit / Description) for the\n"
"      given message. MessageID is decimal by default; append 'h' or use\n"
"      '0x' prefix for hex. Use 'ID/SubID' for sub-id messages.\n"
"\n"
"  %s -p PORT [opts] -c MessageID Field2 Field3 ...\n"
"      Send a Set / Configure command. Wait for ACK or NACK.\n"
"\n"
"  %s -p PORT [opts] -q MessageID Field2 Field3 ...\n"
"      Send a Query command. Wait for ACK plus the response message.\n"
"\n"
"  %s -p PORT [opts] -i SCRIPT_FILE\n"
"      Run -c / -q commands from a script file (one per line).\n"
"\n"
"Options:\n"
"  -p PORT      Serial port (e.g. COM3, /dev/ttyUSB0)\n"
"  -b BAUD      Baud rate. Any integer. Default 115200.\n"
"  -t MS        Response timeout in milliseconds. Default 3000.\n"
"  -r N         Number of response frames to collect after the ACK (default 1).\n"
"               Use N>1 when one query produces several responses (e.g. 0x5B with\n"
"               slot=0 dumps all 24 GLONASS slots as 24 x 0x90 frames).\n"
"               Use 0 to collect every response until -t timeout (streaming mode).\n"
"               With -o, every collected frame is appended to the output file.\n"
"  -o FILE      With -q: save the raw response frame(s) (0xA0..0x0D 0x0A) to FILE.\n"
"  -i FILE      Batch script of -c / -q commands.\n"
"\n"
"Numeric format:\n"
"   123     -> decimal\n"
"   1Bh     -> hex (suffix 'h')\n"
"   0x1B    -> hex (C-style)\n"
"\n"
"Examples:\n"
"  %s -h 0x02                            # show fields of QUERY SOFTWARE VERSION\n"
"  %s -h 100/23                          # show fields of CONFIGURE GNSS NAVIGATION MODE\n"
"  %s -p /dev/ttyUSB0 -q 2 0             # query software version, system code\n"
"  %s -p COM3 -b 115200 -c 0x09 1 1      # set NMEA output, save to SRAM+FLASH\n"
"  %s -p /dev/ttyUSB0 -o ver.bin -q 2 1\n"
"  %s -p COM28 -t 6000 -r 0 -o glo.bin -q 0x5B 0   # capture all GLONASS ephemeris\n",
        prog, prog, prog, prog,
        prog, prog, prog, prog, prog, prog);
}

/* ----------- main -------------------------------------------------------- */
int main(int argc, char **argv) {
    /* Modes:
     *   -h MessageID                        no port needed
     *   -c / -q ... (with -p)               port required
     *   -i FILE  (with -p)                  port required
     */
    if (argc < 2) { usage(argv[0]); return 1; }

    /* First scan for -h MessageID; if present, just print and exit. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "-h needs MessageID\n"); return 1; }
            return cmd_help(argv[i+1]);
        }
    }

    /* Otherwise we'll be talking to a serial port. */
    const char *port = NULL;
    int baud = 115200;
    int timeout_ms = 3000;
    int repeat = 1;
    const char *outfile = NULL;
    const char *scriptfile = NULL;
    int do_set = 0, do_query = 0;
    char **fields = NULL;
    int n_fields = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-p") == 0 && i+1 < argc) { port = argv[++i]; }
        else if (strcmp(a, "-b") == 0 && i+1 < argc) {
            unsigned long v;
            if (parse_uint(argv[++i], &v) != 0) {
                fprintf(stderr, "Bad baud rate\n"); return 1;
            }
            baud = (int)v;
        }
        else if (strcmp(a, "-t") == 0 && i+1 < argc) {
            unsigned long v;
            if (parse_uint(argv[++i], &v) != 0) {
                fprintf(stderr, "Bad timeout\n"); return 1;
            }
            timeout_ms = (int)v;
        }
        else if (strcmp(a, "-r") == 0 && i+1 < argc) {
            unsigned long v;
            if (parse_uint(argv[++i], &v) != 0) {
                fprintf(stderr, "Bad repeat count\n"); return 1;
            }
            if (v > 1000000) {
                fprintf(stderr, "Repeat count too large\n"); return 1;
            }
            repeat = (int)v;
        }
        else if (strcmp(a, "-o") == 0 && i+1 < argc) { outfile = argv[++i]; }
        else if (strcmp(a, "-i") == 0 && i+1 < argc) { scriptfile = argv[++i]; }
        else if (strcmp(a, "-c") == 0) {
            do_set = 1;
            fields = &argv[i+1];
            n_fields = argc - (i+1);
            break;
        }
        else if (strcmp(a, "-q") == 0) {
            do_query = 1;
            fields = &argv[i+1];
            n_fields = argc - (i+1);
            break;
        }
        else {
            fprintf(stderr, "Unknown option '%s'\n", a);
            usage(argv[0]);
            return 1;
        }
    }

    if (!port) {
        fprintf(stderr, "Error: -p PORT is required for -c / -q / -i operations.\n\n");
        usage(argv[0]);
        return 1;
    }
    if (!do_set && !do_query && !scriptfile) {
        fprintf(stderr, "Error: nothing to do (use -c, -q, -i, or -h).\n\n");
        usage(argv[0]);
        return 1;
    }

    serial_handle_t h = serial_open(port, baud);
    if (h == SERIAL_INVALID_HANDLE) return 2;
    printf("Opened %s @ %d baud  (timeout %d ms, repeat %d)\n",
           port, baud, timeout_ms,
           repeat == 0 ? -1 : repeat);

    ctx_t cx = { h, timeout_ms, outfile, repeat };
    int rc = 0;
    if (scriptfile) {
        rc = run_script(&cx, scriptfile);
    } else if (do_set) {
        rc = run_cq(&cx, 0, n_fields, fields);
    } else if (do_query) {
        rc = run_cq(&cx, 1, n_fields, fields);
    }
    serial_close(h);
    return (rc == 0) ? 0 : 1;
}
