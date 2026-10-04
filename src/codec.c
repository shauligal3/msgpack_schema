/* SPDX-License-Identifier: MIT */
/**
 * @file codec.c
 * @brief Public one-shot encode/decode API.
 */
#include <string.h>

#include "internal.h"

static int encode_to_buffer(const void *obj, const mpschema_t *schema, char *buf, size_t *sz,
                            int export_names)
{
    mpschema_buffer_t out;
    mps_encode_opts_t opts;
    int rc;

    if (obj == NULL || schema == NULL || buf == NULL || sz == NULL) {
        return mpschema_invalid_param;
    }

    /* exported objects carry no type information, so an empty one is omitted */
    if (export_names && mps_count_present(schema, obj) == 0) {
        *sz = 0;
        return mpschema_ok;
    }

    memset(&out, 0, sizeof(out));
    out.data = buf;
    out.alloced = *sz;

    memset(&opts, 0, sizeof(opts));
    opts.writer = mpschema_buffer_write;
    opts.writer_arg = &out;
    /*
     * v1 encoding never writes the object type: some v1 schemas use tag 0 for
     * an ordinary member.
     */
    opts.obj_type = 0;
    opts.export_names = export_names;

    rc = mps_encode(schema, obj, &opts);
    *sz = out.used;
    return rc;
}

int mpschema_encode_obj(const void *in_obj, const mpschema_t *schema, char *out_buf, size_t *sz)
{
    return encode_to_buffer(in_obj, schema, out_buf, sz, 0);
}

int mpschema_export_obj(const void *in_obj, const mpschema_t *schema, char *out_buf, size_t *sz)
{
    return encode_to_buffer(in_obj, schema, out_buf, sz, 1);
}

int mpschema_decode_obj(void *out_obj, const mpschema_t *schema, const char *in_buf, size_t sz)
{
    return mps_decode_buffer(schema, out_obj, 0, in_buf, sz);
}

int mpschema_decode_varlen_obj(void *out_obj, size_t out_obj_sz, const mpschema_t *schema,
                               const char *in_buf, size_t sz)
{
    if (out_obj_sz == 0) {
        return mpschema_invalid_param;
    }
    return mps_decode_buffer(schema, out_obj, out_obj_sz, in_buf, sz);
}
