/* SPDX-License-Identifier: MIT */
/**
 * @file buffer.h
 * @brief A bump-pointer buffer used both as an output sink for the encoder and
 *        as an arena for variable-length data produced by the decoder.
 *
 * The buffer never allocates: it carves space out of memory supplied by the
 * caller and everything is released at once when that memory is.
 */
#ifndef MPSCHEMA_BUFFER_H
#define MPSCHEMA_BUFFER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A caller-owned memory region with a fill level. */
typedef struct mpschema_buffer_ {
    size_t used;    /**< Bytes consumed so far. */
    size_t alloced; /**< Total capacity of @ref data. */
    char *data;     /**< Start of the region. */
} mpschema_buffer_t;

/**
 * @brief Appends @p len bytes, msgpack-writer style.
 *
 * If the buffer is too small, as many bytes as fit are written and
 * ::mpschema_no_space is returned. The signature matches `msgpack_packer_write`,
 * so the function can be handed straight to `msgpack_packer_init()`.
 *
 * @param data a `mpschema_buffer_t *`
 * @param buf  bytes to append
 * @param len  number of bytes
 * @return ::mpschema_ok or ::mpschema_no_space
 */
int mpschema_buffer_write(void *data, const char *buf, size_t len);

/**
 * @brief Copies @p len bytes into the arena and NUL-terminates them.
 * @return the copy, or NULL if the arena does not have `len + 1` free bytes
 *         (in which case the arena is left untouched).
 */
char *mpschema_buffer_strdup(mpschema_buffer_t *arena, const char *buf, size_t len);

/**
 * @brief Allocates a zeroed array of @p nmemb elements of @p size bytes,
 *        aligned for any C object type.
 * @return the allocation, or NULL if it does not fit or the size overflows
 *         (in which case the arena is left untouched).
 */
void *mpschema_buffer_calloc(mpschema_buffer_t *arena, size_t nmemb, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MPSCHEMA_BUFFER_H */
