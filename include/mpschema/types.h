/* SPDX-License-Identifier: MIT */
/**
 * @file types.h
 * @brief Core types of the mpschema library: status codes, member types,
 *        schema descriptors and flags.
 *
 * A *schema* (::mpschema_t) describes how a plain C struct maps onto a
 * MessagePack map. Every struct member that should travel on the wire gets a
 * ::mpschema_member_t entry holding its numeric tag, its type, and its offset
 * inside the struct. The encoder walks the schema and emits a compact map of
 * `tag -> value`; the decoder does the reverse.
 *
 * Schemas are normally built with the helper macros in schema_macros.h rather
 * than by filling these structures by hand.
 */
#ifndef MPSCHEMA_TYPES_H
#define MPSCHEMA_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Status codes returned by the library.
 *
 * Every function returning `int` returns ::mpschema_ok (zero) on success and
 * one of the negative values below on failure, unless documented otherwise.
 */
typedef enum {
    mpschema_ok = 0,             /**< Success. */
    mpschema_invalid_param = -1, /**< A required argument was NULL or out of range. */
    mpschema_not_found = -2,     /**< Nothing available (e.g. no complete message yet). */
    mpschema_no_space = -3,      /**< An output buffer or arena was too small. */
    mpschema_no_mem = -4,        /**< A heap allocation failed. */
    mpschema_malformed = -5,     /**< Malformed schema or malformed MessagePack input. */
    mpschema_unknown = -6,       /**< Unexpected internal condition. */
    mpschema_unsupported = -7,   /**< Operation not supported for this schema version. */
    mpschema_io_error = -8,      /**< A socket or user writer callback failed. */
    mpschema_incomplete = -9,    /**< More input bytes are needed to finish parsing. */
    mpschema_closed = -10        /**< The peer closed the connection. */
} mpschema_status_t;

/** Older name of ::mpschema_status_t, kept for source compatibility. */
typedef mpschema_status_t mpschema_env_status_t;

/** Maximum size of a framed message handled by the socket helpers (transport.h). */
#define MPSCHEMA_MAX_MSG_SZ 4000
/** Suggested buffer size for mpschema_print_json(). */
#define MPSCHEMA_MAX_JSON_SZ 1000

/**
 * @brief The C type of a schema member.
 *
 * | Type                       | C member declaration                          |
 * |----------------------------|-----------------------------------------------|
 * | SCHEMA_TAG_BOOL            | `uint8_t`                                     |
 * | SCHEMA_TAG_UINT8..UINT64   | `uint8_t` .. `uint64_t`                       |
 * | SCHEMA_TAG_DOUBLE          | `double`                                      |
 * | SCHEMA_TAG_STRING          | `char name[N]` (fixed, NUL-terminated)        |
 * | SCHEMA_TAG_VARLEN_STRING   | `char *name` (decoded into the arena)         |
 * | SCHEMA_TAG_NESTED          | an embedded struct described by another schema|
 * | SCHEMA_TAG_REFERS          | a pointer to a struct (decoded into the arena)|
 */
typedef enum {
    SCHEMA_TAG_BOOL,
    SCHEMA_TAG_UINT8,
    SCHEMA_TAG_UINT16,
    SCHEMA_TAG_UINT32,
    SCHEMA_TAG_UINT64,
    SCHEMA_TAG_STRING,
    SCHEMA_TAG_VARLEN_STRING,
    SCHEMA_TAG_NESTED,
    SCHEMA_TAG_REFERS,
    SCHEMA_TAG_DOUBLE
} mpschema_tag_type_t;

/** @name Member flags (mpschema_member_t::sm_flags) */
/** @{ */
#define SM_REQUIRED 0x1          /**< Reserved, not enforced. */
#define SM_HAS_DATA_OFF 0x2      /**< sm_data_off is valid. */
#define SM_HAS_CT_OFF 0x4        /**< sm_ct_off is valid (member is an array with a counter). */
#define SM_DEF_SUPPRESSION 0x8   /**< Reserved, not enforced. */
#define SM_RECORD_VALUE_SET 0x10 /**< sm_set_prepend_off points to a "value was decoded" flag. */
/** Both offsets are valid (arrays). */
#define SM_HAS_BOTH_OFF (SM_HAS_DATA_OFF | SM_HAS_CT_OFF)
/** @} */

/** @name Reserved tags */
/** @{ */
#define MPSCHEMA_TAG_EOF 0xFFFF      /**< Terminates a member list. */
#define MPSCHEMA_TAG_OBSOLETE 0xFFFE /**< Placeholder for a retired tag. */
/** @} */

struct mpschema_s;

/**
 * @brief Describes a single struct member.
 */
typedef struct mpschema_member_s {
    uint16_t sm_tag;             /**< Wire tag; must equal the member's index in the schema. */
    const char *sm_name;         /**< Member name (used by export, print and JSON helpers). */
    size_t sm_name_len;          /**< strlen(sm_name). */
    mpschema_tag_type_t sm_type; /**< C type of the member. */
    size_t sm_elem_size;         /**< Capacity of a STRING member, max length of a VARLEN_STRING. */
    size_t sm_max_elems;         /**< 1 for scalars, the array capacity for arrays. */
    uint16_t sm_flags;           /**< Combination of SM_* flags. */
    size_t sm_data_off;          /**< offsetof() the member inside the struct. */
    size_t sm_ct_off;            /**< offsetof() the `uint16_t <member>_ct` counter. */
    size_t sm_set_prepend_off;   /**< offsetof() the `uint8_t set_<member>` flag. */
    const struct mpschema_s *sm_nested; /**< Schema of NESTED / REFERS members. */
    size_t sm_nested_size;              /**< sizeof() the nested struct. */
} mpschema_member_t;

/**
 * @brief A schema: a named list of members terminated by MPSCHEMA_MEMBER_EOF.
 *
 * Invariant: `mps_members[i].sm_tag == i` for every non-obsolete member.
 * Use mpschema_validate() to check it.
 */
typedef struct mpschema_s {
    uint16_t mps_obj_type;           /**< Application-defined object/message type. */
    const char *mps_name;            /**< Human readable schema name. */
    size_t mps_name_len;             /**< strlen(mps_name). */
    size_t mps_size;                 /**< sizeof() the described struct (0 if unknown). */
    uint8_t mps_version;             /**< 0/1 for classic schemas, MPSCHEMAV2 for v2. */
    mpschema_member_t mps_members[]; /**< Members, terminated by an EOF entry. */
} mpschema_t;

/** True if @p tag (a `const mpschema_member_t *`) is the list terminator. */
#define is_mpschema_eof(tag) ((tag)->sm_tag == MPSCHEMA_TAG_EOF)
/** True if @p tag (a `const mpschema_member_t *`) is an obsolete placeholder. */
#define is_mpschema_obsolete(tag) ((tag)->sm_tag == MPSCHEMA_TAG_OBSOLETE)

/** @name Schema v2 */
/** @{ */
#define MPSCHEMAV2 2             /**< Version number of v2 schemas. */
#define MPSCHEMAV2_OBJTYPE_TAG 0 /**< In v2, tag 0 carries the object type. */
/** True if schema @p s embeds its object type on the wire (v2). */
#define MPSCHEMA_HAS_BUILTIN_OBJTYPE(s) ((s)->mps_version >= MPSCHEMAV2)
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* MPSCHEMA_TYPES_H */
