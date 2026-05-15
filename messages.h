/*
 * messages.h - SkyTraq AN0037 message database
 *
 * Each entry describes one binary message: its primary ID, optional Sub-ID,
 * category (input set/query, output) and the field layout used to render
 * help text (-h) and to parse responses (-q).
 *
 * Field offsets are byte offsets within the *payload* (which begins at the
 * Message ID byte). For input messages, payload[0] is the Message ID and,
 * if has_subid, payload[1] is the Sub-ID; user-supplied fields begin
 * after that.
 *
 * The set is curated rather than exhaustive: it covers the common control,
 * configuration, query and response messages from the AN0037 manual. Any
 * message not in the table can still be sent via -c / -q by supplying the
 * raw bytes; the response is simply rendered as a hex dump (and saved as
 * raw bytes when -o is given).
 */
#ifndef GNSS_MESSAGES_H
#define GNSS_MESSAGES_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    FT_U8 = 0, FT_U16, FT_U32,
    FT_S8,    FT_S16, FT_S32,
    FT_SPFP,  FT_DPFP,
    FT_BYTES                  /* fixed-length opaque byte array */
} field_type_t;

typedef struct {
    const char *range;        /* e.g. "1", "2-3", "10-11" */
    const char *name;         /* human-readable name */
    field_type_t type;        /* data type */
    int         size;         /* bytes (for BYTES); else derived from type */
    const char *unit;         /* "" if none */
    const char *desc;         /* short description, may contain enum values */
} msg_field_t;

typedef enum {
    MSG_INPUT_SET    = 1,     /* host -> receiver, configure (no extra response) */
    MSG_INPUT_QUERY  = 2,     /* host -> receiver, request (response message follows ACK) */
    MSG_OUTPUT       = 3      /* receiver -> host (response or asynchronous) */
} msg_kind_t;

typedef struct msg_def {
    uint8_t  id;              /* primary Message ID  */
    int      has_subid;       /* 1 if sub_id is valid */
    uint8_t  sub_id;
    msg_kind_t kind;
    const char *name;         /* short title, e.g. "QUERY SOFTWARE VERSION" */
    const char *summary;      /* one-line summary */
    /* Linked response (only meaningful for queries): id and sub_id of the
     * response message. resp_subid_used = 0 when the response is plain ID. */
    int      resp_id;         /* -1 if no specific response (just ACK) */
    int      resp_subid_used;
    int      resp_subid;
    /* Field table. fields[0] is always Message ID. For sub-ID-bearing
     * messages, fields[1] is Sub ID. */
    const msg_field_t *fields;
    int               n_fields;
    /* Optional trailing field: when non-NULL, the user may supply one
     * extra argument; its encoded bytes are inserted in-wire just before
     * the LAST entry in `fields[]` (the Attributes byte by convention).
     * Used to handle messages whose payload length depends on firmware,
     * e.g. 0x62/0x05 with optional SouthPAN PRN. */
    const msg_field_t *optional_trailing_field;
} msg_def_t;

/* Look up by primary ID (and optional sub-ID).  When has_subid is 0, the
 * caller is asking for "no sub-id" form; messages requiring a sub-ID will
 * be skipped and NULL returned. */
const msg_def_t *msg_lookup(uint8_t id, int has_subid, uint8_t sub_id);

/* Look up by primary ID only - returns the *first* match, used by
 * response handling when we don't know the sub-id yet. */
const msg_def_t *msg_lookup_id(uint8_t id);

/* Iterate.  Returns NULL when done. */
const msg_def_t *msg_iter(int index);

/* Returns the byte size of a non-BYTES type. */
int field_type_size(field_type_t t);

#endif
