/* SPDX-License-Identifier: MIT */
/**
 * @file encode.c
 * @brief Struct -> MessagePack encoder.
 */
#include <string.h>

#include "internal.h"

typedef struct mps_encoder_ {
    msgpack_packer pk;
    int export_names;
} mps_encoder_t;

/* msgpack_pack_* return whatever the writer returned; map it to a status. */
static int pack_status(int rc)
{
    if (rc == 0) {
        return mpschema_ok;
    }
    return rc == mpschema_no_space ? mpschema_no_space : mpschema_io_error;
}

#define MPS_PACK(expr)                                                                             \
    do {                                                                                           \
        int rc_ = pack_status(expr);                                                               \
        if (rc_ != mpschema_ok) {                                                                  \
            return rc_;                                                                            \
        }                                                                                          \
    } while (0)

static int encode_string(mps_encoder_t *enc, const mpschema_member_t *m, const char *s)
{
    size_t len = 0;

    if (s != NULL) {
        /* a member of N bytes holds at most N - 1 characters plus the NUL */
        len = m->sm_elem_size != 0 ? strnlen(s, m->sm_elem_size - 1) : strlen(s);
    }
    MPS_PACK(msgpack_pack_str(&enc->pk, len));
    MPS_PACK(msgpack_pack_str_body(&enc->pk, s, len));
    return mpschema_ok;
}

static int encode_scalar(mps_encoder_t *enc, const mpschema_member_t *m, const void *elem)
{
    switch (m->sm_type) {
    case SCHEMA_TAG_BOOL: {
        uint8_t v;
        memcpy(&v, elem, sizeof(v));
        MPS_PACK(v ? msgpack_pack_true(&enc->pk) : msgpack_pack_false(&enc->pk));
        return mpschema_ok;
    }
    case SCHEMA_TAG_UINT8: {
        uint8_t v;
        memcpy(&v, elem, sizeof(v));
        MPS_PACK(msgpack_pack_uint8(&enc->pk, v));
        return mpschema_ok;
    }
    case SCHEMA_TAG_UINT16: {
        uint16_t v;
        memcpy(&v, elem, sizeof(v));
        MPS_PACK(msgpack_pack_uint16(&enc->pk, v));
        return mpschema_ok;
    }
    case SCHEMA_TAG_UINT32: {
        uint32_t v;
        memcpy(&v, elem, sizeof(v));
        MPS_PACK(msgpack_pack_uint32(&enc->pk, v));
        return mpschema_ok;
    }
    case SCHEMA_TAG_UINT64: {
        uint64_t v;
        memcpy(&v, elem, sizeof(v));
        MPS_PACK(msgpack_pack_uint64(&enc->pk, v));
        return mpschema_ok;
    }
    case SCHEMA_TAG_DOUBLE: {
        double v;
        memcpy(&v, elem, sizeof(v));
        MPS_PACK(msgpack_pack_double(&enc->pk, v));
        return mpschema_ok;
    }
    case SCHEMA_TAG_STRING:
        return encode_string(enc, m, (const char *)elem);
    case SCHEMA_TAG_VARLEN_STRING: {
        const char *s;
        memcpy((void *)&s, elem, sizeof(s));
        return encode_string(enc, m, s);
    }
    default:
        return mpschema_malformed;
    }
}

static int encode_map(mps_encoder_t *enc, const mpschema_t *schema, const void *obj,
                      uint16_t obj_type);

static int encode_member(mps_encoder_t *enc, const mpschema_member_t *m, const void *obj)
{
    const int is_nested = m->sm_type == SCHEMA_TAG_NESTED || m->sm_type == SCHEMA_TAG_REFERS;
    const size_t count = mps_member_elem_count(m, obj);
    size_t i;
    int rc;

    if (enc->export_names) {
        MPS_PACK(msgpack_pack_str(&enc->pk, m->sm_name_len));
        MPS_PACK(msgpack_pack_str_body(&enc->pk, m->sm_name, m->sm_name_len));
    } else {
        MPS_PACK(msgpack_pack_uint16(&enc->pk, m->sm_tag));
    }

    if (m->sm_max_elems > 1) {
        MPS_PACK(msgpack_pack_array(&enc->pk, count));
    }

    for (i = 0; i < count; i++) {
        const void *elem = mps_member_elem(m, obj, i, NULL);
        if (elem == NULL) {
            return mpschema_malformed;
        }
        rc = is_nested ? encode_map(enc, m->sm_nested, elem, 0) : encode_scalar(enc, m, elem);
        if (rc != mpschema_ok) {
            return rc;
        }
    }
    return mpschema_ok;
}

static int encode_map(mps_encoder_t *enc, const mpschema_t *schema, const void *obj,
                      uint16_t obj_type)
{
    const mpschema_member_t *m;
    size_t entries = mps_count_present(schema, obj);
    int rc;

    if (obj_type != 0) {
        entries++;
    }
    MPS_PACK(msgpack_pack_map(&enc->pk, entries));

    if (obj_type != 0) {
        MPS_PACK(msgpack_pack_uint16(&enc->pk, MPSCHEMAV2_OBJTYPE_TAG));
        MPS_PACK(msgpack_pack_uint16(&enc->pk, obj_type));
    }

    if (obj == NULL) {
        return mpschema_ok;
    }

    /* must visit exactly the members counted by mps_count_present() */
    for (m = schema->mps_members; !is_mpschema_eof(m); m++) {
        if (!mps_member_is_present(m, obj)) {
            continue;
        }
        rc = encode_member(enc, m, obj);
        if (rc != mpschema_ok) {
            return rc;
        }
    }
    return mpschema_ok;
}

int mps_encode(const mpschema_t *schema, const void *obj, const mps_encode_opts_t *opts)
{
    mps_encoder_t enc;

    if (schema == NULL || obj == NULL || opts == NULL || opts->writer == NULL) {
        return mpschema_invalid_param;
    }
    memset(&enc, 0, sizeof(enc));
    enc.export_names = opts->export_names;
    msgpack_packer_init(&enc.pk, opts->writer_arg, opts->writer);
    return encode_map(&enc, schema, obj, opts->obj_type);
}
