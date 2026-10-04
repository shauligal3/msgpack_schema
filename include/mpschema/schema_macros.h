/* SPDX-License-Identifier: MIT */
/**
 * @file schema_macros.h
 * @brief Macros for declaring schemas statically, next to the structs they describe.
 *
 * ### Conventions
 *
 * Before using the macros, define `MPSCHEMA_MODULE` to the common prefix of
 * your struct type names. A message called `point` then refers to the struct
 * type `<MPSCHEMA_MODULE>point_t`, and its members are tagged by enumerators
 * named `e_point__<member>`:
 *
 * @code
 * #define MPSCHEMA_MODULE demo_
 *
 * typedef struct demo_point_ {
 *     uint32_t x;
 *     uint32_t y;
 *     char     label[16];
 *     uint16_t samples[8];
 *     uint16_t samples_ct;          // element counter required by arrays
 * } demo_point_t;
 *
 * enum {                            // tags must match the member positions
 *     e_point__objtype = 0,         // v2 schemas reserve tag 0
 *     e_point__x,
 *     e_point__y,
 *     e_point__label,
 *     e_point__samples,
 * };
 *
 * static mpschema_t point_schema = {
 *     MPSCHEMAV2_DEF(point, MSG_POINT)
 *     MPSCHEMA_MEMBER(point, x, UINT32),
 *     MPSCHEMA_MEMBER(point, y, UINT32),
 *     MPSCHEMA_STRING(point, label, sizeof(((demo_point_t *)0)->label)),
 *     MPSCHEMA_ARRAY(point, samples, UINT16, 8),
 *     MPSCHEMA_MEMBER_EOF
 * };
 * @endcode
 *
 * The `*_DEF` macros open the member list and MPSCHEMA_MEMBER_EOF closes it, so
 * every schema is exactly "DEF, members, EOF".
 *
 * @note Statically initialising the flexible `mps_members` array relies on a
 *       GCC/Clang extension; compile with `-std=gnu11` (or any GNU dialect).
 */
#ifndef MPSCHEMA_SCHEMA_MACROS_H
#define MPSCHEMA_SCHEMA_MACROS_H

#include <stddef.h>

#include "types.h"

/** @cond INTERNAL */
#define MPSCHEMA_TOKENPASTE(x, y, z) x##y##z
#define MPSCHEMA_TOKENPASTE2(x, y, z) MPSCHEMA_TOKENPASTE(x, y, z)
#define MPSCHEMA_STRUCT_SUFFIX _t
/** @endcond */

/** The struct type described by message @p msg: `<MPSCHEMA_MODULE><msg>_t`. */
#define MPSCHEMA_MSG_NAME(msg) MPSCHEMA_TOKENPASTE2(MPSCHEMA_MODULE, msg, MPSCHEMA_STRUCT_SUFFIX)
/** Name of the element counter that accompanies array member @p mem. */
#define MPSCHEMA_CT_NAME(mem) mem##_ct

/* clang-format off */

/** @cond INTERNAL */
#define MPSCHEMA_MEMBER_BASE(msg, mem, type)                                   \
    .sm_tag      = e_##msg##__##mem,                                           \
    .sm_name     = #mem,                                                       \
    .sm_name_len = sizeof(#mem) - 1,                                           \
    .sm_type     = SCHEMA_TAG_##type,                                          \
    .sm_data_off = offsetof(MPSCHEMA_MSG_NAME(msg), mem),

#define MPSCHEMA_MEMBER_PLAIN(msg, mem, type)                                  \
    MPSCHEMA_MEMBER_BASE(msg, mem, type)                                       \
    .sm_flags = SM_HAS_DATA_OFF,
/** @endcond */

/**
 * A scalar member: BOOL, UINT8, UINT16, UINT32, UINT64 or DOUBLE.
 * @code MPSCHEMA_MEMBER(point, x, UINT32) @endcode
 */
#define MPSCHEMA_MEMBER(msg, mem, type)                                        \
{                                                                              \
    MPSCHEMA_MEMBER_PLAIN(msg, mem, type)                                      \
    .sm_max_elems = 1,                                                         \
}

/**
 * A scalar member with a companion `uint8_t set_<mem>` flag that the decoder
 * sets to 1 whenever the member was present in the input. Useful when zero is
 * a meaningful value.
 */
#define MPSCHEMA_MEMBER_RECORD_SET(msg, mem, type)                             \
{                                                                              \
    MPSCHEMA_MEMBER_BASE(msg, mem, type)                                       \
    .sm_max_elems       = 1,                                                   \
    .sm_set_prepend_off = offsetof(MPSCHEMA_MSG_NAME(msg), set_##mem),         \
    .sm_flags           = (SM_RECORD_VALUE_SET | SM_HAS_DATA_OFF),             \
}

/** A fixed-size `char mem[sz]` string; at most `sz - 1` characters travel on the wire. */
#define MPSCHEMA_STRING(msg, mem, sz)                                          \
{                                                                              \
    MPSCHEMA_MEMBER_PLAIN(msg, mem, STRING)                                    \
    .sm_elem_size = (sz),                                                      \
    .sm_max_elems = 1,                                                         \
}

/**
 * A `char *mem` string. On encode, at most `sz - 1` characters are sent
 * (`sz == 0` means unlimited). On decode the string is copied into the arena
 * (see mpschema_decode_varlen_obj()).
 */
#define MPSCHEMA_VARLEN_STRING(msg, mem, sz)                                   \
{                                                                              \
    MPSCHEMA_MEMBER_PLAIN(msg, mem, VARLEN_STRING)                             \
    .sm_elem_size = (sz),                                                      \
    .sm_max_elems = 1,                                                         \
}

/** An array of scalars `type mem[max]` with a `uint16_t mem_ct` element counter. */
#define MPSCHEMA_ARRAY(msg, mem, type, max)                                    \
{                                                                              \
    MPSCHEMA_MEMBER_BASE(msg, mem, type)                                       \
    .sm_max_elems = (max),                                                     \
    .sm_ct_off    = offsetof(MPSCHEMA_MSG_NAME(msg), MPSCHEMA_CT_NAME(mem)),   \
    .sm_flags     = SM_HAS_BOTH_OFF,                                           \
}

/** An embedded struct described by schema pointer @p nested. */
#define MPSCHEMA_NESTED(msg, mem, nested)                                      \
{                                                                              \
    MPSCHEMA_MEMBER_BASE(msg, mem, NESTED)                                     \
    .sm_max_elems = 1,                                                         \
    .sm_flags     = SM_HAS_DATA_OFF,                                           \
    .sm_nested    = (nested),                                                  \
}

/**
 * A pointer to a struct of type `MPSCHEMA_MSG_NAME(nested_msg)`. On decode the
 * pointee is allocated from the arena.
 */
#define MPSCHEMA_REFERS(msg, mem, nested, nested_msg)                          \
{                                                                              \
    MPSCHEMA_MEMBER_BASE(msg, mem, REFERS)                                     \
    .sm_max_elems   = 1,                                                       \
    .sm_flags       = SM_HAS_DATA_OFF,                                         \
    .sm_nested      = (nested),                                                \
    .sm_nested_size = sizeof(MPSCHEMA_MSG_NAME(nested_msg)),                   \
}

/**
 * A pointer to an array of up to @p max structs, with a `uint16_t mem_ct`
 * counter. On decode the array is allocated from the arena.
 */
#define MPSCHEMA_NREFER(msg, mem, nested, nested_msg, max)                     \
{                                                                              \
    MPSCHEMA_MEMBER_BASE(msg, mem, REFERS)                                     \
    .sm_max_elems   = (max),                                                   \
    .sm_flags       = SM_HAS_BOTH_OFF,                                         \
    .sm_nested      = (nested),                                                \
    .sm_nested_size = sizeof(MPSCHEMA_MSG_NAME(nested_msg)),                   \
    .sm_ct_off      = offsetof(MPSCHEMA_MSG_NAME(msg), MPSCHEMA_CT_NAME(mem)), \
}

/** An embedded array of up to @p max structs, with a `uint16_t mem_ct` counter. */
#define MPSCHEMA_NARRAY(msg, mem, nested, nested_msg, max)                     \
{                                                                              \
    MPSCHEMA_MEMBER_BASE(msg, mem, NESTED)                                     \
    .sm_max_elems   = (max),                                                   \
    .sm_flags       = SM_HAS_BOTH_OFF,                                         \
    .sm_nested      = (nested),                                                \
    .sm_nested_size = sizeof(MPSCHEMA_MSG_NAME(nested_msg)),                   \
    .sm_ct_off      = offsetof(MPSCHEMA_MSG_NAME(msg), MPSCHEMA_CT_NAME(mem)), \
}

/** Keeps the position of a retired tag so later tags keep their numbers. */
#define MPSCHEMA_MEMBER_OBSOLETE { .sm_tag = MPSCHEMA_TAG_OBSOLETE }

/** Terminates the member list and closes the brace opened by a `*_DEF` macro. */
#define MPSCHEMA_MEMBER_EOF { .sm_tag = MPSCHEMA_TAG_EOF } }

/**
 * Opens a classic (v1) schema whose object type is @p type. Tags start at 0.
 * The struct size is unknown, so v1 schemas declared this way cannot use
 * arena-backed members; use MPSCHEMA_VARLEN_DEF() for that.
 */
#define MPSCHEMA_DEF(type)                                                     \
    .mps_obj_type = (type),                                                    \
    .mps_name     = #type,                                                     \
    .mps_name_len = sizeof(#type) - 1,                                         \
    .mps_members  = {

/** Opens a v1 schema for message @p msg that records the struct size. */
#define MPSCHEMA_VARLEN_DEF(msg, type)                                         \
    .mps_obj_type = (type),                                                    \
    .mps_name     = #msg,                                                      \
    .mps_name_len = sizeof(#msg) - 1,                                          \
    .mps_size     = sizeof(MPSCHEMA_MSG_NAME(msg)),                            \
    .mps_members  = {

/**
 * Opens a v2 schema for message @p msg. Tag 0 is reserved for the object type,
 * which the v2 encoder writes into every message so a receiver can pick the
 * right schema (see env.h). Your own members therefore start at tag 1.
 */
#define MPSCHEMAV2_DEF(msg, type)                                              \
    .mps_size    = sizeof(MPSCHEMA_MSG_NAME(msg)),                             \
    .mps_version = MPSCHEMAV2,                                                 \
    MPSCHEMA_DEF(type)                                                         \
        { .sm_tag = MPSCHEMAV2_OBJTYPE_TAG },

/* clang-format on */

#endif /* MPSCHEMA_SCHEMA_MACROS_H */
