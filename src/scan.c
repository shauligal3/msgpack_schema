/* SPDX-License-Identifier: MIT */
/**
 * @file scan.c
 * @brief A non-allocating MessagePack framing scanner.
 *
 * msgpack-c reserves memory for a container as soon as it reads the
 * container's header, based on the *declared* element count. A few hostile
 * bytes can therefore request gigabytes. This scanner walks one object
 * without allocating and only reports it complete once every declared element
 * is actually present, which bounds the memory msgpack-c can be asked for by
 * the size of the input. It also tells the streaming decoder where an object
 * ends.
 */
#include "internal.h"

/* Reads an n-byte big-endian unsigned integer. */
static uint64_t read_be(const unsigned char *p, size_t n)
{
    uint64_t v = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

/* How the bytes after a type byte are laid out. */
enum {
    K_FIXED, /* `fixed` payload bytes */
    K_BYTES, /* `n` payload bytes, then `fixed` more (ext type byte) */
    K_ARRAY, /* `n` items follow */
    K_MAP    /* `n` key/value pairs follow */
};

typedef struct {
    size_t hdr;     /* bytes after the type byte that hold the length / count `n` */
    size_t fixed;   /* payload bytes that do not depend on `n` */
    int kind;       /* one of the K_* values */
    int has_inline; /* fix* formats: `n` is encoded in the type byte itself */
    uint64_t inl;   /* that inline `n` */
} mps_fmt_t;

/* Fills @p f for type byte @p b. Returns 0 for the reserved byte 0xc1. */
static int describe(unsigned char b, mps_fmt_t *f)
{
    f->hdr = 0;
    f->fixed = 0;
    f->kind = K_FIXED;
    f->has_inline = 0;
    f->inl = 0;

    if (b <= 0x7f || b >= 0xe0) { /* positive / negative fixint */
        return 1;
    }
    if (b <= 0x8f) { /* fixmap */
        f->kind = K_MAP;
        f->has_inline = 1;
        f->inl = b & 0x0FU;
        return 1;
    }
    if (b <= 0x9f) { /* fixarray */
        f->kind = K_ARRAY;
        f->has_inline = 1;
        f->inl = b & 0x0FU;
        return 1;
    }
    if (b <= 0xbf) { /* fixstr */
        f->kind = K_BYTES;
        f->has_inline = 1;
        f->inl = b & 0x1FU;
        return 1;
    }

    switch (b) {
    case 0xc0: /* nil */
    case 0xc2: /* false */
    case 0xc3: /* true */
        return 1;
    case 0xc4: /* bin 8/16/32 */
    case 0xd9: /* str 8 */
        f->kind = K_BYTES;
        f->hdr = 1;
        return 1;
    case 0xc5:
    case 0xda:
        f->kind = K_BYTES;
        f->hdr = 2;
        return 1;
    case 0xc6:
    case 0xdb:
        f->kind = K_BYTES;
        f->hdr = 4;
        return 1;
    case 0xc7: /* ext 8/16/32: length, then a type byte, then data */
        f->kind = K_BYTES;
        f->hdr = 1;
        f->fixed = 1;
        return 1;
    case 0xc8:
        f->kind = K_BYTES;
        f->hdr = 2;
        f->fixed = 1;
        return 1;
    case 0xc9:
        f->kind = K_BYTES;
        f->hdr = 4;
        f->fixed = 1;
        return 1;
    case 0xcc: /* uint8 */
    case 0xd0: /* int8 */
        f->fixed = 1;
        return 1;
    case 0xcd:
    case 0xd1:
        f->fixed = 2;
        return 1;
    case 0xca: /* float32 */
    case 0xce:
    case 0xd2:
        f->fixed = 4;
        return 1;
    case 0xcb: /* float64 */
    case 0xcf:
    case 0xd3:
        f->fixed = 8;
        return 1;
    case 0xd4: /* fixext 1/2/4/8/16: type byte + data */
        f->fixed = 2;
        return 1;
    case 0xd5:
        f->fixed = 3;
        return 1;
    case 0xd6:
        f->fixed = 5;
        return 1;
    case 0xd7:
        f->fixed = 9;
        return 1;
    case 0xd8:
        f->fixed = 17;
        return 1;
    case 0xdc: /* array 16/32 */
        f->kind = K_ARRAY;
        f->hdr = 2;
        return 1;
    case 0xdd:
        f->kind = K_ARRAY;
        f->hdr = 4;
        return 1;
    case 0xde: /* map 16/32 */
        f->kind = K_MAP;
        f->hdr = 2;
        return 1;
    case 0xdf:
        f->kind = K_MAP;
        f->hdr = 4;
        return 1;
    default: /* 0xc1 is never used */
        return 0;
    }
}

int mps_msgpack_scan(const char *buf, size_t len, size_t *obj_len)
{
    const unsigned char *p = (const unsigned char *)buf;
    uint64_t pending = 1; /* items still to be read */
    size_t pos = 0;

    if (obj_len != NULL) {
        *obj_len = 0;
    }
    if (buf == NULL && len != 0) {
        return mpschema_invalid_param;
    }

    while (pending != 0) {
        mps_fmt_t f;
        uint64_t n;
        size_t avail;

        if (pos >= len) {
            return mpschema_incomplete;
        }
        if (!describe(p[pos], &f)) {
            return mpschema_malformed;
        }
        pos++;
        avail = len - pos;
        if (f.hdr > avail) {
            return mpschema_incomplete;
        }
        n = f.has_inline ? f.inl : read_be(p + pos, f.hdr);
        pos += f.hdr;
        avail -= f.hdr;
        pending--;

        switch (f.kind) {
        case K_BYTES:
            if (n > avail || f.fixed > avail - n) {
                return mpschema_incomplete;
            }
            pos += (size_t)n + f.fixed;
            break;
        case K_ARRAY:
            pending += n;
            break;
        case K_MAP:
            pending += 2 * n;
            break;
        default:
            if (f.fixed > avail) {
                return mpschema_incomplete;
            }
            pos += f.fixed;
            break;
        }
    }

    if (obj_len != NULL) {
        *obj_len = pos;
    }
    return mpschema_ok;
}
