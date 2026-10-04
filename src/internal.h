/* SPDX-License-Identifier: MIT */
/**
 * @file internal.h
 * @brief Declarations shared by the library sources. Not installed.
 */
#ifndef MPSCHEMA_INTERNAL_H
#define MPSCHEMA_INTERNAL_H

#include <msgpack.h>
#include <stddef.h>
#include <stdint.h>

#include "mpschema/mpschema.h"

/* ------------------------------------------------------------------------ */
/* Logging                                                                  */
/* ------------------------------------------------------------------------ */

extern mpschema_debug_func_t g_mpschema_debug_func;
extern mpschema_warning_func_t g_mpschema_warning_func;

/* The format string is part of __VA_ARGS__ so these stay ISO C compliant. */
#define MPS_DEBUG(...)                                                                             \
    do {                                                                                           \
        if (g_mpschema_debug_func) {                                                               \
            g_mpschema_debug_func(__VA_ARGS__);                                                    \
        }                                                                                          \
    } while (0)

#define MPS_WARNING(...)                                                                           \
    do {                                                                                           \
        if (g_mpschema_warning_func) {                                                             \
            g_mpschema_warning_func(__VA_ARGS__);                                                  \
        }                                                                                          \
    } while (0)

#define MPS_MIN(a, b) ((a) < (b) ? (a) : (b))

/* ------------------------------------------------------------------------ */
/* Member access (schema.c)                                                 */
/* ------------------------------------------------------------------------ */

/**
 * Drops const from a pointer. The member accessors below are shared by the
 * encoder (read-only object) and the decoder (writable object), so they take
 * a const object and return mutable pointers; only the decoder writes.
 */
static inline void *mps_deconst(const void *p)
{
    union {
        const void *in;
        void *out;
    } u;

    u.in = p;
    return u.out;
}

/** Pointer to the member's storage inside @p obj, or NULL if it has none. */
static inline unsigned char *mps_member_data(const mpschema_member_t *m, const void *obj)
{
    return (m->sm_flags & SM_HAS_DATA_OFF) ? (unsigned char *)mps_deconst(obj) + m->sm_data_off
                                           : NULL;
}

/** Pointer to the member's element counter, or NULL if it has none. */
static inline uint16_t *mps_member_ct(const mpschema_member_t *m, const void *obj)
{
    return (m->sm_flags & SM_HAS_CT_OFF)
               ? (uint16_t *)((unsigned char *)mps_deconst(obj) + m->sm_ct_off)
               : NULL;
}

/** Pointer to the member's "value was decoded" flag, or NULL if it has none. */
static inline uint8_t *mps_member_set_flag(const mpschema_member_t *m, const void *obj)
{
    return (m->sm_flags & SM_RECORD_VALUE_SET) ? (uint8_t *)mps_deconst(obj) + m->sm_set_prepend_off
                                               : NULL;
}

/** Number of elements to encode: the counter (clamped to capacity) or sm_max_elems. */
size_t mps_member_elem_count(const mpschema_member_t *m, const void *obj);

/** Number of members up to (not including) the EOF terminator. */
size_t mps_schema_member_count(const mpschema_t *schema);

/**
 * Address of element @p pos of member @p m inside @p obj, or NULL if the
 * member has no storage, @p pos is past the capacity, or a REFERS pointer is
 * NULL. If @p exists is non-NULL, @p pos is also checked against the element
 * counter and *exists tells whether the value would be encoded (non-zero,
 * non-empty, or a nested object with at least one present member).
 */
void *mps_member_elem(const mpschema_member_t *m, const void *obj, size_t pos, int *exists);

/** Whether member @p m of @p obj would be emitted by the encoder. */
int mps_member_is_present(const mpschema_member_t *m, const void *obj);

/** Number of members of @p obj that would be emitted by the encoder. */
size_t mps_count_present(const mpschema_t *schema, const void *obj);

/* ------------------------------------------------------------------------ */
/* Encoder (encode.c)                                                       */
/* ------------------------------------------------------------------------ */

/** Encoder options. */
typedef struct mps_encode_opts_ {
    msgpack_packer_write writer; /**< msgpack-style sink. */
    void *writer_arg;            /**< Argument for @ref writer. */
    uint16_t obj_type;           /**< If non-zero, emitted under tag 0 (v2). */
    int export_names;            /**< Use member names instead of tags as keys. */
} mps_encode_opts_t;

/** Encodes @p obj as one MessagePack map. */
int mps_encode(const mpschema_t *schema, const void *obj, const mps_encode_opts_t *opts);

/* ------------------------------------------------------------------------ */
/* Decoder (decode.c)                                                       */
/* ------------------------------------------------------------------------ */

/**
 * Decodes the map @p mpobj into @p obj.
 * @param obj_end end of the writable region (NULL disables bounds checks)
 * @param arena   arena for VARLEN_STRING and REFERS members, or NULL
 */
int mps_decode_object(const mpschema_t *schema, void *obj, const msgpack_object *mpobj,
                      const void *obj_end, mpschema_buffer_t *arena);

/**
 * Parses @p buf (exactly one MessagePack object) and decodes it into @p obj.
 * If @p obj_sz is non-zero, writes are bounded by it and the bytes after
 * `schema->mps_size` serve as the arena.
 */
int mps_decode_buffer(const mpschema_t *schema, void *obj, size_t obj_sz, const char *buf,
                      size_t sz);

/* ------------------------------------------------------------------------ */
/* Framing scanner (scan.c)                                                 */
/* ------------------------------------------------------------------------ */

/**
 * Walks one MessagePack object at the start of @p buf without allocating.
 * @param obj_len out: size of the object in bytes (when complete)
 * @return ::mpschema_ok when the object is complete, ::mpschema_incomplete if
 *         more bytes are needed, ::mpschema_malformed on an invalid type byte
 */
int mps_msgpack_scan(const char *buf, size_t len, size_t *obj_len);

#endif /* MPSCHEMA_INTERNAL_H */
