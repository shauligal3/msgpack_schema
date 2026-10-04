/* SPDX-License-Identifier: MIT */
/**
 * @file env.c
 * @brief v2 environment: streaming decoder and callback-based encoder.
 */
#include <stdlib.h>
#include <string.h>

#include "internal.h"

/** Initial capacity of the stream buffer. */
#define MPS_ENV_INIT_SZ 2048

/*
 * Bytes are buffered here rather than in msgpack_unpacker so that msgpack-c
 * only ever sees complete objects that mps_msgpack_scan() has checked; see
 * scan.c for why.
 */
struct mpschema_env_ {
    mpschema_env_cfg_t cfg;
    mpschema_env_cback_t cback;
    char *buf;                /* incoming byte stream */
    size_t len;               /* bytes buffered */
    size_t cap;               /* capacity of buf */
    size_t obj_len;           /* bytes of buf backing `decoded` (0 if none) */
    char *pinned;             /* old buf kept alive while `decoded` points into it */
    msgpack_unpacked decoded; /* the object extracted by next()/decode_type() */
    int has_obj;              /* decoded holds a v2 object waiting to be decoded */
};

/* Counts the bytes produced through a user writer callback. */
typedef struct mps_writer_ctx_ {
    mpschema_writer_cback_t writer;
    void *writer_arg;
    size_t used;
} mps_writer_ctx_t;

mpschema_env_t *mpschema_env_init(const mpschema_env_cfg_t *cfg, const mpschema_env_cback_t *cback)
{
    const int wants_writer = cfg != NULL && cfg->external_writer;
    const int has_writer = cback != NULL && cback->writer_cb != NULL;
    mpschema_env_t *env;

    if (wants_writer != has_writer) {
        return NULL; /* an external writer must be both requested and provided */
    }
    if (cfg != NULL && cfg->dynamic_decoded_obj) {
        return NULL; /* not supported yet */
    }

    env = calloc(1, sizeof(*env));
    if (env == NULL) {
        return NULL;
    }
    if (cfg != NULL) {
        env->cfg = *cfg;
    }
    if (cback != NULL) {
        env->cback = *cback;
        if (cback->debug_cb != NULL || cback->warning_cb != NULL) {
            mpschema_set_logging(cback->debug_cb, cback->warning_cb);
        }
    }
    env->buf = malloc(MPS_ENV_INIT_SZ);
    if (env->buf == NULL) {
        free(env);
        return NULL;
    }
    env->cap = MPS_ENV_INIT_SZ;
    msgpack_unpacked_init(&env->decoded);
    return env;
}

void mpschema_env_destroy(mpschema_env_t *env)
{
    if (env == NULL) {
        return;
    }
    msgpack_unpacked_destroy(&env->decoded);
    free(env->pinned);
    free(env->buf);
    free(env);
}

/* Releases the current object and, for streamed objects, consumes its bytes. */
static void release_obj(mpschema_env_t *env)
{
    msgpack_unpacked_destroy(&env->decoded);
    env->has_obj = 0;
    free(env->pinned);
    env->pinned = NULL;
    if (env->obj_len != 0) {
        env->len -= env->obj_len;
        memmove(env->buf, env->buf + env->obj_len, env->len);
        env->obj_len = 0;
    }
}

/* Reads the object type of a v2 object: a map whose first entry is 0 -> type. */
static int get_obj_type(const msgpack_object *obj, uint16_t *obj_type)
{
    const msgpack_object_kv *first;

    if (obj->type != MSGPACK_OBJECT_MAP || obj->via.map.size == 0) {
        return mpschema_malformed;
    }
    first = &obj->via.map.ptr[0];
    if (first->key.type != MSGPACK_OBJECT_POSITIVE_INTEGER ||
        first->key.via.u64 != MPSCHEMAV2_OBJTYPE_TAG ||
        first->val.type != MSGPACK_OBJECT_POSITIVE_INTEGER || first->val.via.u64 > UINT16_MAX) {
        return mpschema_malformed;
    }
    *obj_type = (uint16_t)first->val.via.u64;
    return mpschema_ok;
}

int mpschema_env_decode_type(mpschema_env_t *env, const char *in_buf, size_t sz, uint16_t *obj_type)
{
    size_t off = 0;
    int rc;

    if (env == NULL || in_buf == NULL || obj_type == NULL) {
        return mpschema_invalid_param;
    }
    release_obj(env);
    if (mps_msgpack_scan(in_buf, sz, &off) != mpschema_ok) {
        return mpschema_malformed;
    }
    off = 0;
    if (msgpack_unpack_next(&env->decoded, in_buf, sz, &off) != MSGPACK_UNPACK_SUCCESS) {
        release_obj(env);
        return mpschema_malformed;
    }
    rc = get_obj_type(&env->decoded.data, obj_type);
    if (rc != mpschema_ok) {
        release_obj(env);
        return rc;
    }
    env->has_obj = 1;
    return mpschema_ok;
}

int mpschema_env_decode_one(mpschema_env_t *env, const mpschema_t *schema, void *obj, size_t sz)
{
    if (env == NULL || schema == NULL || obj == NULL) {
        return mpschema_invalid_param;
    }
    if (schema->mps_version < MPSCHEMAV2) {
        return mpschema_unsupported;
    }
    if (!env->has_obj) {
        return mpschema_not_found;
    }
    if (sz < schema->mps_size) {
        return mpschema_no_space;
    }
    return mps_decode_object(schema, obj, &env->decoded.data, (const char *)obj + sz, NULL);
}

int mpschema_env_feed(mpschema_env_t *env, const char *in_buf, size_t in_sz)
{
    if (env == NULL || (in_buf == NULL && in_sz != 0)) {
        return mpschema_invalid_param;
    }
    if (in_sz == 0) {
        return mpschema_ok;
    }
    if (in_sz > MPSCHEMA_ENV_MAX_BUFFERED || env->len > MPSCHEMA_ENV_MAX_BUFFERED - in_sz) {
        return mpschema_no_space;
    }
    if (env->len + in_sz > env->cap) {
        size_t cap = env->cap;
        char *grown;
        while (cap < env->len + in_sz) {
            cap *= 2;
        }
        if (env->obj_len != 0 && env->pinned == NULL) {
            /*
             * The pending object refers into buf, so it must not move: copy
             * into a new buffer and keep the old one until the object is
             * released.
             */
            grown = malloc(cap);
            if (grown == NULL) {
                return mpschema_no_mem;
            }
            memcpy(grown, env->buf, env->len);
            env->pinned = env->buf;
        } else {
            grown = realloc(env->buf, cap);
            if (grown == NULL) {
                return mpschema_no_mem;
            }
        }
        env->buf = grown;
        env->cap = cap;
    }
    memcpy(env->buf + env->len, in_buf, in_sz);
    env->len += in_sz;
    return mpschema_ok;
}

int mpschema_env_next(mpschema_env_t *env, uint16_t *out_obj_type)
{
    size_t obj_len = 0;
    size_t off = 0;
    int rc;

    if (env == NULL || out_obj_type == NULL) {
        return mpschema_invalid_param;
    }
    *out_obj_type = 0;
    release_obj(env); /* drop an object the caller chose not to decode */

    switch (mps_msgpack_scan(env->buf, env->len, &obj_len)) {
    case mpschema_ok:
        break;
    case mpschema_incomplete:
        if (env->len >= MPSCHEMA_ENV_MAX_BUFFERED) {
            env->len = 0; /* an object can never fit: resynchronising is impossible */
            return mpschema_malformed;
        }
        return mpschema_not_found;
    default:
        env->len = 0; /* framing is lost: discard the stream */
        return mpschema_malformed;
    }

    /* from here on, the object's bytes are consumed by release_obj() */
    env->obj_len = obj_len;
    switch (msgpack_unpack_next(&env->decoded, env->buf, obj_len, &off)) {
    case MSGPACK_UNPACK_SUCCESS:
        break;
    case MSGPACK_UNPACK_NOMEM_ERROR:
        release_obj(env);
        return mpschema_no_mem;
    default:
        release_obj(env);
        return mpschema_malformed;
    }

    rc = get_obj_type(&env->decoded.data, out_obj_type);
    if (rc != mpschema_ok) {
        release_obj(env);
        return rc;
    }
    env->has_obj = 1;
    return mpschema_ok;
}

int mpschema_env_next_obj_size(mpschema_env_t *env, const mpschema_t *schema, size_t *out_sz)
{
    if (env == NULL || schema == NULL || out_sz == NULL) {
        return mpschema_invalid_param;
    }
    *out_sz = 0;
    if (!env->has_obj) {
        return mpschema_not_found;
    }
    /* objects are fixed-size until dynamic decoding is supported */
    *out_sz = schema->mps_size;
    return mpschema_ok;
}

int mpschema_env_next_decode(mpschema_env_t *env, const mpschema_t *schema, void *out_obj,
                             size_t out_sz)
{
    int rc = mpschema_env_decode_one(env, schema, out_obj, out_sz);

    if (env != NULL && rc != mpschema_not_found) {
        release_obj(env);
    }
    return rc;
}

static int writer_adaptor(void *data, const char *buf, size_t len)
{
    mps_writer_ctx_t *ctx = (mps_writer_ctx_t *)data;
    int rc = ctx->writer(ctx->writer_arg, buf, len);

    if (rc == 0) {
        ctx->used += len;
    }
    return rc;
}

int mpschema_env_encode(mpschema_env_t *env, const mpschema_t *schema, const void *in_obj,
                        void *out_buf, size_t *inout_sz)
{
    mps_encode_opts_t opts;
    mps_writer_ctx_t ctx;
    mpschema_buffer_t buf;
    int rc;

    if (inout_sz == NULL) {
        return mpschema_invalid_param;
    }
    if (env == NULL || schema == NULL || in_obj == NULL || out_buf == NULL) {
        *inout_sz = 0;
        return mpschema_invalid_param;
    }
    if (schema->mps_version < MPSCHEMAV2) {
        *inout_sz = 0;
        return mpschema_unsupported;
    }

    memset(&opts, 0, sizeof(opts));
    memset(&ctx, 0, sizeof(ctx));
    memset(&buf, 0, sizeof(buf));
    opts.obj_type = schema->mps_obj_type;

    if (env->cback.writer_cb != NULL) {
        /*
         * Wrap the user callback instead of casting it to msgpack's writer
         * type: the compiler keeps checking both signatures, and we get to
         * count the bytes written.
         */
        ctx.writer = env->cback.writer_cb;
        ctx.writer_arg = out_buf;
        opts.writer = writer_adaptor;
        opts.writer_arg = &ctx;
    } else {
        buf.data = out_buf;
        buf.alloced = *inout_sz;
        opts.writer = mpschema_buffer_write;
        opts.writer_arg = &buf;
    }

    rc = mps_encode(schema, in_obj, &opts);
    *inout_sz = env->cback.writer_cb != NULL ? ctx.used : buf.used;
    return rc;
}
