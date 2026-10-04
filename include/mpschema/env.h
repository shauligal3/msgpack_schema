/* SPDX-License-Identifier: MIT */
/**
 * @file env.h
 * @brief The v2 API: a per-connection environment for streaming decode and
 *        callback-based encode.
 *
 * v2 objects carry their object type under tag 0, so a byte stream can be
 * decoded without a framing header:
 *
 * @code
 * mpschema_env_t *env = mpschema_env_init(NULL, NULL);
 *
 * // whenever bytes arrive:
 * mpschema_env_feed(env, chunk, chunk_len);
 * uint16_t type;
 * while (mpschema_env_next(env, &type) == mpschema_ok) {
 *     const mpschema_t *schema = lookup_schema(type);  // application-defined
 *     my_struct_t obj = {0};
 *     mpschema_env_next_decode(env, schema, &obj, sizeof(obj));
 * }
 *
 * mpschema_env_destroy(env);
 * @endcode
 */
#ifndef MPSCHEMA_ENV_H
#define MPSCHEMA_ENV_H

#include <stddef.h>
#include <stdint.h>

#include "log.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum number of bytes an environment buffers. A single object larger than
 * this cannot be decoded; mpschema_env_feed() refuses data beyond it.
 */
#define MPSCHEMA_ENV_MAX_BUFFERED ((size_t)1024 * 1024)

/** Opaque environment, one per connection. */
typedef struct mpschema_env_ mpschema_env_t;

/**
 * @brief An output sink for mpschema_env_encode().
 * @param writer_arg the `out_buf` argument given to mpschema_env_encode()
 * @return 0 on success, non-zero to abort encoding
 */
typedef int (*mpschema_writer_cback_t)(void *writer_arg, const char *buf, size_t len);

/** Callbacks used to customise an environment. All optional. */
typedef struct mpschema_env_cback_ {
    mpschema_writer_cback_t writer_cb;  /**< External writer (requires cfg.external_writer). */
    mpschema_debug_func_t debug_cb;     /**< If set, installed as the process-wide debug hook. */
    mpschema_warning_func_t warning_cb; /**< If set, installed as the process-wide warning hook. */
} mpschema_env_cback_t;

/** Configuration of an environment. */
typedef struct mpschema_env_cfg_ {
    uint8_t external_writer : 1;     /**< Encode through cback.writer_cb instead of a buffer. */
    uint8_t dynamic_decoded_obj : 1; /**< Reserved: variable-length decoding (unsupported). */
} mpschema_env_cfg_t;

/**
 * @brief Creates an environment.
 * @param cfg   configuration, or NULL for defaults
 * @param cback callbacks, or NULL
 * @return the environment, or NULL if the configuration is inconsistent
 *         (external_writer without writer_cb or vice versa, or an
 *         unsupported option) or memory is exhausted
 * @see mpschema_env_destroy()
 */
mpschema_env_t *mpschema_env_init(const mpschema_env_cfg_t *cfg, const mpschema_env_cback_t *cback);

/**
 * @brief Parses one complete v2 object from @p in_buf and returns its type.
 *
 * The parsed object refers to @p in_buf, which must stay valid until
 * mpschema_env_decode_one() is called.
 *
 * @return ::mpschema_ok or ::mpschema_malformed
 */
int mpschema_env_decode_type(mpschema_env_t *env, const char *in_buf, size_t sz,
                             uint16_t *obj_type);

/**
 * @brief Decodes the object parsed by mpschema_env_decode_type().
 * @param obj output struct of @p sz bytes
 * @return ::mpschema_ok, ::mpschema_not_found if nothing was parsed,
 *         ::mpschema_unsupported for non-v2 schemas, or a decode error
 */
int mpschema_env_decode_one(mpschema_env_t *env, const mpschema_t *schema, void *obj, size_t sz);

/**
 * @brief Appends a chunk of the incoming byte stream. Chunks need not be
 *        aligned with object boundaries.
 *
 * Nothing is parsed until a whole object has arrived, and a whole object is
 * checked by a non-allocating scanner first, so a peer cannot make the
 * decoder reserve memory for data it never sends.
 *
 * @return ::mpschema_ok, ::mpschema_invalid_param, ::mpschema_no_mem, or
 *         ::mpschema_no_space if more than ::MPSCHEMA_ENV_MAX_BUFFERED bytes
 *         would be buffered
 */
int mpschema_env_feed(mpschema_env_t *env, const char *in_buf, size_t in_sz);

/**
 * @brief Extracts the next complete object from the stream, if any.
 *
 * Any previously extracted object that was not decoded is discarded.
 *
 * @param out_obj_type out: type of the next object
 * @return ::mpschema_ok, ::mpschema_not_found if no complete object is
 *         buffered yet, ::mpschema_no_mem, or ::mpschema_malformed. A
 *         well-formed object that is not a v2 object is dropped and the stream
 *         continues; invalid MessagePack discards everything buffered, since
 *         object boundaries can no longer be found.
 */
int mpschema_env_next(mpschema_env_t *env, uint16_t *out_obj_type);

/**
 * @brief Returns the buffer size needed to decode the pending object.
 * @return ::mpschema_ok, ::mpschema_invalid_param or ::mpschema_not_found
 */
int mpschema_env_next_obj_size(mpschema_env_t *env, const mpschema_t *schema, size_t *out_sz);

/**
 * @brief Decodes the pending object into @p out_obj and releases it.
 * @param out_sz size of @p out_obj; writes beyond it are refused
 * @return ::mpschema_ok, ::mpschema_no_space, ::mpschema_not_found,
 *         ::mpschema_unsupported or another decode error
 */
int mpschema_env_next_decode(mpschema_env_t *env, const mpschema_t *schema, void *out_obj,
                             size_t out_sz);

/**
 * @brief Encodes a v2 object (object type included under tag 0).
 *
 * With an external writer, @p out_buf is passed to the writer as its
 * argument; otherwise it is a byte buffer of `*inout_sz` bytes.
 *
 * @param inout_sz in: capacity of @p out_buf (ignored with an external
 *                 writer); out: bytes written
 * @return ::mpschema_ok, ::mpschema_invalid_param, ::mpschema_unsupported,
 *         ::mpschema_no_space or ::mpschema_io_error
 */
int mpschema_env_encode(mpschema_env_t *env, const mpschema_t *schema, const void *in_obj,
                        void *out_buf, size_t *inout_sz);

/** @brief Destroys an environment (NULL is allowed). */
void mpschema_env_destroy(mpschema_env_t *env);

#ifdef __cplusplus
}
#endif

#endif /* MPSCHEMA_ENV_H */
