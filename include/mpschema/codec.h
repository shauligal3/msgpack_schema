/* SPDX-License-Identifier: MIT */
/**
 * @file codec.h
 * @brief One-shot encoding and decoding of a C struct to and from a buffer.
 *
 * ### Encoding rules
 * - An object is encoded as a MessagePack map of `tag -> value`.
 * - Encoding is *sparse*: scalar members equal to zero, empty strings, NULL
 *   pointers and arrays with a zero counter are omitted. On decode, omitted
 *   members keep whatever the caller left in the struct, so zero-initialise
 *   the output struct first.
 * - Members with `sm_max_elems > 1` are encoded as MessagePack arrays.
 *
 * ### Decoding rules
 * - Unknown tags are ignored (forward compatibility).
 * - Values whose MessagePack type does not match the member type, or integers
 *   that do not fit the member, are skipped and reported through the debug
 *   log hook (see log.h).
 * - Arrays longer than the member capacity are truncated.
 * - Strings longer than a fixed STRING member are truncated.
 */
#ifndef MPSCHEMA_CODEC_H
#define MPSCHEMA_CODEC_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Checks the schema invariants: each non-obsolete member's tag equals
 *        its index, and each member type has the data it needs (string
 *        capacity, nested schema, nested element size).
 * @return ::mpschema_ok, ::mpschema_invalid_param or ::mpschema_malformed
 */
int mpschema_validate(const mpschema_t *schema);

/**
 * @brief Encodes @p in_obj into @p out_buf using numeric tags as map keys.
 *
 * @param in_obj  the struct to encode
 * @param schema  its schema
 * @param out_buf output buffer
 * @param sz      in: capacity of @p out_buf; out: bytes written
 * @return ::mpschema_ok, ::mpschema_invalid_param, ::mpschema_no_space or
 *         ::mpschema_malformed
 */
int mpschema_encode_obj(const void *in_obj, const mpschema_t *schema, char *out_buf, size_t *sz);

/**
 * @brief Like mpschema_encode_obj() but uses member *names* as map keys.
 *
 * The result is self-describing and meant for external consumers (log
 * pipelines, scripts); this library's decoder only understands numeric tags.
 * An object with no present members produces zero bytes.
 */
int mpschema_export_obj(const void *in_obj, const mpschema_t *schema, char *out_buf, size_t *sz);

/**
 * @brief Decodes a single MessagePack map from @p in_buf into @p out_obj.
 *
 * No bounds information is available, so the schema must not contain
 * arena-backed members (VARLEN_STRING, REFERS); use
 * mpschema_decode_varlen_obj() for those.
 *
 * @return ::mpschema_ok, ::mpschema_invalid_param or ::mpschema_malformed
 *         (also returned for trailing bytes after the map)
 */
int mpschema_decode_obj(void *out_obj, const mpschema_t *schema, const char *in_buf, size_t sz);

/**
 * @brief Decodes into a buffer larger than the struct, using the extra space
 *        as an arena for VARLEN_STRING and REFERS members.
 *
 * Layout of @p out_obj: `[ struct (schema->mps_size bytes) | arena ... ]`.
 * Every write is bounds-checked against `out_obj + out_obj_sz`.
 *
 * @return ::mpschema_ok, ::mpschema_no_space if the struct or its
 *         variable-length data does not fit, or another error code
 */
int mpschema_decode_varlen_obj(void *out_obj, size_t out_obj_sz, const mpschema_t *schema,
                               const char *in_buf, size_t sz);

#ifdef __cplusplus
}
#endif

#endif /* MPSCHEMA_CODEC_H */
