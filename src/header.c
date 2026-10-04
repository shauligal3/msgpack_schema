/* SPDX-License-Identifier: MIT */
/**
 * @file header.c
 * @brief Message framing header: `[msgtype][msglen]` as MessagePack uints.
 */
#include <string.h>

#include "internal.h"

int mpschema_encode_header(uint16_t msgtype, uint16_t msglen, char *buf, size_t *sz)
{
    msgpack_packer pk;
    mpschema_buffer_t out;

    if (buf == NULL || sz == NULL) {
        return mpschema_invalid_param;
    }
    memset(&out, 0, sizeof(out));
    out.data = buf;
    out.alloced = *sz;

    msgpack_packer_init(&pk, &out, mpschema_buffer_write);
    if (msgpack_pack_uint16(&pk, msgtype) != 0 || msgpack_pack_uint16(&pk, msglen) != 0) {
        *sz = 0;
        return mpschema_no_space;
    }
    *sz = out.used;
    return mpschema_ok;
}

/* Parses one unsigned integer <= UINT16_MAX at buf[*off]. */
static int decode_u16(msgpack_unpacked *u, const char *buf, size_t sz, size_t *off, uint16_t *out)
{
    switch (msgpack_unpack_next(u, buf, sz, off)) {
    case MSGPACK_UNPACK_SUCCESS:
        break;
    case MSGPACK_UNPACK_CONTINUE:
        return mpschema_incomplete;
    default:
        return mpschema_malformed;
    }
    if (u->data.type != MSGPACK_OBJECT_POSITIVE_INTEGER || u->data.via.u64 > UINT16_MAX) {
        return mpschema_malformed;
    }
    *out = (uint16_t)u->data.via.u64;
    return mpschema_ok;
}

int mpschema_decode_header(uint16_t *msgtype, uint16_t *msglen, size_t *off, const char *buf,
                           size_t sz)
{
    msgpack_unpacked u;
    size_t pos = 0;
    uint16_t type = 0;
    uint16_t len = 0;
    int rc;

    if (msgtype == NULL || msglen == NULL || off == NULL || (buf == NULL && sz != 0)) {
        return mpschema_invalid_param;
    }
    *off = 0;

    msgpack_unpacked_init(&u);
    rc = decode_u16(&u, buf, sz, &pos, &type);
    if (rc == mpschema_ok) {
        rc = decode_u16(&u, buf, sz, &pos, &len);
    }
    msgpack_unpacked_destroy(&u);

    if (rc != mpschema_ok) {
        return rc;
    }
    if (type == 0 || len == 0) {
        return mpschema_malformed;
    }
    *msgtype = type;
    *msglen = len;
    *off = pos;
    return mpschema_ok;
}

size_t mpschema_get_header_max_size(void)
{
    static size_t cached;
    char buf[16];
    size_t sz = sizeof(buf);

    if (cached == 0 && mpschema_encode_header(UINT16_MAX, UINT16_MAX, buf, &sz) == mpschema_ok) {
        cached = sz;
    }
    return cached;
}
