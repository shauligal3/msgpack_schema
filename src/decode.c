/* SPDX-License-Identifier: MIT */
/**
 * @file decode.c
 * @brief MessagePack -> struct decoder.
 */
#include <string.h>

#include "internal.h"

/* Internal, positive: the value did not match the member and was ignored. */
#define MPS_SKIPPED 1

/* Refuses writes of @p n bytes at @p p that would cross @p end. */
static int check_space(const void *p, size_t n, const void *end)
{
    if (end != NULL && (const char *)p + n > (const char *)end) {
        return mpschema_no_space;
    }
    return mpschema_ok;
}

static int skip_value(const mpschema_member_t *m, const msgpack_object *v)
{
    MPS_DEBUG("[mpschema_decode] tag %u (%s): unexpected msgpack type %d, value skipped\n",
              (unsigned)m->sm_tag, m->sm_name ? m->sm_name : "?", (int)v->type);
    return MPS_SKIPPED;
}

static int decode_uint(const mpschema_member_t *m, void *elem, const msgpack_object *v,
                       const void *end, size_t width, uint64_t max)
{
    int rc;

    if (v->type != MSGPACK_OBJECT_POSITIVE_INTEGER || v->via.u64 > max) {
        return skip_value(m, v);
    }
    rc = check_space(elem, width, end);
    if (rc != mpschema_ok) {
        return rc;
    }
    switch (width) {
    case sizeof(uint8_t): {
        uint8_t x = (uint8_t)v->via.u64;
        memcpy(elem, &x, sizeof(x));
        break;
    }
    case sizeof(uint16_t): {
        uint16_t x = (uint16_t)v->via.u64;
        memcpy(elem, &x, sizeof(x));
        break;
    }
    case sizeof(uint32_t): {
        uint32_t x = (uint32_t)v->via.u64;
        memcpy(elem, &x, sizeof(x));
        break;
    }
    default: {
        uint64_t x = v->via.u64;
        memcpy(elem, &x, sizeof(x));
        break;
    }
    }
    return mpschema_ok;
}

static int get_bytes(const msgpack_object *v, const char **ptr, size_t *len)
{
    if (v->type == MSGPACK_OBJECT_STR) {
        *ptr = v->via.str.ptr;
        *len = v->via.str.size;
        return 1;
    }
    if (v->type == MSGPACK_OBJECT_BIN) {
        *ptr = v->via.bin.ptr;
        *len = v->via.bin.size;
        return 1;
    }
    return 0;
}

static int store(void *elem, const void *src, size_t n, const void *end)
{
    int rc = check_space(elem, n, end);

    if (rc == mpschema_ok) {
        memcpy(elem, src, n);
    }
    return rc;
}

static int decode_bool(const mpschema_member_t *m, void *elem, const msgpack_object *v,
                       const void *end)
{
    uint8_t x;

    if (v->type == MSGPACK_OBJECT_BOOLEAN) {
        x = v->via.boolean ? 1 : 0;
    } else if (v->type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
        x = v->via.u64 != 0 ? 1 : 0; /* older encoders sent bools as uint8 */
    } else {
        return skip_value(m, v);
    }
    return store(elem, &x, sizeof(x), end);
}

static int decode_double(const mpschema_member_t *m, void *elem, const msgpack_object *v,
                         const void *end)
{
    double x;

    if (v->type != MSGPACK_OBJECT_FLOAT64 && v->type != MSGPACK_OBJECT_FLOAT32) {
        return skip_value(m, v);
    }
    x = v->via.f64;
    return store(elem, &x, sizeof(x), end);
}

static int decode_fixed_string(const mpschema_member_t *m, void *elem, const msgpack_object *v,
                               const void *end)
{
    const char *str = NULL;
    size_t len = 0;
    int rc;

    if (!get_bytes(v, &str, &len)) {
        return skip_value(m, v);
    }
    if (m->sm_elem_size == 0) {
        return mpschema_malformed;
    }
    rc = check_space(elem, m->sm_elem_size, end);
    if (rc != mpschema_ok) {
        return rc;
    }
    len = MPS_MIN(len, m->sm_elem_size - 1); /* truncate, keeping room for the NUL */
    if (len != 0) {
        memcpy(elem, str, len);
    }
    ((char *)elem)[len] = '\0';
    return mpschema_ok;
}

static int decode_varlen_string(const mpschema_member_t *m, void *elem, const msgpack_object *v,
                                const void *end, mpschema_buffer_t *arena)
{
    const char *str = NULL;
    char *copy = NULL;
    size_t len = 0;
    int rc;

    if (!get_bytes(v, &str, &len)) {
        return skip_value(m, v);
    }
    if (arena == NULL) {
        return mpschema_invalid_param;
    }
    rc = check_space(elem, sizeof(char *), end);
    if (rc != mpschema_ok) {
        return rc;
    }
    if (m->sm_elem_size != 0) {
        len = MPS_MIN(len, m->sm_elem_size - 1);
    }
    if (len != 0) {
        copy = mpschema_buffer_strdup(arena, str, len);
        if (copy == NULL) {
            return mpschema_no_space;
        }
    }
    memcpy(elem, (const void *)&copy, sizeof(copy));
    return mpschema_ok;
}

/*
 * Decodes one value into one element. Returns mpschema_ok, MPS_SKIPPED for a
 * value that does not fit the member, or a negative error.
 */
static int decode_value(const mpschema_member_t *m, void *elem, const msgpack_object *v,
                        const void *end, mpschema_buffer_t *arena)
{
    switch (m->sm_type) {
    case SCHEMA_TAG_BOOL:
        return decode_bool(m, elem, v, end);
    case SCHEMA_TAG_UINT8:
        return decode_uint(m, elem, v, end, sizeof(uint8_t), UINT8_MAX);
    case SCHEMA_TAG_UINT16:
        return decode_uint(m, elem, v, end, sizeof(uint16_t), UINT16_MAX);
    case SCHEMA_TAG_UINT32:
        return decode_uint(m, elem, v, end, sizeof(uint32_t), UINT32_MAX);
    case SCHEMA_TAG_UINT64:
        return decode_uint(m, elem, v, end, sizeof(uint64_t), UINT64_MAX);
    case SCHEMA_TAG_DOUBLE:
        return decode_double(m, elem, v, end);
    case SCHEMA_TAG_STRING:
        return decode_fixed_string(m, elem, v, end);
    case SCHEMA_TAG_VARLEN_STRING:
        return decode_varlen_string(m, elem, v, end, arena);
    case SCHEMA_TAG_NESTED:
    case SCHEMA_TAG_REFERS:
        if (v->type != MSGPACK_OBJECT_MAP) {
            return skip_value(m, v);
        }
        return mps_decode_object(m->sm_nested, elem, v, end, arena);
    default:
        return mpschema_malformed;
    }
}

/* For REFERS members: allocates the pointee(s) in the arena and links them. */
static int alloc_refers(const mpschema_member_t *m, void *obj, size_t nelem, const void *end,
                        mpschema_buffer_t *arena)
{
    unsigned char *slot = mps_member_data(m, obj);
    void *mem = NULL;
    int rc;

    if (arena == NULL) {
        return mpschema_invalid_param;
    }
    rc = check_space(slot, sizeof(void *), end);
    if (rc != mpschema_ok) {
        return rc;
    }
    if (nelem != 0) {
        mem = mpschema_buffer_calloc(arena, nelem, m->sm_nested_size);
        if (mem == NULL) {
            return mpschema_no_space;
        }
    }
    memcpy(slot, (const void *)&mem, sizeof(mem));
    return mpschema_ok;
}

static int decode_member(const mpschema_member_t *m, void *obj, const msgpack_object *val,
                         const void *end, mpschema_buffer_t *arena)
{
    const int is_array = val->type == MSGPACK_OBJECT_ARRAY;
    const size_t n_wire = is_array ? val->via.array.size : 1;
    const size_t n = MPS_MIN(n_wire, m->sm_max_elems);
    uint16_t *ct = mps_member_ct(m, obj);
    uint8_t *set_flag = mps_member_set_flag(m, obj);
    size_t stored = 0;
    size_t i;
    int rc;

    if (ct != NULL) {
        rc = check_space(ct, sizeof(*ct), end);
        if (rc != mpschema_ok) {
            return rc;
        }
    }
    if (set_flag != NULL) {
        rc = check_space(set_flag, sizeof(*set_flag), end);
        if (rc != mpschema_ok) {
            return rc;
        }
    }

    if (m->sm_type == SCHEMA_TAG_REFERS) {
        rc = alloc_refers(m, obj, n, end, arena);
        if (rc != mpschema_ok) {
            return rc;
        }
    }

    /* elements are positional: a skipped value leaves its slot untouched */
    for (i = 0; i < n; i++) {
        void *elem = mps_member_elem(m, obj, i, NULL);
        const msgpack_object *item = is_array ? &val->via.array.ptr[i] : val;
        if (elem == NULL) {
            break;
        }
        rc = decode_value(m, elem, item, end, arena);
        if (rc < 0) {
            return rc;
        }
        if (rc == mpschema_ok) {
            stored++;
        }
    }

    if (ct != NULL) {
        *ct = (uint16_t)MPS_MIN(i, (size_t)UINT16_MAX);
    }
    if (set_flag != NULL && stored != 0) {
        *set_flag = 1;
    }
    if (n_wire > n) {
        MPS_DEBUG("[mpschema_decode] tag %u (%s): %zu elements truncated to %zu\n",
                  (unsigned)m->sm_tag, m->sm_name ? m->sm_name : "?", n_wire, n);
    }
    return mpschema_ok;
}

int mps_decode_object(const mpschema_t *schema, void *obj, const msgpack_object *mpobj,
                      const void *obj_end, mpschema_buffer_t *arena)
{
    const size_t n_members = mps_schema_member_count(schema);
    const int has_objtype = MPSCHEMA_HAS_BUILTIN_OBJTYPE(schema);
    uint32_t i;
    int rc;

    if (mpobj->type != MSGPACK_OBJECT_MAP) {
        return mpschema_malformed;
    }

    for (i = 0; i < mpobj->via.map.size; i++) {
        const msgpack_object *key = &mpobj->via.map.ptr[i].key;
        const msgpack_object *val = &mpobj->via.map.ptr[i].val;
        const mpschema_member_t *m;

        if (key->type != MSGPACK_OBJECT_POSITIVE_INTEGER || key->via.u64 >= n_members) {
            continue; /* unknown tag: newer peer or exported object */
        }
        if (has_objtype && key->via.u64 == MPSCHEMAV2_OBJTYPE_TAG) {
            continue;
        }
        m = &schema->mps_members[key->via.u64];
        if (is_mpschema_obsolete(m) || mps_member_data(m, obj) == NULL) {
            continue;
        }
        rc = decode_member(m, obj, val, obj_end, arena);
        if (rc != mpschema_ok) {
            return rc;
        }
    }
    return mpschema_ok;
}

int mps_decode_buffer(const mpschema_t *schema, void *obj, size_t obj_sz, const char *buf,
                      size_t sz)
{
    msgpack_unpacked unpacked;
    mpschema_buffer_t arena;
    mpschema_buffer_t *arena_ptr = NULL;
    const char *obj_end = NULL;
    size_t off = 0;
    int rc;

    if (schema == NULL || obj == NULL || buf == NULL) {
        return mpschema_invalid_param;
    }

    memset(&arena, 0, sizeof(arena));
    if (obj_sz != 0) {
        if (obj_sz < schema->mps_size) {
            return mpschema_no_space;
        }
        obj_end = (const char *)obj + obj_sz;
        if (schema->mps_size != 0) {
            arena.data = (char *)obj + schema->mps_size;
            arena.alloced = obj_sz - schema->mps_size;
            arena_ptr = &arena;
        }
    }

    /* bound msgpack-c's allocations by the input size before parsing */
    if (mps_msgpack_scan(buf, sz, &off) != mpschema_ok || off != sz) {
        return mpschema_malformed;
    }
    off = 0;

    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, buf, sz, &off) != MSGPACK_UNPACK_SUCCESS || off != sz) {
        rc = mpschema_malformed;
    } else {
        rc = mps_decode_object(schema, obj, &unpacked.data, obj_end, arena_ptr);
    }
    msgpack_unpacked_destroy(&unpacked);
    return rc;
}
