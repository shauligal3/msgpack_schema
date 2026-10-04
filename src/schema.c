/* SPDX-License-Identifier: MIT */
/**
 * @file schema.c
 * @brief Schema validation and member access helpers shared by the encoder,
 *        decoder and inspection code.
 */
#include <string.h>

#include "internal.h"

size_t mps_schema_member_count(const mpschema_t *schema)
{
    size_t n = 0;

    while (!is_mpschema_eof(&schema->mps_members[n])) {
        n++;
    }
    return n;
}

size_t mps_member_elem_count(const mpschema_member_t *m, const void *obj)
{
    const uint16_t *ct = mps_member_ct(m, obj);

    if (ct == NULL) {
        return m->sm_max_elems;
    }
    /* never trust the counter beyond the declared capacity */
    return MPS_MIN((size_t)*ct, m->sm_max_elems);
}

/*
 * Scalars are read through memcpy so that the checks stay well-defined even if
 * a caller hands us storage that is not suitably aligned.
 */
#define MPS_DEFINE_NONZERO(name, type)                                                             \
    static int name(const void *p)                                                                 \
    {                                                                                              \
        type v;                                                                                    \
        memcpy(&v, p, sizeof(v));                                                                  \
        return v != 0;                                                                             \
    }

MPS_DEFINE_NONZERO(u8_nonzero, uint8_t)
MPS_DEFINE_NONZERO(u16_nonzero, uint16_t)
MPS_DEFINE_NONZERO(u32_nonzero, uint32_t)
MPS_DEFINE_NONZERO(u64_nonzero, uint64_t)
MPS_DEFINE_NONZERO(double_nonzero, double)

void *mps_member_elem(const mpschema_member_t *m, const void *obj, size_t pos, int *exists)
{
    unsigned char *base = mps_member_data(m, obj);
    unsigned char *elem = NULL;
    unsigned char *nested;
    int present = 0;

    if (exists != NULL) {
        *exists = 0;
    }
    if (base == NULL || is_mpschema_obsolete(m)) {
        return NULL;
    }
    if (pos >= (exists != NULL ? mps_member_elem_count(m, obj) : m->sm_max_elems)) {
        return NULL;
    }

    switch (m->sm_type) {
    case SCHEMA_TAG_BOOL:
    case SCHEMA_TAG_UINT8:
        elem = base + pos;
        present = u8_nonzero(elem);
        break;
    case SCHEMA_TAG_UINT16:
        elem = base + (pos * sizeof(uint16_t));
        present = u16_nonzero(elem);
        break;
    case SCHEMA_TAG_UINT32:
        elem = base + (pos * sizeof(uint32_t));
        present = u32_nonzero(elem);
        break;
    case SCHEMA_TAG_UINT64:
        elem = base + (pos * sizeof(uint64_t));
        present = u64_nonzero(elem);
        break;
    case SCHEMA_TAG_DOUBLE:
        elem = base + (pos * sizeof(double));
        present = double_nonzero(elem);
        break;
    case SCHEMA_TAG_STRING:
        elem = base + (pos * m->sm_elem_size);
        present = m->sm_elem_size != 0 && elem[0] != '\0';
        break;
    case SCHEMA_TAG_VARLEN_STRING: {
        const char *s;
        elem = base + (pos * sizeof(char *));
        memcpy((void *)&s, elem, sizeof(s));
        present = s != NULL;
        break;
    }
    case SCHEMA_TAG_NESTED:
    case SCHEMA_TAG_REFERS:
        if (m->sm_type == SCHEMA_TAG_REFERS) {
            memcpy((void *)&nested, base, sizeof(nested));
        } else {
            nested = base;
        }
        if (nested == NULL) {
            return NULL;
        }
        elem = nested + (pos * m->sm_nested_size);
        present = m->sm_nested != NULL && mps_count_present(m->sm_nested, elem) != 0;
        break;
    default:
        return NULL;
    }

    if (exists != NULL) {
        *exists = present;
    }
    return elem;
}

int mps_member_is_present(const mpschema_member_t *m, const void *obj)
{
    int exists = 0;

    if (is_mpschema_obsolete(m) || mps_member_data(m, obj) == NULL) {
        return 0;
    }
    if (mps_member_ct(m, obj) != NULL) {
        /* arrays: present when non-empty, even if individual elements are zero */
        return mps_member_elem_count(m, obj) != 0 && mps_member_elem(m, obj, 0, NULL) != NULL;
    }
    return mps_member_elem(m, obj, 0, &exists) != NULL && exists;
}

size_t mps_count_present(const mpschema_t *schema, const void *obj)
{
    const mpschema_member_t *m;
    size_t n = 0;

    if (schema == NULL || obj == NULL) {
        return 0;
    }
    for (m = schema->mps_members; !is_mpschema_eof(m); m++) {
        if (mps_member_is_present(m, obj)) {
            n++;
        }
    }
    return n;
}

static int validate_member(const mpschema_member_t *m)
{
    if (!(m->sm_flags & SM_HAS_DATA_OFF)) {
        /* descriptor-only entries (e.g. the v2 object type slot) are never encoded */
        return mpschema_ok;
    }
    if (m->sm_max_elems == 0) {
        return mpschema_malformed;
    }
    switch (m->sm_type) {
    case SCHEMA_TAG_BOOL:
    case SCHEMA_TAG_UINT8:
    case SCHEMA_TAG_UINT16:
    case SCHEMA_TAG_UINT32:
    case SCHEMA_TAG_UINT64:
    case SCHEMA_TAG_DOUBLE:
    case SCHEMA_TAG_VARLEN_STRING:
        return mpschema_ok;
    case SCHEMA_TAG_STRING:
        return m->sm_elem_size != 0 ? mpschema_ok : mpschema_malformed;
    case SCHEMA_TAG_NESTED:
    case SCHEMA_TAG_REFERS:
        if (m->sm_nested == NULL) {
            return mpschema_malformed;
        }
        if ((m->sm_max_elems > 1 || m->sm_type == SCHEMA_TAG_REFERS) && m->sm_nested_size == 0) {
            return mpschema_malformed;
        }
        return mpschema_validate(m->sm_nested);
    default:
        return mpschema_malformed;
    }
}

int mpschema_validate(const mpschema_t *schema)
{
    const mpschema_member_t *m;
    size_t i = 0;
    int rc;

    if (schema == NULL) {
        return mpschema_invalid_param;
    }
    for (m = schema->mps_members; !is_mpschema_eof(m); m++, i++) {
        if (is_mpschema_obsolete(m)) {
            continue;
        }
        if (m->sm_tag != i) {
            MPS_DEBUG("[%s] schema %s: member #%zu has tag %u\n", __func__,
                      schema->mps_name ? schema->mps_name : "?", i, (unsigned)m->sm_tag);
            return mpschema_malformed;
        }
        rc = validate_member(m);
        if (rc != mpschema_ok) {
            MPS_DEBUG("[%s] schema %s: member %s is malformed\n", __func__,
                      schema->mps_name ? schema->mps_name : "?", m->sm_name ? m->sm_name : "?");
            return rc;
        }
    }
    return mpschema_ok;
}
